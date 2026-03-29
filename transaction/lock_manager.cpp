#include "lock_manager.h"
#include <algorithm>
#include <thread>

namespace orangesql {

LockManager::LockManager(BufferPool* buffer_pool)
    : buffer_pool_(buffer_pool) {
}

LockManager::~LockManager() {
}

LockStatus LockManager::acquireLock(TransactionId tx_id, const std::string& resource,
                                    LockMode mode, LockGranularity granularity,
                                    uint64_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto& current_locks = granted_locks_[resource];
    auto& wait_queue = wait_queues_[resource];
    
    // Verificar se pode conceder imediatamente
    if (canGrant(current_locks, mode)) {
        current_locks.emplace_back(mode, tx_id, granularity);
        stats_.granted_locks++;
        stats_.total_locks++;
        return LockStatus::GRANTED;
    }
    
    // Adicionar à fila de espera
    wait_queue.emplace(tx_id, mode);
    waiting_for_[tx_id].push_back(resource);
    stats_.waiting_locks++;
    
    // Esperar pelo lock com timeout
    auto start_time = std::chrono::steady_clock::now();
    
    while (true) {
        // Verificar timeout
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>
                      (now - start_time).count();
        
        if (elapsed > timeout_ms) {
            // Timeout, remover da fila
            removeFromWaitQueue(resource, tx_id);
            stats_.lock_timeouts++;
            return LockStatus::TIMEOUT;
        }
        
        // Verificar deadlock periodicamente
        if (elapsed % 100 == 0) { // A cada 100ms
            if (hasDeadlock(tx_id)) {
                removeFromWaitQueue(resource, tx_id);
                stats_.deadlocks_detected++;
                return LockStatus::DEADLOCK;
            }
        }
        
        // Tentar conceder novamente
        if (canGrant(current_locks, mode)) {
            // Remover da fila de espera
            wait_queue.pop();
            
            // Remover do mapa de espera
            auto& waiting = waiting_for_[tx_id];
            waiting.erase(std::remove(waiting.begin(), waiting.end(), resource),
                         waiting.end());
            if (waiting.empty()) {
                waiting_for_.erase(tx_id);
            }
            
            // Conceder lock
            current_locks.emplace_back(mode, tx_id, granularity);
            
            stats_.granted_locks++;
            stats_.total_locks++;
            stats_.waiting_locks--;
            
            return LockStatus::GRANTED;
        }
        
        // Esperar um pouco antes de tentar novamente
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        lock.lock();
    }
}

bool LockManager::releaseLock(TransactionId tx_id, const std::string& resource) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = granted_locks_.find(resource);
    if (it == granted_locks_.end()) {
        return false;
    }
    
    auto& locks = it->second;
    
    // Remover lock da transação
    locks.erase(std::remove_if(locks.begin(), locks.end(),
                               [tx_id](const LockEntry& entry) {
                                   return entry.holder == tx_id;
                               }),
                locks.end());
    
    if (locks.empty()) {
        granted_locks_.erase(it);
    }
    
    stats_.total_locks--;
    
    return true;
}

bool LockManager::releaseAllLocks(TransactionId tx_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Remover todos os locks desta transação
    for (auto it = granted_locks_.begin(); it != granted_locks_.end();) {
        auto& locks = it->second;
        
        locks.erase(std::remove_if(locks.begin(), locks.end(),
                                   [tx_id](const LockEntry& entry) {
                                       return entry.holder == tx_id;
                                   }),
                    locks.end());
        
        if (locks.empty()) {
            it = granted_locks_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Remover da fila de espera
    for (auto& [resource, queue] : wait_queues_) {
        std::queue<LockWaitEntry> new_queue;
        while (!queue.empty()) {
            if (queue.front().waiter != tx_id) {
                new_queue.push(queue.front());
            }
            queue.pop();
        }
        queue = std::move(new_queue);
    }
    
    waiting_for_.erase(tx_id);
    
    return true;
}

LockStatus LockManager::upgradeLock(TransactionId tx_id, const std::string& resource,
                                    LockMode new_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& locks = granted_locks_[resource];
    
    // Encontrar lock existente
    auto it = std::find_if(locks.begin(), locks.end(),
                          [tx_id](const LockEntry& entry) {
                              return entry.holder == tx_id;
                          });
    
    if (it == locks.end()) {
        return LockStatus::TIMEOUT; // Não tem lock para fazer upgrade
    }
    
    // Verificar se pode fazer upgrade
    LockMode old_mode = it->mode;
    
    // Remover lock antigo temporariamente
    locks.erase(it);
    
    // Tentar adquirir novo modo
    if (canGrant(locks, new_mode)) {
        locks.emplace_back(new_mode, tx_id, it->granularity);
        return LockStatus::GRANTED;
    }
    
    // Não pode fazer upgrade, restaurar lock antigo
    locks.emplace_back(old_mode, tx_id, it->granularity);
    
    return LockStatus::WAITING;
}

bool LockManager::hasLock(TransactionId tx_id, const std::string& resource,
                          LockMode mode) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = granted_locks_.find(resource);
    if (it == granted_locks_.end()) {
        return false;
    }
    
    const auto& locks = it->second;
    
    return std::any_of(locks.begin(), locks.end(),
                      [tx_id, mode](const LockEntry& entry) {
                          return entry.holder == tx_id && entry.mode == mode;
                      });
}

bool LockManager::isLocked(const std::string& resource) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return granted_locks_.find(resource) != granted_locks_.end();
}

LockMode LockManager::getLockMode(const std::string& resource) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = granted_locks_.find(resource);
    if (it == granted_locks_.end() || it->second.empty()) {
        return LockMode::SHARED; // Não há lock, modo mais permissivo
    }
    
    // Retornar modo mais restritivo
    LockMode mode = LockMode::SHARED;
    for (const auto& entry : it->second) {
        if (entry.mode == LockMode::EXCLUSIVE) {
            return LockMode::EXCLUSIVE;
        }
        if (entry.mode == LockMode::SHARED_INTENT_EXCLUSIVE) {
            mode = LockMode::SHARED_INTENT_EXCLUSIVE;
        } else if (entry.mode == LockMode::INTENT_EXCLUSIVE && 
                   mode != LockMode::SHARED_INTENT_EXCLUSIVE) {
            mode = LockMode::INTENT_EXCLUSIVE;
        } else if (entry.mode == LockMode::INTENT_SHARED && 
                   mode == LockMode::SHARED) {
            mode = LockMode::INTENT_SHARED;
        }
    }
    
    return mode;
}

std::vector<TransactionId> LockManager::getWaitingTransactions(TransactionId tx_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<TransactionId> waiting;
    
    for (const auto& [waiter, resources] : waiting_for_) {
        if (waiter == tx_id) continue;
        
        for (const auto& resource : resources) {
            // Verificar se tx_id tem lock neste recurso
            auto it = granted_locks_.find(resource);
            if (it != granted_locks_.end()) {
                for (const auto& entry : it->second) {
                    if (entry.holder == tx_id) {
                        waiting.push_back(waiter);
                        break;
                    }
                }
            }
        }
    }
    
    return waiting;
}

bool LockManager::hasDeadlock(TransactionId tx_id) const {
    std::unordered_map<TransactionId, bool> visited;
    std::unordered_map<TransactionId, TransactionId> parent;
    
    return checkDeadlock(tx_id, visited, parent);
}

bool LockManager::areCompatible(LockMode existing, LockMode requested) const {
    // Matriz de compatibilidade
    // S = Shared, X = Exclusive, IS = Intent Shared, IX = Intent Exclusive, SIX = Shared Intent Exclusive
    
    if (existing == LockMode::EXCLUSIVE || requested == LockMode::EXCLUSIVE) {
        return false;
    }
    
    if (existing == LockMode::SHARED_INTENT_EXCLUSIVE) {
        return requested == LockMode::SHARED || requested == LockMode::INTENT_SHARED;
    }
    
    if (requested == LockMode::SHARED_INTENT_EXCLUSIVE) {
        return existing == LockMode::SHARED || existing == LockMode::INTENT_SHARED;
    }
    
    if (existing == LockMode::INTENT_EXCLUSIVE) {
        return requested == LockMode::INTENT_SHARED;
    }
    
    if (requested == LockMode::INTENT_EXCLUSIVE) {
        return existing == LockMode::INTENT_SHARED;
    }
    
    return true; // Shared e Intent Shared são compatíveis
}

bool LockManager::canGrant(const std::vector<LockEntry>& current_locks,
                           LockMode requested) const {
    for (const auto& lock : current_locks) {
        if (!areCompatible(lock.mode, requested)) {
            return false;
        }
    }
    return true;
}

bool LockManager::checkDeadlock(TransactionId start_tx,
                               std::unordered_map<TransactionId, bool>& visited,
                               std::unordered_map<TransactionId, TransactionId>& parent) {
    if (visited[start_tx]) {
        // Ciclo detectado
        return true;
    }
    
    visited[start_tx] = true;
    
    // Obter transações que esta transação está esperando
    auto waiting = getWaitingTransactions(start_tx);
    
    for (TransactionId waiter : waiting) {
        parent[waiter] = start_tx;
        if (checkDeadlock(waiter, visited, parent)) {
            return true;
        }
    }
    
    visited[start_tx] = false;
    return false;
}

void LockManager::checkTimeouts() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    
    for (auto& [resource, queue] : wait_queues_) {
        std::queue<LockWaitEntry> new_queue;
        
        while (!queue.empty()) {
            const auto& entry = queue.front();
            
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>
                          (now - entry.request_time).count();
            
            if (elapsed > 5000) { // 5 segundos timeout
                // Timeout, remover
                removeFromWaitQueue(resource, entry.waiter);
                stats_.lock_timeouts++;
            } else {
                new_queue.push(entry);
            }
            
            queue.pop();
        }
        
        queue = std::move(new_queue);
    }
}

void LockManager::removeFromWaitQueue(const std::string& resource, TransactionId tx_id) {
    auto& queue = wait_queues_[resource];
    std::queue<LockWaitEntry> new_queue;
    
    while (!queue.empty()) {
        if (queue.front().waiter != tx_id) {
            new_queue.push(queue.front());
        }
        queue.pop();
    }
    
    queue = std::move(new_queue);
    
    // Remover do mapa de espera
    auto& waiting = waiting_for_[tx_id];
    waiting.erase(std::remove(waiting.begin(), waiting.end(), resource),
                 waiting.end());
    if (waiting.empty()) {
        waiting_for_.erase(tx_id);
    }
}

void LockManager::resetStats() {
    stats_ = LockStats();
}

void LockManager::dumpLocks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::cout << "Current Locks:\n";
    for (const auto& [resource, locks] : granted_locks_) {
        std::cout << "  Resource: " << resource << "\n";
        for (const auto& lock : locks) {
            std::cout << "    TX " << lock.holder << ": "
                      << static_cast<int>(lock.mode) << "\n";
        }
    }
}

void LockManager::dumpWaitGraph() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::cout << "Wait-for Graph:\n";
    for (const auto& [tx, resources] : waiting_for_) {
        std::cout << "  TX " << tx << " waiting for: ";
        for (const auto& r : resources) {
            std::cout << r << " ";
        }
        std::cout << "\n";
    }
}

} // namespace orangesql