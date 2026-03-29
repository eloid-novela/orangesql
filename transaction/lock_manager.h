#ifndef ORANGESQL_LOCK_MANAGER_H
#define ORANGESQL_LOCK_MANAGER_H

#include "../include/types.h"
#include "../include/constants.h"
#include <unordered_map>
#include <set>
#include <queue>
#include <chrono>

namespace orangesql {

// Modos de lock
enum class LockMode {
    SHARED,        // Leitura
    EXCLUSIVE,     // Escrita
    INTENT_SHARED, // Intenção de leitura
    INTENT_EXCLUSIVE, // Intenção de escrita
    SHARED_INTENT_EXCLUSIVE // Leitura com intenção de escrita
};

// Granularidade do lock
enum class LockGranularity {
    TABLE,
    PAGE,
    ROW
};

// Status do lock
enum class LockStatus {
    GRANTED,
    WAITING,
    TIMEOUT,
    DEADLOCK
};

// Entrada de lock
struct LockEntry {
    LockMode mode;
    TransactionId holder;
    LockGranularity granularity;
    std::chrono::system_clock::time_point granted_time;
    
    LockEntry(LockMode m, TransactionId t, LockGranularity g)
        : mode(m), holder(t), granularity(g),
          granted_time(std::chrono::system_clock::now()) {}
};

// Fila de espera
struct LockWaitEntry {
    TransactionId waiter;
    LockMode requested_mode;
    std::chrono::system_clock::time_point request_time;
    
    LockWaitEntry(TransactionId t, LockMode m)
        : waiter(t), requested_mode(m),
          request_time(std::chrono::system_clock::now()) {}
};

// Estatísticas de locks
struct LockStats {
    size_t total_locks;
    size_t granted_locks;
    size_t waiting_locks;
    size_t deadlocks_detected;
    size_t lock_timeouts;
    double avg_wait_time_ms;
    
    LockStats() : total_locks(0), granted_locks(0), waiting_locks(0),
                 deadlocks_detected(0), lock_timeouts(0), avg_wait_time_ms(0) {}
};

// Gerenciador de locks (2PL - Two-Phase Locking)
class LockManager {
public:
    LockManager(BufferPool* buffer_pool);
    ~LockManager();
    
    // Aquisição de locks
    LockStatus acquireLock(TransactionId tx_id, const std::string& resource,
                          LockMode mode, LockGranularity granularity,
                          uint64_t timeout_ms = 5000);
    
    // Liberação de locks
    bool releaseLock(TransactionId tx_id, const std::string& resource);
    bool releaseAllLocks(TransactionId tx_id);
    
    // Upgrade de lock
    LockStatus upgradeLock(TransactionId tx_id, const std::string& resource,
                          LockMode new_mode);
    
    // Verificação
    bool hasLock(TransactionId tx_id, const std::string& resource, LockMode mode) const;
    bool isLocked(const std::string& resource) const;
    LockMode getLockMode(const std::string& resource) const;
    
    // Wait-for graph
    std::vector<TransactionId> getWaitingTransactions(TransactionId tx_id) const;
    bool hasDeadlock(TransactionId tx_id) const;
    
    // Estatísticas
    const LockStats& getStats() const { return stats_; }
    void resetStats();
    
    // Debug
    void dumpLocks() const;
    void dumpWaitGraph() const;

private:
    BufferPool* buffer_pool_;
    
    // Locks concedidos
    std::unordered_map<std::string, std::vector<LockEntry>> granted_locks_;
    
    // Filas de espera
    std::unordered_map<std::string, std::queue<LockWaitEntry>> wait_queues_;
    
    // Mapa de transações esperando
    std::unordered_map<TransactionId, std::vector<std::string>> waiting_for_;
    
    // Estatísticas
    mutable LockStats stats_;
    mutable std::mutex mutex_;
    
    // Compatibilidade de modos
    bool areCompatible(LockMode existing, LockMode requested) const;
    bool canGrant(const std::vector<LockEntry>& current_locks, LockMode requested) const;
    
    // Deadlock detection
    bool checkDeadlock(TransactionId start_tx, 
                      std::unordered_map<TransactionId, bool>& visited,
                      std::unordered_map<TransactionId, TransactionId>& parent);
    
    // Timeout handling
    void checkTimeouts();
    void removeFromWaitQueue(const std::string& resource, TransactionId tx_id);
};

} // namespace orangesql

#endif // ORANGESQL_LOCK_MANAGER_H