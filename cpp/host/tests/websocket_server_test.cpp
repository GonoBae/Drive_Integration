#include "websocket/ws_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core/tcp_stream.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace websocket = boost::beast::websocket;

namespace {

class IoContextRunner final {
public:
    explicit IoContextRunner(net::io_context& ioc)
        : ioc_(ioc)
        , thread_error_()
        , thread_([this] {
            try {
                ioc_.run();
            } catch (...) {
                thread_error_ = std::current_exception();
            }
        })
    {
    }

    ~IoContextRunner()
    {
        stop_and_join_noexcept();
    }

    IoContextRunner(const IoContextRunner&) = delete;
    IoContextRunner& operator=(const IoContextRunner&) = delete;

    void stop_and_join()
    {
        stop_and_join_noexcept();
        if (thread_error_) {
            std::rethrow_exception(thread_error_);
        }
    }

private:
    void stop_and_join_noexcept() noexcept
    {
        ioc_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    net::io_context& ioc_;
    std::exception_ptr thread_error_;
    std::thread thread_;
};

using TestWebSocket = websocket::stream<boost::beast::tcp_stream>;

struct BoundedReadResult {
    boost::system::error_code error;
    std::size_t bytes_transferred = 0;
};

BoundedReadResult read_frame_with_timeout(
    net::io_context& ioc,
    TestWebSocket& client,
    boost::beast::flat_buffer& buffer,
    std::chrono::milliseconds timeout = std::chrono::seconds(2))
{
    ioc.restart();
    net::steady_timer timer(ioc);
    timer.expires_after(timeout);

    BoundedReadResult result;
    bool read_finished = false;
    bool timed_out = false;
    client.async_read(
        buffer,
        [&](boost::system::error_code ec, std::size_t bytes_transferred) {
            result.error = ec;
            result.bytes_transferred = bytes_transferred;
            read_finished = true;
            static_cast<void>(timer.cancel());
        });
    timer.async_wait([&](boost::system::error_code ec) {
        if (ec == net::error::operation_aborted || read_finished) {
            return;
        }
        timed_out = true;
        // Blocking tcp_stream operations ignore expires_after(). Cancel and
        // close the test socket so the composed async_read is guaranteed to
        // complete before this helper returns.
        boost::system::error_code ignored;
        auto& socket = boost::beast::get_lowest_layer(client).socket();
        socket.cancel(ignored);
        socket.shutdown(tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
    });
    ioc.run();

    if (timed_out) {
        throw std::runtime_error("timed out waiting for WebSocket frame");
    }
    if (!read_finished) {
        throw std::runtime_error("WebSocket read did not complete");
    }
    return result;
}

std::size_t drain_raw_until_tcp_closed(
    TestWebSocket& client,
    std::chrono::milliseconds timeout = std::chrono::seconds(2))
{
    auto& socket = boost::beast::get_lowest_layer(client).socket();
    boost::system::error_code ec;
    socket.non_blocking(true, ec);
    if (ec) {
        throw std::runtime_error(
            "could not make controlled client non-blocking: " + ec.message());
    }

    std::size_t total_bytes = 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        char bytes[16 * 1024];
        const std::size_t count = socket.read_some(net::buffer(bytes), ec);
        if (!ec) {
            total_bytes += count;
            continue;
        }
        if (ec == net::error::eof
            || ec == net::error::connection_reset
            || ec == net::error::operation_aborted
            || ec == net::error::bad_descriptor) {
            return total_bytes;
        }
        if (ec != net::error::would_block && ec != net::error::try_again) {
            throw std::runtime_error(
                "unexpected controlled-client read error: " + ec.message());
        }
        ec.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error(
        "server did not force-close the controlled client before the deadline");
}

} // namespace

void test_second_server_cannot_bind_the_same_port()
{
    net::io_context first_ioc;
    WsServer first(first_ioc, 0, [](std::uint64_t, const std::string&) {
        return true;
    }, [] {
        return std::string();
    });

    bool rejected = false;
    try {
        net::io_context second_ioc;
        WsServer second(second_ioc, first.port(), [](std::uint64_t, const std::string&) {
            return true;
        }, [] {
            return std::string();
        });
    } catch (const boost::system::system_error&) {
        rejected = true;
    }
    if (!rejected) {
        throw std::runtime_error(
            "a second WebSocket server bound the active SimCore port");
    }
}

void test_close_before_handshake_closes_tcp_connection()
{
    net::io_context server_ioc;
    WsServer server(server_ioc, 0, [](std::uint64_t, const std::string&) {
        return true;
    }, [] {
        return std::string("initial-state");
    });
    server.start();

    net::io_context client_ioc;
    tcp::socket client(client_ioc);
    client.connect({net::ip::make_address("127.0.0.1"), server.port()});

    // Wait until the TCP accept handler has actually run. A single poll_one()
    // can return zero on macOS even after connect() succeeds; that also leaves
    // io_context in the stopped state and turns this test into a timing race.
    // Once this sole initial handler runs, WsSession is waiting for the HTTP
    // upgrade and close_all exercises the intended pre-handshake path.
    bool accept_completed = false;
    const auto accept_timeout =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!accept_completed && std::chrono::steady_clock::now() < accept_timeout) {
        server_ioc.restart();
        accept_completed = server_ioc.poll_one() == 1;
        if (!accept_completed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (!accept_completed) {
        throw std::runtime_error("server did not accept pre-handshake test connection");
    }
    server.close_all("server stopping before handshake");
    client.non_blocking(true);

    bool connection_closed = false;
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!connection_closed && std::chrono::steady_clock::now() < timeout) {
        server_ioc.poll();
        char byte = 0;
        boost::system::error_code ec;
        client.read_some(net::buffer(&byte, 1), ec);
        connection_closed = ec == net::error::eof
            || ec == net::error::connection_reset
            || ec == net::error::operation_aborted;
        if (ec && !connection_closed && ec != net::error::would_block
               && ec != net::error::try_again) {
            throw std::runtime_error("unexpected pre-handshake read error: " + ec.message());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    server.stop();
    if (!connection_closed) {
        throw std::runtime_error("pre-handshake connection remained open after close_all");
    }
}

void test_server_destruction_safely_cancels_pending_accept()
{
    net::io_context ioc;
    {
        WsServer server(ioc, 0, [](std::uint64_t, const std::string&) {
            return true;
        }, [] {
            return std::string();
        });
        server.start();
    }

    // The accept completion retains the internal server state, not a dangling
    // pointer to the destroyed public wrapper.
    ioc.run();
}

void test_oversized_message_is_not_delivered_to_application()
{
    std::atomic<int> delivered_messages{0};
    net::io_context server_ioc;
    WsServer server(server_ioc, 0, [&](std::uint64_t, const std::string&) {
        ++delivered_messages;
        return true;
    }, [] {
        return std::string("initial-state");
    });
    server.start();
    IoContextRunner server_runner(server_ioc);

    net::io_context client_ioc;
    websocket::stream<boost::beast::tcp_stream> client(client_ioc);
    boost::beast::get_lowest_layer(client).connect(
        {net::ip::make_address("127.0.0.1"), server.port()});
    client.handshake("127.0.0.1", "/simcore");

    boost::beast::flat_buffer initial_state;
    const auto initial_read = read_frame_with_timeout(
        client_ioc, client, initial_state);
    if (initial_read.error) {
        throw std::runtime_error(
            "oversized-message test initial read failed: "
            + initial_read.error.message());
    }
    client.binary(true);
    const std::string oversized(WsSession::kMaxIncomingMessageBytes + 1, 'x');
    boost::system::error_code write_error;
    client.write(net::buffer(oversized), write_error);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    server_runner.stop_and_join();
    if (delivered_messages.load() != 0) {
        throw std::runtime_error("oversized WebSocket message reached application callback");
    }
}

void test_broadcast_waits_for_accepted_client_hello()
{
    net::io_context server_ioc;
    std::promise<void> hello_seen_promise;
    auto hello_seen = hello_seen_promise.get_future();
    std::promise<void> rejected_control_seen_promise;
    auto rejected_control_seen = rejected_control_seen_promise.get_future();
    std::atomic<int> delivered_messages{0};
    WsServer server(
        server_ioc,
        0,
        [&](std::uint64_t, const std::string& message) {
            const int delivery_index = ++delivered_messages;
            if (delivery_index == 1 && message == "client-hello") {
                hello_seen_promise.set_value();
                return true;
            }
            if (delivery_index == 2 && message == "rejected-control") {
                rejected_control_seen_promise.set_value();
            }
            return false;
        },
        [] {
            return std::string("server-hello");
        });
    server.start();
    IoContextRunner server_runner(server_ioc);

    net::io_context client_ioc;
    websocket::stream<boost::beast::tcp_stream> client(client_ioc);
    boost::beast::get_lowest_layer(client).connect(
        {net::ip::make_address("127.0.0.1"), server.port()});
    client.handshake("127.0.0.1", "/simcore");

    boost::beast::flat_buffer initial_message;
    const auto initial_read = read_frame_with_timeout(
        client_ioc, client, initial_message);
    if (initial_read.error) {
        throw std::runtime_error(
            "readiness test initial read failed: "
            + initial_read.error.message());
    }
    if (boost::beast::buffers_to_string(initial_message.data()) != "server-hello") {
        throw std::runtime_error(
            "the readiness exception did not deliver the initial server Hello");
    }

    std::promise<void> blocked_broadcast_done_promise;
    auto blocked_broadcast_done = blocked_broadcast_done_promise.get_future();
    net::post(server_ioc, [&] {
        server.broadcast_binary("blocked-before-client-hello");
        blocked_broadcast_done_promise.set_value();
    });
    if (blocked_broadcast_done.wait_for(std::chrono::seconds(2))
        != std::future_status::ready) {
        throw std::runtime_error("pre-Hello broadcast was not exercised");
    }

    client.binary(true);
    client.write(net::buffer(std::string("client-hello")));
    if (hello_seen.wait_for(std::chrono::seconds(2))
        != std::future_status::ready) {
        throw std::runtime_error("accepted client Hello did not reach the server");
    }

    // Once readiness is established, an ordinary application-level rejection
    // must not close the transport. The next state should still be delivered.
    client.write(net::buffer(std::string("rejected-control")));
    if (rejected_control_seen.wait_for(std::chrono::seconds(2))
        != std::future_status::ready) {
        throw std::runtime_error("post-Hello rejected control was not processed");
    }

    std::promise<void> ready_broadcast_done_promise;
    auto ready_broadcast_done = ready_broadcast_done_promise.get_future();
    net::post(server_ioc, [&] {
        server.broadcast_binary("state-after-client-hello");
        ready_broadcast_done_promise.set_value();
    });
    if (ready_broadcast_done.wait_for(std::chrono::seconds(2))
        != std::future_status::ready) {
        throw std::runtime_error("ready-state broadcast was not exercised");
    }

    boost::beast::flat_buffer ready_state;
    const auto ready_read = read_frame_with_timeout(
        client_ioc, client, ready_state);
    if (ready_read.error) {
        throw std::runtime_error(
            "ready socket closed after an ordinary rejected payload: "
            + ready_read.error.message());
    }
    const auto ready_bytes = boost::beast::buffers_to_string(ready_state.data());
    if (ready_bytes != "state-after-client-hello") {
        throw std::runtime_error(
            "a pre-Hello WorldState escaped the readiness gate");
    }

    server_runner.stop_and_join();
}

void test_rejected_first_application_message_closes_socket()
{
    net::io_context server_ioc;
    std::atomic<int> delivered_messages{0};
    WsServer server(
        server_ioc,
        0,
        [&](std::uint64_t, const std::string&) {
            ++delivered_messages;
            return false;
        },
        [] {
            return std::string("server-hello");
        });
    server.start();
    IoContextRunner server_runner(server_ioc);

    net::io_context client_ioc;
    websocket::stream<boost::beast::tcp_stream> client(client_ioc);
    boost::beast::get_lowest_layer(client).connect(
        {net::ip::make_address("127.0.0.1"), server.port()});
    client.handshake("127.0.0.1", "/simcore");

    boost::beast::flat_buffer initial_message;
    const auto initial_read = read_frame_with_timeout(
        client_ioc, client, initial_message);
    if (initial_read.error) {
        throw std::runtime_error(
            "first-rejection test initial read failed: "
            + initial_read.error.message());
    }
    client.binary(true);
    client.write(net::buffer(std::string("not-a-client-hello")));

    boost::beast::flat_buffer close_frame;
    const auto close_read = read_frame_with_timeout(
        client_ioc, client, close_frame);

    server_runner.stop_and_join();
    if (delivered_messages.load() != 1) {
        throw std::runtime_error(
            "the rejected first application frame was not handled exactly once");
    }
    if (close_read.error != websocket::error::closed
        || client.reason().code != websocket::close_code::policy_error) {
        throw std::runtime_error(
            "a rejected first application frame did not close the socket");
    }
}

void test_forced_close_cancels_stalled_write()
{
    constexpr std::size_t kPayloadBytes = 16 * 1024 * 1024;

    net::io_context server_ioc;
    std::promise<void> handshake_complete_promise;
    auto handshake_complete = handshake_complete_promise.get_future();
    std::promise<void> close_requested_promise;
    auto close_requested = close_requested_promise.get_future();
    WsServer server(
        server_ioc,
        0,
        [](std::uint64_t, const std::string&) {
            return true;
        },
        [&handshake_complete_promise] {
            handshake_complete_promise.set_value();
            return std::string(kPayloadBytes, 'w');
        });
    server.start();
    IoContextRunner server_runner(server_ioc);

    net::io_context client_ioc;
    TestWebSocket client(client_ioc);
    boost::beast::get_lowest_layer(client).connect(
        {net::ip::make_address("127.0.0.1"), server.port()});
    boost::beast::get_lowest_layer(client).socket().set_option(
        net::socket_base::receive_buffer_size(4096));
    client.handshake("127.0.0.1", "/simcore");
    if (handshake_complete.wait_for(std::chrono::seconds(2))
        != std::future_status::ready) {
        throw std::runtime_error(
            "stalled-write test handshake did not complete");
    }

    net::post(server_ioc, [&] {
        server.close_all("forced-close stalled-write test");
        close_requested_promise.set_value();
    });
    if (close_requested.wait_for(std::chrono::seconds(2))
        != std::future_status::ready) {
        throw std::runtime_error(
            "stalled-write test did not request close");
    }

    // Do not read any of the large application frame while the server's close
    // grace period elapses. This keeps async_write back-pressured and forces
    // the deadline path to cancel the lowest layer.
    std::this_thread::sleep_for(
        WsSession::kCloseGracePeriod + std::chrono::milliseconds(100));
    const std::size_t bytes_received = drain_raw_until_tcp_closed(client);

    server_runner.stop_and_join();
    if (bytes_received >= kPayloadBytes) {
        throw std::runtime_error(
            "stalled-write fixture received the complete payload before force-close");
    }
}

void test_forced_close_when_peer_ignores_close_reply()
{
    net::io_context server_ioc;
    std::promise<void> close_requested_promise;
    auto close_requested = close_requested_promise.get_future();
    WsServer server(
        server_ioc,
        0,
        [](std::uint64_t, const std::string&) {
            return true;
        },
        [] {
            return std::string("server-hello");
        });
    server.start();
    IoContextRunner server_runner(server_ioc);

    net::io_context client_ioc;
    TestWebSocket client(client_ioc);
    boost::beast::get_lowest_layer(client).connect(
        {net::ip::make_address("127.0.0.1"), server.port()});
    client.handshake("127.0.0.1", "/simcore");

    boost::beast::flat_buffer initial_message;
    const auto initial_read = read_frame_with_timeout(
        client_ioc, client, initial_message);
    if (initial_read.error) {
        throw std::runtime_error(
            "ignored-close test initial read failed: "
            + initial_read.error.message());
    }

    net::post(server_ioc, [&] {
        server.close_all("forced-close missing peer reply test");
        close_requested_promise.set_value();
    });
    if (close_requested.wait_for(std::chrono::seconds(2))
        != std::future_status::ready) {
        throw std::runtime_error(
            "ignored-close test did not request close");
    }

    // Do not call websocket::read: a Beast client would consume the close
    // frame and automatically complete the closing handshake. After the grace
    // period, inspect raw TCP only to observe the server's forced EOF.
    std::this_thread::sleep_for(
        WsSession::kCloseGracePeriod + std::chrono::milliseconds(100));
    const std::size_t close_frame_bytes = drain_raw_until_tcp_closed(client);

    server_runner.stop_and_join();
    if (close_frame_bytes == 0) {
        throw std::runtime_error(
            "server did not attempt a graceful close before forced shutdown");
    }
}

void test_server_initiated_close_retires_existing_socket()
{
    net::io_context server_ioc;
    std::promise<void> handshake_complete_promise;
    auto handshake_complete = handshake_complete_promise.get_future();
    WsServer server(
        server_ioc,
        0,
        [](std::uint64_t, const std::string&) {
            return true;
        },
        [&handshake_complete_promise] {
            // on_connect runs only after WsSession has completed the WebSocket
            // upgrade. Signal before constructing the large payload so the
            // client can queue close_all while the initial async_write is
            // guaranteed to be the next operation on the one server thread.
            handshake_complete_promise.set_value();
            return std::string(2 * 1024 * 1024, 's');
        });
    server.start();
    IoContextRunner server_runner(server_ioc);

    net::io_context client_ioc;
    websocket::stream<boost::beast::tcp_stream> client(client_ioc);
    boost::system::error_code connect_error;
    boost::beast::get_lowest_layer(client).connect(
        {net::ip::make_address("127.0.0.1"), server.port()}, connect_error);

    boost::system::error_code handshake_error;
    if (!connect_error) {
        client.handshake("127.0.0.1", "/simcore", handshake_error);
    }

    if (!connect_error && !handshake_error
        && handshake_complete.wait_for(std::chrono::seconds(2))
            != std::future_status::ready) {
        server_runner.stop_and_join();
        throw std::runtime_error(
            "server WebSocket accept handler did not complete after client handshake");
    }

    if (!connect_error && !handshake_error) {
        net::post(server_ioc, [&server] {
            server.close_all("control lease timed out; reconnect required");
        });
    }

    boost::beast::flat_buffer initial_state;
    boost::system::error_code initial_read_error;
    if (!connect_error && !handshake_error) {
        initial_read_error = read_frame_with_timeout(
            client_ioc, client, initial_state).error;
    }

    boost::beast::flat_buffer close_frame;
    boost::system::error_code close_error;
    if (!connect_error && !handshake_error && !initial_read_error) {
        close_error = read_frame_with_timeout(
            client_ioc, client, close_frame).error;
    }

    server_runner.stop_and_join();

    if (connect_error) {
        throw std::runtime_error("close test connect failed: " + connect_error.message());
    }
    if (handshake_error) {
        throw std::runtime_error("close test handshake failed: " + handshake_error.message());
    }
    if (initial_read_error) {
        throw std::runtime_error("close test initial read failed: " + initial_read_error.message());
    }
    if (initial_state.size() != 2 * 1024 * 1024) {
        throw std::runtime_error(
            "close test did not receive the complete initial binary state");
    }
    if (close_error != websocket::error::closed) {
        throw std::runtime_error("server close frame was not received: " + close_error.message());
    }
    if (client.reason().code != websocket::close_code::policy_error) {
        throw std::runtime_error("server close frame used the wrong reason code");
    }
}

int run_tests()
{
    test_second_server_cannot_bind_the_same_port();
    test_close_before_handshake_closes_tcp_connection();
    test_server_destruction_safely_cancels_pending_accept();
    test_oversized_message_is_not_delivered_to_application();
    test_broadcast_waits_for_accepted_client_hello();
    test_rejected_first_application_message_closes_socket();
    test_forced_close_cancels_stalled_write();
    test_forced_close_when_peer_ignores_close_reply();

    net::io_context server_ioc;
    WsServer server(server_ioc, 0, [](std::uint64_t, const std::string&) {
        return true;
    }, [] {
        return std::string("initial-state");
    });
    server.start();

    net::io_context client_ioc;
    tcp::socket client(client_ioc);
    client.connect({net::ip::make_address("127.0.0.1"), server.port()});

    // Let the TCP accept complete, but deliberately broadcast before the
    // client sends its WebSocket upgrade request. The first bytes returned to
    // the client must still be the HTTP 101 response, never a binary frame.
    server_ioc.poll();
    server.broadcast_binary("early-state");

    const std::string request =
        "GET /simcore HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";
    net::write(client, net::buffer(request));
    client.non_blocking(true);

    std::string response;
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (response.size() < 12 && std::chrono::steady_clock::now() < timeout) {
        server_ioc.poll();

        char bytes[1024];
        boost::system::error_code ec;
        const std::size_t count = client.read_some(net::buffer(bytes), ec);
        if (!ec) {
            response.append(bytes, count);
        } else if (ec != net::error::would_block && ec != net::error::try_again) {
            throw std::runtime_error("client read failed: " + ec.message());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!response.starts_with("HTTP/1.1 101")) {
        throw std::runtime_error(
            "WebSocket payload was written before the HTTP 101 handshake");
    }

    boost::system::error_code ignored;
    client.close(ignored);
    server_ioc.stop();

    test_server_initiated_close_retires_existing_socket();
    std::cout << "websocket_server_tests: all tests passed\n";
    return 0;
}

int main()
{
    try {
        return run_tests();
    } catch (const std::exception& error) {
        std::cerr << "websocket_server_tests: " << error.what() << '\n';
        return 1;
    }
}
