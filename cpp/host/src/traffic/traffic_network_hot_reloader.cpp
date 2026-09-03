#include "traffic/traffic_network_hot_reloader.hpp"

#include "terrain/map_package_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

namespace simcore_host {

TrafficNetworkHotReloader::TrafficNetworkHotReloader(
    std::filesystem::path traffic_network_path,
    std::filesystem::path map_package_path,
    ReadyCallback on_ready,
    std::chrono::milliseconds poll_interval)
    : traffic_network_path_(std::move(traffic_network_path))
    , map_package_path_(std::move(map_package_path))
    , on_ready_(std::move(on_ready))
    , poll_interval_(poll_interval)
{
}

TrafficNetworkHotReloader::~TrafficNetworkHotReloader()
{
    stop();
}

void TrafficNetworkHotReloader::start()
{
    if (worker_.joinable()) {
        return;
    }
    worker_ = std::jthread(
        [this](std::stop_token stop_token) { run(stop_token); });
}

void TrafficNetworkHotReloader::stop()
{
    if (!worker_.joinable()) {
        return;
    }
    worker_.request_stop();
    worker_.join();
}

void TrafficNetworkHotReloader::run(std::stop_token stop_token)
{
    std::string last_success;
    std::string last_observed;
    std::string last_error;
    auto retry_after = std::chrono::steady_clock::time_point::min();
    auto retry_delay = std::chrono::milliseconds(250);
    while (!stop_token.stop_requested()) {
        try {
            const auto signature = [](const std::filesystem::path& path) {
                return std::to_string(std::filesystem::last_write_time(path)
                                          .time_since_epoch().count())
                    + ":" + std::to_string(std::filesystem::file_size(path));
            };
            const auto stamp = signature(traffic_network_path_) + "/"
                + signature(map_package_path_ / "manifest.cfg");
            if (stamp != last_observed) {
                last_observed = stamp;
                retry_after = std::chrono::steady_clock::time_point::min();
                retry_delay = std::chrono::milliseconds(250);
            }
            if (stamp != last_success
                && std::chrono::steady_clock::now() >= retry_after) {
                const auto candidate_map =
                    load_runtime_map_package(map_package_path_);
                auto candidate = std::make_shared<const TrafficNetwork>(
                    load_traffic_network(
                        traffic_network_path_,
                        candidate_map.collision_checksum,
                        *candidate_map.ground_query));
                on_ready_(std::move(candidate));
                last_success = stamp;
                last_error.clear();
                retry_delay = std::chrono::milliseconds(250);
            }
        } catch (const std::exception& error) {
            // A read/sharing/manifest race may fail without any later mtime
            // change. Only acknowledge successful loads; retry unchanged
            // failures with bounded backoff, logging once.
            if (last_error != error.what()) {
                std::cerr << "[Traffic] reload not applied: " << error.what()
                          << "\n";
                last_error = error.what();
            }
            retry_after = std::chrono::steady_clock::now() + retry_delay;
            retry_delay = std::min(
                retry_delay * 2,
                std::chrono::milliseconds(5000));
        }
        std::this_thread::sleep_for(poll_interval_);
    }
}

} // namespace simcore_host
