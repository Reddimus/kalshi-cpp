// Runs the real libcurl transport against a loopback HTTP server.

#include "kalshi/http_client.hpp"
#include "kalshi/retry.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <gtest/gtest.h>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "test_signer_fixture.hpp"

namespace {

using namespace std::chrono_literals;

#ifdef _WIN32
using Socket = SOCKET;
using AddressLength = int;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
constexpr int kShutdownBoth = SD_BOTH;
void close_socket(Socket socket) {
	(void)::closesocket(socket);
}
void initialize_sockets() {
	struct Winsock {
		Winsock() {
			WSADATA data{};
			if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
				throw std::runtime_error("WSAStartup() failed");
			}
		}
		~Winsock() { (void)::WSACleanup(); }
	};
	static const Winsock initialized;
}
#else
using Socket = int;
using AddressLength = socklen_t;
constexpr Socket kInvalidSocket = -1;
constexpr int kShutdownBoth = SHUT_RDWR;
void close_socket(Socket socket) {
	(void)::close(socket);
}
void initialize_sockets() {}
#endif
using SocketIoResult = decltype(::recv(Socket{}, nullptr, 0, 0));

class NoProxyEnvironment : public testing::Environment {
	void SetUp() override {
#ifdef _WIN32
		(void)::_putenv_s("NO_PROXY", "127.0.0.1");
		(void)::_putenv_s("no_proxy", "127.0.0.1");
#else
		(void)::setenv("NO_PROXY", "127.0.0.1", 1);
		(void)::setenv("no_proxy", "127.0.0.1", 1);
#endif
	}
};
[[maybe_unused]] testing::Environment* const kNoProxy =
	testing::AddGlobalTestEnvironment(new NoProxyEnvironment);

struct RecordedRequest {
	std::string method;
	std::string target;
	std::map<std::string, std::string> headers; // lower-case names
	std::string body;
};

/// Accepts connections on 127.0.0.1, records each request, and replies with a
/// fixed response, or never replies when `response` is empty.
class LoopbackServer {
public:
	explicit LoopbackServer(std::string response) : response_(std::move(response)) {
		initialize_sockets();
		listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		if (listener_ == kInvalidSocket ||
			::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
			::listen(listener_, 8) != 0) {
			return;
		}
		AddressLength length = sizeof(address);
		::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
		port_ = ntohs(address.sin_port);
		thread_ = std::thread([this] { serve(); });
	}

	~LoopbackServer() {
		stopping_ = true;
		if (listener_ != kInvalidSocket) {
			::shutdown(listener_, kShutdownBoth);
			close_socket(listener_);
		}
		if (thread_.joinable()) {
			thread_.join();
		}
	}

	LoopbackServer(const LoopbackServer&) = delete;
	LoopbackServer& operator=(const LoopbackServer&) = delete;

	[[nodiscard]] std::string base_url() const {
		return "http://127.0.0.1:" + std::to_string(port_) + "/trade-api/v2";
	}

	[[nodiscard]] RecordedRequest last() const {
		const std::scoped_lock lock(mutex_);
		return requests_.empty() ? RecordedRequest{} : requests_.back();
	}

private:
	void serve() {
		while (!stopping_) {
			const Socket connection = ::accept(listener_, nullptr, nullptr);
			if (connection == kInvalidSocket) {
				return;
			}
			handle(connection);
			close_socket(connection);
		}
	}

	void handle(Socket connection) {
		std::string data;
		std::array<char, 4096> buffer{};
		std::size_t header_end = std::string::npos;
		while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
			const SocketIoResult received =
				::recv(connection, buffer.data(), static_cast<int>(buffer.size()), 0);
			if (received <= 0) {
				return;
			}
			data.append(buffer.data(), static_cast<std::size_t>(received));
		}

		RecordedRequest request;
		const std::string head = data.substr(0, header_end);
		const std::size_t line_end = head.find("\r\n");
		const std::string request_line = head.substr(0, line_end);
		request.method = request_line.substr(0, request_line.find(' '));
		const std::size_t target_start = request.method.size() + 1;
		request.target = request_line.substr(target_start, request_line.rfind(' ') - target_start);
		std::size_t position = line_end == std::string::npos ? head.size() : line_end + 2;
		while (position < head.size()) {
			const std::size_t next = std::min(head.find("\r\n", position), head.size());
			const std::string line = head.substr(position, next - position);
			const std::size_t colon = line.find(':');
			if (colon != std::string::npos) {
				std::string name = line.substr(0, colon);
				std::transform(name.begin(), name.end(), name.begin(),
							   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				request.headers[name] = line.substr(line.find_first_not_of(' ', colon + 1));
			}
			position = next + 2;
		}

		const std::size_t length = request.headers.contains("content-length")
									   ? std::stoul(request.headers["content-length"])
									   : 0;
		request.body = data.substr(header_end + 4);
		while (request.body.size() < length) {
			const SocketIoResult received =
				::recv(connection, buffer.data(), static_cast<int>(buffer.size()), 0);
			if (received <= 0) {
				break;
			}
			request.body.append(buffer.data(), static_cast<std::size_t>(received));
		}
		{
			const std::scoped_lock lock(mutex_);
			requests_.push_back(request);
		}

		if (response_.empty()) {
			while (!stopping_) {
				std::this_thread::sleep_for(10ms);
			}
			return;
		}
#ifdef SO_NOSIGPIPE
		const int no_sigpipe = 1;
		(void)::setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
#ifdef MSG_NOSIGNAL
		constexpr int kSendFlags = MSG_NOSIGNAL;
#else
		constexpr int kSendFlags = 0;
#endif
		std::size_t sent = 0;
		while (sent < response_.size()) {
			const int chunk =
				static_cast<int>(std::min(response_.size() - sent, std::size_t{65536}));
			const SocketIoResult written =
				::send(connection, response_.data() + sent, chunk, kSendFlags);
			if (written <= 0) {
				return;
			}
			sent += static_cast<std::size_t>(written);
		}
	}

	std::string response_;
	Socket listener_{kInvalidSocket};
	std::uint16_t port_{0};
	std::atomic<bool> stopping_{false};
	std::thread thread_;
	mutable std::mutex mutex_;
	std::vector<RecordedRequest> requests_;
};

std::string http_response(int status, std::string_view body, std::string_view extra = {}) {
	return "HTTP/1.1 " + std::to_string(status) +
		   " Status\r\nContent-Length: " + std::to_string(body.size()) +
		   "\r\nConnection: close\r\n" + std::string(extra) + "\r\n" + std::string(body);
}

kalshi::ClientConfig config_for(const LoopbackServer& server) {
	kalshi::ClientConfig config;
	config.base_url = server.base_url();
	config.timeout = 2s;
	config.connect_timeout = 1s;
	return config;
}

} // namespace

TEST(HttpClientLoopback, UnsignedGetSendsIdentityAndNoCredentials) {
	const LoopbackServer server(http_response(200, R"({"ok":true})"));
	const kalshi::HttpClient client(config_for(server));

	const kalshi::Result<kalshi::HttpResponse> response = client.get("/markets?limit=1");

	ASSERT_TRUE(response.has_value()) << response.error().message;
	EXPECT_EQ(response->status_code, 200);
	EXPECT_EQ(response->body, R"({"ok":true})");
	RecordedRequest request = server.last();
	EXPECT_EQ(request.method, "GET");
	EXPECT_EQ(request.target, "/trade-api/v2/markets?limit=1");
	EXPECT_TRUE(request.headers["user-agent"].starts_with("kalshi-cpp/"));
	EXPECT_FALSE(request.headers["accept-encoding"].empty());
	EXPECT_FALSE(request.headers.contains("kalshi-access-key"));
	EXPECT_FALSE(request.headers.contains("content-type"));
}

TEST(HttpClientLoopback, SignedEmptyPostSendsAnExplicitEmptyBody) {
	const LoopbackServer server(http_response(201, R"({"subaccount_number":2})"));
	const kalshi::HttpClient client(kalshi::test::make_signer("key-id"), config_for(server));

	const kalshi::Result<kalshi::HttpResponse> response = client.post("/portfolio/subaccounts");

	ASSERT_TRUE(response.has_value()) << response.error().message;
	EXPECT_EQ(response->status_code, 201);
	RecordedRequest request = server.last();
	EXPECT_EQ(request.method, "POST");
	EXPECT_EQ(request.headers["content-length"], "0");
	EXPECT_EQ(request.headers["content-type"], "application/json");
	EXPECT_EQ(request.headers["kalshi-access-key"], "key-id");
	EXPECT_FALSE(request.headers["kalshi-access-signature"].empty());
	EXPECT_FALSE(request.headers["kalshi-access-timestamp"].empty());
	EXPECT_TRUE(request.body.empty());
}

TEST(HttpClientLoopback, SignaturesCoverTheMethodAndThePathFromTheHostRootWithoutTheQuery) {
	const LoopbackServer server(http_response(200, "{}"));
	const kalshi::HttpClient client(kalshi::test::make_signer("key-id"), config_for(server));
	const std::string& key = kalshi::test::private_key_pem();

	struct Call {
		kalshi::HttpMethod method;
		std::string endpoint;
		std::string body;
		std::string signed_path;
	};
	for (const Call& call :
		 {Call{kalshi::HttpMethod::GET, "/portfolio/orders?limit=5&status=resting", "",
			   "/trade-api/v2/portfolio/orders"},
		  Call{kalshi::HttpMethod::POST, "/portfolio/orders", R"({"ticker":"KXA"})",
			   "/trade-api/v2/portfolio/orders"},
		  Call{kalshi::HttpMethod::PUT, "/portfolio/order_groups/g-1/limit", "{}",
			   "/trade-api/v2/portfolio/order_groups/g-1/limit"},
		  Call{kalshi::HttpMethod::DEL, "/portfolio/orders/o-1?subaccount=2", "",
			   "/trade-api/v2/portfolio/orders/o-1"}}) {
		ASSERT_TRUE(client.request(call.method, call.endpoint, call.body).has_value());
		RecordedRequest request = server.last();
		const std::string method{kalshi::to_string(call.method)};
		ASSERT_EQ(request.method, method);
		// What the server saw, which is what it verifies the signature against.
		EXPECT_EQ(request.target, "/trade-api/v2" + call.endpoint);
		ASSERT_EQ(request.target.substr(0, request.target.find('?')), call.signed_path);
		const std::string& timestamp = request.headers["kalshi-access-timestamp"];
		const std::string& signature = request.headers["kalshi-access-signature"];
		EXPECT_EQ(timestamp.size(), 13U); // milliseconds
		EXPECT_TRUE(
			kalshi::test::signature_verifies(key, timestamp + method + call.signed_path, signature))
			<< method << ' ' << call.endpoint;
		// Not over the endpoint alone, the query string, or another method.
		EXPECT_FALSE(kalshi::test::signature_verifies(
			key, timestamp + method + call.endpoint.substr(0, call.endpoint.find('?')), signature));
		EXPECT_FALSE(kalshi::test::signature_verifies(
			key, timestamp + method + call.signed_path + "?", signature));
		EXPECT_FALSE(kalshi::test::signature_verifies(
			key, timestamp + (method == "GET" ? "POST" : "GET") + call.signed_path, signature));
	}
}

TEST(HttpClientLoopback, DeleteCarriesItsJsonBody) {
	const LoopbackServer server(http_response(200, "{}"));
	const kalshi::HttpClient client(config_for(server));

	const std::string body = R"({"orders":[{"order_id":"a"}]})";
	ASSERT_TRUE(client.del("/portfolio/events/orders/batched", body).has_value());

	RecordedRequest request = server.last();
	EXPECT_EQ(request.method, "DELETE");
	EXPECT_EQ(request.body, body);
}

TEST(HttpClientLoopback, ErrorStatusesComeBackAsResponsesWithHeaders) {
	const LoopbackServer server(http_response(404, R"({"error":{"code":"not_found"}})",
											  "X-Request-Id: abc123\r\nX-Blank:   \r\n"));
	const kalshi::HttpClient client(config_for(server));

	const kalshi::Result<kalshi::HttpResponse> response = client.get("/markets/NOPE");

	ASSERT_TRUE(response.has_value());
	EXPECT_EQ(response->status_code, 404);
	EXPECT_EQ(response->header("x-request-id"), "abc123");
	EXPECT_EQ(response->header("X-REQUEST-ID"), "abc123");
	EXPECT_EQ(response->header("x-blank"), "");
	EXPECT_FALSE(response->header("missing").has_value());
}

TEST(HttpClientLoopback, SlowServersTimeOutAsNetworkErrors) {
	const LoopbackServer server("");
	kalshi::ClientConfig config = config_for(server);
	config.timeout = 300ms;
	const kalshi::HttpClient client(config);

	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const kalshi::Result<kalshi::HttpResponse> response = client.get("/markets");

	ASSERT_FALSE(response.has_value());
	EXPECT_EQ(response.error().code, kalshi::ErrorCode::NetworkError);
	EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
}

TEST(HttpClientLoopback, RefusedConnectionsAreNetworkErrors) {
	kalshi::ClientConfig config;
	config.base_url = "http://127.0.0.1:1/trade-api/v2";
	config.connect_timeout = 1s;
	const kalshi::HttpClient client(config);

	const kalshi::Result<kalshi::HttpResponse> response = client.get("/markets");

	ASSERT_FALSE(response.has_value());
	EXPECT_EQ(response.error().code, kalshi::ErrorCode::NetworkError);
}

TEST(HttpClientLoopback, ResponseLimitAppliesToKnownAndChunkedBodies) {
	for (const std::string& response :
		 {http_response(200, "12345"),
		  std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: "
					  "close\r\n\r\n5\r\n12345\r\n0\r\n\r\n")}) {
		const LoopbackServer server(response);
		kalshi::ClientConfig config = config_for(server);
		config.max_response_bytes = 4;
		const kalshi::HttpClient client(config);
		const kalshi::Result<kalshi::HttpResponse> result = client.get("/markets");
		ASSERT_FALSE(result);
		// The server received the request and answered, so this is not a
		// rejection before sending, which InvalidRequest would claim.
		EXPECT_EQ(result.error().code, kalshi::ErrorCode::ParseError);
		EXPECT_EQ(server.last().method, "GET");
	}
}

TEST(HttpClientLoopback, AnOversizedReplyToAWriteSaysTheRequestWasSent) {
	const LoopbackServer server(http_response(201, R"({"order_id":"o-1"})"));
	kalshi::ClientConfig config = config_for(server);
	config.max_response_bytes = 4;
	const kalshi::HttpClient client(config);

	const kalshi::Result<kalshi::HttpResponse> result =
		client.post("/portfolio/orders", R"({"ticker":"KXA"})");

	// The order was placed. Neither "rejected before sending" nor a code that
	// a retry policy repeats may come back.
	ASSERT_FALSE(result);
	EXPECT_EQ(server.last().method, "POST");
	EXPECT_EQ(result.error().code, kalshi::ErrorCode::ParseError);
	EXPECT_NE(result.error().message.find("was sent"), std::string::npos) << result.error().message;
	kalshi::RetryPolicy retry_everything;
	retry_everything.retry_writes = true;
	EXPECT_FALSE(kalshi::should_retry(kalshi::HttpMethod::POST, result.error(), retry_everything));
	EXPECT_FALSE(kalshi::should_retry(kalshi::HttpMethod::GET, result.error(), retry_everything));
}

TEST(HttpClientLoopback, ExactResponseLimitAndUnlimitedBodiesSucceed) {
	const LoopbackServer server(http_response(200, "12345"));
	for (const std::size_t limit : {5U, 0U}) {
		kalshi::ClientConfig config = config_for(server);
		config.max_response_bytes = limit;
		const kalshi::HttpClient client(config);
		const kalshi::Result<kalshi::HttpResponse> result = client.get("/markets");
		ASSERT_TRUE(result);
		EXPECT_EQ(result->body, "12345");
	}
}

TEST(HttpClientLoopback, RejectsNonHttpProtocols) {
	kalshi::ClientConfig config;
	config.base_url = "file:///etc";
	const kalshi::HttpClient client(config);
	const kalshi::Result<kalshi::HttpResponse> result = client.get("/hosts");
	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, kalshi::ErrorCode::InvalidRequest);
}

TEST(HttpClientLoopback, HttpSchemesAreCaseInsensitive) {
	const LoopbackServer server(http_response(200, "{}"));
	for (const std::string_view scheme : {"HTTP", "hTtP"}) {
		kalshi::ClientConfig config = config_for(server);
		config.base_url.replace(0, 4, scheme);
		const kalshi::HttpClient client(config);
		const kalshi::Result<kalshi::HttpResponse> result = client.get("/markets");
		ASSERT_TRUE(result);
		EXPECT_EQ(result->status_code, 200);
	}
}

TEST(HttpClientLoopback, BaseUrlsWithAQueryOrFragmentAreRejectedBeforeSending) {
	const LoopbackServer server(http_response(200, "{}"));
	for (const std::string_view suffix : {"?x=1", "#frag", "?x=/y#z", "/?x=1", "?", "#"}) {
		kalshi::ClientConfig config = config_for(server);
		config.base_url += suffix;
		const kalshi::HttpClient client(kalshi::test::make_signer(), config);
		const kalshi::Result<kalshi::HttpResponse> result = client.get("/markets");
		ASSERT_FALSE(result) << suffix;
		EXPECT_EQ(result.error().code, kalshi::ErrorCode::InvalidRequest) << suffix;
	}
	EXPECT_TRUE(server.last().method.empty()) << "a request reached the server";
}

TEST(HttpClientLoopback, EndpointsCannotSupplyTheSchemeOrHost) {
	// Each of these joins into a valid URL for the server, but the signed path
	// would come from a base that names no host.
	const LoopbackServer server(http_response(200, "{}"));
	const std::string authority = server.base_url().substr(std::string_view{"http://"}.size());
	struct Split {
		std::string base_url;
		std::string endpoint;
	};
	for (const Split& split :
		 {Split{"", "http://" + authority + "/markets"},
		  Split{"http:", "//" + authority + "/markets"}, Split{"http://", authority + "/markets"},
		  Split{"http:///", authority + "/markets"}}) {
		kalshi::ClientConfig config = config_for(server);
		config.base_url = split.base_url;
		const kalshi::HttpClient client(kalshi::test::make_signer(), config);
		const kalshi::Result<kalshi::HttpResponse> result = client.get(split.endpoint);
		ASSERT_FALSE(result) << split.base_url;
		EXPECT_EQ(result.error().code, kalshi::ErrorCode::InvalidRequest) << split.base_url;
	}
	EXPECT_TRUE(server.last().method.empty()) << "a request reached the server";
}

TEST(HttpClientLoopback, NonPositiveTimeoutsAreRejectedInsteadOfDisablingTheTimeout) {
	struct Timeouts {
		std::chrono::milliseconds timeout;
		std::chrono::milliseconds connect_timeout;
	};
	for (const Timeouts& timeouts :
		 {Timeouts{0ms, 1s}, Timeouts{-1ms, 1s}, Timeouts{std::chrono::milliseconds::min(), 1s},
		  Timeouts{2s, 0ms}, Timeouts{2s, -1ms}}) {
		// Never answers, so only a timeout or a rejection ends the request.
		std::optional<LoopbackServer> server{std::in_place, ""};
		kalshi::ClientConfig config = config_for(*server);
		config.timeout = timeouts.timeout;
		config.connect_timeout = timeouts.connect_timeout;
		const kalshi::HttpClient client(config);

		std::future<kalshi::Result<kalshi::HttpResponse>> pending =
			std::async(std::launch::async, [&client] { return client.get("/markets"); });
		EXPECT_EQ(pending.wait_for(3s), std::future_status::ready)
			<< "still in flight with timeout " << timeouts.timeout.count()
			<< " ms and connect_timeout " << timeouts.connect_timeout.count() << " ms";
		const bool sent = !server->last().method.empty();
		server.reset(); // closes the connection, which ends a request that has no timeout
		const kalshi::Result<kalshi::HttpResponse> result = pending.get();

		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().code, kalshi::ErrorCode::InvalidRequest) << result.error().message;
		EXPECT_FALSE(sent) << "the request was sent";
	}
}
