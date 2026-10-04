# Lite-Exchange: CoderPad interview

Built from the candidate's repo: <https://github.com/chriskontsis/lite-exchange>, a C++23 matching engine with SPSC rings, a flat-array book, bitmap best-price lookup and generation-tagged cancel tokens.

The exercise is a **trimmed copy of their repo**, with the same layout, headers, tests, gtest style and compiler flags. It tests whether they understand what they built and can extend it. All the bugs discussed below are in their repo; none were planted.

| Folder | Use |
|---|---|
| `candidate/` | Upload to CoderPad as a multi-file project. `candidate/README.md` is the candidate's instructions. |
| `solution/` | Reference solution, interviewer only. Same tree with every part implemented (changes marked `SOLUTION`). |

### How close is it to their repo?

Checked file by file against their latest commit (`5fb8b77`):
- **Identical:** `util/huge_page.hpp`, `util/cache.hpp`, `queue/spsc.hpp`, `book/order.hpp`, `book/pool.hpp`, `book/price_level.hpp`, `book/level_bitmap.hpp`, `book/order_book.hpp`, `engine/shard_router.hpp`, and 6 of their test files: `book_test`, `level_bitmap_test`, `engine_test`, `shard_symbols_test`, `shard_router_test`, `spsc_test`. Their 44 tests pass.
- **Additions only, marked `EXERCISE`:**
  - `proto/messages.hpp` (+10 lines): a `MASS_CANCEL` message type and its 16-byte struct.
  - `engine/shard.hpp` (+18 lines): one `switch` case, the Part 2 TODO comment, and the `handle_mass_cancel` stub.

  No line of their code is changed or removed; the diff against their repo is additions only.
- **Left out:** the gateway/net layer, `ShardSet`, benchmarks and liburing. `CMakeLists.txt` is their root file minus those dependencies.

### Setting up the pad

1. Create a CoderPad **project** (multi-file) using C++, and upload the contents of `candidate/`.
2. Set the run command to `./run.sh`. This runs everything except the optional part. `./run.sh 'Part2*'` runs one part, and `./run.sh '*'` runs everything.
3. Run it once before the interview. Expect 51 tests to run: 45 pass (their 44, plus 1 exercise test that already passes against the stub) and 6 Part 2/3 tests fail. `./run.sh '*'` adds the optional test, which fails too. The solution passes all 54 (`./run.sh '*'` in `solution/`, which includes 2 bonus tests).

`run.sh` uses CMake if it's installed, and otherwise calls `g++` directly. It uses real GoogleTest if available, and otherwise a small shim in `third_party/mini_gtest`. Both paths were tested here (g++ 13, no GoogleTest installed). **I couldn't check CoderPad's C++ image itself.** It needs a compiler with `-std=c++23` (g++ 11+ or clang 14+); do a dry run first.

## Timeline (60 min)

| Min | Part |
|---|---|
| 0–10 | Part 1: walkthrough of their design |
| 10–20 | Part 2: cancel ownership |
| 20–45 | Part 3: mass cancel for an account |
| 45–60 | Part 4: discussion / bonus bug |
| *if time* | **Optional:** debug the crossed book |

Use the optional part in place of Part 4, or for a strong candidate who finishes early. `./run.sh` skips the optional test by default.

---

## Part 1: Walkthrough (10 min, no coding)

Ask them to explain, pointing at the code:

1. **How does `find_best` stay fast?** They should cover the occupancy bitmap with `ctz`/`clz` (`level_bitmap.hpp`). Better answers mention it's still O(ladder/64) in the worst case (a large empty region), and that `best_*_idx_` is cached, so the bitmap is only scanned when a level empties.
2. **What does each field of the cancel token do?** slot (index into the arena, so no hash map), gen (ABA guard), shard (routing, because a cancel carries no symbol).
3. **Why is `total_qty -= o.qty` in `PriceLevel::pop_front` correct during matching, when `match` already subtracted `fill_qty`?** By the time `pop_front` runs, `resting.qty == 0`, so it subtracts 0. This checks whether they understand the invariant or just wrote it.
4. **Why are books templated on `MAX_ORDERS` / `LADDER_SIZE`?** Fixed layout, no allocation on the hot path. Follow-up: *"What's the largest `max_orders` the token supports?"* The answer is 2^24, because the slot has 24 bits. There's no `static_assert`, so a larger arena would make tokens silently point at the wrong slot.

## Part 2: Cancel ownership (10 min)

Test: `test/exercise/part2_test.cc`. In `Shard::handle_cancel`, only the session that entered an order may cancel it. Their repo has no such check. This is a short warm-up for Part 3, which works in the same file with the same data.

**What to look for:**
- They reuse `order_session_[slot]`, which already exists for routing passive fills, and they check it **after** validating slot + gen, as the existing comment in `handle_cancel` says.
- **Strong signal:** they reject a cancel from the wrong owner with the same `UNKNOWN_ORDER` as a bad token, so a probe can't learn that someone else's order exists.

**Why ownership matters:** tokens are `slot | gen << 24`, and early in the day most slots are at gen 0, so another firm's tokens are easy to guess. Ask whether they consider the missing check a security bug (yes).

## Part 3: Mass cancel for an account (25 min)

Tests: `test/exercise/part3_test.cc`. Implement `Shard::handle_mass_cancel(session_id)`: cancel every resting order the account has **on any book on this shard**, and ACK each one with its `order_id` and token. Real venues run this on **cancel-on-disconnect** and for risk kill switches. In this exercise an account is its `session_id`.

This builds directly on their design: one shard's books share one arena (their README's "Shared arena, per-instrument books" section), so one account's orders are spread across several books.

**What happens with the obvious approach:** scan every slot and cancel wherever `order_session_[slot] == session_id`. It **fails** `SkipsOrdersAlreadyFilledOrCancelled` with 3 ACKs instead of 1, and `TokensDeadAfterMassCancel...` keeps ACKing after everything is gone. Freed slots keep the old owner's `session_id` and their current generation, so the scan "cancels" orders that no longer exist and frees their slots a second time. That's the Part 4 bonus bug appearing on its own; let them find it.
- **Worth pointing out:** ASan does *not* catch the double free. The arena is `mmap`'d (`huge_page.hpp`), which ASan doesn't track, so the free list is silently corrupted. Ask what that means for their Debug-build safety net.
- Good candidates see that the pool can't tell a live slot from a free one, and add a live flag (or gen parity).

**Complexity, the main discussion:** *"1M-slot arena, the account has 3 orders, and this runs on every disconnect. What does it cost?"*
- **Scan:** O(arena). Fine as a first version if they say the cost out loud.
- **Better (`solution/`):** a per-account doubly linked list threaded through **slot-indexed side arrays** (`book/account_index.hpp`), owned by the shard and borrowed by every book, the same pattern as their `pool_` and `order_session_`. Link when an order rests; unlink wherever a resting order is freed: `cancel_order` **and on a full fill inside `match`**. Mass cancel is then O(orders the account has).
- **Missing the unlink on fill is the classic bug.** It's caught by `FilledOrdersSlotReusedByAnotherAccountIsNotCancelled`: the filled order's slot stays on A's list, B reuses the slot, and A's mass cancel walks into B's orders. We checked this by removing the unlink from the solution, and exactly that test fails.
- **Why side arrays instead of new fields on `Order`?** `order.hpp` has `static_assert(sizeof(Order) == 32)`, which keeps two orders per cache line for matching. Strong candidates raise this themselves.
- **Iterating while cancelling:** read `next` before `cancel_order`, which unlinks the node.

**Follow-ups:**
- **Their router drops it.** `ShardRouter::push` returns `true` for any type that isn't `NEW_ORDER` or `CANCEL_ORDER` ("unknown type: skip"), so a `MASS_CANCEL` would never reach a shard. It has to be **broadcast** to every shard's ring. Then ask what the client should see: one ACK per order (as here), or one summary per shard. Also, an order can fill on shard 2 while the mass cancel is still on its way there.
- **Why a message and not a direct call from the gateway on disconnect?** A direct call would be a data race with the shard thread, and the SPSC rings are what keep the shard single-threaded.
- **Heads:** `unordered_map` allocates the first time an account rests an order. How would they avoid that? Dense account ids assigned at logon, plus a flat array.
- **Zero orders:** the spec emits nothing for an account with no orders. Should the client get a confirmation anyway?

## Part 4: Discussion / bonus (10–15 min)

**Bonus bug** (if the Part 3 scan didn't already expose it): *"Is there a token that passes the gen check but doesn't point to a live order?"*

Yes. `Pool::free` increments `gen`, so a slot that was **never allocated** still has `gen == 0`.
- **On their real code, one `CANCEL_ORDER` with token `0` crashes the shard.** We verified it: ASan reports a SEGV in `PriceLevel::remove`. Slot 0's zeroed `Order` has price 0, which is below `base_price`, so `price_to_idx` goes negative and indexes far outside the ladder. That's a remote, one-message crash of the matching engine.
- When the price does land on the ladder, the forged cancel instead unlinks a non-existent order (which can wipe out the level's `head_idx`) and frees a slot that's already free. Two later orders then share one slot.
- `(slot, gen + 1)` works the same way for any slot that has been freed.
- **Fix:** a live flag per slot (`solution/include/lx/book/pool.hpp`), or gen parity (odd = live, no extra memory), plus `assert(live)` in `free`. The `Bonus` tests in `solution/` cover both cases.

**Other points to raise if there's time:**
- **Off-tick prices.** `price_to_idx` uses truncating division. An off-tick price is silently rounded to a level, but the `Order` keeps the original price, so fills report a price that isn't on the ladder. A price just *below* `base_price` (e.g. `base - tick/2`) rounds toward zero to index 0 and is **accepted**. A large price difference can also overflow `int32`. `RejectReason::INVALID_PRICE` exists but is never used.
- **`qty == 0`** is never validated (`INVALID_QTY` is also unused).
- **SPSC queue.** The memory ordering is correct, but `push`/`pop` load the other side's atomic on every call, which moves that cache line between cores. Their own benchmark shows ring transit is 87% of latency. How would they reduce it? Cache the other side's index locally and reload only when the ring looks full or empty; batch pops.
- **Gateway `send_all`** (in their repo, not this pad). On a non-blocking socket it gives up on `EAGAIN` partway through a frame, so a slow client gets a torn message and every later frame is misaligned.

## Optional: Debug the crossed book (15–20 min)

Test: `Optional.LargeSweepNeverLeavesACrossedBook` (`./run.sh 'Optional*'`). Account A rests 100 asks of qty 1 @ 105, then account B buys 100 @ 105.

**Root cause:** `Shard::handle_new` passes a 64-entry fill buffer (`MAX_FILLS_PER_ORDER`), and `match()` does `if (fill_count >= max_fills) return;`. Matching stops after 64 fills, then `add_order` sees `o.qty > 0` and **rests the leftover 36 as a bid at 105 while 36 asks at 105 are still on the book**. The book is crossed.

**Hints, if they get stuck:**
1. "Print `shard.best_bid(0)`, `shard.best_ask(0)` and the number of fills after the sweep."
2. "Why exactly 64?"

**Fixes, from best to acceptable:**
- **Stream fills to a sink** (`solution/`): `add_order` takes a callable, and the shard passes `emit_fill`, so there's no cap. The solution keeps the array overload so their `book_test.cc` still compiles.
- Stop matching and **cancel (don't rest) the remainder**, reporting a partial fill. Safe, but it changes the order's behaviour; they should say so.
- Resumable matching: works, but it's a lot of complexity.

**Red flags:** raising the constant to 1024 (that only moves the bug), or not noticing the book is crossed.

**Follow-up:** the sink pushes into an SPSC ring that can be full, and `Shard::push_out` busy-spins, so one slow gateway stalls matching for every symbol on the shard. Discuss backpressure: reject new orders above a high-water mark, or size the rings for the worst burst.

## Scoring

| | Strong hire | Hire | No hire |
|---|---|---|---|
| Part 1 | Explains the invariants, raises the 2^24 limit unprompted | Explains the design correctly | Can't explain their own token/bitmap |
| Part 2 | Checks after validating slot + gen, same reject for wrong owner, explains why guessable tokens make this a security bug | Correct after a test failure | Checks before validating the token, or can't say why it matters |
| Part 3 | Shard-wide per-account list in side arrays, unlinks on fill, explains the liveness bug the scan exposes, spots the router drop | Working scan plus a live check, can describe the O(account) version | Scan that corrupts the pool / can't explain why |
| Part 4 | Finds the never-allocated-slot bug | Understands it once shown | — |
| Optional | Finds the crossed book fast, streams fills, discusses backpressure | Finds it with a hint, safe fix | Raises the constant / misses the crossed book |
