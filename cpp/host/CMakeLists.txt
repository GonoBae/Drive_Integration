cmake_minimum_required(VERSION 3.21)
project(SimCorePublisher CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

add_executable(simcore_publisher
    src/main.cpp
    src/physics/vehicle_physics.cpp
    src/websocket/ws_server.cpp
    src/publisher/zmq_publisher.cpp
)

target_include_directories(simcore_publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(simcore_publisher PRIVATE
    simcore_protocol
    Boost::system
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

    add_executable(vehicle_protocol_tests
        tests/vehicle_protocol_test.cpp
    )

    target_link_libraries(vehicle_protocol_tests PRIVATE
        simcore_protocol
    )

    add_test(NAME vehicle_protocol_tests COMMAND vehicle_protocol_tests)
endif()
