// OPTIONAL B: Shard::handle_timer (shard.hpp) for orders with arbitrary expiry.
#include "exercise_util.hpp"

using namespace ex;

static size_t expired(ExShard& shard, uint64_t now, uint32_t session = SESSION_A)
{
  return of(send(shard, in_timer(now)), MsgType::ACK, session).size();
}

TEST(OptionalB, ExpiresExactlyTheOrdersThatAreDue)
{
  ExShard shard{BASE, 1};
  rest(shard, SESSION_A, 0, 1, Side::BUY, 110, 10, /*expire_at=*/10);
  rest(shard, SESSION_A, 0, 2, Side::BUY, 109, 10, 20);
  rest(shard, SESSION_A, 0, 3, Side::BUY, 108, 10);  // expire_at = 0: never
  EXPECT_EQ(expired(shard, 10), 1u);
  EXPECT_EQ(shard.best_bid(0), int64_t{109});
  EXPECT_EQ(expired(shard, 15), 0u);
  EXPECT_EQ(expired(shard, 20), 1u);
  EXPECT_EQ(shard.best_bid(0), int64_t{108});
  EXPECT_EQ(expired(shard, UINT64_MAX), 0u);
  EXPECT_EQ(shard.best_bid(0), int64_t{108});
}

TEST(OptionalB, ExpiriesInsertedOutOfOrderAcrossBooks)
{
  ExShard  shard{BASE, 1};
  uint64_t exp[] = {50, 10, 30, 10, 40};
  for (uint64_t i = 0; i < 5; ++i)
    rest(shard, SESSION_A, uint16_t(i % 2), i, Side::SELL, 110 + int64_t(i), 1, exp[i]);
  EXPECT_EQ(expired(shard, 30), 3u);
  EXPECT_EQ(shard.best_ask(0), int64_t{110});  // order 0, exp 50
  EXPECT_EQ(shard.best_ask(1), INT64_MAX);
  EXPECT_EQ(expired(shard, 50), 2u);
  EXPECT_EQ(shard.best_ask(0), INT64_MAX);
}

TEST(OptionalB, FilledOrCancelledOrdersAreNotExpiredAgain)
{
  ExShard shard{BASE, 1};
  rest(shard, SESSION_A, 0, 1, Side::SELL, 105, 5, 10);
  uint64_t t2 = rest(shard, SESSION_A, 0, 2, Side::SELL, 106, 5, 10);
  rest(shard, SESSION_A, 0, 3, Side::SELL, 107, 5, 10);
  send(shard, in_new(SESSION_B, 0, 4, Side::BUY, 105, 5));  // fills order 1
  send(shard, in_cancel(SESSION_A, t2));                    // cancels order 2
  EXPECT_EQ(expired(shard, 10), 1u);                        // only order 3
  EXPECT_EQ(shard.best_ask(0), INT64_MAX);
}

TEST(OptionalB, PartiallyFilledOrdersStillExpire)
{
  ExShard shard{BASE, 1};
  rest(shard, SESSION_A, 0, 1, Side::SELL, 105, 10, 10);
  send(shard, in_new(SESSION_B, 0, 2, Side::BUY, 105, 4));
  EXPECT_EQ(expired(shard, 10), 1u);
  EXPECT_EQ(shard.best_ask(0), INT64_MAX);
}

TEST(OptionalB, AReusedSlotDoesNotInheritTheOldExpiry)
{
  ExShard shard{BASE, 1};
  rest(shard, SESSION_A, 0, 1, Side::SELL, 105, 5, 10);
  send(shard, in_new(SESSION_B, 0, 2, Side::BUY, 105, 5));  // fills order 1, frees its slot
  // New orders that never expire; one of them lands in order 1's old slot.
  for (uint64_t i = 0; i < 4; ++i)
    rest(shard, SESSION_A, 0, 10 + i, Side::SELL, 106, 1);
  EXPECT_EQ(expired(shard, 10), 0u);
  EXPECT_EQ(shard.best_ask(0), int64_t{106});
}
