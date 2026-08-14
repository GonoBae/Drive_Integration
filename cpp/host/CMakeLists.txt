cmake_minimum_required(VERSION 3.16)
project(SimCorePublisher CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SIMCORE_PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../protocol")
set(SIMCORE_VEHICLE_PROTO "${SIMCORE_PROTOCOL_DIR}/vehicle.proto")

# Protobuf
find_package(protobuf CONFIG REQUIRED)

# Boost (Beast WebSocket + JSON)
find_package(Boost REQUIRED COMPONENTS system json)

# ZMQ
find_package(ZeroMQ CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)

add_executable(simcore_publisher
    src/main.cpp
    src/physics/vehicle_physics.cpp
    src/websocket/ws_server.cpp
    src/publisher/zmq_publisher.cpp
)

protobuf_generate(
    TARGET simcore_publisher
    PROTOS "${SIMCORE_VEHICLE_PROTO}"
    LANGUAGE cpp
    IMPORT_DIRS "${SIMCORE_PROTOCOL_DIR}"
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

target_include_directories(simcore_publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(simcore_publisher PRIVATE
    protobuf::libprotobuf
    Boost::system
    Boost::json
    cppzmq
)

include(CTest)

if(BUILD_TESTING)
    add_executable(vehicle_physics_tests
        tests/vehicle_physics_test.cpp
        src/physics/vehicle_physics.cpp
    )

    target_include_directories(vehicle_physics_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME vehicle_physics_tests COMMAND vehicle_physics_tests)
endif()
