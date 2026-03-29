#include "console.h"
#include <iostream>
#include <memory>
#include <cstdlib>

// Forward declarations
namespace orangesql {
    class OrangeSQLInstance;
}

// Classe principal da aplicação
class OrangeSQLApplication {
public:
    OrangeSQLApplication() = default;
    ~OrangeSQLApplication() = default;
    
    bool initialize(int argc, char** argv) {
        // Processar argumentos da linha de comando
        if (!parseArguments(argc, argv)) {
            return false;
        }
        
        // Inicializar console
        console_ = std::make_unique<orangesql::Console>();
        
        if (!console_->initialize()) {
            std::cerr << "Erro ao inicializar console" << std::endl;
            return false;
        }
        
        // Aplicar configurações
        if (verbose_) console_->setVerbose(true);
        if (no_pager_) console_->setPager(false);
        if (no_timing_) console_->setTiming(false);
        
        return true;
    }
    
    int run() {
        if (!console_) {
            return 1;
        }
        
        // Se tiver query direta, executar e sair
        if (!direct_query_.empty()) {
            console_->executeQuery(direct_query_);
            return 0;
        }
        
        // Modo interativo
        console_->run();
        
        return 0;
    }
    
    void shutdown() {
        if (console_) {
            console_->shutdown();
        }
    }
    
private:
    bool parseArguments(int argc, char** argv) {
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            
            if (arg == "-h" || arg == "--help") {
                printHelp();
                return false;
            }
            else if (arg == "-v" || arg == "--version") {
                printVersion();
                return false;
            }
            else if (arg == "-V" || arg == "--verbose") {
                verbose_ = true;
            }
            else if (arg == "--no-pager") {
                no_pager_ = true;
            }
            else if (arg == "--no-timing") {
                no_timing_ = true;
            }
            else if (arg == "-c" || arg == "--command") {
                if (i + 1 < argc) {
                    direct_query_ = argv[++i];
                }
            }
            else if (arg == "-f" || arg == "--file") {
                if (i + 1 < argc) {
                    script_file_ = argv[++i];
                    // TODO: Implementar execução de script
                }
            }
            else {
                std::cerr << "Argumento desconhecido: " << arg << std::endl;
                return false;
            }
        }
        
        return true;
    }
    
    void printHelp() {
        std::cout << "OrangeSQL - Banco de Dados ACID com B-Tree\n"
                  << "\n"
                  << "Uso: orangesql [OPÇÕES]\n"
                  << "\n"
                  << "Opções:\n"
                  << "  -h, --help        Mostra esta mensagem de ajuda\n"
                  << "  -v, --version     Mostra a versão\n"
                  << "  -V, --verbose     Modo verboso\n"
                  << "  --no-pager        Desativa pager para resultados longos\n"
                  << "  --no-timing       Desativa exibição do tempo de execução\n"
                  << "  -c, --command SQL Executa comando SQL e sai\n"
                  << "  -f, --file ARQ    Executa comandos do arquivo e sai\n"
                  << "\n"
                  << "Exemplos:\n"
                  << "  orangesql\n"
                  << "  orangesql -c \"SELECT * FROM usuarios;\"\n"
                  << "  orangesql -f script.sql\n"
                  << std::endl;
    }
    
    void printVersion() {
        std::cout << "OrangeSQL versão 1.0.0\n"
                  << "Compilado com C++17\n"
                  << "Copyright (c) 2024 OrangeSQL\n"
                  << std::endl;
    }
    
private:
    std::unique_ptr<orangesql::Console> console_;
    
    // Configurações
    bool verbose_ = false;
    bool no_pager_ = false;
    bool no_timing_ = false;
    
    // Modos de execução
    std::string direct_query_;
    std::string script_file_;
};

// Ponto de entrada principal
int main(int argc, char** argv) {
    OrangeSQLApplication app;
    
    try {
        if (!app.initialize(argc, argv)) {
            return 1;
        }
        
        int result = app.run();
        
        app.shutdown();
        
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "Erro fatal: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Erro fatal desconhecido" << std::endl;
        return 1;
    }
}