#include "passwordhash.h"

#include <cstddef>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <stdexcept>

namespace 
{
constexpr int ITERATIONS = 600000;
constexpr size_t SALT_BYTES = 16;
constexpr size_t KEY_BYTES = 32;

const std::string PREFIX = "pbkdf2-sha256$600000$";

std::string Hex(const unsigned char* bytes, size_t length)
{
    static const char digites[] = "0123456789abcdef";

    std::string result;
    result.reserve(length * 2);

    for (size_t i = 0; i < length; ++ i)
    {
        result += digites[bytes[i] >> 4];
        result += digites[bytes[i] & 15];
    }

    return result;
}

bool Unhex(const std::string& text, unsigned char* output, std::size_t length)
{
    if (text.size() != length * 2)
        return false;

    auto digit = [](char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };

    for (std::size_t i = 0; i < length; ++i)
    {
        const int high = digit(text[i * 2]);
        const int low = digit(text[i * 2 + 1]);

        if (high < 0 || low < 0)
            return false;

        output[i] =
            static_cast<unsigned char>((high << 4) | low);
    }
    return true;
}

std::array<unsigned char, KEY_BYTES> Derive(const std::string& password, const std::array<unsigned char, SALT_BYTES>& salt)
{
    if (password.empty() || password.size() > 128)
        throw std::invalid_argument("invalid password length");

    std::array<unsigned char, KEY_BYTES> key{};

    if (PKCS5_PBKDF2_HMAC(
            password.data(),
            static_cast<int>(password.size()),
            salt.data(),
            static_cast<int>(salt.size()),
            ITERATIONS,
            EVP_sha256(),
            static_cast<int>(key.size()),
            key.data()) != 1)
        throw std::runtime_error("password derivation failed");

    return key;
}
}

// return encoded
std::string HashPwd(const std::string& password)
{
    std::array<unsigned char, SALT_BYTES> salt{};

    if (RAND_bytes(
            salt.data(),
            static_cast<int>(salt.size())) != 1)
        throw std::runtime_error("password salt generation failed");

    auto key = Derive(password, salt);

    const std::string encoded =
        PREFIX
        + Hex(salt.data(), salt.size())
        + "$"
        + Hex(key.data(), key.size());

    OPENSSL_cleanse(key.data(), key.size());

    return encoded;
}

bool VerifyPwd(const std::string& password, const std::string& encoded)
{
    const auto saltStart = PREFIX.size();
    const auto separator = saltStart + SALT_BYTES * 2;

    if (encoded.size() != separator + 1 + KEY_BYTES * 2
        || encoded.compare(0, saltStart, PREFIX) != 0
        || encoded[separator] != '$')
    {
        throw std::runtime_error("unsupported stored password format");
    }

    std::array<unsigned char, SALT_BYTES> salt{};
    std::array<unsigned char, KEY_BYTES> expected{};

    if (!Unhex(
            encoded.substr(saltStart, SALT_BYTES * 2),
            salt.data(),
            salt.size())
        || !Unhex(
            encoded.substr(separator + 1),
            expected.data(),
            expected.size()))
    {
        throw std::runtime_error("invalid stored password encoding");
    }

    auto actual = Derive(password, salt);

    const bool matches =
        CRYPTO_memcmp(
            actual.data(),
            expected.data(),
            actual.size()) == 0;

    OPENSSL_cleanse(actual.data(), actual.size());
    OPENSSL_cleanse(expected.data(), expected.size());

    return matches;
}

