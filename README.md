# OrangeSQL 🍊

[![CI](https://github.com/orangesql/orangesql/actions/workflows/ci.yml/badge.svg)](https://github.com/orangesql/orangesql/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/orangesql/orangesql/branch/main/graph/badge.svg)](https://codecov.io/gh/orangesql/orangesql)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

OrangeSQL is a lightweight, ACID-compliant SQL database engine written in modern C++17. It features B-Tree indexing, WAL-based transactions, and a modular architecture designed for both embedded use and server deployments.

## ✨ Features

- **Full ACID Compliance** - Transactions with WAL, MVCC, and recovery
- **B-Tree Indexing** - Efficient indexing with range scans
- **SQL Support** - Standard SQL with joins, subqueries, aggregations
- **Multi-Version Concurrency Control** - Serializable isolation level
- **Write-Ahead Logging** - Durable transactions with checkpointing
- **Buffer Pool** - LRU caching with configurable size
- **Modular Architecture** - Clean separation of parser, optimizer, executor, storage
- **Cross-Platform** - Linux, macOS, Windows
- **Embeddable** - Can be linked as a library

## 🚀 Quick Start

### Prerequisites

- CMake 3.20+
- C++17 compiler (GCC 9+, Clang 12+, MSVC 2019+)
- Git (for vcpkg)

### Build with vcpkg (Recommended)

```bash
# Clone the repository
git clone https://github.com/orangesql/orangesql.git
cd orangesql

# Install vcpkg and dependencies
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install nlohmann-json gtest readline

# Build
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
make -j4

# Run
./bin/orangesql






# OrangeSQL 🍊

Banco de dados SQL com suporte ACID, índices B-Tree e persistência em disco.

## Requisitos

- CMake 3.20+
- Compilador C++17 (GCC 9+, Clang 12+, MSVC 2019+)
- Git (para vcpkg)

## Build Rápido

### Linux/macOS

```bash
# Clone o repositório
git clone https://github.com/orangesql/orangesql.git
cd orangesql

# Configure o ambiente
chmod +x scripts/setup_dev_env.sh
./scripts/setup_dev_env.sh

# Compile
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
make -j4

# Execute
./bin/orangesql


mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4


# Clone o repositório
git clone https://github.com/orangesql/orangesql.git
cd orangesql

# Execute o script de build
.\scripts\build_with_vcpkg.ps1

# Execute
.\build\bin\Release\orangesql.exe

# Build da imagem
docker build -t orangesql -f docker/Dockerfile .

# Execute
docker run -it --rm -v $PWD/data:/app/data orangesql


Uso Básico
sql
-- Criar tabela
CREATE TABLE usuarios (
    id INTEGER PRIMARY KEY,
    nome VARCHAR(100),
    idade INTEGER
);

-- Inserir dados
INSERT INTO usuarios VALUES (1, 'João', 30);
INSERT INTO usuarios VALUES (2, 'Maria', 25);

-- Consultar com WHERE
SELECT * FROM usuarios WHERE idade > 20;

-- Criar índice
CREATE INDEX idx_nome ON usuarios(nome);

-- Transação
BEGIN TRANSACTION;
UPDATE usuarios SET idade = 31 WHERE id = 1;
COMMIT;

Com vcpkg (recomendado):
bash
# 1. Clone o repositório
git clone https://github.com/seu/orangesql.git
cd orangesql

# 2. Configure com vcpkg
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[caminho-para-vcpkg]/scripts/buildsystems/vcpkg.cmake

# 3. Compile
cmake --build . --config Release

# 4. Execute
./bin/orangesql

Sem vcpkg (dependências do sistema):
bash
# Ubuntu/Debian
sudo apt-get install libreadline-dev nlohmann-json3-dev libgtest-dev

# macOS
brew install readline nlohmann-json googletest

# Compile
mkdir build && cd build
cmake .. -DUSE_VCPKG=OFF
make


Edite orangesql.conf:

ini
[memory]
buffer_pool_size = 1024    # Número de páginas em cache

[transaction]
isolation_level = REPEATABLE_READ

[logging]
log_level = INFO
Testes
bash
# Rodar todos os testes
cd build
ctest --output-on-failure

# Rodar teste específico
./bin/orangesql_tests --gtest_filter=BTreeTest.*
Benchmark
bash
./bin/orangesql_benchmark


Contribuindo
Fork o projeto

Crie sua feature branch (git checkout -b feature/amazing-feature)

Commit suas mudanças (git commit -m 'Add amazing feature')

Push para a branch (git push origin feature/amazing-feature)

Abra um Pull Request