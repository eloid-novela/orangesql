#ifndef ORANGESQL_AST_H
#define ORANGESQL_AST_H

#include "../include/types.h"
#include <string>
#include <vector>
#include <memory>
#include <variant>

namespace orangesql {

// Tipos de nós da AST
enum class ASTNodeType {
    // Comandos DDL
    CREATE_TABLE,
    DROP_TABLE,
    CREATE_INDEX,
    DROP_INDEX,
    ALTER_TABLE,
    
    // Comandos DML
    SELECT,
    INSERT,
    UPDATE,
    DELETE,
    
    // Comandos DCL
    BEGIN,
    COMMIT,
    ROLLBACK,
    
    // Expressões
    BINARY_EXPR,
    UNARY_EXPR,
    COLUMN_REF,
    LITERAL,
    FUNCTION_CALL,
    
    // Cláusulas
    WHERE_CLAUSE,
    JOIN_CLAUSE,
    ORDER_BY,
    GROUP_BY,
    HAVING,
    LIMIT
};

// Tipos de operadores
enum class Operator {
    // Aritméticos
    PLUS, MINUS, MULTIPLY, DIVIDE, MODULO,
    
    // Comparação
    EQ, NE, LT, LE, GT, GE,
    LIKE, NOT_LIKE,
    IN, NOT_IN,
    IS_NULL, IS_NOT_NULL,
    BETWEEN, NOT_BETWEEN,
    
    // Lógicos
    AND, OR, NOT,
    
    // Outros
    ASSIGN,
    CONCAT
};

// Tipos de joins
enum class JoinType {
    INNER,
    LEFT,
    RIGHT,
    FULL,
    CROSS,
    NATURAL
};

// Nó base da AST
struct ASTNode {
    ASTNodeType type;
    int line;
    int column;
    
    ASTNode(ASTNodeType t) : type(t), line(0), column(0) {}
    virtual ~ASTNode() = default;
};

// Literal (constante)
struct LiteralNode : ASTNode {
    Value value;
    
    LiteralNode() : ASTNode(ASTNodeType::LITERAL) {}
    explicit LiteralNode(const Value& v) : ASTNode(ASTNodeType::LITERAL), value(v) {}
};

// Referência a coluna
struct ColumnRefNode : ASTNode {
    std::string table;
    std::string column;
    
    ColumnRefNode() : ASTNode(ASTNodeType::COLUMN_REF) {}
    ColumnRefNode(const std::string& col) : ASTNode(ASTNodeType::COLUMN_REF), column(col) {}
    ColumnRefNode(const std::string& tbl, const std::string& col) 
        : ASTNode(ASTNodeType::COLUMN_REF), table(tbl), column(col) {}
};

// Expressão binária
struct BinaryExprNode : ASTNode {
    Operator op;
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;
    
    BinaryExprNode() : ASTNode(ASTNodeType::BINARY_EXPR) {}
    BinaryExprNode(Operator o, std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r)
        : ASTNode(ASTNodeType::BINARY_EXPR), op(o), left(std::move(l)), right(std::move(r)) {}
};

// Expressão unária
struct UnaryExprNode : ASTNode {
    Operator op;
    std::unique_ptr<ASTNode> expr;
    
    UnaryExprNode() : ASTNode(ASTNodeType::UNARY_EXPR) {}
    UnaryExprNode(Operator o, std::unique_ptr<ASTNode> e)
        : ASTNode(ASTNodeType::UNARY_EXPR), op(o), expr(std::move(e)) {}
};

// Chamada de função
struct FunctionCallNode : ASTNode {
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> arguments;
    bool is_aggregate;
    
    FunctionCallNode() : ASTNode(ASTNodeType::FUNCTION_CALL), is_aggregate(false) {}
    FunctionCallNode(const std::string& n) 
        : ASTNode(ASTNodeType::FUNCTION_CALL), name(n), is_aggregate(false) {}
};

// Expressão (usando variant)
using Expr = std::variant<
    std::unique_ptr<LiteralNode>,
    std::unique_ptr<ColumnRefNode>,
    std::unique_ptr<BinaryExprNode>,
    std::unique_ptr<UnaryExprNode>,
    std::unique_ptr<FunctionCallNode>
>;

// Definição de coluna em CREATE TABLE
struct ColumnDef {
    std::string name;
    DataType type;
    int length;  // Para VARCHAR
    bool nullable;
    bool primary_key;
    bool unique;
    std::unique_ptr<ASTNode> default_value;
    std::string check_expr;
    
    ColumnDef() : type(DataType::INTEGER), length(0), nullable(true), 
                  primary_key(false), unique(false) {}
};

// Cláusula WHERE
struct WhereClause : ASTNode {
    std::unique_ptr<ASTNode> condition;
    
    WhereClause() : ASTNode(ASTNodeType::WHERE_CLAUSE) {}
    explicit WhereClause(std::unique_ptr<ASTNode> cond) 
        : ASTNode(ASTNodeType::WHERE_CLAUSE), condition(std::move(cond)) {}
};

// Cláusula JOIN
struct JoinClause : ASTNode {
    JoinType type;
    std::string table;
    std::string alias;
    std::unique_ptr<ASTNode> condition;
    std::vector<std::string> using_columns;
    
    JoinClause() : ASTNode(ASTNodeType::JOIN_CLAUSE), type(JoinType::INNER) {}
};

// Cláusula ORDER BY
struct OrderByNode : ASTNode {
    struct Item {
        std::unique_ptr<ASTNode> expr;
        bool ascending;
    };
    std::vector<Item> items;
    
    OrderByNode() : ASTNode(ASTNodeType::ORDER_BY) {}
};

// Cláusula GROUP BY
struct GroupByNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> columns;
    
    GroupByNode() : ASTNode(ASTNodeType::GROUP_BY) {}
};

// Cláusula LIMIT
struct LimitNode : ASTNode {
    int limit;
    int offset;
    
    LimitNode() : ASTNode(ASTNodeType::LIMIT), limit(-1), offset(0) {}
    LimitNode(int l, int o) : ASTNode(ASTNodeType::LIMIT), limit(l), offset(o) {}
};

// Nó SELECT
struct SelectNode : ASTNode {
    // SELECT clause
    bool distinct;
    std::vector<std::unique_ptr<ASTNode>> select_list;
    std::vector<std::string> select_aliases;
    
    // FROM clause
    struct TableRef {
        std::string table_name;
        std::string alias;
        std::unique_ptr<SelectNode> subquery;
    };
    std::vector<TableRef> from_tables;
    
    // JOINs
    std::vector<std::unique_ptr<JoinClause>> joins;
    
    // WHERE
    std::unique_ptr<WhereClause> where;
    
    // GROUP BY
    std::unique_ptr<GroupByNode> group_by;
    
    // HAVING
    std::unique_ptr<WhereClause> having;
    
    // ORDER BY
    std::unique_ptr<OrderByNode> order_by;
    
    // LIMIT/OFFSET
    std::unique_ptr<LimitNode> limit;
    
    // UNION/INTERSECT/EXCEPT
    enum class SetOperation {
        NONE, UNION, UNION_ALL, INTERSECT, EXCEPT
    };
    SetOperation set_operation;
    std::unique_ptr<SelectNode> set_operand;
    
    SelectNode() : ASTNode(ASTNodeType::SELECT), distinct(false), 
                   set_operation(SetOperation::NONE) {}
};

// Nó INSERT
struct InsertNode : ASTNode {
    std::string table;
    std::vector<std::string> columns;
    std::vector<std::vector<std::unique_ptr<ASTNode>>> values;
    std::unique_ptr<SelectNode> select;  // INSERT ... SELECT
    
    InsertNode() : ASTNode(ASTNodeType::INSERT) {}
};

// Nó UPDATE
struct UpdateNode : ASTNode {
    std::string table;
    struct Assignment {
        std::string column;
        std::unique_ptr<ASTNode> value;
    };
    std::vector<Assignment> assignments;
    std::unique_ptr<WhereClause> where;
    
    UpdateNode() : ASTNode(ASTNodeType::UPDATE) {}
};

// Nó DELETE
struct DeleteNode : ASTNode {
    std::string table;
    std::unique_ptr<WhereClause> where;
    
    DeleteNode() : ASTNode(ASTNodeType::DELETE) {}
};

// Nó CREATE TABLE
struct CreateTableNode : ASTNode {
    std::string table_name;
    std::vector<ColumnDef> columns;
    struct Constraint {
        enum Type { PRIMARY_KEY, FOREIGN_KEY, UNIQUE, CHECK };
        Type type;
        std::vector<std::string> columns;
        std::string ref_table;
        std::vector<std::string> ref_columns;
        std::string check_expr;
    };
    std::vector<Constraint> constraints;
    
    CreateTableNode() : ASTNode(ASTNodeType::CREATE_TABLE) {}
};

// Nó CREATE INDEX
struct CreateIndexNode : ASTNode {
    std::string index_name;
    std::string table_name;
    std::vector<std::string> columns;
    bool unique;
    
    CreateIndexNode() : ASTNode(ASTNodeType::CREATE_INDEX), unique(false) {}
};

// Nó DROP TABLE
struct DropTableNode : ASTNode {
    std::string table_name;
    bool if_exists;
    
    DropTableNode() : ASTNode(ASTNodeType::DROP_TABLE), if_exists(false) {}
};

// Nó BEGIN TRANSACTION
struct BeginNode : ASTNode {
    std::string name;  // savepoint name
    bool is_savepoint;
    
    BeginNode() : ASTNode(ASTNodeType::BEGIN), is_savepoint(false) {}
};

// Nó COMMIT
struct CommitNode : ASTNode {
    std::string name;  // savepoint name
    bool is_savepoint;
    
    CommitNode() : ASTNode(ASTNodeType::COMMIT), is_savepoint(false) {}
};

// Nó ROLLBACK
struct RollbackNode : ASTNode {
    std::string name;  // savepoint name
    bool is_savepoint;
    
    RollbackNode() : ASTNode(ASTNodeType::ROLLBACK), is_savepoint(false) {}
};

// Funções auxiliares
inline std::string operatorToString(Operator op) {
    switch(op) {
        case Operator::PLUS: return "+";
        case Operator::MINUS: return "-";
        case Operator::MULTIPLY: return "*";
        case Operator::DIVIDE: return "/";
        case Operator::MODULO: return "%";
        case Operator::EQ: return "=";
        case Operator::NE: return "!=";
        case Operator::LT: return "<";
        case Operator::LE: return "<=";
        case Operator::GT: return ">";
        case Operator::GE: return ">=";
        case Operator::LIKE: return "LIKE";
        case Operator::NOT_LIKE: return "NOT LIKE";
        case Operator::IN: return "IN";
        case Operator::NOT_IN: return "NOT IN";
        case Operator::IS_NULL: return "IS NULL";
        case Operator::IS_NOT_NULL: return "IS NOT NULL";
        case Operator::BETWEEN: return "BETWEEN";
        case Operator::NOT_BETWEEN: return "NOT BETWEEN";
        case Operator::AND: return "AND";
        case Operator::OR: return "OR";
        case Operator::NOT: return "NOT";
        case Operator::ASSIGN: return ":=";
        case Operator::CONCAT: return "||";
        default: return "UNKNOWN";
    }
}

} // namespace orangesql

#endif // ORANGESQL_AST_H