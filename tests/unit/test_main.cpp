#include <cstdlib>
#include <cstring>

#include "engine/core/log.h"
#include "test_framework.h"

namespace fhtest {
std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}
static int g_failures = 0;
void reportFailure(const char* file, int line, const std::string& msg) {
  ++g_failures;
  std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, msg.c_str());
}
}  // namespace fhtest

int main(int argc, char** argv) {
  // Many tests exercise error paths on purpose; set FH_TEST_VERBOSE=1 to see their logs.
  fh::log::setMinLevel(std::getenv("FH_TEST_VERBOSE") ? fh::log::Level::Debug : fh::log::Level::Off);
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int run = 0, failedTests = 0;
  for (auto& t : fhtest::registry()) {
    if (filter && !std::strstr(t.name, filter)) continue;
    const int before = fhtest::g_failures;
    t.fn();
    ++run;
    const bool ok = fhtest::g_failures == before;
    if (!ok) ++failedTests;
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
  }
  std::printf("\n%d tests, %d failed\n", run, failedTests);
  return failedTests == 0 ? 0 : 1;
}
