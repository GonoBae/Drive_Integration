#pragma once

#include "traffic/traffic_network.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>

namespace simcore_host {

// Watches the authored traffic sidecar and its owning MapPackage manifest on
// a worker thread. A candidate is delivered only after both the MapPackage and
// traffic network have been loaded and cross-validated successfully.
class TrafficNetworkHotReloader {
public:
    using ReadyCallback =
        std::function<void(std::shared_ptr<const TrafficNetwork>)>;

    TrafficNetworkHotReloader(
        std::filesystem::path traffic_network_path,
        std::filesystem::path map_package_path,
        ReadyCallback on_ready,
        std::chrono::milliseconds poll_interval =
            std::chrono::milliseconds(250));
    ~TrafficNetworkHotReloader();

    TrafficNetworkHotReloader(const TrafficNetworkHotReloader&) = delete;
    TrafficNetworkHotReloader& operator=(
        const TrafficNetworkHotReloader&) = delete;

    void start();
    void stop();

private:
    void run(std::stop_token stop_token);

    std::filesystem::path traffic_network_path_;
    std::filesystem::path map_package_path_;
    ReadyCallback on_ready_;
    std::chrono::milliseconds poll_interval_;
    std::jthread worker_;
};

} // namespace simcore_host
