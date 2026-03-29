#include "log_manager.h"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>

namespace orangesql {

LogManager::LogManager(FileManager* file_manager)
    : file_manager_(file_manager)
    , mode_(LogMode::NORMAL)
    , max_log_size_(1024 * 1024 * 100) // 100MB
    , sync_interval_ms_(1000)
    , recovery_mode_(false)
    , current_lsn_(1)
    , last_checkpoint_(0)
    , buffer_pos_(0)
    , sync_thread_running_(false) {
    
    write_buffer_.reserve(WRITE_BUFFER_SIZE);
}

LogManager::~LogManager() {
    shutdown();
}

bool LogManager::init() {
    log_filename_ = std::string(LOG_DIR) + "/orangesql.log";
    
    // Abrir arquivo de log
    log_file_.open(log_filename_, std::ios::binary | std::ios::in | std::ios::out);
    
    if (!log_file_.is_open()) {
        // Criar novo arquivo
        log_file_.open(log_filename_, std::ios::binary | std::ios::out);
        if (!log_file_.is_open()) {
            return false;
        }
        
        // Escrever header
        LogFileHeader header;
        log_file_.write(reinterpret_cast<char*>(&header), sizeof(header));
        log_file_.flush();
        log_file_.close();
        
        // Reabrir para leitura/escrita
        log_file_.open(log_filename_, std::ios::binary | std::ios::in | std::ios::out);
    } else {
        // Ler último LSN
        log_file_.seekg(0, std::ios::end);
        size_t file_size = log_file_.tellg();
        
        if (file_size > sizeof(LogFileHeader)) {
            // Encontrar último registro válido
            char buffer[sizeof(LogRecord)];
            size_t pos = sizeof(LogFileHeader);
            
            while (pos + sizeof(LogRecord) <= file_size) {
                log_file_.seekg(pos);
                log_file_.read(buffer, sizeof(LogRecord));
                
                LogRecord record;
                deserializeRecord(buffer, record);
                
                if (record.lsn > 0) {
                    current_lsn_ = record.lsn + 1;
                    log_buffer_[record.lsn] = std::move(record);
                }
                
                pos += sizeof(LogRecord) + record.data_length;
            }
        }
    }
    
    startSyncThread();
    return true;
}

void LogManager::shutdown() {
    stopSyncThread();
    flushAll();
    
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

LogSequenceNumber LogManager::appendLogRecord(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LogSequenceNumber lsn = current_lsn_++;
    
    // Criar cópia do registro
    LogRecord copy = record;
    copy.lsn = lsn;
    copy.timestamp = std::chrono::system_clock::now();
    
    // Manter referência para a mesma transação
    if (last_tx_lsn_.find(record.transaction_id) != last_tx_lsn_.end()) {
        copy.prev_lsn = last_tx_lsn_[record.transaction_id];
    }
    last_tx_lsn_[record.transaction_id] = lsn;
    
    // Adicionar ao buffer em memória
    log_buffer_[lsn] = std::move(copy);
    
    // Escrever no buffer de disco
    if (mode_ != LogMode::NO_LOG) {
        size_t record_size = sizeof(LogRecord) + record.data_length;
        
        if (buffer_pos_ + record_size > WRITE_BUFFER_SIZE) {
            flushBuffer();
        }
        
        char* buffer = write_buffer_.data() + buffer_pos_;
        buffer_pos_ += serializeRecord(log_buffer_[lsn], buffer);
    }
    
    stats_.total_records++;
    stats_.bytes_written += sizeof(LogRecord) + record.data_length;
    
    return lsn;
}

LogSequenceNumber LogManager::appendLogRecord(LogRecordType type, TransactionId tx_id,
                                               PageId page_id, const char* data,
                                               size_t data_len) {
    LogRecord record;
    record.type = type;
    record.transaction_id = tx_id;
    record.page_id = page_id;
    
    if (data && data_len > 0) {
        record.data_length = data_len;
        record.data = std::make_unique<char[]>(data_len);
        memcpy(record.data.get(), data, data_len);
    }
    
    return appendLogRecord(record);
}

LogRecord LogManager::getLogRecord(LogSequenceNumber lsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = log_buffer_.find(lsn);
    if (it != log_buffer_.end()) {
        return it->second;
    }
    
    // Buscar no disco
    std::string filename = getLogFileName(lsn);
    std::ifstream file(filename, std::ios::binary);
    
    if (file.is_open()) {
        file.seekg(sizeof(LogFileHeader) + (lsn * sizeof(LogRecord)));
        
        char buffer[sizeof(LogRecord)];
        file.read(buffer, sizeof(LogRecord));
        
        LogRecord record;
        deserializeRecord(buffer, record);
        
        if (record.data_length > 0) {
            record.data = std::make_unique<char[]>(record.data_length);
            file.read(record.data.get(), record.data_length);
        }
        
        return record;
    }
    
    return LogRecord();
}

std::vector<LogRecord> LogManager::getLogsSince(LogSequenceNumber lsn) {
    std::vector<LogRecord> result;
    
    for (const auto& [log_lsn, record] : log_buffer_) {
        if (log_lsn > lsn) {
            result.push_back(record);
        }
    }
    
    // Ordenar por LSN
    std::sort(result.begin(), result.end(),
              [](const LogRecord& a, const LogRecord& b) {
                  return a.lsn < b.lsn;
              });
    
    return result;
}

std::vector<LogRecord> LogManager::getLogsForTransaction(TransactionId tx_id) {
    std::vector<LogRecord> result;
    
    for (const auto& [lsn, record] : log_buffer_) {
        if (record.transaction_id == tx_id) {
            result.push_back(record);
        }
    }
    
    // Ordenar por LSN
    std::sort(result.begin(), result.end(),
              [](const LogRecord& a, const LogRecord& b) {
                  return a.lsn < b.lsn;
              });
    
    return result;
}

void LogManager::flush(LogSequenceNumber lsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (buffer_pos_ > 0) {
        flushBuffer();
    }
    
    log_file_.flush();
    stats_.flushes++;
}

void LogManager::flushAll() {
    flush(current_lsn_);
}

void LogManager::truncate(LogSequenceNumber lsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Remover registros antigos do buffer
    for (auto it = log_buffer_.begin(); it != log_buffer_.end();) {
        if (it->first < lsn) {
            it = log_buffer_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Truncar arquivo se necessário
    if (lsn > last_checkpoint_) {
        // TODO: Truncar arquivo físico
    }
}

Status LogManager::recover() {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    recovery_mode_ = true;
    
    // 1. Analysis pass - encontrar último checkpoint
    LogSequenceNumber redo_lsn = 0;
    std::unordered_map<TransactionId, TransactionState> tx_states;
    
    for (const auto& [lsn, record] : log_buffer_) {
        switch (record.type) {
            case LogRecordType::BEGIN:
                tx_states[record.transaction_id] = TransactionState::ACTIVE;
                break;
            case LogRecordType::COMMIT:
                tx_states[record.transaction_id] = TransactionState::COMMITTED;
                break;
            case LogRecordType::ABORT:
                tx_states[record.transaction_id] = TransactionState::ABORTED;
                break;
            case LogRecordType::CHECKPOINT:
                redo_lsn = lsn;
                break;
            default:
                break;
        }
    }
    
    // 2. Redo pass (forward)
    for (const auto& [lsn, record] : log_buffer_) {
        if (lsn <= redo_lsn) continue;
        
        auto it = tx_states.find(record.transaction_id);
        if (it != tx_states.end() && it->second == TransactionState::COMMITTED) {
            // TODO: Redo operation
        }
    }
    
    // 3. Undo pass (backward)
    for (auto it = log_buffer_.rbegin(); it != log_buffer_.rend(); ++it) {
        const auto& [lsn, record] = *it;
        
        auto state_it = tx_states.find(record.transaction_id);
        if (state_it != tx_states.end() && state_it->second == TransactionState::ACTIVE) {
            // TODO: Undo operation
        }
    }
    
    recovery_mode_ = false;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    stats_.recovery_time_ms = std::chrono::duration<double, std::milli>
                              (end_time - start_time).count();
    
    return Status::OK;
}

void LogManager::setCheckpoint(LogSequenceNumber lsn) {
    last_checkpoint_ = lsn;
    stats_.checkpoints++;
}

void LogManager::resetStats() {
    stats_ = LogStats();
}

bool LogManager::archiveLog(LogSequenceNumber start, LogSequenceNumber end) {
    std::string archive_name = std::string(LOG_DIR) + "/archive_" +
                               std::to_string(start) + "_" + std::to_string(end) + ".log";
    
    std::ofstream archive(archive_name, std::ios::binary);
    if (!archive.is_open()) {
        return false;
    }
    
    // Copiar registros para arquivo de archive
    for (LogSequenceNumber lsn = start; lsn <= end; lsn++) {
        auto it = log_buffer_.find(lsn);
        if (it != log_buffer_.end()) {
            char buffer[sizeof(LogRecord) + it->second.data_length];
            size_t size = serializeRecord(it->second, buffer);
            archive.write(buffer, size);
        }
    }
    
    archive.close();
    return true;
}

bool LogManager::deleteArchivedLogs(LogSequenceNumber before) {
    // Remover arquivos de log antigos
    for (LogSequenceNumber lsn = 1; lsn < before; lsn += 1000) { // Assumindo 1000 registros por arquivo
        std::string filename = getLogFileName(lsn);
        std::remove(filename.c_str());
    }
    
    return true;
}

void LogManager::startSyncThread() {
    if (mode_ == LogMode::ASYNC) {
        sync_thread_running_ = true;
        sync_thread_ = std::make_unique<std::thread>(&LogManager::syncLoop, this);
    }
}

void LogManager::stopSyncThread() {
    if (sync_thread_ && sync_thread_->joinable()) {
        sync_thread_running_ = false;
        sync_thread_->join();
    }
}

void LogManager::syncLoop() {
    while (sync_thread_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sync_interval_ms_));
        flushAll();
    }
}

void LogManager::writeToDisk(const LogRecord& record) {
    // Verificar se precisa rotacionar
    log_file_.seekp(0, std::ios::end);
    size_t file_size = log_file_.tellp();
    
    if (file_size > max_log_size_) {
        rotateLogFile();
    }
    
    // Escrever registro
    char buffer[sizeof(LogRecord) + record.data_length];
    size_t size = serializeRecord(record, buffer);
    log_file_.write(buffer, size);
}

void LogManager::flushBuffer() {
    if (buffer_pos_ > 0) {
        log_file_.write(write_buffer_.data(), buffer_pos_);
        log_file_.flush();
        buffer_pos_ = 0;
        stats_.flushes++;
    }
}

bool LogManager::rotateLogFile() {
    log_file_.close();
    
    // Renomear arquivo atual
    std::string new_name = log_filename_ + "." + 
                          std::to_string(current_lsn_);
    std::rename(log_filename_.c_str(), new_name.c_str());
    
    // Criar novo arquivo
    log_file_.open(log_filename_, std::ios::binary | std::ios::out);
    if (!log_file_.is_open()) {
        return false;
    }
    
    // Escrever header
    LogFileHeader header;
    header.start_lsn = current_lsn_;
    log_file_.write(reinterpret_cast<char*>(&header), sizeof(header));
    
    return true;
}

std::string LogManager::getLogFileName(LogSequenceNumber lsn) const {
    // Determinar qual arquivo contém este LSN
    // Implementação simples: todos os logs no mesmo arquivo
    return log_filename_;
}

LogSequenceNumber LogManager::parseLSN(const std::string& filename) const {
    size_t pos = filename.find_last_of('.');
    if (pos != std::string::npos) {
        return std::stoull(filename.substr(pos + 1));
    }
    return 0;
}

size_t LogManager::serializeRecord(const LogRecord& record, char* buffer) {
    char* start = buffer;
    
    // Campos fixos
    memcpy(buffer, &record.lsn, sizeof(record.lsn));
    buffer += sizeof(record.lsn);
    
    memcpy(buffer, &record.type, sizeof(record.type));
    buffer += sizeof(record.type);
    
    memcpy(buffer, &record.transaction_id, sizeof(record.transaction_id));
    buffer += sizeof(record.transaction_id);
    
    memcpy(buffer, &record.page_id, sizeof(record.page_id));
    buffer += sizeof(record.page_id);
    
    memcpy(buffer, &record.page_offset, sizeof(record.page_offset));
    buffer += sizeof(record.page_offset);
    
    memcpy(buffer, &record.data_length, sizeof(record.data_length));
    buffer += sizeof(record.data_length);
    
    memcpy(buffer, &record.prev_lsn, sizeof(record.prev_lsn));
    buffer += sizeof(record.prev_lsn);
    
    // Timestamp
    auto timestamp = std::chrono::system_clock::to_time_t(record.timestamp);
    memcpy(buffer, &timestamp, sizeof(timestamp));
    buffer += sizeof(timestamp);
    
    // Dados variáveis
    if (record.data_length > 0 && record.data) {
        memcpy(buffer, record.data.get(), record.data_length);
        buffer += record.data_length;
    }
    
    return buffer - start;
}

size_t LogManager::deserializeRecord(const char* buffer, LogRecord& record) {
    const char* start = buffer;
    
    // Campos fixos
    memcpy(&record.lsn, buffer, sizeof(record.lsn));
    buffer += sizeof(record.lsn);
    
    memcpy(&record.type, buffer, sizeof(record.type));
    buffer += sizeof(record.type);
    
    memcpy(&record.transaction_id, buffer, sizeof(record.transaction_id));
    buffer += sizeof(record.transaction_id);
    
    memcpy(&record.page_id, buffer, sizeof(record.page_id));
    buffer += sizeof(record.page_id);
    
    memcpy(&record.page_offset, buffer, sizeof(record.page_offset));
    buffer += sizeof(record.page_offset);
    
    memcpy(&record.data_length, buffer, sizeof(record.data_length));
    buffer += sizeof(record.data_length);
    
    memcpy(&record.prev_lsn, buffer, sizeof(record.prev_lsn));
    buffer += sizeof(record.prev_lsn);
    
    // Timestamp
    std::time_t timestamp;
    memcpy(&timestamp, buffer, sizeof(timestamp));
    record.timestamp = std::chrono::system_clock::from_time_t(timestamp);
    buffer += sizeof(timestamp);
    
    // Dados variáveis
    if (record.data_length > 0) {
        record.data = std::make_unique<char[]>(record.data_length);
        memcpy(record.data.get(), buffer, record.data_length);
        buffer += record.data_length;
    }
    
    return buffer - start;
}

} // namespace orangesql