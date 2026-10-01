#pragma once
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

#include "lx/book/order_book.hpp"
#include "lx/proto/messages.hpp"
#include "lx/queue/spsc.hpp"

namespace lx::engine
{
// Compile-time capacities for a Shard, passed as a single class-type NTTP so
// call sites name each knob instead of listing bare numbers positionally.
struct ShardConfig
{
  uint32_t max_orders;        // shard-wide order arena (one huge-page pool)
  uint32_t ladder_size;       // price levels per symbol book
  uint32_t queue_depth;       // inbound/outbound SPSC ring slots
  uint16_t max_symbols = 1;   // instruments this shard hosts (= books allocated)
  uint16_t symbol_space = 0;  // global id range it can be addressed by; 0 = max_symbols
};

// One matching shard: owns max_symbols books over a single shared order arena,
// so a pool slot is unique shard-wide and a cancel token needs no symbol field.
// Instruments are assigned by reference data, so global symbol ids are sparse;
// a lookup table maps them to dense book slots and books scale with the
// instruments hosted, not with the id space.
//
// Books hold references into this object, so a Shard cannot be moved or copied.
template <ShardConfig CFG>
class Shard
{
 public:
  static constexpr uint16_t NO_BOOK = UINT16_MAX;
  static constexpr uint16_t SYMBOL_SPACE =
      CFG.symbol_space != 0 ? CFG.symbol_space : CFG.max_symbols;

  // An empty symbol list means the shard owns [0, max_symbols) — the identity
  // mapping, which is what the single-shard and N=1 setups want.
  Shard(int64_t base_price, int64_t tick_size, uint8_t shard_id = 0,
        std::initializer_list<uint16_t> symbols = {})
      : Shard(base_price, tick_size, shard_id, std::span<const uint16_t>{symbols})
  {
  }

  // Overload for ownership computed at runtime (a bench picking symbols per N).
  Shard(int64_t base_price, int64_t tick_size, uint8_t shard_id, std::span<const uint16_t> symbols)
      : books_(make_books(base_price, tick_size, std::make_index_sequence<CFG.max_symbols>{})),
        shard_id_(shard_id)
  {
    for (uint16_t& book : local_of_)
      book = NO_BOOK;

    if (symbols.empty())
    {
      for (uint16_t s = 0; s < CFG.max_symbols && s < SYMBOL_SPACE; ++s)
        local_of_[s] = s;
      return;
    }

    assert(symbols.size() <= CFG.max_symbols);
    uint16_t next = 0;
    for (uint16_t s : symbols)
    {
      assert(s < SYMBOL_SPACE);
      local_of_[s] = next++;
    }
  }

  Shard(const Shard&) = delete;
  Shard& operator=(const Shard&) = delete;

  void tick()
  {
    proto::InboundMsg msg{};
    if (!inbound_.pop(msg))
      return;

    uint32_t session_id = msg.hdr.session_id;  // stamped by the gateway
    switch (msg.hdr.type)
    {
      case proto::MsgType::NEW_ORDER:
        handle_new(msg.new_order, session_id);
        break;
      case proto::MsgType::CANCEL_ORDER:
        handle_cancel(msg.cancel, session_id);
        break;
      case proto::MsgType::MASS_CANCEL:  // EXERCISE (part 3)
        handle_mass_cancel(session_id);
        break;
      case proto::MsgType::TIMER:  // EXERCISE (optional B)
        handle_timer(msg.timer.now_ns);
        break;
      default:
        break;  // unknown type: drop
    }
  }

  void run()
  {
    while (running_.load(std::memory_order_relaxed))
      tick();
  }

  void stop() { running_.store(false, std::memory_order_relaxed); }

  SpscQueue<proto::InboundMsg, CFG.queue_depth>& inbound() { return inbound_; }
  SpscQueue<proto::OutboundMsg, CFG.queue_depth>& outbound() { return outbound_; }

  int64_t best_bid(uint16_t symbol = 0) const
  {
    uint16_t book = local_book(symbol);
    return book == NO_BOOK ? INT64_MIN : books_[book].best_bid_price();
  }

  int64_t best_ask(uint16_t symbol = 0) const
  {
    uint16_t book = local_book(symbol);
    return book == NO_BOOK ? INT64_MAX : books_[book].best_ask_price();
  }

 private:
  using Book = book::OrderBook<CFG.max_orders, CFG.ladder_size>;

  // Global symbol id -> dense book slot, or NO_BOOK if another shard owns it.
  uint16_t local_book(uint16_t symbol) const
  {
    return symbol < SYMBOL_SPACE ? local_of_[symbol] : NO_BOOK;
  }

  // Every book borrows pool_/order_session_, so both must already be built —
  // they are declared ahead of books_ below.
  template <std::size_t... I>
  std::array<Book, CFG.max_symbols> make_books(int64_t base_price, int64_t tick_size,
                                               std::index_sequence<I...>)
  {
    return {((void)I, Book{base_price, tick_size, pool_, order_session_, &accounts_})...};
  }

  void handle_new(const proto::NewOrder& order, uint32_t session_id)
  {
    uint16_t book = local_book(order.symbol_id);
    if (book == NO_BOOK)
    {
      emit_reject(order.order_id, proto::RejectReason::UNKNOWN_SYMBOL, session_id);
      return;
    }

    // SOLUTION (optional A): no fixed fill buffer; each fill goes straight out.
    book::OrderHandle h = books_[book].add_order(
        order, [&](const proto::Fill& f) { emit_fill(f, session_id); });

    if (h.valid())  // resting order: Ack carries the cancel token
    {
      emit_ack(order.order_id, h.to_token(shard_id_), session_id);
      if (order.expire_at != 0)  // SOLUTION (optional B)
        expiries_.push({order.expire_at, h.slot, h.gen});
    }
  }

  void handle_cancel(const proto::CancelOrder& cancel, uint32_t session_id)
  {
    book::OrderHandle h = book::OrderHandle::from_token(cancel.order_token);

    // Validate slot+gen BEFORE reading the slot's symbol: a stale token may
    // point at a recycled order now belonging to a different book.
    // SOLUTION (bonus): plus live, so a token for a free slot can't pass.
    // SOLUTION (part 2): plus ownership. Same reject as an unknown token, so a
    // probe can't learn that someone else's order exists.
    if (h.slot >= CFG.max_orders || !pool_.live(h.slot) || pool_.gen(h.slot) != h.gen ||
        order_session_[h.slot] != session_id)
    {
      emit_reject(0, proto::RejectReason::UNKNOWN_ORDER, session_id);
      return;
    }

    uint16_t book = local_book(pool_[h.slot].symbol);
    if (book == NO_BOOK || !books_[book].cancel_order(h))
    {
      emit_reject(0, proto::RejectReason::UNKNOWN_ORDER, session_id);
      return;
    }
    emit_ack(0, cancel.order_token, session_id);
  }

  // EXERCISE (part 3) -------------------------------------------------------
  // Cancel every resting order owned by session_id, across every book on this
  // shard. Emit one ACK per cancelled order to session_id, carrying that
  // order's order_id and cancel token (the same token its original ACK had).
  // This runs on cancel-on-disconnect, so think about what it costs with a
  // large arena and an account that has only a few orders.
  // SOLUTION: walks the account's own list, so O(orders the account has), not
  // O(arena). Read next before cancelling: cancel_order unlinks the node.
  void handle_mass_cancel(uint32_t session_id)
  {
    for (uint32_t slot = accounts_.first(session_id); slot != book::NULL_IDX;)
    {
      uint32_t          next = accounts_.next(slot);
      book::OrderHandle h{slot, pool_.gen(slot)};
      cancel_and_ack(h, session_id);
      slot = next;
    }
  }

  // EXERCISE (optional B) ---------------------------------------------------
  // Cancel every resting order with 0 < expire_at <= now_ns. Emit one ACK per
  // expired order to its owner, carrying its order_id and cancel token.
  // TIMER arrives often and usually has nothing to expire.
  // SOLUTION: min-heap by expiry, O(1) when nothing is due. Entries aren't
  // removed on fill/cancel; they carry the gen they were scheduled with and
  // are dropped when they surface. That gen check is also what stops a reused
  // slot from inheriting the previous order's expiry.
  void handle_timer(uint64_t now_ns)
  {
    while (!expiries_.empty() && expiries_.top().at <= now_ns)
    {
      Expiry e = expiries_.top();
      expiries_.pop();
      book::OrderHandle h{e.slot, e.gen};
      if (pool_.live(h.slot) && pool_.gen(h.slot) == h.gen)
        cancel_and_ack(h, order_session_[h.slot]);
    }
  }

  // Cancel a known-live order and ACK its owner with its order_id and token.
  void cancel_and_ack(book::OrderHandle h, uint32_t owner)
  {
    const book::Order& o = pool_[h.slot];
    uint64_t           order_id = o.order_id;  // read before the slot is freed
    uint16_t           book = local_book(o.symbol);
    if (book != NO_BOOK && books_[book].cancel_order(h))
      emit_ack(order_id, h.to_token(shard_id_), owner);
  }
  // -------------------------------------------------------------------------

  void emit_fill(const proto::Fill& fill, uint32_t aggressor_session)
  {
    // The book addressed the fill to the passive owner; send it verbatim.
    proto::OutboundMsg passive{};
    passive.fill = fill;
    push_out(passive);

    // Aggressor's copy, unless it's the same session (self-trade: one report).
    if (aggressor_session != fill.hdr.session_id)
    {
      proto::OutboundMsg agg{};
      agg.fill = fill;
      agg.hdr.session_id = aggressor_session;
      push_out(agg);
    }
  }

  void emit_ack(uint64_t order_id, uint64_t token, uint32_t session_id)
  {
    proto::OutboundMsg m{};
    m.ack.hdr = {sizeof(proto::Ack), proto::MsgType::ACK, 0};
    m.ack.hdr.session_id = session_id;
    m.ack.order_id = order_id;
    m.ack.order_token = token;
    push_out(m);
  }

  void emit_reject(uint64_t order_id, proto::RejectReason reason, uint32_t session_id)
  {
    proto::OutboundMsg m{};
    m.reject.hdr = {sizeof(proto::Reject), proto::MsgType::REJECT, 0};
    m.reject.hdr.session_id = session_id;
    m.reject.order_id = order_id;
    m.reject.reason = reason;
    push_out(m);
  }

  void push_out(const proto::OutboundMsg& m)
  {
    while (!outbound_.push(m))
      ;
  }

  book::Pool<book::Order, CFG.max_orders>        pool_;
  uint32_t                                       order_session_[CFG.max_orders]{};
  book::AccountIndex<CFG.max_orders>             accounts_;  // SOLUTION (part 3)

  // SOLUTION (optional B). expire_at lives only here, not in Order (32 bytes).
  struct Expiry
  {
    uint64_t at;
    uint32_t slot;
    uint32_t gen;
    bool     operator>(const Expiry& o) const { return at > o.at; }
  };
  std::priority_queue<Expiry, std::vector<Expiry>, std::greater<>> expiries_;
  std::array<Book, CFG.max_symbols>              books_;
  uint16_t                                       local_of_[SYMBOL_SPACE];
  SpscQueue<proto::InboundMsg, CFG.queue_depth>  inbound_;
  SpscQueue<proto::OutboundMsg, CFG.queue_depth> outbound_;
  std::atomic<bool>                              running_{true};
  uint8_t                                        shard_id_;
};
}  // namespace lx::engine
