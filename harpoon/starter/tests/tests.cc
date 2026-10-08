// =============================================================================
//  harpoon interview tests
//
//  Some tests pass now and some fail. The interviewer will walk you through
//  the tasks. You shouldn't need to change this file.
//
//  Quick FIX refresher
//    - A message is a list of fields:  <tag>=<value><SOH>   (SOH = '\x01')
//    - It starts with  8=<BeginString><SOH>9=<BodyLength><SOH>
//    - BodyLength = number of bytes from right after the 9= field's SOH up to
//      and including the SOH just before the 10= field.
//    - It ends with    10=<CheckSum><SOH>  where CheckSum is the sum of all
//      bytes before "10=", mod 256, written as exactly 3 digits.
// =============================================================================

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

#include "harpoon/harpoon.h"
#include "harpoon/price.h"

constexpr char SOH = '\x01';

// Logs every callback as text so tests can compare against the expected events.
//   "B"          on_message_begin
//   "35=D"       on_field (SOH inside a value is shown as '|')
//   "E"          on_message_end
//   "X"          on_message_error
struct RecordingHandler {
    std::vector<std::string> events;

    void on_message_begin(std::span<const char>) { events.push_back("B"); }
    void on_field(const char* t, std::size_t tl, const char* v, std::size_t vl) {
        std::string value(v, vl);
        for (char& c : value) if (c == SOH) c = '|';
        events.push_back(std::string(t, tl) + "=" + value);
    }
    void on_message_end() { events.push_back("E"); }
    void on_message_error() { events.push_back("X"); }
};

// Builds a valid FIX message from a body written with '|' in place of SOH.
// Example: make_msg("35=D|55=XBT/USD|")
std::string make_msg(std::string body) {
    for (char& c : body) if (c == '|') c = SOH;
    std::string head = "8=FIX.4.4";
    head += SOH;
    head += "9=" + std::to_string(body.size());
    head += SOH;
    std::string m = head + body;
    unsigned sum = 0;
    for (unsigned char c : m) sum += c;
    char cs[8];
    std::snprintf(cs, sizeof cs, "10=%03u", sum % 256);
    return m + cs + SOH;
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

// Calls feed() on its own parser from inside on_field, as a handler that
// replays or forwards messages might.
struct ReentrantHandler : RecordingHandler {
    harpoon::Parser<ReentrantHandler>* parser = nullptr;
    std::string trigger;  // the field that triggers the feed, e.g. "55=XBT/USD"
    std::string inject;   // the bytes it feeds (sent once)

    void on_field(const char* t, std::size_t tl, const char* v, std::size_t vl) {
        RecordingHandler::on_field(t, tl, v, vl);
        if (!inject.empty() && events.back() == trigger) {
            std::string bytes = std::move(inject);
            inject.clear();
            parser->feed(bytes);
        }
    }
};

std::vector<std::string> run_reentrant(const std::string& input,
                                       const std::string& trigger,
                                       const std::string& inject) {
    ReentrantHandler h;
    harpoon::Parser<ReentrantHandler> p{h};
    h.parser = &p;
    h.trigger = trigger;
    h.inject = inject;
    p.feed(input);
    return h.events;
}

int failures = 0;

void report(const char* name, const std::string& got,
            const std::string& expected) {
    bool ok = got == expected;
    if (!ok) ++failures;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    auto clip = [](const std::string& s) {
        return s.size() <= 300 ? s
             : s.substr(0, 300) + "... (" + std::to_string(s.size()) + " chars)";
    };
    if (!ok) std::printf("    expected: %s\n    got:      %s\n",
                         clip(expected).c_str(), clip(got).c_str());
    std::fflush(stdout);
}

void check(const char* name, const std::string& input,
           const std::string& expected, std::size_t chunk = 4096) {
    report(name, join(run(input, chunk)), expected);
}

struct PriceCase {
    const char* text;
    bool ok;
    std::int64_t value;
};

void check_prices(const char* name, std::initializer_list<PriceCase> cases) {
    std::string got, expected;
    for (const PriceCase& c : cases) {
        std::int64_t out = 0;
        bool ok = parse_price(c.text, c.text + std::strlen(c.text), out);
        std::string g = ok ? std::to_string(out) : "reject";
        std::string e = c.ok ? std::to_string(c.value) : "reject";
        if (g != e) {
            got += std::string(got.empty() ? "" : ", ") + '"' + c.text + "\"->" + g;
            expected += std::string(expected.empty() ? "" : ", ") + '"' + c.text + "\"->" + e;
        }
    }
    report(name, got, expected);
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
    // A run of "8=" junk just before a real message must not be joined onto
    // it to form one message with a giant BeginString.
    std::string junk;
    for (int i = 0; i < 20000; ++i) junk += "8=";
    check("7. \"8=\" junk before a message", junk + A, evA);

    // ---- Task 3: malformed fields ---------------------------------------
    // The frame and checksum are valid, but one field has no '='. Every
    // "B" must be followed by exactly one "E" or "X"; send an "X" when a
    // field is malformed.
    // Tags must also be non-empty and all digits.
    const std::string C = make_msg("35=D|55|44=1.5|");
    check("8. field without '=' is rejected", C + B,
          "B 8=FIX.4.4 9=15 35=D X " + evB);
    const std::string D = make_msg("35=D|=oops|");
    check("9. empty tag is rejected", D + B, "B 8=FIX.4.4 9=11 35=D X " + evB);

    // =====================================================================
    //  Stretch tasks: the interviewer will pick which ones to do.
    // =====================================================================

    // ---- Task 4: RawData ------------------------------------------------
    // Tag 95 (RawDataLength) gives the exact byte length of the value of
    // the field that must follow it, tag 96 (RawData). That value can contain
    // SOH and '=' (it's binary). If 96 doesn't follow or the length doesn't
    // fit, send an "X".
    const std::string R = make_msg("35=D|95=5|96=a|b=c|55=X|");
    check("10. RawData may contain SOH and '='", R,
          "B 8=FIX.4.4 9=24 35=D 95=5 96=a|b=c 55=X 10=" +
          R.substr(R.size() - 4, 3) + " E");
    const std::string R2 = make_msg("35=D|95=50|96=abc|");
    check("11. RawData length past the end is rejected", R2 + B,
          "B 8=FIX.4.4 9=18 35=D 95=50 X " + evB);

    // ---- Task 6: fixed-point prices -------------------------------------
    // See parse_price() in harpoon/price.h.
    check_prices("12. parse_price", {
        {"0.1", true, 10000000},
        {"-2.5", true, -250000000},
        {"43125.5", true, 4312550000000},
        {"0.00000001", true, 1},
        {"92233720368.54775807", true, 9223372036854775807},
        {"92233720368.54775808", false, 0},
        {"1.123456789", false, 0},
        {"", false, 0},
        {"-", false, 0},
        {"1.", false, 0},
        {".5", false, 0},
        {"1.2.3", false, 0},
        {"+1", false, 0},
        {"1e5", false, 0},
    });

    // ---- Task 5: re-entrant feed() ---------------------------------------
    // A handler may call feed() on the same parser from inside a callback.
    // Those bytes must be parsed after the current message, in order, and
    // every message delivered exactly once.
    // (These run last because, before the fix, they can crash.)
    report("13. feed() from inside on_field",
           join(run_reentrant(A, "55=XBT/USD", B)), evA + " " + evB);
    report("14. feed() from inside on_field, large (buffer reallocates)",
           join(run_reentrant(A, "55=XBT/USD", std::string(20000, 'z') + B)),
           evA + " " + evB);

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
