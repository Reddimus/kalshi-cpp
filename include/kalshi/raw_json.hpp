#pragma once

#include <string>

namespace kalshi {

/// A JSON value kept as text, for free-form fields such as product metadata.
/// `text` is empty when the field was absent.
struct RawJson {
	std::string text;

	friend bool operator==(const RawJson&, const RawJson&) = default;
};

} // namespace kalshi
