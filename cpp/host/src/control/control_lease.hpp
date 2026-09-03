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

enum class ControlLeaseResetDecision {
    Ready,
    SameSession,
    RetiredSession,
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

    ControlLease(
        std::chrono::nanoseconds timeout,
        std::chrono::nanoseconds max_queue_age,
        std::chrono::nanoseconds hard_timeout = std::chrono::seconds(1))
        : timeout_(timeout)
        , hard_timeout_(hard_timeout)
        , max_queue_age_(max_queue_age)
    {
        if (timeout_ <= std::chrono::nanoseconds::zero()) {
            throw std::invalid_argument("Control lease timeout must be positive");
        }
        if (hard_timeout_ <= timeout_) {
            throw std::invalid_argument(
                "Hard control lease timeout must exceed the SafeStop timeout");
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
            // A soft timeout keeps the current connection/session owner. Only
            // an explicit lifecycle handover or the hard timeout may replace
            // it, so an unrelated source cannot exploit SafeStop to preempt.
            if (!active_key_.empty()) {
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

    // The soft deadline applies SafeStop without retiring the session. A
    // fresh, ordered command from the same owner may recover immediately;
    // queued commands still have to pass the queue-age validation in accept().
    bool update_timeout(Clock::time_point now)
    {
        if (safe_stop_active_ || last_receive_time_ == Clock::time_point::min()) {
            return false;
        }
        if (now - last_receive_time_ <= timeout_) {
            return false;
        }
        safe_stop_active_ = true;
        return true;
    }

    // The hard deadline is the connection-fencing boundary. It deliberately
    // follows the soft SafeStop deadline so a transient editor/transport hitch
    // cannot force a reconnect while motion has already been made safe.
    bool update_hard_timeout(Clock::time_point now)
    {
        if (last_receive_time_ == Clock::time_point::min()
            || active_key_.empty()
            || now - last_receive_time_ <= hard_timeout_) {
            return false;
        }
        safe_stop_active_ = true;
        retired_sessions_.insert(active_key_);
        active_key_.clear();
        return true;
    }

    // Start a new authoritative simulation while permanently fencing off the
    // previous controller session. Retired sessions are deliberately kept so
    // delayed packets from an earlier PIE run cannot re-arm the vehicle.
    ControlLeaseResetDecision begin_new_simulation(
        std::string_view source_id,
        std::string_view session_id)
    {
        const std::string key = make_key(source_id, session_id);
        if (retired_sessions_.contains(key)) {
            return ControlLeaseResetDecision::RetiredSession;
        }

        // Validate the candidate before changing any current ownership. This
        // makes a rejected lifecycle packet strictly side-effect free.
        if (!active_key_.empty() && active_key_ != key) {
            retired_sessions_.insert(active_key_);
        }
        if (!reset_session_key_.empty() && reset_session_key_ != key) {
            retired_sessions_.insert(reset_session_key_);
        }
        active_key_.clear();
        active_source_id_.clear();
        active_session_id_.clear();
        highest_sequence_ = 0;
        last_client_time_ns_ = 0;
        last_receive_time_ = Clock::time_point::min();
        safe_stop_active_ = true;
        reset_session_key_ = key;
        return ControlLeaseResetDecision::Ready;
    }

    void reset_for_new_simulation()
    {
        if (!active_key_.empty()) {
            retired_sessions_.insert(active_key_);
        }
        if (!reset_session_key_.empty()) {
            retired_sessions_.insert(reset_session_key_);
        }
        active_key_.clear();
        reset_session_key_.clear();
        active_source_id_.clear();
        active_session_id_.clear();
        highest_sequence_ = 0;
        last_client_time_ns_ = 0;
        last_receive_time_ = Clock::time_point::min();
        safe_stop_active_ = true;
    }

    // A reconnect within the same PIE run changes Envelope.session_id but must
    // not reset physics. Retire the former connection so the new one can take
    // the lease immediately, while refusing any already-retired connection.
    ControlLeaseResetDecision reset_for_reconnect(
        std::string_view source_id,
        std::string_view session_id)
    {
        const std::string key = make_key(source_id, session_id);
        if (retired_sessions_.contains(key)) {
            return ControlLeaseResetDecision::RetiredSession;
        }
        if (key == reset_session_key_) {
            return ControlLeaseResetDecision::SameSession;
        }
        if (!reset_session_key_.empty()) {
            retired_sessions_.insert(reset_session_key_);
        }
        if (!active_key_.empty()) {
            retired_sessions_.insert(active_key_);
        }
        active_key_.clear();
        active_source_id_.clear();
        active_session_id_.clear();
        highest_sequence_ = 0;
        last_client_time_ns_ = 0;
        last_receive_time_ = Clock::time_point::min();
        safe_stop_active_ = true;
        reset_session_key_ = key;
        return ControlLeaseResetDecision::Ready;
    }

    bool safe_stop_active() const { return safe_stop_active_; }
    bool has_control_command() const {
        return last_receive_time_ != Clock::time_point::min();
    }
    // Only hard expiry clears an acquired owner while retaining its command
    // timestamp. Lifecycle reset/reconnect clears both, so waiting for a first
    // command is distinguishable from a retired control connection.
    bool requires_reconnect() const {
        return has_control_command() && active_key_.empty();
    }
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
    std::chrono::nanoseconds hard_timeout_;
    std::chrono::nanoseconds max_queue_age_;
    std::string active_key_;
    std::string reset_session_key_;
    std::string active_source_id_;
    std::string active_session_id_;
    std::unordered_set<std::string> retired_sessions_;
    std::uint64_t highest_sequence_ = 0;
    std::uint64_t last_client_time_ns_ = 0;
    Clock::time_point last_receive_time_ = Clock::time_point::min();
    bool safe_stop_active_ = true;
};
