#ifndef ORANGESQL_QUERY_EXECUTOR_H
#define ORANGESQL_QUERY_EXECUTOR_H

#include "executor_context.h"
#include "optimizer.h"
#include "../parser/ast.h"
#include "../storage/table.h"
#include "../storage/buffer_pool.h"
#include "../transaction/transaction_manager.h"
#include "../metadata/catalog.h"
#include <memory>
#include <vector>
#include <functional>

namespace orangesql {

// Interface para operadores do plano de execução
class ExecutorOperator {
public:
    virtual ~ExecutorOperator() = default;
    
    // Inicialização
    virtual Status open() = 0;
    
    // Próxima linha
    virtual Status next(std::vector<Value>& row) = 0;
    
    // Finalização
    virtual void close() = 0;
    
    // Resets
    virtual void rewind() = 0;
    
    // Estatísticas
    virtual const OperatorProfiler& getProfiler() const = 0;
    
    // Informações do operador
    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;
};

// Scan sequencial
class SeqScanOperator : public ExecutorOperator {
public:
    SeqScanOperator(ExecutorContext* context, const std::string& table_name,
                    std::unique_ptr<ASTNode> filter = nullptr);
    
    Status open() override;
    Status next(std::vector<Value>& row) override;
    void close() override;
    void rewind() override;
    
    const OperatorProfiler& getProfiler() const override { return profiler_; }
    std::string getName() const override { return "SeqScan"; }
    std::string getDescription() const override;
    
private:
    ExecutorContext* context_;
    std::string table_name_;
    std::unique_ptr<ASTNode> filter_;
    Table* table_;
    TableIterator iterator_;
    OperatorProfiler profiler_;
    
    bool evaluateFilter(const std::vector<Value>& row);
};

// Index scan
class IndexScanOperator : public ExecutorOperator {
public:
    IndexScanOperator(ExecutorContext* context, const std::string& table_name,
                      const std::string& index_name,
                      std::unique_ptr<ASTNode> filter = nullptr);
    
    Status open() override;
    Status next(std::vector<Value>& row) override;
    void close() override;
    void rewind() override;
    
    const OperatorProfiler& getProfiler() const override { return profiler_; }
    std::string getName() const override { return "IndexScan"; }
    std::string getDescription() const override;
    
private:
    ExecutorContext* context_;
    std::string table_name_;
    std::string index_name_;
    std::unique_ptr<ASTNode> filter_;
    Table* table_;
    std::unique_ptr<BTree<Value, RecordId>> index_;
    typename BTree<Value, RecordId>::Iterator index_iter_;
    OperatorProfiler profiler_;
};

// Filter (WHERE)
class FilterOperator : public ExecutorOperator {
public:
    FilterOperator(ExecutorContext* context, std::unique_ptr<ExecutorOperator> child,
                   std::unique_ptr<ASTNode> condition);
    
    Status open() override;
    Status next(std::vector<Value>& row) override;
    void close() override;
    void rewind() override;
    
    const OperatorProfiler& getProfiler() const override { return profiler_; }
    std::string getName() const override { return "Filter"; }
    std::string getDescription() const override;
    
private:
    ExecutorContext* context_;
    std::unique_ptr<ExecutorOperator> child_;
    std::unique_ptr<ASTNode> condition_;
    OperatorProfiler profiler_;
    
    bool evaluateCondition(const std::vector<Value>& row);
};

// Project (SELECT list)
class ProjectOperator : public ExecutorOperator {
public:
    ProjectOperator(ExecutorContext* context, std::unique_ptr<ExecutorOperator> child,
                    std::vector<std::unique_ptr<ASTNode>> expressions,
                    std::vector<std::string> aliases);
    
    Status open() override;
    Status next(std::vector<Value>& row) override;
    void close() override;
    void rewind() override;
    
    const OperatorProfiler& getProfiler() const override { return profiler_; }
    std::string getName() const override { return "Project"; }
    std::string getDescription() const override;
    
    const std::vector<std::string>& getOutputColumns() const { return aliases_; }
    
private:
    ExecutorContext* context_;
    std::unique_ptr<ExecutorOperator> child_;
    std::vector<std::unique_ptr<ASTNode>> expressions_;
    std::vector<std::string> aliases_;
    OperatorProfiler profiler_;
    
    Value evaluateExpression(const ASTNode* expr, const std::vector<Value>& row);
};

// Join
class JoinOperator : public ExecutorOperator {
public:
    enum class JoinAlgorithm {
        NESTED_LOOP,
        HASH,
        MERGE
    };
    
    JoinOperator(ExecutorContext* context, 
                 std::unique_ptr<ExecutorOperator> left,
                 std::unique_ptr<ExecutorOperator> right,
                 std::unique_ptr<ASTNode> condition,
                 JoinType type,
                 JoinAlgorithm algorithm = JoinAlgorithm::HASH);
    
    Status open() override;
    Status next(std::vector<Value>& row) override;
    void close() override;
    void rewind() override;
    
    const OperatorProfiler& getProfiler() const override { return profiler_; }
    std::string getName() const override;
    std::string getDescription() const override;
    
private:
    ExecutorContext* context_;
    std::unique_ptr<ExecutorOperator> left_;
    std::unique_ptr<ExecutorOperator> right_;
    std::unique_ptr<ASTNode> condition_;
    JoinType join_type_;
    JoinAlgorithm algorithm_;
    OperatorProfiler profiler_;
    
    // Estado para nested loop
    std::vector<Value> current_left_row_;
    std::vector<std::vector<Value>> right_rows_;
    size_t right_index_;
    
    // Hash join
    std::unordered_multimap<size_t, std::vector<Value>> hash_table_;
    
    bool evaluateJoinCondition(const std::vector<Value>& left_row,
                               const std::vector<Value>& right_row);
    size_t hashRow(const std::vector<Value>& row, const ASTNode* condition);
};

// Sort (ORDER BY)
class SortOperator : public ExecutorOperator {
public:
    struct SortKey {
        std::unique_ptr<ASTNode> expr;
        bool ascending;
    };
    
    SortOperator(ExecutorContext* context, std::unique_ptr<ExecutorOperator> child,
                 std::vector<SortKey> keys);
    
    Status open() override;
    Status next(std::vector<Value>& row) override;
    void close() override;
    void rewind() override;
    
    const OperatorProfiler& getProfiler() const override { return profiler_; }
    std::string getName() const override { return "Sort"; }
    std::string getDescription() const override;
    
private:
    ExecutorContext* context_;
    std::unique_ptr<ExecutorOperator> child_;
    std::vector<SortKey> keys_;
    std::vector<std::vector<Value>> sorted_rows_;
    size_t current_index_;
    OperatorProfiler profiler_;
    
    void sortRows();
    bool compareRows(const std::vector<Value>& a, const std::vector<Value>& b);
};

// Limit
class LimitOperator : public ExecutorOperator {
public:
    LimitOperator(ExecutorContext* context, std::unique_ptr<ExecutorOperator> child,
                  size_t limit, size_t offset = 0);
    
    Status open() override;
    Status next(std::vector<Value>& row) override;
    void close() override;
    void rewind() override;
    
    const OperatorProfiler& getProfiler() const override { return profiler_; }
    std::string getName() const override { return "Limit"; }
    std::string getDescription() const override;
    
private:
    ExecutorContext* context_;
    std::unique_ptr<ExecutorOperator> child_;
    size_t limit_;
    size_t offset_;
    size_t current_row_;
    OperatorProfiler profiler_;
};

// Aggregate (GROUP BY)
class AggregateOperator : public ExecutorOperator {
public:
    struct AggregateFunction {
        enum Type { COUNT, SUM, AVG, MIN, MAX };
        Type type;
        std::unique_ptr<ASTNode> expr;
    };
    
    AggregateOperator(ExecutorContext* context, std::unique_ptr<ExecutorOperator> child,
                      std::vector<std::unique_ptr<ASTNode>> group_by,
                      std::vector<AggregateFunction> aggregates);
    
    Status open() override;
    Status next(std::vector<Value>& row) override;
    void close() override;
    void rewind() override;
    
    const OperatorProfiler& getProfiler() const override { return profiler_; }
    std::string getName() const override { return "Aggregate"; }
    std::string getDescription() const override;
    
private:
    ExecutorContext* context_;
    std::unique_ptr<ExecutorOperator> child_;
    std::vector<std::unique_ptr<ASTNode>> group_by_;
    std::vector<AggregateFunction> aggregates_;
    
    struct GroupKey {
        std::vector<Value> values;
        bool operator==(const GroupKey& other) const;
    };
    
    struct GroupHash {
        size_t operator()(const GroupKey& key) const;
    };
    
    struct AggregateState {
        size_t count;
        double sum;
        Value min;
        Value max;
        
        AggregateState() : count(0), sum(0) {}
    };
    
    std::unordered_map<GroupKey, AggregateState, GroupHash> groups_;
    std::vector<std::pair<GroupKey, AggregateState>> results_;
    size_t current_index_;
    OperatorProfiler profiler_;
    
    void processRow(const std::vector<Value>& row);
};

// Executor principal
class QueryExecutor {
public:
    QueryExecutor(Catalog* catalog, BufferPool* buffer_pool, 
                  TransactionManager* tx_manager);
    
    ~QueryExecutor();
    
    // Execução de queries
    Status execute(const std::string& sql, std::vector<std::vector<Value>>& results);
    Status execute(const ASTNode* query, std::vector<std::vector<Value>>& results);
    
    // Prepared statements
    Status prepare(const std::string& sql, size_t& stmt_id);
    Status executePrepared(size_t stmt_id, 
                           const std::vector<Value>& params,
                           std::vector<std::vector<Value>>& results);
    void closePrepared(size_t stmt_id);
    
    // EXPLAIN
    std::string explain(const std::string& sql, bool analyze = false);
    
    // Estatísticas
    const ExecutionStats& getStats() const { return stats_; }
    void resetStats();
    
    // Configurações
    void setOptimizationLevel(OptimizationLevel level) { opt_level_ = level; }
    void setProfilingEnabled(bool enabled) { profiling_enabled_ = enabled; }
    
private:
    Catalog* catalog_;
    BufferPool* buffer_pool_;
    TransactionManager* tx_manager_;
    
    OptimizationLevel opt_level_;
    bool profiling_enabled_;
    ExecutionStats stats_;
    
    struct PreparedStatement {
        std::string sql;
        std::unique_ptr<ASTNode> ast;
        std::unique_ptr<ExecutionPlan> plan;
        std::vector<DataType> param_types;
    };
    std::unordered_map<size_t, PreparedStatement> prepared_statements_;
    size_t next_stmt_id_;
    
    // Execução interna
    Status executeSelect(const SelectNode* select, 
                         std::vector<std::vector<Value>>& results,
                         ExecutorContext* context);
    Status executeInsert(const InsertNode* insert, ExecutorContext* context);
    Status executeUpdate(const UpdateNode* update, ExecutorContext* context);
    Status executeDelete(const DeleteNode* delete_node, ExecutorContext* context);
    Status executeCreateTable(const CreateTableNode* create, ExecutorContext* context);
    Status executeCreateIndex(const CreateIndexNode* create, ExecutorContext* context);
    Status executeDropTable(const DropTableNode* drop, ExecutorContext* context);
    Status executeBegin(const BeginNode* begin, ExecutorContext* context);
    Status executeCommit(const CommitNode* commit, ExecutorContext* context);
    Status executeRollback(const RollbackNode* rollback, ExecutorContext* context);
    
    // Construção do plano de execução
    std::unique_ptr<ExecutorOperator> buildPlan(const ExecutionPlan::PlanNode* plan_node,
                                                ExecutorContext* context);
    
    // Avaliação de expressões
    Value evaluateExpression(const ASTNode* expr, const std::vector<Value>& row,
                            ExecutorContext* context);
    
    // Validação de tipos
    bool validateTypes(const ASTNode* expr, const TableSchema& schema);
    
    // Helpers
    Table* getTable(const std::string& name, ExecutorContext* context);
    IndexId getIndex(const std::string& table, const std::string& column);
};

} // namespace orangesql

#endif // ORANGESQL_QUERY_EXECUTOR_H