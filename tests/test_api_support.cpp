// The plumbing every generated operation shares: encoding, error mapping, JSON
// adapters, and request validation.

#include "kalshi/api.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support.hpp"
#include "support/recording_transport.hpp"
#include "validate.hpp"

namespace {

kalshi::HttpResponse response(int status, std::string body) {
	return kalshi::HttpResponse{status, std::move(body), {}};
}

template <class T>
concept QueryAccepts = requires(kalshi::detail::Query& query, T value) { query.add("k", value); };

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

// A pointer converts to bool by a standard conversion, which beats the
// user-defined conversion to std::string_view, so a string literal once sent
// `key=true` (#130).
TEST(ApiSupport, QuerySendsStringLiteralsAndCharPointersAsText) {
	const char* pointer = "a b&c=d";
	kalshi::detail::Query query("/markets");
	query.add("literal", "a b&c=d");
	query.add("pointer", pointer);
	query.add("flag", true);
	query.add("off", false);

	EXPECT_EQ(std::move(query).str(),
			  "/markets?literal=a%20b%26c%3Dd&pointer=a%20b%26c%3Dd&flag=true&off=false");
	// Only a real bool reaches the bool overload; other pointers do not compile.
	static_assert(QueryAccepts<bool>);
	static_assert(!QueryAccepts<const int*>);
	static_assert(!QueryAccepts<std::nullptr_t>);
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

TEST(ApiSupport, DotSegmentPathParametersAreRejected) {
	// libcurl removes "." and ".." segments before sending, so these values
	// would reach a different route than the one that was asked for and signed.
	for (const std::string_view value : {".", ".."}) {
		const kalshi::Result<std::string> path =
			kalshi::detail::expand_path("/markets/{ticker}", {value});
		ASSERT_FALSE(path.has_value()) << value;
		EXPECT_EQ(path.error().code, kalshi::ErrorCode::InvalidRequest) << value;
		EXPECT_NE(path.error().message.find("ticker"), std::string::npos) << value;
	}
	EXPECT_FALSE(kalshi::detail::expand_path("/series/{series}/markets/{ticker}", {"KXA", ".."}));
	EXPECT_FALSE(kalshi::detail::expand_path("/series/{series}/markets/{ticker}", {".", "KXA"}));

	// Only a whole segment of dots is a dot segment, and an encoded dot in a
	// value is escaped again, so the server decodes it back to the same text.
	EXPECT_EQ(kalshi::detail::expand_path("/markets/{ticker}", {"..."}).value_or(""),
			  "/markets/...");
	EXPECT_EQ(kalshi::detail::expand_path("/markets/{ticker}", {".KXA"}).value_or(""),
			  "/markets/.KXA");
	EXPECT_EQ(kalshi::detail::expand_path("/markets/{ticker}", {"KXA.."}).value_or(""),
			  "/markets/KXA..");
	EXPECT_EQ(kalshi::detail::expand_path("/markets/{ticker}", {"%2E"}).value_or(""),
			  "/markets/%252E");
	EXPECT_EQ(kalshi::detail::expand_path("/markets/{ticker}", {"%2e%2E"}).value_or(""),
			  "/markets/%252e%252E");
	EXPECT_EQ(kalshi::detail::expand_path("/markets/{ticker}", {"../x"}).value_or(""),
			  "/markets/..%2Fx");
}

TEST(ApiSupport, DotSegmentPathParametersNeverReachTheTransport) {
	const std::shared_ptr<kalshi::test::RecordingTransport> transport =
		std::make_shared<kalshi::test::RecordingTransport>();
	// What GET /markets answers; it parsed as a Market with an empty ticker.
	transport->response_body = R"({"markets":[],"cursor":""})";
	kalshi::KalshiClient client{transport};

	const kalshi::Result<kalshi::Market> market = client.get_market(".");
	ASSERT_FALSE(market.has_value());
	EXPECT_EQ(market.error().code, kalshi::ErrorCode::InvalidRequest);
	const kalshi::Result<kalshi::Order> order = client.get_order("..");
	ASSERT_FALSE(order.has_value());
	EXPECT_EQ(order.error().code, kalshi::ErrorCode::InvalidRequest);
	EXPECT_EQ(transport->calls, 0);
}

TEST(ApiSupport, HttpStatusesMapToErrorCodes) {
	const struct {
		int status;
		kalshi::ErrorCode code;
	} cases[] = {{400, kalshi::ErrorCode::InvalidRequest},
				 {401, kalshi::ErrorCode::AuthenticationError},
				 {403, kalshi::ErrorCode::AuthenticationError},
				 {404, kalshi::ErrorCode::NotFound},
				 {405, kalshi::ErrorCode::InvalidRequest},
				 {408, kalshi::ErrorCode::NetworkError},
				 {409, kalshi::ErrorCode::InvalidRequest},
				 {413, kalshi::ErrorCode::InvalidRequest},
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
			response(200, R"({"balance":"secret-looking text"})"));
	ASSERT_FALSE(decoded.has_value());
	EXPECT_EQ(decoded.error().code, kalshi::ErrorCode::ParseError);
	// Bodies can hold secrets, such as a newly generated private key.
	EXPECT_EQ(decoded.error().message.find("secret"), std::string::npos) << decoded.error().message;
}

TEST(ApiSupport, MalformedJsonIsNotRepairedByNullFallback) {
	for (const std::string& body : {std::string{"{\"balance\":0} garbage"},
									std::string{"{\"balance\":null,,\"portfolio_value\":0}"},
									std::string{"{\"balance\":0}\0{}", 16}}) {
		const kalshi::Result<kalshi::GetBalanceResponse> decoded =
			kalshi::detail::decode<kalshi::GetBalanceResponse>(response(200, body));
		ASSERT_FALSE(decoded.has_value()) << body;
		EXPECT_EQ(decoded.error().code, kalshi::ErrorCode::ParseError);
	}
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

TEST(ApiSupport, WellFormedBodiesKeepNullsInsideRawJson) {
	const kalshi::Result<kalshi::Market> market = kalshi::detail::decode<kalshi::Market>(
		response(200, R"({"ticker":"A","custom_strike":{"a":null}})"));
	ASSERT_TRUE(market.has_value()) << market.error().message;
	ASSERT_TRUE(market->custom_strike.has_value());
	EXPECT_EQ(market->custom_strike->text, R"({"a":null})");
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

TEST(ApiSupport, NullTransportUsesTheDefaultHttpClient) {
	const kalshi::KalshiClient client{std::shared_ptr<const kalshi::HttpTransport>{}};
	EXPECT_NE(dynamic_cast<const kalshi::HttpClient*>(&client.transport()), nullptr);
}

TEST(ApiSupport, ExitSlippageValidatesUnitDependentBounds) {
	for (const kalshi::Unit unit : {kalshi::Unit::Bps, kalshi::Unit::Cents}) {
		EXPECT_TRUE(kalshi::detail::validate(kalshi::EventContractExitSlippage{unit, 0}));
		EXPECT_TRUE(kalshi::detail::validate(kalshi::EventContractExitSlippage{unit, 99}));
		EXPECT_FALSE(kalshi::detail::validate(kalshi::EventContractExitSlippage{unit, -1}));
		EXPECT_FALSE(kalshi::detail::validate(kalshi::EventContractExitSlippage{unit, 10000}));
	}
	EXPECT_TRUE(
		kalshi::detail::validate(kalshi::EventContractExitSlippage{kalshi::Unit::Bps, 9999}));
	EXPECT_FALSE(
		kalshi::detail::validate(kalshi::EventContractExitSlippage{kalshi::Unit::Cents, 100}));
	EXPECT_FALSE(
		kalshi::detail::validate(kalshi::EventContractExitSlippage{kalshi::Unit::Unknown, 1}));
}

TEST(ApiSupport, ExitTriggerRequiresAtLeastOneLeg) {
	kalshi::SetEventContractExitTriggerRequest request;
	EXPECT_FALSE(kalshi::detail::validate(request));
	request.slippage = kalshi::EventContractExitSlippage{kalshi::Unit::Bps, 1};
	EXPECT_FALSE(kalshi::detail::validate(request));
	request.stop_loss_price = "0.20";
	EXPECT_TRUE(kalshi::detail::validate(request));
	request.stop_loss_price.reset();
	request.take_profit_price = "0.80";
	EXPECT_TRUE(kalshi::detail::validate(request));
}

TEST(ApiSupport, ValidExitTriggerUpdateUsesPutAndEncodesTicker) {
	const std::shared_ptr<kalshi::test::RecordingTransport> transport =
		std::make_shared<kalshi::test::RecordingTransport>();
	kalshi::KalshiClient client(transport);
	const kalshi::Result<kalshi::EventContractExitTrigger> result =
		client.set_event_contract_exit_trigger("A/B", {.stop_loss_price = "0.20"},
											   {.subaccount = 1});
	ASSERT_TRUE(result);
	EXPECT_EQ(transport->calls, 1);
	EXPECT_EQ(transport->method, kalshi::HttpMethod::PUT);
	EXPECT_EQ(transport->target, "/portfolio/positions/A%2FB/exit_trigger?subaccount=1");
	EXPECT_EQ(transport->body, R"({"stop_loss_price":"0.20"})");
}

TEST(ApiSupport, ValidOrderWithExitTriggerUsesPost) {
	const std::shared_ptr<kalshi::test::RecordingTransport> transport =
		std::make_shared<kalshi::test::RecordingTransport>();
	kalshi::KalshiClient client(transport);
	kalshi::CreateOrderWithExitTriggerRequest request;
	request.ticker = "A";
	request.side = kalshi::BookSide::Bid;
	request.count = "1.00";
	request.price = "0.50";
	request.time_in_force = kalshi::TimeInForce::GoodTillCanceled;
	request.self_trade_prevention_type = kalshi::SelfTradePreventionType::TakerAtCross;
	request.exit_trigger.stop_loss_price = "0.20";
	const kalshi::Result<kalshi::CreateOrderV2Response> result =
		client.create_order_with_exit_trigger(request);
	ASSERT_TRUE(result);
	EXPECT_EQ(transport->calls, 1);
	EXPECT_EQ(transport->method, kalshi::HttpMethod::POST);
	EXPECT_EQ(transport->target, "/portfolio/events/orders/with_exit_trigger");
	EXPECT_NE(transport->body.find(R"("exit_trigger":{"stop_loss_price":"0.20"})"),
			  std::string::npos);
}

TEST(ApiSupport, AMovedFromClientFailsWithoutTouchingTheTransport) {
	const std::shared_ptr<kalshi::test::RecordingTransport> transport =
		std::make_shared<kalshi::test::RecordingTransport>();
	kalshi::KalshiClient client(transport);
	kalshi::KalshiClient moved(std::move(client));
	ASSERT_TRUE(moved.get_balance({}).has_value());
	EXPECT_EQ(transport->calls, 1);

	// NOLINTBEGIN(bugprone-use-after-move): moved-from must stay safe
	const kalshi::Result<kalshi::GetBalanceResponse> balance = client.get_balance({});
	ASSERT_FALSE(balance.has_value());
	EXPECT_EQ(balance.error().code, kalshi::ErrorCode::NetworkError);
	EXPECT_NE(balance.error().message.find("moved-from"), std::string::npos);
	const kalshi::Result<kalshi::HttpResponse> direct = client.transport().get("/markets");
	// NOLINTEND(bugprone-use-after-move)
	ASSERT_FALSE(direct.has_value());
	EXPECT_EQ(direct.error().code, kalshi::ErrorCode::NetworkError);
	EXPECT_EQ(transport->calls, 1);
}

TEST(ApiSupport, ValidationEnforcesDecreaseAndRfqBatchProseRules) {
	// DecreaseOrderV2Request: exactly one of reduce_by or reduce_to.
	EXPECT_FALSE(kalshi::detail::validate(kalshi::DecreaseOrderV2Request{}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::DecreaseOrderV2Request{.reduce_by = "1.00", .reduce_to = "2.00"}));
	EXPECT_TRUE(kalshi::detail::validate(kalshi::DecreaseOrderV2Request{.reduce_by = "1.00"}));
	EXPECT_TRUE(kalshi::detail::validate(kalshi::DecreaseOrderV2Request{.reduce_to = "2.00"}));

	// CreateRFQBatchChild: exactly one of market_ticker or selected_markets; at most one size.
	EXPECT_FALSE(kalshi::detail::validate(kalshi::CreateRFQBatchChild{}));
	kalshi::CreateRFQBatchChild both_markets;
	both_markets.market_ticker = "KXA";
	both_markets.selected_markets = {kalshi::TickerPair{.market_ticker = "M", .event_ticker = "E"}};
	EXPECT_FALSE(kalshi::detail::validate(both_markets));
	EXPECT_FALSE(kalshi::detail::validate(kalshi::CreateRFQBatchChild{
		.market_ticker = "KXA", .contracts_fp = "1.00", .target_cost_dollars = "5.00"}));
	kalshi::CreateRFQBatchChild too_many_legs;
	too_many_legs.selected_markets =
		std::vector<kalshi::TickerPair>(7, {.market_ticker = "M", .event_ticker = "E"});
	EXPECT_FALSE(kalshi::detail::validate(too_many_legs));
	EXPECT_TRUE(kalshi::detail::validate(kalshi::CreateRFQBatchChild{.market_ticker = "KXA"}));

	// CreateRFQBatchRequest: min 2 children, at most one batch size, sized children, rest_remainder
	// false, and mve_collection_ticker when selected_markets is used.
	kalshi::CreateRFQBatchRequest batch;
	EXPECT_FALSE(kalshi::detail::validate(batch)); // 0 children
	batch.children = {{.market_ticker = "KXA"}};
	EXPECT_FALSE(kalshi::detail::validate(batch)); // 1 child
	batch.children = {{.market_ticker = "KXA"}, {.market_ticker = "KXB"}};
	EXPECT_FALSE(kalshi::detail::validate(batch)); // unsized
	batch.contracts_fp = "2.00";
	EXPECT_TRUE(kalshi::detail::validate(batch));
	batch.target_cost_dollars = "10.00";
	EXPECT_FALSE(kalshi::detail::validate(batch)); // both batch sizes
	batch.target_cost_dollars.reset();
	batch.rest_remainder = true;
	EXPECT_FALSE(kalshi::detail::validate(batch)); // rest_remainder must be false
	batch.rest_remainder = false;
	EXPECT_TRUE(kalshi::detail::validate(batch));

	// Every child setting its own size works without a batch size, as long as
	// every child is sized the same way.
	kalshi::CreateRFQBatchRequest child_sized;
	child_sized.children = {{.market_ticker = "KXA", .contracts_fp = "1.00"},
							{.market_ticker = "KXB", .contracts_fp = "2.00"}};
	EXPECT_TRUE(kalshi::detail::validate(child_sized));
	child_sized.children[1] = {.market_ticker = "KXB", .target_cost_dollars = "5.00"};
	EXPECT_FALSE(kalshi::detail::validate(child_sized));

	// selected_markets requires mve_collection_ticker.
	kalshi::CreateRFQBatchRequest with_legs;
	with_legs.contracts_fp = "1.00";
	kalshi::CreateRFQBatchChild leg_child;
	leg_child.selected_markets = std::vector<kalshi::TickerPair>{
		{.market_ticker = "M1", .event_ticker = "E", .side = kalshi::Side::Yes}};
	with_legs.children = {leg_child, {.market_ticker = "KXB"}};
	EXPECT_FALSE(kalshi::detail::validate(with_legs));
	with_legs.mve_collection_ticker = "COLL-1";
	EXPECT_TRUE(kalshi::detail::validate(with_legs));
}

TEST(ApiSupport, ValidationEnforcesOrderPlacementAndBlockTradeConstraints) {
	kalshi::CreateOrderV2Request order;
	order.ticker = "KXTEST";
	order.side = kalshi::BookSide::Bid;
	order.count = "1.00";
	order.price = "0.5000";
	order.time_in_force = kalshi::TimeInForce::GoodTillCanceled;
	order.self_trade_prevention_type = kalshi::SelfTradePreventionType::TakerAtCross;
	order.client_order_id = std::string(58, 'a') + "_-:=+/";
	EXPECT_TRUE(kalshi::detail::validate(order));

	// client_order_id length and charset
	kalshi::CreateOrderV2Request bad_id = order;
	bad_id.client_order_id = std::string(65, 'a');
	EXPECT_FALSE(kalshi::detail::validate(bad_id));
	bad_id.client_order_id = "abc!def";
	EXPECT_FALSE(kalshi::detail::validate(bad_id));

	// IOC + expiration_time
	kalshi::CreateOrderV2Request ioc_exp = order;
	ioc_exp.time_in_force = kalshi::TimeInForce::ImmediateOrCancel;
	ioc_exp.expiration_time = 1800000000;
	EXPECT_FALSE(kalshi::detail::validate(ioc_exp));

	// reduce_only requires IOC
	kalshi::CreateOrderV2Request reduce_gtc = order;
	reduce_gtc.reduce_only = true;
	EXPECT_FALSE(kalshi::detail::validate(reduce_gtc));
	reduce_gtc.time_in_force = kalshi::TimeInForce::ImmediateOrCancel;
	EXPECT_TRUE(kalshi::detail::validate(reduce_gtc));

	// AmendOrderV2Request expiration_time >= 0
	kalshi::AmendOrderV2Request amend;
	amend.ticker = "KXTEST";
	amend.side = kalshi::BookSide::Bid;
	amend.price = "0.5000";
	amend.count = "1.00";
	amend.expiration_time = 0;
	EXPECT_TRUE(kalshi::detail::validate(amend));
	amend.expiration_time = -1;
	EXPECT_FALSE(kalshi::detail::validate(amend));

	// ProposeBlockTradeRequest positive price/count and mutual exclusion
	kalshi::ProposeBlockTradeRequest block;
	block.buyer_user_id = "u1";
	block.seller_user_id = "u2";
	block.market_ticker = "KXTEST";
	block.price_centi_cents = 5600;
	block.centicount = 1000;
	block.maker_side = kalshi::Side::Yes;
	block.expiration_ts = "2027-01-01T00:00:00Z";
	EXPECT_TRUE(kalshi::detail::validate(block));

	kalshi::ProposeBlockTradeRequest bad_block = block;
	bad_block.price_centi_cents = 0;
	EXPECT_FALSE(kalshi::detail::validate(bad_block));
	bad_block = block;
	bad_block.centicount = 0;
	EXPECT_FALSE(kalshi::detail::validate(bad_block));
	bad_block = block;
	bad_block.buyer_subtrader_id = "sub1";
	bad_block.buyer_subaccount = 1;
	EXPECT_FALSE(kalshi::detail::validate(bad_block));
}

TEST(ApiSupport, ValidationEnforcesQueryParameterRules) {
	// GetMarketsParams: timestamp groups, status compatibility, and limit bounds
	EXPECT_TRUE(kalshi::detail::validate(kalshi::GetMarketsParams{
		.min_created_ts = 1700000000, .status = kalshi::GetMarketsStatus::Open}));
	EXPECT_FALSE(kalshi::detail::validate(kalshi::GetMarketsParams{
		.min_created_ts = 1700000000, .status = kalshi::GetMarketsStatus::Closed}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::GetMarketsParams{.min_created_ts = 1700000000, .min_close_ts = 1700000000}));
	EXPECT_FALSE(kalshi::detail::validate(kalshi::GetMarketsParams{
		.min_updated_ts = 1700000000, .status = kalshi::GetMarketsStatus::Open}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::GetMarketsParams{.series_ticker = "KX", .min_updated_ts = 1700000000}));
	EXPECT_TRUE(kalshi::detail::validate(
		kalshi::GetMarketsParams{.series_ticker = "KX",
								 .min_updated_ts = 1700000000,
								 .mve_filter = kalshi::GetMarketsMveFilter::Exclude}));
	EXPECT_FALSE(kalshi::detail::validate(kalshi::GetMarketsParams{.limit = 1001}));

	// GetEventsParams: limit [1, 200]
	EXPECT_TRUE(kalshi::detail::validate(kalshi::GetEventsParams{.limit = 200}));
	EXPECT_FALSE(kalshi::detail::validate(kalshi::GetEventsParams{.limit = 0}));
	EXPECT_FALSE(kalshi::detail::validate(kalshi::GetEventsParams{.limit = 500}));

	// GetWeatherIndexParams: last_sec mutually exclusive with from/to
	EXPECT_TRUE(kalshi::detail::validate(kalshi::GetWeatherIndexParams{.last_sec = 3600}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::GetWeatherIndexParams{.from = 1700000000000, .last_sec = 3600}));

	// GetHistoricalMarketsParams: mutually exclusive filters
	EXPECT_TRUE(kalshi::detail::validate(kalshi::GetHistoricalMarketsParams{.tickers = "KXA"}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::GetHistoricalMarketsParams{.tickers = "KXA", .event_ticker = "EVT-1"}));
}

using Weather = kalshi::CreateWeatherPointMarketRequest;
using Flight = kalshi::CreateFlightDisruptionMarketRequest;

// Whether `request` passes validation after `change` edits a copy of it.
template <class Request, class Change>
bool valid_with(Request request, Change change) {
	change(request);
	return kalshi::detail::validate(request).has_value();
}

// Rules the spec gives only as go-validator tags in x-oapi-codegen-extra-tags,
// with no JSON Schema keyword beside them (#131).
TEST(ApiSupport, ValidationEnforcesSpecValidatorTags) {
	const Weather weather{.latitude = 90,
						  .longitude = -180,
						  .variable = kalshi::WeatherPointVariable::MaxTemp,
						  .strike_type = kalshi::WeatherPointStrikeType::Greater,
						  .floor_value = 80,
						  .date = "2028-02-29",
						  .end_date = "2028-03-01",
						  .start_hour = 0,
						  .end_hour = 24,
						  .timezone = "America/New_York"};
	EXPECT_TRUE(kalshi::detail::validate(weather));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.latitude = 91; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.latitude = -91; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.longitude = 181; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.longitude = -181; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.latitude = std::nan(""); }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.date = "not-a-date"; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.date = "12/01/2026"; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.date = "2026-02-29"; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.date = "2026-13-01"; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.date = "2026-04-31"; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.date = "2026-1-01"; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.end_date = "tomorrow"; }));
	EXPECT_FALSE(valid_with(weather, [](Weather& r) { r.end_date = ""; }));

	const Flight flight{.carrier = "B6",
						.flight_number = "1234",
						.departure_airport = "JFK",
						.arrival_airport = "LAX",
						.scheduled_date = "2026-12-01",
						.cover = kalshi::FlightCover::Either,
						.delay_minutes = 30};
	EXPECT_TRUE(kalshi::detail::validate(flight));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.scheduled_date = "tomorrow"; }));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.carrier = "A"; }));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.carrier = "!!"; }));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.flight_number = "12345"; }));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.flight_number = "ABCD"; }));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.departure_airport = "AB"; }));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.departure_airport = "ABCD"; }));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.departure_airport = "123"; }));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.arrival_airport = "L0N"; }));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.delay_minutes = 0; }));
	EXPECT_FALSE(valid_with(flight, [](Flight& r) { r.delay_minutes = -5; }));
	EXPECT_TRUE(valid_with(flight, [](Flight& r) { r.delay_minutes = std::nullopt; }));

	EXPECT_TRUE(kalshi::detail::validate(
		kalshi::CreateConcertCancellationMarketRequest{.jambase_event_id = "1",
													   .scheduled_date = "2026-12-01",
													   .cover = kalshi::ConcertCover::Either}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::CreateConcertCancellationMarketRequest{.jambase_event_id = "1",
													   .scheduled_date = "Dec 1",
													   .cover = kalshi::ConcertCover::Either}));

	const kalshi::CreateFCMPositionTransferRequest fcm{.source_subtrader_id = "a",
													   .target_subtrader_id = "b",
													   .market_ticker = "M",
													   .position_centicount = -100,
													   .price_centicents = 5000};
	EXPECT_TRUE(kalshi::detail::validate(fcm));
	kalshi::CreateFCMPositionTransferRequest no_position = fcm;
	no_position.position_centicount = 0;
	EXPECT_FALSE(kalshi::detail::validate(no_position));

	EXPECT_TRUE(kalshi::detail::validate(kalshi::ApplySubaccountTransferRequest{
		.client_transfer_id = "id", .amount_cents = 1, .exchange_index = 0}));
	EXPECT_FALSE(kalshi::detail::validate(kalshi::ApplySubaccountTransferRequest{
		.client_transfer_id = "id", .amount_cents = 1, .exchange_index = -1}));
	const kalshi::IntraExchangeInstanceTransferRequest intra{
		.source = kalshi::ExchangeInstance::EventContract,
		.destination = kalshi::ExchangeInstance::Margined,
		.amount = 1,
		.source_subaccount = 0,
		.destination_subaccount = 1};
	EXPECT_TRUE(kalshi::detail::validate(intra));
	kalshi::IntraExchangeInstanceTransferRequest bad_source = intra;
	bad_source.source_subaccount = -1;
	EXPECT_FALSE(kalshi::detail::validate(bad_source));
	kalshi::IntraExchangeInstanceTransferRequest bad_destination = intra;
	bad_destination.destination_subaccount = -1;
	EXPECT_FALSE(kalshi::detail::validate(bad_destination));

	// Query parameters carry the same tags.
	EXPECT_TRUE(kalshi::detail::validate(
		kalshi::GetFlightsParams{.carrier = "B6", .flight_number = "12", .date = "2026-12-01"}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::GetFlightsParams{.carrier = "!!", .flight_number = "12", .date = "2026-12-01"}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::GetFlightsParams{.carrier = "B6", .flight_number = "1A", .date = "2026-12-01"}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::GetFlightsParams{.carrier = "B6", .flight_number = "12", .date = "20261201"}));
	EXPECT_FALSE(
		kalshi::detail::validate(kalshi::GetConcertsParams{.metro_id = "1", .date_to = "soon"}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::GetConcertCancellationAvailabilityParams{.date_from = "x"}));
	EXPECT_TRUE(kalshi::detail::validate(
		kalshi::GetMarketCandlesticksParams{.start_ts = 1, .end_ts = 2, .period_interval = 60}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::GetMarketCandlesticksParams{.start_ts = 1, .end_ts = 2, .period_interval = 5}));
	EXPECT_TRUE(kalshi::detail::validate(
		kalshi::GetMarketOrderbooksParams{.tickers = {std::string(200, 'A')}}));
	EXPECT_FALSE(kalshi::detail::validate(
		kalshi::GetMarketOrderbooksParams{.tickers = {std::string(201, 'A')}}));
}

TEST(ApiSupport, CreateSubaccountValidatesItsExchangeIndex) {
	const std::shared_ptr<kalshi::test::RecordingTransport> transport =
		std::make_shared<kalshi::test::RecordingTransport>();
	kalshi::KalshiClient client{transport};
	const kalshi::Result<std::int64_t> rejected = client.create_subaccount({.exchange_index = -1});
	ASSERT_FALSE(rejected.has_value());
	EXPECT_EQ(rejected.error().code, kalshi::ErrorCode::InvalidRequest);
	EXPECT_EQ(transport->calls, 0);
}

// The route tests send default params, which now fail on period_interval 0, so
// these send valid ones through all three candlestick operations.
TEST(ApiSupport, CandlestickOperationsSendAValidPeriodInterval) {
	const std::shared_ptr<kalshi::test::RecordingTransport> transport =
		std::make_shared<kalshi::test::RecordingTransport>();
	kalshi::KalshiClient client{transport};

	EXPECT_TRUE(client.get_market_candlesticks(
		"KXS", "KXM", {.start_ts = 1, .end_ts = 2, .period_interval = 60}));
	EXPECT_EQ(transport->method, kalshi::HttpMethod::GET);
	EXPECT_EQ(transport->target,
			  "/series/KXS/markets/KXM/candlesticks?start_ts=1&end_ts=2&period_interval=60");

	EXPECT_TRUE(client.get_market_candlesticks_by_event(
		"KXS", "KXE", {.start_ts = 1, .end_ts = 2, .period_interval = 1440}));
	EXPECT_EQ(transport->method, kalshi::HttpMethod::GET);
	EXPECT_EQ(transport->target,
			  "/series/KXS/events/KXE/candlesticks?start_ts=1&end_ts=2&period_interval=1440");

	EXPECT_TRUE(client.get_market_candlesticks_historical(
		"KXM", {.start_ts = 1, .end_ts = 2, .period_interval = 1}));
	EXPECT_EQ(transport->method, kalshi::HttpMethod::GET);
	EXPECT_EQ(transport->target,
			  "/historical/markets/KXM/candlesticks?start_ts=1&end_ts=2&period_interval=1");
	EXPECT_EQ(transport->calls, 3);
}

// The spec gives period_interval as a JSON Schema enum and the percentiles as
// item bounds, with no go-validator tag beside them (#135).
TEST(ApiSupport, ForecastHistoryValidatesPeriodIntervalAndPercentiles) {
	using Forecast = kalshi::GetEventForecastPercentilesHistoryParams;
	const Forecast forecast{.percentiles = {0, 5000, 9999}, .start_ts = 1, .end_ts = 2};
	EXPECT_TRUE(kalshi::detail::validate(forecast)); // period_interval 0 is 5-second periods
	for (const std::int64_t period : {1, 60, 1440}) {
		EXPECT_TRUE(valid_with(forecast, [period](Forecast& p) { p.period_interval = period; }))
			<< period;
	}
	for (const std::int64_t period : {-1, 2, 5, 30, 1441}) {
		EXPECT_FALSE(valid_with(forecast, [period](Forecast& p) { p.period_interval = period; }))
			<< period;
	}
	EXPECT_FALSE(valid_with(forecast, [](Forecast& p) { p.percentiles = {-1}; }));
	EXPECT_FALSE(valid_with(forecast, [](Forecast& p) { p.percentiles = {10000}; }));
	EXPECT_FALSE(valid_with(forecast, [](Forecast& p) { p.percentiles = {5000, 10000}; }));
	EXPECT_TRUE(valid_with(forecast, [](Forecast& p) { p.percentiles = {}; }));

	const std::shared_ptr<kalshi::test::RecordingTransport> transport =
		std::make_shared<kalshi::test::RecordingTransport>();
	kalshi::KalshiClient client{transport};
	const kalshi::Result<std::vector<kalshi::ForecastPercentilesPoint>> rejected =
		client.get_event_forecast_percentiles_history(
			"KXS", "KXE", {.percentiles = {10000}, .start_ts = 1, .end_ts = 2});
	ASSERT_FALSE(rejected.has_value());
	EXPECT_EQ(rejected.error().code, kalshi::ErrorCode::InvalidRequest);
	EXPECT_EQ(transport->calls, 0);
}

// Prose rules the demo exchange enforces with 400 invalid_parameters (#136):
// "count_fp must match count", "target_cost_excludes_fees requires a target
// cost", and "every child must be sized by contracts_fp, or every child by
// target_cost_dollars".
TEST(ApiSupport, ValidationEnforcesMatchingCountsAndTargetCostRules) {
	using Rfq = kalshi::CreateRFQRequest;
	const Rfq rfq{.market_ticker = "KXA", .contracts = 10, .contracts_fp = "10.00"};
	EXPECT_TRUE(kalshi::detail::validate(rfq));
	EXPECT_TRUE(valid_with(rfq, [](Rfq& r) { r.contracts_fp = "10"; }));
	EXPECT_FALSE(valid_with(rfq, [](Rfq& r) { r.contracts_fp = "5.00"; }));
	EXPECT_FALSE(valid_with(rfq, [](Rfq& r) { r.contracts_fp = "10.50"; }));
	// Kalshi reads a zero `contracts` as unset, so only contracts_fp counts.
	EXPECT_TRUE(valid_with(rfq, [](Rfq& r) { r.contracts = 0; }));

	using Group = kalshi::CreateOrderGroupRequest;
	const Group group{.contracts_limit = 5, .contracts_limit_fp = "5.00"};
	EXPECT_TRUE(kalshi::detail::validate(group));
	EXPECT_FALSE(valid_with(group, [](Group& g) { g.contracts_limit_fp = "3.00"; }));
	EXPECT_TRUE(valid_with(group, [](Group& g) { g.contracts_limit.reset(); }));
	using Limit = kalshi::UpdateOrderGroupLimitRequest;
	const Limit limit{.contracts_limit = 5, .contracts_limit_fp = "5.00"};
	EXPECT_TRUE(kalshi::detail::validate(limit));
	EXPECT_FALSE(valid_with(limit, [](Limit& l) { l.contracts_limit_fp = "3.00"; }));

	const Rfq sized{
		.market_ticker = "KXA", .contracts_fp = "5.00", .target_cost_excludes_fees = true};
	EXPECT_FALSE(kalshi::detail::validate(sized));
	EXPECT_FALSE(valid_with(sized, [](Rfq& r) { r.target_cost_centi_cents = 0; }));
	EXPECT_TRUE(valid_with(sized, [](Rfq& r) { r.target_cost_excludes_fees = false; }));
	EXPECT_TRUE(valid_with(sized, [](Rfq& r) {
		r.contracts_fp.reset();
		r.target_cost_dollars = "5.00";
	}));
	EXPECT_TRUE(valid_with(sized, [](Rfq& r) {
		r.contracts_fp.reset();
		r.target_cost_centi_cents = 50000;
	}));

	using Batch = kalshi::CreateRFQBatchRequest;
	Batch batch;
	batch.children = {{.market_ticker = "KXA"}, {.market_ticker = "KXB"}};
	batch.contracts_fp = "1.00";
	EXPECT_TRUE(kalshi::detail::validate(batch));
	EXPECT_FALSE(valid_with(batch, [](Batch& b) { b.target_cost_excludes_fees = true; }));
	EXPECT_TRUE(valid_with(batch, [](Batch& b) {
		b.contracts_fp.reset();
		b.target_cost_dollars = "5.00";
		b.target_cost_excludes_fees = true;
	}));
	// One child sized by target cost while the other takes the batch's contracts.
	EXPECT_FALSE(valid_with(batch, [](Batch& b) { b.children[0].target_cost_dollars = "5.00"; }));
	EXPECT_TRUE(valid_with(batch, [](Batch& b) { b.children[0].contracts_fp = "2.00"; }));
	EXPECT_TRUE(valid_with(batch, [](Batch& b) {
		b.contracts_fp.reset();
		b.target_cost_dollars = "5.00";
		b.children[0].target_cost_dollars = "2.00";
		b.target_cost_excludes_fees = true;
	}));
}
