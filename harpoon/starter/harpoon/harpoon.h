#ifndef HARPOON_H
#define HARPOON_H

#include <array>
#include <vector>
#include <span>
#include <cstddef>

#include "simd.h"
#include "parse.h"
#include "dispatch.h"

namespace harpoon {

    template <typename Handler>
    class Parser {

        Handler& handler;
        std::vector<char> pending;

        //char       delimiter = '\x01';
        char       delimiter = '\x01';
        std::size_t read_pos = 0;

        void parse_pending() {
            while (true) {
                std::size_t consumed = 0;

                std::span<const char> readable {
                    pending.data() + read_pos,
                    pending.size() - read_pos
                };

                if (!try_parse_one(readable, consumed)) {
                    break;
                }

                read_pos += consumed;

            }
            compact_if_needed();
        }

        void compact_if_needed() {
            if (read_pos == 0) {
                return;
            }
            if (read_pos == pending.size()) {
                pending.clear();
                read_pos = 0;
                return;
            }
            if (read_pos > 4096 && read_pos > pending.size() / 2) {
                pending.erase(pending.begin(), pending.begin() + read_pos);
                read_pos = 0;
            }
        }
        bool try_parse_one( std::span<const char> bytes, 
                            std::size_t& consumed ) {
            consumed = 0;

            const char* data = reinterpret_cast<const char*>(bytes.data());
            const std::size_t n = bytes.size();

            if (n < 2) {
                return false;
            }

            std::size_t begin = find_begin_avx2(data, n, 0);

            if (begin == n) {
                if (n > 1) {
                    consumed = n - 1;
                    return true;
                }
                return false;
            }

            if (begin > 0) {
                consumed = begin;
                return true;
            }

            std::size_t begin_field_end = find_char_avx2(data, n, 0, delimiter);

            if (begin_field_end == n) {
                return false;
            }

            std::size_t body_len_tag = begin_field_end + 1;
            if (body_len_tag + 2 > n) {
                return false;
            }

            if (data[body_len_tag] != '9' || data[body_len_tag + 1] != '=') {
                consumed = 1;
                return true;
            }

            std::size_t body_len_value_start = body_len_tag + 2;
            std::size_t body_len_field_end =
                find_char_avx2(data, n, body_len_value_start, delimiter);

            if (body_len_field_end == n) {
                return false;
            }

            std::size_t body_length = 0;

            if (!parse_uint_ascii(
                        data,
                        body_len_value_start,
                        body_len_field_end,
                        body_length
                )) {
                consumed = 1;
                return true;
            }

            std::size_t body_start = body_len_field_end + 1;
            std::size_t checksum_start = body_start + body_length;

            constexpr std::size_t checksum_field_len = 7;
            std::size_t message_end = checksum_start + checksum_field_len;

            if (message_end > n) {
                return false;
            }

            if (!starts_with(data, n, checksum_start, "10=", 3)) {
                consumed = 1;
                return true;
            }

            if (!is_digit(data[checksum_start + 3]) ||
                !is_digit(data[checksum_start + 4]) ||
                !is_digit(data[checksum_start + 5]) ||
                data[checksum_start + 6] != delimiter) {
                consumed = 1;
                return true;
            }

            std::span<const char> msg{
                bytes.data(),
                message_end
            };

            parse_message(msg);

            consumed = message_end;
            return true;

        }


        void parse_message(std::span<const char> msg) {
            handler.on_message_begin(msg);
            const char* p = msg.data();
            const char* end = msg.data() + msg.size();

            while (p < end) {
                const char* tag = p;

                while (p < end && *p != '=') {
                    ++p;
                }

                if (p == end) {
                    return;
                }

                const char* eq = p;
                ++p;

                const char* value = p;

                while (p < end && *p != delimiter) {
                    ++p;
                }

                if (p == end) {
                    return;
                }

                const char* value_end = p;
                ++p;

                std::size_t tag_len = static_cast<std::size_t>(eq - tag);
                std::size_t value_len = static_cast<std::size_t>(value_end - value);

                handler.on_field(tag, tag_len, value, value_len);

                //dispatch_field(tag, tag_len, value, value_end);
            }
            handler.on_message_end();
        }


    public:
        explicit Parser(Handler& h) : handler(h) {
            pending.reserve(8192);
        }

        void feed(std::span<const char> bytes) {
            pending.insert(pending.end(), bytes.begin(), bytes.end());
            parse_pending();
        }


    };
}

#endif
