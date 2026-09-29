// =============================================================================
//  Lite-Exchange: order book exercise
//
//  This is a trimmed, single-file version of the order book from your
//  lite-exchange repo (book/order_book.hpp, pool.hpp, price_level.hpp,
//  level_bitmap.hpp). The huge-page allocation, wire headers and sharding have
//  been removed so the file compiles on its own. The matching logic is the same
//  as in your repo.
//
//  Build: g++ -std=c++20 -O1 -g -fsanitize=address,undefined candidate.cpp
//
//  PART 1    Walk us through the code (no changes needed).
//  PART 2    Implement reduce_order() and cancel ownership checks (see the TODOs).
//  PART 3    Implement cancel_all() for an account (see the TODO).
//  OPTIONAL  The "OPTIONAL" test in main() fails. Your interviewer will say
//            whether to look at it. If so, find the cause and fix it.
//
//  For this exercise an account is identified by its session_id.
// =============================================================================
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

// ---------------------------------------------------------------- wire types
enum class Side : uint8_t { BUY = 0, SELL = 1 };
enum class TimeInForce : uint8_t { GTC = 0, IOC = 1 };

struct NewOrder
{
  uint32_t    session_id;  // stamped by the gateway, server-authoritative
  uint64_t    order_id;
  int64_t     price;
  uint32_t    qty;
  Side        side;
  TimeInForce tif;
};

struct Fill
{
  uint32_t session_id;  // passive owner
  uint64_t aggressor_id;
  uint64_t resting_id;
  int64_t  price;
  uint32_t qty;
};

// ---------------------------------------------------------------- order + pool
static constexpr uint32_t NULL_IDX = UINT32_MAX;

struct Order
{
  uint64_t    order_id;
  int64_t     price;
  uint32_t    qty;
  uint32_t    next_idx;
  uint32_t    prev_idx;
  Side        side;
  TimeInForce tif;
};

template <typename T, uint32_t N>
class Pool
{
 public:
  static constexpr uint32_t INVALID_IDX = UINT32_MAX;
  Pool() : slots_(N), free_list_(N), gen_(N, 0)
  {
    for (uint32_t i = 0; i < N; ++i)
      free_list_[i] = i;
    free_top_ = N;
  }
  uint32_t alloc() { return free_top_ == 0 ? INVALID_IDX : free_list_[--free_top_]; }
  void     free(uint32_t idx)
  {
    assert(idx < N);
    gen_[idx]++;
    free_list_[free_top_++] = idx;
  }
  T&       operator[](uint32_t idx) { return slots_[idx]; }
  T*       data() { return slots_.data(); }
  uint32_t gen(uint32_t idx) const { return gen_[idx]; }
  uint32_t free_count() const { return free_top_; }

 private:
  std::vector<T>        slots_;
  std::vector<uint32_t> free_list_;
  std::vector<uint32_t> gen_;
  uint32_t              free_top_;
};

// ---------------------------------------------------------------- price level
struct PriceLevel
{
  uint32_t head_idx = NULL_IDX;
  uint32_t tail_idx = NULL_IDX;
  uint32_t order_count = 0;
  uint32_t total_qty = 0;

  bool empty() const { return head_idx == NULL_IDX; }

  void push_back(uint32_t idx, Order* base)
  {
    Order& o = base[idx];
    o.prev_idx = tail_idx;
    o.next_idx = NULL_IDX;
    if (tail_idx != NULL_IDX)
      base[tail_idx].next_idx = idx;
    else
      head_idx = idx;
    tail_idx = idx;
    ++order_count;
    total_qty += o.qty;
  }

  uint32_t pop_front(Order* base)
  {
    if (head_idx == NULL_IDX)
      return NULL_IDX;
    uint32_t idx = head_idx;
    Order&   o = base[idx];
    head_idx = o.next_idx;
    if (head_idx != NULL_IDX)
      base[head_idx].prev_idx = NULL_IDX;
    else
      tail_idx = NULL_IDX;
    --order_count;
    total_qty -= o.qty;
    return idx;
  }

  void remove(uint32_t idx, Order* base)
  {
    Order& o = base[idx];
    if (o.prev_idx != NULL_IDX)
      base[o.prev_idx].next_idx = o.next_idx;
    else
      head_idx = o.next_idx;
    if (o.next_idx != NULL_IDX)
      base[o.next_idx].prev_idx = o.prev_idx;
    else
      tail_idx = o.prev_idx;
    --order_count;
    total_qty -= o.qty;
  }
};

// ---------------------------------------------------------------- bitmap
template <uint32_t N>
class LevelBitmap
{
 public:
  void set(uint32_t idx) { words_[idx >> 6] |= (1ULL << (idx & 63)); }
  void clear(uint32_t idx) { words_[idx >> 6] &= ~(1ULL << (idx & 63)); }

  // lowest set bit >= from, else NULL_IDX
  uint32_t next_up(uint32_t from) const
  {
    if (from >= N)
      return NULL_IDX;
    uint32_t w = from >> 6;
    uint64_t bits = words_[w] & (~0ULL << (from & 63));
    while (true)
    {
      if (bits)
        return (w << 6) + static_cast<uint32_t>(__builtin_ctzll(bits));
      if (++w >= NUM_WORDS)
        return NULL_IDX;
      bits = words_[w];
    }
  }

  // highest set bit <= from, else NULL_IDX
  uint32_t next_down(uint32_t from) const
  {
    if (from >= N)
      return NULL_IDX;
    uint32_t w = from >> 6;
    uint64_t bits = words_[w] & (~0ULL >> (63 - (from & 63)));
    while (true)
    {
      if (bits)
        return (w << 6) + (63u - static_cast<uint32_t>(__builtin_clzll(bits)));
      if (w == 0)
        return NULL_IDX;
      bits = words_[--w];
    }
  }

 private:
  static constexpr uint32_t NUM_WORDS = (N + 63) / 64;
  uint64_t                  words_[NUM_WORDS]{};
};

// ---------------------------------------------------------------- order book
struct OrderHandle
{
  static constexpr uint32_t INVALID = UINT32_MAX;
  uint32_t                  slot = INVALID;
  uint32_t                  gen = 0;
  bool                      valid() const { return slot != INVALID; }

  // [63:56] shard | [55:24] gen | [23:0] slot
  uint64_t to_token(uint8_t shard = 0) const
  {
    return (static_cast<uint64_t>(shard) << 56) | (static_cast<uint64_t>(gen) << 24) |
           (static_cast<uint64_t>(slot) & 0xFFFFFF);
  }
  static OrderHandle from_token(uint64_t token)
  {
    return {static_cast<uint32_t>(token & 0xFFFFFF),
            static_cast<uint32_t>((token >> 24) & 0xFFFFFFFF)};
  }
};

template <uint32_t MAX_ORDERS, uint32_t LADDER_SIZE>
class OrderBook
{
 public:
  OrderBook(int64_t base_price, int64_t tick_size)
      : base_price_(base_price), tick_size_(tick_size), order_session_(MAX_ORDERS, 0)
  {
  }

  // Valid handle if the order rests; invalid if filled or rejected.
  OrderHandle add_order(const NewOrder& msg, Fill* fills, uint32_t& fill_count,
                        uint32_t max_fills)
  {
    int32_t  idx = price_to_idx(msg.price);
    uint32_t uidx = static_cast<uint32_t>(idx);
    if (idx < 0 || uidx >= LADDER_SIZE)
      return {};

    uint32_t slot = pool_.alloc();
    if (slot == Pool<Order, MAX_ORDERS>::INVALID_IDX)
      return {};

    Order& o = pool_[slot];
    o.order_id = msg.order_id;
    o.price = msg.price;
    o.qty = msg.qty;
    o.side = msg.side;
    o.tif = msg.tif;
    o.next_idx = NULL_IDX;
    o.prev_idx = NULL_IDX;
    order_session_[slot] = msg.session_id;

    fill_count = 0;

    if (msg.side == Side::BUY)
    {
      match<Side::BUY>(o, uidx, fills, fill_count, max_fills);
      if (o.qty > 0 && msg.tif != TimeInForce::IOC)
      {
        bid_levels_[uidx].push_back(slot, pool_.data());
        bid_occ_.set(uidx);
        if (best_bid_idx_ == NULL_IDX || uidx > best_bid_idx_)
          best_bid_idx_ = uidx;
        return {slot, pool_.gen(slot)};
      }
    }
    else
    {
      match<Side::SELL>(o, uidx, fills, fill_count, max_fills);
      if (o.qty > 0 && msg.tif != TimeInForce::IOC)
      {
        ask_levels_[uidx].push_back(slot, pool_.data());
        ask_occ_.set(uidx);
        if (best_ask_idx_ == NULL_IDX || uidx < best_ask_idx_)
          best_ask_idx_ = uidx;
        return {slot, pool_.gen(slot)};
      }
    }

    pool_.free(slot);
    return {};
  }

  bool cancel_order(OrderHandle h)
  {
    if (h.slot >= MAX_ORDERS || pool_.gen(h.slot) != h.gen)
      return false;

    Order&   o = pool_[h.slot];
    uint32_t uidx = static_cast<uint32_t>(price_to_idx(o.price));

    if (o.side == Side::BUY)
    {
      bid_levels_[uidx].remove(h.slot, pool_.data());
      if (bid_levels_[uidx].empty())
      {
        bid_occ_.clear(uidx);
        if (best_bid_idx_ == uidx)
          best_bid_idx_ = find_best<Side::BUY>(uidx > 0 ? uidx - 1 : NULL_IDX);
      }
    }
    else
    {
      ask_levels_[uidx].remove(h.slot, pool_.data());
      if (ask_levels_[uidx].empty())
      {
        ask_occ_.clear(uidx);
        if (best_ask_idx_ == uidx)
          best_ask_idx_ = find_best<Side::SELL>(uidx + 1);
      }
    }

    pool_.free(h.slot);
    return true;
  }

  bool cancel_by_token(uint64_t token) { return cancel_order(OrderHandle::from_token(token)); }

  // ------------------------------------------------------------ PART 2 TODOs
  // Cancel is only allowed for the session that entered the order.
  bool cancel_by_token(uint64_t token, uint32_t session_id)
  {
    (void)session_id;  // TODO(part 2): enforce ownership
    return cancel_by_token(token);
  }

  // Reduce a resting order's quantity to new_qty WITHOUT losing time priority.
  //   - only the owning session may reduce
  //   - new_qty must be strictly less than the current open qty
  //   - new_qty == 0 behaves like a cancel
  // Returns true on success, false if rejected.
  bool reduce_order(uint64_t token, uint32_t session_id, uint32_t new_qty)
  {
    (void)token, (void)session_id, (void)new_qty;
    return false;  // TODO(part 2)
  }

  // ------------------------------------------------------------ PART 3 TODO
  // Cancel every resting order belonging to session_id (both sides, all
  // price levels). Returns the number of orders cancelled.
  // Think about: what does this cost with 1M resting orders in the arena and
  // an account that has 3 of them? This runs when a client disconnects.
  uint32_t cancel_all(uint32_t session_id)
  {
    (void)session_id;
    return 0;  // TODO(part 3)
  }
  // --------------------------------------------------------------------------

  int64_t best_bid_price() const
  {
    return best_bid_idx_ == NULL_IDX ? INT64_MIN
                                     : base_price_ + int64_t(best_bid_idx_) * tick_size_;
  }
  int64_t best_ask_price() const
  {
    return best_ask_idx_ == NULL_IDX ? INT64_MAX
                                     : base_price_ + int64_t(best_ask_idx_) * tick_size_;
  }
  uint32_t qty_at(Side side, int64_t price) const
  {
    int32_t idx = price_to_idx(price);
    if (idx < 0 || uint32_t(idx) >= LADDER_SIZE)
      return 0;
    return side == Side::BUY ? bid_levels_[idx].total_qty : ask_levels_[idx].total_qty;
  }
  uint32_t live_orders() const { return MAX_ORDERS - pool_.free_count(); }

 private:
  int64_t  base_price_;
  int64_t  tick_size_;
  uint32_t best_bid_idx_ = NULL_IDX;
  uint32_t best_ask_idx_ = NULL_IDX;

  PriceLevel               bid_levels_[LADDER_SIZE];
  PriceLevel               ask_levels_[LADDER_SIZE];
  LevelBitmap<LADDER_SIZE> bid_occ_;
  LevelBitmap<LADDER_SIZE> ask_occ_;
  Pool<Order, MAX_ORDERS>  pool_;
  std::vector<uint32_t>    order_session_;  // owner session per slot

  int32_t price_to_idx(int64_t price) const
  {
    return static_cast<int32_t>((price - base_price_) / tick_size_);
  }

  template <Side S>
  uint32_t find_best(uint32_t from)
  {
    if constexpr (S == Side::BUY)
      return bid_occ_.next_down(from);
    else
      return ask_occ_.next_up(from);
  }

  template <Side S>
  void match(Order& aggressor, uint32_t agg_idx, Fill* fills, uint32_t& fill_count,
             uint32_t max_fills)
  {
    uint32_t&   best = (S == Side::BUY ? best_ask_idx_ : best_bid_idx_);
    PriceLevel* levels = (S == Side::BUY ? ask_levels_ : bid_levels_);

    while (aggressor.qty > 0 && best != NULL_IDX)
    {
      if constexpr (S == Side::BUY)
      {
        if (best > agg_idx)
          break;
      }
      else
      {
        if (best < agg_idx)
          break;
      }

      PriceLevel& level = levels[best];
      while (aggressor.qty > 0 && !level.empty())
      {
        uint32_t rslot = level.head_idx;
        Order&   resting = pool_[rslot];
        uint32_t fill_qty = std::min(aggressor.qty, resting.qty);

        if (fill_count >= max_fills)
          return;
        fills[fill_count++] = Fill{.session_id = order_session_[rslot],
                                   .aggressor_id = aggressor.order_id,
                                   .resting_id = resting.order_id,
                                   .price = resting.price,
                                   .qty = fill_qty};
        level.total_qty -= fill_qty;
        aggressor.qty -= fill_qty;
        resting.qty -= fill_qty;

        if (resting.qty == 0)
        {
          level.pop_front(pool_.data());
          pool_.free(rslot);
        }
      }

      if (level.empty())
      {
        if constexpr (S == Side::BUY)
        {
          ask_occ_.clear(best);
          best = find_best<Side::SELL>(best + 1);
        }
        else
        {
          bid_occ_.clear(best);
          best = best > 0 ? find_best<Side::BUY>(best - 1) : NULL_IDX;
        }
      }
    }
  }
};

// =============================================================================
//  Tests (a tiny harness, since CoderPad has no gtest)
// =============================================================================
using Book = OrderBook</*MAX_ORDERS=*/4096, /*LADDER_SIZE=*/1024>;

// Mirrors Shard::MAX_FILLS_PER_ORDER in the repo.
static constexpr uint32_t kMaxFills = 64;

struct Result
{
  OrderHandle       handle;
  std::vector<Fill> fills;
};

// Change this helper if you change add_order's signature.
static Result submit(Book& b, uint32_t session, uint64_t id, Side side, int64_t px, uint32_t qty,
                     TimeInForce tif = TimeInForce::GTC)
{
  NewOrder m{session, id, px, qty, side, tif};
  Fill     buf[kMaxFills];
  uint32_t n = 0;
  Result   r;
  r.handle = b.add_order(m, buf, n, kMaxFills);
  r.fills.assign(buf, buf + n);
  return r;
}

static int g_failed = 0;
static int g_checks_failed_in_test = 0;
#define CHECK(cond)                                                    \
  do                                                                   \
  {                                                                    \
    if (!(cond))                                                       \
    {                                                                  \
      std::printf("    CHECK failed (line %d): %s\n", __LINE__, #cond); \
      ++g_checks_failed_in_test;                                       \
    }                                                                  \
  } while (0)

static void run(const char* name, const std::function<void()>& fn)
{
  g_checks_failed_in_test = 0;
  fn();
  if (g_checks_failed_in_test)
    ++g_failed;
  std::printf("[%s] %s\n", g_checks_failed_in_test ? "FAIL" : " ok ", name);
}

static constexpr uint32_t A = 1, B = 2;  // session ids

int main()
{
  // ------------------------------------------------------------ baseline
  run("passive orders rest", [] {
    auto b = std::make_unique<Book>(0, 1);
    submit(*b, 1, 1, Side::BUY, 100, 10);
    submit(*b, 1, 2, Side::SELL, 105, 10);
    CHECK(b->best_bid_price() == 100);
    CHECK(b->best_ask_price() == 105);
  });

  run("full match empties the book", [] {
    auto b = std::make_unique<Book>(0, 1);
    submit(*b, 1, 1, Side::SELL, 100, 10);
    auto r = submit(*b, 2, 2, Side::BUY, 100, 10);
    CHECK(r.fills.size() == 1);
    CHECK(!r.handle.valid());
    CHECK(b->best_ask_price() == INT64_MAX);
    CHECK(b->live_orders() == 0);
  });

  run("multi-level sweep fills at resting prices", [] {
    auto b = std::make_unique<Book>(0, 1);
    submit(*b, 1, 1, Side::SELL, 100, 5);
    submit(*b, 1, 2, Side::SELL, 101, 5);
    auto r = submit(*b, 2, 3, Side::BUY, 101, 8);
    CHECK(r.fills.size() == 2);
    CHECK(r.fills[0].price == 100 && r.fills[0].qty == 5);
    CHECK(r.fills[1].price == 101 && r.fills[1].qty == 3);
    CHECK(b->best_ask_price() == 101);
    CHECK(b->qty_at(Side::SELL, 101) == 2);
  });

  run("IOC remainder is not rested", [] {
    auto b = std::make_unique<Book>(0, 1);
    submit(*b, 1, 1, Side::SELL, 100, 5);
    auto r = submit(*b, 2, 2, Side::BUY, 100, 8, TimeInForce::IOC);
    CHECK(r.fills.size() == 1);
    CHECK(b->best_bid_price() == INT64_MIN);
  });

  run("cancel + stale token rejected", [] {
    auto     b = std::make_unique<Book>(0, 1);
    uint64_t t = submit(*b, 1, 1, Side::BUY, 100, 10).handle.to_token();
    CHECK(b->cancel_by_token(t));
    CHECK(b->best_bid_price() == INT64_MIN);
    CHECK(!b->cancel_by_token(t));
  });

  // ------------------------------------------------------------ PART 2
  run("PART 2: reduce keeps time priority", [] {
    auto     b = std::make_unique<Book>(0, 1);
    uint64_t t1 = submit(*b, A, 1, Side::SELL, 100, 10).handle.to_token();
    submit(*b, A, 2, Side::SELL, 100, 10);
    CHECK(b->reduce_order(t1, A, 4));
    CHECK(b->qty_at(Side::SELL, 100) == 14);
    auto r = submit(*b, B, 3, Side::BUY, 100, 6);
    CHECK(r.fills.size() == 2);
    CHECK(r.fills.size() == 2 && r.fills[0].resting_id == 1 && r.fills[0].qty == 4);
    CHECK(r.fills.size() == 2 && r.fills[1].resting_id == 2 && r.fills[1].qty == 2);
  });

  run("PART 2: reduce to zero removes the order", [] {
    auto     b = std::make_unique<Book>(0, 1);
    uint64_t t = submit(*b, A, 1, Side::BUY, 100, 10).handle.to_token();
    CHECK(b->reduce_order(t, A, 0));
    CHECK(b->best_bid_price() == INT64_MIN);
    CHECK(b->live_orders() == 0);
  });

  run("PART 2: reduce cannot increase or keep qty", [] {
    auto     b = std::make_unique<Book>(0, 1);
    uint64_t t = submit(*b, A, 1, Side::BUY, 100, 10).handle.to_token();
    CHECK(!b->reduce_order(t, A, 10));
    CHECK(!b->reduce_order(t, A, 11));
    CHECK(b->qty_at(Side::BUY, 100) == 10);
  });

  run("PART 2: other sessions cannot reduce or cancel", [] {
    auto     b = std::make_unique<Book>(0, 1);
    uint64_t t = submit(*b, A, 1, Side::BUY, 100, 10).handle.to_token();
    CHECK(!b->reduce_order(t, B, 5));
    CHECK(!b->cancel_by_token(t, B));
    CHECK(b->qty_at(Side::BUY, 100) == 10);
    CHECK(b->cancel_by_token(t, A));
  });

  // ------------------------------------------------------------ PART 3
  run("PART 3: cancel_all removes only that account's orders", [] {
    auto b = std::make_unique<Book>(0, 1);
    submit(*b, A, 1, Side::BUY, 100, 10);
    submit(*b, A, 2, Side::BUY, 99, 5);
    submit(*b, B, 3, Side::BUY, 99, 4);
    submit(*b, A, 4, Side::SELL, 105, 7);
    submit(*b, A, 5, Side::SELL, 106, 3);
    submit(*b, B, 6, Side::SELL, 106, 2);
    CHECK(b->cancel_all(A) == 4);
    CHECK(b->best_bid_price() == 99);
    CHECK(b->qty_at(Side::BUY, 99) == 4);
    CHECK(b->best_ask_price() == 106);
    CHECK(b->qty_at(Side::SELL, 106) == 2);
    CHECK(b->live_orders() == 2);
  });

  run("PART 3: cancel_all skips orders already filled or cancelled", [] {
    auto b = std::make_unique<Book>(0, 1);
    submit(*b, A, 1, Side::SELL, 100, 5);
    uint64_t t = submit(*b, A, 2, Side::SELL, 101, 5).handle.to_token();
    submit(*b, A, 3, Side::SELL, 102, 5);
    submit(*b, B, 4, Side::BUY, 100, 5);  // fills order 1
    CHECK(b->cancel_by_token(t, A));      // cancels order 2
    CHECK(b->cancel_all(A) == 1);         // only order 3 is left
    CHECK(b->best_ask_price() == INT64_MAX);
    CHECK(b->live_orders() == 0);
  });

  run("PART 3: cancel_all includes partially filled orders", [] {
    auto b = std::make_unique<Book>(0, 1);
    submit(*b, A, 1, Side::SELL, 100, 10);
    submit(*b, B, 2, Side::BUY, 100, 4);
    CHECK(b->cancel_all(A) == 1);
    CHECK(b->best_ask_price() == INT64_MAX);
  });

  run("PART 3: tokens are dead after cancel_all; account can trade again", [] {
    auto     b = std::make_unique<Book>(0, 1);
    uint64_t t = submit(*b, A, 1, Side::BUY, 100, 10).handle.to_token();
    CHECK(b->cancel_all(A) == 1);
    CHECK(!b->cancel_by_token(t, A));
    CHECK(submit(*b, A, 2, Side::BUY, 101, 1).handle.valid());
    CHECK(b->best_bid_price() == 101);
    CHECK(b->cancel_all(A) == 1);
    CHECK(b->cancel_all(A) == 0);
  });

  run("PART 3: cancel_all for an account with no orders is a no-op", [] {
    auto b = std::make_unique<Book>(0, 1);
    submit(*b, A, 1, Side::BUY, 100, 10);
    CHECK(b->cancel_all(B) == 0);
    CHECK(b->best_bid_price() == 100);
  });

  // ------------------------------------------------------------ OPTIONAL
  run("OPTIONAL: large sweep never leaves a crossed book", [] {
    auto b = std::make_unique<Book>(0, 1);
    for (uint64_t i = 0; i < 100; ++i)
      submit(*b, 1, 1000 + i, Side::SELL, 100, 1);
    auto r = submit(*b, 2, 1, Side::BUY, 100, 100);

    uint32_t filled = 0;
    for (auto& f : r.fills)
      filled += f.qty;
    CHECK(b->best_bid_price() < b->best_ask_price());  // book must never be crossed
    CHECK(filled == 100);
  });

  std::printf("\n%s (%d failing)\n", g_failed ? "FAILURES" : "ALL PASS", g_failed);
  return g_failed ? 1 : 0;
}
