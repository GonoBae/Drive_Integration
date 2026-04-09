#pragma once

#include <zmq.hpp>
#include <string>

class ZmqPublisher {
public:
    explicit ZmqPublisher(const std::string& bind_addr);
    void publish(const std::string& data);

private:
    zmq::context_t ctx_;
    zmq::socket_t  socket_;
};
