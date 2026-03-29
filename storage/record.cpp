#include "../include/types.h"
#include <cstring>

namespace orangesql {

// Funções auxiliares para serialização de registros
size_t serializeRecord(const std::vector<Value>& record, char* buffer) {
    char* start = buffer;
    
    // Número de campos
    uint16_t field_count = record.size();
    memcpy(buffer, &field_count, sizeof(uint16_t));
    buffer += sizeof(uint16_t);
    
    // Offset table
    uint16_t offsets[field_count];
    size_t current_offset = sizeof(uint16_t) * (field_count + 1);
    
    for (size_t i = 0; i < field_count; i++) {
        offsets[i] = current_offset;
        
        switch (record[i].type) {
            case DataType::INTEGER:
                current_offset += sizeof(int32_t);
                break;
            case DataType::BIGINT:
                current_offset += sizeof(int64_t);
                break;
            case DataType::BOOLEAN:
                current_offset += sizeof(bool);
                break;
            case DataType::VARCHAR:
                current_offset += sizeof(uint32_t) + record[i].str_val.length();
                break;
            default:
                break;
        }
    }
    
    // Escrever offsets
    memcpy(buffer, offsets, sizeof(uint16_t) * field_count);
    buffer += sizeof(uint16_t) * field_count;
    
    // Escrever dados
    for (size_t i = 0; i < field_count; i++) {
        switch (record[i].type) {
            case DataType::INTEGER:
                memcpy(buffer, &record[i].data.int_val, sizeof(int32_t));
                buffer += sizeof(int32_t);
                break;
                
            case DataType::BIGINT:
                memcpy(buffer, &record[i].data.bigint_val, sizeof(int64_t));
                buffer += sizeof(int64_t);
                break;
                
            case DataType::BOOLEAN:
                memcpy(buffer, &record[i].data.bool_val, sizeof(bool));
                buffer += sizeof(bool);
                break;
                
            case DataType::VARCHAR: {
                uint32_t len = record[i].str_val.length();
                memcpy(buffer, &len, sizeof(uint32_t));
                buffer += sizeof(uint32_t);
                memcpy(buffer, record[i].str_val.c_str(), len);
                buffer += len;
                break;
            }
            
            default:
                break;
        }
    }
    
    return buffer - start;
}

size_t deserializeRecord(const char* buffer, std::vector<Value>& record) {
    const char* start = buffer;
    
    // Ler número de campos
    uint16_t field_count;
    memcpy(&field_count, buffer, sizeof(uint16_t));
    buffer += sizeof(uint16_t);
    
    // Ler offsets
    uint16_t offsets[field_count];
    memcpy(offsets, buffer, sizeof(uint16_t) * field_count);
    buffer += sizeof(uint16_t) * field_count;
    
    record.resize(field_count);
    
    // Ler dados
    for (size_t i = 0; i < field_count; i++) {
        const char* field_start = start + offsets[i];
        
        // Determinar tipo pelo tamanho (simplificado)
        size_t next_offset = (i < field_count - 1) ? offsets[i + 1] : (buffer - start);
        size_t field_size = next_offset - offsets[i];
        
        if (field_size == sizeof(int32_t)) {
            record[i].type = DataType::INTEGER;
            memcpy(&record[i].data.int_val, field_start, sizeof(int32_t));
        } else if (field_size == sizeof(int64_t)) {
            record[i].type = DataType::BIGINT;
            memcpy(&record[i].data.bigint_val, field_start, sizeof(int64_t));
        } else if (field_size == sizeof(bool)) {
            record[i].type = DataType::BOOLEAN;
            memcpy(&record[i].data.bool_val, field_start, sizeof(bool));
        } else {
            record[i].type = DataType::VARCHAR;
            uint32_t len;
            memcpy(&len, field_start, sizeof(uint32_t));
            record[i].str_val.assign(field_start + sizeof(uint32_t), len);
        }
    }
    
    return buffer - start;
}

size_t getRecordSize(const std::vector<Value>& record) {
    size_t size = sizeof(uint16_t); // field count
    
    for (const auto& val : record) {
        size += sizeof(uint16_t); // offset
        
        switch (val.type) {
            case DataType::INTEGER:
                size += sizeof(int32_t);
                break;
            case DataType::BIGINT:
                size += sizeof(int64_t);
                break;
            case DataType::BOOLEAN:
                size += sizeof(bool);
                break;
            case DataType::VARCHAR:
                size += sizeof(uint32_t) + val.str_val.length();
                break;
            default:
                break;
        }
    }
    
    return size;
}

bool compareValues(const Value& a, const Value& b) {
    if (a.type != b.type) {
        return false;
    }
    
    switch (a.type) {
        case DataType::INTEGER:
            return a.data.int_val == b.data.int_val;
        case DataType::BIGINT:
            return a.data.bigint_val == b.data.bigint_val;
        case DataType::BOOLEAN:
            return a.data.bool_val == b.data.bool_val;
        case DataType::VARCHAR:
            return a.str_val == b.str_val;
        default:
            return false;
    }
}

int compareValuesOrdered(const Value& a, const Value& b) {
    if (a.type != b.type) {
        return static_cast<int>(a.type) - static_cast<int>(b.type);
    }
    
    switch (a.type) {
        case DataType::INTEGER:
            if (a.data.int_val < b.data.int_val) return -1;
            if (a.data.int_val > b.data.int_val) return 1;
            return 0;
            
        case DataType::BIGINT:
            if (a.data.bigint_val < b.data.bigint_val) return -1;
            if (a.data.bigint_val > b.data.bigint_val) return 1;
            return 0;
            
        case DataType::BOOLEAN:
            if (!a.data.bool_val && b.data.bool_val) return -1;
            if (a.data.bool_val && !b.data.bool_val) return 1;
            return 0;
            
        case DataType::VARCHAR:
            return a.str_val.compare(b.str_val);
            
        default:
            return 0;
    }
}

Value aggregateValues(const std::vector<Value>& values, 
                      AggregateFunction func) {
    if (values.empty()) return Value();
    
    Value result = values[0];
    
    switch (func) {
        case AggregateFunction::COUNT:
            return Value(static_cast<int32_t>(values.size()));
            
        case AggregateFunction::SUM: {
            if (values[0].type == DataType::INTEGER) {
                int64_t sum = 0;
                for (const auto& v : values) {
                    sum += v.data.int_val;
                }
                return Value(static_cast<int32_t>(sum));
            }
            break;
        }
        
        case AggregateFunction::AVG: {
            if (values[0].type == DataType::INTEGER) {
                int64_t sum = 0;
                for (const auto& v : values) {
                    sum += v.data.int_val;
                }
                return Value(static_cast<int32_t>(sum / values.size()));
            }
            break;
        }
        
        case AggregateFunction::MIN: {
            result = values[0];
            for (size_t i = 1; i < values.size(); i++) {
                if (compareValuesOrdered(values[i], result) < 0) {
                    result = values[i];
                }
            }
            break;
        }
        
        case AggregateFunction::MAX: {
            result = values[0];
            for (size_t i = 1; i < values.size(); i++) {
                if (compareValuesOrdered(values[i], result) > 0) {
                    result = values[i];
                }
            }
            break;
        }
    }
    
    return result;
}

std::string valueToString(const Value& val) {
    switch (val.type) {
        case DataType::INTEGER:
            return std::to_string(val.data.int_val);
        case DataType::BIGINT:
            return std::to_string(val.data.bigint_val);
        case DataType::BOOLEAN:
            return val.data.bool_val ? "true" : "false";
        case DataType::VARCHAR:
            return val.str_val;
        default:
            return "NULL";
    }
}

Value stringToValue(const std::string& str, DataType type) {
    Value val;
    val.type = type;
    
    switch (type) {
        case DataType::INTEGER:
            val.data.int_val = std::stoi(str);
            break;
        case DataType::BIGINT:
            val.data.bigint_val = std::stoll(str);
            break;
        case DataType::BOOLEAN:
            val.data.bool_val = (str == "true" || str == "1");
            break;
        case DataType::VARCHAR:
            val.str_val = str;
            break;
        default:
            break;
    }
    
    return val;
}

} // namespace orangesql