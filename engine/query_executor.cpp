#include "query_executor.h"
#include "../parser/sql_parser.h"
#include <chrono>
#include <cmath>
#include <algorithm>

namespace orangesql {

// ============================================
// QueryExecutor
// ============================================

QueryExecutor::QueryExecutor(Catalog* catalog, BufferPool* buffer_pool, 
                             TransactionManager* tx_manager)
    : catalog_(catalog)
    , buffer_pool_(buffer_pool)
    , tx_manager_(tx_manager)
    , opt_level_(OptimizationLevel::O2)
    , profiling_enabled_(false)
    , next_stmt_id_(1) {
}

QueryExecutor::~QueryExecutor() {
    // Liberar recursos
    for (auto& [id, stmt] : prepared_statements_) {
        // TODO: Limpar recursos
    }
}

Status QueryExecutor::execute(const std::string& sql, 
                              std::vector<std::vector<Value>>& results) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // Parse SQL
        SQLParser parser;
        auto ast = parser.parse(sql);
        
        if (!ast) {
            return Status::ERROR;
        }
        
        // Executar
        auto status = execute(ast.get(), results);
        
        // Atualizar estatísticas
        auto end_time = std::chrono::high_resolution_clock::now();
        stats_.execution_time_ms = std::chrono::duration<double, std::milli>
                                   (end_time - start_time).count();
        stats_.rows_returned = results.size();
        
        return status;
        
    } catch (const std::exception& e) {
        // Log error
        return Status::ERROR;
    }
}

Status QueryExecutor::execute(const ASTNode* query, 
                              std::vector<std::vector<Value>>& results) {
    // Iniciar transação se não existir
    Transaction* tx = tx_manager_->begin();
    
    // Criar contexto de execução
    ExecutorContext context(tx, catalog_, buffer_pool_);
    context.setOptimizationLevel(opt_level_);
    context.setProfilingEnabled(profiling_enabled_);
    
    Status status;
    
    switch (query->type) {
        case ASTNodeType::SELECT:
            status = executeSelect(static_cast<const SelectNode*>(query), 
                                   results, &context);
            break;
            
        case ASTNodeType::INSERT:
            status = executeInsert(static_cast<const InsertNode*>(query), &context);
            break;
            
        case ASTNodeType::UPDATE:
            status = executeUpdate(static_cast<const UpdateNode*>(query), &context);
            break;
            
        case ASTNodeType::DELETE:
            status = executeDelete(static_cast<const DeleteNode*>(query), &context);
            break;
            
        case ASTNodeType::CREATE_TABLE:
            status = executeCreateTable(static_cast<const CreateTableNode*>(query), 
                                        &context);
            break;
            
        case ASTNodeType::CREATE_INDEX:
            status = executeCreateIndex(static_cast<const CreateIndexNode*>(query), 
                                        &context);
            break;
            
        case ASTNodeType::DROP_TABLE:
            status = executeDropTable(static_cast<const DropTableNode*>(query), 
                                      &context);
            break;
            
        case ASTNodeType::BEGIN:
            status = executeBegin(static_cast<const BeginNode*>(query), &context);
            break;
            
        case ASTNodeType::COMMIT:
            status = executeCommit(static_cast<const CommitNode*>(query), &context);
            break;
            
        case ASTNodeType::ROLLBACK:
            status = executeRollback(static_cast<const RollbackNode*>(query), &context);
            break;
            
        default:
            status = Status::ERROR;
    }
    
    // Finalizar transação se foi criada aqui
    if (status == Status::OK) {
        tx_manager_->commit(tx);
    } else {
        tx_manager_->abort(tx);
    }
    
    // Mesclar estatísticas
    stats_.merge(context.getStats());
    
    return status;
}

Status QueryExecutor::executeSelect(const SelectNode* select,
                                     std::vector<std::vector<Value>>& results,
                                     ExecutorContext* context) {
    // Otimizar consulta
    QueryOptimizer optimizer(catalog_, context);
    auto plan = optimizer.optimize(select);
    
    if (context->getMode() == ExecutionMode::EXPLAIN) {
        // Apenas retornar o plano
        std::vector<Value> row;
        row.emplace_back(optimizer.explainPlan(plan.get(), false));
        results.push_back(row);
        return Status::OK;
    }
    
    if (context->getMode() == ExecutionMode::EXPLAIN_ANALYZE) {
        // Executar e analisar
        context->setProfilingEnabled(true);
    }
    
    // Construir operadores
    auto root = buildPlan(plan->root.get(), context);
    
    if (!root) {
        return Status::ERROR;
    }
    
    // Executar
    Status status = root->open();
    if (status != Status::OK) {
        return status;
    }
    
    std::vector<Value> row;
    while ((status = root->next(row)) == Status::OK) {
        results.push_back(row);
        
        // Verificar limite máximo
        if (context->getMaxRows() > 0 && results.size() >= context->getMaxRows()) {
            break;
        }
        
        // Verificar timeout
        if (context->checkTimeout()) {
            status = Status::ERROR;
            break;
        }
    }
    
    root->close();
    
    if (context->getMode() == ExecutionMode::EXPLAIN_ANALYZE) {
        // Incluir estatísticas nos resultados
        std::string analysis = optimizer.explainPlan(plan.get(), true);
        results.clear();
        std::vector<Value> row;
        row.emplace_back(analysis);
        results.push_back(row);
    }
    
    return Status::OK;
}

Status QueryExecutor::executeInsert(const InsertNode* insert,
                                     ExecutorContext* context) {
    auto* table = getTable(insert->table, context);
    if (!table) {
        return Status::NOT_FOUND;
    }
    
    size_t inserted = 0;
    
    for (const auto& row_values : insert->values) {
        std::vector<Value> values;
        
        // Avaliar expressões
        for (const auto& expr : row_values) {
            values.push_back(evaluateExpression(expr.get(), {}, context));
        }
        
        // Inserir
        RecordId rid;
        Status status = table->insertRecord(values, rid);
        
        if (status != Status::OK) {
            return status;
        }
        
        // Log para transação
        context->getTransaction()->addModifiedPage(rid >> 32);
        inserted++;
    }
    
    context->getStats().rows_scanned = inserted;
    
    return Status::OK;
}

Status QueryExecutor::executeUpdate(const UpdateNode* update,
                                     ExecutorContext* context) {
    auto* table = getTable(update->table, context);
    if (!table) {
        return Status::NOT_FOUND;
    }
    
    size_t updated = 0;
    
    // Scan na tabela
    for (auto it = table->begin(); it != table->end(); ++it) {
        auto [rid, row] = *it;
        
        // Avaliar condição WHERE
        if (update->where) {
            bool matches = evaluateExpression(update->where->condition.get(), 
                                             row, context).data.bool_val;
            if (!matches) {
                continue;
            }
        }
        
        // Aplicar updates
        std::vector<Value> new_row = row;
        for (const auto& assign : update->assignments) {
            // Encontrar coluna
            size_t col_index = 0;
            for (size_t i = 0; i < table->getSchema().columns.size(); i++) {
                if (table->getSchema().columns[i].name == assign.column) {
                    col_index = i;
                    break;
                }
            }
            
            new_row[col_index] = evaluateExpression(assign.value.get(), row, context);
        }
        
        // Atualizar
        Status status = table->updateRecord(rid, new_row);
        
        if (status != Status::OK) {
            return status;
        }
        
        context->getTransaction()->addModifiedPage(rid >> 32);
        updated++;
    }
    
    context->getStats().rows_scanned = updated;
    
    return Status::OK;
}

Status QueryExecutor::executeDelete(const DeleteNode* delete_node,
                                     ExecutorContext* context) {
    auto* table = getTable(delete_node->table, context);
    if (!table) {
        return Status::NOT_FOUND;
    }
    
    size_t deleted = 0;
    
    // Scan na tabela
    for (auto it = table->begin(); it != table->end(); ++it) {
        auto [rid, row] = *it;
        
        // Avaliar condição WHERE
        if (delete_node->where) {
            bool matches = evaluateExpression(delete_node->where->condition.get(), 
                                             row, context).data.bool_val;
            if (!matches) {
                continue;
            }
        }
        
        // Deletar
        Status status = table->deleteRecord(rid);
        
        if (status != Status::OK) {
            return status;
        }
        
        context->getTransaction()->addModifiedPage(rid >> 32);
        deleted++;
    }
    
    context->getStats().rows_scanned = deleted;
    
    return Status::OK;
}

Status QueryExecutor::executeCreateTable(const CreateTableNode* create,
                                          ExecutorContext* context) {
    // Criar schema
    TableSchema schema;
    schema.id = catalog_->getNextTableId();
    schema.name = create->table_name;
    
    ColumnId col_id = 0;
    for (const auto& col_def : create->columns) {
        ColumnSchema col;
        col.id = col_id++;
        col.name = col_def.name;
        col.type = col_def.type;
        col.length = col_def.length;
        col.nullable = col_def.nullable;
        col.is_primary_key = col_def.primary_key;
        
        schema.columns.push_back(col);
        
        if (col_def.primary_key) {
            schema.primary_key.push_back(col.id);
        }
    }
    
    // Persistir no catálogo
    catalog_->createTable(schema.name, schema.columns);
    
    // Criar arquivo de dados
    // TODO: Inicializar arquivo da tabela
    
    // Criar índice para primary key se necessário
    if (!schema.primary_key.empty()) {
        std::string col_name = schema.columns[schema.primary_key[0]].name;
        catalog_->createIndex(schema.name, col_name);
    }
    
    return Status::OK;
}

Status QueryExecutor::executeCreateIndex(const CreateIndexNode* create,
                                          ExecutorContext* context) {
    // Verificar se tabela existe
    auto* schema = catalog_->getTable(create->table_name);
    if (!schema) {
        return Status::NOT_FOUND;
    }
    
    // Criar índice no catálogo
    catalog_->createIndex(create->table_name, create->columns[0]);
    
    // Construir índice populando com dados existentes
    auto* table = getTable(create->table_name, context);
    IndexId index_id = getIndex(create->table_name, create->columns[0]);
    
    // Criar B-Tree
    BTree<Value, RecordId> index(index_id, buffer_pool_);
    
    // Popular índice
    for (auto it = table->begin(); it != table->end(); ++it) {
        auto [rid, row] = *it;
        
        // Encontrar valor da coluna indexada
        size_t col_index = 0;
        for (size_t i = 0; i < schema->columns.size(); i++) {
            if (schema->columns[i].name == create->columns[0]) {
                col_index = i;
                break;
            }
        }
        
        index.insert(row[col_index], rid);
    }
    
    return Status::OK;
}

Status QueryExecutor::executeDropTable(const DropTableNode* drop,
                                        ExecutorContext* context) {
    // Remover do catálogo
    catalog_->dropTable(drop->table_name);
    
    // Remover arquivo de dados
    // TODO: Deletar arquivo físico
    
    return Status::OK;
}

Status QueryExecutor::executeBegin(const BeginNode* begin,
                                    ExecutorContext* context) {
    // Já iniciada no execute()
    return Status::OK;
}

Status QueryExecutor::executeCommit(const CommitNode* commit,
                                     ExecutorContext* context) {
    // Commit será feito no execute()
    return Status::OK;
}

Status QueryExecutor::executeRollback(const RollbackNode* rollback,
                                       ExecutorContext* context) {
    // Rollback será feito no execute()
    return Status::OK;
}

std::unique_ptr<ExecutorOperator> QueryExecutor::buildPlan(
    const ExecutionPlan::PlanNode* plan_node, ExecutorContext* context) {
    
    if (!plan_node) return nullptr;
    
    switch (plan_node->type) {
        case ExecutionPlan::OperatorType::SEQ_SCAN: {
            return std::make_unique<SeqScanOperator>(
                context, 
                plan_node->table_name,
                plan_node->filter_condition ? plan_node->filter_condition->clone() : nullptr
            );
        }
        
        case ExecutionPlan::OperatorType::INDEX_SCAN: {
            return std::make_unique<IndexScanOperator>(
                context,
                plan_node->table_name,
                plan_node->index_name,
                plan_node->filter_condition ? plan_node->filter_condition->clone() : nullptr
            );
        }
        
        case ExecutionPlan::OperatorType::FILTER: {
            auto child = buildPlan(plan_node->left.get(), context);
            return std::make_unique<FilterOperator>(
                context,
                std::move(child),
                plan_node->filter_condition->clone()
            );
        }
        
        case ExecutionPlan::OperatorType::PROJECT: {
            auto child = buildPlan(plan_node->left.get(), context);
            std::vector<std::unique_ptr<ASTNode>> exprs;
            // TODO: Extrair expressões do plano
            return std::make_unique<ProjectOperator>(
                context,
                std::move(child),
                std::move(exprs),
                std::vector<std::string>()
            );
        }
        
        case ExecutionPlan::OperatorType::JOIN:
        case ExecutionPlan::OperatorType::HASH_JOIN:
        case ExecutionPlan::OperatorType::NESTED_LOOP_JOIN:
        case ExecutionPlan::OperatorType::MERGE_JOIN: {
            auto left = buildPlan(plan_node->left.get(), context);
            auto right = buildPlan(plan_node->right.get(), context);
            
            JoinOperator::JoinAlgorithm algo;
            if (plan_node->type == ExecutionPlan::OperatorType::HASH_JOIN) {
                algo = JoinOperator::JoinAlgorithm::HASH;
            } else if (plan_node->type == ExecutionPlan::OperatorType::NESTED_LOOP_JOIN) {
                algo = JoinOperator::JoinAlgorithm::NESTED_LOOP;
            } else {
                algo = JoinOperator::JoinAlgorithm::MERGE;
            }
            
            return std::make_unique<JoinOperator>(
                context,
                std::move(left),
                std::move(right),
                nullptr, // TODO: Condição de join
                JoinType::INNER,
                algo
            );
        }
        
        case ExecutionPlan::OperatorType::SORT: {
            auto child = buildPlan(plan_node->left.get(), context);
            std::vector<SortOperator::SortKey> keys;
            for (const auto& [col, asc] : plan_node->order_by) {
                SortOperator::SortKey key;
                // TODO: Criar expressão para coluna
                key.ascending = asc;
                keys.push_back(std::move(key));
            }
            return std::make_unique<SortOperator>(
                context,
                std::move(child),
                std::move(keys)
            );
        }
        
        case ExecutionPlan::OperatorType::LIMIT: {
            auto child = buildPlan(plan_node->left.get(), context);
            return std::make_unique<LimitOperator>(
                context,
                std::move(child),
                plan_node->limit,
                0
            );
        }
        
        default:
            return nullptr;
    }
}

Value QueryExecutor::evaluateExpression(const ASTNode* expr,
                                         const std::vector<Value>& row,
                                         ExecutorContext* context) {
    if (!expr) return Value();
    
    switch (expr->type) {
        case ASTNodeType::LITERAL: {
            auto lit = static_cast<const LiteralNode*>(expr);
            return lit->value;
        }
        
        case ASTNodeType::COLUMN_REF: {
            auto col = static_cast<const ColumnRefNode*>(expr);
            // TODO: Mapear nome da coluna para índice
            size_t index = 0;
            if (index < row.size()) {
                return row[index];
            }
            return Value();
        }
        
        case ASTNodeType::BINARY_EXPR: {
            auto bin = static_cast<const BinaryExprNode*>(expr);
            Value left = evaluateExpression(bin->left.get(), row, context);
            Value right = evaluateExpression(bin->right.get(), row, context);
            
            switch (bin->op) {
                case Operator::EQ:
                    return Value(left.data.int_val == right.data.int_val);
                case Operator::NE:
                    return Value(left.data.int_val != right.data.int_val);
                case Operator::LT:
                    return Value(left.data.int_val < right.data.int_val);
                case Operator::LE:
                    return Value(left.data.int_val <= right.data.int_val);
                case Operator::GT:
                    return Value(left.data.int_val > right.data.int_val);
                case Operator::GE:
                    return Value(left.data.int_val >= right.data.int_val);
                case Operator::AND:
                    return Value(left.data.bool_val && right.data.bool_val);
                case Operator::OR:
                    return Value(left.data.bool_val || right.data.bool_val);
                case Operator::PLUS:
                    return Value(left.data.int_val + right.data.int_val);
                case Operator::MINUS:
                    return Value(left.data.int_val - right.data.int_val);
                case Operator::MULTIPLY:
                    return Value(left.data.int_val * right.data.int_val);
                case Operator::DIVIDE:
                    if (right.data.int_val != 0) {
                        return Value(left.data.int_val / right.data.int_val);
                    }
                    return Value();
                default:
                    return Value();
            }
        }
        
        default:
            return Value();
    }
}

Table* QueryExecutor::getTable(const std::string& name, ExecutorContext* context) {
    auto* schema = catalog_->getTable(name);
    if (!schema) return nullptr;
    
    // TODO: Cache de tabelas
    return new Table(*schema, buffer_pool_);
}

IndexId QueryExecutor::getIndex(const std::string& table, const std::string& column) {
    auto* schema = catalog_->getTable(table);
    if (!schema) return 0;
    
    auto it = schema->indexes.find(column);
    if (it != schema->indexes.end()) {
        return it->second;
    }
    
    return 0;
}

void QueryExecutor::resetStats() {
    stats_.reset();
}

std::string QueryExecutor::explain(const std::string& sql, bool analyze) {
    std::vector<std::vector<Value>> results;
    
    // Configurar modo EXPLAIN
    auto old_mode = ExecutionMode::NORMAL;
    if (analyze) {
        old_mode = ExecutionMode::EXPLAIN_ANALYZE;
    } else {
        old_mode = ExecutionMode::EXPLAIN;
    }
    
    execute(sql, results);
    
    if (!results.empty() && !results[0].empty()) {
        return results[0][0].str_val;
    }
    
    return "";
}

// ============================================
// SeqScanOperator
// ============================================

SeqScanOperator::SeqScanOperator(ExecutorContext* context, const std::string& table_name,
                                 std::unique_ptr<ASTNode> filter)
    : context_(context), table_name_(table_name), filter_(std::move(filter)),
      table_(nullptr) {
}

Status SeqScanOperator::open() {
    table_ = context_->getCatalog()->getTable(table_name_);
    if (!table_) {
        return Status::NOT_FOUND;
    }
    
    // TODO: Obter tabela real do storage
    iterator_ = TableIterator(nullptr, 0, 0); // Placeholder
    profiler_.start();
    
    return Status::OK;
}

Status SeqScanOperator::next(std::vector<Value>& row) {
    if (iterator_ == TableIterator(nullptr, INVALID_PAGE_ID, 0)) {
        return Status::NOT_FOUND;
    }
    
    auto [rid, values] = *iterator_;
    
    // Aplicar filtro se existir
    if (filter_ && !evaluateFilter(values)) {
        ++iterator_;
        return next(row); // Pular linha
    }
    
    row = std::move(values);
    ++iterator_;
    
    profiler_.recordRow();
    context_->getStats().rows_scanned++;
    
    return Status::OK;
}

void SeqScanOperator::close() {
    profiler_.stop();
}

void SeqScanOperator::rewind() {
    // TODO: Reiniciar iterator
}

bool SeqScanOperator::evaluateFilter(const std::vector<Value>& row) {
    // TODO: Avaliar expressão do filtro
    return true;
}

std::string SeqScanOperator::getDescription() const {
    return "Seq Scan on " + table_name_;
}

// ============================================
// IndexScanOperator
// ============================================

IndexScanOperator::IndexScanOperator(ExecutorContext* context, 
                                     const std::string& table_name,
                                     const std::string& index_name,
                                     std::unique_ptr<ASTNode> filter)
    : context_(context), table_name_(table_name), index_name_(index_name),
      filter_(std::move(filter)), table_(nullptr) {
}

Status IndexScanOperator::open() {
    table_ = context_->getCatalog()->getTable(table_name_);
    if (!table_) {
        return Status::NOT_FOUND;
    }
    
    IndexId index_id = context_->getCatalog()->getIndexId(index_name_);
    // TODO: Abrir índice
    // index_ = std::make_unique<BTree<Value, RecordId>>(index_id, context_->getBufferPool());
    
    profiler_.start();
    
    return Status::OK;
}

Status IndexScanOperator::next(std::vector<Value>& row) {
    if (index_iter_ == index_->end()) {
        return Status::NOT_FOUND;
    }
    
    auto [key, rid] = *index_iter_;
    
    // Buscar registro completo
    // TODO: table_->getRecord(rid, row);
    
    ++index_iter_;
    
    profiler_.recordRow();
    profiler_.recordIndexLookup();
    context_->getStats().rows_scanned++;
    context_->getStats().index_lookups++;
    
    return Status::OK;
}

void IndexScanOperator::close() {
    profiler_.stop();
}

void IndexScanOperator::rewind() {
    index_iter_ = index_->begin();
}

std::string IndexScanOperator::getDescription() const {
    return "Index Scan using " + index_name_ + " on " + table_name_;
}

// ============================================
// FilterOperator
// ============================================

FilterOperator::FilterOperator(ExecutorContext* context,
                               std::unique_ptr<ExecutorOperator> child,
                               std::unique_ptr<ASTNode> condition)
    : context_(context), child_(std::move(child)), condition_(std::move(condition)) {
}

Status FilterOperator::open() {
    Status status = child_->open();
    if (status != Status::OK) {
        return status;
    }
    
    profiler_.start();
    return Status::OK;
}

Status FilterOperator::next(std::vector<Value>& row) {
    while (true) {
        Status status = child_->next(row);
        if (status != Status::OK) {
            return status;
        }
        
        if (evaluateCondition(row)) {
            profiler_.recordRow();
            return Status::OK;
        }
    }
}

void FilterOperator::close() {
    child_->close();
    profiler_.stop();
}

void FilterOperator::rewind() {
    child_->rewind();
}

bool FilterOperator::evaluateCondition(const std::vector<Value>& row) {
    // TODO: Avaliar condição
    return true;
}

std::string FilterOperator::getDescription() const {
    return "Filter";
}

// ============================================
// ProjectOperator
// ============================================

ProjectOperator::ProjectOperator(ExecutorContext* context,
                                 std::unique_ptr<ExecutorOperator> child,
                                 std::vector<std::unique_ptr<ASTNode>> expressions,
                                 std::vector<std::string> aliases)
    : context_(context), child_(std::move(child)), 
      expressions_(std::move(expressions)), aliases_(std::move(aliases)) {
}

Status ProjectOperator::open() {
    Status status = child_->open();
    if (status != Status::OK) {
        return status;
    }
    
    profiler_.start();
    return Status::OK;
}

Status ProjectOperator::next(std::vector<Value>& row) {
    std::vector<Value> child_row;
    Status status = child_->next(child_row);
    
    if (status != Status::OK) {
        return status;
    }
    
    row.clear();
    for (const auto& expr : expressions_) {
        row.push_back(evaluateExpression(expr.get(), child_row));
    }
    
    profiler_.recordRow();
    return Status::OK;
}

void ProjectOperator::close() {
    child_->close();
    profiler_.stop();
}

void ProjectOperator::rewind() {
    child_->rewind();
}

Value ProjectOperator::evaluateExpression(const ASTNode* expr,
                                           const std::vector<Value>& row) {
    // TODO: Implementar avaliação
    return Value();
}

std::string ProjectOperator::getDescription() const {
    return "Project";
}

// ============================================
// JoinOperator
// ============================================

JoinOperator::JoinOperator(ExecutorContext* context,
                           std::unique_ptr<ExecutorOperator> left,
                           std::unique_ptr<ExecutorOperator> right,
                           std::unique_ptr<ASTNode> condition,
                           JoinType type,
                           JoinAlgorithm algorithm)
    : context_(context), left_(std::move(left)), right_(std::move(right)),
      condition_(std::move(condition)), join_type_(type), algorithm_(algorithm),
      right_index_(0) {
}

Status JoinOperator::open() {
    Status status = left_->open();
    if (status != Status::OK) {
        return status;
    }
    
    status = right_->open();
    if (status != Status::OK) {
        return status;
    }
    
    // Carregar todas as linhas da direita para hash join
    if (algorithm_ == JoinAlgorithm::HASH) {
        std::vector<Value> right_row;
        while (right_->next(right_row) == Status::OK) {
            right_rows_.push_back(right_row);
            size_t hash = hashRow(right_row, condition_.get());
            hash_table_.insert({hash, right_row});
        }
        right_->rewind();
    }
    
    profiler_.start();
    return Status::OK;
}

Status JoinOperator::next(std::vector<Value>& row) {
    while (true) {
        // Se não temos linha atual da esquerda, pegar próxima
        if (current_left_row_.empty()) {
            Status status = left_->next(current_left_row_);
            if (status != Status::OK) {
                if (status == Status::NOT_FOUND) {
                    break;
                }
                return status;
            }
            right_index_ = 0;
        }
        
        // Encontrar linha correspondente da direita
        while (right_index_ < right_rows_.size()) {
            const auto& right_row = right_rows_[right_index_++];
            
            if (evaluateJoinCondition(current_left_row_, right_row)) {
                // Construir linha resultado
                row = current_left_row_;
                row.insert(row.end(), right_row.begin(), right_row.end());
                profiler_.recordRow();
                return Status::OK;
            }
        }
        
        // Sem mais correspondências para esta linha da esquerda
        current_left_row_.clear();
    }
    
    return Status::NOT_FOUND;
}

void JoinOperator::close() {
    left_->close();
    right_->close();
    profiler_.stop();
}

void JoinOperator::rewind() {
    left_->rewind();
    right_->rewind();
    current_left_row_.clear();
    right_index_ = 0;
}

bool JoinOperator::evaluateJoinCondition(const std::vector<Value>& left_row,
                                          const std::vector<Value>& right_row) {
    if (!condition_) {
        return true; // Cross join
    }
    
    // TODO: Avaliar condição com ambas as linhas
    return true;
}

size_t JoinOperator::hashRow(const std::vector<Value>& row, const ASTNode* condition) {
    // TODO: Hash baseado nas colunas de join
    size_t hash = 0;
    for (const auto& val : row) {
        hash = hash * 31 + val.data.int_val;
    }
    return hash;
}

std::string JoinOperator::getName() const {
    switch (algorithm_) {
        case JoinAlgorithm::NESTED_LOOP:
            return "NestedLoopJoin";
        case JoinAlgorithm::HASH:
            return "HashJoin";
        case JoinAlgorithm::MERGE:
            return "MergeJoin";
        default:
            return "Join";
    }
}

std::string JoinOperator::getDescription() const {
    std::string desc = getName();
    switch (join_type_) {
        case JoinType::INNER:
            desc += " (Inner)";
            break;
        case JoinType::LEFT:
            desc += " (Left)";
            break;
        case JoinType::RIGHT:
            desc += " (Right)";
            break;
        case JoinType::FULL:
            desc += " (Full)";
            break;
        default:
            break;
    }
    return desc;
}

// ============================================
// SortOperator
// ============================================

SortOperator::SortOperator(ExecutorContext* context,
                           std::unique_ptr<ExecutorOperator> child,
                           std::vector<SortKey> keys)
    : context_(context), child_(std::move(child)), keys_(std::move(keys)),
      current_index_(0) {
}

Status SortOperator::open() {
    Status status = child_->open();
    if (status != Status::OK) {
        return status;
    }
    
    profiler_.start();
    
    // Carregar todas as linhas
    std::vector<Value> row;
    while (child_->next(row) == Status::OK) {
        sorted_rows_.push_back(row);
        context_->getStats().rows_scanned++;
    }
    
    // Ordenar
    sortRows();
    
    child_->close();
    current_index_ = 0;
    
    return Status::OK;
}

Status SortOperator::next(std::vector<Value>& row) {
    if (current_index_ >= sorted_rows_.size()) {
        return Status::NOT_FOUND;
    }
    
    row = sorted_rows_[current_index_++];
    profiler_.recordRow();
    
    return Status::OK;
}

void SortOperator::close() {
    profiler_.stop();
}

void SortOperator::rewind() {
    current_index_ = 0;
}

void SortOperator::sortRows() {
    std::sort(sorted_rows_.begin(), sorted_rows_.end(),
              [this](const std::vector<Value>& a, const std::vector<Value>& b) {
                  return compareRows(a, b);
              });
}

bool SortOperator::compareRows(const std::vector<Value>& a, const std::vector<Value>& b) {
    for (const auto& key : keys_) {
        // TODO: Extrair valores das linhas baseado nas expressões
        int cmp = 0; // compareValues(val_a, val_b);
        
        if (cmp < 0) return key.ascending;
        if (cmp > 0) return !key.ascending;
    }
    
    return false;
}

std::string SortOperator::getDescription() const {
    return "Sort";
}

// ============================================
// LimitOperator
// ============================================

LimitOperator::LimitOperator(ExecutorContext* context,
                             std::unique_ptr<ExecutorOperator> child,
                             size_t limit, size_t offset)
    : context_(context), child_(std::move(child)), limit_(limit), offset_(offset),
      current_row_(0) {
}

Status LimitOperator::open() {
    Status status = child_->open();
    if (status != Status::OK) {
        return status;
    }
    
    // Pular OFFSET
    std::vector<Value> row;
    for (size_t i = 0; i < offset_; i++) {
        if (child_->next(row) != Status::OK) {
            break;
        }
    }
    
    profiler_.start();
    return Status::OK;
}

Status LimitOperator::next(std::vector<Value>& row) {
    if (current_row_ >= limit_) {
        return Status::NOT_FOUND;
    }
    
    Status status = child_->next(row);
    if (status == Status::OK) {
        current_row_++;
        profiler_.recordRow();
    }
    
    return status;
}

void LimitOperator::close() {
    child_->close();
    profiler_.stop();
}

void LimitOperator::rewind() {
    child_->rewind();
    current_row_ = 0;
}

std::string LimitOperator::getDescription() const {
    return "Limit (" + std::to_string(limit_) + ")";
}

} // namespace orangesql