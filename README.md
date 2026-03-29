## 🍊 OrangeSQL

[![CI](https://github.com/Saraiva-coder/orangesql/actions/workflows/ci.yml/badge.svg)](https://github.com/Saraiva-coder/orangesql/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

OrangeSQL is a lightweight, ACID-compliant SQL database engine written in modern C++17.

It features:
- B-Tree indexing
- Write-Ahead Logging (WAL)
- Transaction management (ACID)
- Modular architecture (parser, engine, storage)

---

# 🚀 Features

- ✅ ACID Transactions (Atomicity, Consistency, Isolation, Durability)
- 🌳 B-Tree indexing for fast queries
- 🧠 SQL Parser (SELECT, INSERT, UPDATE, DELETE)
- 💾 Storage engine with buffer pool
- 🔐 Transaction system with locking
- ⚡ High-performance C++17 implementation
- 🧩 Modular architecture
- 🖥️ Cross-platform (Windows / Linux / macOS)
- 📦 Embeddable database engine

---

# 📦 Requirements

- CMake 3.20+
- C++17 compiler (GCC, Clang, MSVC)
- Git

---

# ⚙️ Build Instructions

## 🐧 Linux / macOS

```bash
git clone https://github.com/Saraiva-coder/orangesql.git
cd orangesql

mkdir build && cd build
cmake ..
make -j4

./bin/orangesql

## Windows
scripts\build_with_vcpkg.ps1

## Docker
docker build -t orangesql -f docker/Dockerfile .
docker run -it --rm orangesql

## Usage Example
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name VARCHAR(100),
    age INTEGER
);

INSERT INTO users VALUES (1, 'João', 30);
INSERT INTO users VALUES (2, 'Maria', 25);

SELECT * FROM users WHERE age > 20;

BEGIN TRANSACTION;
UPDATE users SET age = 31 WHERE id = 1;
COMMIT;

## ⚙️ Configuration

## Edit orangesql.conf:

[memory]
buffer_pool_size = 1024

[transaction]
isolation_level = REPEATABLE_READ

[logging]
log_level = INFO
