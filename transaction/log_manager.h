#ifndef ORANGESQL_LOG_MANAGER_H
#define ORANGESQL_LOG_MANAGER_H

#include "../include/types.h"
#include "../include/constants.h"
#include "../storage/file_manager.h"
#include <vector>
#include <unordered_map>
#include <chrono>

namespace orangesql {

// Tipos de registro de log
enum class LogRecordType {
    BEGIN,
    COMMIT,
    ABORT,
    CHECKPOINT,
    SAVEPOINT,
    ROLLBACK_SAVEPOINT,
    INSERT,
    UPDATE,
    DELETE,
    COMPENSATION,  // Compensating log record (para UNDO)
    CLR            // Compensation log record
};

// Registro de log
struct LogRecord {
    LogSequenceNumber lsn;
    LogRecordType type;
    TransactionId transaction_id;
    PageId page_id;
    uint32_t page_offset;
    uint16_t data_length;
    std::unique_ptr<char[]> data;
    std::chrono::system_clock::time_point timestamp;
    LogSequenceNumber prev_lsn;  // Previous LSN da mesma transação
    
    LogRecord() : lsn(0), type(LogRecordType::BEGIN), transaction_id(0),
                 page_id(INVALID_PAGE_ID), page_offset(0), data_length(0),
                 prev_lsn(0) {}
};

// Cabeçalho do arquivo de log
struct LogFileHeader {
    uint32_t magic;           // "ORLG"
    uint32_t version;
    LogSequenceNumber start_lsn;
    LogSequenceNumber end_lsn;
    LogSequenceNumber checkpoint_lsn;
    size_t page_size;
    uint32_t checksum;
    
    LogFileHeader() : magic(0x4F524C47), version(1), start_lsn(1),
                     end_lsn(1), checkpoint_lsn(0), page_size(PAGE_SIZE) {}
};

// Estatísticas do log
struct LogStats {
    size_t total_records;
    size_t bytes_written;
    size_t flushes;
    size_t checkpoints;
    size_t recovery_time_ms;
    
    LogStats() : total_records(0), bytes_written(0), flushes(0),
                checkpoints(0), recovery_time_ms(0) {}
};

// Modo de log
enum class LogMode {
    NORMAL,
    ASYNC,
    NO_LOG
};

// Gerenciador de logs (Write-Ahead Logging)
class LogManager {
public:
    LogManager(FileManager* file_manager);
    ~LogManager();
    
    // Inicialização
    bool init();
    void shutdown();
    
    // Escrita de logs
    LogSequenceNumber appendLogRecord(const LogRecord& record);
    LogSequenceNumber appendLogRecord(LogRecordType type, TransactionId tx_id,
                                       PageId page_id = INVALID_PAGE_ID,
                                       const char* data = nullptr,
                                       size_t data_len = 0);
    
    // Leitura de logs
    LogRecord getLogRecord(LogSequenceNumber lsn);
    std::vector<LogRecord> getLogsSince(LogSequenceNumber lsn);
    std::vector<LogRecord> getLogsForTransaction(TransactionId tx_id);
    std::unordered_map<LogSequenceNumber, LogRecord> getLogs() const { return log_buffer_; }
    
    // Sincronização
    void flush(LogSequenceNumber lsn);
    void flushAll();
    void truncate(LogSequenceNumber lsn);
    
    // Recovery
    Status recover();
    void setRecoveryMode(bool enabled) { recovery_mode_ = enabled; }
    
    // Checkpoint
    void setCheckpoint(LogSequenceNumber lsn);
    LogSequenceNumber getLastCheckpoint() const { return last_checkpoint_; }
    
    // Estatísticas
    const LogStats& getStats() const { return stats_; }
    void resetStats();
    
    // Configurações
    void setLogMode(LogMode mode) { mode_ = mode; }
    void setLogFileSize(size_t size) { max_log_size_ = size; }
    void setSyncInterval(size_t ms) { sync_interval_ms_ = ms; }
    
    // Gerenciamento de arquivos
    bool archiveLog(LogSequenceNumber start, LogSequenceNumber end);
    bool deleteArchivedLogs(LogSequenceNumber before);
    
private:
    FileManager* file_manager_;
    std::string log_filename_;
    std::fstream log_file_;
    
    LogMode mode_;
    size_t max_log_size_;
    size_t sync_interval_ms_;
    bool recovery_mode_;
    
    LogSequenceNumber current_lsn_;
    LogSequenceNumber last_checkpoint_;
    std::unordered_map<LogSequenceNumber, LogRecord> log_buffer_;
    std::unordered_map<TransactionId, LogSequenceNumber> last_tx_lsn_;
    
    LogStats stats_;
    mutable std::mutex mutex_;
    std::unique_ptr<std::thread> sync_thread_;
    bool sync_thread_running_;
    
    // Buffer de escrita
    static constexpr size_t WRITE_BUFFER_SIZE = 1024 * 1024; // 1MB
    std::vector<char> write_buffer_;
    size_t buffer_pos_;
    
    // Métodos internos
    void startSyncThread();
    void stopSyncThread();
    void syncLoop();
    
    void writeToDisk(const LogRecord& record);
    void flushBuffer();
    bool rotateLogFile();
    
    std::string getLogFileName(LogSequenceNumber lsn) const;
    LogSequenceNumber parseLSN(const std::string& filename) const;
    
    // Serialização
    size_t serializeRecord(const LogRecord& record, char* buffer);
    size_t deserializeRecord(const char* buffer, LogRecord& record);
};

} // namespace orangesql

#endif // ORANGESQL_LOG_MANAGER_H