#ifndef HP_PRICE_H
#define HP_PRICE_H

#include <cstdint>

// [Task 6] Parses a decimal price as fixed-point with 8 implied decimals:
// "43125.5" -> 4312550000000. Accepts -?digits(.digits)? with at most 8
// decimals. Returns false for anything else, or if it overflows int64_t.
inline bool parse_price(
    const char* first,
    const char* last,
    std::int64_t& out
) noexcept {
    (void)first; (void)last; (void)out;
    return false;  // TODO
}

#endif
