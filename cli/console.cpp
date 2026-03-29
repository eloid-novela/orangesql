#include "console.h"
#include "../parser/sql_parser.h"
#include "../engine/query_executor.h"
#include "../metadata/catalog.h"
#include "../storage/buffer_pool.h"
#include "../transaction/transaction_manager.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace orangesql {

// Inicialização do singleton para o console
static Console* g_console_instance = nullptr;

// Handler para SIGINT (Ctrl+C)
void sigint_handler(int) {
    if (g_console_instance) {
        std::cout << std::endl;
        g_console_instance->handleSigInt();
    }
}

// ============================================
// IMPLEMENTAÇÃO DO CONSOLE
// ============================================

Console::Console() 
    : running_(false)
    , verbose_(false)
    , use_pager_(true)
    , show_timing_(true)
    , current_db_("default")
    , current_user_("admin")
    , history_max_size_(1000)
    , history_file_(".orangesql_history") {
    
    stats_.queries_executed = 0;
    stats_.commands_executed = 0;
    stats_.total_execution_time = 0;
    stats_.start_time = std::chrono::system_clock::now();
    
    g_console_instance = this;
    signal(SIGINT, sigint_handler);
}

Console::~Console() {
    shutdown();
    g_console_instance = nullptr;
}

bool Console::initialize() {
    // Carregar histórico
    loadHistory();
    
    // Registrar comandos internos
    registerInternalCommands();
    
    // Configurar auto-complete
    setupAutoComplete();
    
    // Atualizar prompt
    updatePrompt();
    
    return true;
}

void Console::run() {
    running_ = true;
    
    printWelcome();
    
#ifdef _WIN32
    // Versão Windows sem readline
    std::string input;
    while (running_) {
        printPrompt();
        std::getline(std::cin, input);
        
        if (std::cin.eof()) {
            std::cout << std::endl;
            break;
        }
        
        if (!input.empty()) {
            executeCommand(input);
        }
    }
#else
    // Versão Unix com readline
    char* line;
    while (running_ && (line = readline(prompt_.c_str())) != nullptr) {
        std::string input(line);
        free(line);
        
        if (!input.empty()) {
            addToHistory(input);
            executeCommand(input);
        }
    }
#endif
    
    std::cout << "Até logo!" << std::endl;
}

void Console::shutdown() {
    if (running_) {
        running_ = false;
        saveHistory();
    }
}

bool Console::executeCommand(const std::string& input) {
    // Ignorar linhas vazias
    if (input.empty()) {
        return true;
    }
    
    // Verificar se é comando especial do console
    if (input[0] == '\\') {
        auto parts = splitCommand(input);
        if (!parts.empty()) {
            std::string cmd = parts[0].substr(1); // Remove o '\'
            parts.erase(parts.begin());
            return handleInternalCommand(cmd, parts);
        }
        return true;
    }
    
    // Verificar se termina com ';'
    std::string query = input;
    if (query.back() != ';') {
        // Modo multi-linha
        std::string line;
        std::cout << "    -> ";
        while (std::getline(std::cin, line)) {
            query += " " + line;
            if (line.back() == ';') {
                break;
            }
            std::cout << "    -> ";
        }
    }
    
    // Executar query SQL
    return executeQuery(query);
}

bool Console::executeQuery(const std::string& query) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // TODO: Integrar com o executor real
        // Por enquanto, simular resultados
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simular processamento
        
        FormattedResult result;
        result.headers = {"id", "nome", "idade"};
        result.rows = {
            {"1", "João", "30"},
            {"2", "Maria", "25"},
            {"3", "Pedro", "35"}
        };
        result.affected_rows = 3;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.execution_time = std::chrono::duration<double>(end_time - start_time).count();
        
        printResult(result);
        
        stats_.queries_executed++;
        stats_.total_execution_time += result.execution_time;
        
        return true;
        
    } catch (const std::exception& e) {
        printError(e.what());
        return false;
    }
}

void Console::addToHistory(const std::string& command) {
    history_.push_back(command);
    if (history_.size() > history_max_size_) {
        history_.erase(history_.begin());
    }
    
#ifndef _WIN32
    add_history(command.c_str());
#endif
}

void Console::loadHistory() {
    std::ifstream file(history_file_);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            history_.push_back(line);
#ifndef _WIN32
            add_history(line.c_str());
#endif
        }
        file.close();
    }
}

void Console::saveHistory() {
    std::ofstream file(history_file_);
    if (file.is_open()) {
        for (const auto& cmd : history_) {
            file << cmd << std::endl;
        }
        file.close();
    }
}

void Console::clearHistory() {
    history_.clear();
#ifndef _WIN32
    clear_history();
#endif
}

void Console::registerInternalCommands() {
    // Comando \help
    commands_["help"] = {
        "help",
        "Mostra esta mensagem de ajuda",
        "\\help [comando]",
        [this](const std::vector<std::string>& args) {
            if (args.empty()) {
                printMessage("Comandos disponíveis:", "info");
                for (const auto& [name, cmd] : commands_) {
                    std::cout << "  \\" << std::left << std::setw(12) << name 
                             << " - " << cmd.description << std::endl;
                }
                std::cout << "\nDigite \\help <comando> para ajuda detalhada." << std::endl;
            } else {
                auto it = commands_.find(args[0]);
                if (it != commands_.end()) {
                    std::cout << "Comando: \\" << it->second.name << std::endl;
                    std::cout << "Descrição: " << it->second.description << std::endl;
                    std::cout << "Uso: " << it->second.usage << std::endl;
                } else {
                    printError("Comando desconhecido: " + args[0]);
                }
            }
            return true;
        }
    };
    
    // Comando \quit
    commands_["quit"] = {
        "quit",
        "Sai do OrangeSQL",
        "\\quit",
        [this](const std::vector<std::string>&) {
            std::cout << "Finalizando OrangeSQL..." << std::endl;
            running_ = false;
            return true;
        }
    };
    
    // Comando \exit (alias para quit)
    commands_["exit"] = commands_["quit"];
    
    // Comando \clear
    commands_["clear"] = {
        "clear",
        "Limpa a tela",
        "\\clear",
        [](const std::vector<std::string>&) {
#ifdef _WIN32
            system("cls");
#else
            system("clear");
#endif
            return true;
        }
    };
    
    // Comando \list
    commands_["list"] = {
        "list",
        "Lista tabelas ou bancos de dados",
        "\\list [tables|databases]",
        [this](const std::vector<std::string>& args) {
            std::string target = args.empty() ? "tables" : args[0];
            
            if (target == "tables") {
                std::vector<std::string> headers = {"Tabela"};
                std::vector<std::vector<std::string>> rows = {
                    {"usuarios"},
                    {"produtos"},
                    {"pedidos"}
                };
                printTable(headers, rows);
            } else if (target == "databases") {
                std::vector<std::string> headers = {"Banco de Dados"};
                std::vector<std::vector<std::string>> rows = {
                    {"default"},
                    {"test"}
                };
                printTable(headers, rows);
            }
            return true;
        }
    };
    
    // Comando \describe
    commands_["describe"] = {
        "describe",
        "Descreve a estrutura de uma tabela",
        "\\describe <tabela>",
        [this](const std::vector<std::string>& args) {
            if (args.empty()) {
                printError("Nome da tabela não especificado");
                return false;
            }
            
            std::vector<std::string> headers = {"Coluna", "Tipo", "Nulo", "PK"};
            std::vector<std::vector<std::string>> rows = {
                {"id", "INTEGER", "NOT NULL", "PK"},
                {"nome", "VARCHAR(100)", "NULL", ""},
                {"idade", "INTEGER", "NULL", ""}
            };
            printTable(headers, rows);
            
            return true;
        }
    };
    
    // Comando \timing
    commands_["timing"] = {
        "timing",
        "Ativa/desativa exibição do tempo de execução",
        "\\timing [on|off]",
        [this](const std::vector<std::string>& args) {
            if (args.empty()) {
                show_timing_ = !show_timing_;
            } else if (args[0] == "on") {
                show_timing_ = true;
            } else if (args[0] == "off") {
                show_timing_ = false;
            }
            
            std::cout << "Timing " << (show_timing_ ? "ativado" : "desativado") << std::endl;
            return true;
        }
    };
    
    // Comando \verbose
    commands_["verbose"] = {
        "verbose",
        "Ativa/desativa modo verboso",
        "\\verbose [on|off]",
        [this](const std::vector<std::string>& args) {
            if (args.empty()) {
                verbose_ = !verbose_;
            } else if (args[0] == "on") {
                verbose_ = true;
            } else if (args[0] == "off") {
                verbose_ = false;
            }
            
            std::cout << "Verbose mode " << (verbose_ ? "ativado" : "desativado") << std::endl;
            return true;
        }
    };
    
    // Comando \pager
    commands_["pager"] = {
        "pager",
        "Ativa/desativa uso de pager para resultados longos",
        "\\pager [on|off]",
        [this](const std::vector<std::string>& args) {
            if (args.empty()) {
                use_pager_ = !use_pager_;
            } else if (args[0] == "on") {
                use_pager_ = true;
            } else if (args[0] == "off") {
                use_pager_ = false;
            }
            
            std::cout << "Pager " << (use_pager_ ? "ativado" : "desativado") << std::endl;
            return true;
        }
    };
    
    // Comando \history
    commands_["history"] = {
        "history",
        "Mostra histórico de comandos",
        "\\history [clear]",
        [this](const std::vector<std::string>& args) {
            if (!args.empty() && args[0] == "clear") {
                clearHistory();
                printMessage("Histórico limpo.");
            } else {
                for (size_t i = 0; i < history_.size(); i++) {
                    std::cout << std::setw(4) << (i + 1) << "  " << history_[i] << std::endl;
                }
            }
            return true;
        }
    };
    
    // Comando \stats
    commands_["stats"] = {
        "stats",
        "Mostra estatísticas da sessão",
        "\\stats",
        [this](const std::vector<std::string>&) {
            auto now = std::chrono::system_clock::now();
            auto uptime = std::chrono::duration<double>(now - stats_.start_time).count();
            
            std::cout << "Estatísticas da Sessão:" << std::endl;
            std::cout << "  Queries executadas: " << stats_.queries_executed << std::endl;
            std::cout << "  Comandos executados: " << stats_.commands_executed << std::endl;
            std::cout << "  Tempo total de execução: " << std::fixed << std::setprecision(2) 
                     << stats_.total_execution_time << "s" << std::endl;
            std::cout << "  Tempo médio por query: " 
                     << (stats_.queries_executed > 0 ? 
                         stats_.total_execution_time / stats_.queries_executed : 0)
                     << "s" << std::endl;
            std::cout << "  Uptime: " << std::fixed << std::setprecision(0) 
                     << uptime << "s" << std::endl;
            
            return true;
        }
    };
    
    // Comando \echo
    commands_["echo"] = {
        "echo",
        "Imprime uma mensagem",
        "\\echo <mensagem>",
        [](const std::vector<std::string>& args) {
            for (const auto& arg : args) {
                std::cout << arg << " ";
            }
            std::cout << std::endl;
            return true;
        }
    };
    
    // Comando \sleep
    commands_["sleep"] = {
        "sleep",
        "Pausa a execução por N segundos",
        "\\sleep <segundos>",
        [](const std::vector<std::string>& args) {
            if (args.empty()) {
                printError("Tempo não especificado");
                return false;
            }
            
            try {
                int seconds = std::stoi(args[0]);
                std::this_thread::sleep_for(std::chrono::seconds(seconds));
            } catch (...) {
                printError("Tempo inválido");
                return false;
            }
            
            return true;
        }
    };
}

bool Console::handleInternalCommand(const std::string& cmd, const std::vector<std::string>& args) {
    auto it = commands_.find(cmd);
    if (it != commands_.end()) {
        stats_.commands_executed++;
        return it->second.handler(args);
    }
    
    printError("Comando desconhecido: \\" + cmd);
    return false;
}

void Console::printWelcome() {
    std::cout << R"(
    ╔═══════════════════════════════════════╗
    ║         OrangeSQL v1.0.0              ║
    ║     Banco de Dados ACID com B-Tree     ║
    ╚═══════════════════════════════════════╝
    )" << std::endl;
    
    std::cout << "Conectado ao banco de dados '" << current_db_ << "' como '" 
              << current_user_ << "'" << std::endl;
    std::cout << "Digite '\\help' para ajuda ou '\\quit' para sair." << std::endl;
    std::cout << std::endl;
}

void Console::printPrompt() {
    updatePrompt();
    std::cout << prompt_ << std::flush;
}

void Console::updatePrompt() {
    std::stringstream ss;
    ss << current_db_ << "=> ";
    prompt_ = ss.str();
}

void Console::printResult(const FormattedResult& result) {
    if (!result.rows.empty()) {
        printTable(result.headers, result.rows);
    }
    
    std::cout << "(" << result.affected_rows << " linhas afetadas)";
    
    if (show_timing_) {
        std::cout << "  [Tempo: " << std::fixed << std::setprecision(3) 
                  << result.execution_time << "s]";
    }
    
    std::cout << std::endl;
}

void Console::printError(const std::string& error) {
    std::cout << "\033[1;31mERRO: " << error << "\033[0m" << std::endl;
}

void Console::printMessage(const std::string& msg, const std::string& type) {
    if (type == "info") {
        std::cout << "\033[1;34mINFO: " << msg << "\033[0m" << std::endl;
    } else if (type == "success") {
        std::cout << "\033[1;32mSUCESSO: " << msg << "\033[0m" << std::endl;
    } else if (type == "warning") {
        std::cout << "\033[1;33mAVISO: " << msg << "\033[0m" << std::endl;
    } else {
        std::cout << msg << std::endl;
    }
}

void Console::printTable(const std::vector<std::string>& headers, 
                         const std::vector<std::vector<std::string>>& rows) {
    if (headers.empty() || rows.empty()) {
        return;
    }
    
    // Calcular largura das colunas
    std::vector<size_t> widths(headers.size());
    for (size_t i = 0; i < headers.size(); i++) {
        widths[i] = headers[i].length();
    }
    
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < headers.size(); i++) {
            widths[i] = std::max(widths[i], row[i].length());
        }
    }
    
    // Imprimir linha superior
    std::cout << "+";
    for (size_t width : widths) {
        std::cout << std::string(width + 2, '-') << "+";
    }
    std::cout << std::endl;
    
    // Imprimir cabeçalhos
    std::cout << "|";
    for (size_t i = 0; i < headers.size(); i++) {
        std::cout << " " << std::left << std::setw(widths[i]) << headers[i] << " |";
    }
    std::cout << std::endl;
    
    // Imprimir linha separadora
    std::cout << "|";
    for (size_t width : widths) {
        std::cout << std::string(width + 2, '-') << "|";
    }
    std::cout << std::endl;
    
    // Imprimir linhas
    for (const auto& row : rows) {
        std::cout << "|";
        for (size_t i = 0; i < row.size() && i < headers.size(); i++) {
            std::cout << " " << std::left << std::setw(widths[i]) << row[i] << " |";
        }
        std::cout << std::endl;
    }
    
    // Imprimir linha inferior
    std::cout << "+";
    for (size_t width : widths) {
        std::cout << std::string(width + 2, '-') << "+";
    }
    std::cout << std::endl;
}

std::vector<std::string> Console::splitCommand(const std::string& input) {
    std::vector<std::string> parts;
    std::stringstream ss(input);
    std::string part;
    
    while (ss >> part) {
        parts.push_back(part);
    }
    
    return parts;
}

bool Console::isSpecialCommand(const std::string& cmd) {
    return !cmd.empty() && cmd[0] == '\\';
}

void Console::handleSigInt() {
    std::cout << "\nPara sair, digite \\quit" << std::endl;
    printPrompt();
    std::cout.flush();
}

void Console::setupAutoComplete() {
#ifndef _WIN32
    // Configurar auto-complete para readline
    rl_attempted_completion_function = [](const char* text, int start, int end) -> char** {
        if (start == 0) {
            // Completar comandos internos
            std::string prefix(text);
            std::vector<std::string> matches;
            
            auto* console = g_console_instance;
            if (console) {
                for (const auto& [name, cmd] : console->commands_) {
                    if (name.find(prefix) == 0) {
                        matches.push_back("\\" + name);
                    }
                }
            }
            
            if (!matches.empty()) {
                char** result = (char**)malloc((matches.size() + 1) * sizeof(char*));
                for (size_t i = 0; i < matches.size(); i++) {
                    result[i] = strdup(matches[i].c_str());
                }
                result[matches.size()] = nullptr;
                return result;
            }
        }
        
        return nullptr;
    };
#endif
}

std::string Console::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// ============================================
// IMPLEMENTAÇÃO DO PAGER
// ============================================

Pager::Pager() : pager_process_(nullptr) {
#ifdef _WIN32
    pager_command_ = "more";
#else
    // Detectar pager disponível
    if (system("which less > /dev/null 2>&1") == 0) {
        pager_command_ = "less -R";
    } else if (system("which more > /dev/null 2>&1") == 0) {
        pager_command_ = "more";
    }
#endif
}

Pager::~Pager() {
    close();
}

void Pager::open() {
    if (!pager_command_.empty()) {
#ifdef _WIN32
        pager_process_ = _popen(pager_command_.c_str(), "w");
#else
        pager_process_ = popen(pager_command_.c_str(), "w");
#endif
    }
}

void Pager::write(const std::string& text) {
    if (pager_process_) {
        fputs(text.c_str(), pager_process_);
    } else {
        std::cout << text;
    }
}

void Pager::close() {
    if (pager_process_) {
#ifdef _WIN32
        _pclose(pager_process_);
#else
        pclose(pager_process_);
#endif
        pager_process_ = nullptr;
    }
}

// ============================================
// IMPLEMENTAÇÃO DO SYNTAX HIGHLIGHTER
// ============================================

std::map<SyntaxHighlighter::TokenType, std::string> SyntaxHighlighter::colors_ = {
    {TokenType::KEYWORD, "\033[1;34m"},    // Azul
    {TokenType::STRING, "\033[1;32m"},      // Verde
    {TokenType::NUMBER, "\033[1;35m"},      // Magenta
    {TokenType::COMMENT, "\033[1;30m"},     // Cinza
    {TokenType::FUNCTION, "\033[1;33m"},    // Amarelo
    {TokenType::OPERATOR, "\033[1;31m"},    // Vermelho
    {TokenType::IDENTIFIER, "\033[0m"}      // Normal
};

std::vector<std::string> SyntaxHighlighter::keywords_ = {
    "SELECT", "INSERT", "UPDATE", "DELETE", "CREATE", "DROP", "ALTER",
    "TABLE", "INDEX", "VIEW", "TRIGGER", "FUNCTION", "PROCEDURE",
    "WHERE", "FROM", "JOIN", "INNER", "LEFT", "RIGHT", "OUTER",
    "ON", "AND", "OR", "NOT", "NULL", "IS", "IN", "BETWEEN", "LIKE",
    "ORDER", "BY", "GROUP", "HAVING", "LIMIT", "OFFSET",
    "UNION", "ALL", "DISTINCT", "AS", "CASE", "WHEN", "THEN", "ELSE",
    "END", "EXISTS", "ANY", "SOME", "ALL", "WITH", "RECURSIVE",
    "PRIMARY", "FOREIGN", "KEY", "REFERENCES", "CONSTRAINT",
    "DEFAULT", "AUTO_INCREMENT", "UNIQUE", "CHECK",
    "BEGIN", "COMMIT", "ROLLBACK", "TRANSACTION", "SAVEPOINT",
    "GRANT", "REVOKE", "USER", "ROLE"
};

std::vector<std::string> SyntaxHighlighter::functions_ = {
    "COUNT", "SUM", "AVG", "MIN", "MAX",
    "UPPER", "LOWER", "LENGTH", "SUBSTR", "TRIM",
    "NOW", "DATE", "TIME", "YEAR", "MONTH", "DAY",
    "CONCAT", "COALESCE", "NULLIF", "CAST"
};

std::string SyntaxHighlighter::highlight(const std::string& sql) {
    std::string result;
    std::string token;
    bool in_string = false;
    bool in_comment = false;
    
    for (size_t i = 0; i < sql.length(); i++) {
        char c = sql[i];
        
        // String literals
        if (c == '\'' && !in_comment) {
            if (!result.empty() && result.back() != '\\') {
                if (!in_string) {
                    result += colors_[TokenType::STRING];
                } else {
                    result += "\033[0m";
                }
                result += c;
                in_string = !in_string;
                continue;
            }
        }
        
        if (in_string || in_comment) {
            result += c;
            continue;
        }
        
        // Comments
        if (c == '-' && i + 1 < sql.length() && sql[i + 1] == '-') {
            in_comment = true;
            result += colors_[TokenType::COMMENT];
            result += "--";
            i++;
            continue;
        }
        
        // Identifiers and keywords
        if (isalnum(c) || c == '_') {
            token += c;
        } else {
            if (!token.empty()) {
                // Check if token is keyword
                std::string upper_token = token;
                std::transform(upper_token.begin(), upper_token.end(), 
                              upper_token.begin(), ::toupper);
                
                if (std::find(keywords_.begin(), keywords_.end(), upper_token) != keywords_.end()) {
                    result += colors_[TokenType::KEYWORD] + token + "\033[0m";
                } 
                else if (std::find(functions_.begin(), functions_.end(), upper_token) != functions_.end()) {
                    result += colors_[TokenType::FUNCTION] + token + "\033[0m";
                }
                else if (isdigit(token[0])) {
                    result += colors_[TokenType::NUMBER] + token + "\033[0m";
                }
                else {
                    result += token;
                }
                token.clear();
            }
            
            // Operators
            if (c == '=' || c == '<' || c == '>' || c == '!' || 
                c == '+' || c == '-' || c == '*' || c == '/') {
                result += colors_[TokenType::OPERATOR] + std::string(1, c) + "\033[0m";
            } else {
                result += c;
            }
        }
    }
    
    // Handle last token
    if (!token.empty()) {
        result += token;
    }
    
    if (in_comment) {
        result += "\033[0m";
    }
    
    return result;
}

std::string SyntaxHighlighter::getColorCode(TokenType type) {
    auto it = colors_.find(type);
    return it != colors_.end() ? it->second : "";
}

void SyntaxHighlighter::setColors(const std::map<TokenType, std::string>& colors) {
    for (const auto& [type, color] : colors) {
        colors_[type] = color;
    }
}

} // namespace orangesql