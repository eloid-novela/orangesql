#ifndef ORANGESQL_TABLE_H
#define ORANGESQL_TABLE_H

#include "page.h"
#include "buffer_pool.h"
#include "../metadata/schema.h"
#include <vector>
#include <memory>

namespace orangesql {

// Opções de scan
struct ScanOptions {
    bool descending;
    size_t limit;
    std::vector<std::pair<std::string, bool>> order_by; // column, asc
    
    ScanOptions() : descending(false), limit(0) {}
};

// Iterador de tabela
class TableIterator {
public:
    TableIterator() : table_(nullptr), current_page_id_(INVALID_PAGE_ID), current_slot_(0) {}
    TableIterator(class Table* table, PageId page_id, uint16_t slot);
    
    // Operadores
    TableIterator& operator++();
    TableIterator operator++(int);
    bool operator==(const TableIterator& other) const;
    bool operator!=(const TableIterator& other) const;
    
    // Acesso
    std::pair<RecordId, std::vector<Value>> operator*() const;
    RecordId getRecordId() const;
    
private:
    class Table* table_;
    PageId current_page_id_;
    uint16_t current_slot_;
    mutable Page* current_page_;
    
    void advanceToNextValid();
    void loadCurrentPage() const;
};

// Tabela
class Table {
public:
    Table(const TableSchema& schema, BufferPool* buffer_pool);
    ~Table() = default;
    
    // Operações CRUD
    Status insertRecord(const std::vector<Value>& values, RecordId& rid);
    Status getRecord(RecordId rid, std::vector<Value>& values);
    Status updateRecord(RecordId rid, const std::vector<Value>& values);
    Status deleteRecord(RecordId rid);
    
    // Operações em lote
    size_t insertRecords(const std::vector<std::vector<Value>>& records, 
                         std::vector<RecordId>& rids);
    size_t deleteRecords(const std::vector<RecordId>& rids);
    
    // Scans
    TableIterator begin();
    TableIterator end();
    TableIterator find(const std::vector<Value>& key);
    
    // Scan com opções
    std::vector<std::vector<Value>> scan(const ScanOptions& options);
    
    // Estatísticas
    size_t getRecordCount();
    size_t getPageCount();
    
    // Getters
    const TableSchema& getSchema() const { return schema_; }
    TableId getId() const { return schema_.id; }
    const std::string& getName() const { return schema_.name; }
    
    // Validação
    bool validateRecord(const std::vector<Value>& values) const;
    bool validateColumn(size_t col_idx, const Value& value) const;
    
    // Schema
    size_t getColumnIndex(const std::string& name) const;
    DataType getColumnType(size_t idx) const;
    
    // Debug
    void dump() const;
    
private:
    TableSchema schema_;
    BufferPool* buffer_pool_;
    
    // Cache da primeira página
    Page* first_page_;
    Page* last_page_;
    
    // Estatísticas
    mutable std::atomic<size_t> cached_record_count_;
    mutable std::atomic<size_t> cached_page_count_;
    mutable std::chrono::steady_clock::time_point stats_update_time_;
    
    // Métodos internos
    Page* getPage(PageId page_id);
    Page* getOrCreateFirstPage();
    Page* appendNewPage();
    
    friend class TableIterator;
};

} // namespace orangesql

#endif // ORANGESQL_TABLE_H