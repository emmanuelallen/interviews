// Minimal GoogleTest-compatible shim, used only when the real GoogleTest isn't
// installed (CMakeLists.txt / run.sh pick the real one when it is).
// Supports: TEST, EXPECT_/ASSERT_ {EQ,NE,LT,LE,GT,GE,TRUE,FALSE} and
// --gtest_filter=POS[:POS...][-NEG[:NEG...]] with '*' and '?' wildcards.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace testing
{
namespace detail
{
struct TestCase
{
  const char*           suite;
  const char*           name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
  static std::vector<TestCase> r;
  return r;
}

inline bool& current_failed()
{
  static bool f = false;
  return f;
}

struct Registrar
{
  Registrar(const char* suite, const char* name, std::function<void()> fn)
  {
    registry().push_back({suite, name, std::move(fn)});
  }
};

template <typename T>
std::string show(const T& v)
{
  if constexpr (requires(std::ostream& os) { os << v; })
  {
    std::ostringstream os;
    if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
      os << static_cast<int>(v);
    else
      os << v;
    return os.str();
  }
  else if constexpr (std::is_enum_v<T>)
    return std::to_string(static_cast<long long>(v));
  else
    return "<unprintable>";
}

template <typename A, typename B, typename Op>
bool check(const A& a, const B& b, Op op, const char* ea, const char* eb, const char* ops,
           const char* file, int line)
{
  if (op(a, b))
    return true;
  current_failed() = true;
  std::printf("%s:%d: Failure\nExpected: (%s) %s (%s), actual: %s vs %s\n", file, line, ea, ops,
              eb, show(a).c_str(), show(b).c_str());
  return false;
}

inline bool check_bool(bool v, bool want, const char* e, const char* file, int line)
{
  if (v == want)
    return true;
  current_failed() = true;
  std::printf("%s:%d: Failure\nValue of: %s\n  Actual: %s\nExpected: %s\n", file, line, e,
              v ? "true" : "false", want ? "true" : "false");
  return false;
}

inline bool glob(const char* p, const char* s)
{
  if (*p == '\0')
    return *s == '\0';
  if (*p == '*')
    return glob(p + 1, s) || (*s && glob(p, s + 1));
  if (*s && (*p == '?' || *p == *s))
    return glob(p + 1, s + 1);
  return false;
}

inline bool any_match(const std::string& patterns, const std::string& name)
{
  std::stringstream ss(patterns);
  std::string       p;
  while (std::getline(ss, p, ':'))
    if (glob(p.c_str(), name.c_str()))
      return true;
  return false;
}
}  // namespace detail

inline int RunAllTests(const std::string& filter)
{
  std::string pos = filter, neg;
  if (auto dash = filter.find('-'); dash != std::string::npos)
  {
    pos = filter.substr(0, dash);
    neg = filter.substr(dash + 1);
  }
  if (pos.empty())
    pos = "*";

  std::vector<std::string> failed;
  int                      ran = 0;
  for (auto& t : detail::registry())
  {
    std::string full = std::string(t.suite) + "." + t.name;
    if (!detail::any_match(pos, full) || (!neg.empty() && detail::any_match(neg, full)))
      continue;
    ++ran;
    std::printf("[ RUN      ] %s\n", full.c_str());
    detail::current_failed() = false;
    t.fn();
    std::printf("%s %s\n", detail::current_failed() ? "[  FAILED  ]" : "[       OK ]", full.c_str());
    if (detail::current_failed())
      failed.push_back(full);
  }
  std::printf("[==========] %d tests ran.\n[  PASSED  ] %d tests.\n", ran,
              ran - static_cast<int>(failed.size()));
  if (!failed.empty())
  {
    std::printf("[  FAILED  ] %zu tests, listed below:\n", failed.size());
    for (auto& f : failed)
      std::printf("[  FAILED  ] %s\n", f.c_str());
  }
  return failed.empty() ? 0 : 1;
}
}  // namespace testing

#define TEST(suite, name)                                                                   \
  static void                        suite##_##name##_body();                               \
  static ::testing::detail::Registrar suite##_##name##_reg(#suite, #name, suite##_##name##_body); \
  static void                        suite##_##name##_body()

#define LX_CMP_(a, b, op) \
  ::testing::detail::check((a), (b), [](const auto& x, const auto& y) { return x op y; }, #a, #b, #op, __FILE__, __LINE__)

#define EXPECT_EQ(a, b) LX_CMP_(a, b, ==)
#define EXPECT_NE(a, b) LX_CMP_(a, b, !=)
#define EXPECT_LT(a, b) LX_CMP_(a, b, <)
#define EXPECT_LE(a, b) LX_CMP_(a, b, <=)
#define EXPECT_GT(a, b) LX_CMP_(a, b, >)
#define EXPECT_GE(a, b) LX_CMP_(a, b, >=)
#define EXPECT_TRUE(v) ::testing::detail::check_bool(static_cast<bool>(v), true, #v, __FILE__, __LINE__)
#define EXPECT_FALSE(v) ::testing::detail::check_bool(static_cast<bool>(v), false, #v, __FILE__, __LINE__)

#define ASSERT_EQ(a, b) if (!EXPECT_EQ(a, b)) return
#define ASSERT_NE(a, b) if (!EXPECT_NE(a, b)) return
#define ASSERT_LT(a, b) if (!EXPECT_LT(a, b)) return
#define ASSERT_LE(a, b) if (!EXPECT_LE(a, b)) return
#define ASSERT_GT(a, b) if (!EXPECT_GT(a, b)) return
#define ASSERT_GE(a, b) if (!EXPECT_GE(a, b)) return
#define ASSERT_TRUE(v) if (!EXPECT_TRUE(v)) return
#define ASSERT_FALSE(v) if (!EXPECT_FALSE(v)) return
