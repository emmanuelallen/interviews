// PART 2: OrderBook::reduce_order (order_book.hpp) and cancel ownership
// (Shard::handle_cancel in shard.hpp).
#include "exercise_util.hpp"

using namespace ex;

TEST(Part2, ReduceKeepsTimePriority)
{
  StandaloneBook<64, 64> book{BASE, 1};
  Fill                   fills[16];
  uint32_t               fc = 0;
  book::OrderHandle      first = book.add_order(book_order(1, Side::SELL, 105, 10), fills, fc, 16);
  book.add_order(book_order(2, Side::SELL, 105, 10), fills, fc, 16);

  EXPECT_TRUE(book.reduce_order(first, 4));
  EXPECT_EQ(book.level_qty(Side::SELL, 105), 14u);

  book.add_order(book_order(3, Side::BUY, 105, 6), fills, fc, 16);
  ASSERT_EQ(fc, 2u);
  EXPECT_EQ(fills[0].resting_id, 1u);  // still first in the queue
  EXPECT_EQ(fills[0].qty, 4u);
  EXPECT_EQ(fills[1].resting_id, 2u);
  EXPECT_EQ(fills[1].qty, 2u);
  EXPECT_EQ(book.level_qty(Side::SELL, 105), 8u);
}

TEST(Part2, ReduceToZeroRemovesOrder)
{
  StandaloneBook<64, 64> book{BASE, 1};
  Fill                   fills[16];
  uint32_t               fc = 0;
  book::OrderHandle      h = book.add_order(book_order(1, Side::BUY, 105, 10), fills, fc, 16);
  EXPECT_TRUE(book.reduce_order(h, 0));
  EXPECT_EQ(book.best_bid_price(), INT64_MIN);
  EXPECT_EQ(book.level_qty(Side::BUY, 105), 0u);
  EXPECT_FALSE(book.cancel_order(h));  // already gone
}

TEST(Part2, ReduceCannotIncreaseOrKeepQty)
{
  StandaloneBook<64, 64> book{BASE, 1};
  Fill                   fills[16];
  uint32_t               fc = 0;
  book::OrderHandle      h = book.add_order(book_order(1, Side::BUY, 105, 10), fills, fc, 16);
  EXPECT_FALSE(book.reduce_order(h, 10));
  EXPECT_FALSE(book.reduce_order(h, 11));
  EXPECT_EQ(book.level_qty(Side::BUY, 105), 10u);
}

TEST(Part2, ReduceStaleHandleRejected)
{
  StandaloneBook<64, 64> book{BASE, 1};
  Fill                   fills[16];
  uint32_t               fc = 0;
  book::OrderHandle      h = book.add_order(book_order(1, Side::BUY, 105, 10), fills, fc, 16);
  EXPECT_TRUE(book.cancel_order(h));
  EXPECT_FALSE(book.reduce_order(h, 5));
}

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
