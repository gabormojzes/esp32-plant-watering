#pragma once

#include <cstdint>

struct Color {
    // Full channel value, and the top of scaled()'s brightness range
    static constexpr std::uint8_t MAX_LEVEL{255};

    bool operator==(const Color&) const = default;

    constexpr Color scaled(std::uint8_t level) const {
        return {
            .red = static_cast<std::uint8_t>(red * level / MAX_LEVEL),
            .green = static_cast<std::uint8_t>(green * level / MAX_LEVEL),
            .blue = static_cast<std::uint8_t>(blue * level / MAX_LEVEL),
        };
    }

    std::uint8_t red{0};
    std::uint8_t green{0};
    std::uint8_t blue{0};
};

// Named colors at full value. Use scaled() for brightness.
namespace palette {

inline constexpr Color OFF{.red = 0, .green = 0, .blue = 0};
inline constexpr Color RED{.red = 255, .green = 0, .blue = 0};
inline constexpr Color YELLOW{.red = 255, .green = 255, .blue = 0};
inline constexpr Color GREEN{.red = 0, .green = 255, .blue = 0};
inline constexpr Color BLUE{.red = 0, .green = 0, .blue = 255};
inline constexpr Color WHITE{.red = 255, .green = 255, .blue = 255};

}  // namespace palette
