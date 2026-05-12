// Copyright (c) 2026 PredictionMarketsAI
// SPDX-License-Identifier: MIT
#pragma once

/// @file ws_cmd_bodies.hpp
/// @brief Internal Glaze shim structs for outgoing WS command frames
///
/// NOT a public API. Lives under `src/` (NOT under `include/`) so it
/// is never installed. Mirror of `src/api/json_bodies.hpp` for the WS
/// subscribe / unsubscribe / update_subscription commands.
///
/// Kalshi's WS server rejects frames whose top-level keys are not in
/// the documented order (`id`, then `cmd`, then `params`). The
/// `glz::meta` specializations below enumerate fields in the same
/// order the previous `nlohmann::ordered_json` impl used, and
/// `tests/test_json_serialize.cpp` pins byte-exact equivalence
/// against the pre-migration baselines.
///
/// IMPORTANT: only the OUTGOING command builders use this. The WS
/// `handle_message` hot path uses the hand-rolled scanners in
/// `kalshi/detail/ws_json.hpp` and is deliberately not migrated.

#include <cstdint>
#include <glaze/glaze.hpp>
#include <optional>
#include <string>
#include <vector>

namespace kalshi::ws_cmd {

struct SubscribeParams {
	std::vector<std::string> channels;
	std::optional<std::vector<std::string>> market_tickers;
};

struct SubscribeCmd {
	std::int32_t id{0};
	std::string cmd;
	SubscribeParams params;
};

struct UnsubscribeParams {
	std::vector<std::int32_t> sids;
};

struct UnsubscribeCmd {
	std::int32_t id{0};
	std::string cmd;
	UnsubscribeParams params;
};

struct UpdateParams {
	std::string action;
	std::string channel;
	std::vector<std::int32_t> sids;
	std::vector<std::string> market_tickers;
};

struct UpdateCmd {
	std::int32_t id{0};
	std::string cmd;
	UpdateParams params;
};

inline constexpr glz::opts kCmdOpts{.prettify = false};

template <class T>
[[nodiscard]] inline std::string render_cmd(const T& cmd) {
	std::string out;
	(void)glz::write<kCmdOpts>(cmd, out);
	return out;
}

} // namespace kalshi::ws_cmd

// ===== glz::meta specializations =====

template <>
struct glz::meta<kalshi::ws_cmd::SubscribeParams> {
	using T = kalshi::ws_cmd::SubscribeParams;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("channels", &T::channels, "market_tickers", &T::market_tickers);
};

template <>
struct glz::meta<kalshi::ws_cmd::SubscribeCmd> {
	using T = kalshi::ws_cmd::SubscribeCmd;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("id", &T::id, "cmd", &T::cmd, "params", &T::params);
};

template <>
struct glz::meta<kalshi::ws_cmd::UnsubscribeParams> {
	using T = kalshi::ws_cmd::UnsubscribeParams;
	static constexpr auto value = object("sids", &T::sids); // auto-ok: glz::object
};

template <>
struct glz::meta<kalshi::ws_cmd::UnsubscribeCmd> {
	using T = kalshi::ws_cmd::UnsubscribeCmd;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("id", &T::id, "cmd", &T::cmd, "params", &T::params);
};

template <>
struct glz::meta<kalshi::ws_cmd::UpdateParams> {
	using T = kalshi::ws_cmd::UpdateParams;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("action", &T::action, "channel", &T::channel, "sids", &T::sids, "market_tickers",
			   &T::market_tickers);
};

template <>
struct glz::meta<kalshi::ws_cmd::UpdateCmd> {
	using T = kalshi::ws_cmd::UpdateCmd;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("id", &T::id, "cmd", &T::cmd, "params", &T::params);
};
