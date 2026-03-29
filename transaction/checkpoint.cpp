#include "checkpoint.h"
#include <thread>
#include <chrono>

namespace orangesql {

CheckpointManager::CheckpointManager(LogManager* log_manager, BufferPool* buffer_pool,
                                     TransactionManager* tx_manager)
    : log_manager_(log_manager)
    , buffer_pool_(buffer_pool)
    , tx_manager_(tx_manager)
    , last_checkpoint_lsn_(0)
    , checkpoint_thread_running_(false) {
    
    last_checkpoint_time_ = std::chrono::system_clock::now();
}

CheckpointManager::~CheckpointManager() {
    shutdown();
}

void CheckpointManager::init(const CheckpointConfig& config) {
    config_ = config;
    
    // Tentar recuperar do último checkpoint
    recoverFromCheckpoint();
    
    // Iniciar thread de checkpoint automático
    checkpoint_thread_running_ = true;
    checkpoint_thread_ = std::make_unique<std::thread>(
        &CheckpointManager::checkpointLoop, this);
}

void CheckpointManager::shutdown() {
    if (checkpoint_thread_ && checkpoint_thread_->joinable()) {
        checkpoint_thread_running_ = false;
        checkpoint_thread_->join();
    }
    
    if (config_.force_on_shutdown) {
        createCheckpoint(true);
    }
}

Status CheckpointManager::createCheckpoint(bool force_full) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Obter lista de transações ativas
    auto active_txs = tx_manager_->getActiveTransactions();
    std::vector<TransactionId> tx_ids;
    for (auto* tx : active_txs) {
        tx_ids.push_back(tx->getId());
    }
    
    // Escrever dirty pages no disco
    writeDirtyPages();
    
    // Escrever registro de checkpoint
    writeCheckpointRecord(force_full || !config_.incremental, tx_ids);
    
    // Truncar logs antigos
    truncateLogs();
    
    // Atualizar estatísticas
    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>
                   (end_time - start_time).count();
    
    stats_.total_checkpoints++;
    if (force_full || !config_.incremental) {
        stats_.full_checkpoints++;
    } else {
        stats_.incremental_checkpoints++;
    }
    
    stats_.avg_checkpoint_time_ms = (stats_.avg_checkpoint_time_ms * 
        (stats_.total_checkpoints - 1) + elapsed) / stats_.total_checkpoints;
    
    last_checkpoint_time_ = std::chrono::system_clock::now();
    
    return Status::OK;
}

Status CheckpointManager::createIncrementalCheckpoint() {
    return createCheckpoint(false);
}

Status CheckpointManager::recoverFromCheckpoint() {
    LogSequenceNumber checkpoint_lsn;
    std::vector<TransactionId> active_txs;
    
    if (!findLastCheckpoint(checkpoint_lsn, active_txs)) {
        return Status::NOT_FOUND;
    }
    
    // Redo a partir do checkpoint
    redoFromCheckpoint(checkpoint_lsn);
    
    return Status::OK;
}

LogSequenceNumber CheckpointManager::getLastCheckpointLSN() const {
    return last_checkpoint_lsn_;
}

std::chrono::system_clock::time_point CheckpointManager::getLastCheckpointTime() const {
    return last_checkpoint_time_;
}

void CheckpointManager::resetStats() {
    stats_ = CheckpointStats();
}

void CheckpointManager::checkpointLoop() {
    while (checkpoint_thread_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        if (shouldCreateCheckpoint()) {
            createCheckpoint();
        }
    }
}

bool CheckpointManager::shouldCreateCheckpoint() const {
    // Verificar intervalo de tempo
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>
                   (now - last_checkpoint_time_).count();
    
    if (elapsed >= config_.interval_seconds) {
        return true;
    }
    
    // Verificar tamanho do log
    // TODO: Obter tamanho atual do log
    size_t current_log_size = 0; // log_manager_->getCurrentSize();
    
    if (current_log_size >= config_.max_log_size) {
        return true;
    }
    
    return false;
}

void CheckpointManager::writeCheckpointRecord(bool is_full, 
                                               const std::vector<TransactionId>& active_txs) {
    // Preparar dados do checkpoint
    std::string data;
    
    if (is_full) {
        data = "FULL\n";
    } else {
        data = "INCR\n";
    }
    
    // Adicionar lista de transações ativas
    data += "ACTIVE:";
    for (size_t i = 0; i < active_txs.size(); i++) {
        if (i > 0) data += ",";
        data += std::to_string(active_txs[i]);
    }
    data += "\n";
    
    // Adicionar dirty pages
    data += "DIRTY:";
    // TODO: Lista de dirty pages
    data += "\n";
    
    // Escrever no log
    LogSequenceNumber lsn = log_manager_->appendLogRecord(
        LogRecordType::CHECKPOINT, 0, INVALID_PAGE_ID,
        data.c_str(), data.length() + 1);
    
    log_manager_->setCheckpoint(lsn);
    last_checkpoint_lsn_ = lsn;
}

void CheckpointManager::writeDirtyPages() {
    // Forçar flush de todas as páginas sujas
    buffer_pool_->flushAll();
    
    // Atualizar estatísticas
    // stats_.pages_written += count;
}

void CheckpointManager::truncateLogs() {
    // Remover logs anteriores ao último checkpoint
    log_manager_->truncate(last_checkpoint_lsn_);
    
    // Calcular bytes removidos
    // stats_.log_bytes_removed += bytes;
}

bool CheckpointManager::findLastCheckpoint(LogSequenceNumber& lsn,
                                           std::vector<TransactionId>& active_txs) {
    // Procurar registro de checkpoint mais recente
    auto logs = log_manager_->getLogs();
    
    for (auto it = logs.rbegin(); it != logs.rend(); ++it) {
        if (it->second.type == LogRecordType::CHECKPOINT) {
            lsn = it->first;
            
            // Parse dados do checkpoint
            if (it->second.data) {
                std::string data(it->second.data.get());
                
                // Extrair transações ativas
                size_t pos = data.find("ACTIVE:");
                if (pos != std::string::npos) {
                    pos += 7; // length of "ACTIVE:"
                    size_t end = data.find('\n', pos);
                    if (end != std::string::npos) {
                        std::string txs = data.substr(pos, end - pos);
                        size_t start = 0;
                        while (start < txs.length()) {
                            size_t comma = txs.find(',', start);
                            if (comma == std::string::npos) {
                                active_txs.push_back(std::stoull(txs.substr(start)));
                                break;
                            } else {
                                active_txs.push_back(std::stoull(
                                    txs.substr(start, comma - start)));
                                start = comma + 1;
                            }
                        }
                    }
                }
            }
            
            return true;
        }
    }
    
    return false;
}

void CheckpointManager::redoFromCheckpoint(LogSequenceNumber checkpoint_lsn) {
    // Obter todos os logs após o checkpoint
    auto logs = log_manager_->getLogsSince(checkpoint_lsn);
    
    // Aplicar REDO apenas para transações commitadas
    std::unordered_map<TransactionId, bool> committed;
    
    for (const auto& record : logs) {
        if (record.type == LogRecordType::COMMIT) {
            committed[record.transaction_id] = true;
        }
    }
    
    for (const auto& record : logs) {
        if (committed[record.transaction_id]) {
            // Aplicar operação
            switch (record.type) {
                case LogRecordType::INSERT:
                case LogRecordType::UPDATE:
                case LogRecordType::DELETE:
                    // TODO: Reaplicar operação
                    break;
                default:
                    break;
            }
        }
    }
}

} // namespace orangesql