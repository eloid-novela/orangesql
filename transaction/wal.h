#ifndef ORANGESQL_WAL_H
#define ORANGESQL_WAL_H

#include "log_manager.h"
#include "../storage/buffer_pool.h"

namespace orangesql {

// Gerenciador de Write-Ahead Logging
class WALManager {
public:
    WALManager(LogManager* log_manager, BufferPool* buffer_pool);
    ~WALManager();
    
    // Operações com logging
    LogSequenceNumber logInsert(TransactionId tx_id, PageId page_id,
                                const std::vector<Value>& record, RecordId rid);
    
    LogSequenceNumber logUpdate(TransactionId tx_id, PageId page_id,
                                const std::vector<Value>& old_record,
                                const std::vector<Value>& new_record,
                                RecordId rid);
    
    LogSequenceNumber logDelete(TransactionId tx_id, PageId page_id,
                                const std::vector<Value>& record, RecordId rid);
    
    // Regras WAL
    void beforePageWrite(PageId page_id, LogSequenceNumber lsn);
    bool isPageDurable(PageId page_id) const;
    
    // Flush
    void flush(LogSequenceNumber lsn);
    void flushAll();
    
    // Recovery
    Status recover();
    
private:
    LogManager* log_manager_;
    BufferPool* buffer_pool_;
    
    // Páginas que já foram flushed
    std::unordered_map<PageId, LogSequenceNumber> durable_pages_;
    mutable std::mutex mutex_;
    
    // Serialização de registros para log
    std::vector<char> serializeRecord(const std::vector<Value>& record);
    std::vector<Value> deserializeRecord(const char* data, size_t len);
};

} // namespace orangesql

#endif // ORANGESQL_WAL_H