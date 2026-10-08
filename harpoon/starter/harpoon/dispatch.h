#ifndef H_DISPATCH_H
#define H_DISPATCH_H

#include <cstdint>
#include <cstring>

constexpr std::uint32_t pack3(char a, char b, char c) noexcept {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16);
}

constexpr std::uint32_t pack4(char a, char b, char c, char d) noexcept {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
}

constexpr std::uint64_t pack5(char a, char b, char c, char d, char e) noexcept {
    return static_cast<std::uint64_t>(static_cast<unsigned char>(a)) |
          (static_cast<std::uint64_t>(static_cast<unsigned char>(b)) << 8) |
          (static_cast<std::uint64_t>(static_cast<unsigned char>(c)) << 16) |
          (static_cast<std::uint64_t>(static_cast<unsigned char>(d)) << 24) |
          (static_cast<std::uint64_t>(static_cast<unsigned char>(e)) << 32);
}

inline std::uint32_t load3(const char* p) noexcept {
    std::uint32_t x = 0;
    std::memcpy(&x, p, 3);
    return x;
}

inline std::uint32_t load4(const char* p) noexcept {
    std::uint32_t x = 0;
    std::memcpy(&x, p, 4);
    return x;
}

inline std::uint64_t load5(const char* p) noexcept {
    std::uint64_t x = 0;
    std::memcpy(&x, p, 5);
    return x;
}


void dispatch_field(
    const char* tag,
    std::size_t tag_len,
    const char* value,
    const char* value_end
) {
    if (tag_len == 1) {
        if (tag[0] == '8') {
        } else if (tag[0] == '9') {
        }

        return;
    }

    if (tag_len == 2) {
        std::uint32_t key = load3(tag);

        switch (key) {
            case pack3('1', '0', '='):
                break;

            case pack3('3', '4', '='):
                break;

            case pack3('3', '5', '='):
                break;

            case pack3('4', '9', '='):
                break;

            case pack3('5', '2', '='):
                break;

            case pack3('5', '5', '='):
                break;

            case pack3('5', '6', '='):
                break;

            default:
                break;
        }

        return;
    }

    if (tag_len == 3) {
        std::uint32_t key = load4(tag);

        switch (key) {
            case pack4('2', '6', '2', '='):
                break;

            case pack4('2', '6', '8', '='):
                break;

            case pack4('2', '6', '9', '='):
                break;

            case pack4('2', '7', '0', '='):
                break;

            case pack4('2', '7', '1', '='):
                break;

            case pack4('2', '7', '3', '='):
                break;

            case pack4('2', '7', '8', '='):
                break;

            default:
                break;
        }

        return;
    }

}

#endif
