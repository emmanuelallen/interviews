#pragma once
#include <cstdint>
#include <type_traits>

namespace lx::proto
{
// ---------- Wire Types ----------
enum class MsgType : uint8_t
{
  LOGON = 0x01,
  NEW_ORDER = 0x02,
  CANCEL_ORDER = 0x03,
  MASS_CANCEL = 0x04,  // EXERCISE (part 3): cancel all of the sender's orders
  TIMER = 0x05,        // EXERCISE (optional B): engine clock tick
  ACK = 0x81,
  REJECT = 0x82,
  FILL = 0x83,
};

enum class Side : uint8_t
{
  BUY = 0,
  SELL = 1
};

enum class TimeInForce : uint8_t
{
  GTC = 0,
  IOC = 1
};

enum class RejectReason : uint8_t
{
  UNKNOWN_SYMBOL = 0,
  INVALID_PRICE = 1,
  INVALID_QTY = 2,
  UNKNOWN_ORDER = 3  // cancel token names no live order (stale or forged)
};

// Fields ordered for natural alignment with no padding (verified below).
// session_id is server-assigned: the gateway stamps it on ingress and the
// engine echoes it on every outbound so replies route back to the client.
struct Header
{
  uint16_t len;
  MsgType  type;
  uint8_t  flags;
  uint32_t session_id = 0;
};

struct Logon
{
  Header   hdr;
  uint64_t padding;
};

struct NewOrder
{
  Header      hdr;
  uint64_t    order_id;
  int64_t     price;
  uint32_t    qty;
  uint16_t    symbol_id;
  Side        side;
  TimeInForce tif;
  uint64_t    expire_at;  // EXERCISE (optional B): absolute ns; 0 = never expires
};

struct CancelOrder
{
  Header   hdr;
  uint64_t order_token;  // exchange-assigned
};

// EXERCISE (part 3). The account is the session stamped in hdr.session_id.
struct MassCancel
{
  Header   hdr;
  uint64_t padding;
};

// EXERCISE (optional B). Engine-internal: the sequencer injects one whenever
// the engine clock advances, so expiry happens at a defined point in the stream.
struct Timer
{
  Header   hdr;
  uint64_t now_ns;
};

struct Ack
{
  Header   hdr;
  uint64_t order_id;     // client's
  uint64_t order_token;  // exchange handle for cancels
};

struct Reject
{
  Header       hdr;
  uint64_t     order_id;
  RejectReason reason;
  uint8_t      padding[7];
};

struct Fill
{
  Header   hdr;
  uint64_t aggressor_id;
  uint64_t resting_id;
  int64_t  price;
  uint32_t qty;
  uint32_t padding = 0;
};

// hdr.type is readable through any member (common initial sequence). New orders
// and cancels share one queue so a cancel can't overtake the order it targets.
union InboundMsg
{
  Header      hdr;
  NewOrder    new_order;
  CancelOrder cancel;
  MassCancel  mass_cancel;  // EXERCISE
  Timer       timer;        // EXERCISE
};

union OutboundMsg
{
  Header hdr;
  Ack    ack;
  Reject reject;
  Fill   fill;
};

// ---- Compile-time layout verification ----
static_assert(sizeof(Header) == 8);
static_assert(sizeof(Logon) == 16);
static_assert(sizeof(NewOrder) == 40);  // EXERCISE: was 32 before expire_at
static_assert(sizeof(MassCancel) == 16);
static_assert(sizeof(Timer) == 16);
static_assert(sizeof(CancelOrder) == 16);
static_assert(sizeof(Ack) == 24);
static_assert(sizeof(Reject) == 24);
static_assert(sizeof(Fill) == 40);

static_assert(std::is_trivially_copyable_v<NewOrder>);
static_assert(std::is_trivially_copyable_v<Fill>);

}  // namespace lx::proto
