# lite-exchange: reference solution (interviewer only)

The candidate project with every part implemented. Run `./run.sh '*'`: all tests pass, including two `Bonus` tests that only exist here.

The interviewer guide is [`../README.md`](../README.md). Every solution change is marked `SOLUTION` in the code:
- `include/lx/book/pool.hpp`: a live flag per slot (bonus fix).
- `include/lx/book/account_index.hpp` (new): per-account order list for mass cancel.
- `include/lx/book/order_book.hpp`: the sink-based `add_order` (optional part), the live check in `cancel_order`, and account list link/unlink.
- `include/lx/engine/shard.hpp`: the ownership check, `handle_mass_cancel`, and streaming fills (optional part).
