#ifndef ORANGESQL_CONSTANTS_H
#define ORANGESQL_CONSTANTS_H

#include <cstddef>
#include <string>
#include <chrono>

namespace orangesql {

// ============================================
// Versão do Sistema
// ============================================

constexpr const char* VERSION = "1.0.0";
constexpr const char* COPYRIGHT = "OrangeSQL 2024";
constexpr const char* BUILD_DATE = __DATE__ " " __TIME__;

// ============================================
// Configurações de Página
// ============================================

constexpr size_t PAGE_SIZE = 4096;              // 4KB páginas
constexpr size_t PAGE_HEADER_SIZE = 64;         // Tamanho do header
constexpr size_t MAX_PAGES_PER_TABLE = 1 << 32; // 4B páginas
constexpr size_t MAX_RECORDS_PER_PAGE = 100;    // Máximo de registros por página

// ============================================
// Configurações de Buffer Pool
// ============================================

constexpr size_t DEFAULT_BUFFER_POOL_SIZE = 1024;     // 1024 páginas (4MB)
constexpr size_t MAX_BUFFER_POOL_SIZE = 1048576;       // 1M páginas (4GB)
constexpr size_t MIN_BUFFER_POOL_SIZE = 64;            // Mínimo 64 páginas
constexpr double DEFAULT_BUFFER_POOL_HIT_TARGET = 0.95; // 95% hit ratio target

// ============================================
// Configurações de Índice
// ============================================

constexpr size_t BTREE_ORDER = 128;              // Ordem da B-Tree
constexpr size_t BTREE_MAX_KEYS = BTREE_ORDER - 1;
constexpr size_t BTREE_MIN_KEYS = BTREE_ORDER / 2 - 1;
constexpr size_t DEFAULT_FILL_FACTOR = 70;       // 70% fill factor

// ============================================
// Configurações de Transação
// ============================================

constexpr size_t DEFAULT_LOCK_TIMEOUT_MS = 5000;        // 5 segundos
constexpr size_t DEFAULT_DEADLOCK_TIMEOUT_MS = 1000;    // 1 segundo
constexpr size_t DEFAULT_TX_TIMEOUT_SEC = 300;          // 5 minutos
constexpr size_t MAX_TRANSACTIONS = 1000;                // Máximo transações concorrentes

// ============================================
// Configurações de Log
// ============================================

constexpr const char* LOG_DIR = "data/wal";
constexpr const char* DATA_DIR = "data/tables";
constexpr const char* SYSTEM_DIR = "data/system";
constexpr const char* TEMP_DIR = "data/temp";

constexpr size_t MAX_LOG_FILE_SIZE = 1024 * 1024 * 100;    // 100MB
constexpr size_t WAL_BUFFER_SIZE = 1024 * 1024;            // 1MB
constexpr size_t CHECKPOINT_INTERVAL_SEC = 300;            // 5 minutos
constexpr size_t CHECKPOINT_LOG_SIZE = 50 * 1024 * 1024;   // 50MB

// ============================================
// Configurações de Query
// ============================================

constexpr size_t MAX_JOIN_TABLES = 64;                     // Máximo joins por query
constexpr size_t MAX_SUBQUERY_DEPTH = 10;                   // Profundidade máxima de subqueries
constexpr size_t DEFAULT_LIMIT = 1000;                      // LIMIT padrão
constexpr size_t MAX_LIMIT = 1000000;                       // LIMIT máximo
constexpr size_t QUERY_CACHE_SIZE = 1000;                   // Tamanho do cache de queries

// ============================================
// Configurações de Otimizador
// ============================================

constexpr double INDEX_SCAN_COST = 1.0;                     // Custo base de index scan
constexpr double SEQ_SCAN_COST = 10.0;                       // Custo base de sequential scan
constexpr double INDEX_SELECTIVITY_THRESHOLD = 0.2;          // Threshold para uso de índice (20%)
constexpr size_t STATISTICS_TARGET = 10000;                  // Tamanho alvo de amostra
constexpr size_t HISTOGRAM_BUCKETS = 100;                     // Número de buckets no histograma

// ============================================
// Configurações de Memória
// ============================================

constexpr size_t SORT_BUFFER_SIZE = 64 * 1024 * 1024;        // 64MB para sorting
constexpr size_t JOIN_BUFFER_SIZE = 32 * 1024 * 1024;        // 32MB para hash joins
constexpr size_t TEMP_TABLE_MAX_SIZE = 100 * 1024 * 1024;    // 100MB para tabelas temporárias

// ============================================
// Configurações de Rede
// ============================================

constexpr int DEFAULT_PORT = 5432;                          // Porta padrão (PostgreSQL compatible)
constexpr const char* DEFAULT_HOST = "localhost";
constexpr int MAX_CONNECTIONS = 100;                         // Máximo conexões simultâneas
constexpr int SOCKET_TIMEOUT_SEC = 30;                       // Timeout de socket
constexpr int BACKLOG_SIZE = 128;                            // Tamanho da fila de conexões

// ============================================
// Configurações de Autenticação
// ============================================

constexpr const char* DEFAULT_USER = "orangesql";
constexpr const char* DEFAULT_DATABASE = "default";
constexpr size_t PASSWORD_HASH_LENGTH = 64;                  // SHA-256
constexpr int MAX_LOGIN_ATTEMPTS = 5;                         // Máximo tentativas de login
constexpr int LOGIN_TIMEOUT_MINUTES = 30;                     // Timeout após tentativas

// ============================================
// Timeouts e Intervals
// ============================================

constexpr int CONNECTION_TIMEOUT_SEC = 60;                    // Timeout de conexão
constexpr int STATEMENT_TIMEOUT_SEC = 30;                     // Timeout de statement
constexpr int IDLE_TRANSACTION_TIMEOUT_SEC = 300;             // Timeout de transação ociosa
constexpr int LOCK_TIMEOUT_SEC = 30;                           // Timeout de lock
constexpr int STATISTICS_UPDATE_INTERVAL_SEC = 3600;          // Update de estatísticas a cada hora

// ============================================
// Limites de Objetos
// ============================================

constexpr size_t MAX_TABLES = 10000;                          // Máximo de tabelas por database
constexpr size_t MAX_COLUMNS_PER_TABLE = 1000;                 // Máximo de colunas por tabela
constexpr size_t MAX_INDEXES_PER_TABLE = 100;                  // Máximo de índices por tabela
constexpr size_t MAX_CONSTRAINTS_PER_TABLE = 100;              // Máximo de constraints por tabela
constexpr size_t MAX_VIEWS = 1000;                             // Máximo de views
constexpr size_t MAX_SEQUENCES = 1000;                         // Máximo de sequências
constexpr size_t MAX_FUNCTIONS = 1000;                         // Máximo de funções armazenadas

// ============================================
// Limites de Dados
// ============================================

constexpr size_t MAX_VARCHAR_LENGTH = 65535;                   // Máximo comprimento VARCHAR
constexpr size_t MAX_TEXT_LENGTH = 1024 * 1024 * 1024;         // 1GB para TEXT
constexpr size_t MAX_BLOB_SIZE = 1024 * 1024 * 1024;           // 1GB para BLOB
constexpr int32_t MAX_INT = 2147483647;                         // Máximo INT
constexpr int64_t MAX_BIGINT = 9223372036854775807LL;          // Máximo BIGINT
constexpr double MAX_DOUBLE = 1.7976931348623157e308;          // Máximo DOUBLE

// ============================================
// Valores Padrão
// ============================================

constexpr int DEFAULT_DECIMAL_PRECISION = 10;                   // Precisão padrão DECIMAL
constexpr int DEFAULT_DECIMAL_SCALE = 0;                        // Escala padrão DECIMAL
constexpr int DEFAULT_VARCHAR_LENGTH = 255;                      // Comprimento padrão VARCHAR
constexpr int DEFAULT_CHAR_LENGTH = 1;                           // Comprimento padrão CHAR
constexpr const char* DEFAULT_CHARSET = "UTF8";                  // Charset padrão

// ============================================
// Códigos de Erro
// ============================================

namespace ErrorCode {
    constexpr int SUCCESS = 0;
    constexpr int GENERAL_ERROR = 1;
    constexpr int SYNTAX_ERROR = 2;
    constexpr int CONSTRAINT_VIOLATION = 3;
    constexpr int DEADLOCK_DETECTED = 4;
    constexpr int SERIALIZATION_FAILURE = 5;
    constexpr int OUT_OF_MEMORY = 6;
    constexpr int IO_ERROR = 7;
    constexpr int NOT_FOUND = 8;
    constexpr int ALREADY_EXISTS = 9;
    constexpr int PERMISSION_DENIED = 10;
    constexpr int TIMEOUT = 11;
    constexpr int CONNECTION_ERROR = 12;
    constexpr int TRANSACTION_ERROR = 13;
    constexpr int UNSUPPORTED_FEATURE = 14;
}

// ============================================
// Mensagens de Log
// ============================================

namespace LogMessages {
    constexpr const char* STARTUP = "OrangeSQL iniciado com sucesso";
    constexpr const char* SHUTDOWN = "OrangeSQL finalizado";
    constexpr const char* CHECKPOINT = "Checkpoint realizado";
    constexpr const char* RECOVERY = "Recovery concluído";
    constexpr const char* DEADLOCK = "Deadlock detectado e resolvido";
    constexpr const char* BACKUP = "Backup realizado";
    constexpr const char* RESTORE = "Restauração concluída";
}

// ============================================
// Configurações de Performance
// ============================================

constexpr bool ENABLE_QUERY_CACHE = true;                       // Habilitar cache de queries
constexpr bool ENABLE_RESULT_CACHE = true;                      // Habilitar cache de resultados
constexpr bool ENABLE_PREPARED_STATEMENTS = true;                // Habilitar prepared statements
constexpr bool ENABLE_ASYNC_COMMIT = false;                      // Habilitar commit assíncrono
constexpr bool ENABLE_PARALLEL_QUERY = true;                     // Habilitar execução paralela
constexpr int PARALLEL_WORKERS = 4;                              // Número de workers paralelos

// ============================================
// Configurações de Debug
// ============================================

#ifdef ORANGESQL_DEBUG
    constexpr bool DEBUG_MODE = true;
    constexpr bool LOG_SLOW_QUERIES = true;
    constexpr int SLOW_QUERY_THRESHOLD_MS = 100;                 // 100ms
    constexpr bool LOG_ALL_QUERIES = true;
    constexpr bool LOG_TRANSACTIONS = true;
    constexpr bool LOG_LOCKS = true;
#else
    constexpr bool DEBUG_MODE = false;
    constexpr bool LOG_SLOW_QUERIES = true;
    constexpr int SLOW_QUERY_THRESHOLD_MS = 1000;                // 1s
    constexpr bool LOG_ALL_QUERIES = false;
    constexpr bool LOG_TRANSACTIONS = false;
    constexpr bool LOG_LOCKS = false;
#endif

// ============================================
// Configurações de Backup
// ============================================

constexpr const char* BACKUP_DIR = "data/backup";
constexpr int BACKUP_RETENTION_DAYS = 30;                        // Reter backups por 30 dias
constexpr bool AUTO_BACKUP_ENABLED = true;
constexpr int AUTO_BACKUP_INTERVAL_HOURS = 24;                   // Backup automático a cada 24h

// ============================================
// Limites de Recursos
// ============================================

constexpr size_t MAX_MEMORY_USAGE = 1024 * 1024 * 1024;          // 1GB máximo de memória
constexpr size_t MAX_DISK_USAGE = 1024LL * 1024 * 1024 * 1024;   // 1TB máximo de disco
constexpr size_t MAX_FILE_SIZE = 1024LL * 1024 * 1024 * 10;      // 10GB máximo por arquivo
constexpr int MAX_OPEN_FILES = 1000;                              // Máximo arquivos abertos

// ============================================
// Configurações de Replicação
// ============================================

constexpr bool REPLICATION_ENABLED = false;
constexpr int REPLICATION_FACTOR = 1;                             // Fator de replicação
constexpr int REPLICATION_SYNC_TIMEOUT_SEC = 10;                  // Timeout de sincronização

// ============================================
// Configurações de Compressão
// ============================================

constexpr bool ENABLE_COMPRESSION = false;
constexpr int COMPRESSION_LEVEL = 6;                              // Nível de compressão (1-9)

// ============================================
// Features Experimentais
// ============================================

constexpr bool ENABLE_EXPERIMENTAL_FEATURES = false;
constexpr bool ENABLE_VECTORIZED_EXECUTION = false;
constexpr bool ENABLE_ADAPTIVE_OPTIMIZER = false;

} // namespace orangesql

#endif // ORANGESQL_CONSTANTS_H