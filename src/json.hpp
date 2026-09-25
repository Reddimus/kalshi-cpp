#pragma once

// Private JSON support. Glaze stays out of installed headers.

#include "kalshi/raw_json.hpp"

#include <glaze/core/feature_test.hpp>
#include <glaze/glaze.hpp>

// Another project may have declared an older Glaze through FetchContent first.
#if !defined(glaze_v8_3_0_tuple)
#error "kalshi-cpp requires Glaze 8.3 or newer"
#endif

template <>
struct glz::meta<kalshi::RawJson> {
	static constexpr auto read = [](kalshi::RawJson& value, const glz::raw_json& raw) { // auto-ok
		value.text = raw.str;
	};
	static constexpr auto write = [](const kalshi::RawJson& value) { // auto-ok
		return glz::raw_json_view{value.text.empty() ? std::string_view{"null"}
													 : std::string_view{value.text}};
	};
	static constexpr auto value = glz::custom<read, write>; // auto-ok: Glaze wrapper type
};
