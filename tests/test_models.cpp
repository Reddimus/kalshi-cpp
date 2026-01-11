#include "kalshi/models/market.hpp"
#include "kalshi/models/order.hpp"

#include <iostream>
#include <string>
#include <vector>

// Test declarations from test_main.cpp
struct TestResult {
    std::string name;
    bool passed;
    std::string message;
};
extern std::vector<TestResult> g_results;

#define TEST(name)                                             \
    void test_##name();                                        \
    struct TestRegister_##name {                               \
        TestRegister_##name() {                                \
            try {                                              \
                test_##name();                                 \
                g_results.push_back({#name, true, ""});        \
            } catch (const std::exception& e) {                \
                g_results.push_back({#name, false, e.what()}); \
            }                                                  \
        }                                                      \
    } g_register_##name;                                       \
    void test_##name()

#define ASSERT_TRUE(expr) \
    if (!(expr))          \
    throw std::runtime_error("Assertion failed: " #expr)

#define ASSERT_FALSE(expr) \
    if (expr)              \
    throw std::runtime_error("Assertion failed: NOT " #expr)

#define ASSERT_EQ(a, b) \
    if ((a) != (b))     \
    throw std::runtime_error("Assertion failed: " #a " == " #b)

TEST(market_default_construction) {
    kalshi::Market market;
    ASSERT_TRUE(market.ticker.empty());
    ASSERT_EQ(market.status, kalshi::MarketStatus::Open);
    ASSERT_EQ(market.volume, 0);
}

TEST(orderbook_entry_construction) {
    kalshi::OrderBookEntry entry{50, 100};
    ASSERT_EQ(entry.price_cents, 50);
    ASSERT_EQ(entry.quantity, 100);
}

TEST(position_default_construction) {
    kalshi::Position pos;
    ASSERT_TRUE(pos.market_ticker.empty());
    ASSERT_EQ(pos.yes_contracts, 0);
    ASSERT_EQ(pos.no_contracts, 0);
}

TEST(order_request_construction) {
    kalshi::OrderRequest req;
    req.market_ticker = "TEST-MARKET";
    req.side = kalshi::Side::Yes;
    req.action = kalshi::Action::Buy;
    req.type = kalshi::OrderType::Limit;
    req.count = 10;
    req.price = 50;

    ASSERT_EQ(req.market_ticker, std::string("TEST-MARKET"));
    ASSERT_EQ(req.side, kalshi::Side::Yes);
    ASSERT_EQ(req.action, kalshi::Action::Buy);
    ASSERT_EQ(req.count, 10);
    ASSERT_TRUE(req.price.has_value());
    ASSERT_EQ(*req.price, 50);
}

TEST(order_default_construction) {
    kalshi::Order order;
    ASSERT_TRUE(order.order_id.empty());
    ASSERT_EQ(order.status, kalshi::OrderStatus::Pending);
    ASSERT_EQ(order.initial_count, 0);
}

TEST(trade_default_construction) {
    kalshi::Trade trade;
    ASSERT_TRUE(trade.trade_id.empty());
    ASSERT_EQ(trade.count, 0);
    ASSERT_FALSE(trade.is_taker);
}

TEST(side_enum_values) {
    ASSERT_TRUE(kalshi::Side::Yes != kalshi::Side::No);
}

TEST(action_enum_values) {
    ASSERT_TRUE(kalshi::Action::Buy != kalshi::Action::Sell);
}
