#include "json_nulls.hpp"

#include <algorithm>
#include <vector>

namespace kalshi::detail {

std::string strip_null_members(std::string_view json) {
	struct Frame {
		bool object{false};
		bool expect_key{false};
		std::size_t kept{0};
	};
	std::vector<Frame> frames;
	std::string out;
	out.reserve(json.size());
	std::size_t i = 0;

	const auto skip_space = [&] { // auto-ok: lambda
		while (i < json.size() &&
			   (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) {
			++i;
		}
	};
	// Returns the string token starting at json[i] (a quote) and advances past it.
	const auto string_token = [&] { // auto-ok: lambda
		const std::size_t start = i++;
		while (i < json.size() && json[i] != '"') {
			i += json[i] == '\\' ? 2 : 1;
		}
		i = std::min(i + 1, json.size());
		return json.substr(start, i - start);
	};
	// After a value ends inside an object, the next token is a key.
	const auto value_done = [&] { // auto-ok: lambda
		if (!frames.empty() && frames.back().object) {
			frames.back().expect_key = true;
			++frames.back().kept;
		}
	};

	while (i < json.size()) {
		skip_space();
		if (i >= json.size()) {
			break;
		}
		const char ch = json[i];
		if (!frames.empty() && frames.back().object && frames.back().expect_key && ch == '"') {
			const std::string_view key = string_token();
			skip_space();
			if (i < json.size() && json[i] == ':') {
				++i;
			}
			skip_space();
			if (json.substr(i, 4) == "null") {
				const std::size_t after = i + 4;
				std::size_t next = after;
				while (next < json.size() && (json[next] == ' ' || json[next] == '\t' ||
											  json[next] == '\n' || json[next] == '\r')) {
					++next;
				}
				if (next >= json.size() || json[next] == ',' || json[next] == '}') {
					i = after;
					continue;
				}
			}
			if (frames.back().kept > 0) {
				out.push_back(',');
			}
			out.append(key).push_back(':');
			frames.back().expect_key = false;
			continue;
		}
		switch (ch) {
			case '{':
				out.push_back('{');
				frames.push_back({.object = true, .expect_key = true, .kept = 0});
				++i;
				break;
			case '[':
				out.push_back('[');
				frames.push_back({.object = false, .expect_key = false, .kept = 0});
				++i;
				break;
			case '}':
			case ']':
				out.push_back(ch);
				if (!frames.empty()) {
					frames.pop_back();
				}
				++i;
				value_done();
				break;
			case ',':
				// Objects re-emit their own commas so dropped members leave none behind.
				if (frames.empty() || !frames.back().object) {
					out.push_back(',');
				}
				++i;
				break;
			case '"':
				out.append(string_token());
				value_done();
				break;
			default: {
				const std::size_t start = i;
				while (i < json.size() && json[i] != ',' && json[i] != '}' && json[i] != ']' &&
					   json[i] != ' ' && json[i] != '\t' && json[i] != '\n' && json[i] != '\r') {
					++i;
				}
				out.append(json.substr(start, i - start));
				value_done();
				break;
			}
		}
	}
	return out;
}

} // namespace kalshi::detail
