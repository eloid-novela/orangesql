#ifndef ORANGESQL_CONSOLE_H
#define ORANGESQL_CONSOLE_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>

namespace orangesql {

// Estrutura para comando do console
struct ConsoleCommand {
    std::string name;
    std::string description;
    std::string usage;
    std::function<bool(const std::vector<std::string>&)> handler;
};

// Estrutura para resultado formatado
struct FormattedResult {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    size_t affected_rows;
    double execution_time;
};

// Classe principal do console
class Console {
public:
    Console();
    ~Console();

    // Inicialização e execução
    bool initialize();
    void run();
    void shutdown();

    // Processamento de comandos
    bool executeCommand(const std::string& input);
    bool executeQuery(const std::string& query);

    // Gerenciamento de histórico
    void addToHistory(const std::string& command);
    void loadHistory();
    void saveHistory();
    void clearHistory();

    // Comandos internos
    void registerInternalCommands();
    bool handleInternalCommand(const std::string& cmd, const std::vector<std::string>& args);

    // Formatação de output
    void printWelcome();
    void printPrompt();
    void printResult(const FormattedResult& result);
    void printError(const std::string& error);
    void printTable(const std::vector<std::string>& headers, 
                    const std::vector<std::vector<std::string>>& rows);
    void printMessage(const std::string& msg, const std::string& type = "info");

    // Auto-complete
    std::vector<std::string> getCompletions(const std::string& partial);
    void setupAutoComplete();

    // Configurações
    void setVerbose(bool verbose) { verbose_ = verbose; }
    void setPager(bool use_pager) { use_pager_ = use_pager; }
    void setTiming(bool show_timing) { show_timing_ = show_timing; }

    // Getters
    bool isRunning() const { return running_; }
    std::string getCurrentDatabase() const { return current_db_; }

private:
    // Estado do console
    bool running_;
    bool verbose_;
    bool use_pager_;
    bool show_timing_;
    std::string current_db_;
    std::string current_user_;
    std::string prompt_;

    // Histórico
    std::vector<std::string> history_;
    size_t history_max_size_;
    std::string history_file_;

    // Comandos registrados
    std::map<std::string, ConsoleCommand> commands_;

    // Estatísticas
    struct Statistics {
        size_t queries_executed;
        size_t commands_executed;
        double total_execution_time;
        std::chrono::system_clock::time_point start_time;
    } stats_;

    // Métodos auxiliares
    void updatePrompt();
    std::string getCurrentTime();
    std::vector<std::string> splitCommand(const std::string& input);
    bool isSpecialCommand(const std::string& cmd);
    void handleSigInt();
};

// Classe para pager (less/more)
class Pager {
public:
    Pager();
    ~Pager();
    void open();
    void write(const std::string& text);
    void close();
    bool isActive() const { return pager_process_ != nullptr; }

private:
    FILE* pager_process_;
    std::string pager_command_;
};

// Classe para syntax highlighting
class SyntaxHighlighter {
public:
    enum class TokenType {
        KEYWORD,
        STRING,
        NUMBER,
        COMMENT,
        FUNCTION,
        OPERATOR,
        IDENTIFIER
    };

    static std::string highlight(const std::string& sql);
    static std::string getColorCode(TokenType type);
    static void setColors(const std::map<TokenType, std::string>& colors);

private:
    static std::map<TokenType, std::string> colors_;
    static std::vector<std::string> keywords_;
    static std::vector<std::string> functions_;
};

} // namespace orangesql

#endif // ORANGESQL_CONSOLE_H