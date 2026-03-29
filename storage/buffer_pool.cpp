#include "buffer_pool.h"
#include <iostream>
#include <algorithm>

namespace orangesql {

BufferFrame::BufferFrame() 
    : pin_count_(0)
    , dirty_(false)
    , latched_(false)
    , lsn_(0)
    , last_access_(std::chrono::steady_clock::now()) {
}

BufferFrame::~BufferFrame() = default;

BufferPool::BufferPool(size_t pool_size) 
    : pool_size_(std::min(pool_size, MAX_POOL_SIZE))
    , strategy_(EvictionStrategy::LRU)
    , clock_hand_(0) {
    file_manager_.init();
    resetStats();
}

BufferPool::~BufferPool() {
    flushAll();
}

Page* BufferPool::fetchPage(TableId table, PageId page_id, FetchMode mode) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = frames_.find(page_id);
    if (it != frames_.end()) {
        // Page found in cache
        stats_.hits++;
        
        auto* frame = it->second.get();
        
        // Se está latched, esperar
        while (frame->isLatched()) {
            cv_.wait(lock);
        }
        
        frame->latch();
        frame->updateAccessTime();
        
        if (mode == FetchMode::WRITE) {
            frame->setDirty(true);
        }
        
        if (mode == FetchMode::PIN) {
            frame->pin();
        }
        
        updateLRU(page_id);
        frame->unlatch();
        
        return frame->getPage();
    }
    
    // Page not in cache
    stats_.misses++;
    
    // Evict if necessary
    while (frames_.size() >= pool_size_) {
        evictPage();
    }
    
    // Read from disk
    Page* page = readFromDisk(table, page_id);
    if (!page) {
        return nullptr;
    }
    
    // Create new frame
    auto frame = std::make_unique<BufferFrame>();
    frame->getPage() = std::move(page);
    frame->latch();
    frame->updateAccessTime();
    
    if (mode == FetchMode::WRITE) {
        frame->setDirty(true);
    }
    
    if (mode == FetchMode::PIN) {
        frame->pin();
    }
    
    PageId pid = page->getPageId();
    frames_[pid] = std::move(frame);
    
    lru_list_.push_front(pid);
    lru_iterators_[pid] = lru_list_.begin();
    
    stats_.reads++;
    frames_[pid]->unlatch();
    
    return frames_[pid]->getPage();
}

Page* BufferPool::createNewPage(TableId table, PageType type) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Gerar novo PageId
    static std::atomic<PageId> next_page_id{1};
    PageId new_page_id = next_page_id++;
    
    auto page = std::make_unique<Page>(new_page_id, table);
    page->setType(type);
    
    // Evict if necessary
    while (frames_.size() >= pool_size_) {
        evictPage();
    }
    
    // Create frame
    auto frame = std::make_unique<BufferFrame>();
    frame->getPage() = std::move(page);
    frame->latch();
    frame->setDirty(true);
    frame->updateAccessTime();
    
    frames_[new_page_id] = std::move(frame);
    
    lru_list_.push_front(new_page_id);
    lru_iterators_[new_page_id] = lru_list_.begin();
    
    stats_.writes++;
    frames_[new_page_id]->unlatch();
    
    return frames_[new_page_id]->getPage();
}

void BufferPool::unpinPage(PageId page_id, bool dirty) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = frames_.find(page_id);
    if (it != frames_.end()) {
        it->second->unpin();
        if (dirty) {
            it->second->setDirty(true);
        }
    }
}

void BufferPool::flushPage(PageId page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = frames_.find(page_id);
    if (it != frames_.end() && it->second->isDirty()) {
        writeToDisk(it->second->getPage());
        it->second->setDirty(false);
        stats_.writes++;
    }
}

void BufferPool::flushAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [page_id, frame] : frames_) {
        if (frame->isDirty()) {
            writeToDisk(frame->getPage());
            frame->setDirty(false);
            stats_.writes++;
        }
    }
}

bool BufferPool::isPageInPool(PageId page_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frames_.find(page_id) != frames_.end();
}

void BufferPool::discardPage(PageId page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = frames_.find(page_id);
    if (it != frames_.end()) {
        if (it->second->isDirty()) {
            writeToDisk(it->second->getPage());
        }
        
        lru_list_.erase(lru_iterators_[page_id]);
        lru_iterators_.erase(page_id);
        frames_.erase(it);
    }
}

void BufferPool::discardAll() {
    flushAll();
    
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.clear();
    lru_list_.clear();
    lru_iterators_.clear();
}

void BufferPool::resetStats() {
    stats_.hits = 0;
    stats_.misses = 0;
    stats_.reads = 0;
    stats_.writes = 0;
    stats_.evictions = 0;
}

double BufferPool::getHitRatio() const {
    size_t total = stats_.hits + stats_.misses;
    return total > 0 ? static_cast<double>(stats_.hits) / total : 0.0;
}

void BufferPool::setPoolSize(size_t new_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    new_size = std::min(new_size, MAX_POOL_SIZE);
    
    while (frames_.size() > new_size) {
        evictPage();
    }
    
    pool_size_ = new_size;
}

void BufferPool::sync() {
    flushAll();
}

PageId BufferPool::selectVictim() {
    switch (strategy_) {
        case EvictionStrategy::LRU:
            // LRU: escolher página no final da lista
            for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it) {
                auto frame_it = frames_.find(*it);
                if (frame_it != frames_.end() && !frame_it->second->isPinned()) {
                    return *it;
                }
            }
            break;
            
        case EvictionStrategy::CLOCK:
            return selectVictimClock();
            
        case EvictionStrategy::FIFO:
            // FIFO: escolher a mais antiga
            if (!lru_list_.empty()) {
                return lru_list_.back();
            }
            break;
    }
    
    return INVALID_PAGE_ID;
}

void BufferPool::evictPage() {
    PageId victim = selectVictim();
    if (victim != INVALID_PAGE_ID) {
        auto it = frames_.find(victim);
        
        if (it->second->isDirty()) {
            writeToDisk(it->second->getPage());
            stats_.writes++;
        }
        
        lru_list_.erase(lru_iterators_[victim]);
        lru_iterators_.erase(victim);
        frames_.erase(it);
        
        stats_.evictions++;
    }
}

void BufferPool::updateLRU(PageId page_id) {
    auto it = lru_iterators_.find(page_id);
    if (it != lru_iterators_.end()) {
        lru_list_.erase(it->second);
    }
    
    lru_list_.push_front(page_id);
    lru_iterators_[page_id] = lru_list_.begin();
}

Page* BufferPool::readFromDisk(TableId table, PageId page_id) {
    char buffer[PAGE_SIZE];
    std::string filename = std::string(DATA_DIR) + "/table_" + 
                          std::to_string(table) + ".dat";
    
    if (!file_manager_.readPage(filename, page_id, buffer)) {
        return nullptr;
    }
    
    auto page = new Page();
    page->deserialize(buffer);
    return page;
}

void BufferPool::writeToDisk(Page* page) {
    char buffer[PAGE_SIZE];
    page->serialize(buffer);
    
    std::string filename = std::string(DATA_DIR) + "/table_" + 
                          std::to_string(page->getTableId()) + ".dat";
    
    file_manager_.writePage(filename, page->getPageId(), buffer);
    page->clearDirty();
}

PageId BufferPool::selectVictimClock() {
    size_t start = clock_hand_;
    size_t attempts = 0;
    
    while (attempts < frames_.size() * 2) {
        if (clock_hand_ >= frames_.size()) {
            clock_hand_ = 0;
        }
        
        auto it = std::next(frames_.begin(), clock_hand_);
        if (!it->second->isPinned()) {
            clock_hand_ = (clock_hand_ + 1) % frames_.size();
            return it->first;
        }
        
        clock_hand_++;
        attempts++;
    }
    
    return INVALID_PAGE_ID;
}

bool BufferPool::waitForUnpin(PageId page_id, std::unique_lock<std::mutex>& lock) {
    auto it = frames_.find(page_id);
    if (it != frames_.end() && it->second->isPinned()) {
        cv_.wait(lock);
        return true;
    }
    return false;
}

void BufferPool::dump() const {
    std::cout << "Buffer Pool Statistics:\n";
    std::cout << "  Size: " << frames_.size() << "/" << pool_size_ << "\n";
    std::cout << "  Hits: " << stats_.hits << "\n";
    std::cout << "  Misses: " << stats_.misses << "\n";
    std::cout << "  Hit Ratio: " << (getHitRatio() * 100) << "%\n";
    std::cout << "  Reads: " << stats_.reads << "\n";
    std::cout << "  Writes: " << stats_.writes << "\n";
    std::cout << "  Evictions: " << stats_.evictions << "\n";
}

} // namespace orangesql