#include "lx/queue/spsc.hpp"
#include <gtest/gtest.h>
#include <thread>

using namespace lx;

TEST(Spsc, PushPopSingle) {
  SpscQueue<int, 8> q;
  EXPECT_TRUE(q.push(42));
  int val{};
  EXPECT_TRUE(q.pop(val));
  EXPECT_EQ(val, 42);
}

TEST(Spsc, FullQueueRejectsPush) {
  SpscQueue<int, 4> q;
  EXPECT_TRUE(q.push(1));
  EXPECT_TRUE(q.push(2));
  EXPECT_TRUE(q.push(3));
  EXPECT_TRUE(q.push(4));
  EXPECT_FALSE(q.push(5));
}

TEST(Spsc, EmptyQueueRejectsPop) {
  SpscQueue<int, 4> q;
  int val{};
  EXPECT_FALSE(q.pop(val));
}

TEST(Spsc, WrapAround) {
  SpscQueue<int, 4> q;
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(q.push(i));
    int val{};
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, i);
  }
}

TEST(Spsc, ProducerConsumerThread) {
  SpscQueue<int, 1024> q;
  constexpr int kN = 100'000;

  std::thread p([&]() {
    for (int i = 0; i < kN; ++i)
      while (!q.push(i)) {
      }
  });

  int last = -1;
  for (int i = 0; i < kN; ++i) {
    int val{};
    while (!q.pop(val)) {
    }
    EXPECT_EQ(val, last + 1);
    last = val;
  }
  p.join();
}