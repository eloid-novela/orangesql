#ifndef ORANGESQL_FILE_MANAGER_H
#define ORANGESQL_FILE_MANAGER_H

#include "../include/types.h"
#include "../include/constants.h"
#include <string>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <memory>

namespace orangesql {

// Estatísticas de I/O
struct IOStatistics {
    size_t reads;
    size_t writes;
    size_t read_bytes;
    size_t write_bytes;
    size_t fsyncs;
    double total_io_time_ms;
    
    IOStatistics() : reads(0), writes(0), read_bytes(0), 
                     write_bytes(0), fsyncs(0), total_io_time_ms(0) {}
    
    void reset() {
        reads = writes = read_bytes = write_bytes = fsyncs = 0;
        total_io_time_ms = 0;
    }
};

// Modo de abertura de arquivo
enum class FileMode {
    READ,
    WRITE,
    READ_WRITE,
    CREATE
};

// Gerenciador de arquivos com cache e operações atômicas
class FileManager {
public:
    FileManager();
    ~FileManager();
    
    // Inicialização
    bool init();
    void shutdown();
    
    // Operações básicas com páginas
    bool readPage(const std::string& filename, PageId page_id, char* buffer);
    bool writePage(const std::string& filename, PageId page_id, const char* buffer);
    bool appendPage(const std::string& filename, const char* buffer);
    
    // Operações com arquivos
    bool createFile(const std::string& filename);
    bool deleteFile(const std::string& filename);
    bool fileExists(const std::string& filename);
    bool getFileSize(const std::string& filename, size_t& size);
    bool truncateFile(const std::string& filename, size_t size);
    
    // Operações atômicas
    bool atomicWrite(const std::string& filename, const char* data, size_t size);
    bool atomicRename(const std::string& old_name, const std::string& new_name);
    
    // Sincronização
    bool sync(const std::string& filename);
    bool syncAll();
    
    // Diretórios
    bool createDirectory(const std::string& path);
    bool listDirectory(const std::string& path, std::vector<std::string>& files);
    bool deleteDirectory(const std::string& path);
    
    // Estatísticas
    const IOStatistics& getStats() const { return stats_; }
    void resetStats() { stats_.reset(); }
    
    // Cache de arquivos abertos
    class FileHandle {
    public:
        FileHandle(const std::string& filename, FileMode mode);
        ~FileHandle();
        
        bool read(size_t offset, char* buffer, size_t size);
        bool write(size_t offset, const char* buffer, size_t size);
        bool append(const char* buffer, size_t size);
        bool sync();
        size_t size();
        bool isOpen() const { return file_.is_open(); }
        
    private:
        std::fstream file_;
        std::string filename_;
        FileMode mode_;
        std::mutex mutex_;
    };
    
    FileHandle* openFile(const std::string& filename, FileMode mode);
    void closeFile(const std::string& filename);
    
    // Configurações
    void setCacheSize(size_t max_files) { max_open_files_ = max_files; }
    void setSyncOnWrite(bool sync) { sync_on_write_ = sync; }
    
private:
    std::unordered_map<std::string, std::unique_ptr<FileHandle>> open_files_;
    std::mutex mutex_;
    size_t max_open_files_;
    bool sync_on_write_;
    IOStatistics stats_;
    
    // Gerenciamento de cache de arquivos
    void evictFile();
    std::string normalizePath(const std::string& path);
};

} // namespace orangesql

#endif // ORANGESQL_FILE_MANAGER_H