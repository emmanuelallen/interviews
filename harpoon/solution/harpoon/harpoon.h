#ifndef HARPOON_H
#define HARPOON_H

#include <algorithm>
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

        // [Task 2] Limits for a hostile or corrupt header.
        static constexpr std::size_t max_body_length = 64 * 1024;
        static constexpr std::size_t max_begin_field = 16;  // "8=FIXT.1.1<SOH>" is 11
        static constexpr std::size_t max_body_len_digits = 20;

        // [Task 5] Bytes fed from inside a handler callback, parsed after the
        // current batch.
        bool parsing = false;
        std::vector<char> deferred;

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

            // [Task 2] BeginString must start with "FIX" and be short. Without
            // this check, "8=" junk joins onto the next message's header to
            // form one frame, and about 1 in 256 of those frames passes the
            // 8-bit checksum. The limit on the SOH search also stops "8=" with
            // no SOH after it from buffering forever.
            if (n < 5) {
                return false;
            }

            if (!starts_with(data, n, 0, "8=FIX", 5)) {
                consumed = 1;
                return true;
            }

            std::size_t begin_limit = std::min(n, max_begin_field);
            std::size_t begin_field_end =
                find_char_avx2(data, begin_limit, 0, delimiter);

            if (begin_field_end == begin_limit) {
                if (begin_limit < max_begin_field) {
                    return false;
                }
                consumed = 1;
                return true;
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
            // [Task 2] Same limit for 9=: at most 20 digits before the SOH.
            std::size_t body_len_cap = body_len_value_start + max_body_len_digits + 1;
            std::size_t body_len_limit = std::min(n, body_len_cap);
            std::size_t body_len_field_end =
                find_char_avx2(data, body_len_limit, body_len_value_start, delimiter);

            if (body_len_field_end == body_len_limit) {
                if (body_len_limit < body_len_cap) {
                    return false;
                }
                consumed = 1;
                return true;
            }

            std::size_t body_length = 0;

            // [Task 2] parse_uint_ascii now rejects overflow; also cap the
            // length so a huge value can't make us buffer forever.
            if (!parse_uint_ascii(
                        data,
                        body_len_value_start,
                        body_len_field_end,
                        body_length
                ) || body_length > max_body_length) {
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

            // [Task 1] Verify the checksum. On a mismatch skip one byte, the
            // same as the other framing errors: BodyLength may be the corrupt
            // part, so don't trust it to skip the whole message.
            unsigned sum = 0;
            for (std::size_t i = 0; i < checksum_start; ++i) {
                sum += static_cast<unsigned char>(data[i]);
            }

            unsigned expected = (data[checksum_start + 3] - '0') * 100u +
                                (data[checksum_start + 4] - '0') * 10u +
                                (data[checksum_start + 5] - '0');

            if (sum % 256 != expected) {
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

            // [Task 4] Set by tag 95 (RawDataLength). The next field must be 96
            // and its value is exactly raw_len bytes, which may include SOH.
            std::size_t raw_len = 0;
            bool raw_pending = false;

            while (p < end) {
                const char* tag = p;

                // [Task 3] A tag is one or more digits followed by '='. Scanning
                // for '=' crossed SOH and merged "55<SOH>44=1.5" into one tag.
                while (p < end && is_digit(*p)) {
                    ++p;
                }

                // [Task 3] Every on_message_begin gets an end or an error.
                if (p == tag || p == end || *p != '=') {
                    handler.on_message_error();
                    return;
                }

                const char* eq = p;
                ++p;

                const char* value = p;

                // [Task 4] Jump straight to the end of the RawData value; the
                // loop below then stops right away on its delimiter.
                if (raw_pending) {
                    bool is_raw_data = eq - tag == 2 && tag[0] == '9' && tag[1] == '6';

                    if (!is_raw_data ||
                        raw_len >= static_cast<std::size_t>(end - value) ||
                        value[raw_len] != delimiter) {
                        handler.on_message_error();
                        return;
                    }

                    p = value + raw_len;
                    raw_pending = false;
                }

                while (p < end && *p != delimiter) {
                    ++p;
                }

                if (p == end) {
                    handler.on_message_error();
                    return;
                }

                const char* value_end = p;
                ++p;

                std::size_t tag_len = static_cast<std::size_t>(eq - tag);
                std::size_t value_len = static_cast<std::size_t>(value_end - value);

                handler.on_field(tag, tag_len, value, value_len);

                //dispatch_field(tag, tag_len, value, value_end);

                // [Task 4]
                if (tag_len == 2 && tag[0] == '9' && tag[1] == '5') {
                    if (!parse_uint_ascii(value, 0, value_len, raw_len)) {
                        handler.on_message_error();
                        return;
                    }
                    raw_pending = true;
                }
            }

            // [Task 4] 95 was the last field.
            if (raw_pending) {
                handler.on_message_error();
                return;
            }

            handler.on_message_end();
        }


    public:
        explicit Parser(Handler& h) : handler(h) {
            pending.reserve(8192);
        }

        void feed(std::span<const char> bytes) {
            // [Task 5] Called from inside a handler callback: queue the bytes.
            // Appending to `pending` could reallocate it under the pointers
            // the handler holds, and re-entering parse_pending would parse the
            // in-flight message again (read_pos hasn't advanced yet).
            if (parsing) {
                deferred.insert(deferred.end(), bytes.begin(), bytes.end());
                return;
            }

            parsing = true;
            pending.insert(pending.end(), bytes.begin(), bytes.end());
            parse_pending();

            while (!deferred.empty()) {
                std::vector<char> next;
                next.swap(deferred);
                pending.insert(pending.end(), next.begin(), next.end());
                parse_pending();
            }
            parsing = false;
        }


    };
}

#endif
