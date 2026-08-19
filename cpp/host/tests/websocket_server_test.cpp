#include "websocket/ws_server.hpp"

#include <boost/asio.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace net = boost::asio;
using tcp = net::ip::tcp;

int main()
{
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
    std::cout << "websocket_server_tests: all tests passed\n";
    return 0;
}
