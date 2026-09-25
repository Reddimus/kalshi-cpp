// Shared setup for the examples: environment selection and API keys.
#pragma once

#include <cstdlib>
#include <iostream>
#include <kalshi/kalshi.hpp>
#include <optional>
#include <string_view>

namespace example {

/// KALSHI_ENV=demo selects the demo exchange; anything else is production.
inline kalshi::Environment environment() {
	const char* env = std::getenv("KALSHI_ENV");
	return env != nullptr && std::string_view{env} == "demo" ? kalshi::Environment::Demo
															 : kalshi::Environment::Production;
}

/// Loads KALSHI_API_KEY_ID and the PEM file named by KALSHI_API_KEY_FILE.
inline std::optional<kalshi::Signer> signer() {
	const char* id = std::getenv("KALSHI_API_KEY_ID");
	const char* file = std::getenv("KALSHI_API_KEY_FILE");
	if (id == nullptr || file == nullptr) {
		std::cerr << "Set KALSHI_API_KEY_ID and KALSHI_API_KEY_FILE. Create a key under "
					 "API keys at https://kalshi.com/account/profile.\n";
		return std::nullopt;
	}
	kalshi::Result<kalshi::Signer> loaded = kalshi::Signer::from_pem_file(id, file);
	if (!loaded) {
		std::cerr << loaded.error().message << '\n';
		return std::nullopt;
	}
	return std::move(*loaded);
}

inline int fail(const kalshi::Error& error) {
	std::cerr << error.message << '\n';
	return 1;
}

} // namespace example
