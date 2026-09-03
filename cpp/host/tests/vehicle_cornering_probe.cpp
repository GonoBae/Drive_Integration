#include "physics/vehicle_config.hpp"
#include "physics/vehicle_physics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {
constexpr double dt = 1.0 / 60.0;
constexpr double degrees = 180.0 / std::numbers::pi_v<double>;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void validate(const VehicleState& s, const VehicleParameters& p) {
    require(std::isfinite(s.east) && std::isfinite(s.north)
        && std::isfinite(s.speed) && std::isfinite(s.yaw_rate), "non-finite physics");
    for (const auto& w : s.wheels) {
        require(std::hypot(w.longitudinal_force, w.lateral_force)
            <= p.tire_friction * w.normal_load * 1.01 + .01, "tire friction budget exceeded");
    }
}

struct Sample {
    double speed = 0, path_speed = 0, radius = 0, yaw = 0, beta = 0;
    double steer = 0, left = 0, right = 0;
    double front_slip = 0, rear_slip = 0, front_load = 0, rear_load = 0;
    double front_force = 0, rear_force = 0, roll = 0, utilization = 0;
    double pitch = 0, angular_y = 0, height = 0, navigation_yaw = 0;
    std::array<double, 4> spring{}, impulse{}, stop_used{}, loads{}, fy{}, slips{}, friction_peak{};
    int contacts = 4, count = 0;
    void add(const VehicleState& s, const VehicleState& before, double& last_course,
             const VehicleParameters& p,
             const std::array<WheelContactSupportDiagnostics, 4>& support) {
        validate(s, p);
        const double dx = s.east - before.east, dy = s.north - before.north;
        const double distance = std::hypot(dx, dy);
        const double course = std::atan2(dx, dy);
        const double turn = std::remainder(course - last_course,
            2.0 * std::numbers::pi_v<double>);
        last_course = course;
        speed += s.speed;
        path_speed += distance / dt;
        radius += distance / std::max(2.0 * std::sin(std::abs(turn) * .5), 1e-9);
        yaw += s.yaw_rate * degrees;
        navigation_yaw -= std::remainder(s.heading - before.heading, 360.0) / dt;
        pitch = std::max(pitch, std::abs(static_cast<double>(s.pitch)));
        angular_y = std::max(angular_y, std::abs(s.angular_velocity_body.y) * degrees);
        height += s.position_enu.z;
        beta += std::atan2(s.linear_velocity_body.y, s.linear_velocity_body.x) * degrees;
        steer += s.steering_angle * degrees;
        left += s.wheels[0].steering_angle * degrees;
        right += s.wheels[1].steering_angle * degrees;
        roll = std::max(roll, std::abs(static_cast<double>(s.roll)));
        int current_contacts = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& w = s.wheels[i];
            spring[i] += support[i].suspension_normal_force_n;
            impulse[i] += support[i].hard_stop_impulse_n_s;
            stop_used[i] += support[i].tire_hard_stop_normal_force_n;
            loads[i] += w.normal_load;
            fy[i] += w.lateral_force;
            slips[i] += w.slip_angle * degrees;
            friction_peak[i] = std::max<double>(friction_peak[i],
                std::hypot(w.longitudinal_force, w.lateral_force)
                / std::max(p.tire_friction * w.normal_load, 1.f));
            if (w.in_contact) ++current_contacts;
            if (i < 2) {
                front_slip += std::abs(w.slip_angle) * degrees * .5;
                front_load += w.normal_load;
                front_force += w.lateral_force;
            } else {
                rear_slip += std::abs(w.slip_angle) * degrees * .5;
                rear_load += w.normal_load;
                rear_force += w.lateral_force;
            }
            utilization = std::max<double>(utilization,
                std::hypot(w.longitudinal_force, w.lateral_force)
                / std::max(p.tire_friction * w.normal_load, 1.f));
        }
        contacts = std::min(contacts, current_contacts);
        ++count;
    }
};

void output(std::ostream& out, const std::string& profile, const std::string& mode,
            float target, float fraction, double entry, double elapsed, double heading,
            const Sample& s) {
    const double n = s.count;
    out << profile << ',' << mode << ',' << target * 3.6 << ',' << fraction << ','
        << entry * 3.6 << ',' << elapsed << ',' << s.speed / n * 3.6 << ','
        << s.path_speed / n * 3.6 << ',' << s.radius / n << ',' << s.yaw / n << ','
        << heading << ',' << s.steer / n << ',' << s.left / n << ',' << s.right / n << ','
        << s.beta / n << ',' << s.front_slip / n << ',' << s.rear_slip / n << ','
        << s.front_load / n << ',' << s.rear_load / n << ',' << s.front_force / n << ','
        << s.rear_force / n << ',' << s.utilization << ',' << s.roll << ',' << s.contacts << ','
        << s.navigation_yaw / n << ',' << s.pitch << ',' << s.angular_y << ',' << s.height / n;
    for (std::size_t i = 0; i < 4; ++i) {
        out << ',' << s.spring[i] / n << ',' << s.impulse[i] / n << ',' << s.stop_used[i] / n
            << ',' << s.loads[i] / n << ',' << s.fy[i] / n << ',' << s.slips[i] / n
            << ',' << s.friction_peak[i];
    }
    out << '\n';
}

void run(std::ostream& out, const std::string& profile, const VehicleParameters& p,
         float target, float fraction, const std::string& mode) {
    // Infinite default flat ground isolates tire dynamics from finite map edges.
    // All state is reached through pedals and fixed ticks; never inject pose/speed.
    VehiclePhysics vehicle(0, 0, 0, 0, p);
    VehicleInput input;
    input.throttle = 1;
    vehicle.set_input(input);
    auto state = vehicle.get_state();
    int entry_ticks = 0;
    while (state.speed < target && entry_ticks++ < 12000) state = vehicle.update(dt);
    require(state.speed >= target - .01, "requested entry speed was not reached");
    const double entry = state.speed;
    if (target >= p.max_forward_speed_mps - .01f) {
        require(state.speed >= p.max_forward_speed_mps - .01f,
            "maximum-speed case must reach configured maximum, not merely stay below it");
    }
    double integral = 0;
    const auto tick = [&]() {
        if (mode == "cruise") {
            const double error = target - state.speed;
            integral = std::clamp(integral + error * dt, -1.0, 4.0);
            const double pedal = .06 + .45 * error + .20 * integral;
            input.throttle = static_cast<float>(std::clamp(pedal, 0.0, 1.0));
            input.brake = static_cast<float>(std::clamp(-pedal * .2, 0.0, .25));
        }
        vehicle.set_input(input);
        state = vehicle.update(dt);
    };
    if (mode == "cruise") for (int i = 0; i < 1200; ++i) tick();
    input.steering = fraction;
    if (mode == "drive-coast" || mode == "neutral") input.throttle = 0;
    if (mode == "neutral") input.gear = VehicleGear::Neutral;
    double course = 0, heading_change = 0;
    Sample sample;
    const int total_ticks = mode == "cruise" ? 1200 : 180;
    for (int i = 0; i < total_ticks; ++i) {
        const auto previous = state;
        tick();
        validate(state, p);
        if (i >= 20 && std::abs(fraction) == 1.f) {
            require(std::abs(state.steering_angle - fraction * p.max_steering_angle_rad) < 1e-6,
                "maximum lock must remain independent of speed and pedal mode");
        }
        heading_change -= std::remainder(state.heading - previous.heading, 360.0);
        if ((mode == "cruise" && i >= total_ticks - 180)
            || (mode != "cruise" && (i % 60) >= 30)) {
            sample.add(state, previous, course, p, vehicle.get_wheel_contact_support_diagnostics());
        } else {
            course = std::atan2(state.east - previous.east, state.north - previous.north);
        }
        if (mode != "cruise" && (i + 1) % 60 == 0) {
            output(out, profile, mode, target, fraction, entry, (i + 1) * dt, heading_change, sample);
            sample = {};
        }
    }
    if (mode == "cruise") output(out, profile, mode, target, fraction, entry, total_ticks * dt,
        heading_change, sample);
}

double navigation_yaw_rate(const VehicleState& state) {
    const double roll = state.roll / degrees;
    const double pitch = state.pitch / degrees;
    return (state.angular_velocity_body.y * std::sin(roll)
        + state.angular_velocity_body.z * std::cos(roll)) / std::cos(pitch);
}

// Tick-level force accounting for the deliberately saturated maximum-speed
// steering step. Different integration steps distinguish numerical growth from
// a transient of the actual tire model. Entry speed is reached using pedals.
void run_yaw_transient(std::ostream& out, const VehicleParameters& p,
                       const std::string& mode, double step_dt, float direction) {
    VehiclePhysics vehicle(0, 0, 0, 0, p);
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    auto state = vehicle.get_state();
    const int maximum_entry_steps = static_cast<int>(240.0 / step_dt);
    for (int i = 0; i < maximum_entry_steps && state.speed < p.max_forward_speed_mps; ++i)
        state = vehicle.update(step_dt);
    require(state.speed >= p.max_forward_speed_mps,
        "yaw transient must enter at the configured maximum speed");
    input.steering = direction;
    if (mode != "drive-full") input.throttle = 0.f;
    if (mode == "neutral") input.gear = VehicleGear::Neutral;
    vehicle.set_input(input);
    double heading_change = 0.0;
    const int total_steps = static_cast<int>(std::lround(4.0 / step_dt));
    for (int tick = 0; tick < total_steps; ++tick) {
        const auto previous = state;
        state = vehicle.update(step_dt);
        validate(state, p);
        const auto support = vehicle.get_wheel_contact_support_diagnostics();
        const double before_yaw = navigation_yaw_rate(previous);
        const double after_yaw = navigation_yaw_rate(state);
        const double heading_step = -std::remainder(state.heading - previous.heading, 360.0);
        heading_change += heading_step;
        double moment = 0.0, lateral_power = 0.0, wheel_energy = 0.0;
        std::array<double, 4> moments{}, lateral_velocities{};
        for (std::size_t index = 0; index < 4; ++index) {
            const auto& wheel = state.wheels[index];
            const double x = p.wheelbase_m * (index < 2
                ? 1.0 - p.front_static_load_fraction : -p.front_static_load_fraction);
            const double y = (index % 2 == 0 ? 0.5 : -0.5)
                * (index < 2 ? p.front_track_m : p.rear_track_m);
            const double cosine = std::cos(wheel.steering_angle);
            const double sine = std::sin(wheel.steering_angle);
            const double body_fx = cosine * wheel.longitudinal_force - sine * wheel.lateral_force;
            const double body_fy = sine * wheel.longitudinal_force + cosine * wheel.lateral_force;
            moments[index] = x * body_fy - y * body_fx;
            moment += moments[index];
            lateral_velocities[index] = -sine * (previous.linear_velocity_body.x - before_yaw * y)
                + cosine * (previous.linear_velocity_body.y + before_yaw * x);
            lateral_power += wheel.lateral_force * lateral_velocities[index];
            wheel_energy += .5 * p.wheel_inertia_kg_m2 * wheel.angular_speed * wheel.angular_speed;
        }
        const double planar_energy = .5 * p.mass_kg
            * (state.linear_velocity_body.x * state.linear_velocity_body.x
                + state.linear_velocity_body.y * state.linear_velocity_body.y)
            + .5 * p.yaw_inertia_kg_m2 * after_yaw * after_yaw;
        out << mode << ',' << step_dt << ',' << direction << ',' << (tick + 1) * step_dt
            << ',' << state.speed * 3.6 << ',' << state.linear_velocity_body.y
            << ',' << std::atan2(state.linear_velocity_body.y, state.linear_velocity_body.x) * degrees
            << ',' << state.steering_angle * degrees << ',' << after_yaw * degrees
            << ',' << heading_step / step_dt << ',' << heading_change << ',' << moment
            << ',' << p.yaw_inertia_kg_m2 * (after_yaw - before_yaw) / step_dt
            << ',' << lateral_power << ',' << planar_energy << ',' << wheel_energy
            << ',' << state.pitch << ',' << state.roll << ',' << state.position_enu.z;
        for (std::size_t index = 0; index < 4; ++index) {
            const auto& wheel = state.wheels[index];
            out << ',' << wheel.normal_load << ',' << support[index].suspension_normal_force_n
                << ',' << support[index].hard_stop_impulse_n_s / step_dt
                << ',' << support[index].tire_hard_stop_normal_force_n
                << ',' << wheel.longitudinal_force << ',' << wheel.lateral_force
                << ',' << wheel.slip_angle * degrees << ',' << wheel.longitudinal_slip
                << ',' << wheel.angular_speed << ',' << moments[index]
                << ',' << lateral_velocities[index];
        }
        out << '\n';
    }
}

// Human-facing turn timing for distinguishing 50 km/h from the configured
// 50 m/s maximum. The vehicle is accelerated with public pedal input, then a
// full-lock step is applied while full throttle remains held. No pose, speed,
// yaw, brake, or steering state is injected.
void run_step_turn(std::ostream& out, const VehicleParameters& p,
                   double entry_target_mps, double step_dt) {
    VehiclePhysics vehicle(0, 0, 0, 0, p);
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    auto state = vehicle.get_state();
    const int maximum_entry_steps = static_cast<int>(240.0 / step_dt);
    for (int i = 0; i < maximum_entry_steps && state.speed < entry_target_mps; ++i)
        state = vehicle.update(step_dt);
    require(state.speed >= entry_target_mps - .01,
        "step turn must reach requested entry speed");

    const double entry_speed = state.speed;
    input.steering = 1.f;
    vehicle.set_input(input);
    double elapsed = 0.0, heading_change = 0.0, path_length = 0.0;
    double rack_time = -1.0, ninety_time = -1.0, speed_at_ninety = 0.0;
    double peak_front_slip = 0.0, peak_rear_slip = 0.0;
    double peak_abs_yaw_rate = 0.0, yaw_rate_at_ninety = 0.0;
    double peak_braking_mps2 = 0.0;
    constexpr double maximum_observation_seconds = 12.0;
    const int observation_steps = static_cast<int>(std::lround(maximum_observation_seconds / step_dt));
    for (int tick = 0; tick < observation_steps; ++tick) {
        const auto previous = state;
        state = vehicle.update(step_dt);
        validate(state, p);
        elapsed += step_dt;
        const double heading_step = -std::remainder(state.heading - previous.heading, 360.0);
        heading_change += heading_step;
        path_length += std::hypot(state.east - previous.east, state.north - previous.north);
        const double yaw_rate = navigation_yaw_rate(state) * degrees;
        peak_abs_yaw_rate = std::max(peak_abs_yaw_rate, std::abs(yaw_rate));
        peak_braking_mps2 = std::max(peak_braking_mps2,
            std::max(0.0, (static_cast<double>(previous.speed) - state.speed) / step_dt));
        peak_front_slip = std::max(peak_front_slip,
            .5 * (std::abs(state.wheels[0].slip_angle) + std::abs(state.wheels[1].slip_angle)) * degrees);
        peak_rear_slip = std::max(peak_rear_slip,
            .5 * (std::abs(state.wheels[2].slip_angle) + std::abs(state.wheels[3].slip_angle)) * degrees);
        if (rack_time < 0.0
            && std::abs(state.steering_angle - p.max_steering_angle_rad) < 1e-6)
            rack_time = elapsed;
        if (ninety_time < 0.0 && heading_change >= 90.0) {
            ninety_time = elapsed;
            speed_at_ninety = state.speed;
            yaw_rate_at_ninety = yaw_rate;
            break;
        }
    }
    const double achieved_radians = std::abs(heading_change) / degrees;
    const double effective_radius = achieved_radians > 1e-9 ? path_length / achieved_radians : 0.0;
    out << step_dt << ',' << entry_target_mps << ',' << entry_target_mps * 3.6 << ','
        << entry_speed << ',' << entry_speed * 3.6 << ',' << rack_time << ',' << ninety_time << ','
        << heading_change << ',' << path_length << ',' << effective_radius << ','
        << peak_abs_yaw_rate << ',' << yaw_rate_at_ninety << ',' << state.speed << ','
        << state.speed * 3.6 << ',' << speed_at_ninety * 3.6 << ',' << peak_front_slip << ','
        << peak_rear_slip << ',' << peak_braking_mps2 << ",1,0\n";
}
}

int main(int argc, char** argv) {
    try {
        constexpr const char* usage =
            "usage: vehicle_cornering_probe [--yaw-transient|--step-turn] <new-report.csv>";
        if (argc == 2) {
            const std::string argument = argv[1];
            if (argument == "--help" || argument == "-h") {
                std::cout << usage << '\n';
                return 0;
            }
        }
        const bool yaw_transient = argc == 3 && std::string(argv[1]) == "--yaw-transient";
        const bool step_turn = argc == 3 && std::string(argv[1]) == "--step-turn";
        require(argc == 2 || yaw_transient || step_turn, usage);
        const std::filesystem::path output_path = argv[(yaw_transient || step_turn) ? 2 : 1];
        require(!std::filesystem::exists(output_path), "report already exists; choose a new path");
        std::ofstream report(output_path);
        require(report.good(), "cannot create report");
        const auto loaded = simcore_host::load_vehicle_parameters(SIMCORE_TEST_VEHICLE_CONFIG_PATH);
        if (step_turn) {
            report << std::setprecision(12)
                << "dt_s,target_mps,target_kph,entry_mps,entry_kph,rack_time_s,time_to_90deg_s,"
                   "heading_change_deg,path_length_m,effective_radius_m,peak_abs_yaw_deg_s,"
                   "yaw_at_90_deg_s,final_mps,final_kph,speed_at_90_kph,peak_front_slip_deg,"
                   "peak_rear_slip_deg,peak_deceleration_mps2,throttle_input,brake_input\n";
            for (const double step : {1.0 / 60.0, 1.0 / 120.0, 1.0 / 240.0}) {
                run_step_turn(report, loaded.parameters, 50.0 / 3.6, step);
                run_step_turn(report, loaded.parameters, 50.0, step);
            }
            report.flush();
            require(report.good(), "step turn report write failed");
            std::cout << "PASS step turn: 50 km/h and 50 m/s at 60/120/240 Hz; "
                << output_path.string() << '\n';
            return 0;
        }
        if (yaw_transient) {
            report << std::setprecision(12)
                << "mode,dt_s,input,time_s,longitudinal_kph,vy_mps,beta_deg,steering_deg,"
                   "nav_yaw_deg_s,heading_rate_deg_s,heading_change_deg,tire_yaw_moment_nm,"
                   "integrated_yaw_moment_nm,lateral_slip_power_w,planar_energy_j,wheel_energy_j,"
                   "pitch_deg,roll_deg,height_m";
            for (const char* wheel : {"fl", "fr", "rl", "rr"}) {
                for (const char* value : {"load_n", "spring_n", "stop_reaction_n", "stop_used_n",
                        "fx_n", "fy_n", "slip_deg", "long_slip", "omega_rad_s", "yaw_moment_nm", "vy_mps"})
                    report << ',' << wheel << '_' << value;
            }
            report << '\n';
            for (const double step : {1.0 / 60.0, 1.0 / 120.0, 1.0 / 240.0})
                for (const std::string mode : {"drive-full", "drive-coast", "neutral"})
                    for (const float direction : {-1.f, 1.f})
                        run_yaw_transient(report, loaded.parameters, mode, step, direction);
            report.flush();
            require(report.good(), "yaw transient report write failed");
            std::cout << "PASS yaw transient: 18 scenarios, per-tick forces at 60/120/240 Hz; "
                << output_path.string() << '\n';
            return 0;
        }
        report << std::setprecision(9)
            << "profile,mode,target_kph,input,entry_kph,elapsed_s,longitudinal_kph,path_kph,"
               "radius_m,yaw_deg_s,heading_change_deg,centre_deg,front_left_deg,front_right_deg,"
               "body_beta_deg,front_slip_deg,rear_slip_deg,front_load_n,rear_load_n,front_fy_n,"
               "rear_fy_n,peak_friction_utilization,peak_roll_deg,min_contacts,"
               "navigation_yaw_deg_s,peak_pitch_deg,peak_body_y_rate_deg_s,height_m";
        for (const char* wheel : {"fl", "fr", "rl", "rr"}) {
            for (const char* value : {"spring_n", "stop_impulse_ns", "stop_used_n", "load_n",
                                      "fy_n", "slip_deg", "peak_utilization"}) {
                report << ',' << wheel << '_' << value;
            }
        }
        report << '\n';
        for (const std::string profile : {"equal55", "mild60_50", "neutral60_5_49_5"}) {
            auto p = loaded.parameters;
            p.front_tire_corner_stiffness_n_rad = profile == "equal55" ? 55000.f
                : profile == "mild60_50" ? 60000.f : 60500.f;
            p.rear_tire_corner_stiffness_n_rad = 110000.f - p.front_tire_corner_stiffness_n_rad;
            for (const float speed : {3.f, 5.f, 30.f / 3.6f, 40.f / 3.6f, 50.f / 3.6f, 25.f, 50.f}) {
                for (const float fraction : {.015f, .15f, 1.f}) {
                    run(report, profile, p, speed, fraction, "cruise");
                }
                for (const std::string mode : {"drive-full", "drive-coast", "neutral"}) {
                    for (const float direction : {-1.f, 1.f}) run(report, profile, p, speed, direction, mode);
                }
            }
        }
        report.flush();
        require(report.good(), "report write failed");
        std::cout << "PASS cornering probe: 189 scenarios, 441 measured rows, public pedal-only physics; "
            << output_path.string() << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "vehicle_cornering_probe: " << e.what() << '\n';
        return 1;
    }
}
