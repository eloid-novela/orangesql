#include "index_manager.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace orangesql {

IndexManager::IndexManager(Catalog* catalog, BufferPool* buffer_pool)
    : catalog_(catalog), buffer_pool_(buffer_pool) {
    
    // Carregar metadados dos índices
    std::ifstream file(SYSTEM_DIR + std::string("/indexes.json"));
    if (file.is_open()) {
        nlohmann::json j;
        file >> j;
        
        for (const auto& item : j) {
            IndexMetadata meta;
            meta.id = item["id"];
            meta.name = item["name"];
            meta.table_id = item["table_id"];
            meta.table_name = item["table_name"];
            meta.columns = item["columns"].get<std::vector<std::string>>();
            meta.type = static_cast<IndexType>(item["type"]);
            meta.unique = item["unique"];
            meta.primary = item["primary"];
            
            name_to_id_[meta.name] = meta.id;
        }
    }
}

IndexManager::~IndexManager() {
    // Salvar metadados
    nlohmann::json j = nlohmann::json::array();
    
    for (const auto& [name, id] : name_to_id_) {
        IndexMetadata meta = getIndexMetadata(id);
        nlohmann::json item;
        item["id"] = meta.id;
        item["name"] = meta.name;
        item["table_id"] = meta.table_id;
        item["table_name"] = meta.table_name;
        item["columns"] = meta.columns;
        item["type"] = static_cast<int>(meta.type);
        item["unique"] = meta.unique;
        item["primary"] = meta.primary;
        j.push_back(item);
    }
    
    std::ofstream file(SYSTEM_DIR + std::string("/indexes.json"));
    file << j.dump(4);
    
    // Liberar cache
    clearCache();
}

Status IndexManager::createIndex(const std::string& name, const std::string& table,
                                  const std::vector<std::string>& columns,
                                  const IndexConfig& config) {
    // Verificar se já existe
    if (name_to_id_.find(name) != name_to_id_.end()) {
        return Status::ALREADY_EXISTS;
    }
    
    // Obter schema da tabela
    auto* table_schema = catalog_->getTable(table);
    if (!table_schema) {
        return Status::NOT_FOUND;
    }
    
    // Criar metadados
    IndexMetadata meta;
    meta.id = catalog_->getNextIndexId();
    meta.name = name;
    meta.table_id = table_schema->id;
    meta.table_name = table;
    meta.columns = columns;
    meta.type = IndexType::BTREE;
    meta.unique = config.unique;
    meta.primary = false;
    
    // Criar arquivo do índice
    std::string filename = getIndexFileName(meta.id);
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        return Status::IO_ERROR;
    }
    
    // Inicializar árvore
    if (columns.size() == 1) {
        // Índice simples
        auto* table_obj = new Table(*table_schema, buffer_pool_); // TODO: Gerenciar lifecycle
        
        // Determinar tipo da coluna
        size_t col_idx = table_schema->getColumnIndex(columns[0]);
        DataType col_type = table_schema->columns[col_idx].type;
        
        // Criar índice apropriado
        std::unique_ptr<void> index;
        
        switch (col_type) {
            case DataType::INTEGER:
                index = std::make_unique<IntIndex>(meta.id, buffer_pool_, config);
                break;
            case DataType::BIGINT:
                index = std::make_unique<LongIndex>(meta.id, buffer_pool_, config);
                break;
            case DataType::VARCHAR:
                index = std::make_unique<StringIndex>(meta.id, buffer_pool_, config);
                break;
            default:
                return Status::ERROR;
        }
        
        // Popular índice com dados existentes
        for (auto it = table_obj->begin(); it != table_obj->end(); ++it) {
            auto [rid, row] = *it;
            
            Value key = row[col_idx];
            RecordId value = rid;
            
            switch (col_type) {
                case DataType::INTEGER:
                    static_cast<IntIndex*>(index.get())->insert(key.data.int_val, value);
                    break;
                case DataType::BIGINT:
                    static_cast<LongIndex*>(index.get())->insert(key.data.bigint_val, value);
                    break;
                case DataType::VARCHAR:
                    static_cast<StringIndex*>(index.get())->insert(key.str_val, value);
                    break;
                default:
                    break;
            }
        }
        
        delete table_obj;
        
        // Cache do índice
        cacheIndex(meta.id, std::move(index), IndexType::BTREE);
    }
    
    // Salvar metadados
    name_to_id_[name] = meta.id;
    saveIndexMetadata(meta);
    
    return Status::OK;
}

Status IndexManager::dropIndex(const std::string& name) {
    auto it = name_to_id_.find(name);
    if (it == name_to_id_.end()) {
        return Status::NOT_FOUND;
    }
    
    return dropIndex(it->second);
}

Status IndexManager::dropIndex(IndexId id) {
    // Remover do cache
    index_cache_.erase(id);
    
    // Remover dos metadados
    for (auto it = name_to_id_.begin(); it != name_to_id_.end(); ++it) {
        if (it->second == id) {
            name_to_id_.erase(it);
            break;
        }
    }
    
    // Remover arquivo
    std::string filename = getIndexFileName(id);
    std::remove(filename.c_str());
    
    return Status::OK;
}

template<typename K, typename V>
BTree<K, V>* IndexManager::getIndex(IndexId id) {
    auto it = index_cache_.find(id);
    
    if (it != index_cache_.end()) {
        // Atualizar timestamp
        it->second.last_used = std::chrono::steady_clock::now();
        
        // Retornar índice
        if (it->second.type == IndexType::BTREE) {
            return static_cast<BTree<K, V>*>(it->second.index.get());
        }
    }
    
    // Carregar do disco
    IndexMetadata meta = getIndexMetadata(id);
    
    // Criar índice baseado no tipo da coluna
    std::unique_ptr<void> index;
    
    if (typeid(K) == typeid(int32_t)) {
        index = std::make_unique<BTree<int32_t, V>>(id, buffer_pool_);
    } else if (typeid(K) == typeid(int64_t)) {
        index = std::make_unique<BTree<int64_t, V>>(id, buffer_pool_);
    } else if (typeid(K) == typeid(std::string)) {
        index = std::make_unique<BTree<std::string, V>>(id, buffer_pool_);
    } else {
        return nullptr;
    }
    
    // Cache do índice
    auto* ptr = static_cast<BTree<K, V>*>(index.get());
    cacheIndex(id, std::move(index), IndexType::BTREE);
    
    return ptr;
}

template<typename K, typename V>
BTree<K, V>* IndexManager::getIndex(const std::string& name) {
    auto it = name_to_id_.find(name);
    if (it == name_to_id_.end()) {
        return nullptr;
    }
    
    return getIndex<K, V>(it->second);
}

Status IndexManager::rebuildIndex(IndexId id) {
    auto* table = catalog_->getTableById(getIndexMetadata(id).table_id);
    if (!table) {
        return Status::NOT_FOUND;
    }
    
    // TODO: Reconstruir índice
    return Status::OK;
}

Status IndexManager::rebuildAllIndexes() {
    for (const auto& [name, id] : name_to_id_) {
        Status status = rebuildIndex(id);
        if (status != Status::OK) {
            return status;
        }
    }
    return Status::OK;
}

Status IndexManager::vacuumIndex(IndexId id) {
    auto it = index_cache_.find(id);
    if (it != index_cache_.end()) {
        if (it->second.type == IndexType::BTREE) {
            // TODO: Chamar vacuum específico do tipo
        }
    }
    return Status::OK;
}

IndexMetadata IndexManager::getIndexMetadata(IndexId id) const {
    IndexMetadata meta;
    
    // TODO: Carregar de arquivo de metadados
    for (const auto& [name, idx_id] : name_to_id_) {
        if (idx_id == id) {
            meta.id = id;
            meta.name = name;
            // Carregar outros campos
            break;
        }
    }
    
    return meta;
}

std::vector<IndexMetadata> IndexManager::listIndexes() const {
    std::vector<IndexMetadata> result;
    
    for (const auto& [name, id] : name_to_id_) {
        result.push_back(getIndexMetadata(id));
    }
    
    return result;
}

std::vector<IndexMetadata> IndexManager::listIndexes(const std::string& table) const {
    std::vector<IndexMetadata> result;
    
    for (const auto& [name, id] : name_to_id_) {
        auto meta = getIndexMetadata(id);
        if (meta.table_name == table) {
            result.push_back(meta);
        }
    }
    
    return result;
}

void IndexManager::clearCache() {
    index_cache_.clear();
}

bool IndexManager::validateIndex(IndexId id) {
    auto it = index_cache_.find(id);
    if (it != index_cache_.end()) {
        if (it->second.type == IndexType::BTREE) {
            // TODO: Chamar validate específico
            return true;
        }
    }
    return false;
}

void IndexManager::cacheIndex(IndexId id, std::unique_ptr<void> index, IndexType type) {
    // Limpar cache se necessário
    if (index_cache_.size() >= MAX_CACHED_INDEXES) {
        evictIndex();
    }
    
    IndexEntry entry;
    entry.index = std::move(index);
    entry.type = type;
    entry.last_used = std::chrono::steady_clock::now();
    
    index_cache_[id] = std::move(entry);
}

void IndexManager::evictIndex() {
    // Encontrar índice menos recentemente usado
    IndexId oldest_id = 0;
    auto oldest_time = std::chrono::steady_clock::now();
    
    for (const auto& [id, entry] : index_cache_) {
        if (entry.last_used < oldest_time) {
            oldest_time = entry.last_used;
            oldest_id = id;
        }
    }
    
    if (oldest_id != 0) {
        index_cache_.erase(oldest_id);
    }
}

void IndexManager::cleanupCache() {
    // Remover índices não usados por muito tempo
    auto now = std::chrono::steady_clock::now();
    auto threshold = now - std::chrono::seconds(CACHE_CLEANUP_INTERVAL);
    
    for (auto it = index_cache_.begin(); it != index_cache_.end();) {
        if (it->second.last_used < threshold) {
            it = index_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string IndexManager::getIndexFileName(IndexId id) const {
    return std::string(DATA_DIR) + "/index_" + std::to_string(id) + ".idx";
}

bool IndexManager::loadIndexMetadata(IndexId id, IndexMetadata& metadata) {
    std::ifstream file(getIndexFileName(id) + ".meta");
    if (!file.is_open()) {
        return false;
    }
    
    nlohmann::json j;
    file >> j;
    
    metadata.id = j["id"];
    metadata.name = j["name"];
    metadata.table_id = j["table_id"];
    metadata.table_name = j["table_name"];
    metadata.columns = j["columns"].get<std::vector<std::string>>();
    metadata.type = static_cast<IndexType>(j["type"]);
    metadata.unique = j["unique"];
    metadata.primary = j["primary"];
    
    return true;
}

void IndexManager::saveIndexMetadata(const IndexMetadata& metadata) {
    nlohmann::json j;
    j["id"] = metadata.id;
    j["name"] = metadata.name;
    j["table_id"] = metadata.table_id;
    j["table_name"] = metadata.table_name;
    j["columns"] = metadata.columns;
    j["type"] = static_cast<int>(metadata.type);
    j["unique"] = metadata.unique;
    j["primary"] = metadata.primary;
    
    std::ofstream file(getIndexFileName(metadata.id) + ".meta");
    file << j.dump(4);
}

// Instanciação explícita para tipos comuns
template IntIndex* IndexManager::getIndex<int32_t, RecordId>(IndexId);
template LongIndex* IndexManager::getIndex<int64_t, RecordId>(IndexId);
template StringIndex* IndexManager::getIndex<std::string, RecordId>(IndexId);

} // namespace orangesql