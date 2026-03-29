#include "schema.h"
#include <sstream>
#include <algorithm>

namespace orangesql {

// ============================================
// Column Implementation
// ============================================

Column::Column() 
    : id_(0)
    , type_(ExtendedDataType::INTEGER)
    , length_(0)
    , precision_(10)
    , scale_(0) {
}

Column::Column(const std::string& name, ExtendedDataType type)
    : id_(0)
    , name_(name)
    , type_(type)
    , length_(0)
    , precision_(10)
    , scale_(0) {
    
    // Set default length for string types
    if (type == ExtendedDataType::VARCHAR) {
        length_ = 255;
    } else if (type == ExtendedDataType::CHAR) {
        length_ = 1;
    }
}

bool Column::validateValue(const Value& value) const {
    // Check nullability
    if (!attributes_.nullable) {
        if (value.type == DataType::INTEGER && value.data.int_val == 0 && 
            value.str_val.empty()) {
            return false; // NULL value
        }
    }
    
    // Check type compatibility
    DataType basic_type;
    switch (type_) {
        case ExtendedDataType::INTEGER:
        case ExtendedDataType::SMALLINT:
            basic_type = DataType::INTEGER;
            break;
        case ExtendedDataType::BIGINT:
            basic_type = DataType::BIGINT;
            break;
        case ExtendedDataType::VARCHAR:
        case ExtendedDataType::CHAR:
        case ExtendedDataType::TEXT:
            basic_type = DataType::VARCHAR;
            break;
        case ExtendedDataType::BOOLEAN:
            basic_type = DataType::BOOLEAN;
            break;
        default:
            basic_type = DataType::INTEGER;
    }
    
    if (value.type != basic_type) {
        return false;
    }
    
    // Check string length
    if (basic_type == DataType::VARCHAR && length_ > 0) {
        if (value.str_val.length() > length_) {
            return false;
        }
    }
    
    // Check unique constraint would be handled by index
    
    return true;
}

size_t Column::getSizeInBytes() const {
    switch (type_) {
        case ExtendedDataType::INTEGER:
        case ExtendedDataType::SMALLINT:
            return 4;
        case ExtendedDataType::BIGINT:
            return 8;
        case ExtendedDataType::BOOLEAN:
            return 1;
        case ExtendedDataType::VARCHAR:
        case ExtendedDataType::CHAR:
        case ExtendedDataType::TEXT:
            return length_ + 4; // 4 bytes for length prefix
        default:
            return 8;
    }
}

std::string Column::toString() const {
    std::stringstream ss;
    
    ss << id_ << "|";
    ss << name_ << "|";
    ss << static_cast<int>(type_) << "|";
    ss << length_ << "|";
    ss << precision_ << "|";
    ss << scale_ << "|";
    ss << attributes_.nullable << "|";
    ss << attributes_.unique << "|";
    ss << attributes_.primary_key << "|";
    ss << attributes_.auto_increment << "|";
    ss << attributes_.default_value << "|";
    ss << attributes_.check_expr << "|";
    ss << attributes_.comment;
    
    return ss.str();
}

Column Column::fromString(const std::string& str) {
    Column col;
    std::stringstream ss(str);
    std::string token;
    
    std::getline(ss, token, '|'); col.id_ = std::stoull(token);
    std::getline(ss, token, '|'); col.name_ = token;
    std::getline(ss, token, '|'); col.type_ = static_cast<ExtendedDataType>(std::stoi(token));
    std::getline(ss, token, '|'); col.length_ = std::stoull(token);
    std::getline(ss, token, '|'); col.precision_ = std::stoi(token);
    std::getline(ss, token, '|'); col.scale_ = std::stoi(token);
    
    std::getline(ss, token, '|'); col.attributes_.nullable = (token == "1");
    std::getline(ss, token, '|'); col.attributes_.unique = (token == "1");
    std::getline(ss, token, '|'); col.attributes_.primary_key = (token == "1");
    std::getline(ss, token, '|'); col.attributes_.auto_increment = (token == "1");
    std::getline(ss, token, '|'); col.attributes_.default_value = token;
    std::getline(ss, token, '|'); col.attributes_.check_expr = token;
    std::getline(ss, token, '|'); col.attributes_.comment = token;
    
    return col;
}

// ============================================
// Table Implementation
// ============================================

Table::Table() : id_(0) {
}

Table::Table(const std::string& name, TableId id)
    : id_(id), name_(name) {
}

void Table::addColumn(const Column& column) {
    Column new_col = column;
    new_col.setId(columns_.size());
    columns_.push_back(new_col);
    column_map_[column.getName()] = columns_.size() - 1;
}

void Table::addColumn(const std::string& name, ExtendedDataType type) {
    Column col(name, type);
    col.setId(columns_.size());
    columns_.push_back(col);
    column_map_[name] = columns_.size() - 1;
}

bool Table::hasColumn(const std::string& name) const {
    return column_map_.find(name) != column_map_.end();
}

Column* Table::getColumn(const std::string& name) {
    auto it = column_map_.find(name);
    if (it != column_map_.end()) {
        return &columns_[it->second];
    }
    return nullptr;
}

const Column* Table::getColumn(const std::string& name) const {
    auto it = column_map_.find(name);
    if (it != column_map_.end()) {
        return &columns_[it->second];
    }
    return nullptr;
}

Column* Table::getColumn(size_t index) {
    if (index < columns_.size()) {
        return &columns_[index];
    }
    return nullptr;
}

const Column* Table::getColumn(size_t index) const {
    if (index < columns_.size()) {
        return &columns_[index];
    }
    return nullptr;
}

size_t Table::getColumnIndex(const std::string& name) const {
    auto it = column_map_.find(name);
    if (it != column_map_.end()) {
        return it->second;
    }
    return static_cast<size_t>(-1);
}

void Table::setPrimaryKey(const std::vector<std::string>& columns) {
    primary_key_.clear();
    
    for (const auto& col_name : columns) {
        size_t idx = getColumnIndex(col_name);
        if (idx != static_cast<size_t>(-1)) {
            primary_key_.push_back(idx);
            columns_[idx].getAttributes().primary_key = true;
        }
    }
}

std::vector<size_t> Table::getPrimaryKeyIndices() const {
    return primary_key_;
}

bool Table::isPrimaryKeyColumn(const std::string& name) const {
    size_t idx = getColumnIndex(name);
    if (idx != static_cast<size_t>(-1)) {
        return std::find(primary_key_.begin(), primary_key_.end(), idx) != primary_key_.end();
    }
    return false;
}

void Table::addIndex(const std::string& name, const std::vector<std::string>& columns) {
    Index idx;
    idx.name = name;
    
    for (const auto& col_name : columns) {
        size_t col_idx = getColumnIndex(col_name);
        if (col_idx != static_cast<size_t>(-1)) {
            idx.column_indices.push_back(col_idx);
        }
    }
    
    if (!idx.column_indices.empty()) {
        indexes_[name] = idx;
    }
}

bool Table::hasIndex(const std::string& name) const {
    return indexes_.find(name) != indexes_.end();
}

void Table::addForeignKey(const std::string& name, const std::vector<std::string>& columns,
                          const std::string& ref_table, const std::vector<std::string>& ref_columns) {
    ForeignKey fk;
    fk.name = name;
    fk.ref_table = ref_table;
    fk.ref_columns = ref_columns;
    
    for (const auto& col_name : columns) {
        size_t col_idx = getColumnIndex(col_name);
        if (col_idx != static_cast<size_t>(-1)) {
            fk.column_indices.push_back(col_idx);
        }
    }
    
    if (!fk.column_indices.empty()) {
        foreign_keys_.push_back(fk);
    }
}

void Table::setOption(const std::string& key, const std::string& value) {
    options_[key] = value;
}

std::string Table::getOption(const std::string& key) const {
    auto it = options_.find(key);
    if (it != options_.end()) {
        return it->second;
    }
    return "";
}

size_t Table::getRecordSize() const {
    size_t size = 0;
    for (const auto& col : columns_) {
        size += col.getSizeInBytes();
    }
    return size;
}

bool Table::validateRecord(const std::vector<Value>& record) const {
    if (record.size() != columns_.size()) {
        return false;
    }
    
    for (size_t i = 0; i < columns_.size(); i++) {
        if (!columns_[i].validateValue(record[i])) {
            return false;
        }
    }
    
    // Check primary key uniqueness would be handled by index
    // Check foreign key would be handled by constraint
    
    return true;
}

std::string Table::toString() const {
    using json = nlohmann::json;
    
    json j;
    j["id"] = id_;
    j["name"] = name_;
    
    j["columns"] = json::array();
    for (const auto& col : columns_) {
        j["columns"].push_back(col.toString());
    }
    
    j["primary_key"] = primary_key_;
    
    j["indexes"] = json::object();
    for (const auto& [name, idx] : indexes_) {
        j["indexes"][name] = idx.column_indices;
    }
    
    j["foreign_keys"] = json::array();
    for (const auto& fk : foreign_keys_) {
        json fk_json;
        fk_json["name"] = fk.name;
        fk_json["columns"] = fk.column_indices;
        fk_json["ref_table"] = fk.ref_table;
        fk_json["ref_columns"] = fk.ref_columns;
        j["foreign_keys"].push_back(fk_json);
    }
    
    j["options"] = options_;
    
    return j.dump();
}

Table Table::fromString(const std::string& str) {
    using json = nlohmann::json;
    
    json j = json::parse(str);
    
    Table table;
    table.id_ = j["id"];
    table.name_ = j["name"];
    
    for (const auto& col_str : j["columns"]) {
        table.columns_.push_back(Column::fromString(col_str));
    }
    
    // Rebuild column map
    for (size_t i = 0; i < table.columns_.size(); i++) {
        table.column_map_[table.columns_[i].getName()] = i;
    }
    
    if (j.contains("primary_key")) {
        table.primary_key_ = j["primary_key"].get<std::vector<size_t>>();
    }
    
    if (j.contains("indexes")) {
        for (const auto& [name, indices] : j["indexes"].items()) {
            Index idx;
            idx.name = name;
            idx.column_indices = indices.get<std::vector<size_t>>();
            table.indexes_[name] = idx;
        }
    }
    
    if (j.contains("foreign_keys")) {
        for (const auto& fk_json : j["foreign_keys"]) {
            ForeignKey fk;
            fk.name = fk_json["name"];
            fk.column_indices = fk_json["columns"].get<std::vector<size_t>>();
            fk.ref_table = fk_json["ref_table"];
            fk.ref_columns = fk_json["ref_columns"].get<std::vector<std::string>>();
            table.foreign_keys_.push_back(fk);
        }
    }
    
    if (j.contains("options")) {
        table.options_ = j["options"].get<std::unordered_map<std::string, std::string>>();
    }
    
    return table;
}

} // namespace orangesql