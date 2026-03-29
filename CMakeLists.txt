# ============================================
# OrangeSQL - CMake Configuration
# ============================================

cmake_minimum_required(VERSION 3.20)
project(OrangeSQL VERSION 1.0.0 LANGUAGES CXX)

# ============================================
# Project Configuration
# ============================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================
# Build Options
# ============================================

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
option(BUILD_DOCS "Build documentation" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)

option(ENABLE_DEBUG "Enable debug output and assertions" OFF)
option(ENABLE_SANITIZERS "Enable address and undefined sanitizers" OFF)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
option(ENABLE_PROFILING "Enable profiling with gprof" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

option(USE_VCPKG "Use vcpkg for dependency management" ON)
option(USE_SYSTEM_LIBS "Use system libraries instead of vcpkg" OFF)
option(USE_CCACHE "Use ccache for faster builds" ON)

option(OPTIMIZE_FOR_NATIVE "Optimize for native architecture" ON)
option(LTO_ENABLED "Enable Link Time Optimization" ON)

# ============================================
# Compiler and Platform Detection
# ============================================

if(ENABLE_DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
    add_compile_definitions(ORANGESQL_DEBUG)
else()
    set(CMAKE_BUILD_TYPE Release)
endif()

# Detect compiler
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(COMPILER_GCC TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(COMPILER_CLANG TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    set(COMPILER_APPLE_CLANG TRUE)
elseif(MSVC)
    set(COMPILER_MSVC TRUE)
endif()

# Detect platform
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_LINUX TRUE)
endif()

# ============================================
# Ccache Support
# ============================================

if(USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
        message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    endif()
endif()

# ============================================
# vcpkg Integration
# ============================================

if(USE_VCPKG AND NOT USE_SYSTEM_LIBS)
    # Allow user to specify vcpkg root
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "Vcpkg toolchain file")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "Vcpkg toolchain file")
    else()
        message(WARNING "vcpkg not found. Install vcpkg or set VCPKG_ROOT environment variable.")
    endif()
endif()

# ============================================
# Find Required Packages
# ============================================

# Threads (always required)
find_package(Threads REQUIRED)

# nlohmann/json - for catalog and configuration
find_package(nlohmann_json CONFIG)
if(nlohmann_json_FOUND)
    message(STATUS "Found nlohmann_json: ${nlohmann_json_DIR}")
    set(JSON_LIBRARIES nlohmann_json::nlohmann_json)
else()
    find_package(nlohmann_json REQUIRED)
endif()

# Readline - for CLI
find_package(Readline)
if(NOT READLINE_FOUND)
    # Fallback: try to find manually
    find_path(READLINE_INCLUDE_DIR NAMES readline/readline.h)
    find_library(READLINE_LIBRARY NAMES readline)
    if(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
        set(READLINE_FOUND TRUE)
        message(STATUS "Found Readline: ${READLINE_LIBRARY}")
    else()
        message(WARNING "Readline library not found. CLI will have limited functionality.")
    endif()
endif()

# Google Test - for tests
if(BUILD_TESTS)
    find_package(GTest CONFIG)
    if(NOT GTest_FOUND)
        find_package(GTest REQUIRED)
    endif()
endif()

# Google Benchmark - for benchmarks
if(BUILD_BENCHMARKS)
    find_package(benchmark CONFIG REQUIRED)
endif()

# Optional: liburing for Linux async I/O
if(PLATFORM_LINUX)
    find_package(LibUring)
    if(LibUring_FOUND)
        message(STATUS "Found liburing: enabled async I/O")
        add_compile_definitions(HAVE_LIBURING)
    endif()
endif()

# Optional: jemalloc/tcmalloc
option(USE_JEMALLOC "Use jemalloc for memory allocation" OFF)
if(USE_JEMALLOC)
    find_package(JeMalloc)
    if(JeMalloc_FOUND)
        message(STATUS "Found jemalloc")
        set(MALLOC_LIBRARY JeMalloc::JeMalloc)
    endif()
endif()

# ============================================
# Compiler and Linker Flags
# ============================================

# Warning flags
if(COMPILER_GCC OR COMPILER_CLANG)
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    add_compile_options(-Wno-unused-parameter)  # Allow unused parameters
    add_compile_options(-Wno-deprecated-declarations)
    
    if(ENABLE_DEBUG)
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O3 -DNDEBUG)
        if(LTO_ENABLED)
            add_compile_options(-flto)
            add_link_options(-flto)
        endif()
    endif()
    
    if(OPTIMIZE_FOR_NATIVE)
        add_compile_options(-march=native -mtune=native)
    endif()
    
    # Coverage
    if(ENABLE_COVERAGE)
        add_compile_options(--coverage)
        add_link_options(--coverage)
    endif()
    
    # Profiling
    if(ENABLE_PROFILING)
        add_compile_options(-pg)
        add_link_options(-pg)
    endif()
    
    # Sanitizers
    if(ENABLE_SANITIZERS OR ENABLE_ASAN)
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
    endif()
    if(ENABLE_SANITIZERS OR ENABLE_UBSAN)
        add_compile_options(-fsanitize=undefined)
        add_link_options(-fsanitize=undefined)
    endif()
    if(ENABLE_SANITIZERS OR ENABLE_TSAN)
        add_compile_options(-fsanitize=thread)
        add_link_options(-fsanitize=thread)
    endif()
    
elseif(COMPILER_MSVC)
    add_compile_options(/W4 /WX)
    if(ENABLE_DEBUG)
        add_compile_options(/Od /RTC1)
    else()
        add_compile_options(/O2)
    endif()
endif()

# Platform-specific flags
if(PLATFORM_LINUX)
    add_link_options(-pthread)
    add_link_options(-lrt)
endif()

# ============================================
# Project Directories
# ============================================

set(ORANGESQL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(ORANGESQL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ORANGESQL_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

include_directories(${ORANGESQL_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# ============================================
# Source Files
# ============================================

set(ORANGESQL_SOURCES
    # CLI
    cli/main.cpp
    cli/console.cpp
    cli/console.h
    
    # Parser
    parser/sql_parser.cpp
    parser/sql_parser.h
    parser/ast.h
    parser/lexer.cpp
    
    # Engine
    engine/query_executor.cpp
    engine/query_executor.h
    engine/optimizer.cpp
    engine/optimizer.h
    engine/executor_context.h
    
    # Storage
    storage/file_manager.cpp
    storage/file_manager.h
    storage/page.cpp
    storage/page.h
    storage/buffer_pool.cpp
    storage/buffer_pool.h
    storage/table.cpp
    storage/table.h
    storage/record.cpp
    
    # Index
    index/btree.cpp
    index/btree.h
    index/btree_node.cpp
    index/btree_node.h
    index/index_manager.cpp
    index/index_manager.h
    
    # Transaction
    transaction/transaction_manager.cpp
    transaction/transaction_manager.h
    transaction/log_manager.cpp
    transaction/log_manager.h
    transaction/lock_manager.cpp
    transaction/lock_manager.h
    transaction/wal.cpp
    transaction/wal.h
    transaction/checkpoint.cpp
    transaction/checkpoint.h
    
    # Metadata
    metadata/catalog.cpp
    metadata/catalog.h
    metadata/schema.cpp
    metadata/schema.h
    metadata/statistics.cpp
    metadata/statistics.h
    
    # Include headers (for IDE)
    include/types.h
    include/constants.h
    include/status.h
    include/utils.h
)

# ============================================
# Main Library
# ============================================

add_library(orangesql_core STATIC ${ORANGESQL_SOURCES})

target_include_directories(orangesql_core
    PUBLIC
        $<BUILD_INTERFACE:${ORANGESQL_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(orangesql_core
    PUBLIC
        Threads::Threads
        ${JSON_LIBRARIES}
    PRIVATE
        ${MALLOC_LIBRARY}
)

if(READLINE_FOUND)
    target_link_libraries(orangesql_core PRIVATE ${READLINE_LIBRARY})
    target_include_directories(orangesql_core PRIVATE ${READLINE_INCLUDE_DIR})
    target_compile_definitions(orangesql_core PRIVATE HAVE_READLINE)
endif()

if(LibUring_FOUND)
    target_link_libraries(orangesql_core PRIVATE LibUring::LibUring)
endif()

# Set properties
set_target_properties(orangesql_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME orangesql
    PUBLIC_HEADER "include/types.h;include/constants.h;include/status.h;include/utils.h"
)

# ============================================
# Executable
# ============================================

add_executable(orangesql ${ORANGESQL_SOURCES})

target_link_libraries(orangesql
    PRIVATE
        orangesql_core
        ${MALLOC_LIBRARY}
)

if(WIN32)
    # Windows-specific settings
    set_target_properties(orangesql PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
endif()

# ============================================
# Tests
# ============================================

if(BUILD_TESTS)
    enable_testing()
    
    set(TEST_SOURCES
        tests/test_btree.cpp
        tests/test_page.cpp
        tests/test_transaction.cpp
        tests/test_parser.cpp
        tests/test_catalog.cpp
        tests/test_buffer_pool.cpp
        tests/test_lock_manager.cpp
        tests/test_utils.cpp
        tests/test_integration.cpp
    )
    
    add_executable(orangesql_tests ${TEST_SOURCES})
    
    target_link_libraries(orangesql_tests
        PRIVATE
            orangesql_core
            GTest::gtest
            GTest::gtest_main
            GTest::gmock
    )
    
    target_include_directories(orangesql_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    
    # Register tests
    add_test(NAME btree_test COMMAND orangesql_tests --gtest_filter=BTreeTest.*)
    add_test(NAME page_test COMMAND orangesql_tests --gtest_filter=PageTest.*)
    add_test(NAME transaction_test COMMAND orangesql_tests --gtest_filter=TransactionTest.*)
    add_test(NAME parser_test COMMAND orangesql_tests --gtest_filter=ParserTest.*)
    add_test(NAME catalog_test COMMAND orangesql_tests --gtest_filter=CatalogTest.*)
    add_test(NAME buffer_pool_test COMMAND orangesql_tests --gtest_filter=BufferPoolTest.*)
    add_test(NAME lock_manager_test COMMAND orangesql_tests --gtest_filter=LockManagerTest.*)
    add_test(NAME utils_test COMMAND orangesql_tests --gtest_filter=UtilsTest.*)
    add_test(NAME integration_test COMMAND orangesql_tests --gtest_filter=IntegrationTest.*)
    
    # Add test target
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS orangesql_tests
        COMMENT "Running tests..."
    )
endif()

# ============================================
# Benchmarks
# ============================================

if(BUILD_BENCHMARKS)
    set(BENCHMARK_SOURCES
        benchmarks/btree_benchmark.cpp
        benchmarks/transaction_benchmark.cpp
        benchmarks/query_benchmark.cpp
        benchmarks/concurrency_benchmark.cpp
    )
    
    add_executable(orangesql_benchmark ${BENCHMARK_SOURCES})
    
    target_link_libraries(orangesql_benchmark
        PRIVATE
            orangesql_core
            benchmark::benchmark
    )
endif()

# ============================================
# Examples
# ============================================

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ============================================
# Documentation
# ============================================

if(BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    
    set(DOXYGEN_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs)
    set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/html/index.html)
    
    doxygen_add_docs(docs
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/cli
        ${CMAKE_CURRENT_SOURCE_DIR}/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/index
        ${CMAKE_CURRENT_SOURCE_DIR}/metadata
        ${CMAKE_CURRENT_SOURCE_DIR}/parser
        ${CMAKE_CURRENT_SOURCE_DIR}/storage
        ${CMAKE_CURRENT_SOURCE_DIR}/transaction
        COMMENT "Generate documentation"
    )
endif()

# ============================================
# Installation
# ============================================

install(TARGETS orangesql orangesql_core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include/orangesql
)

install(DIRECTORY include/ 
    DESTINATION include/orangesql 
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY data/ 
    DESTINATION share/orangesql/data 
    OPTIONAL
)

install(FILES 
    README.md 
    LICENSE 
    orangesql.conf
    DESTINATION share/orangesql
)

# ============================================
# Packaging
# ============================================

set(CPACK_PACKAGE_NAME "OrangeSQL")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ACID compliant SQL database with B-Tree indexes")
set(CPACK_PACKAGE_VENDOR "OrangeSQL")
set(CPACK_PACKAGE_CONTACT "support@orangesql.org")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_GENERATOR "TGZ;DEB;RPM")
set(CPACK_SOURCE_GENERATOR "TGZ")

set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libreadline8")
set(CPACK_RPM_PACKAGE_REQUIRES "readline")

include(CPack)

# ============================================
# Custom Targets
# ============================================

# Format code
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_SOURCE_FILES 
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp 
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    )
    list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*/build/.*")
    
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_SOURCE_FILES}
        COMMENT "Formatting code with clang-format"
    )
endif()

# Lint code
find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
    add_custom_target(lint
        COMMAND ${CLANG_TIDY} ${ALL_SOURCE_FILES} -checks=* -- -std=c++17
        COMMENT "Running clang-tidy"
    )
endif()

# Create data directories
add_custom_target(dirs
    COMMAND ${CMAKE_COMMAND} -E make_directory data/tables
    COMMAND ${CMAKE_COMMAND} -E make_directory data/wal
    COMMAND ${CMAKE_COMMAND} -E make_directory data/system
    COMMAND ${CMAKE_COMMAND} -E make_directory data/temp
    COMMENT "Creating data directories"
)

# Build and run
add_custom_target(rundb
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/orangesql
    DEPENDS orangesql
    COMMENT "Building and running OrangeSQL"
)

# Clean everything
add_custom_target(distclean
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning build directory"
)

# ============================================
# Generate Config File
# ============================================

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OrangeSQLConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/OrangeSQLConfig.cmake"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/OrangeSQLConfig.cmake"
    DESTINATION lib/cmake/OrangeSQL
)

# ============================================
# Print Configuration Summary
# ============================================

message(STATUS "")
message(STATUS "=========================================")
message(STATUS "OrangeSQL Build Configuration")
message(STATUS "=========================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Build benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "Build docs: ${BUILD_DOCS}")
message(STATUS "Build examples: ${BUILD_EXAMPLES}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  Debug mode: ${ENABLE_DEBUG}")
message(STATUS "  Sanitizers: ${ENABLE_SANITIZERS}")
message(STATUS "  Coverage: ${ENABLE_COVERAGE}")
message(STATUS "  Profiling: ${ENABLE_PROFILING}")
message(STATUS "  LTO: ${LTO_ENABLED}")
message(STATUS "  Native optimize: ${OPTIMIZE_FOR_NATIVE}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  Using vcpkg: ${USE_VCPKG}")
message(STATUS "  nlohmann_json: ${nlohmann_json_FOUND}")
message(STATUS "  Readline: ${READLINE_FOUND}")
message(STATUS "  GTest: ${GTest_FOUND}")
message(STATUS "  Benchmark: ${benchmark_FOUND}")
if(LibUring_FOUND)
    message(STATUS "  liburing: yes")
endif()
if(MALLOC_LIBRARY)
    message(STATUS "  Custom malloc: yes")
endif()
message(STATUS "")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "=========================================")
message(STATUS "")

# ============================================
# Post-build steps
# ============================================

add_custom_command(TARGET orangesql POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
    COMMENT "Copying data directory"
)

# ============================================
# Uninstall target
# ============================================

if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
        IMMEDIATE @ONLY
    )
    
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake
        COMMENT "Uninstalling OrangeSQL"
    )
endif()