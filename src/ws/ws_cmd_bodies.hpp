// Copyright (c) 2026 PredictionMarketsAI
// SPDX-License-Identifier: MIT
#pragma once

// Outgoing WebSocket command frames. Kalshi expects the keys `id`, `cmd`, then
// `params`; the `glz::meta` below fixes that order and
// `tests/test_ws_commands.cpp` pins it.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../json.hpp"

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
