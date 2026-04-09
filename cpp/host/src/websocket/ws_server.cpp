#include "websocket/ws_server.hpp"

#include <iostream>

namespace beast = boost::beast;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

// --- WsSession ---

WsSession::WsSession(tcp::socket socket, InputCallback on_msg, ConnectCallback on_connect)
    : ws_(std::move(socket))
    , on_message_(std::move(on_msg))
    , on_connect_(std::move(on_connect))
{}

void WsSession::start() {
    ws_.async_accept([self = shared_from_this()](beast::error_code ec) {
        if (ec) {
            std::cerr << "[WS] accept error: " << ec.message() << "\n";
            return;
        }
        std::cout << "[WS] Unreal connected\n";

        // 접속 즉시 초기 위치 전송 (Unreal 스폰용)
        auto init_msg = self->on_connect_();
        self->do_write_init(init_msg);
    });
}

void WsSession::do_write_init(const std::string& msg) {
    auto buf = std::make_shared<std::string>(msg);
    ws_.async_write(net::buffer(*buf),
        [self = shared_from_this(), buf](beast::error_code ec, std::size_t) {
            if (ec) {
                std::cerr << "[WS] write error: " << ec.message() << "\n";
                return;
            }
            self->do_read();
        });
}

void WsSession::do_read() {
    ws_.async_read(buf_,
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (ec) {
                std::cout << "[WS] Unreal disconnected\n";
                return;
            }
            auto msg = beast::buffers_to_string(self->buf_.data());
            self->buf_.consume(self->buf_.size());
            self->on_message_(msg);
            self->do_read();
        });
}

// --- WsServer ---

WsServer::WsServer(net::io_context& ioc, unsigned short port,
                   InputCallback on_msg, ConnectCallback on_connect)
    : ioc_(ioc)
    , acceptor_(ioc, tcp::endpoint(tcp::v4(), port))
    , on_message_(std::move(on_msg))
    , on_connect_(std::move(on_connect))
{}

void WsServer::start() {
    do_accept();
}

void WsServer::do_accept() {
    acceptor_.async_accept(
        [this](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<WsSession>(
                    std::move(socket), on_message_, on_connect_
                )->start();
            }
            do_accept(); // 다음 연결 대기
        });
}
