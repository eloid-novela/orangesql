OrangeSQL Documentation 📚
Table of Contents
Introduction

Getting Started

Architecture Overview

SQL Reference

API Reference

Configuration

Storage Engine

Transaction System

Indexing

Query Optimization

Performance Tuning

Development Guide

Deployment

Troubleshooting

FAQ

Introduction
OrangeSQL is a lightweight, ACID-compliant SQL database engine written in modern C++17. It combines the simplicity of embedded databases with the robustness of enterprise-grade systems.

Key Features
Full ACID Compliance: Transactions with Write-Ahead Logging (WAL) and recovery

B-Tree Indexing: Efficient indexing with range scans and composite keys

SQL Support: Standard SQL with joins, subqueries, aggregations

MVCC: Multi-Version Concurrency Control with serializable isolation

Modular Architecture: Clean separation of components

Cross-Platform: Linux, macOS, Windows

Embeddable: Can be linked as a library or run as a server

Use Cases
Embedded applications

IoT data storage

Local caching layer

Educational purposes

Small to medium web applications

Prototyping and development

Getting Started
Installation
From Source
bash
# Clone repository
git clone https://github.com/orangesql/orangesql.git
cd orangesql

# Build with vcpkg (recommended)
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
make -j4
sudo make install

# Build with system libraries
cmake .. -DUSE_VCPKG=OFF
make -j4
Package Managers
Ubuntu/Debian (coming soon):

bash
sudo apt-add-repository ppa:orangesql/stable
sudo apt-get update
sudo apt-get install orangesql
macOS Homebrew:

bash
brew tap orangesql/orangesql
brew install orangesql
Windows:

bash
# Using vcpkg
vcpkg install orangesql
Docker
bash
docker pull orangesql/orangesql:latest
docker run -it -v /data:/var/lib/orangesql orangesql/orangesql
Quick Start
Interactive CLI
bash
$ orangesql
OrangeSQL v1.0.0 - ACID compliant SQL database
Type '\help' for help, '\quit' to exit

default=> CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name VARCHAR(100),
    email VARCHAR(255) UNIQUE,
    age INTEGER,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

default=> INSERT INTO users (id, name, email, age) 
          VALUES (1, 'Alice', 'alice@example.com', 30);

default=> INSERT INTO users (id, name, email, age) 
          VALUES (2, 'Bob', 'bob@example.com', 25);

default=> SELECT * FROM users WHERE age > 25;
+----+-------+-------------------+-----+
| id | name  | email             | age |
+----+-------+-------------------+-----+
| 1  | Alice | alice@example.com | 30  |
+----+-------+-------------------+-----+
(1 row affected)  [Time: 0.015s]

default=> \quit
Using as a Library
cpp
#include <orangesql/orangesql.h>
#include <iostream>

int main() {
    // Initialize database
    orangesql::OrangeSQL db;
    db.initialize("test.db");
    
    // Execute SQL
    db.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR(100))");
    db.execute("INSERT INTO users VALUES (1, 'Alice')");
    
    // Query with parameters
    auto stmt = db.prepare("SELECT * FROM users WHERE id = ?");
    stmt.bind(1, 1);
    auto results = stmt.execute();
    
    for (const auto& row : results) {
        std::cout << "User: " << row[1].asString() << std::endl;
    }
    
    // Transaction
    auto tx = db.beginTransaction();
    try {
        db.execute("UPDATE users SET name = 'Alicia' WHERE id = 1");
        tx.commit();
    } catch (const std::exception& e) {
        tx.rollback();
    }
    
    return 0;
}
Running as a Server
bash
# Start server
orangesqld --port 5432 --data-dir /var/lib/orangesql

# Connect with client
orangesql -h localhost -p 5432 -d mydb
Architecture Overview
System Architecture
text
┌─────────────────────────────────────────────────────┐
│                    Client Layer                       │
├─────────────────────────────────────────────────────┤
│   CLI    │   Network Server   │   Embedded API       │
├─────────────────────────────────────────────────────┤
│                    SQL Layer                          │
├─────────────────────────────────────────────────────┤
│   Parser  │   Optimizer  │   Executor                │
├─────────────────────────────────────────────────────┤
│                 Transaction Layer                     │
├─────────────────────────────────────────────────────┤
│   MVCC    │   Lock Manager  │   Log Manager          │
├─────────────────────────────────────────────────────┤
│                  Storage Layer                        │
├─────────────────────────────────────────────────────┤
│   Buffer Pool  │   B-Tree  │   Page Manager          │
└─────────────────────────────────────────────────────┘
Component Interaction













Data Flow
Query Parsing: SQL → Abstract Syntax Tree (AST)

Optimization: AST → Logical Plan → Physical Plan

Execution: Volcano-style iterator model

Transaction Management: ACID guarantees via MVCC

Storage: Pages with buffer pool caching

SQL Reference
Data Types
Type	Description	Size	Range
INTEGER	Signed 32-bit integer	4 bytes	-2^31 to 2^31-1
BIGINT	Signed 64-bit integer	8 bytes	-2^63 to 2^63-1
SMALLINT	Signed 16-bit integer	2 bytes	-32,768 to 32,767
VARCHAR(n)	Variable-length string	n + 4 bytes	Up to 65,535 chars
CHAR(n)	Fixed-length string	n bytes	Up to 255 chars
TEXT	Long text	Variable	Up to 1GB
BOOLEAN	True/false	1 byte	TRUE/FALSE
DATE	Calendar date	4 bytes	0001-01-01 to 9999-12-31
TIME	Time of day	4 bytes	00:00:00 to 23:59:59
TIMESTAMP	Date and time	8 bytes	0001-01-01 to 9999-12-31
DECIMAL(p,s)	Fixed-point decimal	Variable	p digits total, s decimal
FLOAT	32-bit floating point	4 bytes	±1.5e-45 to ±3.4e38
DOUBLE	64-bit floating point	8 bytes	±5.0e-324 to ±1.7e308
BLOB	Binary large object	Variable	Up to 1GB
JSON	JSON data	Variable	Up to 1GB
UUID	Universally unique ID	16 bytes	Standard UUID format
Data Definition Language (DDL)
CREATE TABLE
sql
CREATE TABLE [IF NOT EXISTS] table_name (
    column1 datatype [constraints],
    column2 datatype [constraints],
    ...
    [table_constraints]
) [options];

-- Example
CREATE TABLE employees (
    id INTEGER PRIMARY KEY AUTO_INCREMENT,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(255) UNIQUE,
    salary DECIMAL(10,2) CHECK (salary > 0),
    department_id INTEGER,
    hire_date DATE DEFAULT CURRENT_DATE,
    active BOOLEAN DEFAULT TRUE,
    FOREIGN KEY (department_id) REFERENCES departments(id)
);
ALTER TABLE
sql
-- Add column
ALTER TABLE table_name ADD COLUMN column_name datatype [constraints];

-- Drop column
ALTER TABLE table_name DROP COLUMN column_name;

-- Modify column
ALTER TABLE table_name MODIFY COLUMN column_name new_datatype;

-- Rename column
ALTER TABLE table_name RENAME COLUMN old_name TO new_name;

-- Rename table
ALTER TABLE old_name RENAME TO new_name;

-- Add constraint
ALTER TABLE table_name ADD CONSTRAINT constraint_name 
    PRIMARY KEY (column1, column2);

-- Drop constraint
ALTER TABLE table_name DROP CONSTRAINT constraint_name;
DROP TABLE
sql
DROP TABLE [IF EXISTS] table_name [CASCADE | RESTRICT];
CREATE INDEX
sql
CREATE [UNIQUE] INDEX index_name ON table_name (column1, column2, ...);

-- Example
CREATE INDEX idx_emp_name ON employees(name);
CREATE UNIQUE INDEX idx_emp_email ON employees(email);
CREATE INDEX idx_emp_dept_salary ON employees(department_id, salary);
DROP INDEX
sql
DROP INDEX [IF EXISTS] index_name;
Data Manipulation Language (DML)
INSERT
sql
-- Single row
INSERT INTO table_name (column1, column2, ...) 
VALUES (value1, value2, ...);

-- Multiple rows
INSERT INTO table_name (column1, column2, ...) 
VALUES 
    (value1, value2, ...),
    (value3, value4, ...);

-- INSERT FROM SELECT
INSERT INTO table_name (column1, column2, ...)
SELECT column1, column2, ... FROM other_table WHERE condition;

-- Example
INSERT INTO users (name, email, age) 
VALUES ('Alice', 'alice@example.com', 30);
SELECT
sql
-- Basic SELECT
SELECT [DISTINCT] column1, column2, ...
FROM table_name
[WHERE condition]
[GROUP BY column1, column2, ...]
[HAVING condition]
[ORDER BY column1 [ASC|DESC], column2 [ASC|DESC], ...]
[LIMIT n [OFFSET m]];

-- JOINs
SELECT columns
FROM table1
[INNER|LEFT|RIGHT|FULL|CROSS] JOIN table2
ON join_condition
[WHERE condition];

-- Subqueries
SELECT * FROM table1 
WHERE column1 IN (SELECT column2 FROM table2 WHERE condition);

-- Aggregations
SELECT 
    department_id,
    COUNT(*) as emp_count,
    AVG(salary) as avg_salary,
    MIN(salary) as min_salary,
    MAX(salary) as max_salary,
    SUM(salary) as total_salary
FROM employees
GROUP BY department_id
HAVING COUNT(*) > 5;

-- Complex example
SELECT 
    d.name as department,
    e.name as employee,
    e.salary,
    RANK() OVER (PARTITION BY d.id ORDER BY e.salary DESC) as rank
FROM employees e
JOIN departments d ON e.department_id = d.id
WHERE e.active = TRUE
ORDER BY d.name, rank;
UPDATE
sql
UPDATE table_name
SET column1 = value1, column2 = value2, ...
[WHERE condition];

-- Example
UPDATE employees 
SET salary = salary * 1.1, 
    updated_at = CURRENT_TIMESTAMP
WHERE department_id = 3 AND performance_rating > 8;
DELETE
sql
DELETE FROM table_name
[WHERE condition];

-- Example
DELETE FROM logs WHERE created_at < DATE('now', '-30 days');
Transaction Control
sql
-- Start transaction
BEGIN [TRANSACTION];

-- Set savepoint
SAVEPOINT savepoint_name;

-- Rollback to savepoint
ROLLBACK TO SAVEPOINT savepoint_name;

-- Release savepoint
RELEASE SAVEPOINT savepoint_name;

-- Commit transaction
COMMIT [TRANSACTION];

-- Rollback transaction
ROLLBACK [TRANSACTION];

-- Example
BEGIN;
UPDATE accounts SET balance = balance - 100 WHERE id = 1;
UPDATE accounts SET balance = balance + 100 WHERE id = 2;
COMMIT;
Views
sql
-- Create view
CREATE VIEW view_name AS
SELECT columns FROM tables WHERE conditions;

-- Create materialized view
CREATE MATERIALIZED VIEW view_name AS
SELECT columns FROM tables WHERE conditions;

-- Refresh materialized view
REFRESH MATERIALIZED VIEW view_name;

-- Drop view
DROP VIEW [IF EXISTS] view_name;

-- Example
CREATE VIEW active_employees AS
SELECT id, name, department_id 
FROM employees 
WHERE active = TRUE;
Functions and Aggregates
Built-in Functions
String Functions

sql
UPPER(string)      -- Convert to uppercase
LOWER(string)      -- Convert to lowercase
LENGTH(string)     -- String length
SUBSTR(string, start, length) -- Substring
TRIM(string)       -- Remove whitespace
CONCAT(a, b)       -- Concatenate strings
REPLACE(str, from, to) -- Replace substring
Numeric Functions

sql
ABS(x)             -- Absolute value
ROUND(x, d)        -- Round to d decimals
CEIL(x)            -- Ceiling
FLOOR(x)           -- Floor
POWER(x, y)        -- x raised to y
SQRT(x)            -- Square root
RANDOM()           -- Random number
Date/Time Functions

sql
CURRENT_DATE       -- Current date
CURRENT_TIME       -- Current time
CURRENT_TIMESTAMP  -- Current timestamp
DATE(expr)         -- Extract date
TIME(expr)         -- Extract time
YEAR(date)         -- Extract year
MONTH(date)        -- Extract month
DAY(date)          -- Extract day
HOUR(time)         -- Extract hour
MINUTE(time)       -- Extract minute
SECOND(time)       -- Extract second
Aggregate Functions
sql
COUNT(*)           -- Count rows
COUNT(column)      -- Count non-null values
SUM(column)        -- Sum of values
AVG(column)        -- Average of values
MIN(column)        -- Minimum value
MAX(column)        -- Maximum value
GROUP_CONCAT(column) -- Concatenate values
STDDEV(column)     -- Standard deviation
VARIANCE(column)   -- Variance
Constraints
sql
-- Column constraints
NOT NULL           -- Column cannot be NULL
UNIQUE             -- All values must be unique
PRIMARY KEY        -- Unique identifier for row
FOREIGN KEY        -- References another table
CHECK (condition)  -- Values must satisfy condition
DEFAULT value      -- Default value if not specified
AUTO_INCREMENT     -- Automatically incrementing integer

-- Table constraints
PRIMARY KEY (col1, col2)      -- Composite primary key
FOREIGN KEY (col) REFERENCES other(col) -- Foreign key
UNIQUE (col1, col2)            -- Composite unique
CHECK (condition)               -- Table-level check
API Reference
C++ API
OrangeSQL Class
cpp
namespace orangesql {

class OrangeSQL {
public:
    // Construction & Initialization
    OrangeSQL();
    ~OrangeSQL();
    
    Status initialize(const std::string& db_path);
    void shutdown();
    
    // Query execution
    ResultSet execute(const std::string& sql);
    Status execute(const std::string& sql, ResultSet& results);
    
    // Prepared statements
    PreparedStatement prepare(const std::string& sql);
    void unprepare(const std::string& sql);
    
    // Transactions
    Transaction beginTransaction(IsolationLevel level = REPEATABLE_READ);
    
    // Metadata
    std::vector<std::string> listTables();
    TableInfo getTableInfo(const std::string& table_name);
    
    // Configuration
    void setConfig(const Config& config);
    Config getConfig() const;
    
    // Statistics
    Stats getStats() const;
    void resetStats();
    
    // Backup & Restore
    Status backup(const std::string& path);
    Status restore(const std::string& path);
};

} // namespace orangesql
ResultSet Class
cpp
class ResultSet {
public:
    // Iteration
    size_t size() const;
    bool empty() const;
    
    // Row access
    Row& operator[](size_t index);
    const Row& operator[](size_t index) const;
    
    // Iterators
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    
    // Metadata
    const std::vector<std::string>& columns() const;
    size_t affectedRows() const;
    double executionTime() const;
};
Row Class
cpp
class Row {
public:
    // Value access by index
    Value& operator[](size_t index);
    const Value& operator[](size_t index) const;
    
    // Value access by column name
    Value& operator[](const std::string& name);
    const Value& operator[](const std::string& name) const;
    
    // Size
    size_t size() const;
    
    // Metadata
    const std::vector<std::string>& columns() const;
};
Value Class
cpp
class Value {
public:
    // Constructors
    Value();                    // NULL
    Value(int32_t val);         // INTEGER
    Value(int64_t val);         // BIGINT
    Value(double val);          // DOUBLE
    Value(bool val);            // BOOLEAN
    Value(const char* val);     // VARCHAR
    Value(const std::string& val); // VARCHAR
    Value(const std::vector<uint8_t>& val); // BLOB
    
    // Type checking
    DataType type() const;
    bool isNull() const;
    
    // Conversion
    int32_t asInt() const;
    int64_t asLong() const;
    double asDouble() const;
    bool asBool() const;
    std::string asString() const;
    std::vector<uint8_t> asBlob() const;
    
    // Comparison
    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const;
    bool operator<(const Value& other) const;
    bool operator<=(const Value& other) const;
    bool operator>(const Value& other) const;
    bool operator>=(const Value& other) const;
};
PreparedStatement Class
cpp
class PreparedStatement {
public:
    // Binding parameters
    void bind(size_t index, int32_t value);
    void bind(size_t index, int64_t value);
    void bind(size_t index, double value);
    void bind(size_t index, bool value);
    void bind(size_t index, const char* value);
    void bind(size_t index, const std::string& value);
    void bind(size_t index, const std::vector<uint8_t>& value);
    void bindNull(size_t index);
    
    // Clear bindings
    void clearBindings();
    
    // Execute
    ResultSet execute();
    
    // Metadata
    size_t parameterCount() const;
    DataType parameterType(size_t index) const;
};
Transaction Class
cpp
class Transaction {
public:
    // Control
    void commit();
    void rollback();
    
    // Savepoints
    void savepoint(const std::string& name);
    void rollbackToSavepoint(const std::string& name);
    void releaseSavepoint(const std::string& name);
    
    // State
    TransactionId id() const;
    TransactionState state() const;
    IsolationLevel isolationLevel() const;
};
C API
c
#include <orangesql/c_api.h>

// Connection handling
orangesql_t* orangesql_open(const char* path);
void orangesql_close(orangesql_t* db);

// Query execution
orangesql_result_t* orangesql_execute(orangesql_t* db, const char* sql);
void orangesql_free_result(orangesql_result_t* result);

// Result inspection
int orangesql_column_count(orangesql_result_t* result);
int orangesql_row_count(orangesql_result_t* result);
const char* orangesql_column_name(orangesql_result_t* result, int col);
orangesql_value_t orangesql_get_value(orangesql_result_t* result, int row, int col);

// Value handling
int orangesql_value_type(orangesql_value_t value);
int orangesql_value_int(orangesql_value_t value);
long long orangesql_value_long(orangesql_value_t value);
double orangesql_value_double(orangesql_value_t value);
const char* orangesql_value_text(orangesql_value_t value);
int orangesql_value_bytes(orangesql_value_t value);

// Prepared statements
orangesql_stmt_t* orangesql_prepare(orangesql_t* db, const char* sql);
void orangesql_bind_int(orangesql_stmt_t* stmt, int index, int value);
void orangesql_bind_text(orangesql_stmt_t* stmt, int index, const char* value);
orangesql_result_t* orangesql_step(orangesql_stmt_t* stmt);
void orangesql_finalize(orangesql_stmt_t* stmt);
Python API (via bindings)
python
import orangesql

# Connect to database
db = orangesql.connect("test.db")

# Execute query
cursor = db.execute("SELECT * FROM users WHERE age > ?", (25,))
for row in cursor:
    print(row['id'], row['name'], row['age'])

# Transaction
with db.transaction():
    db.execute("UPDATE users SET age = age + 1 WHERE id = 1")

# Prepared statement
stmt = db.prepare("INSERT INTO users (name, age) VALUES (?, ?)")
stmt.execute(["Alice", 30])
stmt.execute(["Bob", 25])

# Close connection
db.close()
Configuration
Configuration File
ini
# /etc/orangesql/orangesql.conf

[server]
port = 5432
host = localhost
max_connections = 100

[memory]
buffer_pool_size = 1024
max_sort_memory = 64
join_buffer_size = 32
temp_table_max_size = 100

[storage]
data_directory = /var/lib/orangesql/data
wal_directory = /var/lib/orangesql/wal
sync_on_write = false

[transaction]
default_isolation = REPEATABLE_READ
lock_timeout_ms = 5000
deadlock_timeout_ms = 1000
transaction_timeout_sec = 300

[logging]
log_level = INFO
log_file = /var/log/orangesql/orangesql.log
log_slow_queries = true
slow_query_threshold_ms = 1000

[checkpoint]
checkpoint_interval_sec = 300
checkpoint_log_size = 50

[optimizer]
optimization_level = 2
index_selectivity_threshold = 0.2
enable_parallel_query = true
parallel_workers = 4
Environment Variables
Variable	Description	Default
ORANGESQL_HOME	Installation directory	/usr/local/orangesql
ORANGESQL_DATA	Data directory	/var/lib/orangesql
ORANGESQL_CONFIG	Config file path	/etc/orangesql/orangesql.conf
ORANGESQL_LOG	Log directory	/var/log/orangesql
ORANGESQL_TEMP	Temp directory	/tmp/orangesql
ORANGESQL_PORT	Server port	5432
ORANGESQL_HOST	Server host	localhost
Runtime Configuration
sql
-- Show configuration
SHOW ALL;
SHOW buffer_pool_size;
SHOW lock_timeout;

-- Set configuration
SET buffer_pool_size = 2048;
SET lock_timeout = 10000;
SET optimization_level = 3;
Storage Engine
Page Structure
Each page is 4KB with the following layout:

text
┌─────────────────────────┐
│      Page Header        │ 64 bytes
├─────────────────────────┤
│         Slots           │ Variable
├─────────────────────────┤
│         Free Space      │
├─────────────────────────┤
│         Records         │
└─────────────────────────┘
Page Header:

cpp
struct PageHeader {
    PageId page_id;
    TableId table_id;
    PageType type;
    uint16_t free_space_offset;
    uint16_t free_space_end;
    uint16_t record_count;
    uint16_t slot_count;
    PageId next_page_id;
    PageId prev_page_id;
    uint32_t checksum;
    uint32_t lsn;
    bool is_dirty;
};
Buffer Pool
The buffer pool manages pages in memory with LRU eviction:

text
┌─────────────────────────────────────┐
│           Buffer Pool                │
├─────────────────────────────────────┤
│ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐   │
│ │Page1│ │Page2│ │Page3│ │Page4│...│
│ └─────┘ └─────┘ └─────┘ └─────┘   │
├─────────────────────────────────────┤
│         LRU List                     │
│  [Page4] <-> [Page2] <-> [Page1]    │
└─────────────────────────────────────┘
Configuration:

Default size: 1024 pages (4MB)

Max size: 1M pages (4GB)

Eviction strategies: LRU, CLOCK, FIFO

File Format
Tables are stored as .dat files with the following structure:

text
[Page 0][Page 1][Page 2]...
Each page is exactly 4KB, allowing direct random access.

Transaction System
ACID Properties
Property	Implementation
Atomicity	Write-Ahead Logging (WAL)
Consistency	Constraints, triggers, MVCC
Isolation	Multi-Version Concurrency Control
Durability	WAL + Checkpoints
Isolation Levels
Level	Dirty Read	Non-repeatable Read	Phantom Read
READ UNCOMMITTED	❌	❌	❌
READ COMMITTED	✅	❌	❌
REPEATABLE READ	✅	✅	❌
SERIALIZABLE	✅	✅	✅
Lock Modes
Mode	Description
SHARED	Read lock
EXCLUSIVE	Write lock
INTENT_SHARED	Intention to read at finer granularity
INTENT_EXCLUSIVE	Intention to write at finer granularity
SHARED_INTENT_EXCLUSIVE	Read with intention to write
Deadlock Detection
OrangeSQL uses a wait-for graph to detect deadlocks:







When a deadlock is detected, the transaction with the lowest priority is aborted.

Write-Ahead Logging
Log record format:

text
┌─────────────────────────────────────┐
│ LSN (8 bytes)                        │
├─────────────────────────────────────┤
│ Record Type (1 byte)                  │
├─────────────────────────────────────┤
│ Transaction ID (8 bytes)              │
├─────────────────────────────────────┤
│ Page ID (8 bytes)                     │
├─────────────────────────────────────┤
│ Data Length (4 bytes)                 │
├─────────────────────────────────────┤
│ Timestamp (8 bytes)                    │
├─────────────────────────────────────┤
│ Prev LSN (8 bytes)                    │
├─────────────────────────────────────┤
│ Data (variable)                        │
└─────────────────────────────────────┘
Recovery Process
Analysis Phase: Find last checkpoint

Redo Phase: Reapply committed transactions

Undo Phase: Rollback uncommitted transactions

Indexing
B-Tree Structure
text
                    [50]
                   /    \
              [20,30]    [70,80]
              /  |  \    /  |  \
             [1,5] [21,25] [51,55] [71,75] [81,85]
Index Types
Type	Description	Use Case
B-Tree	Balanced tree	General purpose, range queries
Hash	Hash table	Equality lookups only
GIN	Generalized Inverted Index	Full-text search, arrays
GiST	Generalized Search Tree	Geometric data, custom types
BRIN	Block Range Index	Very large tables
Creating Indexes
sql
-- Single column
CREATE INDEX idx_name ON users(name);

-- Composite index
CREATE INDEX idx_name_age ON users(name, age);

-- Unique index
CREATE UNIQUE INDEX idx_email ON users(email);

-- Partial index
CREATE INDEX idx_active_users ON users(id) WHERE active = TRUE;

-- Expression index
CREATE INDEX idx_lower_email ON users(LOWER(email));

-- Full-text index
CREATE INDEX idx_description ON articles USING GIN(to_tsvector(description));
Index Usage
sql
-- This query will use idx_name
SELECT * FROM users WHERE name = 'Alice';

-- This query will use idx_name_age
SELECT * FROM users WHERE name = 'Alice' AND age > 25;

-- This query might use idx_name_age for sorting
SELECT * FROM users WHERE name = 'Alice' ORDER BY age;
Index Statistics
sql
-- Show index statistics
ANALYZE;
SELECT * FROM pg_stats WHERE tablename = 'users';

-- Index usage information
SELECT schemaname, tablename, indexname, idx_scan, idx_tup_read
FROM pg_stat_user_indexes;
Query Optimization
Query Planning
The optimizer generates an execution plan:

text
EXPLAIN SELECT * FROM users WHERE age > 25;

Seq Scan on users  (cost=0.00..10.00 rows=100)
  Filter: (age > 25)

EXPLAIN SELECT * FROM users WHERE id = 100;

Index Scan using users_pkey on users  (cost=0.00..8.27 rows=1)
  Index Cond: (id = 100)
Cost Model
Sequential Scan: 10.0 per page + 0.01 per row

Index Scan: 1.0 per page + 0.05 per row

Filter: 0.01 per input row

Join: Varies by algorithm (nested loop, hash, merge)

Join Algorithms
Algorithm	Complexity	Use Case
Nested Loop	O(n*m)	Small tables
Hash Join	O(n+m)	Medium tables, equi-joins
Merge Join	O(n log n + m log m)	Sorted data
Optimization Techniques
Predicate Pushdown: Move filters as early as possible

Projection Pushdown: Select only needed columns

Join Reordering: Optimize join order

Subquery Unnesting: Convert subqueries to joins

Common Subexpression Elimination: Reuse repeated expressions

Query Hints
sql
-- Force index usage
SELECT /*+ INDEX(users idx_name) */ * FROM users WHERE name = 'Alice';

-- Force join order
SELECT /*+ LEADING(users orders) */ * FROM users JOIN orders ON users.id = orders.user_id;

-- Force join method
SELECT /*+ USE_HASH(users orders) */ * FROM users JOIN orders ON users.id = orders.user_id;
Performance Tuning
Buffer Pool Size
The buffer pool size is critical for performance:

sql
-- Set buffer pool size (in pages)
SET buffer_pool_size = 8192;  -- 32MB with 4KB pages

-- Check cache hit ratio
SELECT 
    (buffer_pool_hits * 100.0 / (buffer_pool_hits + buffer_pool_misses)) as hit_ratio
FROM pg_stat_database;
Index Tuning
sql
-- Find unused indexes
SELECT 
    schemaname,
    tablename,
    indexname,
    idx_scan
FROM pg_stat_user_indexes
WHERE idx_scan = 0;

-- Find duplicate indexes
SELECT 
    indrelid::regclass as table,
    array_to_string(indkey, ' ') as columns
FROM pg_index
GROUP BY indrelid, indkey
HAVING COUNT(*) > 1;
Query Optimization Tips
Use appropriate indexes: Create indexes on columns used in WHERE, JOIN, and ORDER BY

**Avoid SELECT ***: Select only needed columns

Use prepared statements: Reduce parsing overhead

Batch operations: Use bulk inserts and updates

Monitor slow queries: Enable slow query logging

Memory Configuration
Parameter	Recommended	Description
buffer_pool_size	25% of RAM	Page cache
max_sort_memory	10% of RAM	Memory for sorting
join_buffer_size	5% of RAM	Memory for hash joins
temp_table_max_size	10% of RAM	Temporary table size
Development Guide
Building from Source
bash
# Clone repository
git clone https://github.com/orangesql/orangesql.git
cd orangesql

# Configure with tests and debug
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_DEBUG=ON

# Build
make -j4

# Run tests
make test

# Run specific test
./bin/orangesql_tests --gtest_filter=BTreeTest.*

# Generate coverage
cmake .. -DENABLE_COVERAGE=ON
make
ctest
make coverage
Code Structure
text
orangesql/
├── cli/           # Command-line interface
├── engine/        # Query execution engine
├── include/       # Public headers
├── index/         # Index implementations
├── metadata/      # Catalog and schema
├── parser/        # SQL parser
├── storage/       # Storage engine
├── transaction/   # Transaction management
├── tests/         # Unit tests
├── benchmarks/    # Performance benchmarks
├── examples/      # Example code
└── docs/          # Documentation
Coding Standards
C++17 with modern style

RAII for resource management

RAII for transaction management

Error handling via Status class

Thread-safe where needed

Documentation for public APIs

Adding a New Feature
Create issue describing the feature

Implement with tests

Update documentation

Submit pull request

Deployment
Production Deployment Checklist
Configure buffer pool size based on RAM

Set up regular backups

Enable monitoring

Configure logging

Set up replication (if needed)

Tune kernel parameters

Set up firewalls

Create service user

Set file permissions

Test recovery procedure

Systemd Service
ini
# /etc/systemd/system/orangesql.service
[Unit]
Description=OrangeSQL Database Server
After=network.target

[Service]
Type=simple
User=orangesql
Group=orangesql
ExecStart=/usr/local/bin/orangesqld --config /etc/orangesql/orangesql.conf
ExecReload=/bin/kill -HUP $MAINPID
Restart=always
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
Docker Deployment
dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    orangesql \
    && rm -rf /var/lib/apt/lists/*

COPY orangesql.conf /etc/orangesql/orangesql.conf
VOLUME /var/lib/orangesql
EXPOSE 5432

USER orangesql
CMD ["orangesqld"]
Backup and Restore
bash
# Full backup
orangesql_backup -f /backup/orangesql_$(date +%Y%m%d).bak

# Incremental backup
orangesql_backup -i -f /backup/orangesql_inc.bak

# Restore
orangesql_restore -f /backup/orangesql.bak

# Point-in-time recovery
orangesql_recover -t "2024-01-15 10:30:00" -f /backup/
Monitoring
sql
-- Database statistics
SELECT * FROM pg_stat_database;

-- Table statistics
SELECT * FROM pg_stat_user_tables;

-- Index statistics
SELECT * FROM pg_stat_user_indexes;

-- Active queries
SELECT * FROM pg_stat_activity;

-- Locks
SELECT * FROM pg_locks;
Troubleshooting
Common Issues
Connection refused
bash
# Check if server is running
ps aux | grep orangesql

# Check port
netstat -tlnp | grep 5432

# Check firewall
iptables -L -n
Slow queries
sql
-- Enable slow query log
SET log_slow_queries = true;
SET slow_query_threshold_ms = 1000;

-- Find slow queries
SELECT * FROM pg_stat_activity WHERE state = 'active' AND now() - query_start > interval '1 second';
Deadlocks
sql
-- Check deadlock statistics
SELECT * FROM pg_stat_database WHERE datname = current_database();

-- Recent deadlocks
SELECT * FROM pg_stat_activity WHERE wait_event = 'deadlock';
Out of memory
sql
-- Check memory settings
SHOW buffer_pool_size;
SHOW max_sort_memory;
SHOW join_buffer_size;

-- Reduce memory usage
SET buffer_pool_size = 512;
Corruption
bash
# Check database integrity
orangesql_check /var/lib/orangesql/data

# Rebuild indexes
orangesql_reindex --all

# Restore from backup
orangesql_restore -f /backup/orangesql.bak
Log Analysis
bash
# View logs
tail -f /var/log/orangesql/orangesql.log

# Filter errors
grep ERROR /var/log/orangesql/orangesql.log

# Slow queries
grep "slow query" /var/log/orangesql/orangesql.log
Debugging
bash
# Run with debug symbols
gdb --args orangesqld --debug

# Enable debug logging
SET debug_mode = true;

# Trace queries
SET log_all_queries = true;
FAQ
General
Q: What is OrangeSQL?
A: OrangeSQL is a lightweight, ACID-compliant SQL database engine written in C++17.

Q: Is it production-ready?
A: Yes, OrangeSQL is designed for production use with proper ACID guarantees and recovery mechanisms.

Q: What platforms are supported?
A: Linux, macOS, and Windows.

Q: What's the maximum database size?
A: Up to 1TB, limited by file system.

Q: Does it support replication?
A: Yes, basic replication is available with configurable replication factor.

Performance
Q: How fast is it?
A: ~10k TPS on commodity hardware with proper configuration.

Q: How much memory does it need?
A: Minimum 64MB, recommended 1GB for production.

Q: Can it handle concurrent connections?
A: Yes, up to 100 concurrent connections.

Q: Does it support parallel queries?
A: Yes, with configurable parallel workers.

Features
Q: What SQL features are supported?
A: Standard SQL with joins, subqueries, aggregations, transactions, views.

Q: Does it support JSON?
A: Yes, JSON data type with basic operators.

Q: Can I create custom functions?
A: Yes, user-defined functions are supported.

Q: Is full-text search available?
A: Yes, via GIN indexes.

Technical
Q: What storage engine does it use?
A: Custom B-Tree based storage with MVCC.

Q: How does it handle concurrency?
A: MVCC with serializable isolation level.

Q: What happens on crash?
A: Automatic recovery using WAL.

Q: Can it be embedded?
A: Yes, can be linked as a static or shared library.

Development
Q: How do I contribute?
A: Fork the repository, make changes, submit pull request.

Q: What's the license?
A: MIT License.

Q: Where can I get help?
A: GitHub Issues, Stack Overflow (tag orangesql), or email support@orangesql.org.

Q: Is there a commercial version?
A: No, OrangeSQL is and will remain open source.

Appendix
System Catalog Tables
Table   Description
pg_tables   Table information
pg_indexes  Index information
pg_views    View definitions
pg_constraints  Constraint definitions
pg_sequences    Sequence information
pg_stat_user_tables Table statistics
pg_stat_user_indexes    Index statistics
pg_stat_activity    Current activity
pg_locks    Lock information
Error Codes
Code    Description
00000   Success
01000   Warning
02000   No data
22000   Data exception
23000   Integrity constraint violation
24000   Invalid cursor state
25000   Invalid transaction state
26000   Invalid SQL statement name
27000   Triggered data change violation
28000   Invalid authorization specification
40000   Transaction rollback
40P01   Deadlock detected
42000   Syntax error
42601   Syntax error
42703   Undefined column
42883   Undefined function
42P01   Undefined table
Keywords
sql
ABORT, ACTION, ADD, AFTER, ALL, ALTER, ANALYZE, AND, ANY, ARRAY, AS, ASC,
BEFORE, BEGIN, BETWEEN, BIGINT, BINARY, BLOB, BOOLEAN, BOTH, BY,
CASCADE, CASE, CAST, CHAR, CHARACTER, CHECK, COLLATE, COLUMN, COMMIT,
CONSTRAINT, CREATE, CROSS, CURRENT_DATE, CURRENT_TIME, CURRENT_TIMESTAMP,
DATABASE, DATE, DECIMAL, DECLARE, DEFAULT, DELETE, DESC, DISTINCT, DOUBLE,
DROP, ELSE, END, ESCAPE, EXCEPT, EXISTS, EXPLAIN, FALSE, FETCH, FLOAT,
FOR, FOREIGN, FROM, FULL, FUNCTION, GRANT, GROUP, HAVING, IF, IN, INDEX,
INNER, INSERT, INT, INTEGER, INTERSECT, INTO, IS, JOIN, KEY, LEFT, LIKE,
LIMIT, MATCH, MATERIALIZED, MAXVALUE, MINVALUE, MODE, NATURAL, NO, NOT,
NULL, NUMERIC, OF, OFF, OFFSET, ON, ONLY, OR, ORDER, OUT, OUTER, OVERLAPS,
PARTIAL, POSITION, PRECISION, PRIMARY, PROCEDURE, RANGE, READ, REAL,
REFERENCES, REINDEX, RENAME, REPEATABLE, REPLACE, RESTRICT, RETURNING,
REVOKE, RIGHT, ROLLBACK, ROW, ROWS, SAVEPOINT, SELECT, SERIALIZABLE,
SESSION, SET, SMALLINT, SOME, TABLE, TEXT, THEN, TIME, TIMESTAMP, TO,
TRANSACTION, TRIGGER, TRUE, TRUNCATE, UNCOMMITTED, UNION, UNIQUE, UPDATE,
USER, USING, UUID, VALUES, VARCHAR, VIEW, WHEN, WHERE, WITH, WRITE
SQLSTATE Codes
Code    Meaning
00000   Successful completion
01000   Warning
02000   No data
03000   SQL statement not yet complete
08000   Connection exception
09000   Triggered action exception
0A000   Feature not supported
0B000   Invalid transaction initiation
0F000   Locator exception
0L000   Invalid grantor
0P000   Invalid role specification
0Z000   Diagnostics exception
20000   Case not found
21000   Cardinality violation
22000   Data exception
23000   Integrity constraint violation
24000   Invalid cursor state
25000   Invalid transaction state
26000   Invalid SQL statement name
27000   Triggered data change violation
28000   Invalid authorization specification
2B000   Dependent privilege descriptors still exist
2D000   Invalid transaction termination
2F000   SQL function exception
34000   Invalid cursor name
38000   External routine exception
39000   External routine invocation exception
3B000   Savepoint exception
40000   Transaction rollback
42000   Syntax error or access rule violation
44000   WITH CHECK OPTION violation
53000   Insufficient resources
54000   Program limit exceeded
55000   Object not in prerequisite state
56000   Operator intervention
57000   System error
58000   IO error
OrangeSQL Documentation v1.0.0
Last updated: March 2024
Copyright © 2024 OrangeSQL Contributors

                    [50]
                   /    \
              [20,30]    [70,80]
              /  |  \    /  |  \
             [1,5] [21,25] [51,55] [71,75] [81,85]


┌─────────────────────────────────────┐
│ LSN (8 bytes)                        │
├─────────────────────────────────────┤
│ Record Type (1 byte)                  │
├─────────────────────────────────────┤
│ Transaction ID (8 bytes)              │
├─────────────────────────────────────┤
│ Page ID (8 bytes)                     │
├─────────────────────────────────────┤
│ Data Length (4 bytes)                 │
├─────────────────────────────────────┤
│ Timestamp (8 bytes)                    │
├─────────────────────────────────────┤
│ Prev LSN (8 bytes)                    │
├─────────────────────────────────────┤
│ Data (variable)                        │
└─────────────────────────────────────┘


graph TD
    T1-->|waits for|T2
    T2-->|waits for|T3
    T3-->|waits for|T1


┌─────────────────────────────────────┐
│           Buffer Pool                │
├─────────────────────────────────────┤
│ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐   │
│ │Page1│ │Page2│ │Page3│ │Page4│...│
│ └─────┘ └─────┘ └─────┘ └─────┘   │
├─────────────────────────────────────┤
│         LRU List                     │
│  [Page4] <-> [Page2] <-> [Page1]    │
└─────────────────────────────────────┘


┌─────────────────────────┐
│      Page Header        │ 64 bytes
├─────────────────────────┤
│         Slots           │ Variable
├─────────────────────────┤
│         Free Space      │
├─────────────────────────┤
│         Records         │
└─────────────────────────┘

Configuration File
# /etc/orangesql/orangesql.conf

[server]
port = 5432
host = localhost
max_connections = 100

[memory]
buffer_pool_size = 1024
max_sort_memory = 64
join_buffer_size = 32
temp_table_max_size = 100

[storage]
data_directory = /var/lib/orangesql/data
wal_directory = /var/lib/orangesql/wal
sync_on_write = false

[transaction]
default_isolation = REPEATABLE_READ
lock_timeout_ms = 5000
deadlock_timeout_ms = 1000
transaction_timeout_sec = 300

[logging]
log_level = INFO
log_file = /var/log/orangesql/orangesql.log
log_slow_queries = true
slow_query_threshold_ms = 1000

[checkpoint]
checkpoint_interval_sec = 300
checkpoint_log_size = 50

[optimizer]
optimization_level = 2
index_selectivity_threshold = 0.2
enable_parallel_query = true
parallel_workers = 4