#ifndef ORANGESQL_PAGE_H
#define ORANGESQL_PAGE_H

#include "../include/types.h"
#include <cstring>
#include <vector>
#include <memory>

namespace orangesql {

// Tipos de página
enum class PageType {
    DATA_PAGE,
    INDEX_PAGE,
    OVERFLOW_PAGE,
    SYSTEM_PAGE
};

// Cabeçalho da página (64 bytes)
struct PageHeader {
    // Identificação
    PageId page_id;
    TableId table_id;
    PageType type;
    
    // Espaço livre
    uint16_t free_space_offset;
    uint16_t free_space_end;
    uint16_t record_count;
    uint16_t slot_count;
    
    // Links para outras páginas
    PageId next_page_id;
    PageId prev_page_id;
    PageId parent_page_id;
    
    // Estatísticas
    uint32_t checksum;
    uint32_t lsn;  // Log Sequence Number para recovery
    
    // Flags
    bool is_dirty;
    bool is_locked;
    uint8_t padding[5];  // Alinhamento
    
    PageHeader() {
        memset(this, 0, sizeof(PageHeader));
        page_id = INVALID_PAGE_ID;
        table_id = 0;
        type = PageType::DATA_PAGE;
        free_space_offset = sizeof(PageHeader);
        free_space_end = PAGE_SIZE;
        next_page_id = INVALID_PAGE_ID;
        prev_page_id = INVALID_PAGE_ID;
        parent_page_id = INVALID_PAGE_ID;
    }
};

// Slot de registro (8 bytes)
struct PageSlot {
    uint16_t offset;   // Offset dentro da página
    uint16_t length;   // Tamanho do registro
    uint16_t flags;    // Flags (deletado, etc)
    uint16_t next;     // Próximo slot na lista livre
    
    PageSlot() : offset(0), length(0), flags(0), next(0) {}
};

// Página de disco (4KB)
class Page {
public:
    static constexpr size_t HEADER_SIZE = sizeof(PageHeader);
    static constexpr size_t SLOT_SIZE = sizeof(PageSlot);
    static constexpr size_t MAX_SLOTS = (PAGE_SIZE - HEADER_SIZE) / SLOT_SIZE;
    static constexpr size_t DATA_SIZE = PAGE_SIZE - HEADER_SIZE - (MAX_SLOTS * SLOT_SIZE);
    
    Page(PageId id = INVALID_PAGE_ID, TableId table = 0);
    ~Page() = default;
    
    // Operações com registros
    bool insertRecord(const std::vector<Value>& record, RecordId& rid, TransactionId tx_id = 0);
    bool getRecord(RecordId rid, std::vector<Value>& record) const;
    bool updateRecord(RecordId rid, const std::vector<Value>& record, TransactionId tx_id = 0);
    bool deleteRecord(RecordId rid, TransactionId tx_id = 0);
    
    // Operações em lote
    size_t insertRecords(const std::vector<std::vector<Value>>& records, 
                         std::vector<RecordId>& rids);
    void getRecords(std::vector<std::vector<Value>>& records) const;
    size_t deleteRecords(const std::vector<RecordId>& rids);
    
    // Gerenciamento de espaço
    bool hasSpaceFor(size_t record_size) const;
    size_t getFreeSpace() const;
    void compactify();
    
    // Serialização
    void serialize(char* buffer) const;
    void deserialize(const char* buffer);
    
    // Getters/Setters
    PageId getPageId() const { return header_.page_id; }
    TableId getTableId() const { return header_.table_id; }
    PageType getType() const { return header_.type; }
    void setType(PageType type) { header_.type = type; markDirty(); }
    
    PageId getNextPage() const { return header_.next_page_id; }
    void setNextPage(PageId pid) { header_.next_page_id = pid; markDirty(); }
    
    PageId getPrevPage() const { return header_.prev_page_id; }
    void setPrevPage(PageId pid) { header_.prev_page_id = pid; markDirty(); }
    
    uint16_t getRecordCount() const { return header_.record_count; }
    bool isEmpty() const { return header_.record_count == 0; }
    bool isFull() const { return getFreeSpace() < 64; }  // Menos de 64 bytes livres
    
    // Flags
    bool isDirty() const { return header_.is_dirty; }
    void markDirty() { header_.is_dirty = true; }
    void clearDirty() { header_.is_dirty = false; }
    
    bool isLocked() const { return header_.is_locked; }
    void setLocked(bool locked) { header_.is_locked = locked; markDirty(); }
    
    // LSN para recovery
    uint32_t getLSN() const { return header_.lsn; }
    void setLSN(uint32_t lsn) { header_.lsn = lsn; markDirty(); }
    
    // Cálculo de checksum
    uint32_t calculateChecksum() const;
    bool verifyChecksum() const;
    
    // Debug
    void dump() const;
    
private:
    PageHeader header_;
    char data_[PAGE_SIZE - sizeof(PageHeader)];
    
    // Acesso aos slots
    PageSlot* getSlotArray() {
        return reinterpret_cast<PageSlot*>(data_);
    }
    
    const PageSlot* getSlotArray() const {
        return reinterpret_cast<const PageSlot*>(data_);
    }
    
    char* getRecordData(uint16_t offset) {
        return data_ + offset - sizeof(PageHeader);
    }
    
    const char* getRecordData(uint16_t offset) const {
        return data_ + offset - sizeof(PageHeader);
    }
    
    // Serialização de valores
    size_t serializeValue(const Value& val, char* buffer) const;
    size_t deserializeValue(const char* buffer, Value& val) const;
    size_t getValueSize(const Value& val) const;
};

} // namespace orangesql

#endif // ORANGESQL_PAGE_H