#include <gtest/gtest.h>

#include "lx/book/level_bitmap.hpp"

using lx::book::LevelBitmap;

TEST(LevelBitmap, EmptyReturnsNull)
{
  LevelBitmap<2048> bm;
  EXPECT_EQ(bm.next_up(0), UINT32_MAX);
  EXPECT_EQ(bm.next_down(2047), UINT32_MAX);
}

TEST(LevelBitmap, SetTestClear)
{
  LevelBitmap<2048> bm;
  bm.set(100);
  EXPECT_TRUE(bm.test(100));
  EXPECT_FALSE(bm.test(101));
  bm.clear(100);
  EXPECT_FALSE(bm.test(100));
}

TEST(LevelBitmap, NextUpInclusiveThenAbove)
{
  LevelBitmap<2048> bm;
  bm.set(100);
  bm.set(500);
  EXPECT_EQ(bm.next_up(100), 100u);
  EXPECT_EQ(bm.next_up(101), 500u);
  EXPECT_EQ(bm.next_up(501), UINT32_MAX);
}

TEST(LevelBitmap, NextDownInclusiveThenBelow)
{
  LevelBitmap<2048> bm;
  bm.set(100);
  bm.set(500);
  EXPECT_EQ(bm.next_down(500), 500u);
  EXPECT_EQ(bm.next_down(499), 100u);
  EXPECT_EQ(bm.next_down(99), UINT32_MAX);
}

TEST(LevelBitmap, CrossWordBoundary)
{
  LevelBitmap<2048> bm;
  bm.set(63);
  bm.set(64);
  bm.set(65);
  EXPECT_EQ(bm.next_up(64), 64u);
  EXPECT_EQ(bm.next_down(64), 64u);
  bm.clear(64);
  EXPECT_EQ(bm.next_up(64), 65u);
  EXPECT_EQ(bm.next_down(64), 63u);
}

TEST(LevelBitmap, NextDownNullInput)
{
  LevelBitmap<2048> bm;
  bm.set(0);
  EXPECT_EQ(bm.next_down(UINT32_MAX), UINT32_MAX);
  EXPECT_EQ(bm.next_down(0), 0u);
}
