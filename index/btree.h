#ifndef ORANGESQL_BTREE_H
#define ORANGESQL_BTREE_H

#include "btree_node.h"
#include "../storage/buffer_pool.h"
#include <functional>
#include <stack>

namespace orangesql {

// Configurações do índice
struct IndexConfig {
    bool unique;           // Chaves únicas
    bool allow_duplicates; // Permitir duplicatas
    size_t fill_factor;    // Fator de preenchimento (%)
    bool auto_vacuum;      // Compactar automaticamente
    
    IndexConfig() : unique(false), allow_duplicates(true),
                   fill_factor(70), auto_vacuum(true) {}
};

// Estatísticas do índice
struct IndexStats {
    size_t total_keys;
    size_t total_nodes;
    size_t leaf_nodes;
    size_t internal_nodes;
    size_t height;
    size_t cache_hits;
    size_t cache_misses;
    double avg_fill_factor;
    
    IndexStats() : total_keys(0), total_nodes(0), leaf_nodes(0),
                  internal_nodes(0), height(0), cache_hits(0),
                  cache_misses(0), avg_fill_factor(0) {}
};

// B-Tree Index
template<typename K, typename V>
class BTree {
public:
    // Iterator para range scans
    class Iterator {
    public:
        Iterator(BTree* tree, bool end = false);
        ~Iterator() = default;
        
        bool operator!=(const Iterator& other) const;
        Iterator& operator++();
        Iterator operator++(int);
        std::pair<K, V> operator*() const;
        
    private:
        BTree* tree_;
        PageId current_page_;
        uint16_t current_slot_;
        bool is_end_;
        std::unique_ptr<BTreeNode<K, V>> current_node_;
        
        void loadCurrentNode();
        void advance();
    };
    
    BTree(IndexId id, BufferPool* buffer_pool, const IndexConfig& config = IndexConfig());
    ~BTree();
    
    // Operações principais
    Status insert(const K& key, const V& value);
    Status remove(const K& key);
    Status find(const K& key, V& value) const;
    std::vector<V> findMany(const K& key) const;  // Para duplicatas
    
    // Range queries
    std::vector<V> rangeQuery(const K& start, const K& end);
    std::vector<std::pair<K, V>> rangeQueryWithKeys(const K& start, const K& end);
    
    // Bulk operations
    Status bulkInsert(const std::vector<std::pair<K, V>>& entries);
    size_t bulkDelete(const std::vector<K>& keys);
    
    // Scans
    Iterator begin() { return Iterator(this, false); }
    Iterator end() { return Iterator(this, true); }
    Iterator findFirst(const K& key);
    Iterator findLast(const K& key);
    
    // Estatísticas e debug
    IndexStats getStats() const;
    void validate() const;
    void dump() const;
    
    // Manutenção
    Status vacuum();                    // Compactar
    Status rebuild();                    // Reconstruir
    
    // Configuração
    void setConfig(const IndexConfig& config) { config_ = config; }
    IndexConfig getConfig() const { return config_; }
    
private:
    IndexId id_;
    BufferPool* buffer_pool_;
    IndexConfig config_;
    PageId root_page_id_;
    mutable IndexStats stats_;
    
    // Gerenciamento de nós
    BTreeNode<K, V>* getNode(PageId page_id) const;
    void releaseNode(BTreeNode<K, V>* node) const;
    PageId createNode(BTreeNodeType type);
    void deleteNode(PageId page_id);
    
    // Operações internas
    Status insertInternal(const K& key, const V& value, PageId node_page);
    Status removeInternal(const K& key, PageId node_page);
    BTreeNode<K, V>* findLeaf(const K& key) const;
    
    // Split e merge
    void splitChild(BTreeNode<K, V>* parent, int child_index);
    bool mergeNodes(BTreeNode<K, V>* parent, int index);
    void redistribute(BTreeNode<K, V>* parent, int index, bool from_left);
    
    // Balanceamento
    void handleUnderflow(BTreeNode<K, V>* node, PageId parent_page, int index_in_parent);
    
    // Traversal
    void traverse(std::function<void(const K&, const V&)> func) const;
    
    // Validação
    bool validateNode(const BTreeNode<K, V>* node, const K& min_key, const K& max_key) const;
    
    friend class Iterator;
};

// Tipos comuns de índices
using IntIndex = BTree<int32_t, RecordId>;
using LongIndex = BTree<int64_t, RecordId>;
using StringIndex = BTree<std::string, RecordId>;

} // namespace orangesql

#endif // ORANGESQL_BTREE_H