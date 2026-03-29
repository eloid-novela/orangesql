#include "statistics.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <fstream>

namespace orangesql {

// ============================================
// Histogram Implementation
// ============================================

double Histogram::estimateSelectivity(const ASTNode* condition) const {
    // TODO: Implementar estimativa baseada no histograma
    return 0.1; // Valor padrão
}

Value Histogram::getPercentile(double p) const {
    if (buckets.empty() || p < 0 || p > 1) {
        return Value();
    }
    
    size_t target = static_cast<size_t>(total_count * p);
    size_t cumulative = 0;
    
    for (const auto& bucket : buckets) {
        cumulative += bucket.count;
        if (cumulative >= target) {
            return bucket.max;
        }
    }
    
    return buckets.back().max;
}

// ============================================
// TableStatistics Implementation
// ============================================

void TableStatistics::update(const std::vector<std::vector<Value>>& sample) {
    if (sample.empty()) return;
    
    row_count = sample.size();
    
    // Inicializar estatísticas por coluna
    for (const auto& [col_name, col_stats] : columns) {
        // Coletar valores da coluna
        std::vector<Value> column_values;
        size_t col_idx = 0; // TODO: Mapear nome para índice
        
        for (const auto& row : sample) {
            if (col_idx < row.size()) {
                column_values.push_back(row[col_idx]);
            }
        }
        
        // Calcular estatísticas
        ColumnStatistics& stats = columns[col_name];
        
        // Valores distintos
        std::set<Value> distinct;
        for (const auto& val : column_values) {
            distinct.insert(val);
        }
        stats.distinct_values = distinct.size();
        
        // Null count
        stats.null_count = 0;
        for (const auto& val : column_values) {
            if (val.type == DataType::INTEGER && val.data.int_val == 0 && 
                val.str_val.empty()) {
                stats.null_count++;
            }
        }
        
        // Min/Max
        if (!column_values.empty()) {
            stats.min_value = *std::min_element(column_values.begin(), column_values.end(),
                [](const Value& a, const Value& b) {
                    return compareValuesOrdered(a, b) < 0;
                });
            
            stats.max_value = *std::max_element(column_values.begin(), column_values.end(),
                [](const Value& a, const Value& b) {
                    return compareValuesOrdered(a, b) < 0;
                });
        }
        
        // Média de tamanho para strings
        if (stats.type == DataType::VARCHAR) {
            size_t total_len = 0;
            for (const auto& val : column_values) {
                total_len += val.str_val.length();
            }
            stats.avg_length = static_cast<double>(total_len) / column_values.size();
        }
        
        // Construir histograma
        stats.histogram = std::make_unique<Histogram>();
        // TODO: Preencher histograma
    }
    
    last_analyzed = std::chrono::system_clock::now();
    analyze_count++;
}

double TableStatistics::estimateSelectivity(const std::string& column,
                                            const Value& value,
                                            Operator op) const {
    auto it = columns.find(column);
    if (it == columns.end()) {
        return 0.5; // Valor padrão
    }
    
    const auto& stats = it->second;
    
    switch (op) {
        case Operator::EQ:
            if (stats.distinct_values > 0) {
                return 1.0 / stats.distinct_values;
            }
            return 0.1;
            
        case Operator::LT:
        case Operator::LE:
            if (stats.histogram) {
                return stats.histogram->estimateSelectivity(nullptr); // TODO: Passar condição
            }
            return 0.3;
            
        case Operator::GT:
        case Operator::GE:
            return 0.3;
            
        default:
            return 0.5;
    }
}

double TableStatistics::estimateJoinSelectivity(const std::string& col1,
                                                const std::string& col2,
                                                const TableStatistics& other) const {
    auto it1 = columns.find(col1);
    auto it2 = other.columns.find(col2);
    
    if (it1 == columns.end() || it2 == other.columns.end()) {
        return 0.1;
    }
    
    // Estimativa baseada em valores distintos
    size_t distinct1 = it1->second.distinct_values;
    size_t distinct2 = it2->second.distinct_values;
    
    if (distinct1 == 0 || distinct2 == 0) {
        return 0;
    }
    
    // Assumindo distribuição uniforme e independência
    double selectivity = 1.0 / std::max(distinct1, distinct2);
    
    // Ajustar por correlação se disponível
    auto corr_it = it1->second.correlations.find(col2);
    if (corr_it != it1->second.correlations.end()) {
        selectivity *= (1 + corr_it->second);
    }
    
    return selectivity;
}

size_t TableStatistics::estimateCardinality(const std::string& column) const {
    auto it = columns.find(column);
    if (it == columns.end()) {
        return row_count;
    }
    
    return it->second.distinct_values;
}

std::vector<Value> TableStatistics::sampleColumn(const std::string& column,
                                                 size_t sample_size) const {
    std::vector<Value> result;
    
    auto it = columns.find(column);
    if (it == columns.end()) {
        return result;
    }
    
    // TODO: Implementar amostragem real da tabela
    return result;
}

// ============================================
// StatisticsManager Implementation
// ============================================

StatisticsManager::StatisticsManager(Catalog* catalog)
    : catalog_(catalog)
    , auto_analyze_(true)
    , analyze_threshold_(10000) {
    
    load();
    last_auto_analyze_ = std::chrono::steady_clock::now();
}

StatisticsManager::~StatisticsManager() {
    save();
}

Status StatisticsManager::analyzeTable(const std::string& table_name, size_t sample_size) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto* table_schema = catalog_->getTable(table_name);
    if (!table_schema) {
        return Status::NOT_FOUND;
    }
    
    // Coletar amostra
    auto sample = collectSample(table_name, sample_size);
    
    // Criar ou atualizar estatísticas
    TableStatistics stats;
    stats.table_name = table_name;
    stats.table_id = table_schema->id;
    
    // Inicializar colunas
    for (const auto& col : table_schema->columns) {
        ColumnStatistics col_stats;
        col_stats.column_name = col.name;
        col_stats.type = col.type;
        stats.columns[col.name] = col_stats;
    }
    
    // Atualizar com a amostra
    stats.update(sample);
    
    // Estimar número de páginas
    stats.page_count = stats.row_count * stats.avg_row_size / PAGE_SIZE + 1;
    
    statistics_[table_name] = stats;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    stats_.tables_analyzed++;
    stats_.total_samples += sample.size();
    stats_.avg_analyze_time_ms = (stats_.avg_analyze_time_ms * 
        (stats_.tables_analyzed - 1) + elapsed) / stats_.tables_analyzed;
    
    return Status::OK;
}

Status StatisticsManager::analyzeDatabase() {
    auto tables = catalog_->listTables();
    
    for (const auto& table : tables) {
        analyzeTable(table);
    }
    
    return Status::OK;
}

const TableStatistics* StatisticsManager::getTableStats(const std::string& table_name) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = statistics_.find(table_name);
    if (it != statistics_.end()) {
        return &it->second;
    }
    
    return nullptr;
}

const ColumnStatistics* StatisticsManager::getColumnStats(const std::string& table_name,
                                                          const std::string& column_name) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto table_it = statistics_.find(table_name);
    if (table_it != statistics_.end()) {
        auto col_it = table_it->second.columns.find(column_name);
        if (col_it != table_it->second.columns.end()) {
            return &col_it->second;
        }
    }
    
    return nullptr;
}

double StatisticsManager::estimateTableSize(const std::string& table_name) const {
    auto stats = getTableStats(table_name);
    if (stats) {
        return stats->page_count * PAGE_SIZE;
    }
    
    // Estimativa padrão
    return 10 * PAGE_SIZE;
}

size_t StatisticsManager::estimateRowCount(const std::string& table_name) const {
    auto stats = getTableStats(table_name);
    if (stats) {
        return stats->row_count;
    }
    
    return 1000; // Estimativa padrão
}

void StatisticsManager::updateAfterInsert(const std::string& table_name,
                                           const std::vector<Value>& row) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto it = statistics_.find(table_name);
    if (it != statistics_.end()) {
        auto& stats = it->second;
        stats.row_count++;
        
        // Atualização incremental aproximada
        // Em produção, seria melhor invalidar e re-analisar periodicamente
    }
}

void StatisticsManager::updateAfterDelete(const std::string& table_name,
                                           const std::vector<Value>& row) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto it = statistics_.find(table_name);
    if (it != statistics_.end()) {
        auto& stats = it->second;
        if (stats.row_count > 0) {
            stats.row_count--;
        }
    }
}

void StatisticsManager::updateAfterUpdate(const std::string& table_name,
                                           const std::vector<Value>& old_row,
                                           const std::vector<Value>& new_row) {
    // Updates não afetam contagem de linhas
}

Status StatisticsManager::load() {
    std::ifstream file(std::string(SYSTEM_DIR) + "/statistics.json");
    if (!file.is_open()) {
        return Status::NOT_FOUND;
    }
    
    using json = nlohmann::json;
    json j;
    file >> j;
    
    for (const auto& [table_name, table_json] : j.items()) {
        TableStatistics stats;
        stats.table_name = table_name;
        stats.table_id = table_json["table_id"];
        stats.row_count = table_json["row_count"];
        stats.page_count = table_json["page_count"];
        stats.avg_row_size = table_json["avg_row_size"];
        
        // Carregar colunas
        for (const auto& [col_name, col_json] : table_json["columns"].items()) {
            ColumnStatistics col_stats;
            col_stats.column_name = col_name;
            col_stats.type = static_cast<DataType>(col_json["type"]);
            col_stats.distinct_values = col_json["distinct_values"];
            col_stats.null_count = col_json["null_count"];
            col_stats.avg_length = col_json["avg_length"];
            
            // TODO: Carregar min/max
            
            stats.columns[col_name] = col_stats;
        }
        
        statistics_[table_name] = stats;
    }
    
    return Status::OK;
}

Status StatisticsManager::save() {
    using json = nlohmann::json;
    json j;
    
    for (const auto& [table_name, stats] : statistics_) {
        json table_json;
        table_json["table_id"] = stats.table_id;
        table_json["row_count"] = stats.row_count;
        table_json["page_count"] = stats.page_count;
        table_json["avg_row_size"] = stats.avg_row_size;
        
        table_json["columns"] = json::object();
        for (const auto& [col_name, col_stats] : stats.columns) {
            json col_json;
            col_json["type"] = static_cast<int>(col_stats.type);
            col_json["distinct_values"] = col_stats.distinct_values;
            col_json["null_count"] = col_stats.null_count;
            col_json["avg_length"] = col_stats.avg_length;
            
            // TODO: Salvar min/max
            
            table_json["columns"][col_name] = col_json;
        }
        
        j[table_name] = table_json;
    }
    
    std::ofstream file(std::string(SYSTEM_DIR) + "/statistics.json");
    file << j.dump(4);
    
    return Status::OK;
}

std::vector<std::vector<Value>> StatisticsManager::collectSample(const std::string& table_name,
                                                                  size_t sample_size) {
    std::vector<std::vector<Value>> sample;
    
    // TODO: Implementar amostragem aleatória da tabela
    // Por enquanto, retorna amostra simulada
    
    auto* schema = catalog_->getTable(table_name);
    if (!schema) {
        return sample;
    }
    
    // Gerar dados simulados para exemplo
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 1000);
    
    for (size_t i = 0; i < sample_size; i++) {
        std::vector<Value> row;
        for (const auto& col : schema->columns) {
            if (col.type == DataType::INTEGER) {
                row.emplace_back(dis(gen));
            } else if (col.type == DataType::VARCHAR) {
                row.emplace_back("value_" + std::to_string(i));
            } else {
                row.emplace_back(dis(gen) % 2 == 0);
            }
        }
        sample.push_back(row);
    }
    
    return sample;
}

std::unique_ptr<Histogram> StatisticsManager::buildHistogram(const std::vector<Value>& sample,
                                                             const std::string& column,
                                                             size_t num_buckets) {
    if (sample.empty()) {
        return nullptr;
    }
    
    auto hist = std::make_unique<Histogram>();
    hist->column_name = column;
    hist->total_count = sample.size();
    
    // Ordenar valores
    std::vector<Value> sorted = sample;
    std::sort(sorted.begin(), sorted.end(),
              [](const Value& a, const Value& b) {
                  return compareValuesOrdered(a, b) < 0;
              });
    
    hist->min_value = sorted.front();
    hist->max_value = sorted.back();
    
    // Contar valores distintos
    std::set<Value> distinct;
    for (const auto& val : sorted) {
        distinct.insert(val);
    }
    hist->distinct_count = distinct.size();
    
    // Criar buckets com equal-depth
    size_t bucket_size = sample.size() / num_buckets;
    if (bucket_size == 0) {
        bucket_size = 1;
    }
    
    size_t pos = 0;
    while (pos < sorted.size()) {
        Histogram::Bucket bucket;
        bucket.min = sorted[pos];
        
        size_t end = std::min(pos + bucket_size, sorted.size());
        bucket.max = sorted[end - 1];
        bucket.count = end - pos;
        
        // Calcular distinct ratio no bucket
        std::set<Value> bucket_distinct;
        for (size_t i = pos; i < end; i++) {
            bucket_distinct.insert(sorted[i]);
        }
        bucket.distinct_ratio = static_cast<double>(bucket_distinct.size()) / bucket.count;
        
        hist->buckets.push_back(bucket);
        pos = end;
    }
    
    return hist;
}

double StatisticsManager::calculateCorrelation(const std::vector<Value>& col1,
                                                const std::vector<Value>& col2) {
    if (col1.size() != col2.size() || col1.empty()) {
        return 0;
    }
    
    // Converter para ranks para correlação de Spearman
    std::vector<double> ranks1(col1.size());
    std::vector<double> ranks2(col2.size());
    
    // Calcular ranks
    std::vector<size_t> idx1(col1.size());
    std::iota(idx1.begin(), idx1.end(), 0);
    std::sort(idx1.begin(), idx1.end(),
              [&col1](size_t a, size_t b) {
                  return compareValuesOrdered(col1[a], col1[b]) < 0;
              });
    
    for (size_t i = 0; i < idx1.size(); i++) {
        ranks1[idx1[i]] = i + 1;
    }
    
    std::vector<size_t> idx2(col2.size());
    std::iota(idx2.begin(), idx2.end(), 0);
    std::sort(idx2.begin(), idx2.end(),
              [&col2](size_t a, size_t b) {
                  return compareValuesOrdered(col2[a], col2[b]) < 0;
              });
    
    for (size_t i = 0; i < idx2.size(); i++) {
        ranks2[idx2[i]] = i + 1;
    }
    
    // Calcular correlação de Spearman
    double mean1 = (col1.size() + 1) / 2.0;
    double mean2 = mean1;
    
    double cov = 0;
    double var1 = 0;
    double var2 = 0;
    
    for (size_t i = 0; i < col1.size(); i++) {
        double diff1 = ranks1[i] - mean1;
        double diff2 = ranks2[i] - mean2;
        cov += diff1 * diff2;
        var1 += diff1 * diff1;
        var2 += diff2 * diff2;
    }
    
    if (var1 == 0 || var2 == 0) {
        return 0;
    }
    
    return cov / std::sqrt(var1 * var2);
}

bool StatisticsManager::needsAnalyze(const std::string& table_name) const {
    auto it = statistics_.find(table_name);
    if (it == statistics_.end()) {
        return true;
    }
    
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::hours>
                   (now - it->second.last_analyzed).count();
    
    // Re-analisar se passou mais de 24 horas
    return elapsed > 24;
}

void StatisticsManager::expireOldStats() {
    auto now = std::chrono::system_clock::now();
    
    for (auto it = statistics_.begin(); it != statistics_.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::days>
                       (now - it->second.last_analyzed).count();
        
        if (elapsed > 30) { // 30 dias
            it = statistics_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace orangesql