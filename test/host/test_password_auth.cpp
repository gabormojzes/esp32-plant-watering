#include <doctest.h>

#include "web/password_auth.hpp"

TEST_CASE("a configured credential is demanded") {
    const PasswordAuth auth{PasswordAuth::Config{.user = "user", .password = "password"}};

    SUBCASE("both halves right is granted") {
        CHECK(auth.granted_for("user", "password"));
    }
    SUBCASE("a wrong password is refused") {
        CHECK_FALSE(auth.granted_for("user", "wrong"));
    }
    SUBCASE("a wrong user is refused") {
        CHECK_FALSE(auth.granted_for("other", "password"));
    }
    SUBCASE("neither right is refused") {
        CHECK_FALSE(auth.granted_for("other", "wrong"));
    }
    SUBCASE("an empty offer is refused") {
        CHECK_FALSE(auth.granted_for("", ""));
    }
    SUBCASE("the halves are not interchangeable") {
        CHECK_FALSE(auth.granted_for("password", "user"));
    }
    // Compared byte for byte, since no protocol folds the case of either half.
    SUBCASE("case counts in both halves") {
        CHECK_FALSE(auth.granted_for("User", "password"));
        CHECK_FALSE(auth.granted_for("user", "Password"));
    }
    // Nothing trims a credential, and what the body carried is what is compared.
    SUBCASE("padding is part of what is offered") {
        CHECK_FALSE(auth.granted_for("user ", "password"));
        CHECK_FALSE(auth.granted_for("user", " password"));
    }
    SUBCASE("a prefix of either half is refused") {
        CHECK_FALSE(auth.granted_for("use", "password"));
        CHECK_FALSE(auth.granted_for("user", "passwor"));
    }
    SUBCASE("a half with a byte appended is refused") {
        CHECK_FALSE(auth.granted_for("users", "password"));
        CHECK_FALSE(auth.granted_for("user", "passwords"));
    }
}
