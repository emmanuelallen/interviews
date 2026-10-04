// BONUS (solution only): tokens that pass the original gen check but name no
// live order. Against the original repo code, the first test's cancel is ACKed
// and slot 0 is freed while already on the free list.
#include "exercise_util.hpp"

using namespace ex;

TEST(Bonus, TokenForNeverUsedSlotRejected)
{
  ExShard shard{BASE, 1};
  rest(shard, 0, 0, 1, Side::BUY, 105, 10);  // session 0 = the default in tests
  // slot 0, gen 0: never allocated (the pool hands out slots from the top).
  auto out = send(shard, in_cancel(0, 0));
  EXPECT_EQ(of(out, MsgType::REJECT, 0).size(), 1u);
  EXPECT_EQ(shard.best_bid(0), int64_t{105});
}

TEST(Bonus, TokenForFreedSlotsNextGenRejected)
{
  ExShard  shard{BASE, 1};
  uint64_t token = rest(shard, 0, 0, 1, Side::SELL, 105, 10);
  send(shard, in_new(0, 0, 2, Side::BUY, 105, 10));  // fills, frees slot, gen + 1
  book::OrderHandle h = book::OrderHandle::from_token(token);
  book::OrderHandle forged{h.slot, h.gen + 1};
  EXPECT_EQ(of(send(shard, in_cancel(0, forged.to_token())), MsgType::REJECT, 0).size(), 1u);
}
