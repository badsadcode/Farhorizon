#pragma once
// Minimal self-registering test harness (no external dependency).
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace fhtest {

struct TestCase {
  const char* name;
  std::function<void()> fn;
};
std::vector<TestCase>& registry();
void reportFailure(const char* file, int line, const std::string& msg);

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

}  // namespace fhtest

#define FH_CONCAT2(a, b) a##b
#define FH_CONCAT(a, b) FH_CONCAT2(a, b)
#define TEST(name)                                                              \
  static void name();                                                           \
  static ::fhtest::Registrar FH_CONCAT(reg_, name)(#name, &name);               \
  static void name()

#define CHECK(cond)                                                             \
  do {                                                                          \
    if (!(cond)) ::fhtest::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                   \
  do {                                                                          \
    const double fh_a = static_cast<double>(a), fh_b = static_cast<double>(b);  \
    if (!(std::fabs(fh_a - fh_b) <= (eps)))                                     \
      ::fhtest::reportFailure(__FILE__, __LINE__,                               \
                              std::string("CHECK_NEAR(" #a ", " #b ") got ") +  \
                                  std::to_string(fh_a) + " vs " + std::to_string(fh_b)); \
  } while (0)

#define CHECK_VEC_NEAR(a, b, eps) \
  do {                            \
    CHECK_NEAR((a).x, (b).x, eps); \
    CHECK_NEAR((a).y, (b).y, eps); \
    CHECK_NEAR((a).z, (b).z, eps); \
  } while (0)
