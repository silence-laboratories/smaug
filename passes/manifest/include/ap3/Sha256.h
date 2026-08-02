// Clean-room SHA-256 (FIPS 180-4). Dependency-free so the AP3 manifest core is
// buildable and testable anywhere - natively for a fast loop and inside the
// hermetic image - without OpenSSL or LLVM. The manifest_hash is the cross-org
// consent anchor, so this must be byte-exact with Python's hashlib.sha256.
#pragma once

#include <cstdint>
#include <string>

namespace ap3 {

// Lowercase hex SHA-256 of the given bytes.
std::string sha256_hex(const void *data, std::size_t len);
std::string sha256_hex(const std::string &s);

} // namespace ap3
