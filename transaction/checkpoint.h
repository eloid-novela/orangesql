#ifndef ORANGESQL_CHECKPOINT_H
#define ORANGESQL_CHECKPOINT_H

#include "log_manager.h"
#include "../storage/buffer_pool.h"
#include <chrono>

namespace orangesql {

// Configurações de checkpoint
struct CheckpointConfig {
    size_t interval_seconds;     // Intervalo entre checkpoints
    size_t min_log_size;         // Tamanho mínimo de log para checkpoint
    size_t max_log_size;         // Tamanho máximo antes de forçar checkpoint
    bool force_on_shutdown;      // Forçar checkpoint no shutdown
    bool incremental;            // Checkpoint incremental
    
    CheckpointConfig() : interval_seconds(300), min_log_size(10 * 1024 * 1024), // 10MB
                        max_log_size(100 * 1024 * 1024), // 100MB
                        force_on_shutdown(true), incremental(false) {}
};

// Estatísticas de checkpoint
struct CheckpointStats {
    size_t total_checkpoints;
    size_t full_checkpoints;
    size_t incremental_checkpoints;
    double avg_checkpoint_time_ms;
    size_t pages_written;
    size_t log_bytes_removed;
    
    CheckpointStats() : total_checkpoints(0), full_checkpoints(0),
                       incremental_checkpoints(0), avg_checkpoint_time_ms(0),
                       pages_written(0), log_bytes_removed(0) {}
};

// Gerenciador de checkpoints
class CheckpointManager {
public:
    CheckpointManager(LogManager* log_manager, BufferPool* buffer_pool,
                     TransactionManager* tx_manager);
    ~CheckpointManager();
    
    // Inicialização
    void init(const CheckpointConfig& config);
    void shutdown();
    
    // Checkpoints manuais
    Status createCheckpoint(bool force_full = false);
    Status createIncrementalCheckpoint();
    
    // Configuração
    void setConfig(const CheckpointConfig& config) { config_ = config; }
    CheckpointConfig getConfig() const { return config_; }
    
    // Estatísticas
    const CheckpointStats& getStats() const { return stats_; }
    void resetStats();
    
    // Recovery
    Status recoverFromCheckpoint();
    
    // Informações
    LogSequenceNumber getLastCheckpointLSN() const;
    std::chrono::system_clock::time_point getLastCheckpointTime() const;

private:
    LogManager* log_manager_;
    BufferPool* buffer_pool_;
    TransactionManager* tx_manager_;
    
    CheckpointConfig config_;
    CheckpointStats stats_;
    
    std::chrono::system_clock::time_point last_checkpoint_time_;
    LogSequenceNumber last_checkpoint_lsn_;
    
    mutable std::mutex mutex_;
    std::unique_ptr<std::thread> checkpoint_thread_;
    bool checkpoint_thread_running_;
    
    // Checkpoint interno
    void checkpointLoop();
    bool shouldCreateCheckpoint() const;
    
    // Escrita de checkpoint
    void writeCheckpointRecord(bool is_full, const std::vector<TransactionId>& active_txs);
    void writeDirtyPages();
    
    // Limpeza de logs
    void truncateLogs();
    
    // Recovery
    bool findLastCheckpoint(LogSequenceNumber& lsn, 
                            std::vector<TransactionId>& active_txs);
    void redoFromCheckpoint(LogSequenceNumber checkpoint_lsn);
};

} // namespace orangesql

#endif // ORANGESQL_CHECKPOINT_H