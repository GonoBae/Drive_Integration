#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "chrono/ChVersion.h"
#if CH_VERSION >= 0x00100000
#include "chrono/core/ChDataPath.h"
#include "chrono_vehicle/ChVehicleDataPath.h"
#else
#include "chrono/core/ChGlobal.h"
#include "chrono_vehicle/ChVehicleModelData.h"
#endif
#include "chrono/physics/ChBodyEasy.h"
#include "chrono/physics/ChSystemSMC.h"
#include "chrono_models/vehicle/sedan/Sedan.h"
#include "chrono_vehicle/ChWorldFrame.h"
#include "chrono_vehicle/terrain/RigidTerrain.h"

namespace {

using Clock = std::chrono::steady_clock;
using chrono::ChContactMaterialData;
using chrono::ChContactMethod;
using chrono::ChCoordsys;
using chrono::QUNIT;
#if CH_VERSION >= 0x00100000
using Vector = chrono::ChVector3d;
using Quaternion = chrono::ChQuaterniond;
#else
using Vector = chrono::ChVector<>;
using Quaternion = chrono::ChQuaternion<>;
#endif
using chrono::vehicle::CollisionType;
using chrono::vehicle::DriverInputs;
using chrono::vehicle::RigidTerrain;
using chrono::vehicle::TireModelType;
#if CH_VERSION >= 0x00100000
using chrono::VisualizationType;
#else
using chrono::vehicle::VisualizationType;
#endif
using chrono::vehicle::sedan::Sedan;

struct Options {
    double duration_seconds = 12.0;
    double step_seconds = 1.0 / 600.0;
    int runs = 3;
    int vehicles = 1;
    double wall_x = 0.0;
    double minimum_realtime_factor = 0.0;
    bool has_wall = false;
};

struct RunResult {
    double wall_seconds = 0.0;
    double simulated_seconds = 0.0;
    std::uint64_t steps = 0;
    Vector position;
    Quaternion rotation;
    double speed = 0.0;
};

double parse_positive_double(const std::string& value, const char* option) {
    const double parsed = std::stod(value);
    if (!std::isfinite(parsed) || parsed <= 0.0) {
        throw std::invalid_argument(std::string(option) + " must be positive");
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument.rfind("--duration=", 0) == 0) {
            options.duration_seconds = parse_positive_double(argument.substr(11), "duration");
        } else if (argument.rfind("--step=", 0) == 0) {
            options.step_seconds = parse_positive_double(argument.substr(7), "step");
        } else if (argument.rfind("--runs=", 0) == 0) {
            options.runs = std::stoi(argument.substr(7));
            if (options.runs <= 0) {
                throw std::invalid_argument("runs must be positive");
            }
        } else if (argument.rfind("--vehicles=", 0) == 0) {
            options.vehicles = std::stoi(argument.substr(11));
            if (options.vehicles <= 0) {
                throw std::invalid_argument("vehicles must be positive");
            }
        } else if (argument.rfind("--wall-x=", 0) == 0) {
            options.wall_x = parse_positive_double(argument.substr(9), "wall-x");
            options.has_wall = true;
        } else if (argument.rfind("--min-rtf=", 0) == 0) {
            options.minimum_realtime_factor = parse_positive_double(argument.substr(10), "min-rtf");
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    return options;
}

DriverInputs make_inputs(double time, double duration, bool collision_smoke_test) {
    DriverInputs inputs{0.0, 0.0, 0.0};

    const double acceleration_end = duration * 0.70;
    const double braking_start = duration * 0.85;

    if (collision_smoke_test) {
        inputs.m_throttle = std::min(time / 1.0, 0.65);
        return inputs;
    }

    if (time < acceleration_end) {
        inputs.m_throttle = std::min(time / 1.0, 0.65);
    } else if (time >= braking_start) {
        inputs.m_braking = 0.75;
    } else {
        inputs.m_throttle = 0.25;
    }

    if (time >= duration * 0.30 && time < duration * 0.50) {
        inputs.m_steering = 0.12;
    } else if (time >= duration * 0.50 && time < duration * 0.70) {
        inputs.m_steering = -0.12;
    }

    return inputs;
}

RunResult run_once(const Options& options) {
    chrono::ChSystemSMC system;
#if CH_VERSION >= 0x00100000
    system.SetCollisionSystemType(chrono::ChCollisionSystem::Type::BULLET);
    system.SetGravitationalAcceleration(-9.81 * chrono::vehicle::ChWorldFrame::Vertical());
#else
    system.SetCollisionSystemType(chrono::collision::ChCollisionSystemType::BULLET);
    system.Set_G_acc(-9.81 * chrono::vehicle::ChWorldFrame::Vertical());
#endif
#if CH_VERSION >= 0x00100000
    system.GetSolver()->AsIterative()->SetMaxIterations(150);
#else
    system.SetSolverMaxIterations(150);
#endif
    system.SetMaxPenetrationRecoverySpeed(4.0);
    std::vector<std::unique_ptr<Sedan>> sedans;
    sedans.reserve(static_cast<std::size_t>(options.vehicles));

    for (int vehicle_index = 0; vehicle_index < options.vehicles; ++vehicle_index) {
        auto sedan = std::make_unique<Sedan>(&system);
        sedan->SetContactMethod(ChContactMethod::SMC);
        sedan->SetChassisCollisionType(CollisionType::PRIMITIVES);
        sedan->SetChassisFixed(false);
        sedan->SetInitPosition(ChCoordsys<>(Vector(0.0, vehicle_index * 4.0, 1.0), QUNIT));
        sedan->SetTireType(TireModelType::TMEASY);
        sedan->SetTireStepSize(std::min(options.step_seconds, 0.001));
        sedan->Initialize();

        sedan->SetChassisVisualizationType(VisualizationType::NONE);
        sedan->SetSuspensionVisualizationType(VisualizationType::NONE);
        sedan->SetSteeringVisualizationType(VisualizationType::NONE);
        sedan->SetWheelVisualizationType(VisualizationType::NONE);
        sedan->SetTireVisualizationType(VisualizationType::NONE);
        sedans.push_back(std::move(sedan));
    }

    RigidTerrain terrain(&system);
    ChContactMaterialData material_data;
    material_data.mu = 0.9f;
    material_data.cr = 0.01f;
    material_data.Y = 2.0e7f;
    auto material = material_data.CreateMaterial(ChContactMethod::SMC);
    terrain.AddPatch(material, ChCoordsys<>(Vector(50.0, 0.0, 0.0), QUNIT), 200.0, 100.0);
    terrain.Initialize();

    if (options.has_wall) {
        auto wall = chrono_types::make_shared<chrono::ChBodyEasyBox>(
            0.5, std::max(20.0, options.vehicles * 4.0 + 4.0), 2.0, 1000.0, false, true, material);
#if CH_VERSION >= 0x00100000
        wall->SetFixed(true);
#else
        wall->SetBodyFixed(true);
#endif
        wall->SetPos(Vector(options.wall_x, (options.vehicles - 1) * 2.0, 1.0));
        system.Add(wall);
    }

    const auto wall_start = Clock::now();
    std::uint64_t step_count = 0;

    while (system.GetChTime() + options.step_seconds * 0.5 < options.duration_seconds) {
        const double time = system.GetChTime();
        const DriverInputs inputs = make_inputs(time, options.duration_seconds, options.has_wall);

        terrain.Synchronize(time);
        for (auto& sedan : sedans) {
            sedan->Synchronize(time, inputs, terrain);
        }
        terrain.Advance(options.step_seconds);
        for (auto& sedan : sedans) {
            sedan->Advance(options.step_seconds);
        }
        system.DoStepDynamics(options.step_seconds);
        ++step_count;
    }

    const auto wall_end = Clock::now();

    RunResult result;
    result.wall_seconds = std::chrono::duration<double>(wall_end - wall_start).count();
    result.simulated_seconds = system.GetChTime();
    result.steps = step_count;
    result.position = sedans.front()->GetVehicle().GetPos();
    result.rotation = sedans.front()->GetVehicle().GetRot();
    result.speed = sedans.front()->GetVehicle().GetSpeed();
    return result;
}

double position_delta(const RunResult& lhs, const RunResult& rhs) {
    return (lhs.position - rhs.position).Length();
}

double quaternion_delta(const RunResult& lhs, const RunResult& rhs) {
    const double dot = std::abs(lhs.rotation.e0() * rhs.rotation.e0() + lhs.rotation.e1() * rhs.rotation.e1() +
                                lhs.rotation.e2() * rhs.rotation.e2() + lhs.rotation.e3() * rhs.rotation.e3());
    return 2.0 * std::acos(std::clamp(dot, 0.0, 1.0));
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);

        chrono::SetChronoDataPath(SPIKE_CHRONO_DATA_DIR);
#if CH_VERSION >= 0x00100000
        chrono::vehicle::SetVehicleDataPath(SPIKE_CHRONO_VEHICLE_DATA_DIR);
#else
        chrono::vehicle::SetDataPath(SPIKE_CHRONO_VEHICLE_DATA_DIR);
#endif

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "chrono_vehicle_spike chrono_version=" << CHRONO_VERSION
                  << " duration_s=" << options.duration_seconds << " step_s=" << options.step_seconds
                  << " runs=" << options.runs << " vehicles=" << options.vehicles;
        if (options.has_wall) {
            std::cout << " wall_x_m=" << options.wall_x;
        }
        if (options.minimum_realtime_factor > 0.0) {
            std::cout << " min_realtime_factor=" << options.minimum_realtime_factor;
        }
        std::cout << '\n';

        std::vector<RunResult> results;
        results.reserve(static_cast<std::size_t>(options.runs));
        double minimum_realtime_factor = std::numeric_limits<double>::infinity();

        for (int run = 0; run < options.runs; ++run) {
            results.push_back(run_once(options));
            const RunResult& result = results.back();
            const double realtime_factor = result.simulated_seconds / result.wall_seconds;
            minimum_realtime_factor = std::min(minimum_realtime_factor, realtime_factor);
            const double average_step_us = result.wall_seconds * 1.0e6 / static_cast<double>(result.steps);

            std::cout << "run=" << (run + 1) << " wall_s=" << result.wall_seconds
                      << " simulated_s=" << result.simulated_seconds << " steps=" << result.steps
                      << " realtime_factor=" << realtime_factor << " avg_step_us=" << average_step_us
                      << " final_position_m=[" << result.position.x() << ',' << result.position.y() << ','
                      << result.position.z() << "] final_speed_mps=" << result.speed << '\n';
        }

        double maximum_position_delta = 0.0;
        double maximum_rotation_delta = 0.0;
        for (std::size_t index = 1; index < results.size(); ++index) {
            maximum_position_delta = std::max(maximum_position_delta, position_delta(results.front(), results[index]));
            maximum_rotation_delta =
                std::max(maximum_rotation_delta, quaternion_delta(results.front(), results[index]));
        }

        std::cout << "repeatability max_position_delta_m=" << maximum_position_delta
                  << " max_rotation_delta_rad=" << maximum_rotation_delta << '\n';

        constexpr double kMaximumRepeatPositionDelta = 0.01;
        constexpr double kMaximumRepeatRotationDelta = 0.0017453292519943296;
        if (maximum_position_delta > kMaximumRepeatPositionDelta ||
            maximum_rotation_delta > kMaximumRepeatRotationDelta) {
            throw std::runtime_error("repeatability validation failed");
        }

        if (options.has_wall) {
            for (const RunResult& result : results) {
                if (result.position.x() > options.wall_x + 0.5 || std::abs(result.speed) > 0.5) {
                    throw std::runtime_error("collision validation failed: vehicle crossed the wall or did not stop");
                }
            }
        }

        if (options.minimum_realtime_factor > 0.0 &&
            minimum_realtime_factor < options.minimum_realtime_factor) {
            throw std::runtime_error("performance validation failed: minimum real-time factor not met");
        }

        std::cout << "validation status=passed minimum_realtime_factor=" << minimum_realtime_factor << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "chrono_vehicle_spike error: " << error.what() << '\n';
        return 1;
    }
}
