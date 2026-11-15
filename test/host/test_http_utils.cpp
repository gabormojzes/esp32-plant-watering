#include <doctest.h>

#include "shared/http_utils.hpp"

namespace {

constexpr char JSON[]{"application/json"};

}  // namespace

TEST_CASE("to_lower_ascii folds the capital letters") {
    CHECK(http_utils::to_lower_ascii('A') == 'a');
    CHECK(http_utils::to_lower_ascii('J') == 'j');
    CHECK(http_utils::to_lower_ascii('Z') == 'z');
}

TEST_CASE("to_lower_ascii leaves every other character as it is") {
    CHECK(http_utils::to_lower_ascii('a') == 'a');
    CHECK(http_utils::to_lower_ascii('0') == '0');
    CHECK(http_utils::to_lower_ascii('/') == '/');
    // The neighbors of the capitals, which a fold by setting 0x20 would turn
    // into '`' and '{'
    CHECK(http_utils::to_lower_ascii('@') == '@');
    CHECK(http_utils::to_lower_ascii('[') == '[');
    // A UTF-8 lead byte, negative where char is signed
    CHECK(http_utils::to_lower_ascii('\xC3') == '\xC3');
}

TEST_CASE("is_ascii_case_insensitive_equal matches text that differs at most in letter case") {
    CHECK(http_utils::is_ascii_case_insensitive_equal("application/json", "Application/JSON"));
    CHECK(http_utils::is_ascii_case_insensitive_equal("application/json", "application/json"));
    CHECK(http_utils::is_ascii_case_insensitive_equal("", ""));
}

TEST_CASE("is_ascii_case_insensitive_equal refuses text spelled otherwise or of another length") {
    CHECK_FALSE(
        http_utils::is_ascii_case_insensitive_equal("application/json", "application/jsom")
    );
    CHECK_FALSE(http_utils::is_ascii_case_insensitive_equal("application/json", "application/jso"));
    CHECK_FALSE(http_utils::is_ascii_case_insensitive_equal("application/jso", "application/json"));
    CHECK_FALSE(http_utils::is_ascii_case_insensitive_equal("", "a"));
}

TEST_CASE("is_ascii_case_insensitive_equal folds nothing but the letters") {
    CHECK_FALSE(http_utils::is_ascii_case_insensitive_equal("@", "`"));
    CHECK_FALSE(http_utils::is_ascii_case_insensitive_equal("[", "{"));
}

TEST_CASE("media_type is the type and subtype ahead of any parameters") {
    CHECK(http_utils::media_type("application/json") == "application/json");
    CHECK(http_utils::media_type("application/json;charset=utf-8") == "application/json");
    CHECK(http_utils::media_type("application/json ; charset=utf-8") == "application/json");
    CHECK(http_utils::media_type("application/json\t;charset=utf-8") == "application/json");
}

TEST_CASE("media_type leaves out the whitespace padding the value") {
    CHECK(http_utils::media_type(" \tapplication/json") == "application/json");
    CHECK(http_utils::media_type("application/json \t") == "application/json");
}

TEST_CASE("media_type is empty for a value that names no type") {
    CHECK(http_utils::media_type("").empty());
    CHECK(http_utils::media_type(" \t").empty());
    CHECK(http_utils::media_type("; charset=utf-8").empty());
}

TEST_CASE("is_media_type accepts the type in any letter case") {
    CHECK(http_utils::is_media_type("application/json", JSON));
    CHECK(http_utils::is_media_type("APPLICATION/JSON", JSON));
    CHECK(http_utils::is_media_type("Application/Json", JSON));
}

TEST_CASE("is_media_type accepts the type carrying parameters or padded with whitespace") {
    CHECK(http_utils::is_media_type("application/json; charset=utf-8", JSON));
    CHECK(http_utils::is_media_type("Application/JSON;charset=UTF-8", JSON));
    CHECK(http_utils::is_media_type("\tapplication/json", JSON));
    CHECK(http_utils::is_media_type("application/json ", JSON));
}

TEST_CASE("is_media_type refuses a type spelled only partly like the one expected") {
    CHECK_FALSE(http_utils::is_media_type("application/jsonl", JSON));
    CHECK_FALSE(http_utils::is_media_type("application/json-patch+json", JSON));
    CHECK_FALSE(http_utils::is_media_type("application/js", JSON));
    CHECK_FALSE(http_utils::is_media_type("application/ json", JSON));
}

TEST_CASE("is_media_type refuses a value that names no type") {
    CHECK_FALSE(http_utils::is_media_type("", JSON));
    CHECK_FALSE(http_utils::is_media_type(" ", JSON));
    CHECK_FALSE(http_utils::is_media_type("; charset=utf-8", JSON));
}

TEST_CASE(
    "is_cors_safelisted_media_type names the types a cross-origin page posts without a preflight"
) {
    CHECK(http_utils::is_cors_safelisted_media_type("text/plain"));
    CHECK(http_utils::is_cors_safelisted_media_type("application/x-www-form-urlencoded"));
    CHECK(http_utils::is_cors_safelisted_media_type("multipart/form-data; boundary=x"));
    CHECK(http_utils::is_cors_safelisted_media_type("Text/Plain; charset=utf-8"));
}

TEST_CASE("is_cors_safelisted_media_type leaves out a type that is preflighted") {
    CHECK_FALSE(http_utils::is_cors_safelisted_media_type(JSON));
    CHECK_FALSE(http_utils::is_cors_safelisted_media_type("text/html"));
    CHECK_FALSE(http_utils::is_cors_safelisted_media_type("text/plainx"));
}

TEST_CASE("is_token68 accepts letters, digits and the punctuation token68 allows") {
    CHECK(http_utils::is_token68("abcXYZ019"));
    CHECK(http_utils::is_token68("-._~+/"));
    CHECK(http_utils::is_token68("a"));
}

TEST_CASE("is_token68 accepts = as padding at the end") {
    CHECK(http_utils::is_token68("dGVzdA=="));
    CHECK(http_utils::is_token68("a="));
}

TEST_CASE("is_token68 refuses = anywhere but the end") {
    CHECK_FALSE(http_utils::is_token68("a=b"));
    CHECK_FALSE(http_utils::is_token68("=a"));
    CHECK_FALSE(http_utils::is_token68("a==b="));
}

TEST_CASE("is_token68 refuses empty text and padding with nothing before it") {
    CHECK_FALSE(http_utils::is_token68(""));
    CHECK_FALSE(http_utils::is_token68("="));
    CHECK_FALSE(http_utils::is_token68("=="));
}

TEST_CASE("is_token68 refuses whitespace and every other character") {
    CHECK_FALSE(http_utils::is_token68("a b"));
    CHECK_FALSE(http_utils::is_token68("a\tb"));
    CHECK_FALSE(http_utils::is_token68("a\"b"));
    CHECK_FALSE(http_utils::is_token68("a,b"));
    CHECK_FALSE(http_utils::is_token68("a:b"));
    CHECK_FALSE(http_utils::is_token68("\xC3\xA4"));
}

TEST_CASE("is_from_another_origin names a request another origin made") {
    CHECK(http_utils::is_from_another_origin("cross-site"));
    CHECK(http_utils::is_from_another_origin("same-site"));
}

TEST_CASE("is_from_another_origin passes the device's own requests and the user's") {
    CHECK_FALSE(http_utils::is_from_another_origin("same-origin"));
    CHECK_FALSE(http_utils::is_from_another_origin("none"));
    CHECK_FALSE(http_utils::is_from_another_origin(""));
}
