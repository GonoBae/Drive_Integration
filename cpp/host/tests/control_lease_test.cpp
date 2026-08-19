#include "control/control_lease.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace std::chrono_literals;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ControlCommandStamp command(std::string_view source, std::string_view session,
                            std::uint64_t sequence, std::uint64_t client_time_ns)
{
    return {source, session, sequence, client_time_ns};
}

void test_sequence_and_owner_rules()
{
    ControlLease lease(250ms, 100ms);
    const auto start = ControlLease::Clock::time_point{};

    require(lease.accept(command("manual", "session-a", 1, 1'000'000'000), start)
                == ControlLeaseDecision::Accepted,
            "first valid command must acquire the lease");
    require(lease.accept(command("manual", "session-a", 1, 1'000'000'000), start + 1ms)
                == ControlLeaseDecision::StaleSequence,
            "duplicate sequence must be rejected");
    require(lease.accept(command("autonomy", "session-b", 1, 2'000'000'000), start + 2ms)
                == ControlLeaseDecision::ActiveOwnerConflict,
            "a second controller must not preempt an active lease");
}

void test_stale_queued_packet_cannot_release_safe_stop()
{
    ControlLease lease(250ms, 100ms);
    const auto start = ControlLease::Clock::time_point{};
    require(lease.accept(command("manual", "session-a", 1, 1'000'000'000), start)
                == ControlLeaseDecision::Accepted,
            "initial command must be accepted");
    require(lease.update_timeout(start + 251ms),
            "250ms command timeout must enter SafeStop");

    require(lease.accept(command("manual", "session-a", 2, 1'010'000'000), start + 252ms)
                == ControlLeaseDecision::RetiredSession,
            "the timed-out session must be retired with its queued packets");
    require(lease.safe_stop_active(),
            "rejected queued input must leave SafeStop active");

    require(lease.accept(command("manual", "session-b", 1, 10'000'000), start + 260ms)
                == ControlLeaseDecision::Accepted,
            "a new connection session may re-arm control");
    require(!lease.safe_stop_active(),
            "fresh input must release SafeStop");
}

void test_new_session_can_acquire_only_after_timeout()
{
    ControlLease lease(250ms, 100ms);
    const auto start = ControlLease::Clock::time_point{};
    require(lease.accept(command("manual", "session-a", 9, 1'000'000'000), start)
                == ControlLeaseDecision::Accepted,
            "first session must acquire");
    require(lease.accept(command("manual", "session-b", 1, 10'000'000), start + 100ms)
                == ControlLeaseDecision::ActiveOwnerConflict,
            "reconnect session must wait for lease expiry");
    require(lease.update_timeout(start + 251ms), "lease must expire");
    require(lease.accept(command("manual", "session-b", 1, 20'000'000), start + 252ms)
                == ControlLeaseDecision::Accepted,
            "new process session must acquire after timeout");
    require(lease.accept(command("manual", "session-a", 10, 1'260'000'000), start + 253ms)
                == ControlLeaseDecision::RetiredSession,
            "packets from the replaced session must stay retired");
}

void test_queue_age_is_rejected_before_timeout()
{
    ControlLease lease(250ms, 100ms);
    const auto start = ControlLease::Clock::time_point{};
    require(lease.accept(command("manual", "session-a", 1, 1'000'000'000), start)
                == ControlLeaseDecision::Accepted,
            "initial command must be accepted");
    require(lease.accept(command("manual", "session-a", 2, 1'010'000'000), start + 150ms)
                == ControlLeaseDecision::ExcessiveQueueAge,
            "an aged FIFO packet must be rejected even before timeout");
    require(!lease.safe_stop_active(),
            "one rejected stale packet does not stop a still-valid lease early");
}

void test_required_identity_fields()
{
    ControlLease lease(250ms, 100ms);
    const auto now = ControlLease::Clock::time_point{};
    require(lease.accept(command("manual", "", 1, 1), now)
                == ControlLeaseDecision::MissingSession,
            "session id is required");
    require(lease.accept(command("manual", "session", 1, 0), now)
                == ControlLeaseDecision::MissingClientTime,
            "client monotonic timestamp is required");
}

void test_rejected_packet_does_not_poison_sequence_state()
{
    ControlLease lease(250ms, 100ms);
    const auto start = ControlLease::Clock::time_point{};
    require(lease.accept(command("manual", "session-a", 1, 1'000'000'000), start)
                == ControlLeaseDecision::Accepted,
            "initial command must be accepted");

    require(lease.accept(command("manual", "session-a", 100, 999'000'000), start + 1ms)
                == ControlLeaseDecision::ClientClockRegression,
            "regressed client time must be rejected");
    require(lease.accept(command("manual", "session-a", 2, 1'002'000'000), start + 2ms)
                == ControlLeaseDecision::Accepted,
            "a rejected high sequence must not poison the accepted high-water mark");
}

void test_rejected_queue_age_does_not_poison_sequence_state()
{
    ControlLease lease(250ms, 100ms);
    const auto start = ControlLease::Clock::time_point{};
    require(lease.accept(command("manual", "session-a", 1, 1'000'000'000), start)
                == ControlLeaseDecision::Accepted,
            "initial command must be accepted");

    require(lease.accept(command("manual", "session-a", 100, 1'010'000'000), start + 150ms)
                == ControlLeaseDecision::ExcessiveQueueAge,
            "queued command must be rejected");
    require(lease.accept(command("manual", "session-a", 2, 1'151'000'000), start + 151ms)
                == ControlLeaseDecision::Accepted,
            "queue-age rejection must not advance the sequence high-water mark");
}

} // namespace

int main()
{
    try {
        test_sequence_and_owner_rules();
        test_stale_queued_packet_cannot_release_safe_stop();
        test_new_session_can_acquire_only_after_timeout();
        test_queue_age_is_rejected_before_timeout();
        test_required_identity_fields();
        test_rejected_packet_does_not_poison_sequence_state();
        test_rejected_queue_age_does_not_poison_sequence_state();
        std::cout << "control_lease_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "control_lease_tests: " << error.what() << '\n';
        return 1;
    }
}
