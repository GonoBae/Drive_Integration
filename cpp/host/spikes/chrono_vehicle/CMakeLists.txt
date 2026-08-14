cmake_minimum_required(VERSION 3.20)

project(chrono_vehicle_spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Chrono REQUIRED COMPONENTS Vehicle CONFIG)

add_executable(chrono_vehicle_spike main.cpp)

target_compile_definitions(chrono_vehicle_spike PRIVATE
    SPIKE_CHRONO_DATA_DIR="${CHRONO_DATA_DIR}"
    SPIKE_CHRONO_VEHICLE_DATA_DIR="${CHRONO_VEHICLE_DATA_DIR}"
)

if(CHRONO_TARGETS)
    target_link_libraries(chrono_vehicle_spike PRIVATE ${CHRONO_TARGETS})
else()
    target_include_directories(chrono_vehicle_spike PRIVATE ${CHRONO_INCLUDE_DIRS})
    target_compile_options(chrono_vehicle_spike PRIVATE ${CHRONO_CXX_FLAGS})
    target_link_options(chrono_vehicle_spike PRIVATE ${CHRONO_LINKER_FLAGS})
    target_link_libraries(chrono_vehicle_spike PRIVATE ${CHRONO_LIBRARIES})
endif()

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
if(COMMAND add_DLL_copy_command)
    add_DLL_copy_command("${CHRONO_DLLS}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME chrono_vehicle_repeatability_smoke
        COMMAND chrono_vehicle_spike --duration=2 --runs=2 --vehicles=1
    )
    add_test(
        NAME chrono_vehicle_collision_smoke
        COMMAND chrono_vehicle_spike --duration=8 --runs=2 --vehicles=1 --wall-x=35
    )
endif()
