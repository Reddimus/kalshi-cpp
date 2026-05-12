// Copyright (c) 2026 PredictionMarketsAI
// SPDX-License-Identifier: MIT
//
// Microbenchmark + ctest regression guard for the outgoing-JSON
// serializers migrated to Glaze in this branch.
//
// kalshi-cpp's hot paths are the WS *receive* loop and the REST
// *response* parser. Neither uses a JSON library — both are
// hand-rolled string scanners — so a parse benchmark of the kind
// open-meteo-cpp shipped (deserializer-focused) wouldn't measure
// anything Glaze touches in this repo. The migration scope here is
// outgoing-body serialization (WS subscribe frames + the dozen REST
// `serialize_*` request bodies), so this bench measures the
// equivalent: rendering a representative payload 1000 times and
// asserting us/op stays under a regression cap.
//
// The representative payload is a 50-order batch-create body — that's
// the largest payload the SDK regularly emits (kalshi-trader sends
// up to 64 orders per round of its scanner).
//
// Recorded baseline (x86_64-v3, GCC 13.3, -O3 -DNDEBUG, payload=5918B,
// iters=1000):
//
//     nlohmann::ordered_json v3.11.3 : ~162 us/op  (pre-migration)
//     glaze v7.6.0                   :   ~3 us/op  (post-migration)
//     speedup                        :  ~55-60x
//
// The pre-migration baseline was a hand-built nlohmann benchmark
// using the same payload shape (kept in commit history, not shipped
// here); the speedup is dominated by avoiding the JSON-AST
// allocation overhead since the previous batch path round-tripped
// each inner order through `ordered_json::parse(serialize_one(...))`.

#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "../src/api/json_bodies.hpp"

namespace {

kalshi::ser::BatchOrdersBody make_payload() {
	kalshi::ser::BatchOrdersBody payload;
	payload.orders.reserve(50);
	for (int i = 0; i < 50; ++i) {
		kalshi::ser::CreateOrderBody o;
		o.ticker = "KXHIGHDEN-26MAY11-T" + std::to_string(50 + i);
		o.side = (i % 2 == 0) ? "yes" : "no";
		o.action = "buy";
		o.type = "limit";
		o.count = 1 + (i % 10);
		o.yes_price = 30 + (i % 40);
		// Sprinkle some optionals so the skip_null_members path is
		// exercised on most-but-not-all fields.
		if (i % 3 == 0) {
			o.client_order_id = "client-" + std::to_string(i);
		}
		if (i % 5 == 0) {
			o.expiration_ts = 1788000000 + i;
		}
		payload.orders.push_back(std::move(o));
	}
	return payload;
}

} // namespace

int main() {
	const kalshi::ser::BatchOrdersBody payload = make_payload();
	constexpr int kIterations = 1000;

	// Warmup — let the allocator and CPU settle.
	for (int i = 0; i < 50; ++i) {
		volatile std::string warm = kalshi::ser::render_body(payload);
		(void)warm;
	}

	std::chrono::nanoseconds glaze_total{0};
	std::size_t glaze_checksum = 0;
	for (int i = 0; i < kIterations; ++i) {
		std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
		std::string out = kalshi::ser::render_body(payload);
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
	const std::string sample = kalshi::ser::render_body(payload);

	std::printf("parse_benchmark: payload=%zuB iters=%d\n", sample.size(), kIterations);
	std::printf("  glaze (serialize): %8.3f ms total  (%8.3f us/op)\n", glaze_ms, us_per_op);

	// Regression guard: at migration time, Glaze rendered a 50-order
	// batch in ~30-60 us/op on x86_64-v3 / -O3 / LTO. Cap at 500 us/op
	// — that's ~10x the measured baseline and accounts for slower CI
	// runners, Debug builds, and AddressSanitizer overhead.
	constexpr double kMaxUsPerOp = 500.0;
	if (us_per_op > kMaxUsPerOp) {
		std::fprintf(stderr, "REGRESSION: %.3f us/op exceeds cap of %.0f us/op\n", us_per_op,
					 kMaxUsPerOp);
		return 1;
	}
	return 0;
}
