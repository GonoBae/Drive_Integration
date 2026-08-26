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
                == ControlLeaseDecision::ExcessiveQueueAge,
            "an old queued packet must not release the soft SafeStop");
    require(lease.safe_stop_active(),
            "rejected queued input must leave SafeStop active");

    require(lease.accept(command("manual", "session-a", 3, 1'253'000'000),
                         start + 253ms)
                == ControlLeaseDecision::Accepted,
            "a fresh command from the retained session must re-arm control");
    require(!lease.safe_stop_active(),
            "fresh input must release SafeStop");
}

void test_new_session_can_acquire_only_after_hard_timeout()
{
    ControlLease lease(250ms, 100ms);
    const auto start = ControlLease::Clock::time_point{};
    require(lease.accept(command("manual", "session-a", 9, 1'000'000'000), start)
                == ControlLeaseDecision::Accepted,
            "first session must acquire");
    require(lease.accept(command("manual", "session-b", 1, 10'000'000), start + 100ms)
                == ControlLeaseDecision::ActiveOwnerConflict,
            "reconnect session must wait for lease expiry");
    require(lease.update_timeout(start + 251ms),
            "soft timeout must enter SafeStop");
    require(lease.accept(command("manual", "session-b", 1, 20'000'000),
                         start + 252ms)
                == ControlLeaseDecision::ActiveOwnerConflict,
            "soft timeout must retain the original session owner");
    require(!lease.update_hard_timeout(start + 1s),
            "hard timeout is strict and must not fire at exactly one second");
    require(lease.update_hard_timeout(start + 1001ms),
            "hard timeout must retire the stale owner after one second");
    require(lease.accept(command("manual", "session-b", 1, 20'000'000),
                         start + 1002ms)
                == ControlLeaseDecision::Accepted,
            "new process session must acquire after hard timeout");
    require(lease.accept(command("manual", "session-a", 10, 1'260'000'000),
                         start + 1003ms)
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

void test_new_simulation_retires_the_previous_control_session()
{
    ControlLease lease(250ms, 100ms);
    const auto start = ControlLease::Clock::time_point{};
    require(lease.accept(command("manual", "session-a", 7, 1'000'000'000), start)
                == ControlLeaseDecision::Accepted,
            "test setup must acquire the original PIE lease");

    lease.reset_for_new_simulation();

    require(lease.safe_stop_active(),
            "a newly reset simulation must wait in SafeStop for fresh control");
    require(lease.active_source_id().empty()
            && lease.active_session_id().empty(),
            "reset must release the previous active owner");
    require(lease.accept(command("manual", "session-a", 8, 1'010'000'000),
                         start + 1ms)
                == ControlLeaseDecision::RetiredSession,
            "delayed control from the previous PIE must stay fenced off");
    require(lease.accept(command("manual", "session-b", 1, 2'000'000'000),
                         start + 2ms)
                == ControlLeaseDecision::Accepted,
            "the new PIE connection must acquire control without a timeout wait");
}

void test_same_pie_reconnect_hands_over_without_reusing_retired_sessions()
{
    ControlLease lease(250ms, 100ms);
    const auto start = ControlLease::Clock::time_point{};
    require(lease.accept(command("manual", "socket-a", 3, 1'000'000'000), start)
                == ControlLeaseDecision::Accepted,
            "test setup must acquire the pre-reconnect lease");

    require(lease.reset_for_reconnect("manual", "socket-b")
                == ControlLeaseResetDecision::Ready,
            "a fresh socket in the same PIE must be prepared for handover");
    require(lease.safe_stop_active(),
            "handover must apply SafeStop until the new socket sends control");
    require(lease.accept(command("manual", "socket-a", 4, 1'010'000'000),
                         start + 1ms)
                == ControlLeaseDecision::RetiredSession,
            "queued control from the disconnected socket must be rejected");
    require(lease.accept(command("manual", "socket-b", 2, 2'000'000'000),
                         start + 2ms)
                == ControlLeaseDecision::Accepted,
            "the reconnect socket must acquire immediately after handover");
    require(lease.reset_for_reconnect("manual", "socket-a")
                == ControlLeaseResetDecision::RetiredSession,
            "an old socket must never be reusable as a reconnect target");
    require(lease.reset_for_reconnect("manual", "socket-b")
                == ControlLeaseResetDecision::SameSession,
            "repeating handover for the active socket must be idempotent");
}

void test_retired_new_simulation_candidate_is_side_effect_free()
{
    ControlLease lease(250ms, 100ms);
    const auto start = ControlLease::Clock::time_point{};
    require(lease.begin_new_simulation("manual", "socket-a")
                == ControlLeaseResetDecision::Ready,
            "first lifecycle session must be prepared");
    require(lease.accept(command("manual", "socket-a", 2, 1'000'000'000), start)
                == ControlLeaseDecision::Accepted,
            "first lifecycle session must acquire control");
    require(lease.reset_for_reconnect("manual", "socket-b")
                == ControlLeaseResetDecision::Ready,
            "fresh reconnect must retire socket-a");
    require(lease.accept(command("manual", "socket-b", 2, 2'000'000'000),
                         start + 1ms)
                == ControlLeaseDecision::Accepted,
            "socket-b must become the active owner");

    require(lease.begin_new_simulation("manual", "socket-a")
                == ControlLeaseResetDecision::RetiredSession,
            "retired candidate must be rejected before lifecycle mutation");
    require(!lease.safe_stop_active()
            && lease.active_session_id() == "socket-b",
            "rejected candidate must leave the current owner and safety state intact");
    require(lease.accept(command("manual", "socket-b", 3, 2'001'000'000),
                         start + 2ms)
                == ControlLeaseDecision::Accepted,
            "current owner must remain valid after rejected lifecycle candidate");
}

} // namespace

int main()
{
    try {
        test_sequence_and_owner_rules();
        test_stale_queued_packet_cannot_release_safe_stop();
        test_new_session_can_acquire_only_after_hard_timeout();
        test_queue_age_is_rejected_before_timeout();
        test_required_identity_fields();
        test_rejected_packet_does_not_poison_sequence_state();
        test_rejected_queue_age_does_not_poison_sequence_state();
        test_new_simulation_retires_the_previous_control_session();
        test_same_pie_reconnect_hands_over_without_reusing_retired_sessions();
        test_retired_new_simulation_candidate_is_side_effect_free();
        std::cout << "control_lease_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "control_lease_tests: " << error.what() << '\n';
        return 1;
    }
}
