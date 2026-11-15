#include <doctest.h>

#include "web/bearer_auth.hpp"

namespace {

constexpr char VALID[]{"Bearer s3cret-token"};

}  // namespace

TEST_CASE("a configured token is demanded") {
    const BearerAuth auth{BearerAuth::Config{.api_token = "s3cret-token"}};

    SUBCASE("the exact header is granted") {
        CHECK(auth.granted_for(VALID));
    }
    SUBCASE("a wrong token is refused") {
        CHECK_FALSE(auth.granted_for("Bearer wrong-token"));
    }
    SUBCASE("a truncated token is refused") {
        CHECK_FALSE(auth.granted_for("Bearer s3cret-toke"));
    }
    SUBCASE("a token with a byte appended is refused") {
        CHECK_FALSE(auth.granted_for("Bearer s3cret-tokenx"));
    }
    SUBCASE("the scheme alone is refused") {
        CHECK_FALSE(auth.granted_for("Bearer"));
        CHECK_FALSE(auth.granted_for("Bearer "));
    }
    SUBCASE("the token without the scheme is refused") {
        CHECK_FALSE(auth.granted_for("s3cret-token"));
    }
    SUBCASE("a longer scheme that starts the same way is refused") {
        CHECK_FALSE(auth.granted_for("Bearerish s3cret-token"));
    }
    SUBCASE("the token run onto the scheme is refused") {
        CHECK_FALSE(auth.granted_for("Bearers3cret-token"));
    }
    SUBCASE("another scheme is refused") {
        CHECK_FALSE(auth.granted_for("Basic s3cret-token"));
    }
    SUBCASE("an absent or blank header is refused") {
        CHECK_FALSE(auth.granted_for(""));
        CHECK_FALSE(auth.granted_for("   "));
    }
}

// Per RFC 9110, the scheme token is case-insensitive, one or more spaces part it
// from the credential, and whitespace either side of the field is ignored. The
// token itself is compared as the bytes it was configured as.
TEST_CASE("a token is taken however the field around it is spelled") {
    const BearerAuth auth{BearerAuth::Config{.api_token = "s3cret-token"}};

    SUBCASE("the scheme in any case") {
        CHECK(auth.granted_for("bearer s3cret-token"));
        CHECK(auth.granted_for("BEARER s3cret-token"));
        CHECK(auth.granted_for("BeArEr s3cret-token"));
    }
    SUBCASE("more than one space after the scheme") {
        CHECK(auth.granted_for("Bearer   s3cret-token"));
    }
    SUBCASE("a tab as the separator") {
        CHECK(auth.granted_for("Bearer\ts3cret-token"));
    }
    SUBCASE("whitespace padding either side") {
        CHECK(auth.granted_for("  Bearer s3cret-token  "));
        CHECK(auth.granted_for("\tBearer s3cret-token\t"));
    }
    SUBCASE("the token's own case still counts") {
        CHECK_FALSE(auth.granted_for("Bearer S3CRET-TOKEN"));
    }
    // Same length as the valid one, leaving the compare to refuse it
    SUBCASE("whitespace inside the token is not padding") {
        CHECK_FALSE(auth.granted_for("Bearer s3cret token"));
    }
}
