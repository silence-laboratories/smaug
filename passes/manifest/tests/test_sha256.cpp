// Known-answer tests for the clean-room SHA-256 (FIPS 180-4 examples + boundary
// cases around the 55/56/64-byte padding edges).
#include "ap3/Sha256.h"
#include "test_util.h"

#include <string>

using ap3::sha256_hex;
using namespace ap3test;

int main() {
  check_eq(sha256_hex(std::string("")),
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
           "sha256(\"\")");
  check_eq(sha256_hex(std::string("abc")),
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "sha256(\"abc\")");
  check_eq(sha256_hex(std::string("The quick brown fox jumps over the lazy dog")),
           "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
           "sha256(fox)");
  // 448-bit / two-block example from FIPS 180-4.
  check_eq(sha256_hex(std::string(
               "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
           "sha256(56-byte msg)");
  // Padding boundaries: 55 fits with length in one block, 56 forces a second.
  check_eq(sha256_hex(std::string(55, 'a')),
           "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
           "sha256(55*'a')");
  check_eq(sha256_hex(std::string(56, 'a')),
           "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
           "sha256(56*'a')");
  check_eq(sha256_hex(std::string(64, 'a')),
           "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
           "sha256(64*'a')");
  check_eq(sha256_hex(std::string(1000, 'a')),
           "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3",
           "sha256(1000*'a')");
  return report("sha256");
}
