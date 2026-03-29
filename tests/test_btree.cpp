#include <gtest/gtest.h>
#include "index/btree.h"
#include "storage/buffer_pool.h"

using namespace orangesql;

class BTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        buffer_pool = std::make_unique<BufferPool>(100);
        btree = std::make_unique<IntIndex>(1, buffer_pool.get());
    }
    
    std::unique_ptr<BufferPool> buffer_pool;
    std::unique_ptr<IntIndex> btree;
};

TEST_F(BTreeTest, InsertAndFind) {
    // Inserir alguns valores
    for (int i = 0; i < 1000; i++) {
        RecordId rid = i;
        auto status = btree->insert(i, rid);
        EXPECT_EQ(status, Status::OK);
    }
    
    // Buscar valores
    for (int i = 0; i < 1000; i++) {
        RecordId rid;
        auto status = btree->find(i, rid);
        EXPECT_EQ(status, Status::OK);
        EXPECT_EQ(rid, i);
    }
}

TEST_F(BTreeTest, RangeQuery) {
    // Inserir valores
    for (int i = 0; i < 100; i++) {
        btree->insert(i, i);
    }
    
    // Query range
    auto results = btree->rangeQuery(30, 50);
    EXPECT_EQ(results.size(), 21); // 30 a 50 inclusive
    
    for (int i = 30; i <= 50; i++) {
        EXPECT_EQ(results[i - 30], i);
    }
}

TEST_F(BTreeTest, DuplicateKey) {
    btree->insert(42, 100);
    auto status = btree->insert(42, 200);
    EXPECT_EQ(status, Status::ALREADY_EXISTS);
}

TEST_F(BTreeTest, NotFound) {
    RecordId rid;
    auto status = btree->find(999, rid);
    EXPECT_EQ(status, Status::NOT_FOUND);
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}