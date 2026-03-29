#include "table.h"
#include <algorithm>

namespace orangesql {

Table::Table(const TableSchema& schema, BufferPool* buffer_pool)
    : schema_(schema)
    , buffer_pool_(buffer_pool)
    , first_page_(nullptr)
    , last_page_(nullptr)
    , cached_record_count_(0)
    , cached_page_count_(0) {
    
    // Carregar primeira página
    first_page_ = buffer_pool_->fetchPage(schema_.id, 0);
    if (!first_page_) {
        // Criar primeira página
        first_page_ = buffer_pool_->createNewPage(schema_.id);
        last_page_ = first_page_;
    } else {
        // Encontrar última página
        Page* page = first_page_;
        while (page->getNextPage() != INVALID_PAGE_ID) {
            page = buffer_pool_->fetchPage(schema_.id, page->getNextPage());
        }
        last_page_ = page;
    }
}

Status Table::insertRecord(const std::vector<Value>& values, RecordId& rid) {
    // Validar valores
    if (!validateRecord(values)) {
        return Status::ERROR;
    }
    
    // Tentar inserir na última página
    if (last_page_->insertRecord(values, rid)) {
        buffer_pool_->unpinPage(last_page_->getPageId(), true);
        cached_record_count_++;
        return Status::OK;
    }
    
    // Página cheia, criar nova
    Page* new_page = appendNewPage();
    if (!new_page) {
        return Status::ERROR;
    }
    
    // Inserir na nova página
    if (!new_page->insertRecord(values, rid)) {
        return Status::ERROR;
    }
    
    cached_record_count_++;
    return Status::OK;
}

Status Table::getRecord(RecordId rid, std::vector<Value>& values) {
    PageId page_id = rid >> 32;
    Page* page = getPage(page_id);
    
    if (!page || !page->getRecord(rid, values)) {
        return Status::NOT_FOUND;
    }
    
    return Status::OK;
}

Status Table::updateRecord(RecordId rid, const std::vector<Value>& values) {
    // Validar valores
    if (!validateRecord(values)) {
        return Status::ERROR;
    }
    
    PageId page_id = rid >> 32;
    Page* page = getPage(page_id);
    
    if (!page || !page->updateRecord(rid, values)) {
        return Status::NOT_FOUND;
    }
    
    return Status::OK;
}

Status Table::deleteRecord(RecordId rid) {
    PageId page_id = rid >> 32;
    Page* page = getPage(page_id);
    
    if (!page || !page->deleteRecord(rid)) {
        return Status::NOT_FOUND;
    }
    
    cached_record_count_--;
    return Status::OK;
}

size_t Table::insertRecords(const std::vector<std::vector<Value>>& records, 
                            std::vector<RecordId>& rids) {
    size_t inserted = 0;
    for (const auto& record : records) {
        RecordId rid;
        if (insertRecord(record, rid) == Status::OK) {
            rids.push_back(rid);
            inserted++;
        } else {
            break;
        }
    }
    return inserted;
}

size_t Table::deleteRecords(const std::vector<RecordId>& rids) {
    size_t deleted = 0;
    for (auto rid : rids) {
        if (deleteRecord(rid) == Status::OK) {
            deleted++;
        }
    }
    return deleted;
}

TableIterator Table::begin() {
    return TableIterator(this, first_page_->getPageId(), 0);
}

TableIterator Table::end() {
    return TableIterator(this, INVALID_PAGE_ID, 0);
}

TableIterator Table::find(const std::vector<Value>& key) {
    // Busca linear - será substituída por índice
    for (auto it = begin(); it != end(); ++it) {
        auto [rid, values] = *it;
        
        // Comparar com a chave (primeira coluna por enquanto)
        if (values.size() > 0 && values[0].data.int_val == key[0].data.int_val) {
            return it;
        }
    }
    
    return end();
}

std::vector<std::vector<Value>> Table::scan(const ScanOptions& options) {
    std::vector<std::vector<Value>> results;
    
    for (auto it = begin(); it != end(); ++it) {
        auto [rid, values] = *it;
        results.push_back(values);
        
        if (options.limit > 0 && results.size() >= options.limit) {
            break;
        }
    }
    
    // Ordenar se necessário
    if (!options.order_by.empty()) {
        std::sort(results.begin(), results.end(),
            [this, &options](const std::vector<Value>& a, const std::vector<Value>& b) {
                for (const auto& [col, asc] : options.order_by) {
                    size_t idx = getColumnIndex(col);
                    if (idx >= a.size() || idx >= b.size()) continue;
                    
                    // Comparação simples (apenas inteiros por enquanto)
                    if (a[idx].data.int_val < b[idx].data.int_val) return asc;
                    if (a[idx].data.int_val > b[idx].data.int_val) return !asc;
                }
                return false;
            });
    }
    
    return results;
}

size_t Table::getRecordCount() {
    // Se cache expirou, recalcular
    auto now = std::chrono::steady_clock::now();
    if (now - stats_update_time_ > std::chrono::seconds(5)) {
        cached_record_count_ = 0;
        
        for (auto it = begin(); it != end(); ++it) {
            cached_record_count_++;
        }
        
        stats_update_time_ = now;
    }
    
    return cached_record_count_;
}

size_t Table::getPageCount() {
    size_t count = 0;
    Page* page = first_page_;
    
    while (page) {
        count++;
        page = getPage(page->getNextPage());
    }
    
    return count;
}

bool Table::validateRecord(const std::vector<Value>& values) const {
    if (values.size() != schema_.columns.size()) {
        return false;
    }
    
    for (size_t i = 0; i < values.size(); i++) {
        if (!validateColumn(i, values[i])) {
            return false;
        }
    }
    
    return true;
}

bool Table::validateColumn(size_t col_idx, const Value& value) const {
    if (col_idx >= schema_.columns.size()) {
        return false;
    }
    
    const auto& col = schema_.columns[col_idx];
    
    // Verificar tipo
    if (value.type != col.type) {
        return false;
    }
    
    // Verificar nullabilidade
    if (!col.nullable && value.type == DataType::INTEGER && 
        value.data.int_val == 0 && value.str_val.empty()) {
        // TODO: Melhor verificação de NULL
        return false;
    }
    
    return true;
}

size_t Table::getColumnIndex(const std::string& name) const {
    for (size_t i = 0; i < schema_.columns.size(); i++) {
        if (schema_.columns[i].name == name) {
            return i;
        }
    }
    return -1;
}

DataType Table::getColumnType(size_t idx) const {
    if (idx < schema_.columns.size()) {
        return schema_.columns[idx].type;
    }
    return DataType::INTEGER;
}

Page* Table::getPage(PageId page_id) {
    return buffer_pool_->fetchPage(schema_.id, page_id);
}

Page* Table::getOrCreateFirstPage() {
    if (!first_page_) {
        first_page_ = buffer_pool_->createNewPage(schema_.id);
    }
    return first_page_;
}

Page* Table::appendNewPage() {
    Page* new_page = buffer_pool_->createNewPage(schema_.id);
    
    if (new_page) {
        // Linkar com a última página
        if (last_page_) {
            last_page_->setNextPage(new_page->getPageId());
            buffer_pool_->unpinPage(last_page_->getPageId(), true);
        }
        
        new_page->setPrevPage(last_page_ ? last_page_->getPageId() : INVALID_PAGE_ID);
        last_page_ = new_page;
        
        if (!first_page_) {
            first_page_ = new_page;
        }
    }
    
    return new_page;
}

void Table::dump() const {
    std::cout << "Table " << schema_.name << ":\n";
    std::cout << "  ID: " << schema_.id << "\n";
    std::cout << "  Columns: " << schema_.columns.size() << "\n";
    for (const auto& col : schema_.columns) {
        std::cout << "    " << col.name << ": " 
                  << static_cast<int>(col.type) << "\n";
    }
    
    // Contar registros
    size_t count = 0;
    for (auto it = const_cast<Table*>(this)->begin(); 
         it != const_cast<Table*>(this)->end(); ++it) {
        count++;
    }
    std::cout << "  Records: " << count << "\n";
}

// ============================================
// TableIterator
// ============================================

TableIterator::TableIterator(Table* table, PageId page_id, uint16_t slot)
    : table_(table)
    , current_page_id_(page_id)
    , current_slot_(slot)
    , current_page_(nullptr) {
    
    if (page_id != INVALID_PAGE_ID) {
        loadCurrentPage();
        if (current_page_ && current_slot_ >= current_page_->getRecordCount()) {
            advanceToNextValid();
        }
    }
}

TableIterator& TableIterator::operator++() {
    if (current_page_id_ != INVALID_PAGE_ID) {
        current_slot_++;
        advanceToNextValid();
    }
    return *this;
}

TableIterator TableIterator::operator++(int) {
    TableIterator tmp = *this;
    ++(*this);
    return tmp;
}

bool TableIterator::operator==(const TableIterator& other) const {
    return current_page_id_ == other.current_page_id_ && 
           current_slot_ == other.current_slot_;
}

bool TableIterator::operator!=(const TableIterator& other) const {
    return !(*this == other);
}

std::pair<RecordId, std::vector<Value>> TableIterator::operator*() const {
    RecordId rid = (static_cast<RecordId>(current_page_id_) << 32) | current_slot_;
    std::vector<Value> values;
    
    if (current_page_) {
        current_page_->getRecord(rid, values);
    }
    
    return {rid, values};
}

RecordId TableIterator::getRecordId() const {
    return (static_cast<RecordId>(current_page_id_) << 32) | current_slot_;
}

void TableIterator::advanceToNextValid() {
    while (current_page_id_ != INVALID_PAGE_ID) {
        if (current_page_ && current_slot_ < current_page_->getRecordCount()) {
            return;
        }
        
        // Avançar para próxima página
        if (current_page_) {
            current_page_id_ = current_page_->getNextPage();
        } else {
            current_page_id_ = INVALID_PAGE_ID;
        }
        
        if (current_page_id_ != INVALID_PAGE_ID) {
            loadCurrentPage();
            current_slot_ = 0;
        }
    }
}

void TableIterator::loadCurrentPage() const {
    if (table_ && current_page_id_ != INVALID_PAGE_ID) {
        current_page_ = table_->buffer_pool_->fetchPage(
            table_->schema_.id, current_page_id_);
    }
}

} // namespace orangesql