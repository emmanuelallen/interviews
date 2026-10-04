// PART 2: cancel ownership (Shard::handle_cancel in shard.hpp).
#include "exercise_util.hpp"

using namespace ex;

TEST(Part2, OtherSessionCannotCancel)
{
  ExShard  shard{BASE, 1};
  uint64_t token = rest(shard, SESSION_A, 0, 1, Side::BUY, 105, 10);

  auto out = send(shard, in_cancel(SESSION_B, token));
  auto rejects = of(out, MsgType::REJECT, SESSION_B);
  ASSERT_EQ(rejects.size(), 1u);
  EXPECT_EQ(rejects[0].reject.reason, RejectReason::UNKNOWN_ORDER);
  EXPECT_TRUE(of(out, MsgType::ACK, SESSION_B).empty());
  EXPECT_EQ(shard.best_bid(0), int64_t{105});  // still resting

  EXPECT_EQ(of(send(shard, in_cancel(SESSION_A, token)), MsgType::ACK, SESSION_A).size(), 1u);
  EXPECT_EQ(shard.best_bid(0), INT64_MIN);
}
