#include "sql_parser.h"
#include <cctype>
#include <sstream>
#include <algorithm>

namespace orangesql {

// Mapa de palavras-chave
std::unordered_map<std::string, TokenType> Lexer::keywords_ = {
    // DML
    {"SELECT", TokenType::SELECT},
    {"INSERT", TokenType::INSERT},
    {"UPDATE", TokenType::UPDATE},
    {"DELETE", TokenType::DELETE},
    {"FROM", TokenType::FROM},
    {"WHERE", TokenType::WHERE},
    {"JOIN", TokenType::JOIN},
    {"INNER", TokenType::INNER},
    {"LEFT", TokenType::LEFT},
    {"RIGHT", TokenType::RIGHT},
    {"OUTER", TokenType::OUTER},
    {"FULL", TokenType::FULL},
    {"CROSS", TokenType::CROSS},
    {"ON", TokenType::ON},
    {"USING", TokenType::USING},
    {"AND", TokenType::AND},
    {"OR", TokenType::OR},
    {"NOT", TokenType::NOT},
    {"NULL", TokenType::NULL},
    {"IS", TokenType::IS},
    {"IN", TokenType::IN},
    {"BETWEEN", TokenType::BETWEEN},
    {"LIKE", TokenType::LIKE},
    {"ORDER", TokenType::ORDER},
    {"BY", TokenType::BY},
    {"GROUP", TokenType::GROUP},
    {"HAVING", TokenType::HAVING},
    {"LIMIT", TokenType::LIMIT},
    {"OFFSET", TokenType::OFFSET},
    {"UNION", TokenType::UNION},
    {"INTERSECT", TokenType::INTERSECT},
    {"EXCEPT", TokenType::EXCEPT},
    {"ALL", TokenType::ALL},
    {"DISTINCT", TokenType::DISTINCT},
    {"AS", TokenType::AS},
    {"ASC", TokenType::ASC},
    {"DESC", TokenType::DESC},
    {"INTO", TokenType::INTO},
    {"VALUES", TokenType::VALUES},
    {"SET", TokenType::SET},
    
    // DDL
    {"CREATE", TokenType::CREATE},
    {"DROP", TokenType::DROP},
    {"ALTER", TokenType::ALTER},
    {"TABLE", TokenType::TABLE},
    {"INDEX", TokenType::INDEX},
    {"VIEW", TokenType::VIEW},
    {"PRIMARY", TokenType::PRIMARY},
    {"FOREIGN", TokenType::FOREIGN},
    {"KEY", TokenType::KEY},
    {"REFERENCES", TokenType::REFERENCES},
    {"CONSTRAINT", TokenType::CONSTRAINT},
    {"DEFAULT", TokenType::DEFAULT},
    {"AUTO_INCREMENT", TokenType::AUTO_INCREMENT},
    {"UNIQUE", TokenType::UNIQUE},
    {"CHECK", TokenType::CHECK},
    
    // Transações
    {"BEGIN", TokenType::BEGIN},
    {"COMMIT", TokenType::COMMIT},
    {"ROLLBACK", TokenType::ROLLBACK},
    {"TRANSACTION", TokenType::TRANSACTION},
    {"SAVEPOINT", TokenType::SAVEPOINT},
    
    // Expressões
    {"CASE", TokenType::CASE},
    {"WHEN", TokenType::WHEN},
    {"THEN", TokenType::THEN},
    {"ELSE", TokenType::ELSE},
    {"END", TokenType::END},
    {"EXISTS", TokenType::EXISTS},
    {"ANY", TokenType::ANY},
    {"SOME", TokenType::SOME},
    
    // Booleanos
    {"TRUE", TokenType::BOOLEAN},
    {"FALSE", TokenType::BOOLEAN}
};

Lexer::Lexer(const std::string& input) 
    : input_(input), pos_(0), line_(1), column_(1) {
    // Converter para uppercase para facilitar matching
    std::transform(input_.begin(), input_.end(), input_.begin(), ::toupper);
}

Token Lexer::nextToken() {
    skipWhitespace();
    skipComment();
    
    if (pos_ >= input_.length()) {
        return Token(TokenType::END_OF_FILE, "", line_, column_);
    }
    
    char c = peek();
    int start_line = line_;
    int start_col = column_;
    
    // Identificadores e palavras-chave
    if (isalpha(c) || c == '_') {
        return readIdentifier();
    }
    
    // Números
    if (isdigit(c)) {
        return readNumber();
    }
    
    // Strings
    if (c == '\'' || c == '"') {
        return readString();
    }
    
    // Operadores e pontuação
    return readOperator();
}

Token Lexer::peekToken() {
    size_t old_pos = pos_;
    int old_line = line_;
    int old_column = column_;
    Token token = nextToken();
    
    pos_ = old_pos;
    line_ = old_line;
    column_ = old_column;
    
    return token;
}

bool Lexer::hasMoreTokens() const {
    return pos_ < input_.length();
}

void Lexer::reset() {
    pos_ = 0;
    line_ = 1;
    column_ = 1;
}

std::string Lexer::getPosition() const {
    return "line " + std::to_string(line_) + ", column " + std::to_string(column_);
}

char Lexer::peek() const {
    if (pos_ >= input_.length()) return '\0';
    return input_[pos_];
}

char Lexer::advance() {
    if (pos_ >= input_.length()) return '\0';
    char c = input_[pos_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

void Lexer::skipWhitespace() {
    while (pos_ < input_.length() && isspace(input_[pos_])) {
        advance();
    }
}

void Lexer::skipComment() {
    // Comentários de linha: -- ou //
    if (pos_ + 1 < input_.length()) {
        if ((input_[pos_] == '-' && input_[pos_ + 1] == '-') ||
            (input_[pos_] == '/' && input_[pos_ + 1] == '/')) {
            // Pular até o final da linha
            while (pos_ < input_.length() && input_[pos_] != '\n') {
                advance();
            }
        }
        // Comentários multi-linha: /* ... */
        else if (input_[pos_] == '/' && input_[pos_ + 1] == '*') {
            advance(); // Skip '/'
            advance(); // Skip '*'
            while (pos_ + 1 < input_.length()) {
                if (input_[pos_] == '*' && input_[pos_ + 1] == '/') {
                    advance(); // Skip '*'
                    advance(); // Skip '/'
                    break;
                }
                advance();
            }
        }
    }
}

Token Lexer::readIdentifier() {
    std::string text;
    int start_line = line_;
    int start_col = column_;
    
    while (isalnum(peek()) || peek() == '_' || peek() == '$') {
        text += advance();
    }
    
    // Verificar se é palavra-chave
    auto it = keywords_.find(text);
    if (it != keywords_.end()) {
        return Token(it->second, text, start_line, start_col);
    }
    
    return Token(TokenType::IDENTIFIER, text, start_line, start_col);
}

Token Lexer::readNumber() {
    std::string text;
    int start_line = line_;
    int start_col = column_;
    bool is_float = false;
    
    while (isdigit(peek())) {
        text += advance();
    }
    
    // Parte decimal
    if (peek() == '.') {
        is_float = true;
        text += advance();
        while (isdigit(peek())) {
            text += advance();
        }
    }
    
    // Notação científica
    if (peek() == 'e' || peek() == 'E') {
        is_float = true;
        text += advance();
        if (peek() == '+' || peek() == '-') {
            text += advance();
        }
        while (isdigit(peek())) {
            text += advance();
        }
    }
    
    return Token(is_float ? TokenType::FLOAT : TokenType::INTEGER, 
                 text, start_line, start_col);
}

Token Lexer::readString() {
    char quote = advance(); // Guarda o tipo de aspas
    std::string text;
    int start_line = line_;
    int start_col = column_;
    
    while (pos_ < input_.length()) {
        char c = advance();
        if (c == quote) {
            break;
        }
        if (c == '\\') { // Escape
            if (pos_ < input_.length()) {
                text += advance();
            }
        } else {
            text += c;
        }
    }
    
    return Token(TokenType::STRING, text, start_line, start_col);
}

Token Lexer::readOperator() {
    char c = advance();
    int start_line = line_;
    int start_col = column_;
    
    // Operadores de dois caracteres
    if (c == '=' && peek() == '=') {
        advance();
        return Token(TokenType::EQ, "==", start_line, start_col);
    }
    if (c == '!' && peek() == '=') {
        advance();
        return Token(TokenType::NE, "!=", start_line, start_col);
    }
    if (c == '<' && peek() == '=') {
        advance();
        return Token(TokenType::LE, "<=", start_line, start_col);
    }
    if (c == '>' && peek() == '=') {
        advance();
        return Token(TokenType::GE, ">=", start_line, start_col);
    }
    if (c == '|' && peek() == '|') {
        advance();
        return Token(TokenType::CONCAT, "||", start_line, start_col);
    }
    if (c == ':' && peek() == '=') {
        advance();
        return Token(TokenType::ASSIGN, ":=", start_line, start_col);
    }
    
    // Operadores de um caractere
    switch (c) {
        case '+': return Token(TokenType::PLUS, "+", start_line, start_col);
        case '-': return Token(TokenType::MINUS, "-", start_line, start_col);
        case '*': return Token(TokenType::STAR, "*", start_line, start_col);
        case '/': return Token(TokenType::SLASH, "/", start_line, start_col);
        case '%': return Token(TokenType::PERCENT, "%", start_line, start_col);
        case '=': return Token(TokenType::ASSIGN, "=", start_line, start_col);
        case '<': return Token(TokenType::LT, "<", start_line, start_col);
        case '>': return Token(TokenType::GT, ">", start_line, start_col);
        case '(': return Token(TokenType::LPAREN, "(", start_line, start_col);
        case ')': return Token(TokenType::RPAREN, ")", start_line, start_col);
        case ',': return Token(TokenType::COMMA, ",", start_line, start_col);
        case '.': return Token(TokenType::DOT, ".", start_line, start_col);
        case ';': return Token(TokenType::SEMICOLON, ";", start_line, start_col);
    }
    
    return Token(TokenType::UNKNOWN, std::string(1, c), start_line, start_col);
}

} // namespace orangesql