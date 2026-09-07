cmake_minimum_required(VERSION 3.21)
project(SimCorePublisher CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(
    SIMCORE_ENABLE_ZMQ_OBSERVER
    "Build the optional C++ to Python ZMQ observer transport"
    OFF
)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

set(SIMCORE_PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../protocol")
set(SIMCORE_VEHICLE_PROTO "${SIMCORE_PROTOCOL_DIR}/vehicle.proto")

# Protobuf
find_package(protobuf CONFIG REQUIRED)

# Boost (Beast WebSocket)
find_package(Boost CONFIG REQUIRED COMPONENTS system property_tree)
find_package(Threads REQUIRED)

if(SIMCORE_ENABLE_ZMQ_OBSERVER)
    find_package(ZeroMQ CONFIG REQUIRED)
    find_package(cppzmq CONFIG REQUIRED)
endif()

add_library(simcore_protocol
    src/protocol/vehicle_message_decoder.cpp
    src/protocol/vehicle_messages.cpp
)

protobuf_generate(
    TARGET simcore_protocol
    PROTOS "${SIMCORE_VEHICLE_PROTO}"
    LANGUAGE cpp
    IMPORT_DIRS "${SIMCORE_PROTOCOL_DIR}"
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

target_include_directories(simcore_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(simcore_protocol PUBLIC
    protobuf::libprotobuf
)

add_library(simcore_collision
    src/collision/collision_world.cpp
    src/collision/map_package_static_collision.cpp
)

target_include_directories(simcore_collision PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(simcore_physics
    src/physics/chassis_ground_contact.cpp
    src/physics/vehicle_config.cpp
    src/physics/vehicle_parameters.cpp
    src/physics/vehicle_physics.cpp
    src/terrain/heightfield_ground_query.cpp
    src/terrain/map_package_ground_query.cpp
    src/terrain/map_package_manifest.cpp
    src/terrain/map_package_runtime.cpp
)

target_include_directories(simcore_physics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(simcore_physics PUBLIC
    simcore_collision
    Threads::Threads
)

add_library(simcore_websocket
    src/websocket/ws_server.cpp
)

add_library(simcore_traffic
    src/traffic/traffic_network.cpp
    src/traffic/traffic_network_hot_reloader.cpp
    src/traffic/npc_lane_follower.cpp
    src/traffic/npc_horn_policy.cpp
    src/traffic/npc_route_planner.cpp
    src/traffic/npc_lane_change.cpp
    src/traffic/impact_recovery.cpp
    src/traffic/pedestrian_impact.cpp
    src/traffic/impact_tumble.cpp
    src/collision/structure_damage.cpp
)
target_include_directories(simcore_traffic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(simcore_traffic PUBLIC simcore_physics Boost::property_tree)

target_include_directories(simcore_websocket PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(simcore_websocket PUBLIC
    Boost::system
)

add_library(simcore_runtime_options
    src/runtime_cli_parser.cpp
    src/runtime_config_loader.cpp
    src/runtime_options.cpp
)

target_include_directories(simcore_runtime_options PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(SIMCORE_ENABLE_ZMQ_OBSERVER)
    add_library(simcore_zmq_publisher
        src/publisher/zmq_publisher.cpp
    )

    target_include_directories(simcore_zmq_publisher PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(simcore_zmq_publisher PUBLIC
        cppzmq
    )
endif()

add_library(simcore_host_core
    src/player_vehicle_profile.cpp
    src/replay/physics_replay.cpp
    src/runtime_collision_reaction.cpp
    src/simulation_host.cpp
    src/simulation_host_npc.cpp
    src/simulation_host_session.cpp
)

target_include_directories(simcore_host_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(simcore_host_core PUBLIC
    simcore_physics
    simcore_traffic
    simcore_protocol
    Boost::system
)

add_executable(simcore_publisher
    src/main.cpp
)

target_include_directories(simcore_publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(simcore_publisher PRIVATE
    simcore_host_core
    simcore_runtime_options
    simcore_websocket
)

target_compile_definitions(simcore_publisher PRIVATE
    SIMCORE_DEFAULT_VEHICLE_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/vehicle_sedan.cfg"
    SIMCORE_DEFAULT_MAP_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../map_packages/wall_broad_v1"
)

if(SIMCORE_ENABLE_ZMQ_OBSERVER)
    target_compile_definitions(simcore_publisher PRIVATE
        SIMCORE_ENABLE_ZMQ_OBSERVER=1
    )
    target_link_libraries(simcore_publisher PRIVATE
        simcore_zmq_publisher
    )
endif()

if(WIN32)
    target_link_libraries(simcore_publisher PRIVATE winmm)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(runtime_collision_reaction_tests tests/runtime_collision_reaction_test.cpp)
    target_link_libraries(runtime_collision_reaction_tests PRIVATE simcore_host_core)
    add_test(NAME runtime_collision_reaction_tests COMMAND runtime_collision_reaction_tests)
    add_executable(physics_replay_tests tests/physics_replay_test.cpp)
    target_link_libraries(physics_replay_tests PRIVATE simcore_host_core simcore_runtime_options)
    add_test(NAME physics_replay_tests COMMAND physics_replay_tests)
    set_tests_properties(physics_replay_tests PROPERTIES TIMEOUT 20)
    add_executable(npc_host_tests tests/npc_host_test.cpp)
    target_link_libraries(npc_host_tests PRIVATE simcore_host_core)
    target_compile_definitions(npc_host_tests PRIVATE
        SIMCORE_TEST_VIRTUAL_CITY_MAP_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../map_packages/virtual_city_v1"
    )
    add_test(NAME npc_host_tests COMMAND npc_host_tests)
    add_test(NAME npc_return_curve_tests COMMAND npc_host_tests --return-curve)
    add_test(NAME npc_lateral_recovery_tests COMMAND npc_host_tests --lateral-recovery)
    set_tests_properties(npc_lateral_recovery_tests PROPERTIES TIMEOUT 40)
    add_executable(npc_navigation_host_tests tests/npc_navigation_host_test.cpp)
    target_link_libraries(npc_navigation_host_tests PRIVATE simcore_host_core)
    target_compile_definitions(npc_navigation_host_tests PRIVATE
        SIMCORE_TEST_SIGNAL_CITY_MAP_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../map_packages/signal_city_v2"
    )
    add_test(NAME npc_navigation_host_tests COMMAND npc_navigation_host_tests)
    set_tests_properties(npc_navigation_host_tests PROPERTIES TIMEOUT 45)
    add_test(NAME npc_delayed_crash_bypass_tests COMMAND npc_navigation_host_tests --delayed-crash)
    set_tests_properties(npc_delayed_crash_bypass_tests PROPERTIES TIMEOUT 30)
    add_test(NAME npc_mixed_fleet_tests COMMAND npc_navigation_host_tests --mixed-fleet)
    set_tests_properties(npc_mixed_fleet_tests PROPERTIES TIMEOUT 15)
    add_test(NAME npc_close_escape_tests COMMAND npc_navigation_host_tests --close-escape)
    set_tests_properties(npc_close_escape_tests PROPERTIES TIMEOUT 30)
    add_test(NAME npc_limited_close_escape_tests COMMAND npc_navigation_host_tests --limited-close-escape)
    set_tests_properties(npc_limited_close_escape_tests PROPERTIES TIMEOUT 30)
    add_test(NAME npc_downed_stopline_escape_tests COMMAND npc_navigation_host_tests --downed-stopline)
    set_tests_properties(npc_downed_stopline_escape_tests PROPERTIES TIMEOUT 60)
    add_test(NAME npc_downed_rear_blocked_tests COMMAND npc_navigation_host_tests --downed-rear-blocked)
    set_tests_properties(npc_downed_rear_blocked_tests PROPERTIES TIMEOUT 30)
    add_executable(motorcycle_physics_tests tests/motorcycle_physics_test.cpp)
    target_link_libraries(motorcycle_physics_tests PRIVATE simcore_host_core)
    add_test(NAME motorcycle_physics_tests COMMAND motorcycle_physics_tests)
    add_executable(npc_lane_follower_tests tests/npc_lane_follower_test.cpp)
    target_link_libraries(npc_lane_follower_tests PRIVATE simcore_traffic)
    add_test(NAME npc_lane_follower_tests COMMAND npc_lane_follower_tests)
    add_executable(npc_horn_policy_tests tests/npc_horn_policy_test.cpp)
    target_link_libraries(npc_horn_policy_tests PRIVATE simcore_traffic)
    add_test(NAME npc_horn_policy_tests COMMAND npc_horn_policy_tests)
    add_executable(npc_route_planner_tests tests/npc_route_planner_test.cpp)
    target_link_libraries(npc_route_planner_tests PRIVATE simcore_traffic)
    add_test(NAME npc_route_planner_tests COMMAND npc_route_planner_tests)
    add_executable(npc_lane_change_tests tests/npc_lane_change_test.cpp)
    target_link_libraries(npc_lane_change_tests PRIVATE simcore_traffic)
    add_test(NAME npc_lane_change_tests COMMAND npc_lane_change_tests)
    add_executable(impact_recovery_tests tests/impact_recovery_test.cpp)
    target_link_libraries(impact_recovery_tests PRIVATE simcore_traffic)
    add_test(NAME impact_recovery_tests COMMAND impact_recovery_tests)
    add_executable(pedestrian_impact_tests tests/pedestrian_impact_test.cpp)
    target_link_libraries(pedestrian_impact_tests PRIVATE simcore_traffic)
    add_test(NAME pedestrian_impact_tests COMMAND pedestrian_impact_tests)
    add_executable(impact_tumble_tests tests/impact_tumble_test.cpp)
    target_link_libraries(impact_tumble_tests PRIVATE simcore_traffic)
    add_test(NAME impact_tumble_tests COMMAND impact_tumble_tests)
    add_executable(structure_damage_tests tests/structure_damage_test.cpp)
    target_link_libraries(structure_damage_tests PRIVATE simcore_traffic)
    add_test(NAME structure_damage_tests COMMAND structure_damage_tests)
    add_executable(traffic_network_tests tests/traffic_network_test.cpp)
    target_link_libraries(traffic_network_tests PRIVATE simcore_traffic)
    target_compile_definitions(traffic_network_tests PRIVATE
        SIMCORE_TEST_VIRTUAL_CITY_MAP_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../map_packages/virtual_city_v1"
    )
    add_test(NAME traffic_network_tests COMMAND traffic_network_tests)

    add_executable(vehicle_physics_tests
        tests/vehicle_physics_test.cpp
    )

    target_link_libraries(vehicle_physics_tests PRIVATE
        simcore_physics
    )

    target_compile_definitions(vehicle_physics_tests PRIVATE
        SIMCORE_TEST_VEHICLE_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/vehicle_sedan.cfg"
        SIMCORE_TEST_LANDSCAPE_MAP_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../map_packages/landscape_local_v1"
    )

    add_test(NAME vehicle_physics_tests COMMAND vehicle_physics_tests)

    add_executable(chassis_ground_contact_tests
        tests/chassis_ground_contact_test.cpp
    )

    target_link_libraries(chassis_ground_contact_tests PRIVATE
        simcore_physics
    )

    add_test(NAME chassis_ground_contact_tests COMMAND chassis_ground_contact_tests)

    # Offline pedal-only characterization; emits a new CSV and never connects
    # to or changes the interactive server/map. Not a real-car calibration.
    add_executable(vehicle_cornering_probe tests/vehicle_cornering_probe.cpp)
    target_link_libraries(vehicle_cornering_probe PRIVATE simcore_physics)
    target_compile_definitions(vehicle_cornering_probe PRIVATE
        SIMCORE_TEST_VEHICLE_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/vehicle_sedan.cfg"
    )

    add_executable(virtual_city_course_tests
        tests/virtual_city_course_test.cpp
    )

    target_link_libraries(virtual_city_course_tests PRIVATE
        simcore_physics
    )

    target_compile_definitions(virtual_city_course_tests PRIVATE
        SIMCORE_TEST_VEHICLE_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/vehicle_sedan.cfg"
        SIMCORE_TEST_VIRTUAL_CITY_MAP_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../map_packages/virtual_city_v1"
    )

    add_test(NAME virtual_city_course_tests COMMAND virtual_city_course_tests)
    set_tests_properties(virtual_city_course_tests PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 90
    )

    add_executable(signal_city_course_tests
        tests/signal_city_course_test.cpp
    )

    target_link_libraries(signal_city_course_tests PRIVATE
        simcore_physics
    )

    target_compile_definitions(signal_city_course_tests PRIVATE
        SIMCORE_TEST_VEHICLE_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/vehicle_sedan.cfg"
        SIMCORE_TEST_SIGNAL_CITY_MAP_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../map_packages/signal_city_v2"
    )

    add_test(NAME signal_city_course_tests COMMAND signal_city_course_tests)
    set_tests_properties(signal_city_course_tests PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 90
    )

    add_executable(vehicle_config_tests
        tests/vehicle_config_test.cpp
    )

    target_link_libraries(vehicle_config_tests PRIVATE
        simcore_physics
    )

    target_compile_definitions(vehicle_config_tests PRIVATE
        SIMCORE_TEST_VEHICLE_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/vehicle_sedan.cfg"
    )

    add_test(NAME vehicle_config_tests COMMAND vehicle_config_tests)

    add_executable(runtime_options_tests
        tests/runtime_options_test.cpp
    )

    target_link_libraries(runtime_options_tests PRIVATE
        simcore_runtime_options
    )

    target_compile_definitions(runtime_options_tests PRIVATE
        SIMCORE_TEST_RUNTIME_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/runtime_server.cfg"
    )

    add_test(NAME runtime_options_tests COMMAND runtime_options_tests)

    add_executable(map_package_ground_query_tests
        tests/map_package_ground_query_test.cpp
    )

    target_link_libraries(map_package_ground_query_tests PRIVATE
        simcore_physics
    )

    target_compile_definitions(map_package_ground_query_tests PRIVATE
        SIMCORE_TEST_MAP_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../map_packages/wall_broad_v1"
    )

    add_test(NAME map_package_ground_query_tests COMMAND map_package_ground_query_tests)

    add_executable(heightfield_ground_query_tests
        tests/heightfield_ground_query_test.cpp
    )

    target_link_libraries(heightfield_ground_query_tests PRIVATE
        simcore_physics
    )

    add_test(
        NAME heightfield_ground_query_tests
        COMMAND heightfield_ground_query_tests
    )

    add_executable(map_package_runtime_tests
        tests/map_package_runtime_test.cpp
    )

    target_link_libraries(map_package_runtime_tests PRIVATE
        simcore_physics
    )

    target_compile_definitions(map_package_runtime_tests PRIVATE
        SIMCORE_TEST_MAP_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../map_packages/wall_broad_v1"
    )

    add_test(NAME map_package_runtime_tests COMMAND map_package_runtime_tests)

    add_executable(collision_world_tests
        tests/collision_world_test.cpp
    )

    target_link_libraries(collision_world_tests PRIVATE
        simcore_collision
    )

    add_test(NAME collision_world_tests COMMAND collision_world_tests)

    add_executable(map_package_static_collision_tests
        tests/map_package_static_collision_test.cpp
    )

    target_link_libraries(map_package_static_collision_tests PRIVATE
        simcore_collision
        simcore_physics
    )

    target_compile_definitions(map_package_static_collision_tests PRIVATE
        SIMCORE_TEST_MAP_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../map_packages/wall_broad_v1"
    )

    add_test(
        NAME map_package_static_collision_tests
        COMMAND map_package_static_collision_tests
    )

    add_executable(vehicle_protocol_tests
        tests/vehicle_protocol_test.cpp
    )

    target_link_libraries(vehicle_protocol_tests PRIVATE
        simcore_protocol
    )

    add_test(NAME vehicle_protocol_tests COMMAND vehicle_protocol_tests)

    add_executable(simulation_clock_tests
        tests/simulation_clock_test.cpp
    )

    target_include_directories(simulation_clock_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME simulation_clock_tests COMMAND simulation_clock_tests)

    add_executable(control_lease_tests
        tests/control_lease_test.cpp
    )

    target_include_directories(control_lease_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME control_lease_tests COMMAND control_lease_tests)

    add_executable(body_frame_adapter_tests
        tests/body_frame_adapter_test.cpp
    )

    target_include_directories(body_frame_adapter_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME body_frame_adapter_tests COMMAND body_frame_adapter_tests)

    add_executable(simulation_host_tests
        tests/simulation_host_test.cpp
    )

    target_link_libraries(simulation_host_tests PRIVATE
        simcore_host_core
    )

    add_test(NAME simulation_host_tests COMMAND simulation_host_tests)

    add_executable(websocket_server_tests
        tests/websocket_server_test.cpp
    )

    target_link_libraries(websocket_server_tests PRIVATE
        simcore_websocket
    )

    add_test(NAME websocket_server_tests COMMAND websocket_server_tests)
    set_tests_properties(websocket_server_tests PROPERTIES TIMEOUT 15)
endif()
