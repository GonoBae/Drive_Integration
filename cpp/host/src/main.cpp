#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <timeapi.h>
#endif

#include "config.hpp"
#include "physics/vehicle_config.hpp"
#if defined(SIMCORE_ENABLE_ZMQ_OBSERVER)
#include "publisher/zmq_publisher.hpp"
#endif
#include "runtime_options.hpp"
#include "simulation_host.hpp"
#include "terrain/map_package_runtime.hpp"
#include "traffic/traffic_network_hot_reloader.hpp"
#include "websocket/ws_server.hpp"

namespace net  = boost::asio;   // 비동기 네트워크

namespace {

#ifndef SIMCORE_DEFAULT_VEHICLE_CONFIG_PATH
#define SIMCORE_DEFAULT_VEHICLE_CONFIG_PATH "cpp/host/config/vehicle_sedan.cfg"
#endif

#ifndef SIMCORE_DEFAULT_MAP_PACKAGE_PATH
#define SIMCORE_DEFAULT_MAP_PACKAGE_PATH "map_packages/wall_broad_v1"
#endif

std::optional<double> ground_height_at(
    const simcore_host::GroundQuery& ground_query,
    double east_m,
    double north_m)
{
    const auto hit = ground_query.query_down(
        {{east_m, north_m, 1'000.0}, 2'000.0});
    if (!hit) {
        return std::nullopt;
    }
    return hit->point_enu.up_m;
}

std::string describe_ground_bounds(
    const simcore_host::GroundBoundsEnu& bounds)
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(3)
           << "[Map] exported_ground_bbox_enu_m"
           << " east=[" << bounds.minimum.east_m
           << "," << bounds.maximum.east_m << "]"
           << " north=[" << bounds.minimum.north_m
           << "," << bounds.maximum.north_m << "]"
           << " up=[" << bounds.minimum.up_m
           << "," << bounds.maximum.up_m << "]"
           << " exported_span_m=" << bounds.east_span_m()
           << "x" << bounds.north_span_m()
           << "x" << bounds.up_span_m();
    return output.str();
}

std::vector<simcore_host::RuntimeEntityState> make_demo_entities(
    const simcore_host::GroundQuery& ground_query)
{
    std::vector<simcore_host::RuntimeEntityState> entities;

    constexpr double npc_east_m = 6.0;
    constexpr double npc_north_m = 10.0;
    constexpr double npc_half_height_m = 0.75;
    if (const auto ground = ground_height_at(
            ground_query, npc_east_m, npc_north_m)) {
        entities.push_back({
            1001,
            simcore_host::RuntimeEntityKind::NpcVehicle,
            {
                "entity-1001-npc",
                simcore_host::ObbPrism{
                    {npc_east_m, npc_north_m},
                    *ground + npc_half_height_m,
                    0.0,
                    2.2,
                    1.0,
                    npc_half_height_m,
                },
                {},
                0.0,
                {0.9, 0.0},
            },
        });
    } else {
        std::cerr << "[Entities] skipped demo NPC: no ground at ENU("
                  << npc_east_m << ", " << npc_north_m << ")\n";
    }

    constexpr double pedestrian_east_m = -4.0;
    constexpr double pedestrian_north_m = 7.0;
    constexpr double pedestrian_half_height_m = 0.9;
    if (const auto ground = ground_height_at(
            ground_query, pedestrian_east_m, pedestrian_north_m)) {
        entities.push_back({
            2001,
            simcore_host::RuntimeEntityKind::Pedestrian,
            {
                "entity-2001-pedestrian",
                simcore_host::VerticalCapsule{
                    {pedestrian_east_m, pedestrian_north_m},
                    *ground + pedestrian_half_height_m,
                    0.35,
                    pedestrian_half_height_m,
                },
                {},
                0.0,
                {0.7, 0.0},
            },
        });
    } else {
        std::cerr << "[Entities] skipped demo pedestrian: no ground at ENU("
                  << pedestrian_east_m << ", " << pedestrian_north_m << ")\n";
    }

    return entities;
}

class PlatformTimerResolution {
public:
    PlatformTimerResolution()
    {
#ifdef _WIN32
        active_ = timeBeginPeriod(kResolutionMs) == TIMERR_NOERROR;
        if (!active_) {
            std::cerr << "[Timing] failed to request 1ms Windows timer resolution\n";
        }
#endif
    }

    ~PlatformTimerResolution()
    {
#ifdef _WIN32
        if (active_) {
            timeEndPeriod(kResolutionMs);
        }
#endif
    }

    PlatformTimerResolution(const PlatformTimerResolution&) = delete;
    PlatformTimerResolution& operator=(const PlatformTimerResolution&) = delete;

private:
#ifdef _WIN32
    static constexpr UINT kResolutionMs = 1;
    bool active_ = false;
#endif
};

} // namespace

int run_simcore(const simcore_host::RuntimeOptions& options)
{
    PlatformTimerResolution timer_resolution;
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "[SimCore] Starting...\n";
    std::cout << "[Runtime] config="
              << (options.runtime_config_path
                    ? options.runtime_config_path->string()
                    : "<built-in-defaults>")
              << " ws_port=" << options.ws_port
              << " physics_hz=" << options.physics_frequency_hz
              << " origin_deg_m=" << options.origin_lat_deg
              << "," << options.origin_lon_deg
              << "," << options.origin_alt_m
              << " spawn_heading_deg=" << options.spawn_heading_deg
              << " command_timeout_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     options.command_timeout).count()
              << " max_queue_age_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     options.max_command_queue_age).count()
              << " hard_timeout_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     options.hard_command_timeout).count()
              << " source_id=" << options.source_id << "\n";

    const auto loaded_vehicle = simcore_host::load_vehicle_parameters(
        options.vehicle_config_path);
    auto map_package = simcore_host::load_runtime_map_package(
        options.map_package_path);
    const std::string map_package_checksum = map_package.collision_checksum;
    const auto& ground_diagnostics = map_package.ground_diagnostics;
    const std::size_t reported_ground_cells =
        ground_diagnostics.payload_kind
                != simcore_host::GroundPayloadKind::TriangleCsvV1
            ? ground_diagnostics.cell_count
            : ground_diagnostics.spatial_cell_count;
    std::cout << "[Config] vehicle=" << loaded_vehicle.source_path.string()
              << " checksum=" << loaded_vehicle.checksum << "\n";
    std::cout << "[Map] package="
              << std::filesystem::absolute(options.map_package_path).string()
              << " id=" << map_package.map_id
              << " collision_checksum=" << map_package_checksum
              << " ground_format="
              << simcore_host::ground_payload_kind_name(
                     ground_diagnostics.payload_kind)
              << " ground_triangles="
              << ground_diagnostics.triangle_count
              << " ground_samples="
              << ground_diagnostics.sample_count
              << " ground_cells="
              << reported_ground_cells
              << " ground_drivable_cells="
              << ground_diagnostics.drivable_cell_count
              << " ground_index_cells="
              << ground_diagnostics.spatial_cell_count
              << " ground_global="
              << ground_diagnostics.spatial_global_triangle_count
              << " ground_max_candidates="
              << ground_diagnostics.maximum_query_candidate_count
              << " ground_lattice="
              << ground_diagnostics.lattice_columns
              << "x" << ground_diagnostics.lattice_rows
              << " static_colliders="
              << map_package.collision_world->static_collider_count() << "\n";
    std::cout << describe_ground_bounds(
        ground_diagnostics.bounds_enu) << "\n";
    auto runtime_entities = options.demo_entities
        ? make_demo_entities(*map_package.ground_query)
        : std::vector<simcore_host::RuntimeEntityState>{};
    std::cout << "[Entities] demo="
              << (options.demo_entities ? "enabled" : "disabled")
              << " runtime_count=" << runtime_entities.size() << "\n";

    std::shared_ptr<const simcore_host::TrafficNetwork> traffic_network;
    if (options.traffic_network_path) {
        try {
            traffic_network = std::make_shared<const simcore_host::TrafficNetwork>(
                simcore_host::load_traffic_network(*options.traffic_network_path,
                    map_package_checksum, *map_package.ground_query));
            std::cout << "[Traffic] verified network=" << options.traffic_network_path->string()
                      << " checksum=" << traffic_network->checksum
                      << " lanes=" << traffic_network->lanes.size()
                      << " signal_heads=" << traffic_network->signals.size()
                      << " " << simcore_host::describe_signal_timing(*traffic_network)
                      << " lane_npc="
                      << (options.npc_route.empty() ? "off" : "enabled") << "\n";
        } catch (const std::exception& error) {
            std::cerr << "[Traffic] disabled: " << error.what()
                      << "; manual driving remains available; watching for a valid network\n";
        }
    }

    // Asio io_context (single-threaded)
    net::io_context ioc;

    std::unique_ptr<WsServer> ws_server;
#if defined(SIMCORE_ENABLE_ZMQ_OBSERVER)
    ZmqPublisher publisher(Config::ZMQ_BIND_ADDR);
#endif

    SimulationHostCallbacks callbacks;
    callbacks.broadcast_world_state = [&ws_server](const std::string& message) {
        ws_server->broadcast_binary(message);
    };
#if defined(SIMCORE_ENABLE_ZMQ_OBSERVER)
    callbacks.publish_observer_state = [&publisher](const std::string& message) {
        publisher.publish(message);
    };
#endif
    callbacks.close_control_connections = [&ws_server](const std::string& reason) {
        ws_server->close_all(reason);
    };

    SimulationHostConfig host_config;
    host_config.origin_lat = options.origin_lat_deg;
    host_config.origin_lon = options.origin_lon_deg;
    host_config.origin_alt = options.origin_alt_m;
    host_config.spawn_heading = options.spawn_heading_deg;
    host_config.physics_frequency_hz = options.physics_frequency_hz;
    host_config.command_timeout = options.command_timeout;
    host_config.max_command_queue_age = options.max_command_queue_age;
    host_config.hard_command_timeout = options.hard_command_timeout;
    host_config.source_id = options.source_id;
    host_config.map_package_checksum = map_package_checksum;
    host_config.vehicle_parameters = loaded_vehicle.parameters;
    host_config.ground_query = map_package.ground_query;
    host_config.collision_world = map_package.collision_world;
    host_config.traffic_network = std::move(traffic_network);
    host_config.npc_route = options.npc_route;
    host_config.npc_alternate_route = options.npc_alternate_route;
    host_config.npc_route_loop = options.npc_route_loop;
    host_config.npc_start_offset_m = options.npc_start_offset_m;
    host_config.npc_max_speed_mps = options.npc_max_speed_mps;
    host_config.npc_count = options.npc_count;
    host_config.npc_spacing_m = options.npc_spacing_m;
    host_config.runtime_entities = std::move(runtime_entities);
    host_config.require_client_hello = true;
    SimulationHost host(
        ioc,
        std::move(host_config),
        std::move(callbacks));

    ws_server = std::make_unique<WsServer>(
        ioc,
        options.ws_port,
        [&host](std::uint64_t connection_generation,
                const std::string& message) {
            const auto result = host.handle_client_message(
                message, connection_generation);
            return result == ClientMessageResult::HelloAccepted
                || result == ClientMessageResult::DuplicateHello;
        },
        [&host] {
            return host.make_initial_hello();
        });

    simcore_host::MapPackageHotReloader map_reloader(
        options.map_package_path,
        map_package_checksum,
        [&host, demo_entities = options.demo_entities](
            simcore_host::RuntimeMapPackage candidate) {
            auto replacement_entities = demo_entities
                ? make_demo_entities(*candidate.ground_query)
                : std::vector<simcore_host::RuntimeEntityState>{};
            host.queue_map_package_reload(
                std::move(candidate),
                std::move(replacement_entities));
        });

    ws_server->start();
    host.start();
    map_reloader.start();

    // File parsing, map verification and lane/ground sampling never run on the
    // physics thread. A Bake can continue driving even if the authored traffic
    // sidecar is stale: the host holds its signals red until a matching,
    // validated sidecar arrives, without requiring a process restart.
    std::unique_ptr<simcore_host::TrafficNetworkHotReloader> traffic_watcher;
    if (options.traffic_network_path) {
        traffic_watcher =
            std::make_unique<simcore_host::TrafficNetworkHotReloader>(
                *options.traffic_network_path,
                options.map_package_path,
                [&host](
                    std::shared_ptr<const simcore_host::TrafficNetwork>
                        candidate) {
                    host.queue_traffic_network_reload(std::move(candidate));
                });
        traffic_watcher->start();
    }

    std::cout << "[SimCore] WS binary :" << options.ws_port;
#if defined(SIMCORE_ENABLE_ZMQ_OBSERVER)
    std::cout << "  ZMQ " << Config::ZMQ_BIND_ADDR;
#endif
    std::cout << "\n";

    ioc.run();
    if (traffic_watcher) {
        traffic_watcher->stop();
    }
    map_reloader.stop();
    host.stop();
    ws_server->stop();
    return 0;
}

int main(int argc, char* argv[])
{
    try {
        const auto defaults = simcore_host::make_runtime_option_defaults(
            SIMCORE_DEFAULT_VEHICLE_CONFIG_PATH,
            SIMCORE_DEFAULT_MAP_PACKAGE_PATH);
        const auto options = simcore_host::parse_runtime_options(
            argc, argv, defaults);
        if (options.show_help) {
            std::cout << simcore_host::runtime_options_help(defaults);
            return 0;
        }
        return run_simcore(options);
    } catch (const std::exception& error) {
        std::cerr << "[SimCore] fatal error: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[SimCore] fatal unknown error\n";
        return 1;
    }
}
