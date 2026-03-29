#ifndef ORANGESQL_EXECUTOR_CONTEXT_H
#define ORANGESQL_EXECUTOR_CONTEXT_H

#include "../include/types.h"
#include "../include/constants.h"
#include "../transaction/transaction_manager.h"
#include "../storage/buffer_pool.h"
#include "../metadata/catalog.h"
#include <memory>
#include <unordered_map>
#include <chrono>

namespace orangesql {

// Estatísticas de execução
struct ExecutionStats {
    size_t rows_scanned;
    size_t rows_returned;
    size_t pages_read;
    size_t pages_written;
    size_t index_lookups;
    double execution_time_ms;
    size_t memory_used_bytes;
    
    ExecutionStats() : rows_scanned(0), rows_returned(0), pages_read(0),
                       pages_written(0), index_lookups(0), execution_time_ms(0),
                       memory_used_bytes(0) {}
    
    void reset() {
        rows_scanned = 0;
        rows_returned = 0;
        pages_read = 0;
        pages_written = 0;
        index_lookups = 0;
        execution_time_ms = 0;
        memory_used_bytes = 0;
    }
    
    void merge(const ExecutionStats& other) {
        rows_scanned += other.rows_scanned;
        rows_returned += other.rows_returned;
        pages_read += other.pages_read;
        pages_written += other.pages_written;
        index_lookups += other.index_lookups;
        memory_used_bytes = std::max(memory_used_bytes, other.memory_used_bytes);
    }
};

// Modo de execução
enum class ExecutionMode {
    NORMAL,
    EXPLAIN,
    EXPLAIN_ANALYZE,
    PROFILE,
    DEBUG
};

// Nível de otimização
enum class OptimizationLevel {
    O0,  // Sem otimização
    O1,  // Otimizações básicas
    O2,  // Otimizações completas
    O3   // Otimizações agressivas
};

// Contexto de execução da query
class ExecutorContext {
public:
    ExecutorContext(Transaction* tx, Catalog* catalog, BufferPool* buffer_pool)
        : transaction_(tx)
        , catalog_(catalog)
        , buffer_pool_(buffer_pool)
        , mode_(ExecutionMode::NORMAL)
        , optimization_level_(OptimizationLevel::O2)
        , enable_profiling_(false)
        , max_rows_(-1)
        , statement_timeout_ms_(30000)  // 30 segundos default
        , start_time_(std::chrono::steady_clock::now()) {
    }
    
    // Getters
    Transaction* getTransaction() const { return transaction_; }
    Catalog* getCatalog() const { return catalog_; }
    BufferPool* getBufferPool() const { return buffer_pool_; }
    ExecutionMode getMode() const { return mode_; }
    OptimizationLevel getOptimizationLevel() const { return optimization_level_; }
    ExecutionStats& getStats() { return stats_; }
    bool isProfilingEnabled() const { return enable_profiling_; }
    int64_t getMaxRows() const { return max_rows_; }
    uint64_t getStatementTimeoutMs() const { return statement_timeout_ms_; }
    
    // Setters
    void setMode(ExecutionMode mode) { mode_ = mode; }
    void setOptimizationLevel(OptimizationLevel level) { optimization_level_ = level; }
    void setProfilingEnabled(bool enabled) { enable_profiling_ = enabled; }
    void setMaxRows(int64_t max) { max_rows_ = max; }
    void setStatementTimeout(uint64_t timeout_ms) { statement_timeout_ms_ = timeout_ms; }
    
    // Verificação de timeout
    bool checkTimeout() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>
                      (now - start_time_).count();
        return elapsed > statement_timeout_ms_;
    }
    
    // Reset do timer
    void resetTimer() {
        start_time_ = std::chrono::steady_clock::now();
    }
    
    // Alocação de memória temporária
    void* allocate(size_t size) {
        stats_.memory_used_bytes += size;
        // TODO: Implementar allocator real
        return malloc(size);
    }
    
    void deallocate(void* ptr, size_t size) {
        stats_.memory_used_bytes -= size;
        free(ptr);
    }
    
    // Tabelas temporárias
    struct TempTable {
        std::string name;
        TableSchema schema;
        std::vector<std::vector<Value>> rows;
    };
    
    TempTable* createTempTable(const std::string& name, const TableSchema& schema) {
        auto table = std::make_unique<TempTable>();
        table->name = name;
        table->schema = schema;
        temp_tables_[name] = std::move(table);
        return temp_tables_[name].get();
    }
    
    TempTable* getTempTable(const std::string& name) {
        auto it = temp_tables_.find(name);
        return it != temp_tables_.end() ? it->second.get() : nullptr;
    }
    
    void dropTempTable(const std::string& name) {
        temp_tables_.erase(name);
    }
    
    // Parâmetros de query preparada
    void setParameter(const std::string& name, const Value& value) {
        parameters_[name] = value;
    }
    
    Value getParameter(const std::string& name) const {
        auto it = parameters_.find(name);
        if (it != parameters_.end()) {
            return it->second;
        }
        return Value(); // NULL
    }
    
    bool hasParameter(const std::string& name) const {
        return parameters_.find(name) != parameters_.end();
    }
    
    // Cache de resultados
    void cacheResult(const std::string& query_hash, 
                     const std::vector<std::vector<Value>>& result) {
        if (result_cache_.size() > 100) { // Limite simples
            result_cache_.clear();
        }
        result_cache_[query_hash] = result;
    }
    
    bool getCachedResult(const std::string& query_hash,
                         std::vector<std::vector<Value>>& result) {
        auto it = result_cache_.find(query_hash);
        if (it != result_cache_.end()) {
            result = it->second;
            return true;
        }
        return false;
    }
    
    void clearCache() {
        result_cache_.clear();
    }
    
private:
    Transaction* transaction_;
    Catalog* catalog_;
    BufferPool* buffer_pool_;
    
    ExecutionMode mode_;
    OptimizationLevel optimization_level_;
    bool enable_profiling_;
    int64_t max_rows_;
    uint64_t statement_timeout_ms_;
    
    ExecutionStats stats_;
    std::chrono::steady_clock::time_point start_time_;
    
    std::unordered_map<std::string, std::unique_ptr<TempTable>> temp_tables_;
    std::unordered_map<std::string, Value> parameters_;
    std::unordered_map<std::string, std::vector<std::vector<Value>>> result_cache_;
};

// Classe para perfilamento de operadores
class OperatorProfiler {
public:
    void start() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }
    
    void stop() {
        auto end_time = std::chrono::high_resolution_clock::now();
        elapsed_ms_ = std::chrono::duration<double, std::milli>
                     (end_time - start_time_).count();
    }
    
    void recordRow() { rows_++; }
    void recordPage() { pages_++; }
    void recordIndexLookup() { index_lookups_++; }
    
    double getElapsedMs() const { return elapsed_ms_; }
    size_t getRows() const { return rows_; }
    size_t getPages() const { return pages_; }
    size_t getIndexLookups() const { return index_lookups_; }
    
    void reset() {
        elapsed_ms_ = 0;
        rows_ = 0;
        pages_ = 0;
        index_lookups_ = 0;
    }
    
private:
    std::chrono::high_resolution_clock::time_point start_time_;
    double elapsed_ms_ = 0;
    size_t rows_ = 0;
    size_t pages_ = 0;
    size_t index_lookups_ = 0;
};

} // namespace orangesql

#endif // ORANGESQL_EXECUTOR_CONTEXT_H