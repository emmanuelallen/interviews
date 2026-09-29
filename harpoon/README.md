# Interview: harpoon (candidate's FIX parser)

**Source:** https://github.com/yutaoz/harpoon, a header-only C++23 streaming FIX
parser with AVX2 scanning. The README claims about 5.75M msgs/s (1.2 GB/s).

**Format:** 60 minutes in CoderPad, language **C++** (needs C++20 for `std::span`).

| File | Use |
|---|---|
| `coderpad_starter.cpp` | Paste into CoderPad. It's a single-file, scalar (no AVX2) version of their parser with the same structure and names, plus a test harness. 3 tests pass and 5 fail. |
| `solution.cpp` | Reference solution (interviewer only). All 8 tests pass, and it's clean under ASan/UBSan. Changes are marked `[Task N]`. |

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

## Task 2: hostile / corrupt BodyLength (≈15 min) → tests 5, 6

> "A bad header should never stall the stream. Assume no real message is
> larger than 64 KiB."

Two separate bugs:
- **Test 5:** `9=999999999` means `message_end > n` is always true, so the
  parser returns "need more bytes" forever. `pending` then grows without
  limit (memory DoS), and every later message is lost.
- **Test 6:** `parse_uint_ascii` has no overflow check. The test builds
  `2^64 + offset` so the wrapped length lands exactly on the **next**
  message's `10=`. That produces one "valid" frame that swallows a real
  message. Ask: *"Is this exploitable if it's an exchange feed?"*

A strong fix adds a max-length cap and does an overflow-safe parse
(`value > (max - d) / 10`). Bonus: they notice a string of leading zeros
(`9=000…0005`) can also defeat a naive digit cap.

**Probe:** *"What should the cap be, and should it be configurable?"*
Probe: *"Test 5 and test 6 feed 64-byte chunks. Why does that matter?"*

## Task 3: malformed fields & callback contract (≈10–15 min) → tests 7, 8

> "Every `on_message_begin` must be followed by exactly one
> `on_message_end` or `on_message_error`. Tags must be non-empty digits."

- The bug: `parse_message` scans for `'='` **without stopping at SOH**, so
  `55<SOH>44=1.5` becomes the tag `"55\x0144"`. Empty tag `=oops` is
  passed to the handler as-is.
- The original also `return`s without calling `on_message_end`, leaving the
  handler in a "message open" state.
- **Design probe:** *"The handler has already seen `35=D` before you find
  the error. Is that ok? What would you change if the handler was placing
  orders?"* Options: validate in a first pass before calling handlers, or
  buffer fields and emit on success. Discuss the latency tradeoff.

---

## Discussion questions (remaining time, pick any)

These are real issues in the candidate's repo (`main` branch):

1. **ODR / "header-only" claim.** `dispatch_field` in `harpoon/dispatch.h`
   is a non-`inline`, non-template function defined in a header. Including
   `harpoon.h` from two .cc files causes a *multiple definition* link error.
   *"You call this header-only. What happens with two translation units?"*
2. **Handler re-entrancy.** `tag`/`value` point into `pending`. If a handler
   calls `feed()` from inside `on_field` (for example, replaying a buffered
   message), `pending.insert` can reallocate and leave dangling pointers.
   *"What's the lifetime of the pointers you hand to the handler? How would
   you document or enforce it?"*
3. **Extra copy.** `feed()` always copies into `pending`. *"When `pending`
   is empty, can you parse straight from the caller's span and only buffer
   the tail?"* That's the obvious next perf step.
4. **Floating-point prices.** `parse_double_simple` does `scale *= 0.1`,
   which accumulates error (`0.1` isn't exact). *"Would you sum prices this
   way at an exchange? What would you use?"* Expected: fixed-point integer
   mantissa + exponent, or decimal.
5. **Repeating groups / raw data.** FIX `RawData` (95/96) may contain SOH.
   *"Where does your field loop break?"* Expected: it needs the length from
   the preceding length tag.
6. **Portability.** `-mavx2 -march=native` and no fallback. *"How would you
   ship this to a machine without AVX2?"* Runtime dispatch
   (`__builtin_cpu_supports`), a scalar fallback, or SSE2/NEON.
7. **Resync false positives.** `find_begin` matches `8=` inside `58=` (the
   Text tag). The `9=` check covers it. *"What's the worst-case cost of
   resync on adversarial input?"* One byte at a time can mean O(n²) rescans.
8. **`fileparse.cc`** uses `std::chrono` without `#include <chrono>`, so it
   only compiles through transitive includes.

---

## Rubric

| | Strong hire | Hire | No hire |
|---|---|---|---|
| Ownership | Explains SIMD, framing, and resync clearly, including design tradeoffs | Explains most of it, with gaps on SIMD details | Can't explain their own code |
| Tasks 1–3 | All 8 tests pass, with reasoning about resync strategy and overflow | Tasks 1–2 done, Task 3 in progress | Stuck on Task 1 or rewrites without understanding |
| Robustness mindset | Raises hostile input and memory DoS on their own | Gets there with hints | Treats the input as trusted |
| Perf reasoning | Proposes SIMD checksum or zero-copy fast path; knows the benchmark's caveats | Reasonable ideas when prompted | Can't reason about cost |
| Communication | Talks through choices, writes tests first or checks against them | Mostly clear | Silent or defensive |

**Timing guide:** Warm-up 10 · Task 1 10 · Task 2 15 · Task 3 15 · Discussion 10.
If they're fast, skip to discussion items 2 and 3, which have the most signal.
