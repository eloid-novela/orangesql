#ifndef ORANGESQL_TYPES_H
#define ORANGESQL_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <variant>
#include <optional>

namespace orangesql {

// ============================================
// IDs e Identificadores
// ============================================

using PageId = uint64_t;
using TableId = uint32_t;
using IndexId = uint32_t;
using TransactionId = uint64_t;
using ColumnId = uint16_t;
using RecordId = uint64_t;
using LogSequenceNumber = uint64_t;
using CheckpointId = uint64_t;

// ============================================
// Constantes de ID
// ============================================

constexpr PageId INVALID_PAGE_ID = UINT64_MAX;
constexpr TableId INVALID_TABLE_ID = UINT32_MAX;
constexpr IndexId INVALID_INDEX_ID = UINT32_MAX;
constexpr TransactionId INVALID_TX_ID = 0;
constexpr ColumnId INVALID_COLUMN_ID = UINT16_MAX;
constexpr RecordId INVALID_RECORD_ID = UINT64_MAX;
constexpr LogSequenceNumber INVALID_LSN = 0;

// ============================================
// Tipos de Dados Básicos
// ============================================

enum class DataType {
    UNKNOWN = 0,
    INTEGER,
    BIGINT,
    SMALLINT,
    VARCHAR,
    CHAR,
    TEXT,
    BOOLEAN,
    DATE,
    TIME,
    TIMESTAMP,
    DECIMAL,
    FLOAT,
    DOUBLE,
    BLOB,
    JSON,
    UUID,
    NULL_TYPE
};

// ============================================
// Tipos de Índice
// ============================================

enum class IndexType {
    BTREE,
    HASH,
    GIN,
    GiST,
    BRIN
};

// ============================================
// Funções de Agregação
// ============================================

enum class AggregateFunction {
    COUNT,
    SUM,
    AVG,
    MIN,
    MAX,
    GROUP_CONCAT,
    STDDEV,
    VARIANCE
};

// ============================================
// Estrutura de Valor
// ============================================

struct Value {
    DataType type;
    
    union {
        int32_t int_val;
        int64_t bigint_val;
        int16_t smallint_val;
        bool bool_val;
        double double_val;
        float float_val;
    } data;
    
    std::string str_val;
    std::vector<uint8_t> blob_val;
    
    // Construtores
    Value() : type(DataType::NULL_TYPE) {
        data.int_val = 0;
    }
    
    explicit Value(int32_t v) : type(DataType::INTEGER) {
        data.int_val = v;
    }
    
    explicit Value(int64_t v) : type(DataType::BIGINT) {
        data.bigint_val = v;
    }
    
    explicit Value(int16_t v) : type(DataType::SMALLINT) {
        data.smallint_val = v;
    }
    
    explicit Value(bool v) : type(DataType::BOOLEAN) {
        data.bool_val = v;
    }
    
    explicit Value(double v) : type(DataType::DOUBLE) {
        data.double_val = v;
    }
    
    explicit Value(float v) : type(DataType::FLOAT) {
        data.float_val = v;
    }
    
    explicit Value(const std::string& v) : type(DataType::VARCHAR), str_val(v) {}
    
    explicit Value(const char* v) : type(DataType::VARCHAR), str_val(v) {}
    
    explicit Value(const std::vector<uint8_t>& v) : type(DataType::BLOB), blob_val(v) {}
    
    // Operadores de comparação
    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
    bool operator<(const Value& other) const;
    bool operator<=(const Value& other) const;
    bool operator>(const Value& other) const;
    bool operator>=(const Value& other) const;
    
    // Verificação de nulidade
    bool isNull() const { return type == DataType::NULL_TYPE; }
    
    // Conversão para string
    std::string toString() const;
    
    // Hash
    size_t hash() const;
};

// ============================================
// Schema de Coluna
// ============================================

struct ColumnSchema {
    ColumnId id;
    std::string name;
    DataType type;
    uint32_t length;      // Para VARCHAR/CHAR
    int precision;        // Para DECIMAL
    int scale;           // Para DECIMAL
    bool nullable;
    bool is_primary_key;
    bool is_unique;
    bool is_auto_increment;
    std::string default_value;
    std::string comment;
    
    ColumnSchema() : id(0), type(DataType::INTEGER), length(0), precision(10),
                     scale(0), nullable(true), is_primary_key(false),
                     is_unique(false), is_auto_increment(false) {}
    
    size_t getSizeInBytes() const;
};

// ============================================
// Schema de Tabela
// ============================================

struct TableSchema {
    TableId id;
    std::string name;
    std::vector<ColumnSchema> columns;
    std::vector<ColumnId> primary_key;
    std::unordered_map<std::string, IndexId> indexes;
    std::unordered_map<std::string, std::string> options;
    
    TableSchema() : id(0) {}
    
    ColumnId getColumnId(const std::string& name) const;
    size_t getColumnIndex(const std::string& name) const;
    size_t getRecordSize() const;
    bool hasColumn(const std::string& name) const;
    
    // Validação
    bool validateRecord(const std::vector<Value>& record) const;
};

// ============================================
// Estatísticas de Execução
// ============================================

struct ExecutionStats {
    size_t rows_scanned;
    size_t rows_returned;
    size_t pages_read;
    size_t pages_written;
    size_t index_lookups;
    double execution_time_ms;
    size_t memory_used_bytes;
    size_t network_bytes;
    
    ExecutionStats() : rows_scanned(0), rows_returned(0), pages_read(0),
                       pages_written(0), index_lookups(0), execution_time_ms(0),
                       memory_used_bytes(0), network_bytes(0) {}
    
    void reset() {
        rows_scanned = 0;
        rows_returned = 0;
        pages_read = 0;
        pages_written = 0;
        index_lookups = 0;
        execution_time_ms = 0;
        memory_used_bytes = 0;
        network_bytes = 0;
    }
    
    void merge(const ExecutionStats& other) {
        rows_scanned += other.rows_scanned;
        rows_returned += other.rows_returned;
        pages_read += other.pages_read;
        pages_written += other.pages_written;
        index_lookups += other.index_lookups;
        execution_time_ms += other.execution_time_ms;
        memory_used_bytes = std::max(memory_used_bytes, other.memory_used_bytes);
        network_bytes += other.network_bytes;
    }
};

// ============================================
// Alias de Tipos Comuns
// ============================================

using ValueList = std::vector<Value>;
using Row = std::vector<Value>;
using ResultSet = std::vector<Row>;

// ============================================
// Implementação inline de métodos
// ============================================

inline bool Value::operator==(const Value& other) const {
    if (type != other.type) return false;
    
    switch (type) {
        case DataType::INTEGER:
            return data.int_val == other.data.int_val;
        case DataType::BIGINT:
            return data.bigint_val == other.data.bigint_val;
        case DataType::SMALLINT:
            return data.smallint_val == other.data.smallint_val;
        case DataType::BOOLEAN:
            return data.bool_val == other.data.bool_val;
        case DataType::DOUBLE:
            return data.double_val == other.data.double_val;
        case DataType::FLOAT:
            return data.float_val == other.data.float_val;
        case DataType::VARCHAR:
        case DataType::CHAR:
        case DataType::TEXT:
            return str_val == other.str_val;
        case DataType::BLOB:
            return blob_val == other.blob_val;
        case DataType::NULL_TYPE:
            return true;
        default:
            return false;
    }
}

inline bool Value::operator<(const Value& other) const {
    if (type != other.type) {
        return static_cast<int>(type) < static_cast<int>(other.type);
    }
    
    switch (type) {
        case DataType::INTEGER:
            return data.int_val < other.data.int_val;
        case DataType::BIGINT:
            return data.bigint_val < other.data.bigint_val;
        case DataType::SMALLINT:
            return data.smallint_val < other.data.smallint_val;
        case DataType::BOOLEAN:
            return !data.bool_val && other.data.bool_val;
        case DataType::DOUBLE:
            return data.double_val < other.data.double_val;
        case DataType::FLOAT:
            return data.float_val < other.data.float_val;
        case DataType::VARCHAR:
        case DataType::CHAR:
        case DataType::TEXT:
            return str_val < other.str_val;
        default:
            return false;
    }
}

inline bool Value::operator<=(const Value& other) const {
    return *this < other || *this == other;
}

inline bool Value::operator>(const Value& other) const {
    return !(*this <= other);
}

inline bool Value::operator>=(const Value& other) const {
    return !(*this < other);
}

inline std::string Value::toString() const {
    switch (type) {
        case DataType::INTEGER:
            return std::to_string(data.int_val);
        case DataType::BIGINT:
            return std::to_string(data.bigint_val);
        case DataType::SMALLINT:
            return std::to_string(data.smallint_val);
        case DataType::BOOLEAN:
            return data.bool_val ? "true" : "false";
        case DataType::DOUBLE:
            return std::to_string(data.double_val);
        case DataType::FLOAT:
            return std::to_string(data.float_val);
        case DataType::VARCHAR:
        case DataType::CHAR:
        case DataType::TEXT:
            return str_val;
        case DataType::NULL_TYPE:
            return "NULL";
        default:
            return "?";
    }
}

inline size_t Value::hash() const {
    size_t h = static_cast<size_t>(type);
    
    switch (type) {
        case DataType::INTEGER:
            h ^= std::hash<int32_t>{}(data.int_val);
            break;
        case DataType::BIGINT:
            h ^= std::hash<int64_t>{}(data.bigint_val);
            break;
        case DataType::SMALLINT:
            h ^= std::hash<int16_t>{}(data.smallint_val);
            break;
        case DataType::BOOLEAN:
            h ^= std::hash<bool>{}(data.bool_val);
            break;
        case DataType::DOUBLE:
            h ^= std::hash<double>{}(data.double_val);
            break;
        case DataType::FLOAT:
            h ^= std::hash<float>{}(data.float_val);
            break;
        case DataType::VARCHAR:
        case DataType::CHAR:
        case DataType::TEXT:
            h ^= std::hash<std::string>{}(str_val);
            break;
        case DataType::BLOB:
            for (uint8_t byte : blob_val) {
                h ^= std::hash<uint8_t>{}(byte);
            }
            break;
        default:
            break;
    }
    
    return h;
}

inline size_t ColumnSchema::getSizeInBytes() const {
    switch (type) {
        case DataType::INTEGER:
            return 4;
        case DataType::BIGINT:
            return 8;
        case DataType::SMALLINT:
            return 2;
        case DataType::BOOLEAN:
            return 1;
        case DataType::DOUBLE:
            return 8;
        case DataType::FLOAT:
            return 4;
        case DataType::VARCHAR:
        case DataType::CHAR:
        case DataType::TEXT:
            return length + 4; // 4 bytes para prefixo de tamanho
        default:
            return 8;
    }
}

inline ColumnId TableSchema::getColumnId(const std::string& name) const {
    for (const auto& col : columns) {
        if (col.name == name) {
            return col.id;
        }
    }
    return INVALID_COLUMN_ID;
}

inline size_t TableSchema::getColumnIndex(const std::string& name) const {
    for (size_t i = 0; i < columns.size(); i++) {
        if (columns[i].name == name) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

inline size_t TableSchema::getRecordSize() const {
    size_t size = 0;
    for (const auto& col : columns) {
        size += col.getSizeInBytes();
    }
    return size;
}

inline bool TableSchema::hasColumn(const std::string& name) const {
    return getColumnIndex(name) != static_cast<size_t>(-1);
}

inline bool TableSchema::validateRecord(const std::vector<Value>& record) const {
    if (record.size() != columns.size()) {
        return false;
    }
    
    for (size_t i = 0; i < columns.size(); i++) {
        const auto& col = columns[i];
        const auto& val = record[i];
        
        // Verificar nulidade
        if (val.isNull() && !col.nullable) {
            return false;
        }
        
        // Verificar tipo
        if (!val.isNull() && val.type != col.type) {
            return false;
        }
        
        // Verificar tamanho de string
        if (!val.isNull() && (col.type == DataType::VARCHAR || col.type == DataType::CHAR)) {
            if (val.str_val.length() > col.length) {
                return false;
            }
        }
    }
    
    return true;
}

} // namespace orangesql

// ============================================
// Especialização de hash para Value
// ============================================

namespace std {
    template<>
    struct hash<orangesql::Value> {
        size_t operator()(const orangesql::Value& v) const {
            return v.hash();
        }
    };
}

#endif // ORANGESQL_TYPES_H