#pragma once

#include <cstddef>
#include <string_view>

namespace pem_utils {

// True when `text` holds a PEM block, a BEGIN line with an END line after it.
// The base64 between them is left to the parser.
constexpr bool is_pem(std::string_view text) {
    const std::size_t begin{text.find("-----BEGIN ")};
    return begin != std::string_view::npos &&
           text.find("-----END ", begin) != std::string_view::npos;
}

// True when `text` holds an encrypted private key, in the PKCS#8 form or under
// the older OpenSSL Proc-Type header.
constexpr bool is_encrypted_key(std::string_view text) {
    return text.contains("-----BEGIN ENCRYPTED PRIVATE KEY-----") ||
           text.contains("Proc-Type: 4,ENCRYPTED");
}

}  // namespace pem_utils
