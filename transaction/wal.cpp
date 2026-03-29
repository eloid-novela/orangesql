#include "wal.h"

namespace orangesql {

WALManager::WALManager(LogManager* log_manager, BufferPool* buffer_pool)
    : log_manager_(log_manager), buffer_pool_(buffer_pool) {
}

WALManager::~WALManager() {
    flushAll();
}

LogSequenceNumber WALManager::logInsert(TransactionId tx_id, PageId page_id,
                                        const std::vector<Value>& record, RecordId rid) {
    auto data = serializeRecord(record);
    
    // Adicionar RID aos dados
    size_t total_len = data.size() + sizeof(rid);
    std::vector<char> full_data(total_len);
    memcpy(full_data.data(), &rid, sizeof(rid));
    memcpy(full_data.data() + sizeof(rid), data.data(), data.size());
    
    return log_manager_->appendLogRecord(LogRecordType::INSERT, tx_id, page_id,
                                        full_data.data(), full_data.size());
}

LogSequenceNumber WALManager::logUpdate(TransactionId tx_id, PageId page_id,
                                        const std::vector<Value>& old_record,
                                        const std::vector<Value>& new_record,
                                        RecordId rid) {
    auto old_data = serializeRecord(old_record);
    auto new_data = serializeRecord(new_record);
    
    // Formato: [RID][old_len][old_data][new_len][new_data]
    size_t total_len = sizeof(rid) + sizeof(size_t) * 2 + 
                      old_data.size() + new_data.size();
    std::vector<char> full_data(total_len);
    
    char* ptr = full_data.data();
    memcpy(ptr, &rid, sizeof(rid));
    ptr += sizeof(rid);
    
    size_t old_len = old_data.size();
    memcpy(ptr, &old_len, sizeof(old_len));
    ptr += sizeof(old_len);
    memcpy(ptr, old_data.data(), old_len);
    ptr += old_len;
    
    size_t new_len = new_data.size();
    memcpy(ptr, &new_len, sizeof(new_len));
    ptr += sizeof(new_len);
    memcpy(ptr, new_data.data(), new_len);
    
    return log_manager_->appendLogRecord(LogRecordType::UPDATE, tx_id, page_id,
                                        full_data.data(), full_data.size());
}

LogSequenceNumber WALManager::logDelete(TransactionId tx_id, PageId page_id,
                                        const std::vector<Value>& record, RecordId rid) {
    auto data = serializeRecord(record);
    
    // Adicionar RID aos dados
    size_t total_len = data.size() + sizeof(rid);
    std::vector<char> full_data(total_len);
    memcpy(full_data.data(), &rid, sizeof(rid));
    memcpy(full_data.data() + sizeof(rid), data.data(), data.size());
    
    return log_manager_->appendLogRecord(LogRecordType::DELETE, tx_id, page_id,
                                        full_data.data(), full_data.size());
}

void WALManager::beforePageWrite(PageId page_id, LogSequenceNumber lsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // WAL: log deve estar no disco antes da página
    log_manager_->flush(lsn);
    durable_pages_[page_id] = lsn;
}

bool WALManager::isPageDurable(PageId page_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return durable_pages_.find(page_id) != durable_pages_.end();
}

void WALManager::flush(LogSequenceNumber lsn) {
    log_manager_->flush(lsn);
}

void WALManager::flushAll() {
    log_manager_->flushAll();
}

Status WALManager::recover() {
    return log_manager_->recover();
}

std::vector<char> WALManager::serializeRecord(const std::vector<Value>& record) {
    // Calcular tamanho total
    size_t total_size = sizeof(uint16_t); // número de campos
    
    for (const auto& val : record) {
        total_size += sizeof(uint8_t); // tipo
        
        switch (val.type) {
            case DataType::INTEGER:
                total_size += sizeof(int32_t);
                break;
            case DataType::BIGINT:
                total_size += sizeof(int64_t);
                break;
            case DataType::BOOLEAN:
                total_size += sizeof(bool);
                break;
            case DataType::VARCHAR:
                total_size += sizeof(uint32_t) + val.str_val.length();
                break;
            default:
                break;
        }
    }
    
    std::vector<char> buffer(total_size);
    char* ptr = buffer.data();
    
    // Número de campos
    uint16_t count = record.size();
    memcpy(ptr, &count, sizeof(count));
    ptr += sizeof(count);
    
    // Campos
    for (const auto& val : record) {
        uint8_t type = static_cast<uint8_t>(val.type);
        memcpy(ptr, &type, sizeof(type));
        ptr += sizeof(type);
        
        switch (val.type) {
            case DataType::INTEGER:
                memcpy(ptr, &val.data.int_val, sizeof(int32_t));
                ptr += sizeof(int32_t);
                break;
                
            case DataType::BIGINT:
                memcpy(ptr, &val.data.bigint_val, sizeof(int64_t));
                ptr += sizeof(int64_t);
                break;
                
            case DataType::BOOLEAN:
                memcpy(ptr, &val.data.bool_val, sizeof(bool));
                ptr += sizeof(bool);
                break;
                
            case DataType::VARCHAR: {
                uint32_t len = val.str_val.length();
                memcpy(ptr, &len, sizeof(len));
                ptr += sizeof(len);
                memcpy(ptr, val.str_val.c_str(), len);
                ptr += len;
                break;
            }
            
            default:
                break;
        }
    }
    
    return buffer;
}

std::vector<Value> WALManager::deserializeRecord(const char* data, size_t len) {
    std::vector<Value> result;
    const char* ptr = data;
    const char* end = data + len;
    
    // Número de campos
    if (ptr + sizeof(uint16_t) > end) return result;
    uint16_t count;
    memcpy(&count, ptr, sizeof(count));
    ptr += sizeof(count);
    
    // Campos
    for (uint16_t i = 0; i < count && ptr < end; i++) {
        if (ptr + sizeof(uint8_t) > end) break;
        
        uint8_t type_byte;
        memcpy(&type_byte, ptr, sizeof(type_byte));
        ptr += sizeof(type_byte);
        
        DataType type = static_cast<DataType>(type_byte);
        Value val;
        val.type = type;
        
        switch (type) {
            case DataType::INTEGER:
                if (ptr + sizeof(int32_t) <= end) {
                    memcpy(&val.data.int_val, ptr, sizeof(int32_t));
                    ptr += sizeof(int32_t);
                }
                break;
                
            case DataType::BIGINT:
                if (ptr + sizeof(int64_t) <= end) {
                    memcpy(&val.data.bigint_val, ptr, sizeof(int64_t));
                    ptr += sizeof(int64_t);
                }
                break;
                
            case DataType::BOOLEAN:
                if (ptr + sizeof(bool) <= end) {
                    memcpy(&val.data.bool_val, ptr, sizeof(bool));
                    ptr += sizeof(bool);
                }
                break;
                
            case DataType::VARCHAR: {
                if (ptr + sizeof(uint32_t) > end) break;
                uint32_t str_len;
                memcpy(&str_len, ptr, sizeof(str_len));
                ptr += sizeof(str_len);
                
                if (ptr + str_len <= end) {
                    val.str_val.assign(ptr, str_len);
                    ptr += str_len;
                }
                break;
            }
            
            default:
                break;
        }
        
        result.push_back(val);
    }
    
    return result;
}

} // namespace orangesql