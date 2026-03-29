#include "optimizer.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace orangesql {

QueryOptimizer::QueryOptimizer(Catalog* catalog, ExecutorContext* context)
    : catalog_(catalog), context_(context) {
    initializeRules();
    // Carregar estatísticas existentes
    for (const auto& [name, schema] : catalog_->getAllTables()) {
        updateStatistics(name);
    }
}

void QueryOptimizer::initializeRules() {
    rules_ = {
        {
            "index_scan",
            "Usa índice quando disponível",
            true,
            [this](const ASTNode* query) { return applyRule_IndexScan(query); }
        },
        {
            "join_reordering",
            "Reordena joins para minimizar custo",
            true,
            [this](const ASTNode* query) { return applyRule_JoinReordering(query); }
        },
        {
            "predicate_pushdown",
            "Empurra predicados para baixo na árvore",
            true,
            [this](const ASTNode* query) { return applyRule_PredicatePushdown(query); }
        },
        {
            "projection_pushdown",
            "Projeta colunas o mais cedo possível",
            true,
            [this](const ASTNode* query) { return applyRule_ProjectionPushdown(query); }
        },
        {
            "subquery_unnesting",
            "Transforma subconsultas em joins",
            true,
            [this](const ASTNode* query) { return applyRule_SubqueryUnnesting(query); }
        },
        {
            "view_merging",
            "Mescla visualizações na consulta principal",
            true,
            [this](const ASTNode* query) { return applyRule_ViewMerging(query); }
        }
    };
}

std::unique_ptr<ExecutionPlan> QueryOptimizer::optimize(const ASTNode* query) {
    // EXPLAIN apenas retorna o plano sem executar
    if (context_->getMode() == ExecutionMode::EXPLAIN) {
        auto plan = logicalOptimization(query);
        return plan;
    }
    
    // Otimização lógica
    auto logical_plan = logicalOptimization(query);
    
    // Aplicar regras de otimização
    if (context_->getOptimizationLevel() >= OptimizationLevel::O1) {
        for (const auto& rule : rules_) {
            if (rule.enabled) {
                auto alternative = rule.apply(query);
                if (alternative && alternative->total_cost.total_cost < 
                                   logical_plan->total_cost.total_cost) {
                    logical_plan = std::move(alternative);
                }
            }
        }
    }
    
    // Otimização física (escolha de algoritmos)
    auto physical_plan = physicalOptimization(std::move(logical_plan));
    
    // Otimização baseada em custo (se nível >= O2)
    if (context_->getOptimizationLevel() >= OptimizationLevel::O2) {
        physical_plan = costBasedOptimization(std::move(physical_plan));
    }
    
    return physical_plan;
}

std::unique_ptr<ExecutionPlan> QueryOptimizer::logicalOptimization(const ASTNode* query) {
    auto plan = std::make_unique<ExecutionPlan>();
    
    if (query->type == ASTNodeType::SELECT) {
        auto select = static_cast<const SelectNode*>(query);
        
        // Criar nó raiz (PROJECT)
        auto project_node = std::make_unique<ExecutionPlan::PlanNode>
            (ExecutionPlan::OperatorType::PROJECT);
        
        // Determinar fonte de dados
        std::unique_ptr<ExecutionPlan::PlanNode> source;
        
        if (!select->from_tables.empty()) {
            const auto& table_ref = select->from_tables[0];
            
            // Verificar se pode usar índice
            if (!select->where) {
                // Sem WHERE, usa scan sequencial
                source = std::make_unique<ExecutionPlan::PlanNode>
                    (ExecutionPlan::OperatorType::SEQ_SCAN);
                source->table_name = table_ref.table_name;
            } else {
                // Com WHERE, avaliar uso de índice
                auto usable_indexes = findUsableIndexes(table_ref.table_name, 
                                                        select->where->condition.get());
                if (!usable_indexes.empty() && 
                    shouldUseIndex(table_ref.table_name, usable_indexes[0], 
                                   select->where->condition.get())) {
                    source = std::make_unique<ExecutionPlan::PlanNode>
                        (ExecutionPlan::OperatorType::INDEX_SCAN);
                    source->table_name = table_ref.table_name;
                    source->index_name = usable_indexes[0];
                    source->filter_condition = select->where->condition->clone();
                } else {
                    source = std::make_unique<ExecutionPlan::PlanNode>
                        (ExecutionPlan::OperatorType::SEQ_SCAN);
                    source->table_name = table_ref.table_name;
                    source->filter_condition = select->where->condition->clone();
                }
            }
            
            project_node->left = std::move(source);
        }
        
        // Adicionar outros operadores (joins, etc)
        for (const auto& join : select->joins) {
            auto join_node = std::make_unique<ExecutionPlan::PlanNode>
                (ExecutionPlan::OperatorType::JOIN);
            join_node->left = std::move(project_node->left);
            // TODO: Adicionar right table
            project_node->left = std::move(join_node);
        }
        
        // ORDER BY
        if (select->order_by) {
            auto sort_node = std::make_unique<ExecutionPlan::PlanNode>
                (ExecutionPlan::OperatorType::SORT);
            for (const auto& item : select->order_by->items) {
                // TODO: Extrair nome da coluna
                sort_node->order_by.emplace_back("", item.ascending);
            }
            sort_node->left = std::move(project_node);
            project_node = std::move(sort_node);
        }
        
        // LIMIT
        if (select->limit && select->limit->limit > 0) {
            auto limit_node = std::make_unique<ExecutionPlan::PlanNode>
                (ExecutionPlan::OperatorType::LIMIT);
            limit_node->limit = select->limit->limit;
            limit_node->left = std::move(project_node);
            project_node = std::move(limit_node);
        }
        
        plan->root = std::move(project_node);
    }
    
    // Estimar custo
    plan->total_cost = estimateCost(plan->root.get());
    
    return plan;
}

std::unique_ptr<ExecutionPlan> QueryOptimizer::physicalOptimization(
    std::unique_ptr<ExecutionPlan> logical) {
    // Converter operadores lógicos em físicos
    // Ex: JOIN -> HASH_JOIN, NESTED_LOOP_JOIN, etc
    
    std::function<void(ExecutionPlan::PlanNode*)> convert = 
        [&](ExecutionPlan::PlanNode* node) {
        if (!node) return;
        
        if (node->type == ExecutionPlan::OperatorType::JOIN) {
            // Escolher algoritmo de join baseado no tamanho estimado
            auto left_cost = estimateCost(node->left.get());
            auto right_cost = estimateCost(node->right.get());
            
            if (left_cost.estimated_rows < 100 || right_cost.estimated_rows < 100) {
                node->type = ExecutionPlan::OperatorType::NESTED_LOOP_JOIN;
            } else if (left_cost.estimated_rows * right_cost.estimated_rows < 1000000) {
                node->type = ExecutionPlan::OperatorType::HASH_JOIN;
            } else {
                node->type = ExecutionPlan::OperatorType::MERGE_JOIN;
            }
        }
        
        convert(node->left.get());
        convert(node->right.get());
        for (auto& child : node->children) {
            convert(child.get());
        }
    };
    
    convert(logical->root.get());
    return logical;
}

std::unique_ptr<ExecutionPlan> QueryOptimizer::costBasedOptimization(
    std::unique_ptr<ExecutionPlan> plan) {
    // Gerar alternativas e escolher a de menor custo
    auto alternatives = generateAlternatives(nullptr); // TODO: Passar query
    
    if (alternatives.empty()) {
        return plan;
    }
    
    // Escolher melhor plano
    std::unique_ptr<ExecutionPlan> best = std::move(plan);
    double best_cost = best->total_cost.total_cost;
    
    for (auto& alt : alternatives) {
        alt->total_cost = estimateCost(alt->root.get());
        if (alt->total_cost.total_cost < best_cost) {
            best = std::move(alt);
            best_cost = best->total_cost.total_cost;
        }
    }
    
    return best;
}

std::vector<std::unique_ptr<ExecutionPlan>> QueryOptimizer::generateAlternatives(
    const ASTNode* query) {
    std::vector<std::unique_ptr<ExecutionPlan>> alternatives;
    
    // Gerar diferentes ordens de join
    // Gerar diferentes escolhas de índices
    // Gerar diferentes algoritmos
    
    return alternatives;
}

ExecutionPlan::Cost QueryOptimizer::estimateCost(const ExecutionPlan::PlanNode* node) {
    if (!node) return ExecutionPlan::Cost();
    
    ExecutionPlan::Cost cost;
    
    // Custos base por operador
    switch (node->type) {
        case ExecutionPlan::OperatorType::SEQ_SCAN: {
            auto stats = statistics_[node->table_name];
            cost.estimated_rows = stats.row_count;
            cost.io_cost = stats.page_count * 1.0; // 1 I/O por página
            cost.cpu_cost = stats.row_count * 0.01; // CPU por linha
            break;
        }
        
        case ExecutionPlan::OperatorType::INDEX_SCAN: {
            auto stats = statistics_[node->table_name];
            // Índice reduz I/O
            double selectivity = 1.0;
            if (node->filter_condition) {
                selectivity = estimateSelectivity(node->filter_condition.get(), 
                                                  node->table_name);
            }
            cost.estimated_rows = stats.row_count * selectivity;
            cost.io_cost = stats.page_count * selectivity * 0.1; // Menos I/O
            cost.cpu_cost = stats.row_count * selectivity * 0.05;
            break;
        }
        
        case ExecutionPlan::OperatorType::FILTER: {
            auto child_cost = estimateCost(node->left.get());
            double selectivity = 1.0;
            if (node->filter_condition) {
                selectivity = estimateSelectivity(node->filter_condition.get(), 
                                                  node->table_name);
            }
            cost.estimated_rows = child_cost.estimated_rows * selectivity;
            cost.io_cost = child_cost.io_cost;
            cost.cpu_cost = child_cost.cpu_cost + child_cost.estimated_rows * 0.01;
            break;
        }
        
        case ExecutionPlan::OperatorType::JOIN:
        case ExecutionPlan::OperatorType::HASH_JOIN:
        case ExecutionPlan::OperatorType::NESTED_LOOP_JOIN:
        case ExecutionPlan::OperatorType::MERGE_JOIN: {
            auto left_cost = estimateCost(node->left.get());
            auto right_cost = estimateCost(node->right.get());
            
            if (node->type == ExecutionPlan::OperatorType::NESTED_LOOP_JOIN) {
                cost.estimated_rows = left_cost.estimated_rows * right_cost.estimated_rows;
                cost.io_cost = left_cost.io_cost + 
                              left_cost.estimated_rows * right_cost.io_cost;
                cost.cpu_cost = left_cost.cpu_cost + 
                               left_cost.estimated_rows * right_cost.cpu_cost;
            } else if (node->type == ExecutionPlan::OperatorType::HASH_JOIN) {
                cost.estimated_rows = left_cost.estimated_rows * right_cost.estimated_rows;
                cost.io_cost = left_cost.io_cost + right_cost.io_cost;
                cost.cpu_cost = left_cost.cpu_cost + right_cost.cpu_cost + 
                               left_cost.estimated_rows * 0.1;
            } else if (node->type == ExecutionPlan::OperatorType::MERGE_JOIN) {
                cost.estimated_rows = left_cost.estimated_rows * right_cost.estimated_rows;
                cost.io_cost = left_cost.io_cost + right_cost.io_cost;
                cost.cpu_cost = left_cost.cpu_cost + right_cost.cpu_cost + 
                               (left_cost.estimated_rows + right_cost.estimated_rows) * 0.05;
            }
            break;
        }
        
        case ExecutionPlan::OperatorType::SORT: {
            auto child_cost = estimateCost(node->left.get());
            cost.estimated_rows = child_cost.estimated_rows;
            cost.io_cost = child_cost.io_cost * 2; // Sort可能需要额外I/O
            cost.cpu_cost = child_cost.cpu_cost + 
                           child_cost.estimated_rows * log(child_cost.estimated_rows) * 0.01;
            break;
        }
        
        case ExecutionPlan::OperatorType::LIMIT: {
            auto child_cost = estimateCost(node->left.get());
            cost.estimated_rows = std::min(child_cost.estimated_rows, node->limit);
            cost.io_cost = child_cost.io_cost * 
                          (static_cast<double>(cost.estimated_rows) / child_cost.estimated_rows);
            cost.cpu_cost = child_cost.cpu_cost * 
                          (static_cast<double>(cost.estimated_rows) / child_cost.estimated_rows);
            break;
        }
        
        default:
            cost = estimateCost(node->left.get());
    }
    
    // Calcular custo total (fórmula simples)
    cost.total_cost = cost.io_cost * 10.0 + cost.cpu_cost; // I/O 10x mais caro
    
    return cost;
}

double QueryOptimizer::estimateSelectivity(const ASTNode* condition, 
                                           const std::string& table) {
    // Estimativa simples de seletividade
    if (!condition) return 1.0;
    
    auto stats = statistics_[table];
    
    if (condition->type == ASTNodeType::BINARY_EXPR) {
        auto binary = static_cast<const BinaryExprNode*>(condition);
        
        // EQ: 1 / distinct_values
        if (binary->op == Operator::EQ) {
            if (binary->left->type == ASTNodeType::COLUMN_REF) {
                auto col = static_cast<const ColumnRefNode*>(binary->left.get());
                size_t distinct = stats.estimateDistinctValues(col->column);
                if (distinct > 0) {
                    return 1.0 / distinct;
                }
            }
            return 0.1; // Estimativa padrão
        }
        
        // LIKE: estimativa mais complexa
        if (binary->op == Operator::LIKE) {
            return 0.05; // LIKE geralmente seleciona poucas linhas
        }
        
        // AND: produto das seletividades
        if (binary->op == Operator::AND) {
            double left_sel = estimateSelectivity(binary->left.get(), table);
            double right_sel = estimateSelectivity(binary->right.get(), table);
            return left_sel * right_sel;
        }
        
        // OR: soma - produto
        if (binary->op == Operator::OR) {
            double left_sel = estimateSelectivity(binary->left.get(), table);
            double right_sel = estimateSelectivity(binary->right.get(), table);
            return left_sel + right_sel - left_sel * right_sel;
        }
    }
    
    // Comparações de intervalo
    if (condition->type == ASTNodeType::BINARY_EXPR) {
        auto binary = static_cast<const BinaryExprNode*>(condition);
        if (binary->op == Operator::LT || binary->op == Operator::LE ||
            binary->op == Operator::GT || binary->op == Operator::GE) {
            return 0.3; // Intervalos típicos selecionam ~30%
        }
    }
    
    return 0.5; // Valor padrão para condições desconhecidas
}

bool QueryOptimizer::shouldUseIndex(const std::string& table, 
                                    const std::string& column,
                                    const ASTNode* condition) {
    auto stats = statistics_[table];
    
    // Estimar seletividade da condição
    double selectivity = estimateSelectivity(condition, table);
    
    // Usar índice se a seletividade for baixa (< 20%)
    return selectivity < INDEX_SELECTIVITY_THRESHOLD;
}

std::vector<std::string> QueryOptimizer::findUsableIndexes(const std::string& table,
                                                            const ASTNode* condition) {
    std::vector<std::string> indexes;
    
    auto* schema = catalog_->getTable(table);
    if (!schema) return indexes;
    
    // Procurar índices que podem ser usados
    for (const auto& [col_name, index_id] : schema->indexes) {
        // Verificar se a coluna aparece na condição
        std::function<bool(const ASTNode*, const std::string&)> containsColumn = 
            [&](const ASTNode* node, const std::string& col) -> bool {
            if (!node) return false;
            
            if (node->type == ASTNodeType::COLUMN_REF) {
                auto col_ref = static_cast<const ColumnRefNode*>(node);
                return col_ref->column == col;
            }
            
            if (node->type == ASTNodeType::BINARY_EXPR) {
                auto binary = static_cast<const BinaryExprNode*>(node);
                return containsColumn(binary->left.get(), col) ||
                       containsColumn(binary->right.get(), col);
            }
            
            return false;
        };
        
        if (containsColumn(condition, col_name)) {
            indexes.push_back(col_name);
        }
    }
    
    return indexes;
}

void QueryOptimizer::updateStatistics(const std::string& table_name) {
    auto* schema = catalog_->getTable(table_name);
    if (!schema) return;
    
    TableStatistics stats;
    
    // TODO: Coletar estatísticas reais da tabela
    // Por enquanto, usar valores simulados
    stats.row_count = 10000; // 10k linhas
    stats.page_count = 100;   // 100 páginas
    
    for (const auto& col : schema->columns) {
        stats.distinct_values[col.name] = 100; // 100 valores distintos
    }
    
    statistics_[table_name] = stats;
}

std::string QueryOptimizer::explainPlan(const ExecutionPlan* plan, bool analyze) {
    std::stringstream ss;
    
    std::function<void(const ExecutionPlan::PlanNode*, int)> printNode = 
        [&](const ExecutionPlan::PlanNode* node, int depth) {
        if (!node) return;
        
        // Indentação
        ss << std::string(depth * 2, ' ');
        
        // Nome do operador
        switch (node->type) {
            case ExecutionPlan::OperatorType::SEQ_SCAN:
                ss << "Seq Scan on " << node->table_name;
                break;
            case ExecutionPlan::OperatorType::INDEX_SCAN:
                ss << "Index Scan using " << node->index_name 
                   << " on " << node->table_name;
                break;
            case ExecutionPlan::OperatorType::FILTER:
                ss << "Filter";
                break;
            case ExecutionPlan::OperatorType::PROJECT:
                ss << "Project";
                break;
            case ExecutionPlan::OperatorType::JOIN:
                ss << "Join";
                break;
            case ExecutionPlan::OperatorType::HASH_JOIN:
                ss << "Hash Join";
                break;
            case ExecutionPlan::OperatorType::NESTED_LOOP_JOIN:
                ss << "Nested Loop Join";
                break;
            case ExecutionPlan::OperatorType::MERGE_JOIN:
                ss << "Merge Join";
                break;
            case ExecutionPlan::OperatorType::SORT:
                ss << "Sort";
                if (!node->order_by.empty()) {
                    ss << " (";
                    for (size_t i = 0; i < node->order_by.size(); i++) {
                        if (i > 0) ss << ", ";
                        ss << node->order_by[i].first 
                           << (node->order_by[i].second ? " ASC" : " DESC");
                    }
                    ss << ")";
                }
                break;
            case ExecutionPlan::OperatorType::LIMIT:
                ss << "Limit (" << node->limit << ")";
                break;
            case ExecutionPlan::OperatorType::AGGREGATE:
                ss << "Aggregate";
                break;
        }
        
        // Estatísticas
        if (analyze) {
            auto cost = estimateCost(node);
            ss << "  (cost=" << std::fixed << std::setprecision(2) 
               << cost.total_cost << " rows=" << cost.estimated_rows << ")";
        }
        
        ss << "\n";
        
        // Filhos
        printNode(node->left.get(), depth + 1);
        printNode(node->right.get(), depth + 1);
        for (const auto& child : node->children) {
            printNode(child.get(), depth + 1);
        }
    };
    
    printNode(plan->root.get(), 0);
    
    if (analyze) {
        ss << "\nTotal cost: " << std::fixed << std::setprecision(2) 
           << plan->total_cost.total_cost << "\n";
    }
    
    return ss.str();
}

// Implementações das regras de otimização
std::unique_ptr<ExecutionPlan> QueryOptimizer::applyRule_IndexScan(const ASTNode* query) {
    // Já implementado no logicalOptimization
    return nullptr;
}

std::unique_ptr<ExecutionPlan> QueryOptimizer::applyRule_JoinReordering(const ASTNode* query) {
    // TODO: Implementar reordenação de joins
    return nullptr;
}

std::unique_ptr<ExecutionPlan> QueryOptimizer::applyRule_PredicatePushdown(const ASTNode* query) {
    // TODO: Implementar pushdown de predicados
    return nullptr;
}

std::unique_ptr<ExecutionPlan> QueryOptimizer::applyRule_ProjectionPushdown(const ASTNode* query) {
    // TODO: Implementar pushdown de projeções
    return nullptr;
}

std::unique_ptr<ExecutionPlan> QueryOptimizer::applyRule_SubqueryUnnesting(const ASTNode* query) {
    // TODO: Implementar unnesting de subconsultas
    return nullptr;
}

std::unique_ptr<ExecutionPlan> QueryOptimizer::applyRule_ViewMerging(const ASTNode* query) {
    // TODO: Implementar merging de views
    return nullptr;
}

} // namespace orangesql