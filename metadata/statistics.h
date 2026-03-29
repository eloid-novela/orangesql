#ifndef ORANGESQL_STATISTICS_H
#define ORANGESQL_STATISTICS_H

#include "../include/types.h"
#include "catalog.h"
#include <vector>
#include <unordered_map>
#include <random>

namespace orangesql {

// Histograma para uma coluna
struct Histogram {
    struct Bucket {
        Value min;
        Value max;
        size_t count;
        double distinct_ratio;
        
        Bucket() : count(0), distinct_ratio(0) {}
    };
    
    std::string column_name;
    std::vector<Bucket> buckets;
    size_t total_count;
    size_t distinct_count;
    Value min_value;
    Value max_value;
    
    Histogram() : total_count(0), distinct_count(0) {}
    
    // Estimar seletividade para uma condição
    double estimateSelectivity(const ASTNode* condition) const;
    
    // Valor na posição do percentil
    Value getPercentile(double p) const;
};

// Estatísticas de uma coluna
struct ColumnStatistics {
    std::string column_name;
    DataType type;
    
    size_t distinct_values;
    size_t null_count;
    Value min_value;
    Value max_value;
    double avg_length;
    
    // Correlação com outras colunas
    std::unordered_map<std::string, double> correlations;
    
    // Histograma (se aplicável)
    std::unique_ptr<Histogram> histogram;
    
    ColumnStatistics() : distinct_values(0), null_count(0), avg_length(0) {}
};

// Estatísticas de uma tabela
struct TableStatistics {
    std::string table_name;
    TableId table_id;
    
    size_t row_count;
    size_t page_count;
    size_t avg_row_size;
    
    std::unordered_map<std::string, ColumnStatistics> columns;
    
    // Estatísticas de índices
    struct IndexStats {
        std::string index_name;
        size_t distinct_keys;
        size_t leaf_pages;
        size_t height;
    };
    std::unordered_map<std::string, IndexStats> indexes;
    
    // Metadados de atualização
    std::chrono::system_clock::time_point last_analyzed;
    size_t analyze_count;
    
    TableStatistics() : row_count(0), page_count(0), avg_row_size(0), analyze_count(0) {}
    
    // Atualizar estatísticas
    void update(const std::vector<std::vector<Value>>& sample);
    
    // Estimar seletividade
    double estimateSelectivity(const std::string& column, 
                               const Value& value,
                               Operator op) const;
    
    double estimateJoinSelectivity(const std::string& col1,
                                   const std::string& col2,
                                   const TableStatistics& other) const;
    
    // Cardinalidade
    size_t estimateCardinality(const std::string& column) const;
    
    // Amostragem
    std::vector<Value> sampleColumn(const std::string& column, size_t sample_size) const;
};

// Gerenciador de estatísticas
class StatisticsManager {
public:
    StatisticsManager(Catalog* catalog);
    ~StatisticsManager();
    
    // Análise de tabelas
    Status analyzeTable(const std::string& table_name, size_t sample_size = 10000);
    Status analyzeDatabase();
    
    // Consulta de estatísticas
    const TableStatistics* getTableStats(const std::string& table_name) const;
    const ColumnStatistics* getColumnStats(const std::string& table_name,
                                           const std::string& column_name) const;
    
    // Estimativas de custo
    double estimateTableSize(const std::string& table_name) const;
    size_t estimateRowCount(const std::string& table_name) const;
    
    // Atualização incremental
    void updateAfterInsert(const std::string& table_name, 
                           const std::vector<Value>& row);
    void updateAfterDelete(const std::string& table_name,
                           const std::vector<Value>& row);
    void updateAfterUpdate(const std::string& table_name,
                           const std::vector<Value>& old_row,
                           const std::vector<Value>& new_row);
    
    // Persistência
    Status load();
    Status save();
    
    // Configuração
    void setAutoAnalyze(bool enabled) { auto_analyze_ = enabled; }
    void setAnalyzeThreshold(size_t rows) { analyze_threshold_ = rows; }
    
    // Estatísticas do gerenciador
    struct ManagerStats {
        size_t tables_analyzed;
        size_t total_samples;
        double avg_analyze_time_ms;
        
        ManagerStats() : tables_analyzed(0), total_samples(0), avg_analyze_time_ms(0) {}
    };
    
    const ManagerStats& getStats() const { return stats_; }

private:
    Catalog* catalog_;
    std::unordered_map<std::string, TableStatistics> statistics_;
    
    bool auto_analyze_;
    size_t analyze_threshold_;
    std::chrono::steady_clock::time_point last_auto_analyze_;
    
    ManagerStats stats_;
    mutable std::shared_mutex mutex_;
    
    // Coleta de amostras
    std::vector<std::vector<Value>> collectSample(const std::string& table_name,
                                                   size_t sample_size);
    
    // Cálculo de histogramas
    std::unique_ptr<Histogram> buildHistogram(const std::vector<Value>& sample,
                                              const std::string& column,
                                              size_t num_buckets = 100);
    
    // Correlações
    double calculateCorrelation(const std::vector<Value>& col1,
                                const std::vector<Value>& col2);
    
    // Validação
    bool needsAnalyze(const std::string& table_name) const;
    
    // Limpeza
    void expireOldStats();
};

} // namespace orangesql

#endif // ORANGESQL_STATISTICS_H