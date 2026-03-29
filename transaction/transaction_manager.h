#ifndef ORANGESQL_TRANSACTION_MANAGER_H
#define ORANGESQL_TRANSACTION_MANAGER_H

#include "../include/types.h"
#include "../include/constants.h"
#include "log_manager.h"
#include "lock_manager.h"
#include <unordered_map>
#include <memory>
#include <chrono>

namespace orangesql {

// Estado da transação
enum class TransactionState {
    ACTIVE,
    PREPARED,
    COMMITTED,
    ABORTED,
    ROLLBACK
};

// Modo de acesso
enum class AccessMode {
    READ_ONLY,
    READ_WRITE
};

// Nível de isolamento
enum class IsolationLevel {
    READ_UNCOMMITTED,
    READ_COMMITTED,
    REPEATABLE_READ,
    SERIALIZABLE
};

// Prioridade da transação
enum class TransactionPriority {
    LOW,
    NORMAL,
    HIGH,
    CRITICAL
};

// Transação
class Transaction {
public:
    Transaction(TransactionId id, IsolationLevel isolation, AccessMode mode);
    ~Transaction() = default;
    
    // Getters
    TransactionId getId() const { return id_; }
    TransactionState getState() const { return state_; }
    IsolationLevel getIsolation() const { return isolation_; }
    AccessMode getAccessMode() const { return mode_; }
    TransactionPriority getPriority() const { return priority_; }
    
    // State management
    void setState(TransactionState state) { state_ = state; }
    void setPriority(TransactionPriority priority) { priority_ = priority; }
    
    // Timestamps
    std::chrono::system_clock::time_point getStartTime() const { return start_time_; }
    std::chrono::system_clock::time_point getLastAccessTime() const { return last_access_; }
    void updateAccessTime() { last_access_ = std::chrono::system_clock::now(); }
    
    // WAL
    void setLastLSN(LogSequenceNumber lsn) { last_lsn_ = lsn; }
    LogSequenceNumber getLastLSN() const { return last_lsn_; }
    
    // Modified pages
    void addModifiedPage(PageId page_id, uint32_t lsn) {
        modified_pages_[page_id] = lsn;
    }
    
    const std::unordered_map<PageId, uint32_t>& getModifiedPages() const {
        return modified_pages_;
    }
    
    // Savepoints
    struct Savepoint {
        std::string name;
        size_t log_position;
        std::unordered_map<PageId, std::vector<Value>> before_images;
    };
    
    void createSavepoint(const std::string& name);
    void rollbackToSavepoint(const std::string& name);
    void releaseSavepoint(const std::string& name);
    
    // Tempo de vida
    bool isExpired(uint64_t timeout_ms) const;
    
private:
    TransactionId id_;
    TransactionState state_;
    IsolationLevel isolation_;
    AccessMode mode_;
    TransactionPriority priority_;
    
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point last_access_;
    
    LogSequenceNumber last_lsn_;
    std::unordered_map<PageId, uint32_t> modified_pages_;
    
    std::vector<Savepoint> savepoints_;
};

// Estatísticas do gerenciador de transações
struct TransactionManagerStats {
    size_t active_transactions;
    size_t committed_transactions;
    size_t aborted_transactions;
    size_t deadlock_detected;
    double avg_transaction_time_ms;
    size_t max_concurrent_transactions;
    
    TransactionManagerStats() : active_transactions(0), committed_transactions(0),
                               aborted_transactions(0), deadlock_detected(0),
                               avg_transaction_time_ms(0), max_concurrent_transactions(0) {}
};

// Gerenciador de transações
class TransactionManager {
public:
    TransactionManager(BufferPool* buffer_pool, LogManager* log_manager);
    ~TransactionManager();
    
    // Gerenciamento de transações
    Transaction* begin(IsolationLevel isolation = IsolationLevel::REPEATABLE_READ,
                      AccessMode mode = AccessMode::READ_WRITE);
    Status commit(Transaction* tx);
    Status abort(Transaction* tx);
    Status rollback(Transaction* tx);
    
    // Gerenciamento de savepoints
    Status savepoint(Transaction* tx, const std::string& name);
    Status rollbackToSavepoint(Transaction* tx, const std::string& name);
    Status releaseSavepoint(Transaction* tx, const std::string& name);
    
    // Acesso a transações
    Transaction* getTransaction(TransactionId id);
    std::vector<Transaction*> getActiveTransactions() const;
    
    // Checkpoint e recovery
    Status checkpoint();
    Status recover();
    
    // Deadlock detection
    void setDeadlockTimeout(uint64_t timeout_ms) { deadlock_timeout_ms_ = timeout_ms; }
    void setDeadlockDetectionInterval(uint64_t interval_ms) { 
        deadlock_detection_interval_ms_ = interval_ms; 
    }
    
    // Estatísticas
    const TransactionManagerStats& getStats() const { return stats_; }
    void resetStats();
    
    // Configurações
    void setMaxTransactions(size_t max) { max_transactions_ = max; }
    void setDefaultIsolation(IsolationLevel level) { default_isolation_ = level; }
    
    // Validação
    bool validateTransaction(Transaction* tx);

private:
    BufferPool* buffer_pool_;
    LogManager* log_manager_;
    LockManager lock_manager_;
    
    std::unordered_map<TransactionId, std::unique_ptr<Transaction>> active_transactions_;
    std::unordered_map<TransactionId, std::unique_ptr<Transaction>> completed_transactions_;
    
    TransactionId next_tx_id_;
    size_t max_transactions_;
    IsolationLevel default_isolation_;
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    
    // Deadlock detection
    std::unique_ptr<std::thread> deadlock_detector_;
    bool deadlock_detector_running_;
    uint64_t deadlock_timeout_ms_;
    uint64_t deadlock_detection_interval_ms_;
    
    // Estatísticas
    TransactionManagerStats stats_;
    
    // Gerador de IDs
    TransactionId generateTransactionId();
    
    // Deadlock detection
    void startDeadlockDetector();
    void stopDeadlockDetector();
    void detectDeadlocks();
    bool checkDeadlock(TransactionId start_tx, std::vector<TransactionId>& visited);
    
    // Cleanup
    void cleanupExpiredTransactions();
    void archiveCompletedTransaction(std::unique_ptr<Transaction> tx);
    
    // Recovery helpers
    bool redoTransaction(const LogRecord& record);
    bool undoTransaction(TransactionId tx_id);
};

} // namespace orangesql

#endif // ORANGESQL_TRANSACTION_MANAGER_H