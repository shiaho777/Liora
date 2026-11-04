#pragma once

#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace liora::test {

struct Case {
  const char* name;
  std::function<void()> fn;
};

std::vector<Case>& registry();
int runAll();

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure {
  std::string message;
};

[[noreturn]] void fail(const char* file, int line, const std::string& message);

}  // namespace liora::test

#define LIORA_TEST(name)                                                       \
  static void name();                                                          \
  static ::liora::test::Registrar name##_registrar(#name, name);                \
  static void name()

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) ::liora::test::fail(__FILE__, __LINE__, "CHECK failed: " #cond); \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    if (!((a) == (b))) ::liora::test::fail(__FILE__, __LINE__, "CHECK_EQ failed: " #a " == " #b); \
  } while (0)

#define CHECK_NE(a, b)                                                         \
  do {                                                                         \
    if (!((a) != (b))) ::liora::test::fail(__FILE__, __LINE__, "CHECK_NE failed: " #a " != " #b); \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                  \
  do {                                                                         \
    const auto va = (a);                                                       \
    const auto vb = (b);                                                       \
    if (!(va >= vb - (eps) && va <= vb + (eps)))                               \
      ::liora::test::fail(__FILE__, __LINE__, "CHECK_NEAR failed: " #a);       \
  } while (0)

#define CHECK_THROWS(expr)                                                     \
  do {                                                                         \
    bool thrown = false;                                                       \
    try {                                                                      \
      (void)(expr);                                                            \
    } catch (...) {                                                            \
      thrown = true;                                                           \
    }                                                                          \
    if (!thrown) ::liora::test::fail(__FILE__, __LINE__, "expected an exception: " #expr); \
  } while (0)
