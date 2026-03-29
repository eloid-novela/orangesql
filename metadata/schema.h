#ifndef ORANGESQL_SCHEMA_H
#define ORANGESQL_SCHEMA_H

#include "../include/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace orangesql {

// Tipo de dado estendido
enum class ExtendedDataType {
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
    ARRAY,
    ENUM
};

// Atributos de coluna
struct ColumnAttributes {
    bool nullable;
    bool unique;
    bool primary_key;
    bool auto_increment;
    std::string default_value;
    std::string check_expr;
    std::string comment;
    
    ColumnAttributes() : nullable(true), unique(false), primary_key(false),
                        auto_increment(false) {}
};

// Coluna do schema
class Column {
public:
    Column();
    Column(const std::string& name, ExtendedDataType type);
    ~Column() = default;
    
    // Getters/Setters
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
    
    ExtendedDataType getType() const { return type_; }
    void setType(ExtendedDataType type) { type_ = type; }
    
    size_t getLength() const { return length_; }
    void setLength(size_t length) { length_ = length; }
    
    int getPrecision() const { return precision_; }
    void setPrecision(int precision) { precision_ = precision; }
    
    int getScale() const { return scale_; }
    void setScale(int scale) { scale_ = scale; }
    
    const ColumnAttributes& getAttributes() const { return attributes_; }
    ColumnAttributes& getAttributes() { return attributes_; }
    
    ColumnId getId() const { return id_; }
    void setId(ColumnId id) { id_ = id; }
    
    // Validação
    bool validateValue(const Value& value) const;
    size_t getSizeInBytes() const;
    
    // Serialização
    std::string toString() const;
    static Column fromString(const std::string& str);

private:
    ColumnId id_;
    std::string name_;
    ExtendedDataType type_;
    size_t length_;      // Para VARCHAR/CHAR
    int precision_;       // Para DECIMAL
    int scale_;          // Para DECIMAL
    ColumnAttributes attributes_;
    
    std::vector<std::string> enum_values_;  // Para ENUM
};

// Tabela do schema
class Table {
public:
    Table();
    Table(const std::string& name, TableId id = 0);
    ~Table() = default;
    
    // Getters/Setters
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
    
    TableId getId() const { return id_; }
    void setId(TableId id) { id_ = id; }
    
    // Colunas
    void addColumn(const Column& column);
    void addColumn(const std::string& name, ExtendedDataType type);
    bool hasColumn(const std::string& name) const;
    
    Column* getColumn(const std::string& name);
    const Column* getColumn(const std::string& name) const;
    
    Column* getColumn(size_t index);
    const Column* getColumn(size_t index) const;
    
    size_t getColumnCount() const { return columns_.size(); }
    size_t getColumnIndex(const std::string& name) const;
    
    const std::vector<Column>& getColumns() const { return columns_; }
    std::vector<Column>& getColumns() { return columns_; }
    
    // Primary Key
    void setPrimaryKey(const std::vector<std::string>& columns);
    std::vector<size_t> getPrimaryKeyIndices() const;
    bool isPrimaryKeyColumn(const std::string& name) const;
    
    // Índices
    void addIndex(const std::string& name, const std::vector<std::string>& columns);
    bool hasIndex(const std::string& name) const;
    
    // Chaves estrangeiras
    void addForeignKey(const std::string& name, const std::vector<std::string>& columns,
                      const std::string& ref_table, const std::vector<std::string>& ref_columns);
    
    // Opções da tabela
    void setOption(const std::string& key, const std::string& value);
    std::string getOption(const std::string& key) const;
    
    // Tamanho do registro
    size_t getRecordSize() const;
    
    // Validação
    bool validateRecord(const std::vector<Value>& record) const;
    
    // Serialização
    std::string toString() const;
    static Table fromString(const std::string& str);

private:
    TableId id_;
    std::string name_;
    std::vector<Column> columns_;
    std::unordered_map<std::string, size_t> column_map_;
    
    struct Index {
        std::string name;
        std::vector<size_t> column_indices;
    };
    std::unordered_map<std::string, Index> indexes_;
    
    struct ForeignKey {
        std::string name;
        std::vector<size_t> column_indices;
        std::string ref_table;
        std::vector<std::string> ref_columns;
    };
    std::vector<ForeignKey> foreign_keys_;
    
    std::unordered_map<std::string, std::string> options_;
    
    std::vector<size_t> primary_key_;
};

} // namespace orangesql

#endif // ORANGESQL_SCHEMA_H