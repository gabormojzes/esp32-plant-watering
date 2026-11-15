#pragma once

#include <cstdint>

// Constants shared across components. One a single component owns stays with it.
namespace constants {

inline constexpr std::int64_t US_PER_S{1000000};
inline constexpr std::int64_t US_PER_MS{1000};
inline constexpr std::int64_t MS_PER_S{1000};

}  // namespace constants
