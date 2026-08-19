#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_set>

struct ControlCommandStamp {
    std::string_view source_id;
    std::string_view session_id;
    std::uint64_t sequence = 0;
    std::uint64_t client_time_ns = 0;
};

enum class ControlLeaseDecision {
    Accepted,
    MissingSource,
    MissingSession,
    MissingClientTime,
    StaleSequence,
    ClientClockRegression,
    ActiveOwnerConflict,
    RetiredSession,
    ExcessiveQueueAge,
};

inline const char* control_lease_decision_message(ControlLeaseDecision decision)
{
    switch (decision) {
    case ControlLeaseDecision::Accepted:              return "accepted";
    case ControlLeaseDecision::MissingSource:         return "missing source_id";
    case ControlLeaseDecision::MissingSession:        return "missing session_id";
    case ControlLeaseDecision::MissingClientTime:     return "missing client_time_ns";
    case ControlLeaseDecision::StaleSequence:         return "stale sequence";
    case ControlLeaseDecision::ClientClockRegression: return "client clock regressed";
    case ControlLeaseDecision::ActiveOwnerConflict:   return "another control lease is active";
    case ControlLeaseDecision::RetiredSession:        return "control session was retired";
    case ControlLeaseDecision::ExcessiveQueueAge:     return "control packet exceeded queue-age limit";
    }
    return "unknown control lease decision";
}

class ControlLease {
public:
    using Clock = std::chrono::steady_clock;

    ControlLease(std::chrono::nanoseconds timeout,
                 std::chrono::nanoseconds max_queue_age)
        : timeout_(timeout)
        , max_queue_age_(max_queue_age)
    {
        if (timeout_ <= std::chrono::nanoseconds::zero()) {
            throw std::invalid_argument("Control lease timeout must be positive");
        }
        if (max_queue_age_ < std::chrono::nanoseconds::zero()) {
            throw std::invalid_argument("Maximum command queue age cannot be negative");
        }
    }

    ControlLeaseDecision accept(const ControlCommandStamp& command,
                                Clock::time_point received_at)
    {
        if (command.source_id.empty()) {
            return ControlLeaseDecision::MissingSource;
        }
        if (command.session_id.empty()) {
            return ControlLeaseDecision::MissingSession;
        }
        if (command.client_time_ns == 0) {
            return ControlLeaseDecision::MissingClientTime;
        }
        if (command.sequence == 0) {
            return ControlLeaseDecision::StaleSequence;
        }

        const std::string key = make_key(command.source_id, command.session_id);
        const bool same_session = key == active_key_;
        if (!same_session) {
            if (retired_sessions_.contains(key)) {
                return ControlLeaseDecision::RetiredSession;
            }
            if (!active_key_.empty() && !safe_stop_active_) {
                return ControlLeaseDecision::ActiveOwnerConflict;
            }

        }

        // Validation is deliberately side-effect free. A rejected packet must
        // never advance the high-water marks and poison a later valid packet.
        const std::uint64_t previous_sequence = same_session ? highest_sequence_ : 0;
        const std::uint64_t previous_client_time_ns = same_session ? last_client_time_ns_ : 0;
        const Clock::time_point previous_receive_time = same_session
            ? last_receive_time_
            : Clock::time_point::min();

        if (command.sequence <= previous_sequence) {
            return ControlLeaseDecision::StaleSequence;
        }

        if (previous_receive_time != Clock::time_point::min()) {
            if (command.client_time_ns < previous_client_time_ns) {
                return ControlLeaseDecision::ClientClockRegression;
            }

            const auto server_elapsed = received_at - previous_receive_time;
            const auto client_elapsed = std::chrono::nanoseconds(
                command.client_time_ns - previous_client_time_ns);
            const auto estimated_queue_age = server_elapsed > client_elapsed
                ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                      server_elapsed - client_elapsed)
                : std::chrono::nanoseconds::zero();
            if (estimated_queue_age > max_queue_age_) {
                return ControlLeaseDecision::ExcessiveQueueAge;
            }
        }

        // Commit the ownership and ordering state only after every check has
        // accepted the command.
        if (!same_session) {
            if (!active_key_.empty()) {
                retired_sessions_.insert(active_key_);
            }
            active_key_ = key;
            active_source_id_ = std::string(command.source_id);
            active_session_id_ = std::string(command.session_id);
        }
        highest_sequence_ = command.sequence;
        last_client_time_ns_ = command.client_time_ns;
        last_receive_time_ = received_at;
        safe_stop_active_ = false;
        return ControlLeaseDecision::Accepted;
    }

    bool update_timeout(Clock::time_point now)
    {
        if (safe_stop_active_ || last_receive_time_ == Clock::time_point::min()) {
            return false;
        }
        if (now - last_receive_time_ <= timeout_) {
            return false;
        }
        safe_stop_active_ = true;
        retired_sessions_.insert(active_key_);
        active_key_.clear();
        return true;
    }

    bool safe_stop_active() const { return safe_stop_active_; }
    std::string_view active_source_id() const { return active_source_id_; }
    std::string_view active_session_id() const { return active_session_id_; }

    std::chrono::nanoseconds command_age(Clock::time_point now) const
    {
        if (last_receive_time_ == Clock::time_point::min()) {
            return std::chrono::nanoseconds::max();
        }
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - last_receive_time_);
    }

private:
    static std::string make_key(std::string_view source_id,
                                std::string_view session_id)
    {
        std::string key;
        key.reserve(source_id.size() + session_id.size() + 1);
        key.append(source_id);
        key.push_back('\0');
        key.append(session_id);
        return key;
    }

    std::chrono::nanoseconds timeout_;
    std::chrono::nanoseconds max_queue_age_;
    std::string active_key_;
    std::string active_source_id_;
    std::string active_session_id_;
    std::unordered_set<std::string> retired_sessions_;
    std::uint64_t highest_sequence_ = 0;
    std::uint64_t last_client_time_ns_ = 0;
    Clock::time_point last_receive_time_ = Clock::time_point::min();
    bool safe_stop_active_ = true;
};
