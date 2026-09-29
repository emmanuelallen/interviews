# Lite-Exchange: CoderPad interview

Built from the candidate's repo: <https://github.com/chriskontsis/lite-exchange> (C++23 matching engine: SPSC rings, flat-array book, bitmap best-price lookup, generation-tagged cancel tokens).

The exercise uses the candidate's **own order book code**, trimmed to one file so it compiles in CoderPad. It tests whether they really understand what they built. Every bug below is present in the repo, not added for the interview.

| File | Use |
|---|---|
| `candidate.cpp` | Paste into CoderPad (C++, add `-std=c++20`). 12 tests fail at the start (Parts 2–3 plus the optional tests). |
| `solution.cpp` | Reference solution, interviewer only. All 20 tests pass, plus 2 bonus tests. |

Local check: `g++ -std=c++20 -O1 -g -fsanitize=address,undefined candidate.cpp && ./a.out`

## Timeline (60 min)

| Min | Part |
|---|---|
| 0–10 | Part 1: walkthrough of their design |
| 10–25 | Part 2: implement `reduce_order` + ownership checks |
| 25–45 | Part 3: implement `cancel_all` for an account |
| 45–60 | Part 4: discussion / bonus bug |
| *if time* | **Optional A:** debug the crossed book |
| *if time* | **Optional B:** support arbitrary order expiry times |

Use an optional part in place of Part 4, or for a strong candidate who finishes early. Optional A is debugging; Optional B is design + implementation and fits well after Part 3. The optional tests fail from the start, so tell the candidate up front to ignore any test named `OPTIONAL` unless you bring it up.

---

## Part 1: Walkthrough (10 min, no coding)

Ask them to explain, pointing at the code:

1. **How does `find_best` stay fast?** They should cover the occupancy bitmap with `ctz`/`clz`. Better answers mention that it's still O(ladder/64) in the worst case (an empty region), and that `best_*_idx_` is cached, so the bitmap is only scanned when a level empties.
2. **What does each field of the cancel token do?** slot (index into the arena, so no hash map), gen (ABA guard), shard (routing, because a cancel carries no symbol).
3. **Why is `total_qty -= o.qty` in `pop_front` correct during matching, given that `match` already subtracted `fill_qty` from `level.total_qty`?** By the time `pop_front` runs, `resting.qty == 0`, so it subtracts 0. This checks whether they understand the invariant or just wrote it.
4. **Why is the book templated on `MAX_ORDERS` / `LADDER_SIZE`?** Fixed layout, no allocation on the hot path. Follow-up: *"What's the largest `MAX_ORDERS` the token supports?"* The answer is 2^24 (the slot has 24 bits). There's no `static_assert`, so a larger arena would make tokens silently point at the wrong slot. A good candidate will say this should be enforced at compile time.

## Part 2: `reduce_order` + ownership (15 min)

Implement the two TODOs:
- `cancel_by_token(token, session_id)`: only the owning session may cancel.
- `reduce_order(token, session_id, new_qty)`: reduce in place **without losing time priority**; `new_qty == 0` works like a cancel; `new_qty >= qty` is rejected.

**What to look for:**
- They reuse `order_session_[slot]` (it already exists for routing passive fills).
- They update **`level.total_qty`** as well as `o.qty`. Test *"reduce keeps time priority"* checks `qty_at == 14`. Missing this is the most common bug.
- They don't unlink/re-link the order (that would lose priority), and they can explain why an *increase* must lose priority (it's unfair to orders queued behind it).
- They validate the token (slot range + gen) **before** reading `order_session_` or the `Order`, as the repo's `handle_cancel` comment says.

**Why ownership matters here:** tokens are `slot | gen << 24`. Early in the day most slots are at gen 0, so any client can guess another firm's tokens and cancel their orders. The repo's `Shard::handle_cancel` does no ownership check. Ask whether they'd consider that a security bug (yes).

## Part 3: `cancel_all` for an account (20 min)

Implement `cancel_all(session_id)`: cancel every resting order for an account (both sides, all levels) and return the count. Real venues run this on **cancel-on-disconnect** and for risk kill switches, so it needs to be correct under load. In this exercise, an account is identified by its `session_id`.

**What happens with the obvious approach:** scan every slot, and call `cancel_order` wherever `order_session_[slot] == session_id`. **This crashes under ASan** (heap-buffer-overflow in `Pool::free`). Freed slots keep the previous owner's `session_id`, and their current generation matches, so the scan "cancels" orders that are already gone and frees them again. This is the Part 4 bonus bug appearing naturally. Let them find it. The test *"cancel_all skips orders already filled or cancelled"* and the sanitizer will point at it. Good candidates see that the pool can't tell whether a slot is in use, and add a live flag (or gen parity).

**Complexity, the main discussion:** once the scan works, ask *"1M-slot arena, the account has 3 orders, it runs on every disconnect. What does this cost?"*
- **Scan:** O(arena), which is fine as a first version if they say the cost out loud.
- **Better (`solution.cpp`):** a per-account doubly linked list of resting orders, stored in **side arrays** indexed by slot (`acct_next_` / `acct_prev_`) plus a head per account. Link when an order rests; unlink wherever a resting order is freed: `cancel_order` **and when it's fully filled in `match`**. Missing that second place is the classic bug. `cancel_all` is then O(orders the account has).
- **Why side arrays instead of new fields on `Order`?** The repo's `static_assert(sizeof(Order) == 32)` keeps two orders per cache line for matching. Adding 8 bytes of account links to the hot struct would hurt matching to speed up a rare operation. Strong candidates raise this themselves.
- **Iterating while cancelling:** read `next` before calling `cancel_order`, which unlinks the node.

**Follow-ups:**
- Head storage: `unordered_map` allocates the first time an account rests an order. Ask how they'd avoid that (dense account ids from logon → flat array).
- Many shards: the account's orders are spread across shards, so `cancel_all` has to be broadcast to every shard's inbound ring. What should the client see: one ack per shard, or a single aggregated ack? Also, an order can fill in shard 2 while the cancel is still on its way there.
- Ordering: `cancel_all` goes through the same inbound ring as new orders, so it can't overtake an order sent before it. Does the gateway need to block new orders from that session until the cancel is processed?

## Part 4: Discussion / bonus (10–15 min)

**Bonus bug (if the Part 3 scan didn't already expose it):** *"Is there a token that passes the `gen` check but doesn't point to a live order?"*

Yes. `Pool::free` increments `gen`, and a slot that was **never allocated** has `gen == 0`. So token `0` (slot 0, gen 0) on a fresh book passes validation. `cancel_order` then unlinks a zeroed `Order` from a level (which can wipe out `head_idx`, and underflows `order_count`) and calls `free(0)` on a slot that is already on the free list. After that, **two future orders get the same slot**. We confirmed this on the original code: after a forged cancel of token `0`, the next allocations include slot 0, and slot 0 is now on the free list twice. The same applies to `(slot, gen+1)` for any slot that has been freed.
Fix: a live bit per slot (`solution.cpp`), or gen parity (odd = live, 1 bit, no extra memory), plus `assert(live)` in `free`.

**Other points to raise if there's time:**
- `price_to_idx` uses truncating division. A price not on the tick is silently rounded to a level, but the `Order` keeps the original price, so the fill reports a price that isn't on the ladder. A price just *below* `base_price` (e.g. `base - tick/2`) rounds toward zero to idx 0 and is **accepted**. There's also a possible `int32` overflow for large price differences. The fix: reject if `(price - base) % tick != 0` (the `RejectReason::INVALID_PRICE` enum value already exists but is never used).
- A `qty == 0` order is never validated (`INVALID_QTY` is also unused).
- **SPSC queue:** the memory ordering is correct, but `push`/`pop` load the other side's atomic every call, which moves that cache line between cores. Their own benchmark shows ring transit is 87% of latency. Ask how they'd reduce it: cache the other side's index locally and only reload it when the ring looks full/empty, and batch pops.
- **Gateway `send_all`** on a non-blocking fd returns on `EAGAIN` in the middle of a frame, so a slow client receives a torn message and every later frame is misaligned. Discuss per-session send buffers / disconnecting slow consumers.

## Optional A: Debug the crossed book (15–20 min)

Test: **`OPTIONAL A: large sweep never leaves a crossed book`**. 100 resting asks of qty 1 @ 100, then a buy of 100 @ 100.

**Root cause:** `match()` does `if (fill_count >= max_fills) return;`. With `MAX_FILLS_PER_ORDER = 64` (the same value as `Shard` in the repo), matching stops after 64 fills. `add_order` then sees `o.qty > 0` and **rests the leftover 36 as a bid at 100 while 36 asks at 100 are still on the book**. The book is crossed, and the next incoming order will produce nonsense.

**Hints, if they get stuck:**
1. "Print `best_bid_price()`, `best_ask_price()` and `r.fills.size()` after the sweep."
2. "Why exactly 64?"

**Acceptable fixes (from best to acceptable):**
- **Stream fills to a sink/callback** (see `solution.cpp`) so there's no cap. In the real `Shard` the sink is `emit_fill` → outbound ring. This is the best answer, and it also removes the 64 × 40 B stack buffer.
- Stop matching and **cancel (don't rest) the remainder**, reporting it as partially filled. This is safe, but it changes what the order does. The candidate should say that out loud.
- Make matching resumable. This works but is a lot of complexity; probe whether it's worth it.

**Red flags:** increasing `MAX_FILLS` to 1024 (this only moves the bug), or not noticing that the book is crossed and focusing only on the `filled == 100` check.

**Follow-up:** "The sink pushes into an SPSC ring that can be full. What happens then?" `Shard::push_out` busy-spins, so one slow gateway stalls matching for every symbol on that shard. Discuss backpressure: reject new orders when the outbound ring is above a high-water mark, size the rings to the worst-case burst, etc.

## Optional B: Arbitrary expiry times (20 min)

Their engine only has `GTC` (rests until filled or cancelled) and `IOC` (the leftover is dropped straight away), so orders never expire. This part adds **good-till-time** orders: `NewOrder::expire_at` is an absolute time in ns (`0` = never). The candidate implements `expire(now)`, which cancels every resting order with `0 < expire_at <= now` and returns how many it cancelled. The engine calls it every time its clock advances, so it's usually called with nothing due.

**The trap:** the natural first move is to add `expire_at` to `Order`. The candidate file keeps the repo's `static_assert(sizeof(Order) == 32)`, so that no longer compiles. Watch whether they remove the assert (a red flag: it's their own design decision) or store the expiry elsewhere. Ask why the assert exists: two orders per cache line during matching.

**Approaches, from weak to strong:**
- **Scan all slots on every `expire`:** O(arena) on every clock tick, even when nothing is due. Also runs into the same liveness problem as the Part 3 scan.
- **Min-heap of `(expire_at, slot, gen)` (`solution.cpp`):** O(1) when nothing is due, O(log n) per order scheduled. Entries aren't removed when an order fills or is cancelled; they're skipped when they reach the top because the gen no longer matches. **Storing the gen in the entry is essential**: the test *"a reused slot does not inherit the old expiry"* fails if the entry holds only the slot, because the slot gets reused by an order that never expires. We checked this: slot-only tracking fails exactly that test.
- Strong candidates raise the cost of skipping stale entries later: a cancelled order with an expiry far in the future leaves its heap entry behind, so under heavy cancel churn the heap can grow well past the number of live orders. Fixes: an **indexed heap** (store each slot's heap position and remove it on fill/cancel, O(log n)), rebuilding the heap when stale entries exceed half, or a **timing wheel** (buckets per ms/second, O(1) insert/remove, good when many orders share an expiry like end of day).
- `std::priority_queue` allocates as it grows. Ask how they'd keep it off the hot path: reserve capacity up front, or a fixed-size heap over the arena.

**Follow-ups (the key design questions):**
- **Where does `now` come from?** If each shard reads its own clock, replaying the same input can expire different orders, so the result is not deterministic. Better: the gateway/sequencer puts a timestamp or timer message on the inbound ring, so expiry happens at a defined point in the message stream and replays match.
- **Can an order trade after its expiry time?** Between clock updates, an order whose time has passed can still be matched. Options: call `expire(msg_time)` before processing every message, or have `match` check and skip expired resting orders on the way. Ask them to pick one and explain the cost.
- **Should an order that is already expired when it arrives be accepted?** No: reject it (`expire_at <= now`) instead of letting it rest for one tick.
- **Notifications:** each expiry should send the owner an unsolicited cancel message through the outbound ring, like `cancel_all` does.
- `DAY` orders are the special case where almost every order shares one expiry time; a separate list per session end is O(1) per order and beats the heap.

## Scoring

| | Strong hire | Hire | No hire |
|---|---|---|---|
| Part 1 | Explains the invariants, raises the 2^24 limit unprompted | Explains the design correctly | Can't explain their own token/bitmap |
| Part 2 | Correct, keeps `total_qty` in sync, validates first | Correct after a test failure | Loses priority or skips ownership |
| Part 3 | Per-account list in side arrays, unlinks on fill, explains the liveness bug the scan exposes | Working scan with a live check, can describe the O(account) version | Scan that crashes / can't explain why |
| Part 4 | Finds the never-allocated-slot bug | Understands it once shown | — |
| Optional A | Finds the crossed book fast, streams fills, discusses backpressure | Finds it with a hint, safe fix | Raises the constant / misses the crossed book |
| Optional B | Keeps the 32-byte `Order`, heap with gen (or wheel), raises stale-entry growth and clock determinism | Working heap/sorted structure after a hint about slot reuse | Removes the `static_assert`, or scans the arena every tick |
