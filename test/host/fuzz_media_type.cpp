#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "shared/http_utils.hpp"

// Reads any Content-Type value. The type found lies inside it, carries no padding
// or parameters, and matches itself.
// libFuzzer calls the entry point by this name.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view content_type{reinterpret_cast<const char*>(data), size};
    http_utils::is_cors_safelisted_media_type(content_type);

    const std::string_view type{http_utils::media_type(content_type)};
    if (type.empty()) {
        return 0;
    }
    const bool inside_value{
        type.data() >= content_type.data() &&
        type.data() + type.size() <= content_type.data() + content_type.size()
    };
    if (!inside_value || type.find_first_of("; \t") != std::string_view::npos ||
        !http_utils::is_media_type(content_type, type)) {
        std::abort();
    }
    return 0;
}
