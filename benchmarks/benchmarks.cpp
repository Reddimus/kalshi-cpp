// Microbenchmarks for the SDK's CPU-bound paths. REST parsing runs through the
// public client with an in-memory transport, so results stay comparable when
// the internals change.
//
//   cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DKALSHI_BUILD_BENCHMARKS=ON
//   cmake --build build-bench --target kalshi_benchmarks
//   ./build-bench/benchmarks/kalshi_benchmarks

#include "kalshi/detail/ws_message.hpp"
#include "kalshi/kalshi.hpp"

#include <benchmark/benchmark.h>
#include <memory>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "json_bodies.hpp"

namespace {

class StaticTransport final : public kalshi::HttpTransport {
public:
	explicit StaticTransport(std::string body) : body_(std::move(body)) {}

	[[nodiscard]] kalshi::Result<kalshi::HttpResponse> request(kalshi::HttpMethod, std::string_view,
															   std::string_view) const override {
		return kalshi::HttpResponse{200, body_, {}};
	}

private:
	std::string body_;
};

std::string market_json(int index) {
	const std::string n = std::to_string(index);
	return R"({"ticker":"KXHIGHNY-26SEP25-T)" + n +
		   R"(","event_ticker":"KXHIGHNY-26SEP25","market_type":"binary","title":"Will the high be above )" +
		   n + R"(°F?","yes_sub_title":")" + n +
		   R"(° or above","no_sub_title":"Below","status":"active","yes_bid_dollars":"0.4200",)"
		   R"("yes_ask_dollars":"0.4400","no_bid_dollars":"0.5600","no_ask_dollars":"0.5800",)"
		   R"("last_price_dollars":"0.4300","volume_fp":"1234.00","volume_24h_fp":"56.00",)"
		   R"("open_interest_fp":"789.00","notional_value_dollars":"1.0000",)"
		   R"("open_time":"2026-09-24T14:00:00Z","close_time":"2026-09-26T04:59:00Z",)"
		   R"("expected_expiration_time":"2026-09-26T15:00:00Z","exchange_index":0,)"
		   R"("price_level_structure":"linear_cent","rules_primary":"If the \"high\" temperature is above )" +
		   n + R"( degrees, the market resolves Yes.","rules_secondary":""})";
}

std::string markets_page(int count) {
	std::string body = R"({"cursor":"next-page-token","markets":[)";
	for (int i = 0; i < count; ++i) {
		body += (i == 0 ? "" : ",") + market_json(i);
	}
	return body + "]}";
}

std::string orders_page(int count) {
	std::string body = R"({"cursor":"","orders":[)";
	for (int i = 0; i < count; ++i) {
		const std::string n = std::to_string(i);
		body += (i == 0 ? "" : ",");
		body += R"({"order_id":"order-)" + n + R"(","user_id":"user-1","ticker":"KXTEST-)" + n +
				R"(","client_order_id":"client-)" + n +
				R"(","status":"resting","side":"yes","action":"buy","outcome_side":"yes",)"
				R"("book_side":"bid","type":"limit","yes_price_dollars":"0.4200",)"
				R"("no_price_dollars":"0.5800","initial_count_fp":"10.00","fill_count_fp":"2.00",)"
				R"("remaining_count_fp":"8.00","taker_fees_dollars":"0.0100",)"
				R"("created_time":"2026-09-24T14:00:00.123Z","exchange_index":1})";
	}
	return body + "]}";
}

void BM_RestParseMarketsPage(benchmark::State& state) {
	const int count = static_cast<int>(state.range(0));
	kalshi::KalshiClient client(std::make_shared<StaticTransport>(markets_page(count)));
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		kalshi::Result<kalshi::PaginatedResponse<kalshi::Market>> page = client.get_markets();
		benchmark::DoNotOptimize(page);
	}
	state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_RestParseMarketsPage)->Arg(100)->Arg(1000);

void BM_RestParseOrdersPage(benchmark::State& state) {
	const int count = static_cast<int>(state.range(0));
	kalshi::KalshiClient client(std::make_shared<StaticTransport>(orders_page(count)));
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		kalshi::Result<kalshi::PaginatedResponse<kalshi::Order>> page = client.get_orders();
		benchmark::DoNotOptimize(page);
	}
	state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_RestParseOrdersPage)->Arg(1000);

constexpr std::string_view kTradeFrame =
	R"({"type":"trade","sid":7,"seq":9,"msg":{"trade_id":"trade-1","market_ticker":"KXTEST-YES","yes_price_dollars":"0.4200","no_price_dollars":"0.5800","count_fp":"12.00","is_block_trade":false,"taker_side":"yes","taker_outcome_side":"yes","taker_book_side":"bid","ts":1788000000,"ts_ms":1788000000123}})";
constexpr std::string_view kDeltaFrame =
	R"({"type":"orderbook_delta","sid":2,"seq":501,"msg":{"market_ticker":"KXTEST-YES","market_id":"9b0f6b43","price_dollars":"0.4200","delta_fp":"-30.00","side":"yes","ts":"2026-09-24T14:00:00.123456Z","ts_ms":1788000000123}})";
constexpr std::string_view kFillFrame =
	R"({"type":"fill","sid":13,"msg":{"trade_id":"trade-1","order_id":"order-1","market_ticker":"KXTEST-YES","is_taker":true,"side":"yes","action":"buy","outcome_side":"yes","book_side":"bid","yes_price_dollars":"0.4200","count_fp":"3.00","fee_cost":"0.0200","post_position_fp":"10.00","purchased_side":"yes","exchange_index":1,"client_order_id":"client-1","ts":1788000000,"ts_ms":1788000000123}})";

void BM_WsParse(benchmark::State& state, std::string_view frame) {
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		std::optional<kalshi::WsMessage> message = kalshi::detail::parse_ws_data_message(frame);
		benchmark::DoNotOptimize(message);
	}
	state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(frame.size()));
}
BENCHMARK_CAPTURE(BM_WsParse, trade, kTradeFrame);
BENCHMARK_CAPTURE(BM_WsParse, orderbook_delta, kDeltaFrame);
BENCHMARK_CAPTURE(BM_WsParse, fill, kFillFrame);

void BM_SerializeBatchCreate(benchmark::State& state) {
	kalshi::ser::BatchOrdersBody payload;
	for (int i = 0; i < 50; ++i) {
		kalshi::ser::CreateOrderBody order;
		order.ticker = "KXHIGHDEN-26MAY11-T" + std::to_string(50 + i);
		order.side = i % 2 == 0 ? "bid" : "ask";
		order.count = std::to_string(1 + i % 10) + ".00";
		order.price = "0." + std::to_string(30 + i % 40) + "00";
		order.time_in_force = "good_till_canceled";
		order.self_trade_prevention_type = "taker_at_cross";
		if (i % 3 == 0) {
			order.client_order_id = "client-" + std::to_string(i);
		}
		payload.orders.push_back(std::move(order));
	}
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		std::string body = kalshi::ser::render_body(payload);
		benchmark::DoNotOptimize(body);
	}
}
BENCHMARK(BM_SerializeBatchCreate);

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

void BM_SignRsaPss(benchmark::State& state) {
	kalshi::Result<kalshi::Signer> signer =
		kalshi::Signer::from_pem("key-id", generate_pem(EVP_PKEY_RSA));
	if (!signer) {
		state.SkipWithError(signer.error().message.c_str());
		return;
	}
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		kalshi::Result<kalshi::AuthHeaders> headers =
			signer->sign_with_timestamp("GET", "/trade-api/v2/portfolio/orders", 1788000000123);
		benchmark::DoNotOptimize(headers);
	}
}
BENCHMARK(BM_SignRsaPss);

} // namespace
