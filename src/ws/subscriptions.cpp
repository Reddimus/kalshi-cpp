#include "subscriptions.hpp"

#include <algorithm>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "wire.hpp"

namespace kalshi::detail {

namespace {

template <typename T>
struct IsUpdate : std::false_type {};

template <typename Msg>
struct IsUpdate<ws::Update<Msg>> : std::true_type {};

// Adds values missing from `list`, or removes `values` from it.
void merge(std::vector<std::string>& list, const std::vector<std::string>& values,
		   const std::optional<std::string>& single, bool add) {
	std::vector<std::string> changes = values;
	if (single) {
		changes.push_back(*single);
	}
	for (const std::string& value : changes) {
		const std::vector<std::string>::iterator found = std::find(list.begin(), list.end(), value);
		if (add && found == list.end()) {
			list.push_back(value);
		} else if (!add && found != list.end()) {
			list.erase(found);
		}
	}
}

// Keeps the parameters a resubscribe sends in step with confirmed updates.
void apply(ws::SubscribeParams& params, const ws::UpdateSubscriptionParams& update) {
	switch (update.action) {
		case ws::UpdateAction::AddMarkets:
		case ws::UpdateAction::DeleteMarkets: {
			if (params.market_ticker) {
				params.market_tickers.insert(params.market_tickers.begin(), *params.market_ticker);
				params.market_ticker.reset();
			}
			if (params.market_id) {
				params.market_ids.insert(params.market_ids.begin(), *params.market_id);
				params.market_id.reset();
			}
			const bool add = update.action == ws::UpdateAction::AddMarkets;
			merge(params.market_tickers, update.market_tickers, update.market_ticker, add);
			merge(params.market_ids, update.market_ids, update.market_id, add);
			break;
		}
		case ws::UpdateAction::SubscribeIndices:
		case ws::UpdateAction::UnsubscribeIndices:
			merge(params.index_ids, update.index_ids, std::nullopt,
				  update.action == ws::UpdateAction::SubscribeIndices);
			break;
		case ws::UpdateAction::SubscribeUnderlyings:
		case ws::UpdateAction::UnsubscribeUnderlyings:
			merge(params.underlying_tickers, update.underlying_tickers, std::nullopt,
				  update.action == ws::UpdateAction::SubscribeUnderlyings);
			break;
		default:
			break; // snapshots and lists leave the subscription as it is
	}
}

bool has_markets(const ws::SubscribeParams& params) {
	return params.market_ticker || !params.market_tickers.empty() || params.market_id ||
		   !params.market_ids.empty();
}

/// Actions that change what a subscription receives, as opposed to requests
/// such as get_snapshot and the list actions, whose replies go to the
/// connection that asked.
bool changes_subscription(ws::UpdateAction action) {
	switch (action) {
		case ws::UpdateAction::AddMarkets:
		case ws::UpdateAction::DeleteMarkets:
		case ws::UpdateAction::SubscribeIndices:
		case ws::UpdateAction::UnsubscribeIndices:
		case ws::UpdateAction::SubscribeUnderlyings:
		case ws::UpdateAction::UnsubscribeUnderlyings:
		case ws::UpdateAction::SubscribeMatches:
		case ws::UpdateAction::UnsubscribeMatches:
			return true;
		default:
			return false;
	}
}

Error invalid_request(std::string message) {
	return Error{ErrorCode::InvalidRequest, std::move(message), 0, {}};
}

bool is_empty(const std::string& value) {
	return value.empty();
}

/// Checks a command's market selectors and returns the kind it uses. Kalshi
/// selects markets by ID only on the ticker channel. Elsewhere an ID filter is
/// acknowledged but selects nothing, so an orderbook would never receive its
/// snapshots. The spec makes the four selectors mutually exclusive, and an
/// empty ticker or ID selects no market.
template <typename Params>
Result<MarketSelector> check_selectors(ws::Channel channel, const Params& params) {
	const bool ticker = params.market_ticker.has_value();
	const bool tickers = !params.market_tickers.empty();
	const bool id = params.market_id.has_value();
	const bool ids = !params.market_ids.empty();
	if (channel != ws::Channel::Ticker && (id || ids)) {
		return std::unexpected(invalid_request(
			"market_id and market_ids select markets only on the ticker channel; use market "
			"tickers"));
	}
	if (static_cast<int>(ticker) + static_cast<int>(tickers) + static_cast<int>(id) +
			static_cast<int>(ids) >
		1) {
		return std::unexpected(invalid_request(
			"Give only one of market_ticker, market_tickers, market_id, and market_ids"));
	}
	if ((ticker && params.market_ticker->empty()) || (id && params.market_id->empty()) ||
		std::any_of(params.market_tickers.begin(), params.market_tickers.end(), is_empty) ||
		std::any_of(params.market_ids.begin(), params.market_ids.end(), is_empty)) {
		return std::unexpected(invalid_request("Market tickers and IDs must not be empty"));
	}
	if (ticker || tickers) {
		return MarketSelector::Tickers;
	}
	return id || ids ? MarketSelector::Ids : MarketSelector::None;
}

bool cfbenchmarks(ws::Channel channel) {
	return channel == ws::Channel::CfbenchmarksValue ||
		   channel == ws::Channel::CfbenchmarksValue5hz;
}

/// Checks an update against the channels the spec gives its action, and that
/// the action has the list it needs: the spec's errors 13, 14, 23, 24, and 28.
Result<void> check_action(ws::Channel channel, const ws::UpdateSubscriptionParams& params) {
	bool allowed = true;
	const std::vector<std::string>* list = nullptr;
	std::string_view list_name;
	switch (params.action) {
		case ws::UpdateAction::AddMarkets:
		case ws::UpdateAction::DeleteMarkets:
			allowed = !cfbenchmarks(channel) && channel != ws::Channel::PythValue &&
					  channel != ws::Channel::EbattleScores;
			break;
		case ws::UpdateAction::GetSnapshot:
			allowed = channel == ws::Channel::OrderbookDelta;
			break;
		case ws::UpdateAction::SubscribeIndices:
		case ws::UpdateAction::UnsubscribeIndices:
			list = &params.index_ids;
			list_name = "index_ids";
			allowed = cfbenchmarks(channel);
			break;
		case ws::UpdateAction::Indexlist:
			allowed = cfbenchmarks(channel);
			break;
		case ws::UpdateAction::SubscribeUnderlyings:
		case ws::UpdateAction::UnsubscribeUnderlyings:
			list = &params.underlying_tickers;
			list_name = "underlying_tickers";
			allowed = channel == ws::Channel::PythValue;
			break;
		case ws::UpdateAction::UnderlyingList:
			allowed = channel == ws::Channel::PythValue;
			break;
		case ws::UpdateAction::SubscribeMatches:
		case ws::UpdateAction::UnsubscribeMatches:
			list = &params.match_ids;
			list_name = "match_ids";
			allowed = channel == ws::Channel::EbattleScores;
			break;
		case ws::UpdateAction::Matchlist:
			allowed = channel == ws::Channel::EbattleScores;
			break;
		case ws::UpdateAction::Unknown:
			break;
	}
	const std::string action{ws::to_string(params.action)};
	if (!allowed) {
		return std::unexpected(invalid_request(action + " is not available on the " +
											   std::string{ws::to_string(channel)} + " channel"));
	}
	if (list != nullptr && list->empty()) {
		return std::unexpected(invalid_request(action + " requires " + std::string{list_name}));
	}
	const bool market_action = params.action == ws::UpdateAction::AddMarkets ||
							   params.action == ws::UpdateAction::DeleteMarkets ||
							   params.action == ws::UpdateAction::GetSnapshot;
	if (market_action && !params.market_ticker && params.market_tickers.empty() &&
		!params.market_id && params.market_ids.empty()) {
		return std::unexpected(invalid_request(
			action + " requires market_ticker, market_tickers, market_id, or market_ids"));
	}
	return {};
}

/// The bounds behind the spec's error codes 19 to 22.
Result<void> check_shards(const ws::SubscribeParams& params) {
	if (params.shard_key && !params.shard_factor) {
		return std::unexpected(invalid_request("SubscribeParams.shard_key requires shard_factor"));
	}
	if (params.shard_factor && (*params.shard_factor < 1 || *params.shard_factor > 100)) {
		return std::unexpected(
			invalid_request("SubscribeParams.shard_factor must be from 1 to 100"));
	}
	if (params.shard_key && (*params.shard_key < 0 || *params.shard_key >= *params.shard_factor)) {
		return std::unexpected(invalid_request(
			"SubscribeParams.shard_key must be at least 0 and less than shard_factor"));
	}
	return {};
}

Error unknown_subscription(ws::Subscription subscription) {
	return Error{ErrorCode::InvalidRequest,
				 "Unknown subscription " + std::to_string(subscription.id),
				 0,
				 {}};
}

} // namespace

Result<ws::Subscription> SubscriptionRegistry::subscribe(ws::Channel channel,
														 ws::SubscribeParams params, bool connected,
														 std::vector<std::string>& frames) {
	if (ws::to_string(channel).empty()) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "A known channel is required", 0, {}});
	}
	if (params.update_interval_ms && ws::to_string(*params.update_interval_ms).empty()) {
		return std::unexpected(Error{ErrorCode::InvalidRequest,
									 "SubscribeParams.update_interval_ms must be 0, 100, or 1000",
									 0,
									 {}});
	}
	if (Result<void> shards = check_shards(params); !shards) {
		return std::unexpected(std::move(shards.error()));
	}
	Result<MarketSelector> selector = check_selectors(channel, params);
	if (!selector) {
		return std::unexpected(std::move(selector.error()));
	}
	if (channel == ws::Channel::OrderbookDelta &&
		std::any_of(entries_.begin(), entries_.end(),
					[](const std::pair<const std::int64_t, Entry>& item) {
						return item.second.handle.channel == ws::Channel::OrderbookDelta;
					})) {
		return std::unexpected(Error{ErrorCode::InvalidRequest,
									 "An orderbook subscription already exists; use add_markets, "
									 "or wait for unsubscribe confirmation",
									 0,
									 {}});
	}
	const ws::Subscription handle{next_id(), channel};
	Entry& entry = entries_[handle.id];
	entry.handle = handle;
	entry.params = std::move(params);
	entry.filtered = has_markets(entry.params);
	entry.selector = *selector;
	if (connected) {
		frames.push_back(subscribe_frame(entry));
	}
	return handle;
}

Result<void> SubscriptionRegistry::unsubscribe(ws::Subscription subscription,
											   std::vector<std::string>& frames) {
	Entry* entry = find(subscription.id);
	if (entry == nullptr) {
		return std::unexpected(unknown_subscription(subscription));
	}
	switch (entry->state) {
		case State::Unsent:
			erase(subscription.id);
			break;
		case State::Pending:
			entry->unsubscribe_held = true;
			entry->held.clear();
			break;
		case State::Active:
			frames.push_back(unsubscribe_frame(*entry));
			break;
		case State::Unsubscribing:
			break;
	}
	return {};
}

Result<void> SubscriptionRegistry::update(ws::Subscription subscription,
										  const ws::UpdateSubscriptionParams& params,
										  bool connected, std::vector<std::string>& frames) {
	if (params.action == ws::UpdateAction::Unknown) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "UpdateSubscriptionParams.action is required", 0, {}});
	}
	Entry* entry = find(subscription.id);
	if (entry == nullptr) {
		return std::unexpected(unknown_subscription(subscription));
	}
	if (entry->unsubscribe_held || entry->state == State::Unsubscribing) {
		return std::unexpected(Error{
			ErrorCode::InvalidRequest, "Subscription is awaiting unsubscribe confirmation", 0, {}});
	}
	if (Result<void> action = check_action(entry->handle.channel, params); !action) {
		return std::unexpected(std::move(action.error()));
	}
	Result<MarketSelector> selector = check_selectors(entry->handle.channel, params);
	if (!selector) {
		return std::unexpected(std::move(selector.error()));
	}
	if (*selector != MarketSelector::None && entry->selector != MarketSelector::None &&
		*selector != entry->selector) {
		// Mixing kinds would leave a resubscribe naming markets both ways.
		return std::unexpected(invalid_request(
			entry->selector == MarketSelector::Tickers
				? "This subscription names markets by ticker; use market_ticker or market_tickers"
				: "This subscription names markets by ID; use market_id or market_ids"));
	}
	const bool changes_markets = params.action == ws::UpdateAction::AddMarkets ||
								 params.action == ws::UpdateAction::DeleteMarkets;
	if (entry->handle.channel == ws::Channel::Ticker && !entry->filtered && changes_markets &&
		entry->state != State::Unsent) {
		// Kalshi answers with error 13 and keeps streaming every market. An
		// unsent subscription has no server state yet, so the change is its filter.
		return std::unexpected(Error{ErrorCode::InvalidRequest,
									 "A ticker subscription made without markets cannot change; "
									 "unsubscribe, then subscribe with markets",
									 0,
									 {}});
	}
	if (changes_markets && entry->selector == MarketSelector::None) {
		entry->selector = *selector;
	}
	switch (entry->state) {
		case State::Unsent:
			if (!entry->held.empty()) {
				// Changes from a lost connection follow the next confirmation;
				// this one goes after them. As below, requests are dropped: the
				// resubscribe sends fresh snapshots.
				if (changes_subscription(params.action)) {
					entry->held.push_back(params);
				}
				break;
			}
			// No server subscription to change; the next subscribe carries it. A
			// subscription left out for having no markets starts again here.
			record_update(*entry, params);
			if (connected && (!entry->filtered || has_markets(entry->params))) {
				frames.push_back(subscribe_frame(*entry));
			}
			break;
		case State::Pending:
			entry->held.push_back(params);
			break;
		case State::Active:
			frames.push_back(update_frame(*entry, params));
			break;
		case State::Unsubscribing:
			break;
	}
	return {};
}

std::int64_t SubscriptionRegistry::list_subscriptions(std::vector<std::string>& frames) {
	const std::int64_t id = next_id();
	commands_[id] = InFlight{Kind::List, 0};
	frames.push_back(render_command(BareCommand{id, "list_subscriptions"}));
	return id;
}

void SubscriptionRegistry::on_connected(std::vector<std::string>& frames) {
	(void)on_disconnected();
	for (std::pair<const std::int64_t, Entry>& item : entries_) {
		if (resubscribes(item.second)) {
			frames.push_back(subscribe_frame(item.second));
		}
	}
}

std::vector<ws::Subscription> SubscriptionRegistry::on_disconnected() {
	// Whether the server applied these is unknown, and its subscriptions end
	// with the connection anyway. They go again, in the order sent.
	std::map<std::int64_t, std::vector<ws::UpdateSubscriptionParams>> unconfirmed;
	for (const std::pair<const std::int64_t, InFlight>& item : commands_) {
		if (item.second.update && changes_subscription(item.second.update->action)) {
			unconfirmed[item.second.subscription].push_back(*item.second.update);
		}
	}
	commands_.clear();
	by_sid_.clear();
	std::vector<ws::Subscription> ended;
	for (const std::pair<const std::int64_t, Entry>& item : entries_) {
		if (item.second.state == State::Unsubscribing || item.second.unsubscribe_held) {
			ended.push_back(item.second.handle);
		}
	}
	std::erase_if(entries_, [](const std::pair<const std::int64_t, Entry>& item) {
		return item.second.state == State::Unsubscribing || item.second.unsubscribe_held;
	});
	for (std::pair<const std::int64_t, Entry>& item : entries_) {
		Entry& entry = item.second;
		entry.state = State::Unsent;
		entry.sid = 0;
		entry.last_seq.reset();
		// Sent changes come before held ones, which were made later.
		std::vector<ws::UpdateSubscriptionParams> pending = std::move(unconfirmed[item.first]);
		for (ws::UpdateSubscriptionParams& params : entry.held) {
			if (changes_subscription(params.action)) {
				pending.push_back(std::move(params));
			}
		}
		entry.held.clear();
		if (resubscribes(entry)) {
			// Sent on their own after the next confirmation, a rejected change
			// is reported and leaves the subscription as it was.
			entry.held = std::move(pending);
		} else {
			// Not resubscribed until it has markets, so these are its next
			// subscribe, as for a subscription never sent.
			for (const ws::UpdateSubscriptionParams& params : pending) {
				record_update(entry, params);
			}
		}
	}
	return ended;
}

void SubscriptionRegistry::clear() {
	entries_.clear();
	by_sid_.clear();
	commands_.clear();
}

Reaction SubscriptionRegistry::on_subscribed(const SubscribedFrame& frame) {
	Reaction reaction;
	const std::optional<InFlight> command = take(frame.id);
	Entry* entry =
		command && command->kind == Kind::Subscribe ? find(command->subscription) : nullptr;
	if (entry == nullptr) {
		// Nothing here wants this subscription any more; end it.
		const std::int64_t id = next_id();
		commands_[id] = InFlight{Kind::Unsubscribe, 0};
		reaction.frames.push_back(
			render_command(Command<SidsWire>{id, "unsubscribe", {{frame.sid}}}));
		return reaction;
	}
	entry->sid = frame.sid;
	entry->state = State::Active;
	entry->last_seq.reset();
	by_sid_[frame.sid] = entry->handle.id;
	if (entry->unsubscribe_held) {
		entry->unsubscribe_held = false;
		reaction.frames.push_back(unsubscribe_frame(*entry));
		return reaction;
	}
	if (!entry->match_ids.empty()) {
		ws::UpdateSubscriptionParams matches;
		matches.action = ws::UpdateAction::SubscribeMatches;
		matches.match_ids = entry->match_ids;
		reaction.frames.push_back(update_frame(*entry, matches));
	}
	for (const ws::UpdateSubscriptionParams& params : entry->held) {
		reaction.frames.push_back(update_frame(*entry, params));
	}
	entry->held.clear();
	reaction.message = ws::Subscribed{entry->handle, frame.sid, frame.sending_ts_ms};
	return reaction;
}

Reaction SubscriptionRegistry::on_unsubscribed(const UnsubscribedFrame& frame) {
	Reaction reaction;
	const std::optional<InFlight> command = take(frame.id);
	std::int64_t subscription = command ? command->subscription : 0;
	if (subscription == 0 && frame.sid) {
		const std::unordered_map<std::int64_t, std::int64_t>::const_iterator found =
			by_sid_.find(*frame.sid);
		subscription = found == by_sid_.end() ? 0 : found->second;
	}
	if (const Entry* entry = find(subscription)) {
		reaction.message = ws::Unsubscribed{entry->handle, frame.sending_ts_ms};
		erase(subscription);
	}
	return reaction;
}

Reaction SubscriptionRegistry::on_ok(const OkFrame& frame, bool resync) {
	Reaction reaction;
	reaction.error = check_frame_seq(frame.sid, frame.seq, resync, reaction.frames);
	const std::optional<InFlight> command = take(frame.id);
	if (!command) {
		return reaction;
	}
	if (command->kind == Kind::List) {
		std::optional<std::vector<ws::ListedSubscription>> list =
			parse_subscription_list(frame.msg);
		if (!list) {
			// Reporting an empty list would tell the caller it has no subscriptions.
			reaction.error = WsError{0,
									 "Could not parse a list_subscriptions reply",
									 frame.id,
									 std::nullopt,
									 std::nullopt,
									 std::nullopt,
									 frame.sending_ts_ms};
			return reaction;
		}
		// take() found the command, so the reply carried its id.
		reaction.message =
			ws::SubscriptionList{frame.id.value_or(0), std::move(*list), frame.sending_ts_ms};
		return reaction;
	}
	Entry* entry = command->kind == Kind::Update ? find(command->subscription) : nullptr;
	if (entry == nullptr) {
		return reaction;
	}
	if (command->update) {
		record_update(*entry, *command->update);
	}
	ws::Updated updated = parse_updated(frame.msg);
	updated.subscription = entry->handle;
	updated.sending_ts_ms = frame.sending_ts_ms;
	// The reply lists every market afterwards; trust it over local bookkeeping.
	// It may list markets both ways, so only the subscription's own selector is
	// taken, or a resubscribe would name both.
	if (entry->selector == MarketSelector::Tickers && !updated.market_tickers.empty()) {
		entry->params.market_ticker.reset();
		entry->params.market_tickers = updated.market_tickers;
	} else if (entry->selector == MarketSelector::Ids && !updated.market_ids.empty()) {
		entry->params.market_id.reset();
		entry->params.market_ids = updated.market_ids;
	}
	entry->filtered = entry->filtered || has_markets(entry->params);
	reaction.message = std::move(updated);
	return reaction;
}

Reaction SubscriptionRegistry::on_error(const ErrorFrame& frame, bool resync) {
	Reaction reaction;
	// The server's error is reported; a gap it reveals still requests snapshots.
	(void)check_frame_seq(frame.sid, frame.seq, resync, reaction.frames);
	WsError error{frame.code, frame.message, frame.id,			 frame.sid,
				  frame.seq,  std::nullopt,	 frame.sending_ts_ms};
	if (error.message.empty()) {
		error.message = std::string{ws::error_code_name(frame.code)};
	}
	const std::optional<InFlight> command = take(frame.id);
	std::int64_t subscription = command ? command->subscription : 0;
	if (subscription == 0 && frame.sid) {
		const std::unordered_map<std::int64_t, std::int64_t>::const_iterator found =
			by_sid_.find(*frame.sid);
		subscription = found == by_sid_.end() ? 0 : found->second;
	}
	if (Entry* entry = find(subscription)) {
		error.subscription = entry->handle;
		if (frame.code == 10 || frame.code == 25 || (command && command->kind == Kind::Subscribe)) {
			erase(subscription);
		} else if (command && command->kind == Kind::Unsubscribe && frame.code == 7) {
			// Unknown subscription ID: the server no longer has it.
			reaction.message = ws::Unsubscribed{entry->handle, frame.sending_ts_ms};
			erase(subscription);
		} else if (command && command->kind == Kind::Unsubscribe) {
			entry->state = State::Active; // rejection does not confirm removal
		}
	}
	reaction.error = std::move(error);
	return reaction;
}

Reaction SubscriptionRegistry::on_data(WsMessage& message, bool resync) {
	Reaction reaction;
	std::visit(
		[&]<typename T>(T& update) {
			if constexpr (IsUpdate<T>::value) {
				if (update.id) {
					// Replies such as get_snapshot snapshots get no `ok`. Only an
					// update completes this way, never a pending subscribe.
					const std::map<std::int64_t, InFlight>::iterator command =
						commands_.find(*update.id);
					if (command != commands_.end() && command->second.kind == Kind::Update) {
						commands_.erase(command);
					}
				}
				Entry* entry = find_by_sid(update.sid);
				if (entry == nullptr) {
					return;
				}
				update.subscription = entry->handle.id;
				if (update.seq) {
					reaction.error =
						check_seq(*entry, update.sid, *update.seq, resync, reaction.frames);
				}
			}
		},
		message);
	return reaction;
}

std::vector<ws::Subscription> SubscriptionRegistry::subscriptions() const {
	std::vector<ws::Subscription> handles;
	handles.reserve(entries_.size());
	for (const std::pair<const std::int64_t, Entry>& item : entries_) {
		if (item.second.state != State::Unsubscribing && !item.second.unsubscribe_held) {
			handles.push_back(item.second.handle);
		}
	}
	return handles;
}

bool SubscriptionRegistry::resubscribes(const Entry& entry) {
	return !entry.filtered || has_markets(entry.params);
}

SubscriptionRegistry::Entry* SubscriptionRegistry::find(std::int64_t subscription) {
	const std::map<std::int64_t, Entry>::iterator found = entries_.find(subscription);
	return found == entries_.end() ? nullptr : &found->second;
}

void SubscriptionRegistry::record_update(Entry& entry, const ws::UpdateSubscriptionParams& params) {
	apply(entry.params, params);
	entry.filtered = entry.filtered || has_markets(entry.params);
	if (params.action == ws::UpdateAction::SubscribeMatches ||
		params.action == ws::UpdateAction::UnsubscribeMatches) {
		merge(entry.match_ids, params.match_ids, std::nullopt,
			  params.action == ws::UpdateAction::SubscribeMatches);
	}
}

void SubscriptionRegistry::erase(std::int64_t subscription) {
	const std::map<std::int64_t, Entry>::iterator found = entries_.find(subscription);
	if (found == entries_.end()) {
		return;
	}
	if (found->second.sid != 0) {
		by_sid_.erase(found->second.sid);
	}
	entries_.erase(found);
	std::erase_if(commands_, [subscription](const std::pair<const std::int64_t, InFlight>& item) {
		return item.second.subscription == subscription;
	});
}

SubscriptionRegistry::Entry* SubscriptionRegistry::find_by_sid(std::int64_t sid) {
	const std::unordered_map<std::int64_t, std::int64_t>::const_iterator found = by_sid_.find(sid);
	return found == by_sid_.end() ? nullptr : find(found->second);
}

std::optional<WsError> SubscriptionRegistry::check_seq(Entry& entry, std::int64_t sid,
													   std::int64_t seq, bool resync,
													   std::vector<std::string>& frames) {
	std::optional<WsError> gap;
	constexpr std::int64_t kMaxSeq = std::numeric_limits<std::int64_t>::max();
	if (entry.last_seq && (*entry.last_seq == kMaxSeq || seq != *entry.last_seq + 1)) {
		gap = WsError{0,
					  "Missed messages on subscription " + std::to_string(entry.handle.id) +
						  ": expected seq " +
						  (*entry.last_seq == kMaxSeq ? std::string{"beyond INT64_MAX"}
													  : std::to_string(*entry.last_seq + 1)) +
						  ", got " + std::to_string(seq),
					  std::nullopt,
					  sid,
					  seq,
					  entry.handle};
		if (resync && entry.handle.channel == ws::Channel::OrderbookDelta &&
			entry.state == State::Active) {
			ws::UpdateSubscriptionParams snapshots;
			snapshots.action = ws::UpdateAction::GetSnapshot;
			snapshots.market_tickers = entry.params.market_tickers;
			if (entry.params.market_ticker) {
				snapshots.market_tickers.push_back(*entry.params.market_ticker);
			}
			if (!snapshots.market_tickers.empty()) {
				frames.push_back(update_frame(entry, snapshots));
			}
		}
	}
	entry.last_seq = seq;
	return gap;
}

std::optional<WsError> SubscriptionRegistry::check_frame_seq(std::optional<std::int64_t> sid,
															 std::optional<std::int64_t> seq,
															 bool resync,
															 std::vector<std::string>& frames) {
	if (!sid || !seq) {
		return std::nullopt;
	}
	Entry* entry = find_by_sid(*sid);
	return entry == nullptr ? std::nullopt : check_seq(*entry, *sid, *seq, resync, frames);
}

std::optional<SubscriptionRegistry::InFlight>
SubscriptionRegistry::take(std::optional<std::int64_t> command) {
	if (!command) {
		return std::nullopt;
	}
	const std::map<std::int64_t, InFlight>::iterator found = commands_.find(*command);
	if (found == commands_.end()) {
		return std::nullopt;
	}
	InFlight in_flight = std::move(found->second);
	commands_.erase(found);
	return in_flight;
}

std::string SubscriptionRegistry::subscribe_frame(Entry& entry) {
	const std::int64_t id = next_id();
	commands_[id] = InFlight{Kind::Subscribe, entry.handle.id};
	entry.state = State::Pending;
	entry.sid = 0;
	entry.last_seq.reset();
	return render_command(Command<SubscribeWire>{
		id, "subscribe", to_wire(std::vector<ws::Channel>{entry.handle.channel}, entry.params)});
}

std::string SubscriptionRegistry::update_frame(Entry& entry,
											   const ws::UpdateSubscriptionParams& params) {
	const std::int64_t id = next_id();
	commands_[id] = InFlight{Kind::Update, entry.handle.id, params};
	return render_command(
		Command<UpdateSubscriptionWire>{id, "update_subscription", to_wire(entry.sid, params)});
}

std::string SubscriptionRegistry::unsubscribe_frame(Entry& entry) {
	const std::int64_t id = next_id();
	commands_[id] = InFlight{Kind::Unsubscribe, entry.handle.id};
	entry.state = State::Unsubscribing;
	return render_command(Command<SidsWire>{id, "unsubscribe", SidsWire{{entry.sid}}});
}

} // namespace kalshi::detail
