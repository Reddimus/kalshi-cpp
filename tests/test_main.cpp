// Minimal test framework
#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct TestResult {
	std::string name;
	bool passed;
	std::string message;
};

std::vector<TestResult> g_results;

#define TEST(name)                                             \
	void test_##name();                                        \
	struct TestRegister_##name {                               \
		TestRegister_##name() {                                \
			try {                                              \
				test_##name();                                 \
				g_results.push_back({#name, true, ""});        \
			} catch (const std::exception& e) {                \
				g_results.push_back({#name, false, e.what()}); \
			}                                                  \
		}                                                      \
	} g_register_##name;                                       \
	void test_##name()

#define ASSERT_TRUE(expr) \
	if (!(expr))          \
	throw std::runtime_error("Assertion failed: " #expr)

#define ASSERT_FALSE(expr) \
	if (expr)              \
	throw std::runtime_error("Assertion failed: NOT " #expr)

#define ASSERT_EQ(a, b) \
	if ((a) != (b))     \
	throw std::runtime_error("Assertion failed: " #a " == " #b)

#define ASSERT_NE(a, b) \
	if ((a) == (b))     \
	throw std::runtime_error("Assertion failed: " #a " != " #b)

int main() {
	int passed = 0;
	int failed = 0;

	std::cout << "Running " << g_results.size() << " tests...\n\n";

	for (const auto& result : g_results) {
		if (result.passed) {
			std::cout << "✓ " << result.name << "\n";
			++passed;
		} else {
			std::cout << "✗ " << result.name << ": " << result.message << "\n";
			++failed;
		}
	}

	std::cout << "\n" << passed << " passed, " << failed << " failed\n";
	return failed > 0 ? 1 : 0;
}
