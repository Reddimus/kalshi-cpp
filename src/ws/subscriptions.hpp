#pragma once

#include "kalshi/websocket.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frames.hpp"

namespace kalshi::detail {

/// What a server frame means for the caller: commands to send in response, and
/// what to report.
struct Reaction {
	std::vector<std::string> frames;
	std::optional<WsMessage> message;
	/// A sequence gap revealed by a frame that has its own error, such as a
	/// malformed frame, a malformed `list_subscriptions` reply, or a server
	/// `error`. It is reported before `error`.
	std::optional<WsError> gap;
	std::optional<WsError> error;
	bool drop_data{false};
};

/// How a subscription names its markets. Kalshi's subscribe takes exactly one
/// of market_ticker, market_tickers, market_id, and market_ids.
enum class MarketSelector : std::uint8_t { None, Tickers, Ids };

/// Tracks subscriptions and in-flight commands across reconnects.
///
/// A subscription keeps its handle while its server `sid` changes. Commands
/// for a subscription the server has not confirmed yet wait for the
/// confirmation, so they never name a stale or foreign `sid`. Not thread-safe;
/// WebSocketClient guards it with its mutex.
class SubscriptionRegistry {
public:
	/// Adds a subscription, rejecting invalid intervals, duplicate orderbook handles,
	/// market IDs outside the ticker channel, more than one market selector, empty
	/// tickers or IDs, and shard parameters outside the spec's bounds. Frames to
	/// send are appended only when connected.
	Result<ws::Subscription> subscribe(ws::Channel channel, ws::SubscribeParams params,
									   bool connected, std::vector<std::string>& frames);
	Result<void> unsubscribe(ws::Subscription subscription, std::vector<std::string>& frames);
	/// Changes a subscription. The change reaches the parameters used to
	/// resubscribe once the server confirms it, so a rejected change is dropped.
	/// Before the subscription is sent, the change becomes part of its subscribe.
	/// Markets are named with the selector the subscription uses, so its
	/// resubscribe names them one way. Rejects actions on channels the spec
	/// does not give them, market actions without markets, and the index,
	/// underlying, and match actions without their lists.
	Result<void> update(ws::Subscription subscription, const ws::UpdateSubscriptionParams& params,
						bool connected, std::vector<std::string>& frames);
	/// Returns the command's ID, which the reply's SubscriptionList carries.
	std::int64_t list_subscriptions(std::vector<std::string>& frames);

	/// Subscribes everything again on a new connection, except subscriptions
	/// whose market filter is now empty.
	void on_connected(std::vector<std::string>& frames);
	/// Forgets the server's state; subscriptions resend on the next connection.
	/// The resubscribe carries only confirmed changes. Changes sent without a
	/// reply, and changes held for a confirmation, are sent on their own after
	/// the next confirmation, in order, so a rejected one leaves the
	/// subscription alive. A subscription whose markets were all removed is not
	/// resubscribed; its changes become its next subscribe instead. Snapshot and
	/// list requests are dropped: their replies belonged to the old connection,
	/// and a new orderbook subscription sends fresh snapshots.
	/// Returns the subscriptions being unsubscribed, which end with the connection.
	std::vector<ws::Subscription> on_disconnected();
	void clear();

	Reaction on_subscribed(const SubscribedFrame& frame);
	Reaction on_unsubscribed(const UnsubscribedFrame& frame);
	/// `ok` and `error` frames on sequenced channels share the data's `seq`,
	/// so they are checked for gaps like data. An `error` frame's or malformed
	/// `list_subscriptions` reply's gap goes in Reaction::gap.
	Reaction on_ok(const OkFrame& frame, bool resync = false);
	Reaction on_error(const ErrorFrame& frame, bool resync = false);
	/// Fills in Update::subscription and reports sequence gaps, snapshots
	/// included. On an orderbook_delta gap, `resync` requests fresh snapshots.
	/// A reply carrying a command ID, such as a get_snapshot snapshot, completes it.
	/// Sets Reaction::drop_data when `sid` does not belong to a live subscription.
	Reaction on_data(WsMessage& message, bool resync);
	/// Reports a malformed frame, attaching it to the subscription when `sid`
	/// is live and requesting an orderbook snapshot resync when `resync` is set.
	/// A seq the frame skips is reported in Reaction::gap.
	Reaction on_malformed(const MalformedFrame& frame, bool resync);

	[[nodiscard]] std::vector<ws::Subscription> subscriptions() const;

private:
	enum class State : std::uint8_t { Unsent, Pending, Active, Unsubscribing };
	enum class Kind : std::uint8_t { Subscribe, Unsubscribe, Update, List };

	struct Entry {
		ws::Subscription handle;
		ws::SubscribeParams params;
		State state{State::Unsent};
		/// The server's ID once it confirms; 0 before. Kalshi's sids start at 1.
		std::int64_t sid{0};
		std::optional<std::int64_t> last_seq;
		/// update_subscription calls to send once the server confirms: those
		/// made while Pending, and those a lost connection left unanswered.
		std::vector<ws::UpdateSubscriptionParams> held;
		bool unsubscribe_held{false};
		/// Has used a market filter. Once every market is removed, it is
		/// not resubscribed, which would stream every market instead.
		bool filtered{false};
		/// Set by the subscribe, or by the first market change while unfiltered.
		MarketSelector selector{MarketSelector::None};
		/// Matches added with subscribe_matches. A subscribe carries none, so
		/// they are sent again each time the server confirms the subscription.
		std::vector<std::string> match_ids;
	};

	struct InFlight {
		Kind kind{Kind::Subscribe};
		std::int64_t subscription{0};
		/// For Kind::Update: the change to apply when the server confirms it.
		std::optional<ws::UpdateSubscriptionParams> update;
	};

	std::int64_t next_id() { return next_id_++; }
	Entry* find(std::int64_t subscription);
	void erase(std::int64_t subscription);
	static void record_update(Entry& entry, const ws::UpdateSubscriptionParams& params);
	/// False for a subscription whose market filter is now empty.
	static bool resubscribes(const Entry& entry);
	std::optional<InFlight> take(std::optional<std::int64_t> command);
	Entry* find_by_sid(std::int64_t sid);
	/// Records `seq` for the subscription and reports a gap, appending the
	/// snapshot request an orderbook resync needs.
	std::optional<WsError> check_seq(Entry& entry, std::int64_t sid, std::int64_t seq, bool resync,
									 std::vector<std::string>& frames);
	void append_resync(Entry& entry, std::vector<std::string>& frames);
	/// Checks the sequence number that frames such as `ok` carry.
	std::optional<WsError> check_frame_seq(std::optional<std::int64_t> sid,
										   std::optional<std::int64_t> seq, bool resync,
										   std::vector<std::string>& frames);
	std::string subscribe_frame(Entry& entry);
	std::string update_frame(Entry& entry, const ws::UpdateSubscriptionParams& params);
	std::string unsubscribe_frame(Entry& entry);

	std::int64_t next_id_{1};
	std::map<std::int64_t, Entry> entries_; // by Subscription::id
	std::unordered_map<std::int64_t, std::int64_t> by_sid_;
	std::map<std::int64_t, InFlight> commands_; // in flight, in the order sent
};

} // namespace kalshi::detail
