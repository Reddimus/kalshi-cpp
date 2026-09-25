// The plumbing every generated operation shares: encoding, error mapping, JSON
// adapters, and request validation.

#include "kalshi/api.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "support.hpp"
#include "validate.hpp"

namespace {

kalshi::HttpResponse response(int status, std::string body) {
	return kalshi::HttpResponse{status, std::move(body), {}};
}

} // namespace

TEST(ApiSupport, QueryEncodesValuesAndSkipsUnsetOnes) {
	kalshi::detail::Query query("/markets");
	query.add("limit", std::optional<std::int64_t>{100});
	query.add("cursor", std::optional<std::string>{"a b+c/=&"});
	query.add("event_ticker", std::optional<std::string>{});
	query.add("status", std::optional<kalshi::GetMarketsStatus>{kalshi::GetMarketsStatus::Open});
	query.add("with_nested_markets", std::optional<bool>{true});
	query.add("tickers", std::vector<std::string>{"A", "B C"});
	query.add("unknown_enum", kalshi::GetMarketsStatus::Unknown);

	EXPECT_EQ(std::move(query).str(), "/markets?limit=100&cursor=a%20b%2Bc%2F%3D%26&status=open"
									  "&with_nested_markets=true&tickers=A&tickers=B%20C");
}

TEST(ApiSupport, PathParametersArePercentEncodedAndMustBeNonEmpty) {
	const kalshi::Result<std::string> path = kalshi::detail::expand_path(
		"/series/{series_ticker}/markets/{ticker}", {"KX HIGH", "A/B?c"});
	ASSERT_TRUE(path.has_value());
	EXPECT_EQ(*path, "/series/KX%20HIGH/markets/A%2FB%3Fc");

	const kalshi::Result<std::string> empty =
		kalshi::detail::expand_path("/markets/{ticker}", {""});
	ASSERT_FALSE(empty.has_value());
	EXPECT_EQ(empty.error().code, kalshi::ErrorCode::InvalidRequest);
	EXPECT_NE(empty.error().message.find("ticker"), std::string::npos);
}

TEST(ApiSupport, HttpStatusesMapToErrorCodes) {
	const struct {
		int status;
		kalshi::ErrorCode code;
	} cases[] = {{400, kalshi::ErrorCode::InvalidRequest},
				 {401, kalshi::ErrorCode::AuthenticationError},
				 {403, kalshi::ErrorCode::AuthenticationError},
				 {404, kalshi::ErrorCode::NotFound},
				 {409, kalshi::ErrorCode::InvalidRequest},
				 {422, kalshi::ErrorCode::InvalidRequest},
				 {429, kalshi::ErrorCode::RateLimited},
				 {500, kalshi::ErrorCode::ServerError},
				 {503, kalshi::ErrorCode::ServerError},
				 {302, kalshi::ErrorCode::Unknown}};
	for (const auto& test : cases) { // auto-ok: anonymous struct
		const kalshi::Error error = kalshi::detail::http_error(response(test.status, ""), "op");
		EXPECT_EQ(error.code, test.code) << test.status;
		EXPECT_EQ(error.http_status, test.status);
	}
}

TEST(ApiSupport, ErrorBodiesKeepKalshisCodeAndMessage) {
	const kalshi::Error nested = kalshi::detail::http_error(
		response(
			404,
			R"({"error":{"code":"market_not_found","message":"Market not found","details":"KXNOPE"}})"),
		"get_market");
	EXPECT_EQ(nested.api_code, "market_not_found");
	EXPECT_EQ(nested.message,
			  "get_market failed with HTTP 404 [market_not_found]: Market not found (KXNOPE)");

	const kalshi::Error plain = kalshi::detail::http_error(
		response(429, R"({"error":"too many requests"})"), "get_markets");
	EXPECT_EQ(plain.code, kalshi::ErrorCode::RateLimited);
	EXPECT_EQ(plain.message, "get_markets failed with HTTP 429: too many requests");

	const kalshi::Error bare = kalshi::detail::http_error(
		response(400, R"({"code":"invalid_parameters","message":"price is invalid"})"),
		"create_order");
	EXPECT_EQ(bare.api_code, "invalid_parameters");

	const kalshi::Error text =
		kalshi::detail::http_error(response(502, "Bad Gateway"), "get_event");
	EXPECT_EQ(text.message, "get_event failed with HTTP 502: Bad Gateway");
}

TEST(ApiSupport, UnexpectedBodiesAreParseErrors) {
	const kalshi::Result<kalshi::GetBalanceResponse> decoded =
		kalshi::detail::decode<kalshi::GetBalanceResponse>(
			response(200, R"({"balance":"not a number"})"));
	ASSERT_FALSE(decoded.has_value());
	EXPECT_EQ(decoded.error().code, kalshi::ErrorCode::ParseError);
}

TEST(ApiSupport, UnknownEnumValuesAndFieldsDoNotFailParsing) {
	const kalshi::Result<kalshi::CancelOrderV2Response> decoded =
		kalshi::detail::decode<kalshi::CancelOrderV2Response>(response(
			200, R"({"order_id":"o1","reduced_by":"1.00","ts_ms":5,"new_field":{"x":[1]}})"));
	ASSERT_TRUE(decoded.has_value());
	EXPECT_EQ(decoded->order_id, "o1");

	kalshi::Market market;
	constexpr glz::opts options{.error_on_unknown_keys = false};
	ASSERT_FALSE(glz::read<options>(market, std::string(R"({"status":"brand_new_status"})")));
	EXPECT_EQ(market.status, kalshi::MarketStatus::Unknown);
}

TEST(ApiSupport, NullMembersAreDroppedWhereverTheyAppear) {
	using kalshi::detail::strip_null_members;
	EXPECT_EQ(strip_null_members(R"({"a":null,"b":1,"c":null})"), R"({"b":1})");
	EXPECT_EQ(strip_null_members(R"({ "a" : null , "b" : [null, 1, {"x": null}] , "c": "null" })"),
			  R"({"b":[null,1,{}],"c":"null"})");
	EXPECT_EQ(strip_null_members(R"({"s":"a \" : null, b","n":null})"),
			  R"({"s":"a \" : null, b"})");
	EXPECT_EQ(strip_null_members(R"({"map":{"AI":null,"X":["a"]},"t":true,"f":false,"z":0})"),
			  R"({"map":{"X":["a"]},"t":true,"f":false,"z":0})");
	EXPECT_EQ(strip_null_members("[null,{\"a\":null}]"), "[null,{}]");
}

TEST(ApiSupport, NullsForNonNullableFieldsReadAsEmpty) {
	// Kalshi sends null for fields its spec marks as required arrays.
	const kalshi::Result<kalshi::GetEventMetadataResponse> metadata =
		kalshi::detail::decode<kalshi::GetEventMetadataResponse>(response(
			200, R"({"image_url":"x.webp","market_details":null,"settlement_sources":[]})"));
	ASSERT_TRUE(metadata.has_value()) << metadata.error().message;
	EXPECT_TRUE(metadata->market_details.empty());
	EXPECT_EQ(metadata->image_url, "x.webp");
}

TEST(ApiSupport, RawJsonRoundTripsVerbatim) {
	kalshi::Market market;
	constexpr glz::opts options{.error_on_unknown_keys = false};
	const std::string json = R"({"custom_strike":{"city":"NYC","levels":[1,2.5,null]}})";
	ASSERT_FALSE(glz::read<options>(market, json));
	ASSERT_TRUE(market.custom_strike.has_value());
	EXPECT_EQ(market.custom_strike->text, R"({"city":"NYC","levels":[1,2.5,null]})");
}

TEST(ApiSupport, EncodedBodiesOmitUnsetOptionalsAndKeepSpecOrder) {
	kalshi::CreateOrderV2Request order;
	order.ticker = "KXTEST";
	order.side = kalshi::BookSide::Ask;
	order.count = "1.00";
	order.price = "0.4200";
	order.time_in_force = kalshi::TimeInForce::FillOrKill;
	order.self_trade_prevention_type = kalshi::SelfTradePreventionType::Maker;
	order.post_only = false;

	EXPECT_EQ(
		kalshi::detail::encode(order),
		R"({"ticker":"KXTEST","side":"ask","count":"1.00","price":"0.4200",)"
		R"("time_in_force":"fill_or_kill","post_only":false,"self_trade_prevention_type":"maker"})");
}

TEST(ApiSupport, ValidationRejectsMissingEnumsAndMalformedDecimals) {
	kalshi::CreateOrderV2Request order;
	order.ticker = "KXTEST";
	order.side = kalshi::BookSide::Bid;
	order.count = "1.00";
	order.price = "0.42";
	order.time_in_force = kalshi::TimeInForce::GoodTillCanceled;
	order.self_trade_prevention_type = kalshi::SelfTradePreventionType::TakerAtCross;
	EXPECT_TRUE(kalshi::detail::validate(order).has_value());

	kalshi::CreateOrderV2Request bad_price = order;
	bad_price.price = "42c";
	EXPECT_FALSE(kalshi::detail::validate(bad_price).has_value());

	kalshi::CreateOrderV2Request no_side = order;
	no_side.side = kalshi::BookSide::Unknown;
	const kalshi::Result<void> missing = kalshi::detail::validate(no_side);
	ASSERT_FALSE(missing.has_value());
	EXPECT_EQ(missing.error().message, "CreateOrderV2Request.side is required");

	kalshi::BatchCreateOrdersV2Request batch;
	batch.orders = {order, bad_price};
	EXPECT_FALSE(kalshi::detail::validate(batch).has_value());
}
