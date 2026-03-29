#include "catalog.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <shared_mutex>

namespace orangesql {

Catalog::Catalog() 
    : next_table_id_(1)
    , next_index_id_(1) {
}

Catalog::~Catalog() {
    save();
}

bool Catalog::init() {
    return load() == Status::OK;
}

void Catalog::shutdown() {
    save();
}

Status Catalog::createTable(const std::string& name, const TableSchema& schema) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    if (tables_.find(name) != tables_.end()) {
        return Status::ALREADY_EXISTS;
    }
    
    TableSchema new_schema = schema;
    if (new_schema.id == 0) {
        new_schema.id = getNextTableId();
    }
    
    tables_[name] = new_schema;
    table_id_map_[new_schema.id] = name;
    
    // Criar diretório para a tabela
    std::string table_dir = std::string(DATA_DIR) + "/" + name;
    std::filesystem::create_directories(table_dir);
    
    stats_.table_count++;
    
    return Status::OK;
}

Status Catalog::dropTable(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return Status::NOT_FOUND;
    }
    
    // Verificar dependências
    if (!canDrop(name)) {
        return Status::ERROR;
    }
    
    TableId id = it->second.id;
    
    // Remover índices associados
    auto index_it = table_indexes_.find(name);
    if (index_it != table_indexes_.end()) {
        for (IndexId idx_id : index_it->second) {
            std::string idx_name = index_id_map_[idx_id];
            indexes_.erase(idx_name);
            index_id_map_.erase(idx_id);
        }
        table_indexes_.erase(index_it);
    }
    
    // Remover constraints
    auto const_it = table_constraints_.find(name);
    if (const_it != table_constraints_.end()) {
        for (const auto& const_name : const_it->second) {
            constraints_.erase(const_name);
        }
        table_constraints_.erase(const_it);
    }
    
    // Remover dados da tabela
    tables_.erase(it);
    table_id_map_.erase(id);
    
    // Remover diretório da tabela
    std::string table_dir = std::string(DATA_DIR) + "/" + name;
    std::filesystem::remove_all(table_dir);
    
    stats_.table_count--;
    
    return Status::OK;
}

Status Catalog::renameTable(const std::string& old_name, const std::string& new_name) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = tables_.find(old_name);
    if (it == tables_.end()) {
        return Status::NOT_FOUND;
    }
    
    if (tables_.find(new_name) != tables_.end()) {
        return Status::ALREADY_EXISTS;
    }
    
    // Atualizar nome
    TableSchema schema = it->second;
    schema.name = new_name;
    
    tables_.erase(it);
    tables_[new_name] = schema;
    table_id_map_[schema.id] = new_name;
    
    // Renomear diretório
    std::string old_dir = std::string(DATA_DIR) + "/" + old_name;
    std::string new_dir = std::string(DATA_DIR) + "/" + new_name;
    std::filesystem::rename(old_dir, new_dir);
    
    return Status::OK;
}

TableSchema* Catalog::getTable(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = tables_.find(name);
    if (it != tables_.end()) {
        stats_.cache_hits++;
        return &it->second;
    }
    
    stats_.cache_misses++;
    return nullptr;
}

const TableSchema* Catalog::getTable(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = tables_.find(name);
    if (it != tables_.end()) {
        stats_.cache_hits++;
        return &it->second;
    }
    
    stats_.cache_misses++;
    return nullptr;
}

TableSchema* Catalog::getTableById(TableId id) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = table_id_map_.find(id);
    if (it != table_id_map_.end()) {
        return getTable(it->second);
    }
    
    return nullptr;
}

const TableSchema* Catalog::getTableById(TableId id) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = table_id_map_.find(id);
    if (it != table_id_map_.end()) {
        return getTable(it->second);
    }
    
    return nullptr;
}

std::vector<std::string> Catalog::listTables() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    std::vector<std::string> result;
    for (const auto& [name, _] : tables_) {
        result.push_back(name);
    }
    
    return result;
}

std::vector<TableSchema> Catalog::getAllTables() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    std::vector<TableSchema> result;
    for (const auto& [_, schema] : tables_) {
        result.push_back(schema);
    }
    
    return result;
}

Status Catalog::createIndex(const std::string& name, const std::string& table,
                            const std::vector<std::string>& columns, bool unique) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    if (indexes_.find(name) != indexes_.end()) {
        return Status::ALREADY_EXISTS;
    }
    
    auto table_it = tables_.find(table);
    if (table_it == tables_.end()) {
        return Status::NOT_FOUND;
    }
    
    IndexId id = getNextIndexId();
    indexes_[name] = id;
    index_id_map_[id] = name;
    table_indexes_[table].push_back(id);
    
    // Adicionar referência no schema da tabela
    for (const auto& col : columns) {
        table_it->second.indexes[col] = id;
    }
    
    stats_.index_count++;
    
    return Status::OK;
}

Status Catalog::dropIndex(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = indexes_.find(name);
    if (it == indexes_.end()) {
        return Status::NOT_FOUND;
    }
    
    IndexId id = it->second;
    
    // Remover referência das tabelas
    for (auto& [table_name, schema] : tables_) {
        for (auto col_it = schema.indexes.begin(); col_it != schema.indexes.end();) {
            if (col_it->second == id) {
                col_it = schema.indexes.erase(col_it);
            } else {
                ++col_it;
            }
        }
    }
    
    // Remover da lista de índices da tabela
    for (auto& [table, indexes] : table_indexes_) {
        auto vec_it = std::find(indexes.begin(), indexes.end(), id);
        if (vec_it != indexes.end()) {
            indexes.erase(vec_it);
            break;
        }
    }
    
    indexes_.erase(it);
    index_id_map_.erase(id);
    
    stats_.index_count--;
    
    return Status::OK;
}

Status Catalog::dropIndex(IndexId id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = index_id_map_.find(id);
    if (it == index_id_map_.end()) {
        return Status::NOT_FOUND;
    }
    
    return dropIndex(it->second);
}

IndexId Catalog::getIndexId(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = indexes_.find(name);
    if (it != indexes_.end()) {
        return it->second;
    }
    
    return 0;
}

std::string Catalog::getIndexName(IndexId id) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = index_id_map_.find(id);
    if (it != index_id_map_.end()) {
        return it->second;
    }
    
    return "";
}

std::vector<std::string> Catalog::listIndexes(const std::string& table) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    std::vector<std::string> result;
    
    auto it = table_indexes_.find(table);
    if (it != table_indexes_.end()) {
        for (IndexId id : it->second) {
            result.push_back(getIndexName(id));
        }
    }
    
    return result;
}

Status Catalog::addConstraint(const std::string& table, const ConstraintInfo& constraint) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    if (constraints_.find(constraint.name) != constraints_.end()) {
        return Status::ALREADY_EXISTS;
    }
    
    constraints_[constraint.name] = constraint;
    table_constraints_[table].push_back(constraint.name);
    
    return Status::OK;
}

Status Catalog::dropConstraint(const std::string& table, const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = constraints_.find(name);
    if (it == constraints_.end()) {
        return Status::NOT_FOUND;
    }
    
    constraints_.erase(it);
    
    auto table_it = table_constraints_.find(table);
    if (table_it != table_constraints_.end()) {
        auto vec_it = std::find(table_it->second.begin(), table_it->second.end(), name);
        if (vec_it != table_it->second.end()) {
            table_it->second.erase(vec_it);
        }
    }
    
    return Status::OK;
}

std::vector<ConstraintInfo> Catalog::getConstraints(const std::string& table) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    std::vector<ConstraintInfo> result;
    
    auto it = table_constraints_.find(table);
    if (it != table_constraints_.end()) {
        for (const auto& name : it->second) {
            auto const_it = constraints_.find(name);
            if (const_it != constraints_.end()) {
                result.push_back(const_it->second);
            }
        }
    }
    
    return result;
}

Status Catalog::createSequence(const SequenceInfo& sequence) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    if (sequences_.find(sequence.name) != sequences_.end()) {
        return Status::ALREADY_EXISTS;
    }
    
    sequences_[sequence.name] = sequence;
    stats_.sequence_count++;
    
    return Status::OK;
}

Status Catalog::dropSequence(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = sequences_.find(name);
    if (it == sequences_.end()) {
        return Status::NOT_FOUND;
    }
    
    sequences_.erase(it);
    stats_.sequence_count--;
    
    return Status::OK;
}

int64_t Catalog::nextValue(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = sequences_.find(name);
    if (it == sequences_.end()) {
        return -1;
    }
    
    SequenceInfo& seq = it->second;
    int64_t result = seq.current_value;
    
    // Avançar sequência
    seq.current_value += seq.increment;
    
    // Verificar limites
    if (seq.current_value > seq.max_value) {
        if (seq.cycle) {
            seq.current_value = seq.min_value;
        } else {
            seq.current_value = seq.max_value;
        }
    }
    
    return result;
}

SequenceInfo* Catalog::getSequence(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = sequences_.find(name);
    if (it != sequences_.end()) {
        return &it->second;
    }
    
    return nullptr;
}

Status Catalog::createView(const ViewInfo& view) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    if (views_.find(view.name) != views_.end()) {
        return Status::ALREADY_EXISTS;
    }
    
    views_[view.name] = view;
    stats_.view_count++;
    
    return Status::OK;
}

Status Catalog::dropView(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = views_.find(name);
    if (it == views_.end()) {
        return Status::NOT_FOUND;
    }
    
    views_.erase(it);
    stats_.view_count--;
    
    return Status::OK;
}

ViewInfo* Catalog::getView(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = views_.find(name);
    if (it != views_.end()) {
        return &it->second;
    }
    
    return nullptr;
}

std::vector<std::string> Catalog::listViews() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    std::vector<std::string> result;
    for (const auto& [name, _] : views_) {
        result.push_back(name);
    }
    
    return result;
}

TableId Catalog::getNextTableId() {
    return next_table_id_++;
}

IndexId Catalog::getNextIndexId() {
    return next_index_id_++;
}

void Catalog::clearCache() {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    object_cache_.clear();
}

void Catalog::invalidateTable(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    object_cache_.erase("table:" + name);
}

Status Catalog::load() {
    std::ifstream file(std::string(SYSTEM_DIR) + "/catalog.json");
    if (!file.is_open()) {
        return Status::NOT_FOUND;
    }
    
    try {
        deserialize(file);
    } catch (const std::exception& e) {
        return Status::ERROR;
    }
    
    return Status::OK;
}

Status Catalog::save() {
    std::ofstream file(std::string(SYSTEM_DIR) + "/catalog.json");
    if (!file.is_open()) {
        return Status::IO_ERROR;
    }
    
    serialize(file);
    
    return Status::OK;
}

void Catalog::resetStats() {
    stats_ = CatalogStats();
}

bool Catalog::tableExists(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return tables_.find(name) != tables_.end();
}

bool Catalog::indexExists(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return indexes_.find(name) != indexes_.end();
}

bool Catalog::sequenceExists(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return sequences_.find(name) != sequences_.end();
}

bool Catalog::viewExists(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return views_.find(name) != views_.end();
}

std::vector<std::string> Catalog::getDependencies(const std::string& object_name) const {
    std::vector<std::string> result;
    
    // Verificar dependências de visões
    for (const auto& [view_name, view] : views_) {
        if (view.query.find(object_name) != std::string::npos) {
            result.push_back("VIEW:" + view_name);
        }
    }
    
    // Verificar dependências de constraints
    for (const auto& [const_name, constraint] : constraints_) {
        if (constraint.table_name == object_name ||
            constraint.ref_table == object_name) {
            result.push_back("CONSTRAINT:" + const_name);
        }
    }
    
    // Verificar dependências de índices
    for (const auto& [table, indexes] : table_indexes_) {
        if (table == object_name) {
            for (IndexId id : indexes) {
                result.push_back("INDEX:" + getIndexName(id));
            }
        }
    }
    
    return result;
}

bool Catalog::canDrop(const std::string& object_name) const {
    auto deps = getDependencies(object_name);
    return deps.empty();
}

void Catalog::buildIdMaps() {
    for (const auto& [name, schema] : tables_) {
        table_id_map_[schema.id] = name;
    }
    
    for (const auto& [name, id] : indexes_) {
        index_id_map_[id] = name;
    }
}

void Catalog::updateCache(const std::string& key, std::shared_ptr<void> obj) const {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    CacheEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.object = obj;
    
    object_cache_[key] = entry;
    
    // Limpar cache se muito grande
    if (object_cache_.size() > 1000) {
        auto oldest = object_cache_.begin();
        for (auto it = object_cache_.begin(); it != object_cache_.end(); ++it) {
            if (it->second.timestamp < oldest->second.timestamp) {
                oldest = it;
            }
        }
        object_cache_.erase(oldest);
    }
}

std::shared_ptr<void> Catalog::getFromCache(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    
    auto it = object_cache_.find(key);
    if (it != object_cache_.end()) {
        return it->second.object;
    }
    
    return nullptr;
}

void Catalog::serialize(std::ostream& os) const {
    using json = nlohmann::json;
    
    json j;
    
    // Metadados da versão
    j["version"] = 1;
    j["next_table_id"] = next_table_id_.load();
    j["next_index_id"] = next_index_id_.load();
    
    // Tabelas
    j["tables"] = json::array();
    for (const auto& [name, schema] : tables_) {
        json table_json;
        table_json["name"] = name;
        table_json["id"] = schema.id;
        table_json["columns"] = json::array();
        
        for (const auto& col : schema.columns) {
            json col_json;
            col_json["id"] = col.id;
            col_json["name"] = col.name;
            col_json["type"] = static_cast<int>(col.type);
            col_json["length"] = col.length;
            col_json["nullable"] = col.nullable;
            col_json["is_primary_key"] = col.is_primary_key;
            table_json["columns"].push_back(col_json);
        }
        
        table_json["primary_key"] = schema.primary_key;
        
        j["tables"].push_back(table_json);
    }
    
    // Índices
    j["indexes"] = json::array();
    for (const auto& [name, id] : indexes_) {
        json idx_json;
        idx_json["name"] = name;
        idx_json["id"] = id;
        j["indexes"].push_back(idx_json);
    }
    
    // Constraints
    j["constraints"] = json::array();
    for (const auto& [name, constraint] : constraints_) {
        json const_json;
        const_json["name"] = name;
        const_json["type"] = static_cast<int>(constraint.type);
        const_json["table"] = constraint.table_name;
        const_json["columns"] = constraint.columns;
        const_json["ref_table"] = constraint.ref_table;
        const_json["ref_columns"] = constraint.ref_columns;
        const_json["check_expr"] = constraint.check_expr;
        j["constraints"].push_back(const_json);
    }
    
    // Sequências
    j["sequences"] = json::array();
    for (const auto& [name, seq] : sequences_) {
        json seq_json;
        seq_json["name"] = name;
        seq_json["start"] = seq.start_value;
        seq_json["current"] = seq.current_value;
        seq_json["increment"] = seq.increment;
        seq_json["min"] = seq.min_value;
        seq_json["max"] = seq.max_value;
        seq_json["cycle"] = seq.cycle;
        j["sequences"].push_back(seq_json);
    }
    
    // Views
    j["views"] = json::array();
    for (const auto& [name, view] : views_) {
        json view_json;
        view_json["name"] = name;
        view_json["query"] = view.query;
        view_json["columns"] = view.columns;
        view_json["materialized"] = view.materialized;
        j["views"].push_back(view_json);
    }
    
    os << j.dump(4);
}

void Catalog::deserialize(std::istream& is) {
    using json = nlohmann::json;
    
    json j;
    is >> j;
    
    int version = j["version"];
    if (version != 1) {
        throw std::runtime_error("Unsupported catalog version");
    }
    
    next_table_id_ = j["next_table_id"];
    next_index_id_ = j["next_index_id"];
    
    // Carregar tabelas
    for (const auto& table_json : j["tables"]) {
        TableSchema schema;
        schema.id = table_json["id"];
        schema.name = table_json["name"];
        
        for (const auto& col_json : table_json["columns"]) {
            ColumnSchema col;
            col.id = col_json["id"];
            col.name = col_json["name"];
            col.type = static_cast<DataType>(col_json["type"]);
            col.length = col_json.value("length", 0);
            col.nullable = col_json.value("nullable", true);
            col.is_primary_key = col_json.value("is_primary_key", false);
            schema.columns.push_back(col);
        }
        
        schema.primary_key = table_json["primary_key"].get<std::vector<ColumnId>>();
        
        tables_[schema.name] = schema;
    }
    
    // Carregar índices
    for (const auto& idx_json : j["indexes"]) {
        std::string name = idx_json["name"];
        IndexId id = idx_json["id"];
        indexes_[name] = id;
        index_id_map_[id] = name;
    }
    
    // Carregar constraints
    if (j.contains("constraints")) {
        for (const auto& const_json : j["constraints"]) {
            ConstraintInfo constraint;
            constraint.name = const_json["name"];
            constraint.type = static_cast<ConstraintInfo::Type>(const_json["type"]);
            constraint.table_name = const_json["table"];
            constraint.columns = const_json["columns"].get<std::vector<std::string>>();
            constraint.ref_table = const_json.value("ref_table", "");
            constraint.ref_columns = const_json.value("ref_columns", std::vector<std::string>());
            constraint.check_expr = const_json.value("check_expr", "");
            
            constraints_[constraint.name] = constraint;
            table_constraints_[constraint.table_name].push_back(constraint.name);
        }
    }
    
    // Carregar sequências
    if (j.contains("sequences")) {
        for (const auto& seq_json : j["sequences"]) {
            SequenceInfo seq;
            seq.name = seq_json["name"];
            seq.start_value = seq_json["start"];
            seq.current_value = seq_json["current"];
            seq.increment = seq_json["increment"];
            seq.min_value = seq_json["min"];
            seq.max_value = seq_json["max"];
            seq.cycle = seq_json["cycle"];
            
            sequences_[seq.name] = seq;
        }
    }
    
    // Carregar views
    if (j.contains("views")) {
        for (const auto& view_json : j["views"]) {
            ViewInfo view;
            view.name = view_json["name"];
            view.query = view_json["query"];
            view.columns = view_json["columns"].get<std::vector<std::string>>();
            view.materialized = view_json.value("materialized", false);
            
            views_[view.name] = view;
        }
    }
    
    buildIdMaps();
}

} // namespace orangesql