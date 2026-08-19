#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace beast = boost::beast;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

using BinaryMessageCallback = std::function<void(const std::string&)>;
using ConnectMessageFactory = std::function<std::string()>;

// 단일 WebSocket 연결 세션
class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    WsSession(tcp::socket socket, BinaryMessageCallback on_msg,
              ConnectMessageFactory on_connect);
    void start();
    void send_binary(std::shared_ptr<const std::string> message);

private:
    void do_read();
    void do_write();

    beast::websocket::stream<tcp::socket> ws_;
    beast::flat_buffer                    buf_;
    BinaryMessageCallback                 on_message_;
    ConnectMessageFactory                 on_connect_;
    std::deque<std::shared_ptr<const std::string>> write_queue_;
    bool                                  handshake_complete_ = false;
    bool                                  write_in_progress_ = false;
    bool                                  closed_ = false;
};

// WebSocket 서버 (Unreal binary control 수신 + state broadcast)
class WsServer {
public:
    WsServer(net::io_context& ioc, unsigned short port,
             BinaryMessageCallback on_msg, ConnectMessageFactory on_connect);

    void start();
    void broadcast_binary(const std::string& message);
    unsigned short port() const;

private:
    void do_accept();
    void prune_sessions();

    net::io_context&                  ioc_;
    tcp::acceptor                     acceptor_;
    BinaryMessageCallback             on_message_;
    ConnectMessageFactory             on_connect_;
    std::vector<std::weak_ptr<WsSession>> sessions_;
};
