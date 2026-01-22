#include "CryptoHelpers.hpp"
#include <cryptopp/osrng.h>
#include <cryptopp/sha.h>
#include <cryptopp/pwdbased.h>
#include <cryptopp/filters.h>
#include <cryptopp/secblock.h>
#include <cryptopp/base64.h>
#include <cryptopp/hex.h>
#include <string>
#include <vector>

using namespace CryptoPP;

namespace CryptoHelpers {

std::string base64Encode(const std::vector<uint8_t>& data){
    std::string out;
    StringSource ss(data.data(), data.size(), true,
        new Base64Encoder(new StringSink(out), false /* do not insert newlines */));
    return out;
}

std::vector<uint8_t> base64Decode(const std::string& b64){
    std::string decoded;
    StringSource ss(b64, true, new Base64Decoder(new StringSink(decoded)));
    return std::vector<uint8_t>(decoded.begin(), decoded.end());
}

std::string generateSaltBase64(size_t n){
    AutoSeededRandomPool prng;
    SecByteBlock salt(n);
    prng.GenerateBlock(salt, salt.size());
    std::vector<uint8_t> v(salt.begin(), salt.end());
    return base64Encode(v);
}

std::string derivePBKDF2_Base64(const std::string& password, const std::string& saltBase64, int iterations, size_t outLen){
    std::vector<uint8_t> salt = base64Decode(saltBase64);
    SecByteBlock derived(outLen);
    PKCS5_PBKDF2_HMAC<SHA256> pbkdf;
    pbkdf.DeriveKey(derived, derived.size(), 0, (const byte*)password.data(), password.size(), salt.data(), salt.size(), iterations);
    std::vector<uint8_t> v(derived.begin(), derived.end());
    return base64Encode(v);
}

static bool constTimeEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b){
    if(a.size() != b.size()) return false;
    volatile uint8_t result = 0;
    for(size_t i=0;i<a.size();++i) result |= (a[i] ^ b[i]);
    return result == 0;
}

bool pbkdf2_verify(const std::string& password, const std::string& saltBase64, int iterations, const std::string& expectedHashBase64){
    std::string derivedB64 = derivePBKDF2_Base64(password, saltBase64, iterations);
    auto da = base64Decode(derivedB64);
    auto eb = base64Decode(expectedHashBase64);
    return constTimeEqual(da, eb);
}

} // namespace CryptoHelpers
