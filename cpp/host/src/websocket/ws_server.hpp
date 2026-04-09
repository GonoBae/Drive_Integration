#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <functional>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

using InputCallback   = std::function<void(const std::string&)>;
using ConnectCallback = std::function<std::string()>; // 접속 시 초기 상태 JSON 반환

// 단일 WebSocket 연결 세션
class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    WsSession(tcp::socket socket, InputCallback on_msg, ConnectCallback on_connect);
    void start();

private:
    void do_write_init(const std::string& msg);
    void do_read();

    beast::websocket::stream<tcp::socket> ws_;
    beast::flat_buffer                    buf_;
    InputCallback                         on_message_;
    ConnectCallback                       on_connect_;
};

// WebSocket 서버 (Unreal 입력 수신)
class WsServer {
public:
    WsServer(net::io_context& ioc, unsigned short port,
             InputCallback on_msg, ConnectCallback on_connect);

    void start();

private:
    void do_accept();

    net::io_context& ioc_;
    tcp::acceptor    acceptor_;
    InputCallback    on_message_;
    ConnectCallback  on_connect_;
};
