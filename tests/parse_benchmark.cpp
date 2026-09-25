// Coarse throughput guard for request serialization and WebSocket parsing.
// It fails only on order-of-magnitude regressions, so normal CI timing noise
// passes; use benchmarks/ for real measurements.

#include "kalshi/detail/ws_message.hpp"

#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "support.hpp"

namespace {

kalshi::BatchCreateOrdersV2Request make_payload() {
	kalshi::BatchCreateOrdersV2Request payload;
	payload.orders.reserve(50);
	for (int i = 0; i < 50; ++i) {
		kalshi::CreateOrderV2Request o;
		o.ticker = "KXHIGHDEN-26MAY11-T" + std::to_string(50 + i);
		o.side = i % 2 == 0 ? kalshi::BookSide::Bid : kalshi::BookSide::Ask;
		o.count = std::to_string(1 + (i % 10)) + ".00";
		o.price = "0." + std::to_string(30 + (i % 40)) + "00";
		o.time_in_force = kalshi::TimeInForce::GoodTillCanceled;
		o.self_trade_prevention_type = kalshi::SelfTradePreventionType::TakerAtCross;
		if (i % 3 == 0) {
			o.client_order_id = "client-" + std::to_string(i);
		}
		if (i % 5 == 0) {
			o.expiration_time = 1788000000 + i;
		}
		payload.orders.push_back(std::move(o));
	}
	return payload;
}

} // namespace

int main(int argc, char** argv) {
	// Instrumented builds still check results, but their timings mean nothing.
	const bool check_timing = !(argc > 1 && std::string_view{argv[1]} == "--no-timing");
	const kalshi::BatchCreateOrdersV2Request payload = make_payload();
	const int kIterations = check_timing ? 1000 : 10;

	// Warmup — let the allocator and CPU settle.
	for (int i = 0; i < 50; ++i) {
		volatile std::string warm = kalshi::detail::encode(payload);
		(void)warm;
	}

	std::chrono::nanoseconds glaze_total{0};
	std::size_t glaze_checksum = 0;
	for (int i = 0; i < kIterations; ++i) {
		std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
		std::string out = kalshi::detail::encode(payload);
		std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
		glaze_total += (t1 - t0);
		glaze_checksum += out.size();
	}

	if (glaze_checksum == 0) {
		std::fprintf(stderr, "checksum is zero — render_body emitted nothing\n");
		return 1;
	}

	const double glaze_ms = glaze_total.count() / 1e6;
	const double us_per_op = (glaze_total.count() / 1e3) / kIterations;
	const std::string sample = kalshi::detail::encode(payload);

	std::printf("parse_benchmark: payload=%zuB iters=%d\n", sample.size(), kIterations);
	std::printf("  glaze (serialize): %8.3f ms total  (%8.3f us/op)\n", glaze_ms, us_per_op);

	// Regression guard: at migration time, Glaze rendered a 50-order
	// batch in ~30-60 us/op on x86_64-v3 / -O3 / LTO. Cap at 500 us/op
	// — that's ~10x the measured baseline and accounts for slower CI
	// runners, Debug builds, and AddressSanitizer overhead.
	constexpr double kMaxUsPerOp = 500.0;
	if (check_timing && us_per_op > kMaxUsPerOp) {
		std::fprintf(stderr, "REGRESSION: %.3f us/op exceeds cap of %.0f us/op\n", us_per_op,
					 kMaxUsPerOp);
		return 1;
	}

	constexpr std::string_view kTradeFrame =
		R"({"type":"trade","sid":7,"msg":{"trade_id":"trade-1","market_ticker":"KXTEST-YES","yes_price_dollars":"0.4200","no_price_dollars":"0.5800","count_fp":"12.00","is_block_trade":false,"taker_side":"yes","taker_outcome_side":"yes","taker_book_side":"bid","ts":1788000000,"ts_ms":1788000000123}})";
	const int kParseIterations = check_timing ? 10000 : 100;
	std::chrono::nanoseconds parse_total{0};
	std::int64_t parse_checksum = 0;
	for (int i = 0; i < kParseIterations; ++i) {
		const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
		const std::optional<kalshi::WsMessage> message =
			kalshi::detail::parse_ws_data_message(kTradeFrame);
		const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
		parse_total += t1 - t0;
		if (!message) {
			std::fprintf(stderr, "WebSocket parser rejected the benchmark frame\n");
			return 1;
		}
		const kalshi::WsTrade* trade = std::get_if<kalshi::WsTrade>(&*message);
		if (!trade) {
			std::fprintf(stderr, "WebSocket parser returned the wrong message type\n");
			return 1;
		}
		parse_checksum += trade->yes_price + trade->count;
	}
	if (parse_checksum == 0) {
		std::fprintf(stderr, "WebSocket parser checksum is zero\n");
		return 1;
	}
	const double parse_us_per_op = (parse_total.count() / 1e3) / kParseIterations;
	std::printf("  WebSocket (parse): %8.3f ms total  (%8.3f us/op)\n", parse_total.count() / 1e6,
				parse_us_per_op);
	constexpr double kMaxParseUsPerOp = 500.0;
	if (check_timing && parse_us_per_op > kMaxParseUsPerOp) {
		std::fprintf(stderr, "REGRESSION: %.3f us/op exceeds WebSocket cap of %.0f us/op\n",
					 parse_us_per_op, kMaxParseUsPerOp);
		return 1;
	}
	return 0;
}
