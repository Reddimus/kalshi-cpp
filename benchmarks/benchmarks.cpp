// Microbenchmarks for the SDK's CPU-bound paths. REST parsing runs through the
// public client with an in-memory transport, so results stay comparable when
// the internals change.
//
//   cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DKALSHI_BUILD_BENCHMARKS=ON
//   cmake --build build-bench --target kalshi_benchmarks
//   ./build-bench/benchmarks/kalshi_benchmarks
//
// `allocs` and `alloc_bytes` are operator new calls and bytes per iteration on
// the benchmark thread. OpenSSL and libcurl allocate with malloc, which the
// counter does not see. Sanitizer and coverage builds report neither. On macOS,
// `instructions` per iteration varies by less than 0.1% between runs, even on
// a busy machine, so it is the steadiest way to compare two builds.

#include "kalshi/kalshi.hpp"

#include <benchmark/benchmark.h>
#include <cstdint>
#include <memory>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fixtures.hpp"
#include "frames.hpp"
#include "support.hpp"

#ifdef KALSHI_COUNT_ALLOCATIONS
#include "support/allocation_counter.hpp"
#endif

#if defined(__APPLE__)
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {

/// Instructions this process has retired so far, or 0 where the OS doesn't
/// report them.
std::uint64_t instructions_retired() noexcept {
#if defined(__APPLE__)
	rusage_info_v4 info{};
	if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&info)) == 0) {
		return info.ri_instructions;
	}
#endif
	return 0;
}

/// Adds per-iteration counters for the work done between construction and
/// report(): `allocs` and `alloc_bytes`, and `instructions` where available.
class LoopReport {
public:
	void report(benchmark::State& state) const {
		const std::uint64_t instructions = instructions_retired();
		if (instructions_ != 0 && instructions > instructions_) {
			state.counters["instructions"] =
				benchmark::Counter(static_cast<double>(instructions - instructions_),
								   benchmark::Counter::kAvgIterations);
		}
#ifdef KALSHI_COUNT_ALLOCATIONS
		const kalshi::test::AllocationCount used = kalshi::test::allocations() - start_;
		state.counters["allocs"] =
			benchmark::Counter(static_cast<double>(used.count), benchmark::Counter::kAvgIterations);
		state.counters["alloc_bytes"] =
			benchmark::Counter(static_cast<double>(used.bytes), benchmark::Counter::kAvgIterations,
							   benchmark::Counter::kIs1024);
#endif
	}

private:
#ifdef KALSHI_COUNT_ALLOCATIONS
	kalshi::test::AllocationCount start_ = kalshi::test::allocations();
#endif
	std::uint64_t instructions_ = instructions_retired();
};

class StaticTransport final : public kalshi::HttpTransport {
public:
	explicit StaticTransport(std::string body) : body_(std::move(body)) {}

	[[nodiscard]] kalshi::Result<kalshi::HttpResponse> request(kalshi::HttpMethod, std::string_view,
															   std::string_view) const override {
		// Returns a copy on purpose, since a real transport hands back a body it owns.
		return kalshi::HttpResponse{200, body_, {}};
	}

private:
	std::string body_;
};

/// Serves linked pages, picking the one whose cursor the request names.
class PagedTransport final : public kalshi::HttpTransport {
public:
	explicit PagedTransport(std::vector<fixtures::Page> pages) : pages_(std::move(pages)) {}

	[[nodiscard]] kalshi::Result<kalshi::HttpResponse>
	request(kalshi::HttpMethod, std::string_view path, std::string_view) const override {
		constexpr std::string_view key = "cursor=";
		const std::size_t at = path.find(key);
		std::string_view cursor;
		if (at != std::string_view::npos) {
			cursor = path.substr(at + key.size());
			cursor = cursor.substr(0, cursor.find('&'));
		}
		for (const fixtures::Page& page : pages_) {
			if (page.cursor == cursor) {
				return kalshi::HttpResponse{200, page.body, {}};
			}
		}
		return kalshi::HttpResponse{404, R"({"code":"not_found","message":"no such page"})", {}};
	}

private:
	std::vector<fixtures::Page> pages_;
};

void BM_RestParseMarketsPage(benchmark::State& state) {
	const int count = static_cast<int>(state.range(0));
	kalshi::KalshiClient client(std::make_shared<StaticTransport>(fixtures::markets_page(count)));
	const kalshi::Result<kalshi::GetMarketsResponse> check = client.get_markets();
	if (!check || check->markets.size() != static_cast<std::size_t>(count)) {
		state.SkipWithError("markets page did not parse");
		return;
	}
	const LoopReport loop;
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		kalshi::Result<kalshi::GetMarketsResponse> page = client.get_markets();
		benchmark::DoNotOptimize(page);
	}
	loop.report(state);
	state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_RestParseMarketsPage)->Arg(100)->Arg(1000);

// A null where the spec forbids one fails the first parse, so the client strips
// nulls and parses the page again.
void BM_RestParseMarketsPageNulls(benchmark::State& state, fixtures::Nulls nulls) {
	constexpr int count = 100;
	kalshi::KalshiClient client(
		std::make_shared<StaticTransport>(fixtures::markets_page(count, nulls)));
	const kalshi::Result<kalshi::GetMarketsResponse> check = client.get_markets();
	if (!check || check->markets.size() != static_cast<std::size_t>(count)) {
		state.SkipWithError("markets page did not parse");
		return;
	}
	const LoopReport loop;
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		kalshi::Result<kalshi::GetMarketsResponse> page = client.get_markets();
		benchmark::DoNotOptimize(page);
	}
	loop.report(state);
	state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK_CAPTURE(BM_RestParseMarketsPageNulls, last, fixtures::Nulls::Last);
BENCHMARK_CAPTURE(BM_RestParseMarketsPageNulls, all, fixtures::Nulls::All);

void BM_RestParseOrdersPage(benchmark::State& state) {
	const int count = static_cast<int>(state.range(0));
	kalshi::KalshiClient client(std::make_shared<StaticTransport>(fixtures::orders_page(count)));
	const kalshi::Result<kalshi::GetOrdersResponse> check = client.get_orders();
	if (!check || check->orders.size() != static_cast<std::size_t>(count)) {
		state.SkipWithError("orders page did not parse");
		return;
	}
	const LoopReport loop;
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		kalshi::Result<kalshi::GetOrdersResponse> page = client.get_orders();
		benchmark::DoNotOptimize(page);
	}
	loop.report(state);
	state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_RestParseOrdersPage)->Arg(200)->Arg(1000);

void BM_CollectPages(benchmark::State& state, int pages, int per_page) {
	kalshi::KalshiClient client(
		std::make_shared<PagedTransport>(fixtures::market_pages(pages, per_page)));
	kalshi::GetMarketsParams params{.limit = per_page};
	const auto fetch = [&](std::string_view cursor) {
		params.cursor = cursor.empty() ? std::nullopt : std::optional<std::string>(cursor);
		return client.get_markets(params);
	};
	const std::size_t total = static_cast<std::size_t>(pages) * static_cast<std::size_t>(per_page);
	const kalshi::Result<std::vector<kalshi::Market>> check =
		kalshi::collect_pages(fetch, &kalshi::GetMarketsResponse::markets);
	if (!check || check->size() != total) {
		state.SkipWithError("pages did not collect");
		return;
	}
	const LoopReport loop;
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		kalshi::Result<std::vector<kalshi::Market>> markets =
			kalshi::collect_pages(fetch, &kalshi::GetMarketsResponse::markets);
		benchmark::DoNotOptimize(markets);
	}
	loop.report(state);
	state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(total));
}
BENCHMARK_CAPTURE(BM_CollectPages, 1x1000, 1, 1000);
BENCHMARK_CAPTURE(BM_CollectPages, 5x200, 5, 200);

void BM_WsParse(benchmark::State& state, std::string_view text) {
	const std::string frame{text}; // received frames arrive in a std::string
	if (!kalshi::detail::parse_message(frame)) {
		state.SkipWithError("frame did not parse");
		return;
	}
	const LoopReport loop;
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		std::optional<kalshi::WsMessage> message = kalshi::detail::parse_message(frame);
		benchmark::DoNotOptimize(message);
	}
	loop.report(state);
	state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(frame.size()));
}
BENCHMARK_CAPTURE(BM_WsParse, trade, fixtures::kTradeFrame);
BENCHMARK_CAPTURE(BM_WsParse, orderbook_delta, fixtures::kDeltaFrame);
BENCHMARK_CAPTURE(BM_WsParse, fill, fixtures::kFillFrame);
BENCHMARK_CAPTURE(BM_WsParse, ticker, fixtures::kTickerFrame);

void BM_SerializeBatchCreate(benchmark::State& state) {
	const kalshi::BatchCreateOrdersV2Request payload = fixtures::batch_create_request(50);
	const LoopReport loop;
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		std::string body = kalshi::detail::encode(payload);
		benchmark::DoNotOptimize(body);
	}
	loop.report(state);
}
BENCHMARK(BM_SerializeBatchCreate);

// The rate limiter prices a batch by counting its orders before sending it.
void BM_RateLimitCost(benchmark::State& state) {
	const int count = static_cast<int>(state.range(0));
	const std::string body = kalshi::detail::encode(fixtures::batch_create_request(count));
	const kalshi::RateLimitedTransport transport(std::make_shared<StaticTransport>("{}"),
												 fixtures::rate_limits());
	constexpr std::string_view path = "/portfolio/events/orders/batched";
	// Batch creates cost the default 10 tokens per order.
	if (transport.cost(kalshi::HttpMethod::POST, path, body) != 10.0 * count) {
		state.SkipWithError("batch was not priced per order");
		return;
	}
	const LoopReport loop;
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		double cost = transport.cost(kalshi::HttpMethod::POST, path, body);
		benchmark::DoNotOptimize(cost);
	}
	loop.report(state);
}
BENCHMARK(BM_RateLimitCost)->Arg(20);

std::string generate_pem(int key_type) {
	EVP_PKEY* key = key_type == EVP_PKEY_RSA ? EVP_RSA_gen(2048)
											 : EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
	if (key == nullptr) {
		throw std::runtime_error("key generation failed");
	}
	BIO* bio = BIO_new(BIO_s_mem());
	PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
	char* data = nullptr;
	const long size = BIO_get_mem_data(bio, &data);
	std::string pem(data, static_cast<std::size_t>(size));
	BIO_free(bio);
	EVP_PKEY_free(key);
	return pem;
}

void BM_Sign(benchmark::State& state, int key_type) {
	kalshi::Result<kalshi::Signer> signer =
		kalshi::Signer::from_pem("key-id", generate_pem(key_type));
	if (!signer) {
		state.SkipWithError(signer.error().message.c_str());
		return;
	}
	const LoopReport loop;
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		kalshi::Result<kalshi::AuthHeaders> headers =
			signer->sign_with_timestamp("GET", "/trade-api/v2/portfolio/orders", 1788000000123);
		benchmark::DoNotOptimize(headers);
	}
	loop.report(state);
}
BENCHMARK_CAPTURE(BM_Sign, rsa_pss, EVP_PKEY_RSA);
BENCHMARK_CAPTURE(BM_Sign, ed25519, EVP_PKEY_ED25519);

} // namespace
