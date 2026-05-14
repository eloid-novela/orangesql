# OrangeSQL

**A high-performance, ACID-compliant SQL database engine written in modern C++17.**

Built from scratch with production-ready performance and a clean, modular architecture.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)]()
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Code Size](https://img.shields.io/github/languages/code-size/orangesql/orangesql)]()
[![Stars](https://img.shields.io/github/stars/orangesql/orangesql?style=social)]()

---

## What is OrangeSQL?

OrangeSQL is a **relational database engine** developed entirely from the ground up in C++17. It implements fundamental database concepts — from storage management and transaction control to query optimization and crash recovery — without relying on third-party database libraries.

The goal is to deliver a solid, educational, and extensible SQL engine that balances **correctness**, **performance**, and **clean code**.

### Key Strengths
- ⚡ **High Performance** — Optimized for modern hardware with efficient memory management
- **ACID Compliant** — Full transaction support with MVCC and WAL
- **Lightweight** — Minimal dependencies, small footprint
- **Extensible** — Clean modular architecture for easy customization
- **Cross-Platform** — Runs on Windows, Linux, and macOS

---

## Features

### Core Engine
| Feature | Description |
|:--------|:------------|
| **ACID Transactions** | Full atomicity, consistency, isolation, and durability |
| **MVCC** | Multi-Version Concurrency Control for non-blocking reads |
| **WAL** | Write-Ahead Logging for crash safety |
| **Checkpointing** | Periodic checkpoints for fast recovery |
| **Crash Recovery** | Automatic recovery after unexpected shutdowns |

### Storage Layer
| Feature | Description |
|:--------|:------------|
| **Page-based Storage** | 4KB pages with efficient layout |
| **Buffer Pool** | LRU cache with pinning and dirty page tracking |
| **File Manager** | Cross-platform file I/O |
| **Record Management** | Variable-length record storage |
| **Multi-buffer Pools** | Multiple pools for parallel access |

### Indexing
| Feature | Description |
|:--------|:------------|
| **B-Tree Index** | Complete implementation with O(log n) operations |
| **B-Tree Cache** | LRU caching of index nodes |
| **Concurrent B-Tree** | Thread-safe with latch coupling |
| **Bulk Loading** | Optimized batch index construction |
| **Index Statistics** | Performance metrics and optimization hints |

### Query Processing
| Feature | Description |
|:--------|:------------|
| **SQL Parser** | Hand-written recursive descent parser |
| **Lexer** | Token generation with keyword recognition |
| **Query Optimizer** | Cost-based plan selection |
| **Index Scan** | Efficient index-based access paths |
| **Join Processing** | Nested loop joins with optimization |

### Transaction Management
| Feature | Description |
|:--------|:------------|
| **Lock Manager** | Shared/exclusive locks with deadlock detection |
| **Isolation Levels** | READ_UNCOMMITTED, READ_COMMITTED, REPEATABLE_READ, SERIALIZABLE |
| **Transaction Manager** | Transaction state tracking and lifecycle |
| **Log Manager** | Sequential log records with LSN tracking |

### Metadata & Caching
| Feature | Description |
|:--------|:------------|
| **Catalog** | Schema and table metadata management |
| **Statistics** | Table and column statistics for optimization |
| **Schema Management** | Dynamic schema changes and validation |
| **LRU Cache** | Generic LRU with hit rate tracking |
| **Page Cache** | Database page caching with prefetching |

### CLI Interface
| Feature | Description |
|:--------|:------------|
| **Interactive REPL** | Read-eval-print loop with command history |
| **Meta-commands** | Special commands for database management |
| **Formatted Output** | Table-aligned result display |
| **Query Timing** | Execution time measurement |
| **CSV Import/Export** | Data transfer with CSV files |

### Testing
| Feature | Description |
|:--------|:------------|
| **Unit Tests** | Comprehensive coverage with Google Test |
| **Integration Tests** | End-to-end scenario testing |
| **Performance Benchmarks** | B-Tree and bulk load benchmarks |
| **Concurrency Tests** | Multi-threaded access validation |

---

## Roadmap

| Short-term (Next Release) | Medium-term (Future) | Long-term (Roadmap) |
|:--------------------------|:---------------------|:--------------------|
| Hash Joins | Full-Text Search | Distributed Queries |
| Parallel Query Execution | JSON Support | Vectorized Execution |
| Prepared Statements | Materialized Views | Columnar Storage |
| Foreign Keys | Replication | Encryption |
| Query Result Caching | Partitioning | Graph Queries |
| | Stored Procedures | Time-series Extensions |

---

## Requirements

### Compiler & Build Tools
| Tool | Minimum Version | Notes |
|:-----|:----------------|:------|
| **GCC** | 9.0+ | Linux, MinGW |
| **Clang** | 10.0+ | macOS, Linux |
| **MSVC** | 2019+ | Windows |
| **CMake** | 3.15+ | Build system |
| **Git** | 2.20+ | Version control |

### Dependencies
| Library | Version | Purpose | Required |
|:--------|:--------|:--------|:---------|
| **nlohmann/json** | 3.11.0+ | JSON configuration | Yes |
| **Google Test** | 1.11+ | Unit testing | Optional |
| **Google Benchmark** | 1.7+ | Benchmarking | Optional |
| **pthread** | — | Threading (Linux/macOS) | Yes |
| **readline** | 8.0+ | CLI history | Optional |

### System Resources
| Resource | Minimum | Recommended |
|:---------|:--------|:------------|
| **RAM** | 512 MB | 4 GB+ |
| **Disk** | 100 MB | 1 GB+ |
| **CPU** | 1 core | 4+ cores |

---

## Installation

### Ubuntu / Debian
```bash
# Install build tools
sudo apt update
sudo apt install -y build-essential cmake git

# Install dependencies
sudo apt install -y libgtest-dev libbenchmark-dev nlohmann-json3-dev libreadline-dev

# Clone and build
git clone https://github.com/orangesql/orangesql.git
cd orangesql && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel $(nproc)

# Run
./orangesql

macOS (Homebrew)
# Install dependencies
brew install cmake git
brew install googletest google-benchmark nlohmann-json readline

# Clone and build
git clone https://github.com/orangesql/orangesql.git
cd orangesql && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel $(sysctl -n hw.ncpu)

# Run
./orangesql

Windows (MSYS2)
# Step 1: Install MSYS2 from https://www.msys2.org/
# Step 2: Open "MSYS2 UCRT64" terminal

# Update packages
pacman -Syu
# Close and reopen terminal, then:
pacman -Su

# Install build tools
pacman -S --needed base-devel
pacman -S mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-ucrt-x86_64-cmake
pacman -S mingw-w64-ucrt-x86_64-make
pacman -S git

# Install dependencies
pacman -S mingw-w64-ucrt-x86_64-nlohmann-json
pacman -S mingw-w64-ucrt-x86_64-gtest
pacman -S mingw-w64-ucrt-x86_64-readline

# Clone and build
git clone https://github.com/orangesql/orangesql.git
cd orangesql && mkdir build && cd build
cmake .. -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run
./orangesql.exe

Windows (Visual Studio 2022)
# Install vcpkg first
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg && .\bootstrap-vcpkg.bat
.\vcpkg install nlohmann-json gtest benchmark

# Clone and build
git clone https://github.com/orangesql/orangesql.git
cd orangesql && mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release --parallel

# Run
.\Release\orangesql.exe

Docker
# Build Docker image
docker build -t orangesql .

# Run container
docker run -it orangesql


Start the Database
./orangesql
