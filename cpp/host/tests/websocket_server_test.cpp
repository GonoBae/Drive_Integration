#include "websocket/ws_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core/tcp_stream.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace websocket = boost::beast::websocket;

void test_close_before_handshake_closes_tcp_connection()
{
    net::io_context server_ioc;
    WsServer server(server_ioc, 0, [](const std::string&) {}, [] {
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
        WsServer server(ioc, 0, [](const std::string&) {}, [] {
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
    WsServer server(server_ioc, 0, [&](const std::string&) {
        ++delivered_messages;
    }, [] {
        return std::string("initial-state");
    });
    server.start();
    std::thread server_thread([&] { server_ioc.run(); });

    net::io_context client_ioc;
    websocket::stream<boost::beast::tcp_stream> client(client_ioc);
    boost::beast::get_lowest_layer(client).expires_after(std::chrono::seconds(2));
    boost::beast::get_lowest_layer(client).connect(
        {net::ip::make_address("127.0.0.1"), server.port()});
    client.handshake("127.0.0.1", "/simcore");

    boost::beast::flat_buffer initial_state;
    client.read(initial_state);
    client.binary(true);
    const std::string oversized(WsSession::kMaxIncomingMessageBytes + 1, 'x');
    boost::system::error_code write_error;
    client.write(net::buffer(oversized), write_error);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    server_ioc.stop();
    server_thread.join();
    if (delivered_messages.load() != 0) {
        throw std::runtime_error("oversized WebSocket message reached application callback");
    }
}

void test_server_initiated_close_retires_existing_socket()
{
    net::io_context server_ioc;
    WsServer server(server_ioc, 0, [](const std::string&) {}, [] {
        // Large enough to keep async_write alive while close_all is requested.
        return std::string(2 * 1024 * 1024, 's');
    });
    server.start();
    std::thread server_thread([&] { server_ioc.run(); });

    net::io_context client_ioc;
    websocket::stream<boost::beast::tcp_stream> client(client_ioc);
    boost::system::error_code connect_error;
    boost::beast::get_lowest_layer(client).expires_after(std::chrono::seconds(2));
    boost::beast::get_lowest_layer(client).connect(
        {net::ip::make_address("127.0.0.1"), server.port()}, connect_error);

    boost::system::error_code handshake_error;
    if (!connect_error) {
        client.handshake("127.0.0.1", "/simcore", handshake_error);
    }

    if (!connect_error && !handshake_error) {
        net::post(server_ioc, [&server] {
            server.close_all("control lease timed out; reconnect required");
        });
    }

    boost::beast::flat_buffer initial_state;
    boost::system::error_code initial_read_error;
    if (!connect_error && !handshake_error) {
        client.read(initial_state, initial_read_error);
    }

    boost::beast::flat_buffer close_frame;
    boost::system::error_code close_error;
    if (!connect_error && !handshake_error && !initial_read_error) {
        boost::beast::get_lowest_layer(client).expires_after(std::chrono::seconds(2));
        client.read(close_frame, close_error);
    }

    server_ioc.stop();
    server_thread.join();

    if (connect_error) {
        throw std::runtime_error("close test connect failed: " + connect_error.message());
    }
    if (handshake_error) {
        throw std::runtime_error("close test handshake failed: " + handshake_error.message());
    }
    if (initial_read_error) {
        throw std::runtime_error("close test initial read failed: " + initial_read_error.message());
    }
    if (close_error != websocket::error::closed) {
        throw std::runtime_error("server close frame was not received: " + close_error.message());
    }
    if (client.reason().code != websocket::close_code::policy_error) {
        throw std::runtime_error("server close frame used the wrong reason code");
    }
}

int main()
{
    test_close_before_handshake_closes_tcp_connection();
    test_server_destruction_safely_cancels_pending_accept();
    test_oversized_message_is_not_delivered_to_application();

    net::io_context server_ioc;
    WsServer server(server_ioc, 0, [](const std::string&) {}, [] {
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
