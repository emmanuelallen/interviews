#ifndef H_SIMD_H
#define H_SIMD_H

#include <immintrin.h>
#include <cstddef>
#include <cstdint>


inline std::size_t find_char_avx2(
        const char* data,
        std::size_t n,
        std::size_t from,
        char target
) {
    std::size_t i = from;

    const __m256i needle = _mm256_set1_epi8(target);
    
    for (; i + 32 <= n; i += 32) {
        __m256i chunk = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(data + i)
        );

        __m256i cmp = _mm256_cmpeq_epi8(chunk, needle);
        std::uint32_t mask = static_cast<std::uint32_t>(_mm256_movemask_epi8(cmp));

        if (mask != 0) {
            return i + static_cast<std::size_t>(__builtin_ctz(mask));
        }
    }

    for (; i < n; ++i) {
        if (data[i] == target) {
            return i;
        }
    }

    return n;
}

inline std::size_t find_begin_avx2(
    const char* data,
    std::size_t n,
    std::size_t from
) {
    std::size_t i = from;

    const __m256i needle = _mm256_set1_epi8('8');

    for (; i + 32 <= n; i += 32) {
        __m256i chunk = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(data + i)
        );

        __m256i cmp = _mm256_cmpeq_epi8(chunk, needle);
        std::uint32_t mask = static_cast<std::uint32_t>(_mm256_movemask_epi8(cmp));

        while (mask != 0) {
            std::size_t bit = static_cast<std::size_t>(__builtin_ctz(mask));
            std::size_t pos = i + bit;

            if (pos + 1 < n && data[pos + 1] == '=') {
                return pos;
            }

            mask &= mask - 1;
        }
    }

    for (; i + 1 < n; ++i) {
        if (data[i] == '8' && data[i + 1] == '=') {
            return i;
        }
    }

    return n;
}

#endif
