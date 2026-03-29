#include "btree_node.h"
#include <cstring>
#include <algorithm>
#include <iostream>

namespace orangesql {

template<typename K, typename V>
BTreeNode<K, V>::BTreeNode(PageId page_id, BTreeNodeType type)
    : page_id_(page_id), dirty_(false) {
    
    memset(&header_, 0, sizeof(header_));
    header_.type = type;
    header_.parent_id = INVALID_PAGE_ID;
    header_.next_id = INVALID_PAGE_ID;
    header_.prev_id = INVALID_PAGE_ID;
    
    memset(keys_, 0, sizeof(keys_));
    memset(values_, 0, sizeof(values_));
    
    for (size_t i = 0; i < ORDER; i++) {
        children_[i] = INVALID_PAGE_ID;
    }
}

template<typename K, typename V>
const K& BTreeNode<K, V>::getKey(uint16_t index) const {
    if (index >= header_.key_count) {
        throw std::out_of_range("Key index out of range");
    }
    return keys_[index];
}

template<typename K, typename V>
void BTreeNode<K, V>::setKey(uint16_t index, const K& key) {
    if (index >= MAX_KEYS) return;
    keys_[index] = key;
    if (index >= header_.key_count) {
        header_.key_count = index + 1;
    }
    dirty_ = true;
}

template<typename K, typename V>
const V& BTreeNode<K, V>::getValue(uint16_t index) const {
    if (!isLeaf() || index >= header_.key_count) {
        throw std::out_of_range("Value index out of range");
    }
    return values_[index];
}

template<typename K, typename V>
void BTreeNode<K, V>::setValue(uint16_t index, const V& value) {
    if (!isLeaf() || index >= MAX_KEYS) return;
    values_[index] = value;
    dirty_ = true;
}

template<typename K, typename V>
PageId BTreeNode<K, V>::getChild(uint16_t index) const {
    if (isLeaf() || index >= header_.child_count) {
        return INVALID_PAGE_ID;
    }
    return children_[index];
}

template<typename K, typename V>
void BTreeNode<K, V>::setChild(uint16_t index, PageId child) {
    if (isLeaf() || index >= ORDER) return;
    children_[index] = child;
    if (index >= header_.child_count) {
        header_.child_count = index + 1;
    }
    dirty_ = true;
}

template<typename K, typename V>
bool BTreeNode<K, V>::insert(const K& key, const V& value, PageId child) {
    int pos = findKeyIndex(key);
    
    if (pos >= 0 && pos < header_.key_count && keys_[pos] == key) {
        // Chave duplicada
        if (isLeaf()) {
            values_[pos] = value;
            dirty_ = true;
            return true;
        }
        return false;
    }
    
    // Ajustar posição para inserção
    if (pos < 0) {
        pos = 0;
    } else if (key > keys_[pos]) {
        pos++;
    }
    
    return insertAt(pos, key, value, child);
}

template<typename K, typename V>
bool BTreeNode<K, V>::insertAt(uint16_t index, const K& key, const V& value, PageId child) {
    if (isFull()) {
        return false;
    }
    
    // Abrir espaço para nova chave
    shiftRight(index);
    
    // Inserir chave
    keys_[index] = key;
    
    if (isLeaf()) {
        values_[index] = value;
    } else {
        children_[index] = child;
        header_.child_count++;
    }
    
    header_.key_count++;
    dirty_ = true;
    
    return true;
}

template<typename K, typename V>
bool BTreeNode<K, V>::remove(uint16_t index) {
    if (index >= header_.key_count) {
        return false;
    }
    
    // Marcar como deletado (compactação lazy)
    keys_[index] = K();
    if (isLeaf()) {
        values_[index] = V();
    }
    
    shiftLeft(index);
    
    header_.key_count--;
    if (!isLeaf()) {
        header_.child_count--;
    }
    
    dirty_ = true;
    return true;
}

template<typename K, typename V>
bool BTreeNode<K, V>::remove(const K& key) {
    int pos = findKeyIndex(key);
    if (pos < 0 || pos >= header_.key_count || keys_[pos] != key) {
        return false;
    }
    
    return remove(pos);
}

template<typename K, typename V>
int BTreeNode<K, V>::findKeyIndex(const K& key) const {
    // Busca binária
    int left = 0;
    int right = header_.key_count - 1;
    
    while (left <= right) {
        int mid = (left + right) / 2;
        
        if (keys_[mid] == key) {
            return mid;
        } else if (keys_[mid] < key) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    return right; // Retorna índice onde a chave deveria estar
}

template<typename K, typename V>
int BTreeNode<K, V>::findChildIndex(const K& key) const {
    if (isLeaf()) return -1;
    
    int pos = findKeyIndex(key);
    if (pos < 0) return 0;
    
    if (pos < header_.key_count && keys_[pos] <= key) {
        return pos + 1;
    }
    
    return pos;
}

template<typename K, typename V>
std::unique_ptr<BTreeNode<K, V>> BTreeNode<K, V>::split() {
    auto new_node = std::make_unique<BTreeNode<K, V>>(INVALID_PAGE_ID, header_.type);
    new_node->setLevel(header_.level);
    
    int mid = header_.key_count / 2;
    
    // Copiar metade superior das chaves
    for (int i = mid + 1; i < header_.key_count; i++) {
        new_node->keys_[i - mid - 1] = keys_[i];
        if (isLeaf()) {
            new_node->values_[i - mid - 1] = values_[i];
        }
    }
    
    // Copiar filhos para nós internos
    if (!isLeaf()) {
        for (int i = mid + 1; i <= header_.child_count; i++) {
            new_node->children_[i - mid - 1] = children_[i];
        }
        new_node->header_.child_count = header_.child_count - mid - 1;
    }
    
    // Atualizar contagens
    new_node->header_.key_count = header_.key_count - mid - 1;
    header_.key_count = mid;
    
    if (!isLeaf()) {
        header_.child_count = mid + 1;
    }
    
    // Ajustar links da lista ligada de folhas
    if (isLeaf()) {
        new_node->setNextId(header_.next_id);
        new_node->setPrevId(page_id_);
        header_.next_id = new_node->getPageId();
    }
    
    dirty_ = true;
    return new_node;
}

template<typename K, typename V>
bool BTreeNode<K, V>::merge(std::unique_ptr<BTreeNode<K, V>>& right) {
    if (header_.type != right->header_.type || header_.level != right->header_.level) {
        return false;
    }
    
    // Copiar chaves do nó direito
    for (int i = 0; i < right->header_.key_count; i++) {
        keys_[header_.key_count + i] = right->keys_[i];
        if (isLeaf()) {
            values_[header_.key_count + i] = right->values_[i];
        }
    }
    
    // Copiar filhos para nós internos
    if (!isLeaf()) {
        for (int i = 0; i <= right->header_.child_count; i++) {
            children_[header_.child_count + i] = right->children_[i];
        }
        header_.child_count += right->header_.child_count;
    }
    
    header_.key_count += right->header_.key_count;
    
    // Ajustar links da lista ligada
    if (isLeaf()) {
        header_.next_id = right->header_.next_id;
    }
    
    dirty_ = true;
    return true;
}

template<typename K, typename V>
bool BTreeNode<K, V>::redistribute(std::unique_ptr<BTreeNode<K, V>>& neighbor, bool from_left) {
    if (from_left) {
        // Pegar chave do vizinho esquerdo
        shiftRight(0);
        keys_[0] = neighbor->keys_[neighbor->header_.key_count - 1];
        
        if (isLeaf()) {
            values_[0] = neighbor->values_[neighbor->header_.key_count - 1];
        } else {
            children_[0] = neighbor->children_[neighbor->header_.child_count - 1];
        }
        
        neighbor->remove(neighbor->header_.key_count - 1);
    } else {
        // Pegar chave do vizinho direito
        keys_[header_.key_count] = neighbor->keys_[0];
        
        if (isLeaf()) {
            values_[header_.key_count] = neighbor->values_[0];
        } else {
            children_[header_.child_count] = neighbor->children_[0];
        }
        
        neighbor->remove(0);
        header_.key_count++;
        
        if (!isLeaf()) {
            header_.child_count++;
        }
    }
    
    dirty_ = true;
    return true;
}

template<typename K, typename V>
void BTreeNode<K, V>::compact() {
    int write_pos = 0;
    
    for (int read_pos = 0; read_pos < header_.key_count; read_pos++) {
        if (!keys_[read_pos].is_deleted) { // Assumindo que K tem flag deleted
            if (write_pos != read_pos) {
                keys_[write_pos] = keys_[read_pos];
                if (isLeaf()) {
                    values_[write_pos] = values_[read_pos];
                }
            }
            write_pos++;
        }
    }
    
    header_.key_count = write_pos;
    dirty_ = true;
}

template<typename K, typename V>
void BTreeNode<K, V>::shiftLeft(uint16_t start) {
    for (int i = start; i < header_.key_count - 1; i++) {
        keys_[i] = keys_[i + 1];
        if (isLeaf()) {
            values_[i] = values_[i + 1];
        }
    }
    
    if (!isLeaf()) {
        for (int i = start; i < header_.child_count - 1; i++) {
            children_[i] = children_[i + 1];
        }
    }
}

template<typename K, typename V>
void BTreeNode<K, V>::shiftRight(uint16_t start) {
    for (int i = header_.key_count; i > start; i--) {
        keys_[i] = keys_[i - 1];
        if (isLeaf()) {
            values_[i] = values_[i - 1];
        }
    }
    
    if (!isLeaf()) {
        for (int i = header_.child_count; i > start; i--) {
            children_[i] = children_[i - 1];
        }
    }
}

template<typename K, typename V>
void BTreeNode<K, V>::serialize(char* buffer) const {
    size_t offset = 0;
    
    // Header
    memcpy(buffer + offset, &header_, sizeof(header_));
    offset += sizeof(header_);
    
    // Keys
    memcpy(buffer + offset, keys_, sizeof(K) * MAX_KEYS);
    offset += sizeof(K) * MAX_KEYS;
    
    // Values (se leaf)
    if (isLeaf()) {
        memcpy(buffer + offset, values_, sizeof(V) * MAX_KEYS);
        offset += sizeof(V) * MAX_KEYS;
    }
    
    // Children (se internal)
    if (!isLeaf()) {
        memcpy(buffer + offset, children_, sizeof(PageId) * ORDER);
        offset += sizeof(PageId) * ORDER;
    }
}

template<typename K, typename V>
void BTreeNode<K, V>::deserialize(const char* buffer) {
    size_t offset = 0;
    
    // Header
    memcpy(&header_, buffer + offset, sizeof(header_));
    offset += sizeof(header_);
    
    // Keys
    memcpy(keys_, buffer + offset, sizeof(K) * MAX_KEYS);
    offset += sizeof(K) * MAX_KEYS;
    
    // Values (se leaf)
    if (isLeaf()) {
        memcpy(values_, buffer + offset, sizeof(V) * MAX_KEYS);
        offset += sizeof(V) * MAX_KEYS;
    }
    
    // Children (se internal)
    if (!isLeaf()) {
        memcpy(children_, buffer + offset, sizeof(PageId) * ORDER);
    }
    
    dirty_ = false;
}

template<typename K, typename V>
void BTreeNode<K, V>::dump(int level) const {
    std::string indent(level * 2, ' ');
    
    std::cout << indent << "Node " << page_id_ 
              << " (" << (isLeaf() ? "Leaf" : "Internal") 
              << ", keys=" << header_.key_count 
              << ", level=" << header_.level << ")\n";
    
    std::cout << indent << "Keys: [";
    for (int i = 0; i < header_.key_count; i++) {
        if (i > 0) std::cout << ", ";
        std::cout << keys_[i];
    }
    std::cout << "]\n";
    
    if (!isLeaf()) {
        std::cout << indent << "Children: [";
        for (int i = 0; i <= header_.key_count; i++) {
            if (i > 0) std::cout << ", ";
            std::cout << children_[i];
        }
        std::cout << "]\n";
    }
}

// Instanciação explícita para tipos comuns
template class BTreeNode<int32_t, RecordId>;
template class BTreeNode<int64_t, RecordId>;
template class BTreeNode<std::string, RecordId>;

} // namespace orangesql