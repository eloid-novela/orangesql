#include "sql_parser.h"
#include <sstream>
#include <iostream>
#include <memory>

namespace orangesql {

// ============================================
// IMPLEMENTAÇÃO DO PARSER
// ============================================

std::unique_ptr<ASTNode> SQLParser::parse(const std::string& query) {
    lexer_ = std::make_unique<Lexer>(query);
    nextToken();
    
    auto stmt = parseStatement();
    
    if (current_token_.type != TokenType::END_OF_FILE) {
        error("Tokens não processados após o fim da instrução");
        return nullptr;
    }
    
    return stmt;
}

void SQLParser::nextToken() {
    current_token_ = lexer_->nextToken();
}

bool SQLParser::match(TokenType expected) {
    return current_token_.type == expected;
}

bool SQLParser::matchAny(const std::vector<TokenType>& types) {
    for (auto type : types) {
        if (current_token_.type == type) {
            return true;
        }
    }
    return false;
}

void SQLParser::expect(TokenType expected) {
    if (!match(expected)) {
        std::stringstream ss;
        ss << "Esperado " << static_cast<int>(expected) 
           << ", encontrado '" << current_token_.text << "'";
        error(ss.str());
    }
}

Token SQLParser::consume(TokenType expected) {
    Token token = current_token_;
    expect(expected);
    nextToken();
    return token;
}

void SQLParser::error(const std::string& message) {
    std::stringstream ss;
    ss << "Erro de sintaxe em " << lexer_->getPosition() << ": " << message;
    last_error_ = ss.str();
    throw std::runtime_error(last_error_);
}

void SQLParser::warning(const std::string& message) {
    std::cerr << "Aviso: " << message << std::endl;
}

// ============================================
// GRAMÁTICA PRINCIPAL
// ============================================

std::unique_ptr<ASTNode> SQLParser::parseStatement() {
    switch (current_token_.type) {
        // DML
        case TokenType::SELECT:
            return parseSelect();
        case TokenType::INSERT:
            return parseInsert();
        case TokenType::UPDATE:
            return parseUpdate();
        case TokenType::DELETE:
            return parseDelete();
            
        // DDL
        case TokenType::CREATE:
            nextToken();
            if (match(TokenType::TABLE)) {
                return parseCreateTable();
            } else if (match(TokenType::INDEX)) {
                return parseCreateIndex();
            }
            error("Esperado TABLE ou INDEX após CREATE");
            break;
            
        case TokenType::DROP:
            nextToken();
            if (match(TokenType::TABLE)) {
                return parseDropTable();
            }
            error("Esperado TABLE após DROP");
            break;
            
        // Transações
        case TokenType::BEGIN:
            return parseBegin();
        case TokenType::COMMIT:
            return parseCommit();
        case TokenType::ROLLBACK:
            return parseRollback();
            
        default:
            error("Instrução SQL não reconhecida");
    }
    
    return nullptr;
}

// ============================================
// PARSE DE SELECT
// ============================================

std::unique_ptr<SelectNode> SQLParser::parseSelect() {
    auto node = std::make_unique<SelectNode>();
    
    consume(TokenType::SELECT);
    
    // DISTINCT
    if (match(TokenType::DISTINCT)) {
        node->distinct = true;
        nextToken();
    } else if (match(TokenType::ALL)) {
        node->distinct = false;
        nextToken();
    }
    
    // SELECT list
    node->select_list = parseSelectList();
    
    // FROM clause
    if (!match(TokenType::FROM)) {
        error("Esperado FROM após lista de SELECT");
    }
    nextToken();
    
    // Parse table references
    do {
        SelectNode::TableRef table_ref;
        
        if (match(TokenType::LPAREN)) {
            // Subquery
            nextToken();
            table_ref.subquery = parseSelect();
            expect(TokenType::RPAREN);
            nextToken();
        } else {
            // Table name
            table_ref.table_name = consume(TokenType::IDENTIFIER).text;
        }
        
        // Alias
        if (match(TokenType::AS)) {
            nextToken();
            table_ref.alias = consume(TokenType::IDENTIFIER).text;
        } else if (match(TokenType::IDENTIFIER)) {
            table_ref.alias = consume(TokenType::IDENTIFIER).text;
        }
        
        node->from_tables.push_back(std::move(table_ref));
        
        if (!match(TokenType::COMMA)) break;
        nextToken();
    } while (true);
    
    // JOIN clauses
    while (matchAny({TokenType::JOIN, TokenType::INNER, TokenType::LEFT, 
                     TokenType::RIGHT, TokenType::FULL, TokenType::CROSS})) {
        node->joins.push_back(parseJoin());
    }
    
    // WHERE
    if (match(TokenType::WHERE)) {
        node->where = parseWhere();
    }
    
    // GROUP BY
    if (match(TokenType::GROUP)) {
        node->group_by = parseGroupBy();
    }
    
    // HAVING
    if (match(TokenType::HAVING)) {
        nextToken();
        node->having = std::make_unique<WhereClause>(parseExpression());
    }
    
    // ORDER BY
    if (match(TokenType::ORDER)) {
        node->order_by = parseOrderBy();
    }
    
    // LIMIT
    if (match(TokenType::LIMIT)) {
        node->limit = parseLimit();
    }
    
    // UNION/INTERSECT/EXCEPT
    if (matchAny({TokenType::UNION, TokenType::INTERSECT, TokenType::EXCEPT})) {
        if (match(TokenType::UNION)) {
            node->set_operation = SelectNode::SetOperation::UNION;
        } else if (match(TokenType::INTERSECT)) {
            node->set_operation = SelectNode::SetOperation::INTERSECT;
        } else if (match(TokenType::EXCEPT)) {
            node->set_operation = SelectNode::SetOperation::EXCEPT;
        }
        nextToken();
        
        if (match(TokenType::ALL)) {
            if (node->set_operation == SelectNode::SetOperation::UNION) {
                node->set_operation = SelectNode::SetOperation::UNION_ALL;
            }
            nextToken();
        }
        
        node->set_operand = parseSelect();
    }
    
    return node;
}

// ============================================
// PARSE DE INSERT
// ============================================

std::unique_ptr<InsertNode> SQLParser::parseInsert() {
    auto node = std::make_unique<InsertNode>();
    
    consume(TokenType::INSERT);
    expect(TokenType::INTO);
    nextToken();
    
    // Table name
    node->table = consume(TokenType::IDENTIFIER).text;
    
    // Optional column list
    if (match(TokenType::LPAREN)) {
        nextToken();
        node->columns = parseIdentifierList();
        expect(TokenType::RPAREN);
        nextToken();
    }
    
    // VALUES or SELECT
    if (match(TokenType::VALUES)) {
        nextToken();
        node->values = parseValuesList();
    } else if (match(TokenType::SELECT)) {
        node->select = parseSelect();
    } else {
        error("Esperado VALUES ou SELECT após INSERT");
    }
    
    return node;
}

// ============================================
// PARSE DE UPDATE
// ============================================

std::unique_ptr<UpdateNode> SQLParser::parseUpdate() {
    auto node = std::make_unique<UpdateNode>();
    
    consume(TokenType::UPDATE);
    
    // Table name
    node->table = consume(TokenType::IDENTIFIER).text;
    
    expect(TokenType::SET);
    nextToken();
    
    // Parse assignments
    do {
        UpdateNode::Assignment assignment;
        assignment.column = consume(TokenType::IDENTIFIER).text;
        expect(TokenType::ASSIGN);
        nextToken();
        assignment.value = parseExpression();
        
        node->assignments.push_back(std::move(assignment));
        
        if (!match(TokenType::COMMA)) break;
        nextToken();
    } while (true);
    
    // Optional WHERE
    if (match(TokenType::WHERE)) {
        node->where = parseWhere();
    }
    
    return node;
}

// ============================================
// PARSE DE DELETE
// ============================================

std::unique_ptr<DeleteNode> SQLParser::parseDelete() {
    auto node = std::make_unique<DeleteNode>();
    
    consume(TokenType::DELETE);
    expect(TokenType::FROM);
    nextToken();
    
    // Table name
    node->table = consume(TokenType::IDENTIFIER).text;
    
    // Optional WHERE
    if (match(TokenType::WHERE)) {
        node->where = parseWhere();
    }
    
    return node;
}

// ============================================
// PARSE DE CREATE TABLE
// ============================================

std::unique_ptr<CreateTableNode> SQLParser::parseCreateTable() {
    auto node = std::make_unique<CreateTableNode>();
    
    consume(TokenType::TABLE);
    
    // Table name
    node->table_name = consume(TokenType::IDENTIFIER).text;
    
    expect(TokenType::LPAREN);
    nextToken();
    
    // Parse columns and constraints
    do {
        if (matchAny({TokenType::PRIMARY, TokenType::FOREIGN, 
                      TokenType::UNIQUE, TokenType::CHECK})) {
            // Constraint
            CreateTableNode::Constraint constraint;
            
            if (match(TokenType::CONSTRAINT)) {
                nextToken(); // Skip optional CONSTRAINT keyword
                // TODO: Parse constraint name
            }
            
            if (match(TokenType::PRIMARY)) {
                constraint.type = CreateTableNode::Constraint::PRIMARY_KEY;
                nextToken();
                expect(TokenType::KEY);
                nextToken();
                expect(TokenType::LPAREN);
                nextToken();
                constraint.columns = parseIdentifierList();
                expect(TokenType::RPAREN);
                nextToken();
            } else if (match(TokenType::FOREIGN)) {
                constraint.type = CreateTableNode::Constraint::FOREIGN_KEY;
                nextToken();
                expect(TokenType::KEY);
                nextToken();
                expect(TokenType::LPAREN);
                nextToken();
                constraint.columns = parseIdentifierList();
                expect(TokenType::RPAREN);
                nextToken();
                expect(TokenType::REFERENCES);
                nextToken();
                constraint.ref_table = consume(TokenType::IDENTIFIER).text;
                expect(TokenType::LPAREN);
                nextToken();
                constraint.ref_columns = parseIdentifierList();
                expect(TokenType::RPAREN);
                nextToken();
            } else if (match(TokenType::UNIQUE)) {
                constraint.type = CreateTableNode::Constraint::UNIQUE;
                nextToken();
                expect(TokenType::LPAREN);
                nextToken();
                constraint.columns = parseIdentifierList();
                expect(TokenType::RPAREN);
                nextToken();
            } else if (match(TokenType::CHECK)) {
                constraint.type = CreateTableNode::Constraint::CHECK;
                nextToken();
                expect(TokenType::LPAREN);
                nextToken();
                // TODO: Parse check expression
                constraint.check_expr = "TODO";
                expect(TokenType::RPAREN);
                nextToken();
            }
            
            node->constraints.push_back(constraint);
        } else {
            // Column definition
            ColumnDef column;
            column.name = consume(TokenType::IDENTIFIER).text;
            column.type = parseDataType();
            
            // Parse column constraints
            while (true) {
                if (match(TokenType::NOT) && peekToken().type == TokenType::NULL) {
                    nextToken(); // NOT
                    nextToken(); // NULL
                    column.nullable = false;
                } else if (match(TokenType::NULL)) {
                    nextToken();
                    column.nullable = true;
                } else if (match(TokenType::PRIMARY)) {
                    nextToken();
                    expect(TokenType::KEY);
                    nextToken();
                    column.primary_key = true;
                } else if (match(TokenType::UNIQUE)) {
                    nextToken();
                    column.unique = true;
                } else if (match(TokenType::DEFAULT)) {
                    nextToken();
                    // TODO: Parse default value expression
                    nextToken();
                } else {
                    break;
                }
            }
            
            node->columns.push_back(column);
        }
        
        if (!match(TokenType::COMMA)) break;
        nextToken();
    } while (true);
    
    expect(TokenType::RPAREN);
    nextToken();
    
    return node;
}

// ============================================
// PARSE DE CREATE INDEX
// ============================================

std::unique_ptr<CreateIndexNode> SQLParser::parseCreateIndex() {
    auto node = std::make_unique<CreateIndexNode>();
    
    consume(TokenType::INDEX);
    
    // Optional UNIQUE
    if (match(TokenType::UNIQUE)) {
        node->unique = true;
        nextToken();
    }
    
    // Index name
    node->index_name = consume(TokenType::IDENTIFIER).text;
    
    expect(TokenType::ON);
    nextToken();
    
    // Table name
    node->table_name = consume(TokenType::IDENTIFIER).text;
    
    expect(TokenType::LPAREN);
    nextToken();
    
    // Columns
    node->columns = parseIdentifierList();
    
    expect(TokenType::RPAREN);
    nextToken();
    
    return node;
}

// ============================================
// PARSE DE DROP TABLE
// ============================================

std::unique_ptr<DropTableNode> SQLParser::parseDropTable() {
    auto node = std::make_unique<DropTableNode>();
    
    consume(TokenType::TABLE);
    
    // Optional IF EXISTS
    if (match(TokenType::IF)) {
        nextToken();
        expect(TokenType::EXISTS);
        nextToken();
        node->if_exists = true;
    }
    
    // Table name
    node->table_name = consume(TokenType::IDENTIFIER).text;
    
    return node;
}

// ============================================
// PARSE DE TRANSACTIONS
// ============================================

std::unique_ptr<BeginNode> SQLParser::parseBegin() {
    auto node = std::make_unique<BeginNode>();
    
    consume(TokenType::BEGIN);
    
    if (match(TokenType::TRANSACTION)) {
        nextToken();
    }
    
    if (match(TokenType::IDENTIFIER)) {
        node->name = consume(TokenType::IDENTIFIER).text;
        node->is_savepoint = true;
    }
    
    return node;
}

std::unique_ptr<CommitNode> SQLParser::parseCommit() {
    auto node = std::make_unique<CommitNode>();
    
    consume(TokenType::COMMIT);
    
    if (match(TokenType::TRANSACTION)) {
        nextToken();
    }
    
    if (match(TokenType::IDENTIFIER)) {
        node->name = consume(TokenType::IDENTIFIER).text;
        node->is_savepoint = true;
    }
    
    return node;
}

std::unique_ptr<RollbackNode> SQLParser::parseRollback() {
    auto node = std::make_unique<RollbackNode>();
    
    consume(TokenType::ROLLBACK);
    
    if (match(TokenType::TRANSACTION)) {
        nextToken();
    }
    
    if (match(TokenType::TO)) {
        nextToken();
        expect(TokenType::SAVEPOINT);
        nextToken();
        node->name = consume(TokenType::IDENTIFIER).text;
        node->is_savepoint = true;
    } else if (match(TokenType::IDENTIFIER)) {
        node->name = consume(TokenType::IDENTIFIER).text;
        node->is_savepoint = true;
    }
    
    return node;
}

// ============================================
// PARSE DE EXPRESSÕES
// ============================================

std::unique_ptr<ASTNode> SQLParser::parseExpression() {
    return parseLogicalOr();
}

std::unique_ptr<ASTNode> SQLParser::parseLogicalOr() {
    auto left = parseLogicalAnd();
    
    while (match(TokenType::OR)) {
        auto op = Operator::OR;
        nextToken();
        auto right = parseLogicalAnd();
        
        auto node = std::make_unique<BinaryExprNode>();
        node->op = op;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    
    return left;
}

std::unique_ptr<ASTNode> SQLParser::parseLogicalAnd() {
    auto left = parseEquality();
    
    while (match(TokenType::AND)) {
        auto op = Operator::AND;
        nextToken();
        auto right = parseEquality();
        
        auto node = std::make_unique<BinaryExprNode>();
        node->op = op;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    
    return left;
}

std::unique_ptr<ASTNode> SQLParser::parseEquality() {
    auto left = parseRelational();
    
    while (matchAny({TokenType::EQ, TokenType::NE})) {
        Operator op;
        if (match(TokenType::EQ)) op = Operator::EQ;
        else if (match(TokenType::NE)) op = Operator::NE;
        
        nextToken();
        auto right = parseRelational();
        
        auto node = std::make_unique<BinaryExprNode>();
        node->op = op;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    
    // IS [NOT] NULL
    if (match(TokenType::IS)) {
        nextToken();
        
        Operator op;
        if (match(TokenType::NOT)) {
            nextToken();
            op = Operator::IS_NOT_NULL;
        } else {
            op = Operator::IS_NULL;
        }
        
        expect(TokenType::NULL);
        nextToken();
        
        auto node = std::make_unique<UnaryExprNode>();
        node->op = op;
        node->expr = std::move(left);
        left = std::move(node);
    }
    
    // [NOT] IN
    if (match(TokenType::IN) || (match(TokenType::NOT) && peekToken().type == TokenType::IN)) {
        bool not_in = false;
        if (match(TokenType::NOT)) {
            not_in = true;
            nextToken();
        }
        
        expect(TokenType::IN);
        nextToken();
        
        expect(TokenType::LPAREN);
        nextToken();
        
        auto node = std::make_unique<BinaryExprNode>();
        node->op = not_in ? Operator::NOT_IN : Operator::IN;
        node->left = std::move(left);
        
        // Subquery or list
        if (match(TokenType::SELECT)) {
            node->right = parseSelect();
        } else {
            auto list = std::make_unique<ASTNode>(ASTNodeType::LITERAL); // TODO: Create list node
            // Parse expression list
            node->right = std::move(list);
        }
        
        expect(TokenType::RPAREN);
        nextToken();
        
        left = std::move(node);
    }
    
    // [NOT] BETWEEN
    if (match(TokenType::BETWEEN) || (match(TokenType::NOT) && peekToken().type == TokenType::BETWEEN)) {
        bool not_between = false;
        if (match(TokenType::NOT)) {
            not_between = true;
            nextToken();
        }
        
        expect(TokenType::BETWEEN);
        nextToken();
        
        auto lower = parseExpression();
        expect(TokenType::AND);
        nextToken();
        auto upper = parseExpression();
        
        auto node = std::make_unique<BinaryExprNode>();
        node->op = not_between ? Operator::NOT_BETWEEN : Operator::BETWEEN;
        node->left = std::move(left);
        // TODO: Store lower/upper in node
        left = std::move(node);
    }
    
    return left;
}

std::unique_ptr<ASTNode> SQLParser::parseRelational() {
    auto left = parseAdditive();
    
    while (matchAny({TokenType::LT, TokenType::LE, TokenType::GT, TokenType::GE,
                     TokenType::LIKE, TokenType::NOT})) {
        Operator op;
        
        if (match(TokenType::LT)) op = Operator::LT;
        else if (match(TokenType::LE)) op = Operator::LE;
        else if (match(TokenType::GT)) op = Operator::GT;
        else if (match(TokenType::GE)) op = Operator::GE;
        else if (match(TokenType::LIKE)) op = Operator::LIKE;
        else if (match(TokenType::NOT)) {
            nextToken();
            expect(TokenType::LIKE);
            op = Operator::NOT_LIKE;
        }
        
        nextToken();
        auto right = parseAdditive();
        
        auto node = std::make_unique<BinaryExprNode>();
        node->op = op;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    
    return left;
}

std::unique_ptr<ASTNode> SQLParser::parseAdditive() {
    auto left = parseMultiplicative();
    
    while (matchAny({TokenType::PLUS, TokenType::MINUS, TokenType::CONCAT})) {
        Operator op;
        if (match(TokenType::PLUS)) op = Operator::PLUS;
        else if (match(TokenType::MINUS)) op = Operator::MINUS;
        else if (match(TokenType::CONCAT)) op = Operator::CONCAT;
        
        nextToken();
        auto right = parseMultiplicative();
        
        auto node = std::make_unique<BinaryExprNode>();
        node->op = op;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    
    return left;
}

std::unique_ptr<ASTNode> SQLParser::parseMultiplicative() {
    auto left = parseUnary();
    
    while (matchAny({TokenType::STAR, TokenType::SLASH, TokenType::PERCENT})) {
        Operator op;
        if (match(TokenType::STAR)) op = Operator::MULTIPLY;
        else if (match(TokenType::SLASH)) op = Operator::DIVIDE;
        else if (match(TokenType::PERCENT)) op = Operator::MODULO;
        
        nextToken();
        auto right = parseUnary();
        
        auto node = std::make_unique<BinaryExprNode>();
        node->op = op;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    
    return left;
}

std::unique_ptr<ASTNode> SQLParser::parseUnary() {
    if (matchAny({TokenType::PLUS, TokenType::MINUS, TokenType::NOT})) {
        Operator op;
        if (match(TokenType::PLUS)) op = Operator::PLUS;
        else if (match(TokenType::MINUS)) op = Operator::MINUS;
        else if (match(TokenType::NOT)) op = Operator::NOT;
        
        nextToken();
        auto expr = parseUnary();
        
        auto node = std::make_unique<UnaryExprNode>();
        node->op = op;
        node->expr = std::move(expr);
        return node;
    }
    
    return parsePrimary();
}

std::unique_ptr<ASTNode> SQLParser::parsePrimary() {
    // Literals
    if (match(TokenType::INTEGER)) {
        auto node = std::make_unique<LiteralNode>();
        node->value = Value(std::stoi(current_token_.text));
        nextToken();
        return node;
    }
    
    if (match(TokenType::FLOAT)) {
        auto node = std::make_unique<LiteralNode>();
        node->value = Value(static_cast<int64_t>(std::stod(current_token_.text))); // TODO: Add float support
        nextToken();
        return node;
    }
    
    if (match(TokenType::STRING)) {
        auto node = std::make_unique<LiteralNode>();
        node->value = Value(current_token_.text);
        nextToken();
        return node;
    }
    
    if (match(TokenType::BOOLEAN)) {
        auto node = std::make_unique<LiteralNode>();
        node->value = Value(current_token_.text == "TRUE");
        nextToken();
        return node;
    }
    
    if (match(TokenType::NULL)) {
        auto node = std::make_unique<LiteralNode>();
        node->value = Value(); // NULL value
        nextToken();
        return node;
    }
    
    // Column reference
    if (match(TokenType::IDENTIFIER)) {
        std::string first = consume(TokenType::IDENTIFIER).text;
        
        if (match(TokenType::DOT)) {
            nextToken();
            std::string second = consume(TokenType::IDENTIFIER).text;
            return std::make_unique<ColumnRefNode>(first, second);
        } else {
            return std::make_unique<ColumnRefNode>(first);
        }
    }
    
    // Function call
    if (match(TokenType::IDENTIFIER) && peekToken().type == TokenType::LPAREN) {
        std::string name = consume(TokenType::IDENTIFIER).text;
        auto node = std::make_unique<FunctionCallNode>(name);
        
        consume(TokenType::LPAREN);
        
        if (!match(TokenType::RPAREN)) {
            do {
                node->arguments.push_back(parseExpression());
                if (!match(TokenType::COMMA)) break;
                nextToken();
            } while (true);
        }
        
        expect(TokenType::RPAREN);
        nextToken();
        
        return node;
    }
    
    // Subquery
    if (match(TokenType::LPAREN)) {
        nextToken();
        
        if (match(TokenType::SELECT)) {
            auto subquery = parseSelect();
            expect(TokenType::RPAREN);
            nextToken();
            return subquery;
        } else {
            auto expr = parseExpression();
            expect(TokenType::RPAREN);
            nextToken();
            return expr;
        }
    }
    
    // CASE expression
    if (match(TokenType::CASE)) {
        // TODO: Implement CASE parsing
        nextToken();
        while (!match(TokenType::END)) {
            nextToken();
        }
        expect(TokenType::END);
        nextToken();
    }
    
    error("Expressão inválida");
    return nullptr;
}

// ============================================
// PARSE DE CLÁUSULAS
// ============================================

std::unique_ptr<WhereClause> SQLParser::parseWhere() {
    consume(TokenType::WHERE);
    return std::make_unique<WhereClause>(parseExpression());
}

std::unique_ptr<OrderByNode> SQLParser::parseOrderBy() {
    auto node = std::make_unique<OrderByNode>();
    
    consume(TokenType::ORDER);
    expect(TokenType::BY);
    nextToken();
    
    do {
        OrderByNode::Item item;
        item.expr = parseExpression();
        item.ascending = true;
        
        if (match(TokenType::ASC)) {
            nextToken();
        } else if (match(TokenType::DESC)) {
            item.ascending = false;
            nextToken();
        }
        
        node->items.push_back(std::move(item));
        
        if (!match(TokenType::COMMA)) break;
        nextToken();
    } while (true);
    
    return node;
}

std::unique_ptr<GroupByNode> SQLParser::parseGroupBy() {
    auto node = std::make_unique<GroupByNode>();
    
    consume(TokenType::GROUP);
    expect(TokenType::BY);
    nextToken();
    
    do {
        node->columns.push_back(parseExpression());
        if (!match(TokenType::COMMA)) break;
        nextToken();
    } while (true);
    
    return node;
}

std::unique_ptr<LimitNode> SQLParser::parseLimit() {
    auto node = std::make_unique<LimitNode>();
    
    consume(TokenType::LIMIT);
    
    if (match(TokenType::INTEGER)) {
        node->limit = std::stoi(consume(TokenType::INTEGER).text);
    }
    
    if (match(TokenType::OFFSET)) {
        nextToken();
        if (match(TokenType::INTEGER)) {
            node->offset = std::stoi(consume(TokenType::INTEGER).text);
        }
    } else if (match(TokenType::COMMA)) {
        // MySQL-style LIMIT offset, count
        nextToken();
        node->offset = node->limit;
        if (match(TokenType::INTEGER)) {
            node->limit = std::stoi(consume(TokenType::INTEGER).text);
        }
    }
    
    return node;
}

std::unique_ptr<JoinClause> SQLParser::parseJoin() {
    auto node = std::make_unique<JoinClause>();
    
    // Parse join type
    if (match(TokenType::INNER)) {
        node->type = JoinType::INNER;
        nextToken();
    } else if (match(TokenType::LEFT)) {
        node->type = JoinType::LEFT;
        nextToken();
        if (match(TokenType::OUTER)) nextToken();
    } else if (match(TokenType::RIGHT)) {
        node->type = JoinType::RIGHT;
        nextToken();
        if (match(TokenType::OUTER)) nextToken();
    } else if (match(TokenType::FULL)) {
        node->type = JoinType::FULL;
        nextToken();
        if (match(TokenType::OUTER)) nextToken();
    } else if (match(TokenType::CROSS)) {
        node->type = JoinType::CROSS;
        nextToken();
    }
    
    expect(TokenType::JOIN);
    nextToken();
    
    // Table name
    node->table = consume(TokenType::IDENTIFIER).text;
    
    // Optional alias
    if (match(TokenType::AS)) {
        nextToken();
        node->alias = consume(TokenType::IDENTIFIER).text;
    } else if (match(TokenType::IDENTIFIER)) {
        node->alias = consume(TokenType::IDENTIFIER).text;
    }
    
    // ON or USING
    if (match(TokenType::ON)) {
        nextToken();
        node->condition = parseExpression();
    } else if (match(TokenType::USING)) {
        nextToken();
        expect(TokenType::LPAREN);
        nextToken();
        node->using_columns = parseIdentifierList();
        expect(TokenType::RPAREN);
        nextToken();
    }
    
    return node;
}

// ============================================
// PARSE DE LISTAS
// ============================================

std::vector<std::unique_ptr<ASTNode>> SQLParser::parseSelectList() {
    std::vector<std::unique_ptr<ASTNode>> list;
    
    do {
        if (match(TokenType::STAR)) {
            // SELECT *
            auto star = std::make_unique<LiteralNode>();
            star->value = Value("*");
            list.push_back(std::move(star));
            nextToken();
        } else {
            list.push_back(parseExpression());
            
            // Optional alias
            if (match(TokenType::AS)) {
                nextToken();
                // TODO: Store alias
                consume(TokenType::IDENTIFIER);
            } else if (match(TokenType::IDENTIFIER)) {
                // TODO: Store alias
                consume(TokenType::IDENTIFIER);
            }
        }
        
        if (!match(TokenType::COMMA)) break;
        nextToken();
    } while (true);
    
    return list;
}

std::vector<std::string> SQLParser::parseIdentifierList() {
    std::vector<std::string> list;
    
    do {
        list.push_back(consume(TokenType::IDENTIFIER).text);
        if (!match(TokenType::COMMA)) break;
        nextToken();
    } while (true);
    
    return list;
}

std::vector<std::unique_ptr<ASTNode>> SQLParser::parseExpressionList() {
    std::vector<std::unique_ptr<ASTNode>> list;
    
    do {
        list.push_back(parseExpression());
        if (!match(TokenType::COMMA)) break;
        nextToken();
    } while (true);
    
    return list;
}

std::vector<std::vector<std::unique_ptr<ASTNode>>> SQLParser::parseValuesList() {
    std::vector<std::vector<std::unique_ptr<ASTNode>>> values;
    
    do {
        expect(TokenType::LPAREN);
        nextToken();
        
        auto row = parseExpressionList();
        values.push_back(std::move(row));
        
        expect(TokenType::RPAREN);
        nextToken();
        
        if (!match(TokenType::COMMA)) break;
        nextToken();
    } while (true);
    
    return values;
}

DataType SQLParser::parseDataType() {
    if (match(TokenType::IDENTIFIER)) {
        std::string type_name = current_token_.text;
        nextToken();
        
        if (type_name == "INT" || type_name == "INTEGER") {
            return DataType::INTEGER;
        } else if (type_name == "BIGINT") {
            return DataType::BIGINT;
        } else if (type_name == "VARCHAR" || type_name == "STRING") {
            if (match(TokenType::LPAREN)) {
                nextToken();
                if (match(TokenType::INTEGER)) {
                    // TODO: Store length
                    nextToken();
                }
                expect(TokenType::RPAREN);
                nextToken();
            }
            return DataType::VARCHAR;
        } else if (type_name == "BOOLEAN" || type_name == "BOOL") {
            return DataType::BOOLEAN;
        } else if (type_name == "DATE") {
            return DataType::DATE;
        } else if (type_name == "DECIMAL" || type_name == "NUMERIC") {
            return DataType::DECIMAL;
        }
    }
    
    error("Tipo de dado inválido");
    return DataType::INTEGER;
}

} // namespace orangesql