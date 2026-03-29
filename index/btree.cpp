#include "btree.h"
#include <stack>
#include <queue>
#include <algorithm>
#include <iostream>

namespace orangesql {

// ============================================
// BTree Iterator
// ============================================

template<typename K, typename V>
BTree<K, V>::Iterator::Iterator(BTree* tree, bool end)
    : tree_(tree), current_page_(INVALID_PAGE_ID), current_slot_(0), is_end_(end) {
    
    if (!end) {
        // Encontrar primeira folha (mais à esquerda)
        auto* root = tree_->getNode(tree_->root_page_id_);
        if (root) {
            auto* node = root;
            while (node && !node->isLeaf()) {
                PageId child_id = node->getChild(0);
                tree_->releaseNode(node);
                node = tree_->getNode(child_id);
            }
            
            if (node) {
                current_page_ = node->getPageId();
                current_node_ = std::unique_ptr<BTreeNode<K, V>>(node);
                current_slot_ = 0;
                
                if (current_slot_ >= current_node_->getKeyCount()) {
                    advance();
                }
            } else {
                is_end_ = true;
            }
        }
    }
}

template<typename K, typename V>
bool BTree<K, V>::Iterator::operator!=(const Iterator& other) const {
    return is_end_ != other.is_end_ || 
           current_page_ != other.current_page_ || 
           current_slot_ != other.current_slot_;
}

template<typename K, typename V>
typename BTree<K, V>::Iterator& BTree<K, V>::Iterator::operator++() {
    advance();
    return *this;
}

template<typename K, typename V>
typename BTree<K, V>::Iterator BTree<K, V>::Iterator::operator++(int) {
    Iterator tmp = *this;
    advance();
    return tmp;
}

template<typename K, typename V>
std::pair<K, V> BTree<K, V>::Iterator::operator*() const {
    if (is_end_ || !current_node_) {
        throw std::out_of_range("Iterator out of range");
    }
    
    return {current_node_->getKey(current_slot_),
            current_node_->getValue(current_slot_)};
}

template<typename K, typename V>
void BTree<K, V>::Iterator::loadCurrentNode() {
    if (current_page_ != INVALID_PAGE_ID) {
        current_node_.reset(tree_->getNode(current_page_));
    }
}

template<typename K, typename V>
void BTree<K, V>::Iterator::advance() {
    if (!current_node_) {
        loadCurrentNode();
    }
    
    if (!current_node_) {
        is_end_ = true;
        return;
    }
    
    current_slot_++;
    
    if (current_slot_ >= current_node_->getKeyCount()) {
        // Ir para próxima folha
        current_page_ = current_node_->getNextId();
        current_node_.reset();
        current_slot_ = 0;
        
        if (current_page_ == INVALID_PAGE_ID) {
            is_end_ = true;
        } else {
            loadCurrentNode();
        }
    }
}

// ============================================
// BTree Implementation
// ============================================

template<typename K, typename V>
BTree<K, V>::BTree(IndexId id, BufferPool* buffer_pool, const IndexConfig& config)
    : id_(id), buffer_pool_(buffer_pool), config_(config), root_page_id_(INVALID_PAGE_ID) {
    
    // Tentar carregar root existente
    root_page_id_ = 0; // Assumindo que página 0 é a root
    auto* root = getNode(root_page_id_);
    
    if (!root) {
        // Criar nova root
        root_page_id_ = createNode(BTreeNodeType::LEAF);
        root = getNode(root_page_id_);
        if (root) {
            root->setType(BTreeNodeType::ROOT);
            releaseNode(root);
        }
    }
    
    stats_.height = 1;
}

template<typename K, typename V>
BTree<K, V>::~BTree() {
    // Liberar recursos
}

template<typename K, typename V>
Status BTree<K, V>::insert(const K& key, const V& value) {
    stats_.total_keys++;
    
    auto* root = getNode(root_page_id_);
    if (!root) {
        return Status::ERROR;
    }
    
    // Verificar se root está cheio
    if (root->isFull()) {
        // Criar nova root
        PageId new_root_id = createNode(BTreeNodeType::INTERNAL);
        auto* new_root = getNode(new_root_id);
        
        if (!new_root) {
            releaseNode(root);
            return Status::ERROR;
        }
        
        new_root->setType(BTreeNodeType::ROOT);
        new_root->setChild(0, root_page_id_);
        root->setParentId(new_root_id);
        root->setType(BTreeNodeType::INTERNAL);
        
        releaseNode(root);
        
        // Split da root antiga
        splitChild(new_root, 0);
        
        root_page_id_ = new_root_id;
        root = new_root;
        stats_.height++;
    }
    
    Status status = insertInternal(key, value, root_page_id_);
    releaseNode(root);
    
    return status;
}

template<typename K, typename V>
Status BTree<K, V>::insertInternal(const K& key, const V& value, PageId node_page) {
    auto* node = getNode(node_page);
    if (!node) {
        return Status::ERROR;
    }
    
    int pos = node->findKeyIndex(key);
    
    if (node->isLeaf()) {
        // Inserir na folha
        if (config_.unique && pos >= 0 && pos < node->getKeyCount() && 
            node->getKey(pos) == key) {
            // Chave duplicada em índice único
            releaseNode(node);
            return Status::ALREADY_EXISTS;
        }
        
        if (node->insert(key, value)) {
            releaseNode(node);
            return Status::OK;
        } else {
            // Folha cheia - deveria ter sido tratada antes
            releaseNode(node);
            return Status::ERROR;
        }
    } else {
        // Nó interno, descer para o filho apropriado
        int child_index = node->findChildIndex(key);
        PageId child_page = node->getChild(child_index);
        
        auto* child = getNode(child_page);
        if (!child) {
            releaseNode(node);
            return Status::ERROR;
        }
        
        if (child->isFull()) {
            // Filho cheio, fazer split
            splitChild(node, child_index);
            
            // Decidir qual filho descer após split
            if (key > node->getKey(child_index)) {
                child_page = node->getChild(child_index + 1);
            } else {
                child_page = node->getChild(child_index);
            }
            
            releaseNode(child);
            child = getNode(child_page);
        }
        
        Status status = insertInternal(key, value, child_page);
        releaseNode(node);
        releaseNode(child);
        
        return status;
    }
}

template<typename K, typename V>
Status BTree<K, V>::remove(const K& key) {
    auto status = removeInternal(key, root_page_id_);
    
    if (status == Status::OK) {
        stats_.total_keys--;
        
        // Verificar se root está vazia
        auto* root = getNode(root_page_id_);
        if (root && root->getKeyCount() == 0 && !root->isLeaf()) {
            // Root vazia, promover filho
            PageId old_root = root_page_id_;
            root_page_id_ = root->getChild(0);
            
            auto* new_root = getNode(root_page_id_);
            if (new_root) {
                new_root->setType(BTreeNodeType::ROOT);
                new_root->setParentId(INVALID_PAGE_ID);
                releaseNode(new_root);
            }
            
            deleteNode(old_root);
            stats_.height--;
        }
        
        releaseNode(root);
    }
    
    return status;
}

template<typename K, typename V>
Status BTree<K, V>::removeInternal(const K& key, PageId node_page) {
    auto* node = getNode(node_page);
    if (!node) {
        return Status::ERROR;
    }
    
    int pos = node->findKeyIndex(key);
    
    if (node->isLeaf()) {
        // Remover da folha
        if (pos >= 0 && pos < node->getKeyCount() && node->getKey(pos) == key) {
            node->remove(pos);
            releaseNode(node);
            return Status::OK;
        }
        releaseNode(node);
        return Status::NOT_FOUND;
    } else {
        // Nó interno
        bool key_in_node = (pos >= 0 && pos < node->getKeyCount() && 
                            node->getKey(pos) == key);
        
        if (key_in_node) {
            // Remover chave de nó interno
            auto* left_child = getNode(node->getChild(pos));
            auto* right_child = getNode(node->getChild(pos + 1));
            
            if (!left_child || !right_child) {
                releaseNode(node);
                if (left_child) releaseNode(left_child);
                if (right_child) releaseNode(right_child);
                return Status::ERROR;
            }
            
            if (left_child->getKeyCount() >= MIN_KEYS + 1) {
                // Pegar predecessor
                auto* pred = left_child;
                while (!pred->isLeaf()) {
                    PageId child = pred->getChild(pred->getKeyCount());
                    releaseNode(pred);
                    pred = getNode(child);
                }
                
                K pred_key = pred->getKey(pred->getKeyCount() - 1);
                V pred_val = pred->getValue(pred->getKeyCount() - 1);
                
                node->setKey(pos, pred_key);
                if (node->isLeaf()) {
                    node->setValue(pos, pred_val);
                }
                
                releaseNode(pred);
                removeInternal(pred_key, node->getChild(pos));
                
            } else if (right_child->getKeyCount() >= MIN_KEYS + 1) {
                // Pegar sucessor
                auto* succ = right_child;
                while (!succ->isLeaf()) {
                    PageId child = succ->getChild(0);
                    releaseNode(succ);
                    succ = getNode(child);
                }
                
                K succ_key = succ->getKey(0);
                V succ_val = succ->getValue(0);
                
                node->setKey(pos, succ_key);
                if (node->isLeaf()) {
                    node->setValue(pos, succ_val);
                }
                
                releaseNode(succ);
                removeInternal(succ_key, node->getChild(pos + 1));
                
            } else {
                // Merge children
                mergeNodes(node, pos);
                removeInternal(key, node->getChild(pos));
            }
            
            releaseNode(left_child);
            releaseNode(right_child);
            
        } else {
            // Descer para o filho apropriado
            int child_index = node->findChildIndex(key);
            PageId child_page = node->getChild(child_index);
            
            auto* child = getNode(child_page);
            if (!child) {
                releaseNode(node);
                return Status::ERROR;
            }
            
            if (child->isUnderflow()) {
                handleUnderflow(child, node_page, child_index);
                releaseNode(child);
                
                // Recarregar child após possível merge
                child = getNode(child_page);
                if (!child) {
                    releaseNode(node);
                    return Status::ERROR;
                }
            }
            
            Status status = removeInternal(key, child_page);
            releaseNode(node);
            releaseNode(child);
            
            return status;
        }
    }
    
    releaseNode(node);
    return Status::OK;
}

template<typename K, typename V>
Status BTree<K, V>::find(const K& key, V& value) const {
    auto* node = findLeaf(key);
    if (!node) {
        return Status::NOT_FOUND;
    }
    
    int pos = node->findKeyIndex(key);
    
    if (pos >= 0 && pos < node->getKeyCount() && node->getKey(pos) == key) {
        value = node->getValue(pos);
        releaseNode(node);
        stats_.cache_hits++;
        return Status::OK;
    }
    
    releaseNode(node);
    stats_.cache_misses++;
    return Status::NOT_FOUND;
}

template<typename K, typename V>
std::vector<V> BTree<K, V>::findMany(const K& key) const {
    std::vector<V> results;
    
    auto* node = findLeaf(key);
    if (!node) {
        return results;
    }
    
    int pos = node->findKeyIndex(key);
    
    // Coletar todas as ocorrências da chave
    while (pos >= 0 && pos < node->getKeyCount() && node->getKey(pos) == key) {
        results.push_back(node->getValue(pos));
        pos++;
    }
    
    // Verificar próxima folha se necessário
    if (pos >= node->getKeyCount()) {
        PageId next = node->getNextId();
        releaseNode(node);
        
        while (next != INVALID_PAGE_ID) {
            node = getNode(next);
            if (!node) break;
            
            if (node->getKeyCount() > 0 && node->getKey(0) == key) {
                for (int i = 0; i < node->getKeyCount() && node->getKey(i) == key; i++) {
                    results.push_back(node->getValue(i));
                }
                next = node->getNextId();
                releaseNode(node);
            } else {
                releaseNode(node);
                break;
            }
        }
    } else {
        releaseNode(node);
    }
    
    return results;
}

template<typename K, typename V>
std::vector<V> BTree<K, V>::rangeQuery(const K& start, const K& end) {
    std::vector<V> results;
    
    // Encontrar primeira chave >= start
    auto it = findFirst(start);
    
    while (it != end() && (*it).first <= end) {
        results.push_back((*it).second);
        ++it;
    }
    
    return results;
}

template<typename K, typename V>
std::vector<std::pair<K, V>> BTree<K, V>::rangeQueryWithKeys(const K& start, const K& end) {
    std::vector<std::pair<K, V>> results;
    
    auto it = findFirst(start);
    
    while (it != end()) {
        auto [key, value] = *it;
        if (key > end) break;
        
        results.emplace_back(key, value);
        ++it;
    }
    
    return results;
}

template<typename K, typename V>
Status BTree<K, V>::bulkInsert(const std::vector<std::pair<K, V>>& entries) {
    // Ordenar entradas para inserção em lote
    std::vector<std::pair<K, V>> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    Status status = Status::OK;
    for (const auto& [key, value] : sorted) {
        status = insert(key, value);
        if (status != Status::OK && status != Status::ALREADY_EXISTS) {
            break;
        }
    }
    
    return status;
}

template<typename K, typename V>
size_t BTree<K, V>::bulkDelete(const std::vector<K>& keys) {
    size_t deleted = 0;
    
    for (const auto& key : keys) {
        if (remove(key) == Status::OK) {
            deleted++;
        }
    }
    
    return deleted;
}

template<typename K, typename V>
typename BTree<K, V>::Iterator BTree<K, V>::findFirst(const K& key) {
    auto* node = findLeaf(key);
    if (!node) {
        return end();
    }
    
    int pos = node->findKeyIndex(key);
    if (pos < 0) pos = 0;
    
    // Ajustar se necessário
    while (pos < node->getKeyCount() && node->getKey(pos) < key) {
        pos++;
    }
    
    PageId page_id = node->getPageId();
    releaseNode(node);
    
    // Criar iterator
    Iterator it(this);
    it.current_page_ = page_id;
    it.current_slot_ = pos;
    it.loadCurrentNode();
    
    if (it.current_slot_ >= it.current_node_->getKeyCount()) {
        ++it;
    }
    
    return it;
}

template<typename K, typename V>
typename BTree<K, V>::Iterator BTree<K, V>::findLast(const K& key) {
    // Similar ao findFirst, mas para a última ocorrência
    auto* node = findLeaf(key);
    if (!node) {
        return end();
    }
    
    int pos = node->findKeyIndex(key);
    if (pos >= node->getKeyCount()) {
        pos = node->getKeyCount() - 1;
    }
    
    while (pos >= 0 && node->getKey(pos) > key) {
        pos--;
    }
    
    PageId page_id = node->getPageId();
    releaseNode(node);
    
    Iterator it(this);
    it.current_page_ = page_id;
    it.current_slot_ = pos;
    it.loadCurrentNode();
    
    return it;
}

template<typename K, typename V>
BTreeNode<K, V>* BTree<K, V>::getNode(PageId page_id) const {
    if (page_id == INVALID_PAGE_ID) {
        return nullptr;
    }
    
    auto* page = buffer_pool_->fetchPage(id_, page_id);
    if (!page) {
        return nullptr;
    }
    
    auto* node = new BTreeNode<K, V>(page_id);
    node->deserialize(page->getData());
    
    buffer_pool_->unpinPage(page_id, false);
    
    return node;
}

template<typename K, typename V>
void BTree<K, V>::releaseNode(BTreeNode<K, V>* node) const {
    if (!node) return;
    
    if (node->isDirty()) {
        // Persistir nó
        char buffer[PAGE_SIZE];
        node->serialize(buffer);
        
        auto* page = buffer_pool_->fetchPage(id_, node->getPageId(), FetchMode::WRITE);
        if (page) {
            memcpy(page->getData(), buffer, PAGE_SIZE);
            buffer_pool_->unpinPage(node->getPageId(), true);
        }
    }
    
    delete node;
}

template<typename K, typename V>
PageId BTree<K, V>::createNode(BTreeNodeType type) {
    auto* page = buffer_pool_->createNewPage(id_, PageType::INDEX_PAGE);
    if (!page) {
        return INVALID_PAGE_ID;
    }
    
    PageId page_id = page->getPageId();
    buffer_pool_->unpinPage(page_id, true);
    
    // Inicializar nó
    auto* node = new BTreeNode<K, V>(page_id, type);
    node->markDirty();
    releaseNode(node);
    
    stats_.total_nodes++;
    if (type == BTreeNodeType::LEAF) {
        stats_.leaf_nodes++;
    } else {
        stats_.internal_nodes++;
    }
    
    return page_id;
}

template<typename K, typename V>
void BTree<K, V>::deleteNode(PageId page_id) {
    // TODO: Marcar página como livre para reuso
    stats_.total_nodes--;
}

template<typename K, typename V>
BTreeNode<K, V>* BTree<K, V>::findLeaf(const K& key) const {
    auto* node = getNode(root_page_id_);
    if (!node) {
        return nullptr;
    }
    
    while (node && !node->isLeaf()) {
        int child_index = node->findChildIndex(key);
        PageId child_page = node->getChild(child_index);
        
        auto* child = getNode(child_page);
        releaseNode(node);
        node = child;
    }
    
    return node;
}

template<typename K, typename V>
void BTree<K, V>::splitChild(BTreeNode<K, V>* parent, int child_index) {
    PageId child_page = parent->getChild(child_index);
    auto* child = getNode(child_page);
    
    if (!child || !child->isFull()) {
        if (child) releaseNode(child);
        return;
    }
    
    // Criar novo nó
    PageId new_child_page = createNode(child->getType());
    auto* new_child = getNode(new_child_page);
    
    if (!new_child) {
        releaseNode(child);
        return;
    }
    
    // Obter chave média
    K mid_key = child->getSplitKey();
    
    // Dividir filho
    auto split_node = child->split();
    
    // Copiar dados para novo nó
    for (int i = 0; i < split_node->getKeyCount(); i++) {
        new_child->setKey(i, split_node->getKey(i));
        if (child->isLeaf()) {
            new_child->setValue(i, split_node->getValue(i));
        }
    }
    
    if (!child->isLeaf()) {
        for (int i = 0; i <= split_node->getKeyCount(); i++) {
            new_child->setChild(i, split_node->getChild(i));
        }
    }
    
    // Atualizar pai
    parent->insertAt(child_index, mid_key, V(), new_child_page);
    
    // Atualizar links da lista ligada de folhas
    if (child->isLeaf()) {
        new_child->setNextId(child->getNextId());
        new_child->setPrevId(child->getPageId());
        child->setNextId(new_child_page);
    }
    
    // Atualizar parent links
    child->setParentId(parent->getPageId());
    new_child->setParentId(parent->getPageId());
    
    releaseNode(child);
    releaseNode(new_child);
    
    stats_.internal_nodes++;
    stats_.leaf_nodes += child->isLeaf() ? 1 : 0;
}

template<typename K, typename V>
bool BTree<K, V>::mergeNodes(BTreeNode<K, V>* parent, int index) {
    PageId left_page = parent->getChild(index);
    PageId right_page = parent->getChild(index + 1);
    
    auto* left = getNode(left_page);
    auto* right = getNode(right_page);
    
    if (!left || !right) {
        if (left) releaseNode(left);
        if (right) releaseNode(right);
        return false;
    }
    
    // Mover chave do pai para o nó esquerdo (para nós internos)
    if (!left->isLeaf()) {
        left->setKey(left->getKeyCount(), parent->getKey(index));
        left->setChild(left->getChildCount(), right->getChild(0));
    }
    
    // Merge dos nós
    left->merge(right);
    
    // Ajustar links da lista ligada
    if (left->isLeaf()) {
        left->setNextId(right->getNextId());
    }
    
    // Remover chave do pai
    parent->remove(index);
    
    // Limpar nó direito
    deleteNode(right_page);
    
    releaseNode(left);
    releaseNode(right);
    
    return true;
}

template<typename K, typename V>
void BTree<K, V>::redistribute(BTreeNode<K, V>* parent, int index, bool from_left) {
    if (from_left) {
        PageId left_page = parent->getChild(index - 1);
        PageId right_page = parent->getChild(index);
        
        auto* left = getNode(left_page);
        auto* right = getNode(right_page);
        
        if (left && right) {
            right->redistribute(left, true);
            
            // Atualizar chave no pai
            if (!right->isLeaf()) {
                parent->setKey(index - 1, left->getKey(left->getKeyCount() - 1));
            }
            
            releaseNode(left);
            releaseNode(right);
        }
    } else {
        PageId left_page = parent->getChild(index);
        PageId right_page = parent->getChild(index + 1);
        
        auto* left = getNode(left_page);
        auto* right = getNode(right_page);
        
        if (left && right) {
            left->redistribute(right, false);
            
            // Atualizar chave no pai
            if (!left->isLeaf()) {
                parent->setKey(index, right->getKey(0));
            }
            
            releaseNode(left);
            releaseNode(right);
        }
    }
}

template<typename K, typename V>
void BTree<K, V>::handleUnderflow(BTreeNode<K, V>* node, PageId parent_page, int index_in_parent) {
    auto* parent = getNode(parent_page);
    if (!parent) return;
    
    // Tentar redistribuir com irmão esquerdo
    if (index_in_parent > 0) {
        auto* left_sibling = getNode(parent->getChild(index_in_parent - 1));
        if (left_sibling && left_sibling->getKeyCount() > MIN_KEYS) {
            redistribute(parent, index_in_parent, true);
            releaseNode(left_sibling);
            releaseNode(parent);
            return;
        }
        if (left_sibling) releaseNode(left_sibling);
    }
    
    // Tentar redistribuir com irmão direito
    if (index_in_parent < parent->getChildCount() - 1) {
        auto* right_sibling = getNode(parent->getChild(index_in_parent + 1));
        if (right_sibling && right_sibling->getKeyCount() > MIN_KEYS) {
            redistribute(parent, index_in_parent, false);
            releaseNode(right_sibling);
            releaseNode(parent);
            return;
        }
        if (right_sibling) releaseNode(right_sibling);
    }
    
    // Se não foi possível redistribuir, fazer merge
    if (index_in_parent > 0) {
        mergeNodes(parent, index_in_parent - 1);
    } else {
        mergeNodes(parent, index_in_parent);
    }
    
    releaseNode(parent);
}

template<typename K, typename V>
IndexStats BTree<K, V>::getStats() const {
    // Atualizar estatísticas
    stats_.avg_fill_factor = 0;
    
    // Calcular fator de preenchimento médio
    std::queue<PageId> nodes;
    nodes.push(root_page_id_);
    size_t total_fill = 0;
    size_t node_count = 0;
    
    while (!nodes.empty()) {
        PageId current = nodes.front();
        nodes.pop();
        
        auto* node = getNode(current);
        if (!node) continue;
        
        node_count++;
        total_fill += (node->getKeyCount() * 100) / BTreeNode<K, V>::MAX_KEYS;
        
        if (!node->isLeaf()) {
            for (int i = 0; i <= node->getKeyCount(); i++) {
                PageId child = node->getChild(i);
                if (child != INVALID_PAGE_ID) {
                    nodes.push(child);
                }
            }
        }
        
        releaseNode(node);
    }
    
    if (node_count > 0) {
        stats_.avg_fill_factor = static_cast<double>(total_fill) / node_count;
    }
    
    return stats_;
}

template<typename K, typename V>
void BTree<K, V>::validate() const {
    auto* root = getNode(root_page_id_);
    if (root) {
        validateNode(root, K(), K());
        releaseNode(root);
    }
}

template<typename K, typename V>
bool BTree<K, V>::validateNode(const BTreeNode<K, V>* node, const K& min_key, const K& max_key) const {
    if (!node) return false;
    
    // Verificar número de chaves
    if (node->getKeyCount() > BTreeNode<K, V>::MAX_KEYS) {
        std::cerr << "Node has too many keys: " << node->getKeyCount() << std::endl;
        return false;
    }
    
    if (!node->isRoot() && node->getKeyCount() < BTreeNode<K, V>::MIN_KEYS) {
        std::cerr << "Node has too few keys: " << node->getKeyCount() << std::endl;
        return false;
    }
    
    // Verificar ordem das chaves
    for (int i = 1; i < node->getKeyCount(); i++) {
        if (node->getKey(i) < node->getKey(i - 1)) {
            std::cerr << "Keys out of order at index " << i << std::endl;
            return false;
        }
    }
    
    // Verificar limites
    if (node->getKeyCount() > 0) {
        if (node->getKey(0) < min_key) {
            std::cerr << "Key below minimum bound" << std::endl;
            return false;
        }
        if (node->getKey(node->getKeyCount() - 1) > max_key) {
            std::cerr << "Key above maximum bound" << std::endl;
            return false;
        }
    }
    
    // Verificar filhos
    if (!node->isLeaf()) {
        for (int i = 0; i <= node->getKeyCount(); i++) {
            PageId child_id = node->getChild(i);
            auto* child = getNode(child_id);
            
            if (!child) {
                std::cerr << "Missing child at index " << i << std::endl;
                return false;
            }
            
            K child_min = (i == 0) ? min_key : node->getKey(i - 1);
            K child_max = (i == node->getKeyCount()) ? max_key : node->getKey(i);
            
            if (!validateNode(child, child_min, child_max)) {
                releaseNode(child);
                return false;
            }
            
            releaseNode(child);
        }
    }
    
    return true;
}

template<typename K, typename V>
void BTree<K, V>::dump() const {
    std::cout << "B-Tree Index (id=" << id_ << ")\n";
    std::cout << "Height: " << stats_.height << "\n";
    std::cout << "Total keys: " << stats_.total_keys << "\n";
    std::cout << "Total nodes: " << stats_.total_nodes << "\n";
    std::cout << "  Leaf nodes: " << stats_.leaf_nodes << "\n";
    std::cout << "  Internal nodes: " << stats_.internal_nodes << "\n\n";
    
    // BFS traversal
    std::queue<std::pair<PageId, int>> nodes;
    nodes.push({root_page_id_, 0});
    int current_level = 0;
    
    while (!nodes.empty()) {
        auto [page_id, level] = nodes.front();
        nodes.pop();
        
        if (level > current_level) {
            std::cout << "\n";
            current_level = level;
        }
        
        auto* node = getNode(page_id);
        if (node) {
            node->dump(level);
            
            if (!node->isLeaf()) {
                for (int i = 0; i <= node->getKeyCount(); i++) {
                    PageId child = node->getChild(i);
                    if (child != INVALID_PAGE_ID) {
                        nodes.push({child, level + 1});
                    }
                }
            }
            
            releaseNode(node);
        }
    }
    
    std::cout << std::endl;
}

template<typename K, typename V>
void BTree<K, V>::traverse(std::function<void(const K&, const V&)> func) const {
    auto* node = getNode(root_page_id_);
    if (!node) return;
    
    std::stack<PageId> node_stack;
    node_stack.push(root_page_id_);
    
    while (!node_stack.empty()) {
        PageId current = node_stack.top();
        node_stack.pop();
        
        node = getNode(current);
        if (!node) continue;
        
        if (node->isLeaf()) {
            for (int i = 0; i < node->getKeyCount(); i++) {
                func(node->getKey(i), node->getValue(i));
            }
        } else {
            // Processar em ordem reversa para manter ordem
            for (int i = node->getKeyCount(); i >= 0; i--) {
                PageId child = node->getChild(i);
                if (child != INVALID_PAGE_ID) {
                    node_stack.push(child);
                }
            }
        }
        
        releaseNode(node);
    }
}

template<typename K, typename V>
Status BTree<K, V>::vacuum() {
    // Reconstruir árvore compactando espaço
    std::vector<std::pair<K, V>> entries;
    
    traverse([&entries](const K& key, const V& value) {
        entries.emplace_back(key, value);
    });
    
    // Reconstruir
    return rebuild();
}

template<typename K, typename V>
Status BTree<K, V>::rebuild() {
    // Coletar todas as entradas
    std::vector<std::pair<K, V>> entries;
    
    traverse([&entries](const K& key, const V& value) {
        entries.emplace_back(key, value);
    });
    
    // Limpar árvore atual
    // TODO: Deletar todos os nós
    
    // Reconstruir
    return bulkInsert(entries);
}

// Instanciação explícita
template class BTree<int32_t, RecordId>;
template class BTree<int64_t, RecordId>;
template class BTree<std::string, RecordId>;

} // namespace orangesql