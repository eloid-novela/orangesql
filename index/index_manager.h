#ifndef ORANGESQL_INDEX_MANAGER_H
#define ORANGESQL_INDEX_MANAGER_H

#include "btree.h"
#include "../metadata/catalog.h"
#include "../storage/buffer_pool.h"
#include <unordered_map>
#include <memory>

namespace orangesql {

// Metadados do índice
struct IndexMetadata {
    IndexId id;
    std::string name;
    TableId table_id;
    std::string table_name;
    std::vector<std::string> columns;
    IndexType type;
    bool unique;
    bool primary;
    size_t estimated_size;
    size_t unique_values;
    
    IndexMetadata() : id(0), table_id(0), type(IndexType::BTREE), 
                     unique(false), primary(false), estimated_size(0), unique_values(0) {}
};

// Gerenciador de índices
class IndexManager {
public:
    IndexManager(Catalog* catalog, BufferPool* buffer_pool);
    ~IndexManager();
    
    // Criação e remoção
    Status createIndex(const std::string& name, const std::string& table,
                       const std::vector<std::string>& columns,
                       const IndexConfig& config = IndexConfig());
    
    Status dropIndex(const std::string& name);
    Status dropIndex(IndexId id);
    
    // Acesso a índices
    template<typename K, typename V>
    BTree<K, V>* getIndex(IndexId id);
    
    template<typename K, typename V>
    BTree<K, V>* getIndex(const std::string& name);
    
    // Operações de manutenção
    Status rebuildIndex(IndexId id);
    Status rebuildAllIndexes();
    Status vacuumIndex(IndexId id);
    
    // Estatísticas
    IndexMetadata getIndexMetadata(IndexId id) const;
    std::vector<IndexMetadata> listIndexes() const;
    std::vector<IndexMetadata> listIndexes(const std::string& table) const;
    
    // Cache de índices
    void clearCache();
    size_t getCacheSize() const { return index_cache_.size(); }
    
    // Validação
    bool validateIndex(IndexId id);
    
private:
    Catalog* catalog_;
    BufferPool* buffer_pool_;
    
    // Cache de índices abertos
    struct IndexEntry {
        std::unique_ptr<void> index;
        IndexType type;
        std::chrono::steady_clock::time_point last_used;
    };
    
    std::unordered_map<IndexId, IndexEntry> index_cache_;
    std::unordered_map<std::string, IndexId> name_to_id_;
    
    // Configurações de cache
    static constexpr size_t MAX_CACHED_INDEXES = 100;
    static constexpr size_t CACHE_CLEANUP_INTERVAL = 60; // segundos
    
    // Gerenciamento de cache
    void cacheIndex(IndexId id, std::unique_ptr<void> index, IndexType type);
    void evictIndex();
    void cleanupCache();
    
    // Helpers
    std::string getIndexFileName(IndexId id) const;
    bool loadIndexMetadata(IndexId id, IndexMetadata& metadata);
    void saveIndexMetadata(const IndexMetadata& metadata);
};

} // namespace orangesql

#endif // ORANGESQL_INDEX_MANAGER_H