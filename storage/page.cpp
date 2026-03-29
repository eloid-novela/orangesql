#include "page.h"
#include <cstring>
#include <algorithm>
#include <iostream>

namespace orangesql {

Page::Page(PageId id, TableId table) {
    memset(this, 0, sizeof(Page));
    header_.page_id = id;
    header_.table_id = table;
    header_.type = PageType::DATA_PAGE;
    header_.free_space_offset = sizeof(PageHeader) + (MAX_SLOTS * SLOT_SIZE);
    header_.free_space_end = PAGE_SIZE;
    header_.next_page_id = INVALID_PAGE_ID;
    header_.prev_page_id = INVALID_PAGE_ID;
    header_.parent_page_id = INVALID_PAGE_ID;
    header_.checksum = calculateChecksum();
}

bool Page::insertRecord(const std::vector<Value>& record, RecordId& rid, TransactionId tx_id) {
    // Calcular tamanho necessário
    size_t record_size = 0;
    for (const auto& val : record) {
        record_size += sizeof(uint16_t); // tamanho do campo
        record_size += getValueSize(val);
    }
    
    // Verificar se há espaço
    if (!hasSpaceFor(record_size)) {
        return false;
    }
    
    // Obter slot livre
    PageSlot* slots = getSlotArray();
    uint16_t slot_pos = header_.record_count;
    
    // Calcular offset para o novo registro (cresce de trás pra frente)
    uint16_t record_offset = header_.free_space_end - record_size;
    
    // Escrever o registro
    char* record_ptr = getRecordData(record_offset);
    for (const auto& val : record) {
        record_ptr += serializeValue(val, record_ptr);
    }
    
    // Atualizar slot
    slots[slot_pos].offset = record_offset;
    slots[slot_pos].length = record_size;
    slots[slot_pos].flags = 0;
    slots[slot_pos].next = 0;
    
    // Atualizar header
    header_.record_count++;
    header_.free_space_end = record_offset;
    header_.free_space_offset += SLOT_SIZE;
    header_.is_dirty = true;
    header_.lsn = 0; // Será atualizado pelo log manager
    
    // Criar RecordId
    rid = (static_cast<RecordId>(header_.page_id) << 32) | slot_pos;
    
    return true;
}

bool Page::getRecord(RecordId rid, std::vector<Value>& record) const {
    uint16_t slot_pos = rid & 0xFFFF;
    if (slot_pos >= header_.record_count) {
        return false;
    }
    
    const PageSlot* slots = getSlotArray();
    const PageSlot& slot = slots[slot_pos];
    
    // Verificar se slot está vazio
    if (slot.length == 0 || (slot.flags & 0x01)) { // Deleted flag
        return false;
    }
    
    const char* record_ptr = getRecordData(slot.offset);
    const char* record_end = record_ptr + slot.length;
    
    record.clear();
    while (record_ptr < record_end) {
        Value val;
        record_ptr += deserializeValue(record_ptr, val);
        record.push_back(val);
    }
    
    return true;
}

bool Page::updateRecord(RecordId rid, const std::vector<Value>& record, TransactionId tx_id) {
    // Para simplificar, implementar como delete + insert
    // Na prática, seria otimizado para update in-place se possível
    
    std::vector<Value> old_record;
    if (!getRecord(rid, old_record)) {
        return false;
    }
    
    if (!deleteRecord(rid, tx_id)) {
        return false;
    }
    
    RecordId new_rid;
    return insertRecord(record, new_rid, tx_id);
}

bool Page::deleteRecord(RecordId rid, TransactionId tx_id) {
    uint16_t slot_pos = rid & 0xFFFF;
    if (slot_pos >= header_.record_count) {
        return false;
    }
    
    PageSlot* slots = getSlotArray();
    
    // Marcar slot como deletado
    slots[slot_pos].flags |= 0x01; // Deleted flag
    
    // Adicionar à lista livre
    slots[slot_pos].next = 0; // Início da lista livre
    
    header_.is_dirty = true;
    
    return true;
}

size_t Page::insertRecords(const std::vector<std::vector<Value>>& records, 
                           std::vector<RecordId>& rids) {
    size_t inserted = 0;
    for (const auto& record : records) {
        RecordId rid;
        if (insertRecord(record, rid)) {
            rids.push_back(rid);
            inserted++;
        } else {
            break;
        }
    }
    return inserted;
}

void Page::getRecords(std::vector<std::vector<Value>>& records) const {
    const PageSlot* slots = getSlotArray();
    records.reserve(header_.record_count);
    
    for (uint16_t i = 0; i < header_.record_count; i++) {
        if (slots[i].length > 0 && !(slots[i].flags & 0x01)) {
            std::vector<Value> record;
            RecordId rid = (static_cast<RecordId>(header_.page_id) << 32) | i;
            getRecord(rid, record);
            records.push_back(std::move(record));
        }
    }
}

size_t Page::deleteRecords(const std::vector<RecordId>& rids) {
    size_t deleted = 0;
    for (auto rid : rids) {
        if (deleteRecord(rid)) {
            deleted++;
        }
    }
    return deleted;
}

bool Page::hasSpaceFor(size_t record_size) const {
    return (header_.free_space_end - header_.free_space_offset) >= 
           (record_size + SLOT_SIZE);
}

size_t Page::getFreeSpace() const {
    return header_.free_space_end - header_.free_space_offset;
}

void Page::compactify() {
    // Algoritmo de compactação: move todos os registros para o início da área de dados
    PageSlot* slots = getSlotArray();
    char temp_buffer[DATA_SIZE];
    size_t temp_offset = 0;
    
    // Copiar registros válidos para buffer temporário
    for (uint16_t i = 0; i < header_.record_count; i++) {
        if (slots[i].length > 0 && !(slots[i].flags & 0x01)) {
            const char* record_data = getRecordData(slots[i].offset);
            memcpy(temp_buffer + temp_offset, record_data, slots[i].length);
            
            // Atualizar offset do slot
            slots[i].offset = sizeof(PageHeader) + (MAX_SLOTS * SLOT_SIZE) + temp_offset;
            temp_offset += slots[i].length;
        }
    }
    
    // Copiar de volta para a página
    memcpy(data_ + (MAX_SLOTS * SLOT_SIZE), temp_buffer, temp_offset);
    
    // Atualizar headers
    header_.free_space_offset = sizeof(PageHeader) + (MAX_SLOTS * SLOT_SIZE) + temp_offset;
    header_.free_space_end = PAGE_SIZE;
    header_.is_dirty = true;
}

void Page::serialize(char* buffer) const {
    memcpy(buffer, &header_, HEADER_SIZE);
    memcpy(buffer + HEADER_SIZE, data_, PAGE_SIZE - HEADER_SIZE);
}

void Page::deserialize(const char* buffer) {
    memcpy(&header_, buffer, HEADER_SIZE);
    memcpy(data_, buffer + HEADER_SIZE, PAGE_SIZE - HEADER_SIZE);
    header_.is_dirty = false;
}

uint32_t Page::calculateChecksum() const {
    uint32_t checksum = 0;
    const uint32_t* words = reinterpret_cast<const uint32_t*>(this);
    size_t word_count = PAGE_SIZE / sizeof(uint32_t);
    
    for (size_t i = 0; i < word_count; i++) {
        checksum ^= words[i];
    }
    
    return checksum;
}

bool Page::verifyChecksum() const {
    uint32_t expected = header_.checksum;
    const_cast<Page*>(this)->header_.checksum = 0;
    uint32_t actual = calculateChecksum();
    const_cast<Page*>(this)->header_.checksum = expected;
    
    return expected == actual;
}

size_t Page::serializeValue(const Value& val, char* buffer) const {
    char* start = buffer;
    
    // Escrever tipo
    uint8_t type = static_cast<uint8_t>(val.type);
    memcpy(buffer, &type, sizeof(uint8_t));
    buffer += sizeof(uint8_t);
    
    // Escrever tamanho (para strings)
    switch (val.type) {
        case DataType::INTEGER:
            memcpy(buffer, &val.data.int_val, sizeof(int32_t));
            buffer += sizeof(int32_t);
            break;
            
        case DataType::BIGINT:
            memcpy(buffer, &val.data.bigint_val, sizeof(int64_t));
            buffer += sizeof(int64_t);
            break;
            
        case DataType::BOOLEAN:
            memcpy(buffer, &val.data.bool_val, sizeof(bool));
            buffer += sizeof(bool);
            break;
            
        case DataType::VARCHAR: {
            uint32_t len = val.str_val.length();
            memcpy(buffer, &len, sizeof(uint32_t));
            buffer += sizeof(uint32_t);
            memcpy(buffer, val.str_val.c_str(), len);
            buffer += len;
            break;
        }
        
        default:
            break;
    }
    
    return buffer - start;
}

size_t Page::deserializeValue(const char* buffer, Value& val) const {
    const char* start = buffer;
    
    // Ler tipo
    uint8_t type;
    memcpy(&type, buffer, sizeof(uint8_t));
    buffer += sizeof(uint8_t);
    val.type = static_cast<DataType>(type);
    
    // Ler valor
    switch (val.type) {
        case DataType::INTEGER:
            memcpy(&val.data.int_val, buffer, sizeof(int32_t));
            buffer += sizeof(int32_t);
            break;
            
        case DataType::BIGINT:
            memcpy(&val.data.bigint_val, buffer, sizeof(int64_t));
            buffer += sizeof(int64_t);
            break;
            
        case DataType::BOOLEAN:
            memcpy(&val.data.bool_val, buffer, sizeof(bool));
            buffer += sizeof(bool);
            break;
            
        case DataType::VARCHAR: {
            uint32_t len;
            memcpy(&len, buffer, sizeof(uint32_t));
            buffer += sizeof(uint32_t);
            val.str_val.assign(buffer, len);
            buffer += len;
            break;
        }
        
        default:
            break;
    }
    
    return buffer - start;
}

size_t Page::getValueSize(const Value& val) const {
    size_t size = sizeof(uint8_t); // type
    
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
    
    return size;
}

void Page::dump() const {
    std::cout << "Page " << header_.page_id << ":\n";
    std::cout << "  Type: " << static_cast<int>(header_.type) << "\n";
    std::cout << "  Records: " << header_.record_count << "\n";
    std::cout << "  Free space: " << getFreeSpace() << " bytes\n";
    std::cout << "  Next page: " << header_.next_page_id << "\n";
    std::cout << "  Prev page: " << header_.prev_page_id << "\n";
    std::cout << "  Dirty: " << header_.is_dirty << "\n";
}

} // namespace orangesql