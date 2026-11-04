#include "harness.hpp"

#include <cstring>
#include <iostream>

namespace liora::test {

std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

void fail(const char* file, int line, const std::string& message) {
  throw Failure{std::string(file) + ":" + std::to_string(line) + ": " + message};
}

int runAll() {
  int passed = 0;
  std::vector<std::string> failures;
  for (const auto& c : registry()) {
    try {
      c.fn();
      ++passed;
      std::cout << "  ok   " << c.name << "\n";
    } catch (const Failure& f) {
      failures.push_back(std::string(c.name) + "\n    " + f.message);
      std::cout << "  FAIL " << c.name << "\n";
    } catch (const std::exception& e) {
      failures.push_back(std::string(c.name) + "\n    uncaught exception: " + e.what());
      std::cout << "  FAIL " << c.name << "\n";
    }
  }
  std::cout << "\n" << passed << " passed, " << failures.size() << " failed, " << registry().size()
            << " total\n";
  for (const auto& f : failures) std::cout << "\nFAILED: " << f << "\n";
  return failures.empty() ? 0 : 1;
}

}  // namespace liora::test

int main() { return liora::test::runAll(); }
