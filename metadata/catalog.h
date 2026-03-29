#ifndef ORANGESQL_CATALOG_H
#define ORANGESQL_CATALOG_H

#include "../include/types.h"
#include "../include/constants.h"
#include <unordered_map>
#include <memory>
#include <shared_mutex>

namespace orangesql {

// Tipos de objetos do catálogo
enum class CatalogObjectType {
    TABLE,
    INDEX,
    VIEW,
    SEQUENCE,
    FUNCTION,
    TRIGGER,
    CONSTRAINT
};

// Informações de constraint
struct ConstraintInfo {
    enum class Type {
        PRIMARY_KEY,
        FOREIGN_KEY,
        UNIQUE,
        CHECK,
        NOT_NULL
    };
    
    Type type;
    std::string name;
    std::string table_name;
    std::vector<std::string> columns;
    std::string ref_table;
    std::vector<std::string> ref_columns;
    std::string check_expr;
    bool deferrable;
    bool initially_deferred;
    
    ConstraintInfo() : type(Type::NOT_NULL), deferrable(false), initially_deferred(false) {}
};

// Informações de sequência
struct SequenceInfo {
    std::string name;
    int64_t start_value;
    int64_t current_value;
    int64_t increment;
    int64_t min_value;
    int64_t max_value;
    bool cycle;
    
    SequenceInfo() : start_value(1), current_value(1), increment(1),
                    min_value(1), max_value(9223372036854775807), cycle(false) {}
};

// Informações de visão
struct ViewInfo {
    std::string name;
    std::string query;
    std::vector<std::string> columns;
    bool materialized;
    std::chrono::system_clock::time_point last_refresh;
    
    ViewInfo() : materialized(false) {}
};

// Estatísticas do catálogo
struct CatalogStats {
    size_t table_count;
    size_t index_count;
    size_t view_count;
    size_t sequence_count;
    size_t function_count;
    size_t trigger_count;
    size_t cache_hits;
    size_t cache_misses;
    
    CatalogStats() : table_count(0), index_count(0), view_count(0),
                    sequence_count(0), function_count(0), trigger_count(0),
                    cache_hits(0), cache_misses(0) {}
};

// Catálogo do sistema (thread-safe)
class Catalog {
public:
    Catalog();
    ~Catalog();
    
    // Inicialização
    bool init();
    void shutdown();
    
    // Operações com tabelas
    Status createTable(const std::string& name, const TableSchema& schema);
    Status dropTable(const std::string& name);
    Status renameTable(const std::string& old_name, const std::string& new_name);
    
    TableSchema* getTable(const std::string& name);
    const TableSchema* getTable(const std::string& name) const;
    TableSchema* getTableById(TableId id);
    const TableSchema* getTableById(TableId id) const;
    
    std::vector<std::string> listTables() const;
    std::vector<TableSchema> getAllTables() const;
    
    // Operações com índices
    Status createIndex(const std::string& name, const std::string& table,
                       const std::vector<std::string>& columns, bool unique);
    Status dropIndex(const std::string& name);
    Status dropIndex(IndexId id);
    
    IndexId getIndexId(const std::string& name) const;
    std::string getIndexName(IndexId id) const;
    std::vector<std::string> listIndexes(const std::string& table) const;
    
    // Operações com constraints
    Status addConstraint(const std::string& table, const ConstraintInfo& constraint);
    Status dropConstraint(const std::string& table, const std::string& name);
    std::vector<ConstraintInfo> getConstraints(const std::string& table) const;
    
    // Operações com sequências
    Status createSequence(const SequenceInfo& sequence);
    Status dropSequence(const std::string& name);
    int64_t nextValue(const std::string& name);
    SequenceInfo* getSequence(const std::string& name);
    
    // Operações com visões
    Status createView(const ViewInfo& view);
    Status dropView(const std::string& name);
    ViewInfo* getView(const std::string& name);
    std::vector<std::string> listViews() const;
    
    // Geração de IDs
    TableId getNextTableId();
    IndexId getNextIndexId();
    
    // Cache e otimização
    void clearCache();
    void invalidateTable(const std::string& name);
    
    // Persistência
    Status load();
    Status save();
    
    // Estatísticas
    const CatalogStats& getStats() const { return stats_; }
    void resetStats();
    
    // Validação
    bool tableExists(const std::string& name) const;
    bool indexExists(const std::string& name) const;
    bool sequenceExists(const std::string& name) const;
    bool viewExists(const std::string& name) const;
    
    // Dependências
    std::vector<std::string> getDependencies(const std::string& object_name) const;
    bool canDrop(const std::string& object_name) const;

private:
    // Mapas principais
    std::unordered_map<std::string, TableSchema> tables_;
    std::unordered_map<TableId, std::string> table_id_map_;
    
    std::unordered_map<std::string, IndexId> indexes_;
    std::unordered_map<IndexId, std::string> index_id_map_;
    std::unordered_map<std::string, std::vector<IndexId>> table_indexes_;
    
    std::unordered_map<std::string, ConstraintInfo> constraints_;
    std::unordered_map<std::string, std::vector<std::string>> table_constraints_;
    
    std::unordered_map<std::string, SequenceInfo> sequences_;
    std::unordered_map<std::string, ViewInfo> views_;
    
    // Geração de IDs
    std::atomic<TableId> next_table_id_;
    std::atomic<IndexId> next_index_id_;
    
    // Cache de objetos frequentemente acessados
    struct CacheEntry {
        std::chrono::steady_clock::time_point timestamp;
        std::shared_ptr<void> object;
    };
    
    mutable std::unordered_map<std::string, CacheEntry> object_cache_;
    mutable std::shared_mutex cache_mutex_;
    
    // Estatísticas
    mutable CatalogStats stats_;
    
    // Sincronização
    mutable std::shared_mutex rw_mutex_;
    
    // Métodos internos
    void buildIdMaps();
    void updateCache(const std::string& key, std::shared_ptr<void> obj) const;
    std::shared_ptr<void> getFromCache(const std::string& key) const;
    
    // Serialização
    void serialize(std::ostream& os) const;
    void deserialize(std::istream& is);
    
    // Validação de dependências
    void validateDependencies(const std::string& object_name) const;
    void addDependency(const std::string& dependent, const std::string& depends_on);
    void removeDependencies(const std::string& object_name);
};

} // namespace orangesql

#endif // ORANGESQL_CATALOG_H