#ifndef HP_PRICE_H
#define HP_PRICE_H

#include <cstdint>
#include <limits>

#include "parse.h"

// [Task 6] Parses a decimal price as fixed-point with 8 implied decimals:
// "43125.5" -> 4312550000000. Accepts -?digits(.digits)? with at most 8
// decimals. Returns false for anything else, or if it overflows int64_t.
inline bool parse_price(
    const char* first,
    const char* last,
    std::int64_t& out
) noexcept {
    constexpr int decimals_wanted = 8;
    constexpr std::uint64_t max = std::numeric_limits<std::int64_t>::max();
    std::uint64_t mag = 0;

    auto push = [&](char c) {
        std::uint64_t d = static_cast<std::uint64_t>(c - '0');
        if (mag > (max - d) / 10) {
            return false;
        }
        mag = mag * 10 + d;
        return true;
    };

    const char* p = first;
    bool neg = p < last && *p == '-';
    if (neg) {
        ++p;
    }

    const char* int_start = p;
    while (p < last && is_digit(*p)) {
        if (!push(*p++)) {
            return false;
        }
    }
    if (p == int_start) {
        return false;
    }

    int decimals = 0;
    if (p < last && *p == '.') {
        const char* frac_start = ++p;
        while (p < last && is_digit(*p)) {
            if (++decimals > decimals_wanted || !push(*p++)) {
                return false;
            }
        }
        if (p == frac_start) {
            return false;
        }
    }
    if (p != last) {
        return false;
    }

    for (; decimals < decimals_wanted; ++decimals) {
        if (!push('0')) {
            return false;
        }
    }

    out = neg ? -static_cast<std::int64_t>(mag) : static_cast<std::int64_t>(mag);
    return true;
}

#endif
