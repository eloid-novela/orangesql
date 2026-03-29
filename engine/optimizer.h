#ifndef ORANGESQL_OPTIMIZER_H
#define ORANGESQL_OPTIMIZER_H

#include "executor_context.h"
#include "../parser/ast.h"
#include "../metadata/catalog.h"
#include "../index/btree.h"
#include <memory>
#include <vector>
#include <unordered_map>

namespace orangesql {

// Plano de execução
struct ExecutionPlan {
    // Estimativas
    struct Cost {
        double cpu_cost;
        double io_cost;
        double network_cost;
        double total_cost;
        size_t estimated_rows;
        
        Cost() : cpu_cost(0), io_cost(0), network_cost(0), 
                 total_cost(0), estimated_rows(0) {}
        
        Cost operator+(const Cost& other) const {
            Cost result;
            result.cpu_cost = cpu_cost + other.cpu_cost;
            result.io_cost = io_cost + other.io_cost;
            result.network_cost = network_cost + other.network_cost;
            result.total_cost = total_cost + other.total_cost;
            result.estimated_rows = estimated_rows + other.estimated_rows;
            return result;
        }
    };
    
    // Operadores do plano
    enum class OperatorType {
        SEQ_SCAN,
        INDEX_SCAN,
        FILTER,
        PROJECT,
        JOIN,
        AGGREGATE,
        SORT,
        LIMIT,
        HASH_JOIN,
        NESTED_LOOP_JOIN,
        MERGE_JOIN,
        UNION,
        INTERSECT,
        EXCEPT
    };
    
    struct PlanNode {
        OperatorType type;
        Cost cost;
        std::string description;
        std::unique_ptr<PlanNode> left;
        std::unique_ptr<PlanNode> right;
        std::vector<std::unique_ptr<PlanNode>> children;
        
        // Específico por tipo
        std::string table_name;
        std::string index_name;
        std::unique_ptr<ASTNode> filter_condition;
        std::vector<std::string> output_columns;
        std::vector<std::pair<std::string, bool>> order_by;  // column, asc
        size_t limit;
        
        PlanNode(OperatorType t) : type(t), limit(0) {}
    };
    
    std::unique_ptr<PlanNode> root;
    Cost total_cost;
};

// Estatísticas de tabela para otimização
struct TableStatistics {
    size_t row_count;
    size_t page_count;
    std::unordered_map<std::string, size_t> distinct_values;
    std::unordered_map<std::string, std::pair<Value, Value>> min_max_values;
    std::unordered_map<std::string, std::vector<std::pair<Value, double>>> histograms;
    
    TableStatistics() : row_count(0), page_count(0) {}
    
    // Estimativa de seletividade para uma condição
    double estimateSelectivity(const ASTNode* condition) const;
    
    // Estimativa de cardinalidade para uma coluna
    size_t estimateDistinctValues(const std::string& column) const;
};

// Otimizador de consultas
class QueryOptimizer {
public:
    QueryOptimizer(Catalog* catalog, ExecutorContext* context);
    
    // Gera plano de execução otimizado
    std::unique_ptr<ExecutionPlan> optimize(const ASTNode* query);
    
    // EXPLAIN
    std::string explainPlan(const ExecutionPlan* plan, bool analyze = false);
    
    // Atualiza estatísticas
    void updateStatistics(const std::string& table_name);
    
    // Habilita/desabilita técnicas de otimização
    void enableRule(const std::string& rule);
    void disableRule(const std::string& rule);
    
private:
    Catalog* catalog_;
    ExecutorContext* context_;
    std::unordered_map<std::string, TableStatistics> statistics_;
    
    // Regras de otimização
    struct OptimizationRule {
        std::string name;
        std::string description;
        bool enabled;
        std::function<std::unique_ptr<ExecutionPlan>(const ASTNode*)> apply;
    };
    
    std::vector<OptimizationRule> rules_;
    
    // Fases da otimização
    std::unique_ptr<ExecutionPlan> logicalOptimization(const ASTNode* query);
    std::unique_ptr<ExecutionPlan> physicalOptimization(std::unique_ptr<ExecutionPlan> logical);
    std::unique_ptr<ExecutionPlan> costBasedOptimization(std::unique_ptr<ExecutionPlan> plan);
    
    // Geração de alternativas
    std::vector<std::unique_ptr<ExecutionPlan>> generateAlternatives(const ASTNode* query);
    
    // Estimativas de custo
    ExecutionPlan::Cost estimateCost(const ExecutionPlan::PlanNode* node);
    double estimateSelectivity(const ASTNode* condition, const std::string& table);
    
    // Seleção de índices
    bool shouldUseIndex(const std::string& table, const std::string& column,
                        const ASTNode* condition);
    std::vector<std::string> findUsableIndexes(const std::string& table,
                                                const ASTNode* condition);
    
    // Reescrita de consultas
    std::unique_ptr<ASTNode> pushDownPredicates(std::unique_ptr<ASTNode> query);
    std::unique_ptr<ASTNode> eliminateCommonSubexpressions(std::unique_ptr<ASTNode> query);
    std::unique_ptr<ASTNode> optimizeJoins(std::unique_ptr<ASTNode> query);
    
    // Inicialização das regras
    void initializeRules();
    
    // Regras específicas
    std::unique_ptr<ExecutionPlan> applyRule_IndexScan(const ASTNode* query);
    std::unique_ptr<ExecutionPlan> applyRule_JoinReordering(const ASTNode* query);
    std::unique_ptr<ExecutionPlan> applyRule_PredicatePushdown(const ASTNode* query);
    std::unique_ptr<ExecutionPlan> applyRule_ProjectionPushdown(const ASTNode* query);
    std::unique_ptr<ExecutionPlan> applyRule_SubqueryUnnesting(const ASTNode* query);
    std::unique_ptr<ExecutionPlan> applyRule_ViewMerging(const ASTNode* query);
};

} // namespace orangesql

#endif // ORANGESQL_OPTIMIZER_H