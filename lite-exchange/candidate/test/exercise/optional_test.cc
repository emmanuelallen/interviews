// OPTIONAL: this test fails against the code as shipped. Find out why.
#include "exercise_util.hpp"

using namespace ex;

TEST(Optional, LargeSweepNeverLeavesACrossedBook)
{
  ExShard shard{BASE, 1};
  for (uint64_t i = 0; i < 100; ++i)
    rest(shard, SESSION_A, 0, 1000 + i, Side::SELL, 105, 1);

  auto     out = send(shard, in_new(SESSION_B, 0, 1, Side::BUY, 105, 100));
  uint32_t filled = 0;
  for (const auto& f : of(out, MsgType::FILL, SESSION_B))
    filled += f.fill.qty;

  EXPECT_LT(shard.best_bid(0), shard.best_ask(0));  // a book must never be crossed
  EXPECT_EQ(filled, 100u);
  EXPECT_TRUE(of(out, MsgType::ACK, SESSION_B).empty());  // nothing left to rest
}
