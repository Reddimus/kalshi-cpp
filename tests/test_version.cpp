// Pinning tests for kalshi::VERSION. The constant is generated from
// CMakeLists.txt's PROJECT_VERSION via configure_file(); this test makes
// the round-trip load-bearing so a future hand-edit to version.hpp.in
// that breaks the substitution shows up in CI.

#include <gtest/gtest.h>
#include <kalshi/kalshi.hpp>
#include <regex>
#include <string>

TEST(VersionTest, VersionStringNotEmpty) {
	EXPECT_NE(std::string(kalshi::VERSION), "");
}

TEST(VersionTest, VersionStringMatchesSemver) {
	// Driven by CMake's PROJECT_VERSION which CMake itself validates as
	// dotted-decimal — this regex is the post-substitution sanity check.
	const std::regex semver(R"(^[0-9]+\.[0-9]+\.[0-9]+$)");
	EXPECT_TRUE(std::regex_match(std::string(kalshi::VERSION), semver))
		<< "VERSION='" << kalshi::VERSION << "' is not MAJOR.MINOR.PATCH";
}

TEST(VersionTest, ComponentsRoundTripToVersionString) {
	// If MAJOR/MINOR/PATCH and VERSION drift apart, the substitution in
	// version.hpp.in is broken — fail loudly so a regression doesn't ship.
	const std::string assembled = std::to_string(kalshi::VERSION_MAJOR) + "." +
								  std::to_string(kalshi::VERSION_MINOR) + "." +
								  std::to_string(kalshi::VERSION_PATCH);
	EXPECT_EQ(assembled, std::string(kalshi::VERSION));
}
