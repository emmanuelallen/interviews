// =============================================================================
//  harpoon (CoderPad edition) -- REFERENCE SOLUTION (interviewer only)
//
//  This is a simplified, single-file version of your harpoon FIX parser.
//  The structure is the same as your repo (Parser<Handler>, feed(),
//  try_parse_one(), parse_message()); the AVX2 scanning has been replaced with
//  plain loops so it runs anywhere.
//
//  Quick FIX refresher
//    - A message is a list of fields:  <tag>=<value><SOH>   (SOH = '\x01')
//    - It starts with  8=<BeginString><SOH>9=<BodyLength><SOH>
//    - BodyLength = number of bytes from right after the 9= field's SOH up to
//      and including the SOH just before the 10= field.
//    - It ends with    10=<CheckSum><SOH>  where CheckSum is the sum of all
//      bytes before "10=", mod 256, written as exactly 3 digits.
//
//  main() runs a handful of tests. Some pass now and some fail. The
//  interviewer will walk you through the tasks.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace harpoon {

constexpr char SOH = '\x01';

inline bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

// [Task 2] Rejects values greater than `max` (so there's no overflow) and
// caps the digit count so a long run of zeros can't make us scan forever.
inline bool parse_uint_ascii(const char* data, std::size_t first,
                             std::size_t last, std::size_t max,
                             std::size_t& out) noexcept {
    if (first == last || last - first > 20) return false;
    std::size_t value = 0;
    for (std::size_t i = first; i < last; ++i) {
        char c = data[i];
        if (!is_digit(c)) return false;
        std::size_t d = static_cast<std::size_t>(c - '0');
        if (value > (max - d) / 10) return false;
        value = value * 10 + d;
    }
    out = value;
    return true;
}

// Scalar stand-in for find_char_avx2.
inline std::size_t find_char(const char* data, std::size_t n,
                             std::size_t from, char target) {
    for (std::size_t i = from; i < n; ++i)
        if (data[i] == target) return i;
    return n;
}

// Scalar stand-in for find_begin_avx2: finds the first "8=".
inline std::size_t find_begin(const char* data, std::size_t n) {
    for (std::size_t i = 0; i + 1 < n; ++i)
        if (data[i] == '8' && data[i + 1] == '=') return i;
    return n;
}

// Handler interface:
//   void on_message_begin(std::span<const char> msg);
//   void on_field(const char* tag, std::size_t tag_len,
//                 const char* value, std::size_t value_len);
//   void on_message_end();
//   void on_message_error();   // TODO: nothing calls this yet
template <typename Handler>
class Parser {
    // [Task 2] Upper bound on BodyLength. Without it a hostile or corrupt
    // header makes the parser buffer forever while waiting for bytes that will
    // never arrive.
    static constexpr std::size_t kMaxBodyLength = 64 * 1024;

    Handler& handler;
    std::vector<char> pending;
    std::size_t read_pos = 0;

    void parse_pending() {
        while (true) {
            std::size_t consumed = 0;
            std::span<const char> readable{pending.data() + read_pos,
                                           pending.size() - read_pos};
            if (!try_parse_one(readable, consumed)) break;
            read_pos += consumed;
        }
        compact_if_needed();
    }

    void compact_if_needed() {
        if (read_pos == 0) return;
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

    // Returns true if it made progress (sets `consumed`),
    // false if it needs more bytes.
    bool try_parse_one(std::span<const char> bytes, std::size_t& consumed) {
        consumed = 0;
        const char* data = bytes.data();
        const std::size_t n = bytes.size();

        if (n < 2) return false;

        std::size_t begin = find_begin(data, n);
        if (begin == n) {
            consumed = n - 1;  // keep last byte, it might be the '8' of "8="
            return true;
        }
        if (begin > 0) {       // skip garbage before "8="
            consumed = begin;
            return true;
        }

        std::size_t begin_field_end = find_char(data, n, 0, SOH);
        if (begin_field_end == n) return false;

        std::size_t body_len_tag = begin_field_end + 1;
        if (body_len_tag + 2 > n) return false;

        if (data[body_len_tag] != '9' || data[body_len_tag + 1] != '=') {
            consumed = 1;
            return true;
        }

        std::size_t body_len_value_start = body_len_tag + 2;
        std::size_t body_len_field_end =
            find_char(data, n, body_len_value_start, SOH);
        if (body_len_field_end == n) return false;

        std::size_t body_length = 0;
        if (!parse_uint_ascii(data, body_len_value_start, body_len_field_end,
                              kMaxBodyLength, body_length)) {
            consumed = 1;
            return true;
        }

        std::size_t body_start = body_len_field_end + 1;
        std::size_t checksum_start = body_start + body_length;
        constexpr std::size_t checksum_field_len = 7;  // "10=NNN\x01"
        std::size_t message_end = checksum_start + checksum_field_len;

        if (message_end > n) return false;

        if (std::memcmp(data + checksum_start, "10=", 3) != 0 ||
            !is_digit(data[checksum_start + 3]) ||
            !is_digit(data[checksum_start + 4]) ||
            !is_digit(data[checksum_start + 5]) ||
            data[checksum_start + 6] != SOH) {
            consumed = 1;
            return true;
        }

        // [Task 1] Verify the checksum. On a mismatch we skip one byte, the same
        // as the other framing errors. That resyncs on the next "8=" and doesn't
        // trust a BodyLength we now know is suspect.
        unsigned sum = 0;
        for (std::size_t i = 0; i < checksum_start; ++i)
            sum += static_cast<unsigned char>(data[i]);
        unsigned expected = (data[checksum_start + 3] - '0') * 100u +
                            (data[checksum_start + 4] - '0') * 10u +
                            (data[checksum_start + 5] - '0');
        if (sum % 256 != expected) {
            consumed = 1;
            return true;
        }

        parse_message(std::span<const char>{data, message_end});
        consumed = message_end;
        return true;
    }

    void parse_message(std::span<const char> msg) {
        handler.on_message_begin(msg);
        const char* p = msg.data();
        const char* end = msg.data() + msg.size();

        while (p < end) {
            // [Task 3] A tag is one or more digits followed by '='. The
            // original loop scanned for '=' across SOH boundaries, which
            // merged "55<SOH>44=1.5" into the tag "55\x0144".
            const char* tag = p;
            while (p < end && is_digit(*p)) ++p;
            if (p == tag || p == end || *p != '=') {
                handler.on_message_error();
                return;
            }
            const char* eq = p;
            ++p;

            const char* value = p;
            while (p < end && *p != SOH) ++p;
            if (p == end) {  // unreachable given framing, but keep B/E|X paired
                handler.on_message_error();
                return;
            }
            const char* value_end = p;
            ++p;

            handler.on_field(tag, static_cast<std::size_t>(eq - tag), value,
                             static_cast<std::size_t>(value_end - value));
        }
        handler.on_message_end();
    }

public:
    explicit Parser(Handler& h) : handler(h) { pending.reserve(8192); }

    void feed(std::span<const char> bytes) {
        pending.insert(pending.end(), bytes.begin(), bytes.end());
        parse_pending();
    }
};

}  // namespace harpoon

// =============================================================================
//  Test harness: you shouldn't need to change anything below this line.
// =============================================================================

// Logs every callback as text so tests can compare against the expected events.
//   "B"          on_message_begin
//   "35=D"       on_field
//   "E"          on_message_end
//   "X"          on_message_error
struct RecordingHandler {
    std::vector<std::string> events;

    void on_message_begin(std::span<const char>) { events.push_back("B"); }
    void on_field(const char* t, std::size_t tl, const char* v, std::size_t vl) {
        events.push_back(std::string(t, tl) + "=" + std::string(v, vl));
    }
    void on_message_end() { events.push_back("E"); }
    void on_message_error() { events.push_back("X"); }
};

// Builds a valid FIX message from a body written with '|' in place of SOH.
// Example: make_msg("35=D|55=XBT/USD|")
std::string make_msg(std::string body) {
    for (char& c : body) if (c == '|') c = harpoon::SOH;
    std::string head = "8=FIX.4.4";
    head += harpoon::SOH;
    head += "9=" + std::to_string(body.size());
    head += harpoon::SOH;
    std::string m = head + body;
    unsigned sum = 0;
    for (unsigned char c : m) sum += c;
    char cs[8];
    std::snprintf(cs, sizeof cs, "10=%03u", sum % 256);
    return m + cs + harpoon::SOH;
}

std::string with_checksum(std::string msg, const char* cs) {
    msg.replace(msg.size() - 4, 3, cs);  // overwrite the 3 checksum digits
    return msg;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (auto& e : v) s += (s.empty() ? "" : " ") + e;
    return s;
}

std::vector<std::string> run(const std::string& input, std::size_t chunk) {
    RecordingHandler h;
    harpoon::Parser<RecordingHandler> p{h};
    for (std::size_t i = 0; i < input.size(); i += chunk)
        p.feed({input.data() + i, std::min(chunk, input.size() - i)});
    return h.events;
}

int failures = 0;

void check(const char* name, const std::string& input,
           const std::string& expected, std::size_t chunk = 4096) {
    std::string got = join(run(input, chunk));
    bool ok = got == expected;
    if (!ok) ++failures;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) std::printf("    expected: %s\n    got:      %s\n",
                         expected.c_str(), got.c_str());
}

int main() {
    const std::string A = make_msg("35=D|55=XBT/USD|");
    const std::string B = make_msg("35=8|55=ETH/USD|");
    const std::string hdr = "B 8=FIX.4.4 9=16 ";
    const std::string evA = hdr + "35=D 55=XBT/USD 10=" + A.substr(A.size() - 4, 3) + " E";
    const std::string evB = hdr + "35=8 55=ETH/USD 10=" + B.substr(B.size() - 4, 3) + " E";

    // ---- Baseline: these pass today ---------------------------------------
    check("1. single message", A, evA);
    check("2. one byte at a time", A, evA, 1);
    check("3. two messages + leading garbage", "garbage" + A + B, evA + " " + evB);

    // ---- Task 1: checksum -------------------------------------------------
    // A message whose checksum is wrong must not reach the handler. The parser
    // should then keep going and deliver the next good message.
    check("4. bad checksum is dropped", with_checksum(A, "000") + B, evB);

    // ---- Task 2: bad or hostile BodyLength ------------------------------------
    // One bad header must not stall the stream. Assume no real message is
    // longer than 64 KiB.
    std::string huge = "8=FIX.4.4\x01" "9=999999999\x01";
    check("5. absurd BodyLength doesn't block the stream", huge + A, evA, 64);
    // 2^64 + (offset of A's "10=") wraps around to a length that "frames"
    // this header together with message A as a single message.
    std::string overflow = "8=FIX.4.4\x01" "9=18446744073709551" +
                           std::to_string(616 + A.size() - 7) + "\x01";
    check("6. BodyLength overflow", overflow + A, evA, 64);

    // ---- Task 3: malformed fields ---------------------------------------
    // The frame and checksum are valid, but one field has no '='. Every
    // "B" must be followed by exactly one "E" or "X"; send an "X" when a
    // field is malformed.
    // Tags must also be non-empty and all digits.
    const std::string C = make_msg("35=D|55|44=1.5|");
    check("7. field without '=' is rejected", C + B,
          "B 8=FIX.4.4 9=15 35=D X " + evB);
    const std::string D = make_msg("35=D|=oops|");
    check("8. empty tag is rejected", D + B, "B 8=FIX.4.4 9=11 35=D X " + evB);

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
