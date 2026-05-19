#include <gtest/gtest.h>

#include "subscription_registry.hpp"

TEST(WsSubscriptionRegistry, FallsBackToClientIdBeforeAck) {
	kalshi::ws_detail::SubscriptionRegistry registry;

	EXPECT_EQ(registry.resolve(12), 12);
}

TEST(WsSubscriptionRegistry, ResolvesServerSidAfterSubscribedAck) {
	kalshi::ws_detail::SubscriptionRegistry registry;

	registry.register_ack(12, 9876);

	EXPECT_EQ(registry.resolve(12), 9876);
	EXPECT_EQ(registry.resolve(13), 13);
}

TEST(WsSubscriptionRegistry, EraseDropsStaleMappingAfterUnsubscribe) {
	kalshi::ws_detail::SubscriptionRegistry registry;
	registry.register_ack(12, 9876);

	registry.erase(12);

	EXPECT_EQ(registry.resolve(12), 12);
}

TEST(WsSubscriptionRegistry, ClearDropsMappingsAfterReconnectOrClose) {
	kalshi::ws_detail::SubscriptionRegistry registry;
	registry.register_ack(12, 9876);
	registry.register_ack(13, 9877);

	registry.clear();

	EXPECT_EQ(registry.resolve(12), 12);
	EXPECT_EQ(registry.resolve(13), 13);
}
