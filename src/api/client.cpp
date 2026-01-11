#include "kalshi/api.hpp"

#include <charconv>
#include <sstream>
#include <cstring>

namespace kalshi {

struct KalshiClient::Impl {
    HttpClient client;

    explicit Impl(HttpClient c) : client(std::move(c)) {}
};

KalshiClient::KalshiClient(HttpClient client)
    : impl_(std::make_unique<Impl>(std::move(client))) {}

KalshiClient::~KalshiClient() = default;

KalshiClient::KalshiClient(KalshiClient&&) noexcept = default;

KalshiClient& KalshiClient::operator=(KalshiClient&&) noexcept = default;

HttpClient& KalshiClient::http_client() {
    return impl_->client;
}

const HttpClient& KalshiClient::http_client() const {
    return impl_->client;
}

// Simple JSON parsing helpers (minimal implementation without external dependencies)
namespace {

std::string extract_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";

    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";

    size_t start = pos + 1;
    size_t end = json.find('"', start);
    if (end == std::string::npos) return "";

    return json.substr(start, end - start);
}

std::int64_t extract_int(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;

    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;

    // Skip whitespace
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    // Parse number
    std::int64_t result = 0;
    bool negative = false;
    if (pos < json.size() && json[pos] == '-') {
        negative = true;
        pos++;
    }
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        result = result * 10 + (json[pos] - '0');
        pos++;
    }
    return negative ? -result : result;
}

bool extract_bool(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;

    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;

    // Skip whitespace
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    return (pos < json.size() && json[pos] == 't');
}

std::string extract_cursor(const std::string& json) {
    return extract_string(json, "cursor");
}

// Find the start of a JSON object by key
size_t find_object_start(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return std::string::npos;

    pos = json.find('{', pos);
    return pos;
}

// Find matching closing brace
size_t find_object_end(const std::string& json, size_t start) {
    if (start >= json.size() || json[start] != '{') return std::string::npos;

    int depth = 1;
    size_t pos = start + 1;
    while (pos < json.size() && depth > 0) {
        if (json[pos] == '{') depth++;
        else if (json[pos] == '}') depth--;
        pos++;
    }
    return depth == 0 ? pos : std::string::npos;
}

// Find the start of a JSON array by key
size_t find_array_start(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return std::string::npos;

    pos = json.find('[', pos);
    return pos;
}

// Find matching closing bracket
size_t find_array_end(const std::string& json, size_t start) {
    if (start >= json.size() || json[start] != '[') return std::string::npos;

    int depth = 1;
    size_t pos = start + 1;
    bool in_string = false;
    while (pos < json.size() && depth > 0) {
        char c = json[pos];
        if (c == '"' && (pos == 0 || json[pos-1] != '\\')) {
            in_string = !in_string;
        } else if (!in_string) {
            if (c == '[') depth++;
            else if (c == ']') depth--;
        }
        pos++;
    }
    return depth == 0 ? pos : std::string::npos;
}

// Extract array elements as separate JSON strings
std::vector<std::string> extract_array_objects(const std::string& json, const std::string& key) {
    std::vector<std::string> result;

    size_t array_start = find_array_start(json, key);
    if (array_start == std::string::npos) return result;

    size_t array_end = find_array_end(json, array_start);
    if (array_end == std::string::npos) return result;

    std::string array_content = json.substr(array_start + 1, array_end - array_start - 2);

    // Parse individual objects
    size_t pos = 0;
    while (pos < array_content.size()) {
        // Find next object start
        size_t obj_start = array_content.find('{', pos);
        if (obj_start == std::string::npos) break;

        size_t obj_end = find_object_end(array_content, obj_start);
        if (obj_end == std::string::npos) break;

        result.push_back(array_content.substr(obj_start, obj_end - obj_start));
        pos = obj_end;
    }

    return result;
}

std::string escape_json_string(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

void append_query_param(std::string& query, const std::string& key, const std::string& value) {
    if (value.empty()) return;
    query += (query.find('?') == std::string::npos ? '?' : '&');
    query += key + "=" + value;
}

void append_query_param(std::string& query, const std::string& key, std::int32_t value) {
    query += (query.find('?') == std::string::npos ? '?' : '&');
    query += key + "=" + std::to_string(value);
}

void append_query_param(std::string& query, const std::string& key, std::int64_t value) {
    query += (query.find('?') == std::string::npos ? '?' : '&');
    query += key + "=" + std::to_string(value);
}

} // anonymous namespace

// ===== Exchange API =====

Result<ExchangeStatus> KalshiClient::get_exchange_status() {
    auto response = impl_->client.get("/exchange/status");
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get exchange status: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    ExchangeStatus status;
    status.trading_active = extract_bool(response->body, "trading_active");
    status.exchange_active = extract_bool(response->body, "exchange_active");
    return status;
}

// ===== Markets API =====

Result<Market> KalshiClient::get_market(const std::string& ticker) {
    auto response = impl_->client.get("/markets/" + ticker);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get market: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    return parse_market(response->body);
}

Result<Market> KalshiClient::parse_market(const std::string& json) {
    // Find market object (may be nested under "market" key or at root)
    size_t market_start = find_object_start(json, "market");
    std::string market_json = market_start != std::string::npos
        ? json.substr(market_start, find_object_end(json, market_start) - market_start)
        : json;

    Market market;
    market.ticker = extract_string(market_json, "ticker");
    market.title = extract_string(market_json, "title");
    market.subtitle = extract_string(market_json, "subtitle");

    std::string status_str = extract_string(market_json, "status");
    market.status = parse_market_status(status_str);

    market.open_time = extract_int(market_json, "open_time");
    market.close_time = extract_int(market_json, "close_time");

    std::int64_t exp_time = extract_int(market_json, "expiration_time");
    if (exp_time > 0) {
        market.expiration_time = exp_time;
    }

    market.yes_bid = static_cast<std::int32_t>(extract_int(market_json, "yes_bid"));
    market.yes_ask = static_cast<std::int32_t>(extract_int(market_json, "yes_ask"));
    market.no_bid = static_cast<std::int32_t>(extract_int(market_json, "no_bid"));
    market.no_ask = static_cast<std::int32_t>(extract_int(market_json, "no_ask"));
    market.volume = static_cast<std::int32_t>(extract_int(market_json, "volume"));
    market.open_interest = static_cast<std::int32_t>(extract_int(market_json, "open_interest"));

    std::string result_str = extract_string(market_json, "result");
    if (!result_str.empty()) {
        market.result = result_str;
    }

    return market;
}

Result<std::vector<Market>> KalshiClient::parse_markets(const std::string& json) {
    std::vector<Market> markets;
    auto market_objects = extract_array_objects(json, "markets");

    for (const auto& obj : market_objects) {
        auto market = parse_market(obj);
        if (market) {
            markets.push_back(std::move(*market));
        }
    }

    return markets;
}

std::string KalshiClient::build_markets_query(const GetMarketsParams& params) {
    std::string query = "/markets";

    if (params.limit) append_query_param(query, "limit", *params.limit);
    if (params.cursor) append_query_param(query, "cursor", *params.cursor);
    if (params.event_ticker) append_query_param(query, "event_ticker", *params.event_ticker);
    if (params.series_ticker) append_query_param(query, "series_ticker", *params.series_ticker);
    if (params.status) append_query_param(query, "status", *params.status);
    if (params.tickers) append_query_param(query, "tickers", *params.tickers);

    return query;
}

Result<PaginatedResponse<Market>> KalshiClient::get_markets(const GetMarketsParams& params) {
    std::string path = build_markets_query(params);
    auto response = impl_->client.get(path);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get markets: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    auto markets = parse_markets(response->body);
    if (!markets) {
        return std::unexpected(markets.error());
    }

    PaginatedResponse<Market> result;
    result.items = std::move(*markets);

    std::string cursor = extract_cursor(response->body);
    if (!cursor.empty()) {
        result.next_cursor = Cursor{cursor};
    }

    return result;
}

Result<OrderBook> KalshiClient::get_market_orderbook(const std::string& ticker,
                                                      std::optional<std::int32_t> depth) {
    std::string path = "/markets/" + ticker + "/orderbook";
    if (depth) {
        append_query_param(path, "depth", *depth);
    }

    auto response = impl_->client.get(path);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get orderbook: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    return parse_orderbook(response->body);
}

Result<OrderBook> KalshiClient::parse_orderbook(const std::string& json) {
    OrderBook book;

    // Find orderbook object
    size_t ob_start = find_object_start(json, "orderbook");
    std::string ob_json = ob_start != std::string::npos
        ? json.substr(ob_start, find_object_end(json, ob_start) - ob_start)
        : json;

    book.market_ticker = extract_string(ob_json, "market_ticker");

    // Parse yes array [[price, qty], ...]
    size_t yes_start = find_array_start(ob_json, "yes");
    if (yes_start != std::string::npos) {
        size_t yes_end = find_array_end(ob_json, yes_start);
        std::string yes_content = ob_json.substr(yes_start, yes_end - yes_start);

        // Simple parser for [[p,q], [p,q], ...]
        size_t pos = 0;
        while (pos < yes_content.size()) {
            size_t inner_start = yes_content.find('[', pos);
            if (inner_start == std::string::npos || inner_start == 0) break;

            size_t comma = yes_content.find(',', inner_start);
            size_t inner_end = yes_content.find(']', comma);
            if (comma == std::string::npos || inner_end == std::string::npos) break;

            std::string price_str = yes_content.substr(inner_start + 1, comma - inner_start - 1);
            std::string qty_str = yes_content.substr(comma + 1, inner_end - comma - 1);

            OrderBookEntry entry;
            entry.price_cents = std::stoi(price_str);
            entry.quantity = std::stoi(qty_str);
            book.yes_bids.push_back(entry);

            pos = inner_end + 1;
        }
    }

    // Parse no array
    size_t no_start = find_array_start(ob_json, "no");
    if (no_start != std::string::npos) {
        size_t no_end = find_array_end(ob_json, no_start);
        std::string no_content = ob_json.substr(no_start, no_end - no_start);

        size_t pos = 0;
        while (pos < no_content.size()) {
            size_t inner_start = no_content.find('[', pos);
            if (inner_start == std::string::npos || inner_start == 0) break;

            size_t comma = no_content.find(',', inner_start);
            size_t inner_end = no_content.find(']', comma);
            if (comma == std::string::npos || inner_end == std::string::npos) break;

            std::string price_str = no_content.substr(inner_start + 1, comma - inner_start - 1);
            std::string qty_str = no_content.substr(comma + 1, inner_end - comma - 1);

            OrderBookEntry entry;
            entry.price_cents = std::stoi(price_str);
            entry.quantity = std::stoi(qty_str);
            book.no_bids.push_back(entry);

            pos = inner_end + 1;
        }
    }

    return book;
}

Result<std::vector<Candlestick>> KalshiClient::get_market_candlesticks(const GetCandlesticksParams& params) {
    std::string path = "/markets/" + params.ticker + "/candlesticks";
    append_query_param(path, "period", params.period);
    if (params.start_ts) append_query_param(path, "start_ts", *params.start_ts);
    if (params.end_ts) append_query_param(path, "end_ts", *params.end_ts);

    auto response = impl_->client.get(path);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get candlesticks: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    std::vector<Candlestick> candlesticks;
    auto candle_objects = extract_array_objects(response->body, "candlesticks");

    for (const auto& obj : candle_objects) {
        Candlestick c;
        c.timestamp = extract_int(obj, "ts");
        c.open_price = static_cast<std::int32_t>(extract_int(obj, "open"));
        c.close_price = static_cast<std::int32_t>(extract_int(obj, "close"));
        c.high_price = static_cast<std::int32_t>(extract_int(obj, "high"));
        c.low_price = static_cast<std::int32_t>(extract_int(obj, "low"));
        c.volume = static_cast<std::int32_t>(extract_int(obj, "volume"));
        candlesticks.push_back(c);
    }

    return candlesticks;
}

std::string KalshiClient::build_trades_query(const GetTradesParams& params) {
    std::string query = "/trades";

    if (params.limit) append_query_param(query, "limit", *params.limit);
    if (params.cursor) append_query_param(query, "cursor", *params.cursor);
    if (params.market_ticker) append_query_param(query, "ticker", *params.market_ticker);
    if (params.min_ts) append_query_param(query, "min_ts", *params.min_ts);
    if (params.max_ts) append_query_param(query, "max_ts", *params.max_ts);

    return query;
}

Result<PaginatedResponse<PublicTrade>> KalshiClient::get_trades(const GetTradesParams& params) {
    std::string path = build_trades_query(params);
    auto response = impl_->client.get(path);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get trades: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    std::vector<PublicTrade> trades;
    auto trade_objects = extract_array_objects(response->body, "trades");

    for (const auto& obj : trade_objects) {
        PublicTrade t;
        t.trade_id = extract_string(obj, "trade_id");
        t.market_ticker = extract_string(obj, "ticker");
        t.yes_price = static_cast<std::int32_t>(extract_int(obj, "yes_price"));
        t.no_price = static_cast<std::int32_t>(extract_int(obj, "no_price"));
        t.count = static_cast<std::int32_t>(extract_int(obj, "count"));
        t.taker_side = parse_side(extract_string(obj, "taker_side"));
        t.created_time = extract_int(obj, "created_time");
        trades.push_back(t);
    }

    PaginatedResponse<PublicTrade> result;
    result.items = std::move(trades);

    std::string cursor = extract_cursor(response->body);
    if (!cursor.empty()) {
        result.next_cursor = Cursor{cursor};
    }

    return result;
}

// ===== Events API =====

std::string KalshiClient::build_events_query(const GetEventsParams& params) {
    std::string query = "/events";

    if (params.limit) append_query_param(query, "limit", *params.limit);
    if (params.cursor) append_query_param(query, "cursor", *params.cursor);
    if (params.series_ticker) append_query_param(query, "series_ticker", *params.series_ticker);
    if (params.status) append_query_param(query, "status", *params.status);

    return query;
}

Result<Event> KalshiClient::get_event(const std::string& event_ticker) {
    auto response = impl_->client.get("/events/" + event_ticker);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get event: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    // Find event object
    size_t evt_start = find_object_start(response->body, "event");
    std::string evt_json = evt_start != std::string::npos
        ? response->body.substr(evt_start, find_object_end(response->body, evt_start) - evt_start)
        : response->body;

    Event event;
    event.event_ticker = extract_string(evt_json, "event_ticker");
    event.series_ticker = extract_string(evt_json, "series_ticker");
    event.title = extract_string(evt_json, "title");
    event.category = extract_string(evt_json, "category");
    event.sub_title = extract_string(evt_json, "sub_title");

    return event;
}

Result<PaginatedResponse<Event>> KalshiClient::get_events(const GetEventsParams& params) {
    std::string path = build_events_query(params);
    auto response = impl_->client.get(path);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get events: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    std::vector<Event> events;
    auto event_objects = extract_array_objects(response->body, "events");

    for (const auto& obj : event_objects) {
        Event e;
        e.event_ticker = extract_string(obj, "event_ticker");
        e.series_ticker = extract_string(obj, "series_ticker");
        e.title = extract_string(obj, "title");
        e.category = extract_string(obj, "category");
        events.push_back(e);
    }

    PaginatedResponse<Event> result;
    result.items = std::move(events);

    std::string cursor = extract_cursor(response->body);
    if (!cursor.empty()) {
        result.next_cursor = Cursor{cursor};
    }

    return result;
}

// ===== Series API =====

Result<Series> KalshiClient::get_series(const std::string& series_ticker) {
    auto response = impl_->client.get("/series/" + series_ticker);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get series: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    size_t series_start = find_object_start(response->body, "series");
    std::string series_json = series_start != std::string::npos
        ? response->body.substr(series_start, find_object_end(response->body, series_start) - series_start)
        : response->body;

    Series series;
    series.ticker = extract_string(series_json, "ticker");
    series.title = extract_string(series_json, "title");
    series.category = extract_string(series_json, "category");
    series.frequency = extract_string(series_json, "frequency");

    return series;
}

// ===== Portfolio API =====

Result<Balance> KalshiClient::get_balance() {
    auto response = impl_->client.get("/portfolio/balance");
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get balance: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    Balance balance;
    balance.balance = extract_int(response->body, "balance");
    balance.available_balance = extract_int(response->body, "available_balance");

    return balance;
}

std::string KalshiClient::build_positions_query(const GetPositionsParams& params) {
    std::string query = "/portfolio/positions";

    if (params.limit) append_query_param(query, "limit", *params.limit);
    if (params.cursor) append_query_param(query, "cursor", *params.cursor);
    if (params.event_ticker) append_query_param(query, "event_ticker", *params.event_ticker);
    if (params.market_ticker) append_query_param(query, "market_ticker", *params.market_ticker);
    if (params.settlement_status) append_query_param(query, "settlement_status", *params.settlement_status);

    return query;
}

Result<PaginatedResponse<Position>> KalshiClient::get_positions(const GetPositionsParams& params) {
    std::string path = build_positions_query(params);
    auto response = impl_->client.get(path);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get positions: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    std::vector<Position> positions;
    auto pos_objects = extract_array_objects(response->body, "positions");

    for (const auto& obj : pos_objects) {
        Position p;
        p.market_ticker = extract_string(obj, "market_ticker");
        p.yes_contracts = static_cast<std::int32_t>(extract_int(obj, "position"));
        // Total cost might be stored differently
        p.total_cost_cents = static_cast<std::int32_t>(extract_int(obj, "total_traded"));
        positions.push_back(p);
    }

    PaginatedResponse<Position> result;
    result.items = std::move(positions);

    std::string cursor = extract_cursor(response->body);
    if (!cursor.empty()) {
        result.next_cursor = Cursor{cursor};
    }

    return result;
}

std::string KalshiClient::build_orders_query(const GetOrdersParams& params) {
    std::string query = "/portfolio/orders";

    if (params.limit) append_query_param(query, "limit", *params.limit);
    if (params.cursor) append_query_param(query, "cursor", *params.cursor);
    if (params.market_ticker) append_query_param(query, "ticker", *params.market_ticker);
    if (params.status) append_query_param(query, "status", *params.status);

    return query;
}

Result<Order> KalshiClient::parse_order(const std::string& json) {
    // Find order object
    size_t order_start = find_object_start(json, "order");
    std::string order_json = order_start != std::string::npos
        ? json.substr(order_start, find_object_end(json, order_start) - order_start)
        : json;

    Order order;
    order.order_id = extract_string(order_json, "order_id");
    order.market_ticker = extract_string(order_json, "ticker");
    order.side = parse_side(extract_string(order_json, "side"));
    order.action = parse_action(extract_string(order_json, "action"));

    std::string type_str = extract_string(order_json, "type");
    order.type = (type_str == "market") ? OrderType::Market : OrderType::Limit;

    order.status = parse_order_status(extract_string(order_json, "status"));

    order.initial_count = static_cast<std::int32_t>(extract_int(order_json, "original_count"));
    if (order.initial_count == 0) {
        order.initial_count = static_cast<std::int32_t>(extract_int(order_json, "count"));
    }
    order.remaining_count = static_cast<std::int32_t>(extract_int(order_json, "remaining_count"));
    order.filled_count = order.initial_count - order.remaining_count;

    // Price might be yes_price or no_price depending on side
    order.price = static_cast<std::int32_t>(extract_int(order_json, "yes_price"));
    if (order.price == 0) {
        order.price = static_cast<std::int32_t>(extract_int(order_json, "no_price"));
    }

    order.created_time = extract_int(order_json, "created_time");

    std::int64_t exp = extract_int(order_json, "expiration_ts");
    if (exp > 0) {
        order.expiration_ts = exp;
    }

    return order;
}

Result<std::vector<Order>> KalshiClient::parse_orders(const std::string& json) {
    std::vector<Order> orders;
    auto order_objects = extract_array_objects(json, "orders");

    for (const auto& obj : order_objects) {
        auto order = parse_order(obj);
        if (order) {
            orders.push_back(std::move(*order));
        }
    }

    return orders;
}

Result<PaginatedResponse<Order>> KalshiClient::get_orders(const GetOrdersParams& params) {
    std::string path = build_orders_query(params);
    auto response = impl_->client.get(path);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get orders: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    auto orders = parse_orders(response->body);
    if (!orders) {
        return std::unexpected(orders.error());
    }

    PaginatedResponse<Order> result;
    result.items = std::move(*orders);

    std::string cursor = extract_cursor(response->body);
    if (!cursor.empty()) {
        result.next_cursor = Cursor{cursor};
    }

    return result;
}

Result<Order> KalshiClient::get_order(const std::string& order_id) {
    auto response = impl_->client.get("/portfolio/orders/" + order_id);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get order: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    return parse_order(response->body);
}

std::string KalshiClient::build_fills_query(const GetFillsParams& params) {
    std::string query = "/portfolio/fills";

    if (params.limit) append_query_param(query, "limit", *params.limit);
    if (params.cursor) append_query_param(query, "cursor", *params.cursor);
    if (params.market_ticker) append_query_param(query, "ticker", *params.market_ticker);
    if (params.order_id) append_query_param(query, "order_id", *params.order_id);
    if (params.min_ts) append_query_param(query, "min_ts", *params.min_ts);
    if (params.max_ts) append_query_param(query, "max_ts", *params.max_ts);

    return query;
}

Result<PaginatedResponse<Fill>> KalshiClient::get_fills(const GetFillsParams& params) {
    std::string path = build_fills_query(params);
    auto response = impl_->client.get(path);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get fills: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    std::vector<Fill> fills;
    auto fill_objects = extract_array_objects(response->body, "fills");

    for (const auto& obj : fill_objects) {
        Fill f;
        f.trade_id = extract_string(obj, "trade_id");
        f.order_id = extract_string(obj, "order_id");
        f.market_ticker = extract_string(obj, "ticker");
        f.side = parse_side(extract_string(obj, "side"));
        f.action = parse_action(extract_string(obj, "action"));
        f.count = static_cast<std::int32_t>(extract_int(obj, "count"));
        f.yes_price = static_cast<std::int32_t>(extract_int(obj, "yes_price"));
        f.no_price = static_cast<std::int32_t>(extract_int(obj, "no_price"));
        f.created_time = extract_int(obj, "created_time");
        f.is_taker = extract_bool(obj, "is_taker");
        fills.push_back(f);
    }

    PaginatedResponse<Fill> result;
    result.items = std::move(fills);

    std::string cursor = extract_cursor(response->body);
    if (!cursor.empty()) {
        result.next_cursor = Cursor{cursor};
    }

    return result;
}

Result<PaginatedResponse<Settlement>> KalshiClient::get_settlements(const GetPositionsParams& params) {
    std::string path = "/portfolio/settlements";

    if (params.limit) append_query_param(path, "limit", *params.limit);
    if (params.cursor) append_query_param(path, "cursor", *params.cursor);
    if (params.market_ticker) append_query_param(path, "market_ticker", *params.market_ticker);

    auto response = impl_->client.get(path);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to get settlements: " + std::to_string(response->status_code),
                                     response->status_code});
    }

    std::vector<Settlement> settlements;
    auto settlement_objects = extract_array_objects(response->body, "settlements");

    for (const auto& obj : settlement_objects) {
        Settlement s;
        s.market_ticker = extract_string(obj, "market_ticker");
        s.result = extract_string(obj, "result");
        s.yes_count = static_cast<std::int32_t>(extract_int(obj, "yes_count"));
        s.no_count = static_cast<std::int32_t>(extract_int(obj, "no_count"));
        s.revenue = extract_int(obj, "revenue");
        s.settled_time = extract_int(obj, "settled_time");
        settlements.push_back(s);
    }

    PaginatedResponse<Settlement> result;
    result.items = std::move(settlements);

    std::string cursor = extract_cursor(response->body);
    if (!cursor.empty()) {
        result.next_cursor = Cursor{cursor};
    }

    return result;
}

// ===== Order Management =====

std::string KalshiClient::serialize_create_order(const CreateOrderParams& params) {
    std::ostringstream ss;
    ss << "{";
    ss << "\"ticker\":\"" << escape_json_string(params.ticker) << "\"";
    ss << ",\"side\":\"" << to_json_string(params.side) << "\"";
    ss << ",\"action\":\"" << to_json_string(params.action) << "\"";
    ss << ",\"type\":\"" << params.type << "\"";
    ss << ",\"count\":" << params.count;

    if (params.yes_price) {
        ss << ",\"yes_price\":" << *params.yes_price;
    }
    if (params.no_price) {
        ss << ",\"no_price\":" << *params.no_price;
    }
    if (params.client_order_id) {
        ss << ",\"client_order_id\":\"" << escape_json_string(*params.client_order_id) << "\"";
    }
    if (params.expiration_ts) {
        ss << ",\"expiration_ts\":" << *params.expiration_ts;
    }
    if (params.sell_position_floor) {
        ss << ",\"sell_position_floor\":" << *params.sell_position_floor;
    }
    if (params.buy_max_cost) {
        ss << ",\"buy_max_cost\":" << *params.buy_max_cost;
    }

    ss << "}";
    return ss.str();
}

Result<Order> KalshiClient::create_order(const CreateOrderParams& params) {
    std::string body = serialize_create_order(params);

    auto response = impl_->client.post("/portfolio/orders", body);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200 && response->status_code != 201) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to create order: " + response->body,
                                     response->status_code});
    }

    return parse_order(response->body);
}

Result<void> KalshiClient::cancel_order(const std::string& order_id) {
    auto response = impl_->client.del("/portfolio/orders/" + order_id);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200 && response->status_code != 204) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to cancel order: " + response->body,
                                     response->status_code});
    }

    return {};
}

std::string KalshiClient::serialize_amend_order(const AmendOrderParams& params) {
    std::ostringstream ss;
    ss << "{";

    bool first = true;
    if (params.count) {
        ss << "\"count\":" << *params.count;
        first = false;
    }
    if (params.yes_price) {
        if (!first) ss << ",";
        ss << "\"yes_price\":" << *params.yes_price;
        first = false;
    }
    if (params.no_price) {
        if (!first) ss << ",";
        ss << "\"no_price\":" << *params.no_price;
    }

    ss << "}";
    return ss.str();
}

Result<Order> KalshiClient::amend_order(const AmendOrderParams& params) {
    std::string body = serialize_amend_order(params);

    auto response = impl_->client.post("/portfolio/orders/" + params.order_id + "/amend", body);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to amend order: " + response->body,
                                     response->status_code});
    }

    return parse_order(response->body);
}

std::string KalshiClient::serialize_decrease_order(const DecreaseOrderParams& params) {
    std::ostringstream ss;
    ss << "{\"reduce_by\":" << params.reduce_by << "}";
    return ss.str();
}

Result<Order> KalshiClient::decrease_order(const DecreaseOrderParams& params) {
    std::string body = serialize_decrease_order(params);

    auto response = impl_->client.post("/portfolio/orders/" + params.order_id + "/decrease", body);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to decrease order: " + response->body,
                                     response->status_code});
    }

    return parse_order(response->body);
}

std::string KalshiClient::serialize_batch_create(const BatchOrderRequest& request) {
    std::ostringstream ss;
    ss << "{\"orders\":[";

    for (size_t i = 0; i < request.orders.size(); ++i) {
        if (i > 0) ss << ",";
        ss << serialize_create_order(request.orders[i]);
    }

    ss << "]}";
    return ss.str();
}

Result<BatchResponse<Order>> KalshiClient::batch_create_orders(const BatchOrderRequest& request) {
    std::string body = serialize_batch_create(request);

    auto response = impl_->client.post("/portfolio/orders/batched", body);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (response->status_code != 200) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to batch create orders: " + response->body,
                                     response->status_code});
    }

    BatchResponse<Order> result;
    auto orders = parse_orders(response->body);
    if (orders) {
        result.results = std::move(*orders);
    }

    return result;
}

std::string KalshiClient::serialize_batch_cancel(const BatchCancelRequest& request) {
    std::ostringstream ss;
    ss << "{\"order_ids\":[";

    for (size_t i = 0; i < request.order_ids.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << escape_json_string(request.order_ids[i]) << "\"";
    }

    ss << "]}";
    return ss.str();
}

Result<BatchResponse<std::string>> KalshiClient::batch_cancel_orders(const BatchCancelRequest& request) {
    std::string body = serialize_batch_cancel(request);

    auto response = impl_->client.del("/portfolio/orders/batched");
    if (!response) {
        return std::unexpected(response.error());
    }

    // Note: Batch cancel might use POST with body or DELETE
    // Adjust based on actual API behavior

    if (response->status_code != 200 && response->status_code != 204) {
        return std::unexpected(Error{ErrorCode::ServerError,
                                     "Failed to batch cancel orders: " + response->body,
                                     response->status_code});
    }

    BatchResponse<std::string> result;
    result.results = request.order_ids; // Assume all cancelled if 200
    return result;
}

} // namespace kalshi
