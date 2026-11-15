#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "web/bearer_auth.hpp"

namespace {

constexpr char TOKEN[]{"s3cr3t-T0ken"};

}  // namespace

// Offers any Authorization value. Access is granted only to a value carrying
// the token.
// libFuzzer calls the entry point by this name.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static const BearerAuth bearer_auth{BearerAuth::Config{.api_token = TOKEN}};

    const std::string_view authorization{reinterpret_cast<const char*>(data), size};
    if (bearer_auth.granted_for(authorization) && !authorization.contains(TOKEN)) {
        std::abort();
    }
    return 0;
}
