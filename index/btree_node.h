#ifndef ORANGESQL_BTREE_NODE_H
#define ORANGESQL_BTREE_NODE_H

#include "../include/types.h"
#include "../storage/page.h"
#include <vector>
#include <memory>

namespace orangesql {

// Tipos de nó da B-Tree
enum class BTreeNodeType {
    LEAF,
    INTERNAL,
    ROOT
};

// Cabeçalho do nó da B-Tree (estende PageHeader)
struct BTreeNodeHeader {
    BTreeNodeType type;
    uint16_t key_count;
    uint16_t child_count;
    PageId parent_id;
    PageId next_id;      // Próxima folha (para scans)
    PageId prev_id;      // Folha anterior
    uint32_t level;      // Nível na árvore (0 = folha)
    
    BTreeNodeHeader() : type(BTreeNodeType::LEAF), key_count(0), child_count(0),
                        parent_id(INVALID_PAGE_ID), next_id(INVALID_PAGE_ID),
                        prev_id(INVALID_PAGE_ID), level(0) {}
};

// Slot para chave/valor
template<typename K, typename V>
struct BTreeSlot {
    K key;
    V value;
    PageId child_page;   // Para nós internos
    bool is_deleted;
    
    BTreeSlot() : child_page(INVALID_PAGE_ID), is_deleted(false) {}
};

// Nó da B-Tree
template<typename K, typename V>
class BTreeNode {
public:
    static constexpr size_t ORDER = 128;  // Ordem da árvore
    static constexpr size_t MAX_KEYS = ORDER - 1;
    static constexpr size_t MIN_KEYS = ORDER / 2 - 1;
    
    BTreeNode(PageId page_id, BTreeNodeType type = BTreeNodeType::LEAF);
    ~BTreeNode() = default;
    
    // Getters/Setters
    PageId getPageId() const { return page_id_; }
    BTreeNodeType getType() const { return header_.type; }
    void setType(BTreeNodeType type) { header_.type = type; dirty_ = true; }
    
    uint16_t getKeyCount() const { return header_.key_count; }
    uint16_t getChildCount() const { return header_.child_count; }
    
    PageId getParentId() const { return header_.parent_id; }
    void setParentId(PageId pid) { header_.parent_id = pid; dirty_ = true; }
    
    PageId getNextId() const { return header_.next_id; }
    void setNextId(PageId pid) { header_.next_id = pid; dirty_ = true; }
    
    PageId getPrevId() const { return header_.prev_id; }
    void setPrevId(PageId pid) { header_.prev_id = pid; dirty_ = true; }
    
    uint32_t getLevel() const { return header_.level; }
    void setLevel(uint32_t level) { header_.level = level; dirty_ = true; }
    
    bool isLeaf() const { return header_.type == BTreeNodeType::LEAF; }
    bool isRoot() const { return header_.type == BTreeNodeType::ROOT; }
    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void clearDirty() { dirty_ = false; }
    
    // Operações com chaves
    const K& getKey(uint16_t index) const;
    void setKey(uint16_t index, const K& key);
    
    // Operações com valores (folhas)
    const V& getValue(uint16_t index) const;
    void setValue(uint16_t index, const V& value);
    
    // Operações com filhos (internos)
    PageId getChild(uint16_t index) const;
    void setChild(uint16_t index, PageId child);
    
    // Inserção
    bool insert(const K& key, const V& value, PageId child = INVALID_PAGE_ID);
    bool insertAt(uint16_t index, const K& key, const V& value, PageId child = INVALID_PAGE_ID);
    
    // Remoção
    bool remove(uint16_t index);
    bool remove(const K& key);
    
    // Busca
    int findKeyIndex(const K& key) const;
    int findChildIndex(const K& key) const;
    
    // Split e Merge
    bool isFull() const { return header_.key_count >= MAX_KEYS; }
    bool isUnderflow() const { return header_.key_count < MIN_KEYS; }
    
    K getSplitKey() const { return keys_[header_.key_count / 2]; }
    
    std::unique_ptr<BTreeNode<K, V>> split();
    bool merge(std::unique_ptr<BTreeNode<K, V>>& right);
    bool redistribute(std::unique_ptr<BTreeNode<K, V>>& neighbor, bool from_left);
    
    // Serialização
    void serialize(char* buffer) const;
    void deserialize(const char* buffer);
    
    // Debug
    void dump(int level = 0) const;
    
private:
    PageId page_id_;
    BTreeNodeHeader header_;
    
    // Arrays (alocados no mesmo bloco de memória)
    K keys_[MAX_KEYS];
    V values_[MAX_KEYS];        // Usado apenas em folhas
    PageId children_[ORDER];     // Usado apenas em nós internos
    
    bool dirty_;
    
    // Compactação
    void compact();
    void shiftLeft(uint16_t start);
    void shiftRight(uint16_t start);
};

} // namespace orangesql

#endif // ORANGESQL_BTREE_NODE_H