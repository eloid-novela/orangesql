orangesql/
│
├── cli/
│   ├── main.cpp
│   ├── console.cpp
│   └── console.h
│
├── parser/
│   ├── sql_parser.cpp
│   ├── sql_parser.h
│   ├── ast.h
│   └── lexer.cpp
│
├── engine/
│   ├── query_executor.cpp
│   ├── query_executor.h
│   ├── optimizer.cpp
│   ├── optimizer.h
│   └── executor_context.h
│
├── storage/
│   ├── file_manager.cpp
│   ├── file_manager.h
│   ├── page.cpp
│   ├── page.h
│   ├── buffer_pool.cpp
│   ├── buffer_pool.h
│   ├── table.cpp
│   ├── table.h
│   └── record.cpp
│
├── index/
│   ├── btree.cpp
│   ├── btree.h
│   ├── btree_node.cpp
│   ├── btree_node.h
│   └── index_manager.cpp
│
├── transaction/
│   ├── transaction_manager.cpp
│   ├── transaction_manager.h
│   ├── log_manager.cpp
│   ├── log_manager.h
│   ├── lock_manager.cpp
│   ├── lock_manager.h
│   ├── wal.cpp
│   └── checkpoint.cpp
│
├── metadata/
│   ├── catalog.cpp
│   ├── catalog.h
│   ├── schema.cpp
│   └── statistics.cpp
│
├── data/              # Diretório de dados persistidos
│   ├── system/        # Catálogo do sistema
│   ├── wal/           # Write-Ahead Logs
│   └── tables/        # Dados das tabelas
│
├── include/
│   ├── types.h
│   ├── constants.h
│   ├── status.h
│   └── utils.h
│
├── CMakeLists.txt
└── README.md