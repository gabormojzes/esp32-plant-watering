#include "web/cookie.hpp"

#include "shared/string_utils.hpp"

namespace {

// RFC 6265 separates pairs with "; ". The space binds senders, not readers.
constexpr char SEPARATOR{';'};
constexpr char ASSIGNMENT{'='};

}  // namespace

std::string_view cookie::value(std::string_view cookies, std::string_view name) {
    // No pair is stored under an empty name.
    if (name.empty()) {
        return {};
    }

    std::size_t begin{0};
    while (begin < cookies.size()) {
        std::size_t end{cookies.find(SEPARATOR, begin)};
        if (end == std::string_view::npos) {
            end = cookies.size();
        }

        const std::string_view pair{string_utils::trimmed(cookies.substr(begin, end - begin))};
        const std::size_t assignment_at{pair.find(ASSIGNMENT)};
        // A pair spelling no name and value at all is skipped rather than read
        // as a name with an empty value.
        if (assignment_at != std::string_view::npos &&
            string_utils::trimmed(pair.substr(0, assignment_at)) == name) {
            return string_utils::trimmed(pair.substr(assignment_at + 1));
        }

        begin = end + 1;
    }
    return {};
}
