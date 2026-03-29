#ifndef ORANGESQL_SQL_PARSER_H
#define ORANGESQL_SQL_PARSER_H

#include "ast.h"
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>

namespace orangesql {

// Tipos de tokens para o lexer
enum class TokenType {
    // Palavras-chave
    SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER,
    TABLE, INDEX, VIEW, TRIGGER, FUNCTION, PROCEDURE,
    FROM, WHERE, JOIN, INNER, LEFT, RIGHT, OUTER, FULL, CROSS,
    ON, USING, AND, OR, NOT, NULL, IS, IN, BETWEEN, LIKE,
    ORDER, BY, GROUP, HAVING, LIMIT, OFFSET,
    UNION, INTERSECT, EXCEPT, ALL, DISTINCT,
    AS, ASC, DESC,
    INTO, VALUES, SET,
    PRIMARY, FOREIGN, KEY, REFERENCES, CONSTRAINT,
    DEFAULT, AUTO_INCREMENT, UNIQUE, CHECK,
    BEGIN, COMMIT, ROLLBACK, TRANSACTION, SAVEPOINT,
    CASE, WHEN, THEN, ELSE, END,
    EXISTS, ANY, SOME,
    
    // Identificadores e literais
    IDENTIFIER,
    STRING,
    INTEGER,
    FLOAT,
    BOOLEAN,
    
    // Operadores
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQ, NE, LT, LE, GT, GE,
    ASSIGN,
    CONCAT,
    
    // Pontuação
    LPAREN, RPAREN,
    COMMA, DOT, SEMICOLON,
    
    // Fim do arquivo
    END_OF_FILE,
    
    // Desconhecido
    UNKNOWN
};

// Token do lexer
struct Token {
    TokenType type;
    std::string text;
    int line;
    int column;
    
    Token() : type(TokenType::UNKNOWN), line(0), column(0) {}
    Token(TokenType t, const std::string& txt, int l, int c) 
        : type(t), text(txt), line(l), column(c) {}
};

// Classe Lexer
class Lexer {
public:
    explicit Lexer(const std::string& input);
    
    Token nextToken();
    Token peekToken();
    bool hasMoreTokens() const;
    
    // Utilitários
    void reset();
    std::string getPosition() const;
    
private:
    std::string input_;
    size_t pos_;
    int line_;
    int column_;
    Token current_token_;
    
    char peek() const;
    char advance();
    void skipWhitespace();
    void skipComment();
    Token readIdentifier();
    Token readNumber();
    Token readString();
    Token readOperator();
    
    static std::unordered_map<std::string, TokenType> keywords_;
};

// Classe Parser
class SQLParser {
public:
    SQLParser() = default;
    ~SQLParser() = default;
    
    // Parse uma query SQL completa
    std::unique_ptr<ASTNode> parse(const std::string& query);
    
    // Parse específico por tipo de comando
    std::unique_ptr<SelectNode> parseSelect();
    std::unique_ptr<InsertNode> parseInsert();
    std::unique_ptr<UpdateNode> parseUpdate();
    std::unique_ptr<DeleteNode> parseDelete();
    std::unique_ptr<CreateTableNode> parseCreateTable();
    std::unique_ptr<CreateIndexNode> parseCreateIndex();
    std::unique_ptr<DropTableNode> parseDropTable();
    std::unique_ptr<BeginNode> parseBegin();
    std::unique_ptr<CommitNode> parseCommit();
    std::unique_ptr<RollbackNode> parseRollback();
    
    // Parse de expressões
    std::unique_ptr<ASTNode> parseExpression();
    std::unique_ptr<ASTNode> parseLogicalOr();
    std::unique_ptr<ASTNode> parseLogicalAnd();
    std::unique_ptr<ASTNode> parseEquality();
    std::unique_ptr<ASTNode> parseRelational();
    std::unique_ptr<ASTNode> parseAdditive();
    std::unique_ptr<ASTNode> parseMultiplicative();
    std::unique_ptr<ASTNode> parseUnary();
    std::unique_ptr<ASTNode> parsePrimary();
    
    // Validação semântica
    bool validate(const ASTNode* node);
    
    // Mensagens de erro
    std::string getLastError() const { return last_error_; }
    
private:
    std::unique_ptr<Lexer> lexer_;
    Token current_token_;
    std::string last_error_;
    
    // Consumo de tokens
    void nextToken();
    bool match(TokenType expected);
    bool matchAny(const std::vector<TokenType>& types);
    void expect(TokenType expected);
    Token consume(TokenType expected);
    
    // Utilitários
    void error(const std::string& message);
    void warning(const std::string& message);
    
    // Gramática principal
    std::unique_ptr<ASTNode> parseStatement();
    
    // Cláusulas específicas
    std::unique_ptr<WhereClause> parseWhere();
    std::unique_ptr<OrderByNode> parseOrderBy();
    std::unique_ptr<GroupByNode> parseGroupBy();
    std::unique_ptr<LimitNode> parseLimit();
    std::unique_ptr<JoinClause> parseJoin();
    
    // Listas
    std::vector<std::unique_ptr<ASTNode>> parseSelectList();
    std::vector<std::string> parseIdentifierList();
    std::vector<std::unique_ptr<ASTNode>> parseExpressionList();
    std::vector<std::vector<std::unique_ptr<ASTNode>>> parseValuesList();
    
    // Tipos de dados
    DataType parseDataType();
};

} // namespace orangesql

#endif // ORANGESQL_SQL_PARSER_H