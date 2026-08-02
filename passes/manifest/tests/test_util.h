// Minimal test harness — no external framework, so the manifest core tests build
// and run anywhere (natively and inside the hermetic image).
#pragma once

#include <cstdio>
#include <string>

namespace ap3test {

inline int &failures() {
  static int n = 0;
  return n;
}

inline void check(bool cond, const std::string &what) {
  if (cond) {
    std::printf("  ok   %s\n", what.c_str());
  } else {
    std::printf("  FAIL %s\n", what.c_str());
    ++failures();
  }
}

inline void check_eq(const std::string &got, const std::string &want,
                     const std::string &what) {
  if (got == want) {
    std::printf("  ok   %s\n", what.c_str());
  } else {
    std::printf("  FAIL %s\n    want: %s\n    got:  %s\n", what.c_str(),
                want.c_str(), got.c_str());
    ++failures();
  }
}

inline int report(const char *suite) {
  if (failures() == 0) {
    std::printf("[PASS] %s\n", suite);
    return 0;
  }
  std::printf("[FAIL] %s: %d failure(s)\n", suite, failures());
  return 1;
}

} // namespace ap3test
