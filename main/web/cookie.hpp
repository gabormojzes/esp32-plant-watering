#pragma once

#include <string_view>

// The cookies one Cookie header carries
namespace cookie {

// Returns the value stored under `name`, or empty when `cookies` has none.
// The view points into `cookies`, and the value is not decoded.
std::string_view value(std::string_view cookies, std::string_view name);

}  // namespace cookie
