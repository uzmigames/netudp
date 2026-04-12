#include <gtest/gtest.h>
#include "../src/core/pool.h"
#include "../src/core/ring_buffer.h"
#include "../src/core/hash_map.h"

#include <cstring>

/* ===== Pool Tests ===== */

struct TestItem {
    uint64_t id;
    uint8_t padding[56]; /* Ensure >= sizeof(void*) */
};

TEST(Pool, InitAndAcquire) {
    netudp::Pool<TestItem> pool;
    ASSERT_TRUE(pool.init(4));
    EXPECT_EQ(pool.capacity(), 4);
    EXPECT_EQ(pool.available(), 4);

    TestItem* a = pool.acquire();
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(pool.available(), 3);
    EXPECT_EQ(pool.in_use(), 1);

    pool.release(a);
    EXPECT_EQ(pool.available(), 4);
}

TEST(Pool, AcquireAllThenExhaust) {
    netudp::Pool<TestItem> pool;
    ASSERT_TRUE(pool.init(3));

    TestItem* items[3];
    for (int i = 0; i < 3; ++i) {
        items[i] = pool.acquire();
        ASSERT_NE(items[i], nullptr);
    }
    EXPECT_TRUE(pool.empty());

    /* Pool exhausted — should return nullptr */
    EXPECT_EQ(pool.acquire(), nullptr);

    /* Release one and re-acquire */
    pool.release(items[0]);
    EXPECT_EQ(pool.available(), 1);

    TestItem* reacquired = pool.acquire();
    ASSERT_NE(reacquired, nullptr);
    EXPECT_EQ(pool.available(), 0);

    pool.release(reacquired);
    pool.release(items[1]);
    pool.release(items[2]);
}

TEST(Pool, ZeroInitOnAcquire) {
    netudp::Pool<TestItem> pool;
    ASSERT_TRUE(pool.init(1));

    TestItem* item = pool.acquire();
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->id, 0U);

    item->id = 42;
    pool.release(item);

    TestItem* item2 = pool.acquire();
    EXPECT_EQ(item2->id, 0U); /* Re-acquired element is zeroed */
    pool.release(item2);
}

/* ===== FixedRingBuffer Tests ===== */

TEST(RingBuffer, PushPopFIFO) {
    netudp::FixedRingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.is_empty());
    EXPECT_EQ(rb.size(), 0);

    EXPECT_TRUE(rb.push_back(10));
    EXPECT_TRUE(rb.push_back(20));
    EXPECT_TRUE(rb.push_back(30));
    EXPECT_EQ(rb.size(), 3);

    int val = 0;
    EXPECT_TRUE(rb.pop_front(&val));
    EXPECT_EQ(val, 10);
    EXPECT_TRUE(rb.pop_front(&val));
    EXPECT_EQ(val, 20);
    EXPECT_TRUE(rb.pop_front(&val));
    EXPECT_EQ(val, 30);
    EXPECT_TRUE(rb.is_empty());
}

TEST(RingBuffer, FullRejectsPush) {
    netudp::FixedRingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.push_back(1));
    EXPECT_TRUE(rb.push_back(2));
    EXPECT_TRUE(rb.push_back(3));
    EXPECT_TRUE(rb.push_back(4));
    EXPECT_TRUE(rb.full());

    EXPECT_FALSE(rb.push_back(5)); /* Full */
}

TEST(RingBuffer, IndexAccess) {
    netudp::FixedRingBuffer<int, 8> rb;
    rb.push_back(100);
    rb.push_back(200);
    rb.push_back(300);

    EXPECT_EQ(rb[0], 100);
    EXPECT_EQ(rb[1], 200);
    EXPECT_EQ(rb[2], 300);
    EXPECT_EQ(rb.front(), 100);
    EXPECT_EQ(rb.back(), 300);
}

TEST(RingBuffer, WrapAround) {
    netudp::FixedRingBuffer<int, 4> rb;
    rb.push_back(1);
    rb.push_back(2);
    rb.push_back(3);
    rb.push_back(4);

    int val = 0;
    rb.pop_front(&val); /* Remove 1 */
    rb.pop_front(&val); /* Remove 2 */
    EXPECT_TRUE(rb.push_back(5));
    EXPECT_TRUE(rb.push_back(6));

    /* Should be: 3, 4, 5, 6 */
    EXPECT_EQ(rb[0], 3);
    EXPECT_EQ(rb[1], 4);
    EXPECT_EQ(rb[2], 5);
    EXPECT_EQ(rb[3], 6);
}

TEST(RingBuffer, Clear) {
    netudp::FixedRingBuffer<int, 4> rb;
    rb.push_back(1);
    rb.push_back(2);
    rb.clear();
    EXPECT_TRUE(rb.is_empty());
    EXPECT_EQ(rb.size(), 0);
}

/* ===== FixedHashMap Tests ===== */

struct SimpleKey {
    uint32_t id;
};

TEST(HashMap, InsertFind) {
    netudp::FixedHashMap<SimpleKey, int, 16> map;
    EXPECT_TRUE(map.is_empty());

    SimpleKey k1{100};
    SimpleKey k2{200};

    map.insert(k1, 42);
    map.insert(k2, 99);

    EXPECT_EQ(map.size(), 2);

    int* v1 = map.find(k1);
    ASSERT_NE(v1, nullptr);
    EXPECT_EQ(*v1, 42);

    int* v2 = map.find(k2);
    ASSERT_NE(v2, nullptr);
    EXPECT_EQ(*v2, 99);
}

TEST(HashMap, FindMissing) {
    netudp::FixedHashMap<SimpleKey, int, 16> map;
    SimpleKey k{999};
    EXPECT_EQ(map.find(k), nullptr);
}

TEST(HashMap, Remove) {
    netudp::FixedHashMap<SimpleKey, int, 16> map;
    SimpleKey k{42};
    map.insert(k, 100);
    EXPECT_EQ(map.size(), 1);

    EXPECT_TRUE(map.remove(k));
    EXPECT_EQ(map.size(), 0);
    EXPECT_EQ(map.find(k), nullptr);
}

TEST(HashMap, UpdateExisting) {
    netudp::FixedHashMap<SimpleKey, int, 16> map;
    SimpleKey k{1};
    map.insert(k, 10);
    map.insert(k, 20);

    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(*map.find(k), 20);
}

TEST(HashMap, CollisionHandling) {
    /* Fill most of the map to force collisions */
    netudp::FixedHashMap<SimpleKey, int, 8> map;
    for (uint32_t i = 0; i < 6; ++i) {
        SimpleKey k{i};
        ASSERT_NE(map.insert(k, static_cast<int>(i * 10)), nullptr);
    }
    EXPECT_EQ(map.size(), 6);

    /* Verify all are retrievable */
    for (uint32_t i = 0; i < 6; ++i) {
        SimpleKey k{i};
        int* v = map.find(k);
        ASSERT_NE(v, nullptr);
        EXPECT_EQ(*v, static_cast<int>(i * 10));
    }
}

TEST(HashMap, FullMap) {
    netudp::FixedHashMap<SimpleKey, int, 4> map;
    for (uint32_t i = 0; i < 4; ++i) {
        SimpleKey k{i + 100};
        ASSERT_NE(map.insert(k, static_cast<int>(i)), nullptr);
    }
    EXPECT_TRUE(map.full());

    SimpleKey extra{999};
    EXPECT_EQ(map.insert(extra, 0), nullptr); /* Full */
}

TEST(HashMap, ForEach) {
    netudp::FixedHashMap<SimpleKey, int, 8> map;
    map.insert(SimpleKey{1}, 10);
    map.insert(SimpleKey{2}, 20);
    map.insert(SimpleKey{3}, 30);

    int sum = 0;
    map.for_each([&sum](const SimpleKey& /*key*/, int& value) -> bool {
        sum += value;
        return true;
    });
    EXPECT_EQ(sum, 60);
}
