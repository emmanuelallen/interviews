# Interview: harpoon (candidate's FIX parser)

**Source:** https://github.com/yutaoz/harpoon, a header-only C++23 streaming FIX
parser with AVX2 scanning. The README claims about 5.75M msgs/s (1.2 GB/s).

**Format:** 60 minutes in a multi-file CoderPad project, language **C++**
(C++20 or later).

| Path | Use |
|---|---|
| `starter/` | Upload this whole folder to CoderPad. It's the candidate's repo with their files **unchanged** (copied from `main` at commit `2b81a7d`), plus two new files: `tests/tests.cc` (the test harness) and `harpoon/price.h` (a stub for Task 6). `CMakeLists.txt` is theirs with a `tests` target added at the end. |
| `solution/` | Reference solution (interviewer only), the same tree with fixes. All 14 tests pass, and it's clean under ASan/UBSan. Every change is marked `[Task N]`. |
| `solution.diff` | `starter/` → `solution/` as a diff, for reading the changes quickly. |

**Running the tests** (from the project root):

```sh
g++ -std=c++20 -O2 -mavx2 -I. tests/tests.cc -o tests && ./tests
```

Or with CMake: `cmake -B build && cmake --build build && ./build/tests`.

- In the starter, tests 1–3 pass and 4–12 fail. Test 13 then **crashes**
  (segfault) until Task 5 is fixed; results for tests 1–12 print first.
- The candidate's SIMD code is kept as-is, so the CoderPad machine needs
  AVX2. If it doesn't, the tests die with "Illegal instruction" before
  printing anything. Check that beforehand with `grep -c avx2 /proc/cpuinfo`.
- Compiler warnings from `dispatch.h` (unused parameters) come from their
  code. Leave them; they lead into discussion item 1.

**Structure:** the core is Tasks 1–3 (tests 1–9). Tasks 4–6 (tests 10–14) are
stretch tasks. Pick one or two based on how fast the candidate is and what
you want to learn. The tests run in file order (10–11, then 12, then 13–14),
which isn't task order: Task 5's tests run last because they can crash.

The idea is to have them harden **their own design**. Because they already
know the code, you skip the ramp-up and learn quickly whether they wrote and
understand it.

---

## 0. Warm-up / ownership check (5–10 min, no coding)

Share their repo or the starter and ask:

1. *"Walk me through what happens when `feed()` gets half a message."*
   Expected: bytes are appended to `pending`, `try_parse_one` returns `false`
   at `message_end > n`, and the bytes stay buffered until the next `feed`.
2. *"If there's no `8=` in the buffer, why do you consume `n - 1` bytes and
   not `n`?"* Expected: the last byte could be the `8` of an `8=` that
   continues in the next chunk.
3. *"How does `find_begin_avx2` work?"* Expected: `cmpeq` + `movemask` finds
   every `'8'` in 32 bytes, then it checks the next byte for `'='` and uses
   `mask &= mask - 1` to step to the next candidate.
4. *"Where did 5.75M msgs/s come from, and what's excluded?"* Good answers
   mention warm cache, a synthetic feed, that the handler only looks at one
   tag, and that the timing excludes file I/O.

🚩 If they can't explain items 1–3, the code may not be theirs. Dig deeper.

---

## Task 1: checksum (≈10 min) → test 4

> "Your parser checks that `10=` is three digits but never checks the value.
> Make a message with a bad checksum get dropped, and keep the stream going."

- Checksum = sum of every byte before `"10="`, mod 256.
- **Probe:** *"After a bad checksum, do you skip 1 byte or the whole
  `message_end`?"* Skipping 1 byte is safer. The mismatch might mean
  BodyLength itself was corrupt, so trusting it could jump past a good
  message. Both are acceptable **if they give that reason**.
- **Perf probe:** *"This adds a second pass over every message. How would you
  make it cheap?"* Options: SIMD horizontal sum (`_mm256_sad_epu8` against
  zero), or combining it with the field scan.

## Task 2: hostile / corrupt header (≈15 min) → tests 5, 6, 7

> "A bad header should never stall the stream. Assume no real message is
> larger than 64 KiB."

Three separate bugs:
- **Test 5:** `9=999999999` means `message_end > n` is always true, so the
  parser returns "need more bytes" forever. `pending` then grows without
  limit (memory DoS), and every later message is lost.
- **Test 6:** `parse_uint_ascii` has no overflow check. The test builds
  `2^64 + offset` so the wrapped length lands exactly on the **next**
  message's `10=`. That produces one "valid" frame that swallows a real
  message. Ask: *"Is this exploitable if it's an exchange feed?"*
- **Test 7:** 20,000 copies of `8=` right before a real message. The parser
  treats the whole run as one oversized BeginString and joins it onto the
  real message's `9=`. Without a checksum (the starter) that frame is
  delivered right away. **With the Task 1 checksum it still gets
  through**: resync skips one byte at a time, and the 8-bit checksum
  matches by chance within 256 tries, so a fix that only adds the checksum
  fails this test.
  The fix: require `8=FIX` and cap how far to look for the SOH (after
  `8=` and after `9=`). That also stops "`8=` then no SOH, ever" from
  buffering forever.

A strong fix adds a max-length cap and does an overflow-safe parse
(`value > (max - d) / 10`).

**Probe:** *"What should the cap be, and should it be configurable?"*
Probe: *"Test 5 and test 6 feed 64-byte chunks. Why does that matter?"*

## Task 3: malformed fields & callback contract (≈10–15 min) → tests 8, 9

> "Every `on_message_begin` must be followed by exactly one
> `on_message_end` or `on_message_error`. Tags must be non-empty digits."

- The bug: `parse_message` scans for `'='` **without stopping at SOH**, so
  `55<SOH>44=1.5` becomes the tag `"55\x0144"`. Empty tag `=oops` is
  passed to the handler as-is.
- The original also `return`s without calling `on_message_end`, leaving the
  handler in a "message open" state.
- **Breaking change probe:** adding `on_message_error` breaks every
  existing handler, including the `Sum5013Handler` in their own
  `examples/fileparse.cc` (it no longer compiles). Ask: *"Who else uses this
  interface, and how would you add a callback without breaking them?"*
  The solution adds an empty method to the example. A stronger answer
  makes the callback optional with `if constexpr (requires {
  handler.on_message_error(); })`.
- **Design probe:** *"The handler has already seen `35=D` before you find
  the error. Is that ok? What would you change if the handler was placing
  orders?"* Options: validate in a first pass before calling handlers, or
  buffer fields and emit on success. Discuss the latency tradeoff.

---

## Stretch tasks (pick 1–2)

Every stretch task comes from a real gap in the candidate's repo. If the
candidate is fast, **Task 5 gives the most signal**.

### Task 4: RawData (≈10 min) → tests 10, 11

> "Tag 95 gives the exact length of the tag 96 value that follows, and that
> value can contain SOH and `=`. Make your field loop handle it."

- It tests whether they can change the field loop from "scan to the
  delimiter" into a small state machine. This is the general form of FIX
  repeating groups and length-prefixed fields.
- Edge cases worth hearing: 95 not followed by 96, 95 as the last field,
  a length past the end of the message (test 11), and a length that overflows.
- In the solution, field 95 reaches the handler and the error comes on 96.
  That matches the existing contract: earlier fields have already been
  emitted by the time an error is found (see the Task 3 design probe).
- **Probe:** *"How would you support every length/data pair (e.g.
  `EncodedTextLen`/`EncodedText`, 354/355) without hard-coding each?"*
  Expected: a table of length tag → data tag, filled from the FIX dictionary.

### Task 5: re-entrant `feed()` (≈15 min) → tests 13, 14

> "A handler may call `feed()` on the same parser from inside a callback,
> for example to replay a buffered message. The fed bytes must be parsed
> after the current message, in order, and each message delivered exactly
> once."

- **What actually breaks in their code** (ask them to predict before running):
  1. The inner `feed` calls `parse_pending` while the outer call is still
     inside `try_parse_one`. `read_pos` hasn't moved yet, so the in-flight
     message is **parsed a second time**.
  2. The inner call eventually clears `pending` (`read_pos == size`). Then
     the outer call does `read_pos += consumed` on an empty buffer.
  3. `pending.insert` can reallocate, leaving the outer `parse_message`
     reading through dangling `p`/`end` pointers. Test 14 feeds 20 KB to
     force that reallocation. The starter usually segfaults on test 13.
- **Solution:** a `parsing` flag. Bytes fed while parsing go into a separate
  `deferred` buffer, which is drained after `parse_pending` returns.
- **Follow-ups:**
  - *"What if the handler throws?"* `parsing` stays `true` forever. Use an
    RAII guard, or document that handlers must be `noexcept`.
  - *"Could you just ban re-entrancy instead?"* Yes, with an assert. That's
    a fine answer if they explain why and document it.
  - *"What's the lifetime of the `tag`/`value` pointers?"* Valid only for the
    duration of the callback. The handler must copy them if it keeps them.

### Task 6: fixed-point prices (≈10 min) → test 12

> "Your example sums prices as `double` with `scale *= 0.1`. Implement
> `parse_price` (in `harpoon/price.h`) to return an `int64_t` with 8
> implied decimals."

- Tests cover signs, the full `int64_t` range (the max passes and one more
  overflows), more than 8 decimals, and malformed inputs (`"1."`, `".5"`,
  `"+1"`, `"1e5"`).
- Good answers do an overflow check on each digit (not only at the end) and
  pad the missing decimals by multiplying by 10.
- **Probe:** *"Why not just `llround(double * 1e8)`?"* Doubles have about
  15–17 significant digits, so large prices lose their last cents. Rounding
  is also lossy. *"Why 8 decimals?"* Crypto prices commonly have 8
  (e.g. 1 satoshi = 1e-8 BTC); the right value depends on the instrument.

---

## Discussion questions (remaining time, pick any)

These are real issues in the candidate's repo (`main` branch). Each one lists
what a strong answer covers.

1. **ODR / "header-only" claim.** *"You call this header-only. What happens
   if two .cc files include `harpoon.h`?"*
   - `dispatch_field` in `harpoon/dispatch.h` is a non-`inline`, non-template
     function defined in a header, so the link fails with *multiple
     definition of `dispatch_field`*.
   - Fix: mark it `inline` (or `static`, which gives each TU its own copy),
     or delete it since it's dead code (the call is commented out).
   - Strong: they know `inline` means "may be defined in many TUs" and not
     "please inline". They'd catch this with a CI test that links two TUs.
2. **Extra copy / zero-copy fast path.** *"`feed()` copies every byte into
   `pending`. How would you avoid that?"*
   - When `pending` is empty, parse straight from the caller's span, then
     copy only the leftover partial message.
   - Strong: they notice the handler's pointers then point into the
     **caller's** buffer, so the lifetime rule from Task 5 matters even more.
     They'd benchmark before and after; with 4 KB reads most bytes skip the copy.
   - Also: `compact_if_needed` shifts with `erase`, which is O(n). A ring
     buffer or a fixed max message size (from Task 2) bounds it.
3. **Benchmark honesty.** *"What does 5.75M msgs/s measure?"*
   - It's a hot loop over a file already in the page cache, with a handler
     that reads one tag and no checksum or validation. After Tasks 1–3 the
     number will drop.
   - Strong: they'd report p50/p99 latency per message and not only
     throughput, pin the CPU, and use realistic message mixes. They'd check
     that the compiler didn't optimize away the handler's work.
4. **Portability.** *"How would you ship this to a machine without AVX2?"*
   - `-march=native` makes the binary non-portable even between x86 hosts.
   - Options: a scalar fallback chosen at runtime (`__builtin_cpu_supports`
     or function multiversioning with `target_clones`), SSE2 as the x86-64
     baseline, NEON for ARM, or `std::experimental::simd`.
   - Strong: they mention `memchr`, which glibc already vectorizes and is a
     good baseline to benchmark against.
5. **Resync cost and checksum strength.** *"How much work can one bad
   input make your parser do? And how much does the checksum protect you?"*
   - Each framing error consumes 1 byte and the retry scans forward again.
     Without the caps from Task 2 (test 7), a retry can rescan a long run of
     junk.
   - FIX's checksum is a byte sum mod 256. It catches random corruption,
     not malicious input: a given frame passes about 1 time in 256, and
     swapping two bytes never changes it. Integrity comes from TCP/TLS and
     from sequence numbers (tag 34), not from tag 10.
   - `find_begin` matching `8=` inside `58=` (the Text tag) is another
     false start that validation has to reject.
   - Strong: on a failed candidate, continue the search from the next `8=`
     instead of `+1`, and require `8=` to be at the start of the stream or
     right after SOH.
6. **Checksum placement** (if Task 1 is done). *"Your checksum is a second
   pass. Can you fold it into the first scan?"*
   - Yes: `_mm256_sad_epu8(chunk, zero)` gives byte sums per 8-byte lane,
     which you accumulate while scanning for SOH.
7. **`fileparse.cc` missing `#include <chrono>`.** Minor, but it shows
   whether they build with more than one compiler or standard library.
   *"How would CI catch this?"* A build matrix with GCC and Clang, plus
   libstdc++ and libc++.

---

## Rubric

| | Strong hire | Hire | No hire |
|---|---|---|---|
| Ownership | Explains SIMD, framing, and resync clearly, including design tradeoffs | Explains most of it, with gaps on SIMD details | Can't explain their own code |
| Tasks 1–3 | All 9 tests pass, with reasoning about resync strategy and overflow | Tasks 1–2 done, Task 3 in progress | Stuck on Task 1 or rewrites without understanding |
| Stretch (4–6) | Finishes one, and for Task 5 predicts the double parse and dangling pointers before running | Makes real progress on one with hints | Not reached (fine for Hire) |
| Robustness mindset | Raises hostile input and memory DoS on their own | Gets there with hints | Treats the input as trusted |
| Perf reasoning | Proposes SIMD checksum or zero-copy fast path; knows the benchmark's caveats | Reasonable ideas when prompted | Can't reason about cost |
| Communication | Talks through choices, writes tests first or checks against them | Mostly clear | Silent or defensive |

**Timing guide (60 min):** Warm-up 10 · Task 1 10 · Task 2 15 · Task 3 15 ·
stretch or discussion 10.
- **Strong candidates** usually finish Tasks 1–3 in about 30 min. Use the
  remaining time on Task 5, then discussion item 2.
- **To make this a 90-minute senior loop**, do all three stretch tasks and
  discussion items 1–3.
- **If they're slow**, skip the stretch tasks and use discussion items 1
  and 3.
