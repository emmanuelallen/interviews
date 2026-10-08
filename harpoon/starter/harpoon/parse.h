#ifndef HP_PARSE_H
#define HP_PARSE_H

#include <cstdint>
#include <span>

inline bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

inline bool parse_uint_ascii(
    const char* data,
    std::size_t first,
    std::size_t last,
    std::size_t& out
) noexcept {
    if (first == last) {
        return false;
    }

    std::size_t value = 0;

    for (std::size_t i = first; i < last; ++i) {
        char c = data[i];

        if (!is_digit(c)) {
            return false;
        }

        value = value * 10 + static_cast<std::size_t>(c - '0');
    }

    out = value;
    return true;
}

inline bool starts_with(
        const char* data,
        std::size_t n,
        std::size_t pos,
        const char* s,
        std::size_t len
) noexcept {
    if (pos + len > n) {
        return false;
    }

    for (std::size_t i = 0; i < len; ++i) {
        if (data[pos + i] != s[i]) {
            return false;
        }
    }
    return true;
}

inline bool parse_double_simple(
    const char* first,
    const char* last,
    double& out
) noexcept {
    if (first == last) {
        return false;
    }

    double value = 0.0;
    bool seen_digit = false;

    const char* p = first;

    bool neg = false;
    if (*p == '-') {
        neg = true;
        ++p;
    }

    while (p < last && *p >= '0' && *p <= '9') {
        seen_digit = true;
        value = value * 10.0 + static_cast<double>(*p - '0');
        ++p;
    }

    if (p < last && *p == '.') {
        ++p;

        double scale = 0.1;

        while (p < last && *p >= '0' && *p <= '9') {
            seen_digit = true;
            value += static_cast<double>(*p - '0') * scale;
            scale *= 0.1;
            ++p;
        }
    }

    if (!seen_digit || p != last) {
        return false;
    }

    out = neg ? -value : value;
    return true;
}

#endif
