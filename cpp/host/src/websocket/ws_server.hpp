#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace beast = boost::beast;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

// The return value is consulted only for the first client application frame:
// true marks the session Hello-ready, false closes the socket. Later message
// results remain application-level decisions and do not tear down a ready
// transport session.
using BinaryMessageCallback =
    std::function<bool(std::uint64_t, const std::string&)>;
using ConnectMessageFactory = std::function<std::string()>;

// 단일 WebSocket 연결 세션
class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    static constexpr std::size_t kMaxIncomingMessageBytes = 64 * 1024;
    static constexpr auto kCloseGracePeriod = std::chrono::milliseconds(250);

    WsSession(tcp::socket socket, std::uint64_t connection_generation,
              BinaryMessageCallback on_msg,
              ConnectMessageFactory on_connect);
    void start();
    void send_binary(std::shared_ptr<const std::string> message);
    void close(const std::string& reason);

private:
    void send_initial_binary(std::shared_ptr<const std::string> message);
    void do_read();
    void do_write();
    void do_close();
    void arm_close_deadline();
    void cancel_close_deadline();
    void force_close();

    beast::websocket::stream<tcp::socket> ws_;
    net::steady_timer                     close_timer_;
    beast::flat_buffer                    buf_;
    std::uint64_t                         connection_generation_ = 0;
    BinaryMessageCallback                 on_message_;
    ConnectMessageFactory                 on_connect_;
    std::deque<std::shared_ptr<const std::string>> write_queue_;
    bool                                  handshake_complete_ = false;
    bool                                  application_ready_ = false;
    bool                                  write_in_progress_ = false;
    bool                                  close_requested_ = false;
    bool                                  closed_ = false;
    std::string                           close_reason_;
};

// WebSocket 서버 (Unreal binary control 수신 + state broadcast)
class WsServer {
public:
    WsServer(net::io_context& ioc, unsigned short port,
             BinaryMessageCallback on_msg, ConnectMessageFactory on_connect);
    ~WsServer();

    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;
    WsServer(WsServer&&) = delete;
    WsServer& operator=(WsServer&&) = delete;

    void start();
    void stop(const std::string& reason = "server shutting down");
    void broadcast_binary(const std::string& message);
    void close_all(const std::string& reason);
    unsigned short port() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};
