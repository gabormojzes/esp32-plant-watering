#include <doctest.h>

#include "shared/pem_utils.hpp"

namespace {

constexpr char CERTIFICATE[]{R"(-----BEGIN CERTIFICATE-----
MIIBszCCAVmgAwIBAgIUQ2Vy
-----END CERTIFICATE-----
)"};

constexpr char EC_KEY[]{R"(-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIEtleQ==
-----END EC PRIVATE KEY-----
)"};

constexpr char PKCS8_KEY[]{R"(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49
-----END PRIVATE KEY-----
)"};

constexpr char PKCS8_ENCRYPTED_KEY[]{R"(-----BEGIN ENCRYPTED PRIVATE KEY-----
MIHsMFcGCSqGSIb3DQEF
-----END ENCRYPTED PRIVATE KEY-----
)"};

constexpr char OPENSSL_ENCRYPTED_KEY[]{R"(-----BEGIN RSA PRIVATE KEY-----
Proc-Type: 4,ENCRYPTED
DEK-Info: AES-128-CBC,0123456789ABCDEF0123456789ABCDEF

MIIEowIBAAKCAQEA
-----END RSA PRIVATE KEY-----
)"};

}  // namespace

TEST_CASE("is_pem accepts a certificate and every private key form") {
    CHECK(pem_utils::is_pem(CERTIFICATE));
    CHECK(pem_utils::is_pem(EC_KEY));
    CHECK(pem_utils::is_pem(PKCS8_KEY));
    CHECK(pem_utils::is_pem(PKCS8_ENCRYPTED_KEY));
    CHECK(pem_utils::is_pem(OPENSSL_ENCRYPTED_KEY));
}

TEST_CASE("is_pem accepts a chain of several blocks") {
    constexpr char chain[]{R"(-----BEGIN CERTIFICATE-----
MIIB
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIC
-----END CERTIFICATE-----
)"};
    CHECK(pem_utils::is_pem(chain));
}

TEST_CASE("is_pem rejects text that is no PEM block") {
    CHECK_FALSE(pem_utils::is_pem(""));
    CHECK_FALSE(pem_utils::is_pem("certs/ca.pem"));
    CHECK_FALSE(pem_utils::is_pem("MIIBszCCAVmgAwIBAgIUQ2Vy"));
}

TEST_CASE("is_pem rejects a block missing its END line or with it ahead of BEGIN") {
    CHECK_FALSE(pem_utils::is_pem("-----BEGIN CERTIFICATE-----\nMIIB\n"));
    CHECK_FALSE(
        pem_utils::is_pem("-----END CERTIFICATE-----\n-----BEGIN CERTIFICATE-----\nMIIB\n")
    );
}

TEST_CASE("is_encrypted_key spots both encrypted key forms") {
    CHECK(pem_utils::is_encrypted_key(PKCS8_ENCRYPTED_KEY));
    CHECK(pem_utils::is_encrypted_key(OPENSSL_ENCRYPTED_KEY));
}

TEST_CASE("is_encrypted_key passes unencrypted keys and certificates") {
    CHECK_FALSE(pem_utils::is_encrypted_key(""));
    CHECK_FALSE(pem_utils::is_encrypted_key(EC_KEY));
    CHECK_FALSE(pem_utils::is_encrypted_key(PKCS8_KEY));
    CHECK_FALSE(pem_utils::is_encrypted_key(CERTIFICATE));
}
