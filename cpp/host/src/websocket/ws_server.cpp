#include "websocket/ws_server.hpp"

#include <algorithm>
#include <iostream>

namespace beast = boost::beast;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

// --- WsSession ---

WsSession::WsSession(tcp::socket socket, BinaryMessageCallback on_msg,
                     ConnectMessageFactory on_connect)
    : ws_(std::move(socket))
    , on_message_(std::move(on_msg))
    , on_connect_(std::move(on_connect))
{}

void WsSession::start() {
    beast::error_code socket_ec;
    ws_.next_layer().set_option(tcp::no_delay(true), socket_ec);
    if (socket_ec) {
        std::cerr << "[WS] TCP_NODELAY failed: " << socket_ec.message() << "\n";
    }

    ws_.async_accept([self = shared_from_this()](beast::error_code ec) {
        if (ec) {
            std::cerr << "[WS] accept error: " << ec.message() << "\n";
            return;
        }
        std::cout << "[WS] Unreal connected\n";

        self->ws_.binary(true);
        self->do_read();

        // 접속 즉시 현재 authoritative state 전송
        auto initial_message = self->on_connect_();
        if (!initial_message.empty()) {
            self->send_binary(std::make_shared<const std::string>(
                std::move(initial_message)));
        }
    });
}

void WsSession::send_binary(std::shared_ptr<const std::string> message) {
    const bool write_in_progress = !write_queue_.empty();

    // State broadcast is latest-wins; keep the write in progress and one pending update.
    if (write_in_progress && write_queue_.size() >= 2) {
        write_queue_.back() = std::move(message);
        return;
    }

    write_queue_.push_back(std::move(message));
    if (!write_in_progress) {
        do_write();
    }
}

// 수신 대기
void WsSession::do_read() {
    ws_.async_read(buf_,
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (ec) {
                std::cout << "[WS] Unreal disconnected\n";
                return;
            }

            if (!self->ws_.got_binary()) {
                self->buf_.consume(self->buf_.size());
                std::cerr << "[WS] Ignored non-binary frame\n";
                self->do_read();
                return;
            }

            auto msg = beast::buffers_to_string(self->buf_.data());
            self->buf_.consume(self->buf_.size());
            self->on_message_(msg);
            self->do_read();
        });
}

void WsSession::do_write() {
    if (write_queue_.empty()) {
        return;
    }

    ws_.binary(true);
    auto message = write_queue_.front();
    ws_.async_write(net::buffer(*message),
        [self = shared_from_this(), message](beast::error_code ec, std::size_t) {
            if (ec) {
                std::cerr << "[WS] write error: " << ec.message() << "\n";
                self->write_queue_.clear();
                return;
            }

            self->write_queue_.pop_front();
            if (!self->write_queue_.empty()) {
                self->do_write();
            }
        });
}

// --- WsServer ---

WsServer::WsServer(net::io_context& ioc, unsigned short port,
                   BinaryMessageCallback on_msg, ConnectMessageFactory on_connect)
    : ioc_(ioc)
    , acceptor_(ioc, tcp::endpoint(tcp::v4(), port))
    , on_message_(std::move(on_msg))
    , on_connect_(std::move(on_connect))
{}

void WsServer::start() {
    do_accept();
}

void WsServer::broadcast_binary(const std::string& message) {
    prune_sessions();
    auto shared_message = std::make_shared<const std::string>(message);
    for (auto& weak_session : sessions_) {
        if (auto session = weak_session.lock()) {
            session->send_binary(shared_message);
        }
    }
}

// 연결 대기
void WsServer::do_accept() {
    acceptor_.async_accept(
        [this](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                auto session = std::make_shared<WsSession>(
                    std::move(socket), on_message_, on_connect_
                );
                sessions_.push_back(session);
                session->start();
            }
            do_accept(); // 다음 연결 대기
        });
}

void WsServer::prune_sessions() {
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
            [](const std::weak_ptr<WsSession>& session) {
                return session.expired();
            }),
        sessions_.end());
}
