#ifndef ORANGESQL_STATUS_H
#define ORANGESQL_STATUS_H

#include <string>
#include <iostream>

namespace orangesql {

// ============================================
// Status Code Enum
// ============================================

enum class StatusCode {
    // Success (0-99)
    OK = 0,
    DONE = 1,
    NOT_FOUND = 2,
    ALREADY_EXISTS = 3,
    
    // General errors (100-199)
    ERROR = 100,
    NOT_IMPLEMENTED = 101,
    UNSUPPORTED = 102,
    INVALID_ARGUMENT = 103,
    OUT_OF_RANGE = 104,
    
    // Storage errors (200-299)
    IO_ERROR = 200,
    CORRUPTED_DATA = 201,
    NO_SPACE = 202,
    PAGE_NOT_FOUND = 203,
    
    // Transaction errors (300-399)
    TRANSACTION_ERROR = 300,
    DEADLOCK = 301,
    SERIALIZATION = 302,
    LOCK_TIMEOUT = 303,
    TRANSACTION_ABORTED = 304,
    
    // Constraint errors (400-499)
    CONSTRAINT_VIOLATION = 400,
    UNIQUE_VIOLATION = 401,
    FOREIGN_KEY_VIOLATION = 402,
    CHECK_VIOLATION = 403,
    NOT_NULL_VIOLATION = 404,
    
    // Permission errors (500-599)
    PERMISSION_DENIED = 500,
    UNAUTHORIZED = 501,
    
    // Resource errors (600-699)
    OUT_OF_MEMORY = 600,
    OUT_OF_DISK = 601,
    TOO_MANY_CONNECTIONS = 602,
    
    // Network errors (700-799)
    CONNECTION_ERROR = 700,
    TIMEOUT = 701,
    
    // Parser errors (800-899)
    SYNTAX_ERROR = 800,
    SEMANTIC_ERROR = 801,
    
    // Index errors (900-999)
    INDEX_ERROR = 900,
    DUPLICATE_KEY = 901,
    KEY_NOT_FOUND = 902
};

// ============================================
// Classe Status com mensagem
// ============================================

class Status {
public:
    // Construtores
    Status() : code_(StatusCode::OK), message_() {}
    
    Status(StatusCode code) : code_(code), message_() {}
    
    Status(StatusCode code, const std::string& msg) 
        : code_(code), message_(msg) {}
    
    Status(const Status& other) = default;
    Status& operator=(const Status& other) = default;
    
    Status(Status&& other) noexcept = default;
    Status& operator=(Status&& other) noexcept = default;
    
    // Fábricas de status comuns
    static Status OK() { return Status(StatusCode::OK); }
    static Status OK(const std::string& msg) { return Status(StatusCode::OK, msg); }
    
    static Status Error(const std::string& msg = "") { 
        return Status(StatusCode::ERROR, msg); 
    }
    
    static Status NotFound(const std::string& msg = "") { 
        return Status(StatusCode::NOT_FOUND, msg); 
    }
    
    static Status AlreadyExists(const std::string& msg = "") { 
        return Status(StatusCode::ALREADY_EXISTS, msg); 
    }
    
    static Status NotImplemented(const std::string& msg = "") { 
        return Status(StatusCode::NOT_IMPLEMENTED, msg); 
    }
    
    static Status InvalidArgument(const std::string& msg = "") { 
        return Status(StatusCode::INVALID_ARGUMENT, msg); 
    }
    
    static Status IOError(const std::string& msg = "") { 
        return Status(StatusCode::IO_ERROR, msg); 
    }
    
    static Status Deadlock(const std::string& msg = "") { 
        return Status(StatusCode::DEADLOCK, msg); 
    }
    
    static Status ConstraintViolation(const std::string& msg = "") { 
        return Status(StatusCode::CONSTRAINT_VIOLATION, msg); 
    }
    
    static Status UniqueViolation(const std::string& msg = "") { 
        return Status(StatusCode::UNIQUE_VIOLATION, msg); 
    }
    
    static Status PermissionDenied(const std::string& msg = "") { 
        return Status(StatusCode::PERMISSION_DENIED, msg); 
    }
    
    static Status OutOfMemory(const std::string& msg = "") { 
        return Status(StatusCode::OUT_OF_MEMORY, msg); 
    }
    
    static Status SyntaxError(const std::string& msg = "") { 
        return Status(StatusCode::SYNTAX_ERROR, msg); 
    }
    
    // Verificadores
    bool ok() const { return code_ == StatusCode::OK; }
    bool isError() const { return !ok(); }
    bool isNotFound() const { return code_ == StatusCode::NOT_FOUND; }
    bool isAlreadyExists() const { return code_ == StatusCode::ALREADY_EXISTS; }
    bool isDeadlock() const { return code_ == StatusCode::DEADLOCK; }
    bool isConstraintViolation() const { 
        return code_ == StatusCode::CONSTRAINT_VIOLATION ||
               code_ == StatusCode::UNIQUE_VIOLATION ||
               code_ == StatusCode::FOREIGN_KEY_VIOLATION ||
               code_ == StatusCode::CHECK_VIOLATION ||
               code_ == StatusCode::NOT_NULL_VIOLATION;
    }
    
    // Getters
    StatusCode code() const { return code_; }
    const std::string& message() const { return message_; }
    
    // Conversão para string
    std::string toString() const {
        std::string result = codeToString(code_);
        if (!message_.empty()) {
            result += ": " + message_;
        }
        return result;
    }
    
    // Operador de conversão para bool
    explicit operator bool() const { return ok(); }
    
    // Operador de comparação
    bool operator==(const Status& other) const { 
        return code_ == other.code_; 
    }
    
    bool operator!=(const Status& other) const { 
        return !(*this == other); 
    }

private:
    StatusCode code_;
    std::string message_;
    
    static std::string codeToString(StatusCode code) {
        switch (code) {
            case StatusCode::OK: return "OK";
            case StatusCode::DONE: return "DONE";
            case StatusCode::NOT_FOUND: return "NOT_FOUND";
            case StatusCode::ALREADY_EXISTS: return "ALREADY_EXISTS";
            case StatusCode::ERROR: return "ERROR";
            case StatusCode::NOT_IMPLEMENTED: return "NOT_IMPLEMENTED";
            case StatusCode::UNSUPPORTED: return "UNSUPPORTED";
            case StatusCode::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
            case StatusCode::OUT_OF_RANGE: return "OUT_OF_RANGE";
            case StatusCode::IO_ERROR: return "IO_ERROR";
            case StatusCode::CORRUPTED_DATA: return "CORRUPTED_DATA";
            case StatusCode::NO_SPACE: return "NO_SPACE";
            case StatusCode::PAGE_NOT_FOUND: return "PAGE_NOT_FOUND";
            case StatusCode::TRANSACTION_ERROR: return "TRANSACTION_ERROR";
            case StatusCode::DEADLOCK: return "DEADLOCK";
            case StatusCode::SERIALIZATION: return "SERIALIZATION";
            case StatusCode::LOCK_TIMEOUT: return "LOCK_TIMEOUT";
            case StatusCode::TRANSACTION_ABORTED: return "TRANSACTION_ABORTED";
            case StatusCode::CONSTRAINT_VIOLATION: return "CONSTRAINT_VIOLATION";
            case StatusCode::UNIQUE_VIOLATION: return "UNIQUE_VIOLATION";
            case StatusCode::FOREIGN_KEY_VIOLATION: return "FOREIGN_KEY_VIOLATION";
            case StatusCode::CHECK_VIOLATION: return "CHECK_VIOLATION";
            case StatusCode::NOT_NULL_VIOLATION: return "NOT_NULL_VIOLATION";
            case StatusCode::PERMISSION_DENIED: return "PERMISSION_DENIED";
            case StatusCode::UNAUTHORIZED: return "UNAUTHORIZED";
            case StatusCode::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
            case StatusCode::OUT_OF_DISK: return "OUT_OF_DISK";
            case StatusCode::TOO_MANY_CONNECTIONS: return "TOO_MANY_CONNECTIONS";
            case StatusCode::CONNECTION_ERROR: return "CONNECTION_ERROR";
            case StatusCode::TIMEOUT: return "TIMEOUT";
            case StatusCode::SYNTAX_ERROR: return "SYNTAX_ERROR";
            case StatusCode::SEMANTIC_ERROR: return "SEMANTIC_ERROR";
            case StatusCode::INDEX_ERROR: return "INDEX_ERROR";
            case StatusCode::DUPLICATE_KEY: return "DUPLICATE_KEY";
            case StatusCode::KEY_NOT_FOUND: return "KEY_NOT_FOUND";
            default: return "UNKNOWN";
        }
    }
};

// ============================================
// Sobrecarga de operador de stream
// ============================================

inline std::ostream& operator<<(std::ostream& os, const Status& status) {
    os << status.toString();
    return os;
}

// ============================================
// Macros de verificação de status
// ============================================

#define ORANGESQL_CHECK(status) \
    do { \
        if (!(status).ok()) { \
            return (status); \
        } \
    } while (0)

#define ORANGESQL_ASSERT_OK(status) \
    do { \
        if (!(status).ok()) { \
            std::cerr << "Assertion failed: " << (status) \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)

#define ORANGESQL_RETURN_IF_ERROR(expr) \
    do { \
        Status _status = (expr); \
        if (!_status.ok()) return _status; \
    } while (0)

#define ORANGESQL_TRY(expr, error_msg) \
    do { \
        if (!(expr)) { \
            return Status::Error(error_msg); \
        } \
    } while (0)

} // namespace orangesql

#endif // ORANGESQL_STATUS_H