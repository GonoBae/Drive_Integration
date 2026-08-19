cmake_minimum_required(VERSION 3.21)
project(SimCorePublisher CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

set(SIMCORE_PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../protocol")
set(SIMCORE_VEHICLE_PROTO "${SIMCORE_PROTOCOL_DIR}/vehicle.proto")

# Protobuf
find_package(protobuf CONFIG REQUIRED)

# Boost (Beast WebSocket)
find_package(Boost CONFIG REQUIRED COMPONENTS system)

# ZMQ
find_package(ZeroMQ CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)

add_library(simcore_protocol
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

add_library(simcore_physics
    src/physics/vehicle_physics.cpp
)

target_include_directories(simcore_physics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(simcore_websocket
    src/websocket/ws_server.cpp
)

target_include_directories(simcore_websocket PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(simcore_websocket PUBLIC
    Boost::system
)

add_library(simcore_zmq_publisher
    src/publisher/zmq_publisher.cpp
)

target_include_directories(simcore_zmq_publisher PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(simcore_zmq_publisher PUBLIC
    cppzmq
)

add_library(simcore_host_core
    src/simulation_host.cpp
)

target_include_directories(simcore_host_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(simcore_host_core PUBLIC
    simcore_physics
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
    simcore_websocket
    simcore_zmq_publisher
)

if(WIN32)
    target_link_libraries(simcore_publisher PRIVATE winmm)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(vehicle_physics_tests
        tests/vehicle_physics_test.cpp
    )

    target_link_libraries(vehicle_physics_tests PRIVATE
        simcore_physics
    )

    add_test(NAME vehicle_physics_tests COMMAND vehicle_physics_tests)

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
endif()
