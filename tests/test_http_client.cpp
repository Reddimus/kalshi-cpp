// Runs the real libcurl transport against a loopback HTTP server. POSIX only.

#include "kalshi/http_client.hpp"

#include <gtest/gtest.h>

#ifndef _WIN32

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "test_signer_fixture.hpp"

namespace {

using namespace std::chrono_literals;

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
		listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		if (listener_ < 0 ||
			::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
			::listen(listener_, 8) != 0) {
			return;
		}
		socklen_t length = sizeof(address);
		::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
		port_ = ntohs(address.sin_port);
		thread_ = std::thread([this] { serve(); });
	}

	~LoopbackServer() {
		stopping_ = true;
		::shutdown(listener_, SHUT_RDWR);
		::close(listener_);
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
			const int connection = ::accept(listener_, nullptr, nullptr);
			if (connection < 0) {
				return;
			}
			handle(connection);
			::close(connection);
		}
	}

	void handle(int connection) {
		std::string data;
		std::array<char, 4096> buffer{};
		std::size_t header_end = std::string::npos;
		while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
			const ssize_t received = ::recv(connection, buffer.data(), buffer.size(), 0);
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
			const ssize_t received = ::recv(connection, buffer.data(), buffer.size(), 0);
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
		::send(connection, response_.data(), response_.size(), 0);
	}

	std::string response_;
	int listener_{-1};
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

#else

TEST(HttpClientLoopback, SkippedOnWindows) {
	GTEST_SKIP() << "The loopback server uses POSIX sockets";
}

#endif
