#include "publisher/zmq_publisher.hpp"

#include <iostream>

ZmqPublisher::ZmqPublisher(const std::string& bind_addr)
    : ctx_(1)
    , socket_(ctx_, zmq::socket_type::pub)
{
    socket_.bind(bind_addr);
    std::cout << "[ZMQ] Publisher bound to " << bind_addr << "\n";
}

void ZmqPublisher::publish(const std::string& data) {
    socket_.send(zmq::buffer(data), zmq::send_flags::dontwait);
}
