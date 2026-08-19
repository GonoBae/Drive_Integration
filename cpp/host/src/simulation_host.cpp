#include "simulation_host.hpp"

#include <boost/asio/error.hpp>

#include <cmath>
#include <iostream>
#include <utility>

SimulationHost::SimulationHost(boost::asio::io_context& ioc,
                               SimulationHostConfig config,
                               SimulationHostCallbacks callbacks)
    : config_(std::move(config))
    , callbacks_(std::move(callbacks))
    , physics_(config_.origin_lat, config_.origin_lon, config_.origin_alt,
               config_.spawn_heading)
    , simulation_clock_(config_.physics_frequency_hz)
    , control_lease_(config_.command_timeout, config_.max_command_queue_age)
    , timer_(ioc)
{
    last_logged_input_ = make_safe_stop_input();
    physics_.set_input(last_logged_input_);
}

SimulationHost::~SimulationHost()
{
    stop();
    lifetime_token_.reset();
}

void SimulationHost::start()
{
    if (running_) {
        return;
    }
    running_ = true;
    schedule_tick();
}

void SimulationHost::stop()
{
    if (!running_) {
        return;
    }
    running_ = false;
    try {
        timer_.cancel();
    } catch (const boost::system::system_error& error) {
        std::cerr << "[Timing] timer cancellation failed: "
                  << error.what() << "\n";
    }
}

ControlMessageResult SimulationHost::handle_control_message(
    std::string_view message, Clock::time_point received_at)
{
    std::string error;
    auto command = simcore_host::parse_control_command_envelope(message, &error);
    if (!command) {
        std::cerr << "[Input] " << error << "\n";
        return ControlMessageResult::Rejected;
    }
    if (command->map_package_checksum != config_.map_package_checksum) {
        std::cerr << "[Input] map package checksum mismatch\n";
        return ControlMessageResult::Rejected;
    }
    if (command->estop) {
        estop_latched_ = true;
        last_logged_input_ = make_safe_stop_input();
        physics_.set_input(last_logged_input_);
        std::cerr << "[Safety] E-stop latched by source="
                  << command->source_id << " session=" << command->session_id << "\n";
        return ControlMessageResult::EmergencyStopLatched;
    }
    if (estop_latched_) {
        std::cerr << "[Safety] E-stop is latched; restart is required to reset\n";
        return ControlMessageResult::Rejected;
    }

    const bool was_safe_stopped = control_lease_.safe_stop_active();
    const auto decision = control_lease_.accept(
        {command->source_id, command->session_id, command->sequence,
         command->client_time_ns},
        received_at);
    if (decision != ControlLeaseDecision::Accepted) {
        std::cerr << "[Input] rejected source=" << command->source_id
                  << " session=" << command->session_id
                  << " sequence=" << command->sequence << ": "
                  << control_lease_decision_message(decision) << "\n";
        return ControlMessageResult::Rejected;
    }
    if (was_safe_stopped) {
        std::cout << "[Safety] control lease armed source="
                  << command->source_id << " session=" << command->session_id << "\n";
    }

    physics_.set_input(command->input);
    const bool changed = std::abs(last_logged_input_.throttle - command->input.throttle) > 0.001f
        || std::abs(last_logged_input_.brake - command->input.brake) > 0.001f
        || std::abs(last_logged_input_.steering - command->input.steering) > 0.001f
        || last_logged_input_.handbrake != command->input.handbrake
        || last_logged_input_.gear != command->input.gear;
    if (changed) {
        last_logged_input_ = command->input;
        last_control_change_time_ = received_at;
        ++control_change_revision_;
        std::cout << "[Input] change revision=" << control_change_revision_
                  << " sequence=" << command->sequence
                  << " throttle=" << command->input.throttle
                  << " brake=" << command->input.brake
                  << " steering=" << command->input.steering
                  << " handbrake=" << command->input.handbrake << "\n";
    }
    return ControlMessageResult::Accepted;
}

std::string SimulationHost::make_initial_world_state()
{
    return simcore_host::serialize_world_state_envelope(
        physics_.get_state(), make_metadata());
}

VehicleInput SimulationHost::make_safe_stop_input()
{
    VehicleInput input;
    input.brake = 1.f;
    input.handbrake = true;
    return input;
}

simcore_host::EnvelopeMetadata SimulationHost::make_metadata()
{
    return {
        message_sequence_++,
        simulation_clock_.simulation_time_ns(),
        config_.source_id,
        config_.map_package_checksum,
    };
}

void SimulationHost::schedule_tick()
{
    timer_.expires_at(simulation_clock_.next_deadline());
    std::weak_ptr<int> lifetime = lifetime_token_;
    timer_.async_wait([this, lifetime](boost::system::error_code ec) {
        if (lifetime.expired() || ec == boost::asio::error::operation_aborted) {
            return;
        }
        if (ec) {
            std::cerr << "[Timing] timer error: " << ec.message() << "\n";
            running_ = false;
            return;
        }

        run_tick();
        if (running_) {
            schedule_tick();
        }
    });
}

void SimulationHost::run_tick()
{
    const auto started_at = Clock::now();
    if (control_lease_.update_timeout(started_at)) {
        const auto command_age = control_lease_.command_age(started_at);
        const auto command_age_ms = std::chrono::duration<double, std::milli>(
            command_age).count();
        last_logged_input_ = make_safe_stop_input();
        physics_.set_input(last_logged_input_);
        std::cerr << "[Safety] command timeout age_ms=" << command_age_ms
                  << " source=" << control_lease_.active_source_id()
                  << " session=" << control_lease_.active_session_id()
                  << "; SafeStop applied\n";
        if (callbacks_.close_control_connections) {
            callbacks_.close_control_connections(
                "control lease timed out; reconnect required");
        }
    }

    const auto state = physics_.update(1.0 / config_.physics_frequency_hz);
    if (reported_control_change_revision_ != control_change_revision_) {
        const auto command_to_tick = std::chrono::duration<double, std::milli>(
            started_at - last_control_change_time_).count();
        reported_control_change_revision_ = control_change_revision_;
        std::cout << "[Latency] input revision=" << control_change_revision_
                  << " applied_to_tick_ms=" << command_to_tick << "\n";
    }

    simulation_clock_.advance(Clock::now());
    if (simulation_clock_.overrun_count() != last_reported_overrun_count_) {
        last_reported_overrun_count_ = simulation_clock_.overrun_count();
        std::cerr << "[Timing] tick overrun count="
                  << last_reported_overrun_count_ << "\n";
    }

    if (callbacks_.broadcast_world_state) {
        callbacks_.broadcast_world_state(
            simcore_host::serialize_world_state_envelope(state, make_metadata()));
    }
    if (callbacks_.publish_observer_state) {
        callbacks_.publish_observer_state(
            simcore_host::serialize_entity_state_packet(state));
    }
}
