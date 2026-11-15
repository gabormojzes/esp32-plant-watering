#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "web/cookie.hpp"

// The first byte splits the input into a cookie name and a Cookie header. A
// value found lies inside the header, with no padding and no separator left on
// it.
// libFuzzer calls the entry point by this name.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return -1;
    }
    const std::string_view input{reinterpret_cast<const char*>(data) + 1, size - 1};
    const std::size_t name_length{std::min<std::size_t>(data[0], input.size())};
    const std::string_view name{input.substr(0, name_length)};
    const std::string_view cookies{input.substr(name_length)};

    const std::string_view value{cookie::value(cookies, name)};
    if (value.empty()) {
        return 0;
    }
    const bool inside_cookies{
        value.data() >= cookies.data() &&
        value.data() + value.size() <= cookies.data() + cookies.size()
    };
    const bool trimmed{
        value.front() != ' ' && value.front() != '\t' && value.back() != ' ' && value.back() != '\t'
    };
    if (!inside_cookies || !trimmed || value.contains(';')) {
        std::abort();
    }
    return 0;
}
