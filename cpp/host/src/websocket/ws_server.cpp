#include "websocket/ws_server.hpp"

#include <algorithm>
#include <iostream>

namespace beast = boost::beast;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

// --- WsSession ---

WsSession::WsSession(tcp::socket socket,
                     std::uint64_t connection_generation,
                     BinaryMessageCallback on_msg,
                     ConnectMessageFactory on_connect)
    : ws_(std::move(socket))
    , close_timer_(ws_.get_executor())
    , connection_generation_(connection_generation)
    , on_message_(std::move(on_msg))
    , on_connect_(std::move(on_connect))
{}

void WsSession::start() {
    beast::error_code socket_ec;
    ws_.next_layer().set_option(tcp::no_delay(true), socket_ec);
    if (socket_ec) {
        std::cerr << "[WS] TCP_NODELAY failed: " << socket_ec.message() << "\n";
    }

    ws_.read_message_max(kMaxIncomingMessageBytes);
    ws_.async_accept([self = shared_from_this()](beast::error_code ec) {
        if (ec) {
            if (!self->close_requested_ && ec != net::error::operation_aborted) {
                std::cerr << "[WS] handshake error: " << ec.message() << "\n";
            }
            self->closed_ = true;
            self->write_queue_.clear();
            return;
        }
        std::cout << "[WS] Unreal connected\n";

        self->handshake_complete_ = true;
        self->ws_.binary(true);
        if (self->close_requested_) {
            self->write_queue_.clear();
            self->do_close();
            return;
        }
        self->do_read();

        // 접속 직전 쌓인 상태보다 accept 완료 시점의 authoritative state가
        // 더 최신이다. HTTP 101 응답이 완료된 뒤 첫 binary frame을 보낸다.
        self->write_queue_.clear();
        auto initial_message = self->on_connect_();
        if (!initial_message.empty()) {
            self->send_initial_binary(std::make_shared<const std::string>(
                std::move(initial_message)));
        }
    });
}

void WsSession::send_binary(std::shared_ptr<const std::string> message) {
    if (closed_ || close_requested_ || !application_ready_) {
        return;
    }

    send_initial_binary(std::move(message));
}

void WsSession::send_initial_binary(
    std::shared_ptr<const std::string> message) {
    if (closed_ || close_requested_) {
        return;
    }

    // State broadcast is latest-wins; keep the write in progress and one pending update.
    if (write_in_progress_ && write_queue_.size() >= 2) {
        write_queue_.back() = std::move(message);
        return;
    }

    // Before async_accept completes there must be no WebSocket writes. Beast's
    // accept operation owns the HTTP 101 response; writing a frame concurrently
    // can put binary bytes before that response and corrupt the handshake.
    if (!handshake_complete_) {
        if (write_queue_.empty()) {
            write_queue_.push_back(std::move(message));
        } else {
            write_queue_.back() = std::move(message);
        }
        return;
    }

    write_queue_.push_back(std::move(message));
    if (!write_in_progress_) {
        do_write();
    }
}

void WsSession::close(const std::string& reason) {
    if (closed_ || close_requested_) {
        return;
    }
    close_requested_ = true;
    close_reason_ = reason;
    if (!handshake_complete_) {
        // A WebSocket close frame is illegal before the HTTP upgrade has
        // completed. Retire the underlying TCP connection instead; the
        // pending async_accept handler owns this session until cancellation is
        // delivered.
        beast::error_code ignored;
        ws_.next_layer().cancel(ignored);
        ws_.next_layer().shutdown(tcp::socket::shutdown_both, ignored);
        ws_.next_layer().close(ignored);
        closed_ = true;
        write_queue_.clear();
        return;
    }
    arm_close_deadline();
    if (write_in_progress_) {
        // The front message owns the buffer used by async_write and must stay
        // alive until its completion handler runs. Only discard pending state.
        while (write_queue_.size() > 1) {
            write_queue_.pop_back();
        }
    } else {
        write_queue_.clear();
        do_close();
    }
}

// 수신 대기
void WsSession::do_read() {
    ws_.async_read(buf_,
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (ec) {
                std::cout << "[WS] Unreal disconnected\n";
                self->cancel_close_deadline();
                self->force_close();
                return;
            }

            if (!self->ws_.got_binary()) {
                self->buf_.consume(self->buf_.size());
                if (!self->application_ready_) {
                    std::cerr << "[WS] Rejected non-binary frame before client Hello\n";
                    self->close("client application Hello rejected");
                } else {
                    std::cerr << "[WS] Ignored non-binary frame\n";
                    self->do_read();
                }
                return;
            }

            auto msg = beast::buffers_to_string(self->buf_.data());
            self->buf_.consume(self->buf_.size());
            if (self->close_requested_) {
                return;
            }
            const bool first_application_message = !self->application_ready_;
            const bool accepted_as_hello =
                self->on_message_(self->connection_generation_, msg);
            if (first_application_message) {
                if (!accepted_as_hello) {
                    std::cerr << "[WS] Client application Hello rejected\n";
                    self->close("client application Hello rejected");
                    return;
                }
                self->application_ready_ = true;
                std::cout << "[WS] Client application Hello accepted\n";
            }
            self->do_read();
        });
}

void WsSession::do_write() {
    if (closed_ || !handshake_complete_ || write_in_progress_ || write_queue_.empty()) {
        return;
    }

    write_in_progress_ = true;
    ws_.binary(true);
    auto message = write_queue_.front();
    ws_.async_write(net::buffer(*message),
        [self = shared_from_this(), message](beast::error_code ec, std::size_t) {
            self->write_in_progress_ = false;
            if (ec) {
                if (!self->closed_ && ec != net::error::operation_aborted) {
                    std::cerr << "[WS] write error: " << ec.message() << "\n";
                }
                self->cancel_close_deadline();
                self->force_close();
                self->write_queue_.clear();
                return;
            }

            self->write_queue_.pop_front();
            if (self->close_requested_) {
                self->write_queue_.clear();
                self->do_close();
                return;
            }
            if (!self->write_queue_.empty()) {
                self->do_write();
            }
        });
}

void WsSession::do_close() {
    if (closed_ || !handshake_complete_ || write_in_progress_) {
        return;
    }

    beast::websocket::close_reason reason;
    reason.code = beast::websocket::close_code::policy_error;
    reason.reason = close_reason_;
    ws_.async_close(reason, [self = shared_from_this()](beast::error_code ec) {
        if (ec && ec != net::error::operation_aborted) {
            std::cerr << "[WS] close error: " << ec.message() << "\n";
        }
        self->closed_ = true;
        self->cancel_close_deadline();
        self->write_queue_.clear();
    });
}

void WsSession::arm_close_deadline() {
    close_timer_.expires_after(kCloseGracePeriod);
    close_timer_.async_wait(
        [self = shared_from_this()](beast::error_code ec) {
            if (ec == net::error::operation_aborted || self->closed_) {
                return;
            }
            if (ec) {
                std::cerr << "[WS] close deadline timer error: "
                          << ec.message() << "\n";
            } else {
                std::cerr << "[WS] close deadline exceeded; forcing TCP shutdown\n";
            }
            self->force_close();
        });
}

void WsSession::cancel_close_deadline() {
    try {
        static_cast<void>(close_timer_.cancel());
    } catch (const boost::system::system_error& error) {
        std::cerr << "[WS] close deadline cancellation failed: "
                  << error.what() << "\n";
    }
}

void WsSession::force_close() {
    if (closed_) {
        return;
    }

    closed_ = true;
    // An already-completed async_write may still have its success handler
    // queued behind this timer. Preserve its front entry so that handler can
    // pop it safely; its captured shared_ptr keeps the payload bytes alive.
    if (write_in_progress_) {
        while (write_queue_.size() > 1) {
            write_queue_.pop_back();
        }
    } else {
        write_queue_.clear();
    }

    beast::error_code ignored;
    ws_.next_layer().cancel(ignored);
    ws_.next_layer().shutdown(tcp::socket::shutdown_both, ignored);
    ws_.next_layer().close(ignored);
}

// --- WsServer ---

struct WsServer::State : public std::enable_shared_from_this<WsServer::State> {
    State(net::io_context& ioc, unsigned short port,
          BinaryMessageCallback on_msg, ConnectMessageFactory on_connect)
        : acceptor(ioc)
        , on_message(std::move(on_msg))
        , connect_message(std::move(on_connect))
    {
        // Local simulation is the safe default. Exposing an unauthenticated
        // control socket to the LAN must be a separate, explicit feature.
        const tcp::endpoint endpoint(net::ip::address_v4::loopback(), port);
        acceptor.open(endpoint.protocol());
#ifdef _WIN32
        // Windows SO_REUSEADDR semantics can allow two processes to listen on
        // the same address and distribute incoming Unreal connections between
        // independent physics worlds. Require exclusive ownership so a second
        // SimCore fails at startup instead of creating nondeterministic state.
        const BOOL exclusive_address_use = TRUE;
        if (::setsockopt(
                acceptor.native_handle(),
                SOL_SOCKET,
                SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&exclusive_address_use),
                sizeof(exclusive_address_use)) == SOCKET_ERROR) {
            throw boost::system::system_error(
                boost::system::error_code(
                    WSAGetLastError(), boost::system::system_category()),
                "SO_EXCLUSIVEADDRUSE");
        }
#endif
        acceptor.bind(endpoint);
        acceptor.listen(net::socket_base::max_listen_connections);
    }

    void start()
    {
        if (started || stopping) {
            return;
        }
        started = true;
        do_accept();
    }

    void stop(const std::string& reason)
    {
        if (stopping) {
            return;
        }
        stopping = true;

        beast::error_code ignored;
        acceptor.cancel(ignored);
        acceptor.close(ignored);
        close_all(reason);
    }

    void broadcast_binary(const std::string& message)
    {
        prune_sessions();
        auto shared_message = std::make_shared<const std::string>(message);
        for (auto& weak_session : sessions) {
            if (auto session = weak_session.lock()) {
                session->send_binary(shared_message);
            }
        }
    }

    void close_all(const std::string& reason)
    {
        prune_sessions();
        for (auto& weak_session : sessions) {
            if (auto session = weak_session.lock()) {
                session->close(reason);
            }
        }
    }

    void do_accept()
    {
        acceptor.async_accept(
            [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
                if (ec) {
                    if (!self->stopping && ec != net::error::operation_aborted) {
                        std::cerr << "[WS] socket accept error: " << ec.message() << "\n";
                    }
                    return;
                }
                if (self->stopping) {
                    beast::error_code ignored;
                    socket.close(ignored);
                    return;
                }

                const std::uint64_t connection_generation =
                    self->next_connection_generation++;
                auto session = std::make_shared<WsSession>(
                    std::move(socket), connection_generation,
                    self->on_message, self->connect_message);
                self->sessions.push_back(session);
                session->start();
                self->do_accept();
            });
    }

    void prune_sessions()
    {
        sessions.erase(
            std::remove_if(sessions.begin(), sessions.end(),
                [](const std::weak_ptr<WsSession>& session) {
                    return session.expired();
                }),
            sessions.end());
    }

    tcp::acceptor acceptor;
    BinaryMessageCallback on_message;
    ConnectMessageFactory connect_message;
    std::vector<std::weak_ptr<WsSession>> sessions;
    std::uint64_t next_connection_generation = 1;
    bool started = false;
    bool stopping = false;
};

WsServer::WsServer(net::io_context& ioc, unsigned short port,
                   BinaryMessageCallback on_msg, ConnectMessageFactory on_connect)
    : state_(std::make_shared<State>(
          ioc, port, std::move(on_msg), std::move(on_connect)))
{}

WsServer::~WsServer()
{
    stop();
}

void WsServer::start()
{
    state_->start();
}

void WsServer::stop(const std::string& reason)
{
    state_->stop(reason);
}

void WsServer::broadcast_binary(const std::string& message)
{
    state_->broadcast_binary(message);
}

void WsServer::close_all(const std::string& reason)
{
    state_->close_all(reason);
}

unsigned short WsServer::port() const
{
    return state_->acceptor.local_endpoint().port();
}
