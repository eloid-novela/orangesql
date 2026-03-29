#include "file_manager.h"
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

namespace orangesql {

FileManager::FileManager() 
    : max_open_files_(100)
    , sync_on_write_(false) {
}

FileManager::~FileManager() {
    shutdown();
}

bool FileManager::init() {
    // Criar diretórios necessários
    try {
        fs::create_directories(DATA_DIR);
        fs::create_directories(LOG_DIR);
        fs::create_directories(SYSTEM_DIR);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

void FileManager::shutdown() {
    syncAll();
    open_files_.clear();
}

bool FileManager::readPage(const std::string& filename, PageId page_id, char* buffer) {
    auto start = std::chrono::high_resolution_clock::now();
    
    auto* handle = openFile(filename, FileMode::READ);
    if (!handle) {
        return false;
    }
    
    size_t offset = page_id * PAGE_SIZE;
    bool success = handle->read(offset, buffer, PAGE_SIZE);
    
    if (success) {
        stats_.reads++;
        stats_.read_bytes += PAGE_SIZE;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    stats_.total_io_time_ms += std::chrono::duration<double, std::milli>(end - start).count();
    
    return success;
}

bool FileManager::writePage(const std::string& filename, PageId page_id, const char* buffer) {
    auto start = std::chrono::high_resolution_clock::now();
    
    auto* handle = openFile(filename, FileMode::READ_WRITE);
    if (!handle) {
        return false;
    }
    
    size_t offset = page_id * PAGE_SIZE;
    bool success = handle->write(offset, buffer, PAGE_SIZE);
    
    if (success) {
        stats_.writes++;
        stats_.write_bytes += PAGE_SIZE;
        
        if (sync_on_write_) {
            handle->sync();
            stats_.fsyncs++;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    stats_.total_io_time_ms += std::chrono::duration<double, std::milli>(end - start).count();
    
    return success;
}

bool FileManager::appendPage(const std::string& filename, const char* buffer) {
    auto start = std::chrono::high_resolution_clock::now();
    
    auto* handle = openFile(filename, FileMode::READ_WRITE);
    if (!handle) {
        return false;
    }
    
    bool success = handle->append(buffer, PAGE_SIZE);
    
    if (success) {
        stats_.writes++;
        stats_.write_bytes += PAGE_SIZE;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    stats_.total_io_time_ms += std::chrono::duration<double, std::milli>(end - start).count();
    
    return success;
}

bool FileManager::createFile(const std::string& filename) {
    std::string path = normalizePath(filename);
    
    try {
        std::ofstream file(path, std::ios::binary);
        return file.is_open();
    } catch (...) {
        return false;
    }
}

bool FileManager::deleteFile(const std::string& filename) {
    std::string path = normalizePath(filename);
    
    try {
        closeFile(filename);
        return fs::remove(path);
    } catch (...) {
        return false;
    }
}

bool FileManager::fileExists(const std::string& filename) {
    std::string path = normalizePath(filename);
    return fs::exists(path);
}

bool FileManager::getFileSize(const std::string& filename, size_t& size) {
    std::string path = normalizePath(filename);
    
    try {
        size = fs::file_size(path);
        return true;
    } catch (...) {
        size = 0;
        return false;
    }
}

bool FileManager::truncateFile(const std::string& filename, size_t size) {
    std::string path = normalizePath(filename);
    
    try {
        fs::resize_file(path, size);
        return true;
    } catch (...) {
        return false;
    }
}

bool FileManager::atomicWrite(const std::string& filename, const char* data, size_t size) {
    // Escrever em arquivo temporário
    std::string temp_filename = filename + ".tmp";
    
    {
        std::ofstream temp(temp_filename, std::ios::binary);
        if (!temp.write(data, size)) {
            return false;
        }
        temp.flush();
    }
    
    // Renomear atomicamente
    return atomicRename(temp_filename, filename);
}

bool FileManager::atomicRename(const std::string& old_name, const std::string& new_name) {
    std::string old_path = normalizePath(old_name);
    std::string new_path = normalizePath(new_name);
    
    try {
        closeFile(old_name);
        closeFile(new_name);
        fs::rename(old_path, new_path);
        return true;
    } catch (...) {
        return false;
    }
}

bool FileManager::sync(const std::string& filename) {
    auto* handle = openFile(filename, FileMode::READ_WRITE);
    if (handle) {
        bool success = handle->sync();
        if (success) {
            stats_.fsyncs++;
        }
        return success;
    }
    return false;
}

bool FileManager::syncAll() {
    bool success = true;
    for (auto& [name, handle] : open_files_) {
        if (!handle->sync()) {
            success = false;
        }
        stats_.fsyncs++;
    }
    return success;
}

bool FileManager::createDirectory(const std::string& path) {
    try {
        return fs::create_directories(path);
    } catch (...) {
        return false;
    }
}

bool FileManager::listDirectory(const std::string& path, std::vector<std::string>& files) {
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            files.push_back(entry.path().filename().string());
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool FileManager::deleteDirectory(const std::string& path) {
    try {
        return fs::remove_all(path) > 0;
    } catch (...) {
        return false;
    }
}

FileManager::FileHandle* FileManager::openFile(const std::string& filename, FileMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string path = normalizePath(filename);
    
    // Verificar se já está aberto
    auto it = open_files_.find(path);
    if (it != open_files_.end()) {
        return it->second.get();
    }
    
    // Evitar se necessário
    if (open_files_.size() >= max_open_files_) {
        evictFile();
    }
    
    // Abrir novo arquivo
    auto handle = std::make_unique<FileHandle>(path, mode);
    if (!handle->isOpen()) {
        return nullptr;
    }
    
    auto* ptr = handle.get();
    open_files_[path] = std::move(handle);
    return ptr;
}

void FileManager::closeFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string path = normalizePath(filename);
    open_files_.erase(path);
}

void FileManager::evictFile() {
    // Simples: remove o primeiro (poderia ser LRU)
    if (!open_files_.empty()) {
        auto it = open_files_.begin();
        it->second->sync();
        open_files_.erase(it);
    }
}

std::string FileManager::normalizePath(const std::string& path) {
    return fs::absolute(path).string();
}

// Implementação de FileHandle
FileManager::FileHandle::FileHandle(const std::string& filename, FileMode mode)
    : filename_(filename) {
    
    std::ios_base::openmode open_mode = std::ios::binary;
    
    switch (mode) {
        case FileMode::READ:
            open_mode |= std::ios::in;
            break;
        case FileMode::WRITE:
            open_mode |= std::ios::out | std::ios::trunc;
            break;
        case FileMode::READ_WRITE:
            open_mode |= std::ios::in | std::ios::out;
            break;
        case FileMode::CREATE:
            open_mode |= std::ios::out | std::ios::trunc;
            break;
    }
    
    file_.open(filename, open_mode);
    mode_ = mode;
}

FileManager::FileHandle::~FileHandle() {
    if (file_.is_open()) {
        sync();
        file_.close();
    }
}

bool FileManager::FileHandle::read(size_t offset, char* buffer, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    file_.seekg(offset, std::ios::beg);
    file_.read(buffer, size);
    return !file_.fail();
}

bool FileManager::FileHandle::write(size_t offset, const char* buffer, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    file_.seekp(offset, std::ios::beg);
    file_.write(buffer, size);
    return !file_.fail();
}

bool FileManager::FileHandle::append(const char* buffer, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    file_.seekp(0, std::ios::end);
    file_.write(buffer, size);
    return !file_.fail();
}

bool FileManager::FileHandle::sync() {
    file_.flush();
    return !file_.fail();
}

size_t FileManager::FileHandle::size() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto pos = file_.tellg();
    file_.seekg(0, std::ios::end);
    size_t sz = file_.tellg();
    file_.seekg(pos);
    return sz;
}

} // namespace orangesql