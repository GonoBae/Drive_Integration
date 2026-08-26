#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
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
#include "collision/map_package_static_collision.hpp"
#include "physics/vehicle_config.hpp"
#if defined(SIMCORE_ENABLE_ZMQ_OBSERVER)
#include "publisher/zmq_publisher.hpp"
#endif
#include "simulation_host.hpp"
#include "terrain/map_package_ground_query.hpp"
#include "terrain/map_package_manifest.hpp"
#include "websocket/ws_server.hpp"

namespace net  = boost::asio;   // 비동기 네트워크

namespace {

#ifndef SIMCORE_DEFAULT_VEHICLE_CONFIG_PATH
#define SIMCORE_DEFAULT_VEHICLE_CONFIG_PATH "cpp/host/config/vehicle_sedan.cfg"
#endif

#ifndef SIMCORE_DEFAULT_MAP_PACKAGE_PATH
#define SIMCORE_DEFAULT_MAP_PACKAGE_PATH "map_packages/wall_broad_v1"
#endif

struct RuntimeOptions {
    std::filesystem::path vehicle_config_path =
        SIMCORE_DEFAULT_VEHICLE_CONFIG_PATH;
    std::filesystem::path map_package_path =
        SIMCORE_DEFAULT_MAP_PACKAGE_PATH;
    bool demo_entities = false;
};

RuntimeOptions parse_runtime_options(int argc, char* argv[])
{
    RuntimeOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto require_value = [&](const std::string& name)
            -> std::filesystem::path {
            if (index + 1 >= argc) {
                throw std::invalid_argument(name + " requires a path");
            }
            return argv[++index];
        };
        if (argument == "--vehicle-config") {
            options.vehicle_config_path = require_value(argument);
        } else if (argument == "--map-package") {
            options.map_package_path = require_value(argument);
        } else if (argument == "--demo-entities") {
            options.demo_entities = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout
                << "Usage: simcore_publisher [--vehicle-config PATH]"
                   " [--map-package DIRECTORY] [--demo-entities]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown argument: " + argument);
        }
    }
    return options;
}

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

int run_simcore(const RuntimeOptions& options)
{
    PlatformTimerResolution timer_resolution;
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "[SimCore] Starting...\n";

    const auto loaded_vehicle = simcore_host::load_vehicle_parameters(
        options.vehicle_config_path);
    const auto map_manifest = simcore_host::load_map_package_manifest(
        options.map_package_path);
    auto ground_query = std::make_shared<simcore_host::MapPackageGroundQuery>(
        simcore_host::MapPackageGroundQuery::load(options.map_package_path));
    if (ground_query->collision_checksum() != map_manifest.collision_checksum) {
        throw std::runtime_error(
            "MapPackage changed between manifest and ground loading");
    }
    auto collision_world = std::make_shared<simcore_host::CollisionWorld>(
        simcore_host::load_static_collision_world(map_manifest));
    const auto checksum_after_collision_load =
        simcore_host::compute_map_package_collision_checksum(
            map_manifest.package_directory,
            map_manifest.collision_files);
    if (checksum_after_collision_load != map_manifest.collision_checksum) {
        throw std::runtime_error(
            "MapPackage changed while static collision was loading");
    }
    const std::string map_package_checksum = ground_query->collision_checksum();
    std::cout << "[Config] vehicle=" << loaded_vehicle.source_path.string()
              << " checksum=" << loaded_vehicle.checksum << "\n";
    std::cout << "[Map] package="
              << std::filesystem::absolute(options.map_package_path).string()
              << " id=" << ground_query->package_id()
              << " collision_checksum=" << map_package_checksum
              << " ground_triangles=" << ground_query->triangle_count()
              << " ground_cells=" << ground_query->spatial_cell_count()
              << " ground_global="
              << ground_query->spatial_global_triangle_count()
              << " ground_max_candidates="
              << ground_query->maximum_query_candidate_count()
              << " static_colliders="
              << collision_world->static_collider_count() << "\n";
    auto runtime_entities = options.demo_entities
        ? make_demo_entities(*ground_query)
        : std::vector<simcore_host::RuntimeEntityState>{};
    std::cout << "[Entities] demo="
              << (options.demo_entities ? "enabled" : "disabled")
              << " runtime_count=" << runtime_entities.size() << "\n";

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

    SimulationHost host(
        ioc,
        {
            Config::DEFAULT_ORIGIN_LAT,
            Config::DEFAULT_ORIGIN_LON,
            Config::DEFAULT_ORIGIN_ALT,
            Config::DEFAULT_SPAWN_HEADING,
            Config::PHYSICS_HZ,
            std::chrono::nanoseconds(Config::COMMAND_TIMEOUT_NS),
            std::chrono::nanoseconds(Config::MAX_COMMAND_QUEUE_AGE_NS),
            Config::SOURCE_ID,
            map_package_checksum,
            loaded_vehicle.parameters,
            std::move(ground_query),
            std::move(collision_world),
            std::move(runtime_entities),
        },
        std::move(callbacks));

    ws_server = std::make_unique<WsServer>(
        ioc,
        Config::WS_PORT,
        [&host](std::uint64_t connection_generation,
                const std::string& message) {
            host.handle_client_message(message, connection_generation);
        },
        [&host] {
            return host.make_initial_world_state();
        });

    ws_server->start();
    host.start();

    std::cout << "[SimCore] WS binary :" << Config::WS_PORT;
#if defined(SIMCORE_ENABLE_ZMQ_OBSERVER)
    std::cout << "  ZMQ " << Config::ZMQ_BIND_ADDR;
#endif
    std::cout << "\n";

    ioc.run();
    host.stop();
    ws_server->stop();
    return 0;
}

int main(int argc, char* argv[])
{
    try {
        return run_simcore(parse_runtime_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "[SimCore] fatal error: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[SimCore] fatal unknown error\n";
        return 1;
    }
}
