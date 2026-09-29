# Lite-Exchange: CoderPad interview

Built from the candidate's repo: <https://github.com/chriskontsis/lite-exchange> (C++23 matching engine: SPSC rings, flat-array book, bitmap best-price lookup, generation-tagged cancel tokens).

The exercise uses the candidate's **own order book code**, trimmed to one file so it compiles in CoderPad. It tests whether they really understand what they built. Every bug below is present in the repo, not added for the interview.

| File | Use |
|---|---|
| `candidate.cpp` | Paste into CoderPad (C++, add `-std=c++20`). 8 tests fail at the start (Parts 2–3 plus the optional test). |
| `solution.cpp` | Reference solution, interviewer only. All 15 tests pass, plus 2 bonus tests. |

Local check: `g++ -std=c++20 -O1 -g -fsanitize=address,undefined candidate.cpp && ./a.out`

## Timeline (60 min)

| Min | Part |
|---|---|
| 0–10 | Part 1: walkthrough of their design |
| 10–25 | Part 2: implement `reduce_order` + ownership checks |
| 25–45 | Part 3: implement `cancel_all` for an account |
| 45–60 | Part 4: discussion / bonus bug |
| *if time* | **Optional:** debug the crossed book (use in place of Part 4, or for a strong candidate who finishes early) |

The optional test fails from the start. Tell the candidate up front to ignore it unless you bring it up.

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

## Optional: Debug the crossed book (15–20 min)

Test: **`OPTIONAL: large sweep never leaves a crossed book`**. 100 resting asks of qty 1 @ 100, then a buy of 100 @ 100.

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

## Scoring

| | Strong hire | Hire | No hire |
|---|---|---|---|
| Part 1 | Explains the invariants, raises the 2^24 limit unprompted | Explains the design correctly | Can't explain their own token/bitmap |
| Part 2 | Correct, keeps `total_qty` in sync, validates first | Correct after a test failure | Loses priority or skips ownership |
| Part 3 | Per-account list in side arrays, unlinks on fill, explains the liveness bug the scan exposes | Working scan with a live check, can describe the O(account) version | Scan that crashes / can't explain why |
| Part 4 | Finds the never-allocated-slot bug | Understands it once shown | — |
| Optional | Finds the crossed book fast, streams fills, discusses backpressure | Finds it with a hint, safe fix | Raises the constant / misses the crossed book |
