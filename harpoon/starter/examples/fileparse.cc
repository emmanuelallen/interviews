#include <iostream>
#include <fstream>
#include <array>
#include <string>
#include <filesystem>
#include "../harpoon/harpoon.h"

struct Sum5013Handler {
    double sum = 0.0;
    std::size_t count = 0;

    void on_message_begin(std::span<const char>) {}

    void on_field(
        const char* tag,
        std::size_t tag_len,
        const char* value,
        std::size_t value_len
    ) {
        if (tag_len == 4 && load5(tag) == pack5('5', '0', '1', '3', '=')) {
            double x = 0.0;

            if (parse_double_simple(value, value + value_len, x)) {
                sum += x;
                ++count;
            }
        }
    }

    void on_message_end() {}
};

int main() {
    Sum5013Handler handler;
    std::cout << "init!\n";

    std::ifstream ifs;
    std::cout << "cwd: " << std::filesystem::current_path() << "\n";
    ifs.open("./examples/1mil.fix", std::ios::binary);
    if (!ifs) {
        std::cout << "err?" << std::endl;
    }

    std::array<char, 4096> buffer;
    harpoon::Parser<Sum5013Handler> p{handler};

    auto start = std::chrono::steady_clock::now();

    while (ifs) {
        ifs.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize n = ifs.gcount();

        if (n > 0) {
            p.feed(std::span<const char> {
                    buffer.data(),
                    static_cast<std::size_t>(n)
                    });
        }
    }

    auto end = std::chrono::steady_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "elapsed ns: " << ns << "\n";
    std::cout << "elapsed ms: " << ns / 1'000'000.0 << "\n";

    std::cout << "5013 count: " << handler.count << "\n";
    std::cout << "5013 sum: " << handler.sum << "\n";

    if (handler.count > 0) {
        std::cout << "5013 average: " << handler.sum / handler.count << "\n";
    }





    return 0;
}
