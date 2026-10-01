# lite-exchange: interview exercise

This is a trimmed copy of your [lite-exchange](https://github.com/chriskontsis/lite-exchange) repo: the order book, the matching shard, the SPSC queue and the wire protocol, with your tests.

- **Unchanged from your repo:** everything under `include/` except the two files below, and every test under `test/book`, `test/engine` and `test/queue`.
- **Changed for this exercise** (every change is marked `EXERCISE`, and nothing was removed):
  - `include/lx/proto/messages.hpp`: a new `MASS_CANCEL` message.
  - `include/lx/engine/shard.hpp`: dispatch for the new message, a TODO, and a stub.
- **Not included:** the network gateway, `ShardSet`, the benchmarks, and liburing. The build uses no external dependencies.

## Running

```
./run.sh                 # everything except the OPTIONAL part
./run.sh 'Part2*'        # one part (any --gtest_filter pattern)
./run.sh '*'             # everything
```

`run.sh` uses CMake when it's available; otherwise it calls `g++` directly. Either way it uses the same flags as your repo (`-std=c++23 -Wall -Wextra -Wpedantic -Werror`, with ASan and UBSan). If GoogleTest isn't installed, it uses a small compatible shim in `third_party/`.

Tests for parts you haven't done yet will fail. That's expected.

## Tasks

For this exercise, an **account** is identified by its `session_id`.

**Part 1: walkthrough.** Talk us through the code. No changes needed.

**Part 2: cancel ownership** (`test/exercise/part2_test.cc`)
- `Shard::handle_cancel` in `shard.hpp`: only the session that entered an order may cancel it.

**Part 3: mass cancel** (`test/exercise/part3_test.cc`)
- `Shard::handle_mass_cancel` in `shard.hpp`: cancel every resting order an account has on this shard, across all books.

**Optional.** Only if your interviewer asks; ignore this test until then.
- `test/exercise/optional_test.cc` fails against the code as shipped. Find out why and fix it.
