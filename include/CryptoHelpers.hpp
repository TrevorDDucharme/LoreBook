#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace CryptoHelpers {
    // Generate a random salt of `n` bytes, return as base64 string
    std::string generateSaltBase64(size_t n = 16);
    // Derive a PBKDF2-HMAC-SHA256 key from `password` and base64-encoded salt. Returns base64-encoded binary output.
    std::string derivePBKDF2_Base64(const std::string& password, const std::string& saltBase64, int iterations = 100000, size_t outLen = 32);
    // Verify a password against a stored base64-derived key using constant-time comparison
    bool pbkdf2_verify(const std::string& password, const std::string& saltBase64, int iterations, const std::string& expectedHashBase64);

    // Helper: base64 encode/decode
    std::string base64Encode(const std::vector<uint8_t>& data);
    std::vector<uint8_t> base64Decode(const std::string& b64);
}
