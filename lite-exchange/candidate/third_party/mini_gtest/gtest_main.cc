#include <cstring>
#include <string>

#include "gtest/gtest.h"

int main(int argc, char** argv)
{
  std::string filter = "*";
  for (int i = 1; i < argc; ++i)
    if (std::strncmp(argv[i], "--gtest_filter=", 15) == 0)
      filter = argv[i] + 15;
  return ::testing::RunAllTests(filter);
}
