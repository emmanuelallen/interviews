// PART 3: Shard::handle_mass_cancel (shard.hpp).
#include <set>

#include "exercise_util.hpp"

using namespace ex;

TEST(Part3, CancelsOnlyThatAccountAcrossBooks)
{
  ExShard  shard{BASE, 1};
  uint64_t t1 = rest(shard, SESSION_A, 0, 1, Side::BUY, 101, 10);
  uint64_t t2 = rest(shard, SESSION_A, 0, 2, Side::SELL, 110, 10);
  uint64_t t3 = rest(shard, SESSION_A, 1, 3, Side::BUY, 102, 10);
  uint64_t t4 = rest(shard, SESSION_A, 1, 4, Side::SELL, 111, 10);
  rest(shard, SESSION_B, 0, 5, Side::BUY, 100, 10);
  rest(shard, SESSION_B, 1, 6, Side::SELL, 112, 10);

  auto out = send(shard, in_mass_cancel(SESSION_A));
  auto acks = of(out, MsgType::ACK, SESSION_A);
  EXPECT_EQ(out.size(), 4u);
  ASSERT_EQ(acks.size(), 4u);

  std::set<uint64_t> ids, tokens;
  for (const auto& a : acks)
  {
    ids.insert(a.ack.order_id);
    tokens.insert(a.ack.order_token);
  }
  EXPECT_TRUE((ids == std::set<uint64_t>{1, 2, 3, 4}));
  EXPECT_TRUE((tokens == std::set<uint64_t>{t1, t2, t3, t4}));

  EXPECT_EQ(shard.best_bid(0), int64_t{100});
  EXPECT_EQ(shard.best_ask(0), INT64_MAX);
  EXPECT_EQ(shard.best_bid(1), INT64_MIN);
  EXPECT_EQ(shard.best_ask(1), int64_t{112});
}

TEST(Part3, SkipsOrdersAlreadyFilledOrCancelled)
{
  ExShard shard{BASE, 1};
  rest(shard, SESSION_A, 0, 1, Side::SELL, 105, 5);
  uint64_t t2 = rest(shard, SESSION_A, 0, 2, Side::SELL, 106, 5);
  rest(shard, SESSION_A, 1, 3, Side::SELL, 107, 5);
  send(shard, in_new(SESSION_B, 0, 4, Side::BUY, 105, 5));  // fills order 1
  send(shard, in_cancel(SESSION_A, t2));                    // cancels order 2

  auto acks = of(send(shard, in_mass_cancel(SESSION_A)), MsgType::ACK, SESSION_A);
  ASSERT_EQ(acks.size(), 1u);
  EXPECT_EQ(acks[0].ack.order_id, 3u);
  EXPECT_EQ(shard.best_ask(0), INT64_MAX);
  EXPECT_EQ(shard.best_ask(1), INT64_MAX);
}

TEST(Part3, IncludesPartiallyFilledOrders)
{
  ExShard shard{BASE, 1};
  rest(shard, SESSION_A, 0, 1, Side::SELL, 105, 10);
  send(shard, in_new(SESSION_B, 0, 2, Side::BUY, 105, 4));
  EXPECT_EQ(of(send(shard, in_mass_cancel(SESSION_A)), MsgType::ACK, SESSION_A).size(), 1u);
  EXPECT_EQ(shard.best_ask(0), INT64_MAX);
}

TEST(Part3, TokensDeadAfterMassCancelAndAccountCanTradeAgain)
{
  ExShard  shard{BASE, 1};
  uint64_t token = rest(shard, SESSION_A, 0, 1, Side::BUY, 105, 10);
  EXPECT_EQ(of(send(shard, in_mass_cancel(SESSION_A)), MsgType::ACK, SESSION_A).size(), 1u);
  EXPECT_EQ(of(send(shard, in_cancel(SESSION_A, token)), MsgType::REJECT, SESSION_A).size(), 1u);

  rest(shard, SESSION_A, 0, 2, Side::BUY, 106, 1);
  EXPECT_EQ(shard.best_bid(0), int64_t{106});
  EXPECT_EQ(of(send(shard, in_mass_cancel(SESSION_A)), MsgType::ACK, SESSION_A).size(), 1u);
  EXPECT_TRUE(send(shard, in_mass_cancel(SESSION_A)).empty());
}

TEST(Part3, AccountWithNoOrdersIsANoOp)
{
  ExShard shard{BASE, 1};
  rest(shard, SESSION_A, 0, 1, Side::BUY, 105, 10);
  EXPECT_TRUE(send(shard, in_mass_cancel(SESSION_B)).empty());
  EXPECT_EQ(shard.best_bid(0), int64_t{105});
}

TEST(Part3, FilledOrdersSlotReusedByAnotherAccountIsNotCancelled)
{
  ExShard shard{BASE, 1};
  rest(shard, SESSION_A, 0, 1, Side::SELL, 105, 5);
  send(shard, in_new(SESSION_B, 0, 2, Side::BUY, 105, 5));  // fills A's order, frees its slot
  // B's new resting orders; one of them lands in the slot A's order used.
  for (uint64_t i = 0; i < 4; ++i)
    rest(shard, SESSION_B, 0, 10 + i, Side::BUY, 101 + int64_t(i), 1);

  EXPECT_TRUE(send(shard, in_mass_cancel(SESSION_A)).empty());
  EXPECT_EQ(shard.best_bid(0), int64_t{104});  // all of B's orders survive
  EXPECT_EQ(of(send(shard, in_mass_cancel(SESSION_B)), MsgType::ACK, SESSION_B).size(), 4u);
}
