// The plumbing every generated operation shares: encoding, error mapping, JSON
// adapters, and request validation.

#include "kalshi/api.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
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

	// Every child setting its own size works without a batch size.
	kalshi::CreateRFQBatchRequest child_sized;
	child_sized.children = {{.market_ticker = "KXA", .contracts_fp = "1.00"},
							{.market_ticker = "KXB", .target_cost_dollars = "5.00"}};
	EXPECT_TRUE(kalshi::detail::validate(child_sized));

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
