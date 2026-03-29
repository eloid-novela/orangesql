#ifndef ORANGESQL_BUFFER_POOL_H
#define ORANGESQL_BUFFER_POOL_H

#include "page.h"
#include "file_manager.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <condition_variable>

namespace orangesql {

// Estatísticas do buffer pool
struct BufferPoolStats {
    size_t hits;
    size_t misses;
    size_t reads;
    size_t writes;
    size_t evictions;
    size_t pinned_pages;
    size_t dirty_pages;
    double hit_ratio;
    
    BufferPoolStats() : hits(0), misses(0), reads(0), writes(0), 
                       evictions(0), pinned_pages(0), dirty_pages(0), hit_ratio(0) {}
};

// Modo de busca de página
enum class FetchMode {
    READ,        // Apenas leitura
    WRITE,       // Escrita (marca dirty)
    PIN,         // Fixa na memória
    UNPIN        // Libera página
};

// Frame do buffer pool
class BufferFrame {
public:
    BufferFrame();
    ~BufferFrame();
    
    Page* getPage() { return page_.get(); }
    const Page* getPage() const { return page_.get(); }
    
    bool isPinned() const { return pin_count_ > 0; }
    void pin() { pin_count_++; }
    void unpin() { if (pin_count_ > 0) pin_count_--; }
    size_t getPinCount() const { return pin_count_; }
    
    bool isDirty() const { return dirty_; }
    void setDirty(bool dirty) { dirty_ = dirty; }
    
    bool isLatched() const { return latched_; }
    void latch() { latched_ = true; }
    void unlatch() { latched_ = false; }
    
    uint32_t getLSN() const { return lsn_; }
    void setLSN(uint32_t lsn) { lsn_ = lsn; }
    
    std::chrono::steady_clock::time_point getLastAccess() const { return last_access_; }
    void updateAccessTime() { last_access_ = std::chrono::steady_clock::now(); }
    
private:
    std::unique_ptr<Page> page_;
    size_t pin_count_;
    bool dirty_;
    bool latched_;
    uint32_t lsn_;
    std::chrono::steady_clock::time_point last_access_;
};

// Buffer pool com gerenciamento LRU e suporte a pinagem
class BufferPool {
public:
    static constexpr size_t DEFAULT_POOL_SIZE = 1024;  // 1024 páginas (4MB)
    static constexpr size_t MAX_POOL_SIZE = 1048576;   // 1 milhão de páginas (4GB)
    
    explicit BufferPool(size_t pool_size = DEFAULT_POOL_SIZE);
    ~BufferPool();
    
    // Interface principal
    Page* fetchPage(TableId table, PageId page_id, FetchMode mode = FetchMode::READ);
    Page* createNewPage(TableId table, PageType type = PageType::DATA_PAGE);
    void unpinPage(PageId page_id, bool dirty = false);
    void flushPage(PageId page_id);
    void flushAll();
    
    // Gerenciamento de páginas
    bool isPageInPool(PageId page_id) const;
    void discardPage(PageId page_id);
    void discardAll();
    
    // Estatísticas
    const BufferPoolStats& getStats() const { return stats_; }
    void resetStats();
    double getHitRatio() const;
    
    // Configuração
    void setPoolSize(size_t new_size);
    size_t getPoolSize() const { return pool_size_; }
    size_t getUsedFrames() const { return frames_.size(); }
    
    // Checkpoint
    void sync();
    
    // Debug
    void dump() const;
    
private:
    size_t pool_size_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    
    std::unordered_map<PageId, std::unique_ptr<BufferFrame>> frames_;
    std::list<PageId> lru_list_;
    std::unordered_map<PageId, std::list<PageId>::iterator> lru_iterators_;
    
    FileManager file_manager_;
    BufferPoolStats stats_;
    
    // Estratégias de substituição
    enum class EvictionStrategy {
        LRU,        // Least Recently Used
        CLOCK,      // Clock algorithm
        FIFO        // First In First Out
    };
    EvictionStrategy strategy_;
    
    // Clock hand para algoritmo CLOCK
    size_t clock_hand_;
    
    // Métodos internos
    PageId selectVictim();
    void evictPage();
    void updateLRU(PageId page_id);
    
    Page* readFromDisk(TableId table, PageId page_id);
    void writeToDisk(Page* page);
    
    // Clock algorithm
    PageId selectVictimClock();
    
    // Wait para páginas pinadas
    bool waitForUnpin(PageId page_id, std::unique_lock<std::mutex>& lock);
};

} // namespace orangesql

#endif // ORANGESQL_BUFFER_POOL_H