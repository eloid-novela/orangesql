#include "transaction_manager.h"
#include <algorithm>
#include <thread>

namespace orangesql {

// ============================================
// Transaction Implementation
// ============================================

Transaction::Transaction(TransactionId id, IsolationLevel isolation, AccessMode mode)
    : id_(id)
    , state_(TransactionState::ACTIVE)
    , isolation_(isolation)
    , mode_(mode)
    , priority_(TransactionPriority::NORMAL)
    , start_time_(std::chrono::system_clock::now())
    , last_access_(start_time_)
    , last_lsn_(0) {
}

void Transaction::createSavepoint(const std::string& name) {
    Savepoint sp;
    sp.name = name;
    sp.log_position = last_lsn_;
    
    // Salvar before images das páginas modificadas
    for (const auto& [page_id, lsn] : modified_pages_) {
        // TODO: Salvar before image
    }
    
    savepoints_.push_back(sp);
}

void Transaction::rollbackToSavepoint(const std::string& name) {
    auto it = std::find_if(savepoints_.begin(), savepoints_.end(),
                          [&name](const Savepoint& sp) { return sp.name == name; });
    
    if (it != savepoints_.end()) {
        // Restaurar before images
        for (const auto& [page_id, before_image] : it->before_images) {
            // TODO: Restaurar página
        }
        
        // Remover savepoints posteriores
        savepoints_.erase(it + 1, savepoints_.end());
        savepoints_.pop_back();
    }
}

void Transaction::releaseSavepoint(const std::string& name) {
    auto it = std::find_if(savepoints_.begin(), savepoints_.end(),
                          [&name](const Savepoint& sp) { return sp.name == name; });
    
    if (it != savepoints_.end()) {
        savepoints_.erase(it);
    }
}

bool Transaction::isExpired(uint64_t timeout_ms) const {
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>
                  (now - last_access_).count();
    return elapsed > timeout_ms;
}

// ============================================
// TransactionManager Implementation
// ============================================

TransactionManager::TransactionManager(BufferPool* buffer_pool, LogManager* log_manager)
    : buffer_pool_(buffer_pool)
    , log_manager_(log_manager)
    , lock_manager_(buffer_pool)
    , next_tx_id_(1)
    , max_transactions_(1000)
    , default_isolation_(IsolationLevel::REPEATABLE_READ)
    , deadlock_detector_running_(false)
    , deadlock_timeout_ms_(5000)
    , deadlock_detection_interval_ms_(1000) {
    
    startDeadlockDetector();
}

TransactionManager::~TransactionManager() {
    stopDeadlockDetector();
    
    // Abortar transações ativas
    for (auto& [id, tx] : active_transactions_) {
        if (tx->getState() == TransactionState::ACTIVE) {
            abort(tx.get());
        }
    }
}

Transaction* TransactionManager::begin(IsolationLevel isolation, AccessMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (active_transactions_.size() >= max_transactions_) {
        return nullptr;
    }
    
    TransactionId id = generateTransactionId();
    auto tx = std::make_unique<Transaction>(id, isolation, mode);
    
    // Log begin transaction
    LogRecord record;
    record.type = LogRecordType::BEGIN;
    record.transaction_id = id;
    record.timestamp = std::chrono::system_clock::now();
    
    LogSequenceNumber lsn = log_manager_->appendLogRecord(record);
    tx->setLastLSN(lsn);
    
    Transaction* ptr = tx.get();
    active_transactions_[id] = std::move(tx);
    
    stats_.active_transactions++;
    stats_.max_concurrent_transactions = std::max(
        stats_.max_concurrent_transactions, stats_.active_transactions);
    
    return ptr;
}

Status TransactionManager::commit(Transaction* tx) {
    if (!tx || tx->getState() != TransactionState::ACTIVE) {
        return Status::ERROR;
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Verificar se todas as páginas modificadas foram escritas
        for (const auto& [page_id, lsn] : tx->getModifiedPages()) {
            // Forçar flush se necessário
            buffer_pool_->flushPage(page_id);
        }
        
        // Log commit
        LogRecord record;
        record.type = LogRecordType::COMMIT;
        record.transaction_id = tx->getId();
        record.timestamp = std::chrono::system_clock::now();
        
        LogSequenceNumber lsn = log_manager_->appendLogRecord(record);
        tx->setLastLSN(lsn);
        
        // Liberar locks
        lock_manager_.releaseAllLocks(tx->getId());
        
        // Atualizar estado
        tx->setState(TransactionState::COMMITTED);
        
        // Mover para transações completadas
        auto it = active_transactions_.find(tx->getId());
        if (it != active_transactions_.end()) {
            archiveCompletedTransaction(std::move(it->second));
            active_transactions_.erase(it);
        }
        
        stats_.committed_transactions++;
        stats_.active_transactions--;
    }
    
    // Atualizar estatísticas
    auto start = tx->getStartTime();
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    
    stats_.avg_transaction_time_ms = (stats_.avg_transaction_time_ms * 
        (stats_.committed_transactions - 1) + elapsed) / stats_.committed_transactions;
    
    return Status::OK;
}

Status TransactionManager::abort(Transaction* tx) {
    if (!tx || tx->getState() != TransactionState::ACTIVE) {
        return Status::ERROR;
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Fazer rollback das alterações
        if (!undoTransaction(tx->getId())) {
            return Status::ERROR;
        }
        
        // Log abort
        LogRecord record;
        record.type = LogRecordType::ABORT;
        record.transaction_id = tx->getId();
        record.timestamp = std::chrono::system_clock::now();
        
        LogSequenceNumber lsn = log_manager_->appendLogRecord(record);
        tx->setLastLSN(lsn);
        
        // Liberar locks
        lock_manager_.releaseAllLocks(tx->getId());
        
        // Atualizar estado
        tx->setState(TransactionState::ABORTED);
        
        // Mover para transações completadas
        auto it = active_transactions_.find(tx->getId());
        if (it != active_transactions_.end()) {
            archiveCompletedTransaction(std::move(it->second));
            active_transactions_.erase(it);
        }
        
        stats_.aborted_transactions++;
        stats_.active_transactions--;
    }
    
    return Status::OK;
}

Status TransactionManager::rollback(Transaction* tx) {
    // Rollback completo (alias para abort)
    return abort(tx);
}

Status TransactionManager::savepoint(Transaction* tx, const std::string& name) {
    if (!tx || tx->getState() != TransactionState::ACTIVE) {
        return Status::ERROR;
    }
    
    tx->createSavepoint(name);
    
    // Log savepoint
    LogRecord record;
    record.type = LogRecordType::SAVEPOINT;
    record.transaction_id = tx->getId();
    record.data_length = name.length() + 1;
    record.data = std::make_unique<char[]>(record.data_length);
    memcpy(record.data.get(), name.c_str(), name.length() + 1);
    
    log_manager_->appendLogRecord(record);
    
    return Status::OK;
}

Status TransactionManager::rollbackToSavepoint(Transaction* tx, const std::string& name) {
    if (!tx || tx->getState() != TransactionState::ACTIVE) {
        return Status::ERROR;
    }
    
    tx->rollbackToSavepoint(name);
    
    // Log rollback to savepoint
    LogRecord record;
    record.type = LogRecordType::ROLLBACK_SAVEPOINT;
    record.transaction_id = tx->getId();
    record.data_length = name.length() + 1;
    record.data = std::make_unique<char[]>(record.data_length);
    memcpy(record.data.get(), name.c_str(), name.length() + 1);
    
    log_manager_->appendLogRecord(record);
    
    return Status::OK;
}

Status TransactionManager::releaseSavepoint(Transaction* tx, const std::string& name) {
    if (!tx || tx->getState() != TransactionState::ACTIVE) {
        return Status::ERROR;
    }
    
    tx->releaseSavepoint(name);
    return Status::OK;
}

Transaction* TransactionManager::getTransaction(TransactionId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_transactions_.find(id);
    if (it != active_transactions_.end()) {
        it->second->updateAccessTime();
        return it->second.get();
    }
    
    return nullptr;
}

std::vector<Transaction*> TransactionManager::getActiveTransactions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Transaction*> result;
    for (const auto& [id, tx] : active_transactions_) {
        result.push_back(tx.get());
    }
    
    return result;
}

Status TransactionManager::checkpoint() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Forçar flush de todas as páginas sujas
    buffer_pool_->flushAll();
    
    // Escrever registro de checkpoint no log
    LogRecord record;
    record.type = LogRecordType::CHECKPOINT;
    record.transaction_id = 0;
    record.timestamp = std::chrono::system_clock::now();
    
    // Incluir lista de transações ativas
    std::string active_txs;
    for (const auto& [id, tx] : active_transactions_) {
        if (!active_txs.empty()) active_txs += ",";
        active_txs += std::to_string(id);
    }
    
    record.data_length = active_txs.length() + 1;
    record.data = std::make_unique<char[]>(record.data_length);
    memcpy(record.data.get(), active_txs.c_str(), active_txs.length() + 1);
    
    log_manager_->appendLogRecord(record);
    log_manager_->flushAll();
    
    return Status::OK;
}

Status TransactionManager::recover() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 3-phase recovery
    // 1. Analysis: determinar transações ativas no momento do crash
    // 2. Redo: refazer operações de transações commitadas
    // 3. Undo: desfazer operações de transações não commitadas
    
    // Analysis pass
    std::unordered_map<TransactionId, TransactionState> tx_states;
    std::unordered_map<TransactionId, LogSequenceNumber> tx_last_lsn;
    std::unordered_map<PageId, LogSequenceNumber> page_lsns;
    
    auto logs = log_manager_->getLogs();
    LogSequenceNumber checkpoint_lsn = 0;
    
    // Encontrar último checkpoint
    for (const auto& [lsn, record] : logs) {
        if (record.type == LogRecordType::CHECKPOINT) {
            if (lsn > checkpoint_lsn) {
                checkpoint_lsn = lsn;
            }
        }
    }
    
    // Analysis a partir do checkpoint
    for (const auto& [lsn, record] : logs) {
        if (lsn < checkpoint_lsn) continue;
        
        switch (record.type) {
            case LogRecordType::BEGIN:
                tx_states[record.transaction_id] = TransactionState::ACTIVE;
                tx_last_lsn[record.transaction_id] = lsn;
                break;
                
            case LogRecordType::COMMIT:
                tx_states[record.transaction_id] = TransactionState::COMMITTED;
                break;
                
            case LogRecordType::ABORT:
                tx_states[record.transaction_id] = TransactionState::ABORTED;
                break;
                
            case LogRecordType::INSERT:
            case LogRecordType::UPDATE:
            case LogRecordType::DELETE:
                tx_last_lsn[record.transaction_id] = lsn;
                if (record.page_id != INVALID_PAGE_ID) {
                    page_lsns[record.page_id] = lsn;
                }
                break;
        }
    }
    
    // Redo pass (forward)
    for (const auto& [lsn, record] : logs) {
        if (lsn < checkpoint_lsn) continue;
        
        auto tx_state_it = tx_states.find(record.transaction_id);
        if (tx_state_it != tx_states.end() && 
            tx_state_it->second == TransactionState::COMMITTED) {
            
            auto page_lsn_it = page_lsns.find(record.page_id);
            if (page_lsn_it == page_lsns.end() || page_lsn_it->second < lsn) {
                redoTransaction(record);
            }
        }
    }
    
    // Undo pass (backward)
    std::vector<TransactionId> active_txs;
    for (const auto& [tx_id, state] : tx_states) {
        if (state == TransactionState::ACTIVE) {
            active_txs.push_back(tx_id);
        }
    }
    
    // Ordenar por LSN (do mais recente para o mais antigo)
    std::sort(active_txs.begin(), active_txs.end(),
              [&tx_last_lsn](TransactionId a, TransactionId b) {
                  return tx_last_lsn[a] > tx_last_lsn[b];
              });
    
    for (TransactionId tx_id : active_txs) {
        undoTransaction(tx_id);
    }
    
    return Status::OK;
}

void TransactionManager::resetStats() {
    stats_ = TransactionManagerStats();
}

bool TransactionManager::validateTransaction(Transaction* tx) {
    if (!tx) return false;
    
    // Verificar se transação ainda está ativa
    if (tx->getState() != TransactionState::ACTIVE) {
        return false;
    }
    
    // Verificar timeout
    if (tx->isExpired(deadlock_timeout_ms_)) {
        return false;
    }
    
    return true;
}

TransactionId TransactionManager::generateTransactionId() {
    return next_tx_id_++;
}

void TransactionManager::startDeadlockDetector() {
    deadlock_detector_running_ = true;
    deadlock_detector_ = std::make_unique<std::thread>(
        &TransactionManager::detectDeadlocks, this);
}

void TransactionManager::stopDeadlockDetector() {
    deadlock_detector_running_ = false;
    if (deadlock_detector_ && deadlock_detector_->joinable()) {
        deadlock_detector_->join();
    }
}

void TransactionManager::detectDeadlocks() {
    while (deadlock_detector_running_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(deadlock_detection_interval_ms_));
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Detectar deadlocks usando wait-for graph
        for (const auto& [tx_id, tx] : active_transactions_) {
            std::vector<TransactionId> visited;
            if (checkDeadlock(tx_id, visited)) {
                // Deadlock detectado, abortar transação de menor prioridade
                auto victim = std::min_element(visited.begin(), visited.end(),
                    [this](TransactionId a, TransactionId b) {
                        auto tx_a = active_transactions_[a].get();
                        auto tx_b = active_transactions_[b].get();
                        return tx_a->getPriority() < tx_b->getPriority();
                    });
                
                if (victim != visited.end()) {
                    abort(active_transactions_[*victim].get());
                    stats_.deadlock_detected++;
                }
            }
        }
        
        cleanupExpiredTransactions();
    }
}

bool TransactionManager::checkDeadlock(TransactionId start_tx, 
                                        std::vector<TransactionId>& visited) {
    if (std::find(visited.begin(), visited.end(), start_tx) != visited.end()) {
        return true; // Ciclo detectado
    }
    
    visited.push_back(start_tx);
    
    // Obter locks que esta transação está esperando
    auto waiting_for = lock_manager_.getWaitingTransactions(start_tx);
    
    for (TransactionId waiter : waiting_for) {
        if (checkDeadlock(waiter, visited)) {
            return true;
        }
    }
    
    visited.pop_back();
    return false;
}

void TransactionManager::cleanupExpiredTransactions() {
    for (auto it = active_transactions_.begin(); it != active_transactions_.end();) {
        if (it->second->isExpired(deadlock_timeout_ms_)) {
            abort(it->second.get());
            it = active_transactions_.erase(it);
        } else {
            ++it;
        }
    }
}

void TransactionManager::archiveCompletedTransaction(std::unique_ptr<Transaction> tx) {
    // Manter apenas últimas N transações completadas
    const size_t MAX_COMPLETED = 10000;
    
    completed_transactions_[tx->getId()] = std::move(tx);
    
    if (completed_transactions_.size() > MAX_COMPLETED) {
        // Remover mais antiga
        auto oldest = std::min_element(completed_transactions_.begin(),
                                      completed_transactions_.end(),
                                      [](const auto& a, const auto& b) {
                                          return a.second->getStartTime() < 
                                                 b.second->getStartTime();
                                      });
        if (oldest != completed_transactions_.end()) {
            completed_transactions_.erase(oldest);
        }
    }
}

bool TransactionManager::redoTransaction(const LogRecord& record) {
    // Aplicar operação novamente
    switch (record.type) {
        case LogRecordType::INSERT:
        case LogRecordType::UPDATE:
        case LogRecordType::DELETE:
            // TODO: Reaplicar operação
            return true;
        default:
            return false;
    }
}

bool TransactionManager::undoTransaction(TransactionId tx_id) {
    auto tx = getTransaction(tx_id);
    if (!tx) return false;
    
    // Fazer rollback das operações em ordem reversa
    auto logs = log_manager_->getLogsForTransaction(tx_id);
    
    for (auto it = logs.rbegin(); it != logs.rend(); ++it) {
        const auto& record = *it;
        
        switch (record.type) {
            case LogRecordType::INSERT:
                // DELETE correspondente
                break;
            case LogRecordType::UPDATE:
                // Restaurar before image
                break;
            case LogRecordType::DELETE:
                // INSERT correspondente
                break;
            default:
                break;
        }
    }
    
    return true;
}

} // namespace orangesql