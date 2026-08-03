#pragma once
#include <string>

namespace dfts::crypto {

// Lowercase hex SHA-1 of the input bytes. Used by the VOD recovery URL scheme.
std::string sha1Hex(const std::string& data);

} // namespace dfts::crypto
