#include "SimCoreClientComponent.h"
#include "SimCoreProtocol.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#include <limits>

namespace
{
class FHealthTestSocket final : public IWebSocket
{
public:
	bool bConnected = true;
	void Connect() override { bConnected = true; }
	void Close(int32 Code = 1000, const FString& Reason = FString()) override { bConnected = false; }
	bool IsConnected() override { return bConnected; }
	void Send(const FString& Data) override {}
	void Send(const void* Data, SIZE_T Size, bool bIsBinary = false) override {}
	void SetTextMessageMemoryLimit(uint64 Limit) override {}
	FWebSocketConnectedEvent& OnConnected() override { return Connected; }
	FWebSocketConnectionErrorEvent& OnConnectionError() override { return ConnectionError; }
	FWebSocketClosedEvent& OnClosed() override { return Closed; }
	FWebSocketMessageEvent& OnMessage() override { return Message; }
	FWebSocketBinaryMessageEvent& OnBinaryMessage() override { return BinaryMessage; }
	FWebSocketRawMessageEvent& OnRawMessage() override { return RawMessage; }
	FWebSocketMessageSentEvent& OnMessageSent() override { return MessageSent; }

private:
	FWebSocketConnectedEvent Connected;
	FWebSocketConnectionErrorEvent ConnectionError;
	FWebSocketClosedEvent Closed;
	FWebSocketMessageEvent Message;
	FWebSocketBinaryMessageEvent BinaryMessage;
	FWebSocketRawMessageEvent RawMessage;
	FWebSocketMessageSentEvent MessageSent;
};

void AppendVarint(TArray<uint8>& Bytes, uint64 Value)
{
	while (Value >= 0x80)
	{
		Bytes.Add(static_cast<uint8>(Value) | 0x80);
		Value >>= 7;
	}
	Bytes.Add(static_cast<uint8>(Value));
}

void AppendInteger(TArray<uint8>& Bytes, uint32 Field, uint64 Value)
{
	AppendVarint(Bytes, static_cast<uint64>(Field) << 3);
	AppendVarint(Bytes, Value);
}

void AppendMessage(TArray<uint8>& Bytes, uint32 Field, const TArray<uint8>& Message)
{
	AppendVarint(Bytes, (static_cast<uint64>(Field) << 3) | 2);
	AppendVarint(Bytes, static_cast<uint64>(Message.Num()));
	Bytes.Append(Message);
}

void AppendText(TArray<uint8>& Bytes, uint32 Field, const FString& Value)
{
	FTCHARToUTF8 Text(*Value);
	TArray<uint8> Payload;
	Payload.Append(reinterpret_cast<const uint8*>(Text.Get()), Text.Length());
	AppendMessage(Bytes, Field, Payload);
}

template <typename T> void AppendNumber(TArray<uint8>& Bytes, uint32 Field, T Value)
{
	static_assert(sizeof(T) == 4 || sizeof(T) == 8);
	AppendVarint(Bytes, (static_cast<uint64>(Field) << 3) | (sizeof(T) == 4 ? 5 : 1));
	uint64 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(T));
	for (uint32 Index = 0; Index < sizeof(T); ++Index)
	{
		Bytes.Add(static_cast<uint8>(Bits >> (Index * 8)));
	}
}

TArray<uint8> MakeHealth(
	const FString& Status,
	uint64 Overruns = 7,
	uint64 CommandAgeNs = 345'000'000,
	const FString& Message = TEXT("Command lease timed out; braking applied"),
	uint64 HasCommand = 1)
{
	TArray<uint8> Health;
	AppendText(Health, 1, Status);
	AppendInteger(Health, 2, Overruns);
	AppendInteger(Health, 3, CommandAgeNs);
	AppendText(Health, 4, Message);
	AppendInteger(Health, 5, HasCommand);
	return Health;
}

TArray<uint8> MakeWorldEnvelope(
	const TArray<uint8>* Health,
	bool bHealthFirst = false,
	bool bDuplicateHealth = false)
{
	TArray<uint8> World;
	if (Health && bHealthFirst) AppendMessage(World, 2, *Health);
	for (uint32 EntityId : {1u, 2u})
	{
		TArray<uint8> Entity;
		AppendInteger(Entity, 1, EntityId);
		AppendMessage(World, 1, Entity);
	}
	if (Health && !bHealthFirst) AppendMessage(World, 2, *Health);
	if (Health && bDuplicateHealth) AppendMessage(World, 2, *Health);
	TArray<uint8> Envelope;
	AppendInteger(Envelope, 1, SimCoreProtocol::SchemaVersion);
	AppendInteger(Envelope, 2, 22);
	AppendInteger(Envelope, 3, 8'000);
	AppendText(Envelope, 5, TEXT("fnv1a64:0123456789abcdef"));
	AppendText(Envelope, 7, TEXT("health-test-play-session"));
	AppendMessage(Envelope, 12, World);
	return Envelope;
}

TArray<uint8> MakeVehicleHudEnvelope(uint64 Sequence, const FString& Status,
	bool bHealthPresent, float SpeedMps, SimCoreProtocol::EVehicleGear Gear,
	float SteeringRadians, float YawRateRadians,
	const FString& PlaySession = TEXT("debug-hud-play"))
{
	TArray<uint8> Entity;
	AppendInteger(Entity, 1, 1);
	AppendNumber(Entity, 9, SpeedMps);
	AppendNumber(Entity, 15, YawRateRadians);
	AppendNumber(Entity, 16, SteeringRadians);
	AppendInteger(Entity, 17, static_cast<uint8>(Gear));
	AppendInteger(Entity, 22, static_cast<uint8>(SimCoreProtocol::EEntityKind::EgoVehicle));
	AppendNumber(Entity, 28, 25.0f);
	AppendNumber(Entity, 29, 9'125.0f);
	AppendInteger(Entity, 30, static_cast<uint8>(SimCoreProtocol::EVehicleDamageZone::Front));
	AppendInteger(Entity, 31, static_cast<uint32>(Sequence));
	TArray<uint8> World;
	AppendMessage(World, 1, Entity);
	if (bHealthPresent)
	{
		const TArray<uint8> Health = MakeHealth(Status, 0, 0, TEXT("HUD test"), 1);
		AppendMessage(World, 2, Health);
	}
	TArray<uint8> Envelope;
	AppendInteger(Envelope, 1, SimCoreProtocol::SchemaVersion);
	AppendInteger(Envelope, 2, Sequence);
	AppendInteger(Envelope, 3, Sequence * 1'000);
	AppendText(Envelope, 5, TEXT("fnv1a64:0123456789abcdef"));
	AppendText(Envelope, 7, PlaySession);
	AppendMessage(Envelope, 12, World);
	return Envelope;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreWorldStateHealthTest,
	"DriveIntegration.Protocol.WorldStateHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreWorldStateHealthTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	bool bSuccess = true;
	FVehicleState State;
	TArray<FVehicleState> Entities;
	FString Error;
	const TArray<uint8> Health = MakeHealth(TEXT("safe_stop"));
	for (bool bHealthFirst : {false, true})
	{
		const TArray<uint8> Envelope = MakeWorldEnvelope(&Health, bHealthFirst);
		bSuccess &= TestTrue(TEXT("Full WorldState+Health wire parses in either field order"),
			ParseWorldStateEnvelope(Envelope, 2, State, Entities, Error));
		bSuccess &= TestTrue(TEXT("Target and all entities retain atomic Health"),
			State.EntityId == 2 && State.ServerHealth.bPresent && Entities.Num() == 2
			&& Entities[0].ServerHealth.Status == EServerHealthStatus::SafeStop
			&& Entities[1].ServerHealth.Status == EServerHealthStatus::SafeStop);
		bSuccess &= TestTrue(TEXT("Health fields and envelope identity are preserved"),
			State.ServerHealth.TickOverrunCount == 7
			&& State.ServerHealth.LastCommandAgeNs == 345'000'000
			&& State.ServerHealth.bHasControlCommand
			&& State.ServerHealth.Message == TEXT("Command lease timed out; braking applied")
			&& State.Sequence == 22 && State.SimulationTimeNs == 8'000
			&& State.PlaySessionId == TEXT("health-test-play-session"));
	}
	const TArray<uint8> LegacyEnvelope = MakeWorldEnvelope(nullptr);
	bSuccess &= TestTrue(TEXT("Older schema-v2 WorldState without Health is accepted"),
		ParseWorldStateEnvelope(LegacyEnvelope, 1, State, Error));
	bSuccess &= TestTrue(TEXT("Omitted Health is explicitly absent, never Active"),
		!State.ServerHealth.bPresent && State.ServerHealth.Status == EServerHealthStatus::Unknown);
	const TArray<uint8> WaitingHealth = MakeHealth(TEXT("awaiting_control"), 0, 0,
		TEXT("No control received"), 0);
	const TArray<uint8> WaitingEnvelope = MakeWorldEnvelope(&WaitingHealth);
	bSuccess &= TestTrue(TEXT("No command is distinguishable from a zero-age accepted command"),
		ParseWorldStateEnvelope(WaitingEnvelope, 1, State, Error)
		&& !State.ServerHealth.bHasControlCommand && State.ServerHealth.LastCommandAgeNs == 0);

	const TCHAR* Statuses[] = {TEXT("awaiting_reset"), TEXT("awaiting_control"),
		TEXT("active"), TEXT("safe_stop"), TEXT("reconnect_required"), TEXT("estop_latched")};
	const EServerHealthStatus Expected[] = {EServerHealthStatus::AwaitingReset,
		EServerHealthStatus::AwaitingControl, EServerHealthStatus::Active,
		EServerHealthStatus::SafeStop, EServerHealthStatus::ReconnectRequired,
		EServerHealthStatus::EstopLatched};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Statuses); ++Index)
	{
		const TArray<uint8> StatusHealth = MakeHealth(Statuses[Index]);
		const TArray<uint8> Envelope = MakeWorldEnvelope(&StatusHealth);
		bSuccess &= TestTrue(TEXT("Every defined Health status maps exactly"),
			ParseWorldStateEnvelope(Envelope, 1, State, Error)
			&& State.ServerHealth.Status == Expected[Index]);
	}
	for (const TCHAR* Status : {TEXT("future_status"), TEXT("ACTIVE"), TEXT("")})
	{
		const TArray<uint8> UnknownHealth = MakeHealth(Status);
		const TArray<uint8> Envelope = MakeWorldEnvelope(&UnknownHealth);
		bSuccess &= TestTrue(TEXT("Unknown, missing, or case-mismatched status remains Unknown"),
			ParseWorldStateEnvelope(Envelope, 1, State, Error)
			&& State.ServerHealth.bPresent && State.ServerHealth.Status == EServerHealthStatus::Unknown);
	}
	TArray<uint8> MaximumHealth = MakeHealth(TEXT("safe_stop"), MAX_uint32, MAX_uint64,
		TEXT("line one\nline two\r\ttail"));
	AppendInteger(MaximumHealth, 99, 123); // Future additive field.
	const TArray<uint8> MaximumEnvelope = MakeWorldEnvelope(&MaximumHealth);
	bSuccess &= TestTrue(TEXT("Maximum unsigned fields and additive fields are supported"),
		ParseWorldStateEnvelope(MaximumEnvelope, 1, State, Error)
		&& State.ServerHealth.TickOverrunCount == MAX_uint32
		&& State.ServerHealth.LastCommandAgeNs == MAX_uint64);
	bSuccess &= TestEqual(TEXT("Server reason cannot insert diagnostic HUD lines"),
		State.ServerHealth.Message, FString(TEXT("line one line two  tail")));
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreWorldStateHealthValidationTest,
	"DriveIntegration.Protocol.WorldStateHealthValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreWorldStateHealthValidationTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	bool bSuccess = true;
	FVehicleState State;
	FString Error;
	const auto RejectHealth = [&](const TCHAR* Description, const TArray<uint8>& Health)
	{
		const TArray<uint8> Envelope = MakeWorldEnvelope(&Health);
		return TestFalse(Description, ParseWorldStateEnvelope(Envelope, 1, State, Error))
			&& TestFalse(TEXT("Rejected Health never leaves an output snapshot"), State.ServerHealth.bPresent);
	};
	bSuccess &= RejectHealth(TEXT("Overrun uint32 overflow is rejected"),
		MakeHealth(TEXT("active"), static_cast<uint64>(MAX_uint32) + 1));
	bSuccess &= RejectHealth(TEXT("Invalid boolean is rejected"),
		MakeHealth(TEXT("active"), 0, 0, TEXT(""), 2));
	bSuccess &= RejectHealth(TEXT("Oversized status is rejected"),
		MakeHealth(FString::ChrN(65, TEXT('x'))));
	bSuccess &= RejectHealth(TEXT("Oversized reason is rejected"),
		MakeHealth(TEXT("active"), 0, 0, FString::ChrN(513, TEXT('x'))));
	TArray<uint8> WrongWire;
	AppendInteger(WrongWire, 1, 3);
	bSuccess &= RejectHealth(TEXT("Status with wrong wire type is rejected"), WrongWire);
	TArray<uint8> Truncated = MakeHealth(TEXT("safe_stop"));
	Truncated.Pop();
	bSuccess &= RejectHealth(TEXT("Truncated Health is rejected"), Truncated);
	TArray<uint8> BadAge;
	AppendInteger(BadAge, 2, 1);
	AppendVarint(BadAge, 3u << 3);
	for (int32 Index = 0; Index < 9; ++Index) BadAge.Add(0xff);
	BadAge.Add(0x02);
	bSuccess &= RejectHealth(TEXT("Command age uint64 overflow is rejected"), BadAge);
	const TArray<uint8> Valid = MakeHealth(TEXT("active"));
	const TArray<uint8> DuplicateEnvelope = MakeWorldEnvelope(&Valid, false, true);
	bSuccess &= TestFalse(TEXT("Duplicate Health blocks ambiguous state"),
		ParseWorldStateEnvelope(DuplicateEnvelope, 1, State, Error));
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreServerHealthDisplayTest,
	"DriveIntegration.Diagnostics.ServerHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreServerHealthDisplayTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	using namespace SimCoreClientDiagnostics;
	bool bSuccess = true;
	FServerHealth Health;
	Health.bPresent = true;
	Health.Status = EServerHealthStatus::Active;
	Health.bHasControlCommand = true;
	Health.Message = TEXT("Command lease active");
	const FColor ActiveColor(80, 220, 120);
	FHealthDisplay Display = EvaluateServerHealth(Health, true, true, 0.02, 0.1);
	bSuccess &= TestTrue(TEXT("Only a fresh, ready, authoritative active command is green"),
		Display.bAuthoritative && Display.Status == TEXT("Active") && Display.Color == ActiveColor);
	const auto MustNotAdvertiseActive = [&](const TCHAR* Description, const FHealthDisplay& Value)
	{
		return TestTrue(Description, Value.Status != TEXT("Active") && Value.Color != ActiveColor
			&& !Value.bAuthoritative);
	};
	bSuccess &= MustNotAdvertiseActive(TEXT("Disconnect or incomplete Hello/map masks old active Health"),
		EvaluateServerHealth(Health, false, true, 0.02, 0.1));
	bSuccess &= MustNotAdvertiseActive(TEXT("Missing matching play-session state is unknown"),
		EvaluateServerHealth(Health, true, false, 0.02, 0.1));
	Display = EvaluateServerHealth(Health, true, true, 0.101, 0.1);
	bSuccess &= MustNotAdvertiseActive(TEXT("100ms freshness deadline masks active Health"), Display);
	bSuccess &= TestEqual(TEXT("Stale cause is visible"), Display.Status, FString(TEXT("Stale")));
	bSuccess &= MustNotAdvertiseActive(TEXT("NaN age cannot report Active"),
		EvaluateServerHealth(Health, true, true, std::numeric_limits<double>::quiet_NaN(), 0.1));
	bSuccess &= MustNotAdvertiseActive(TEXT("Negative age cannot report Active"),
		EvaluateServerHealth(Health, true, true, -0.01, 0.1));
	bSuccess &= MustNotAdvertiseActive(TEXT("Invalid timeout cannot report Active"),
		EvaluateServerHealth(Health, true, true, 0.02, 0.0));
	Health.bPresent = false;
	bSuccess &= MustNotAdvertiseActive(TEXT("Legacy absent Health cannot report Active"),
		EvaluateServerHealth(Health, true, true, 0.02, 0.1));
	Health.bPresent = true;
	Health.bHasControlCommand = false;
	bSuccess &= MustNotAdvertiseActive(TEXT("Contradictory active-without-command Health is unknown"),
		EvaluateServerHealth(Health, true, true, 0.02, 0.1));
	Health.bHasControlCommand = true;
	Health.Status = EServerHealthStatus::Unknown;
	bSuccess &= MustNotAdvertiseActive(TEXT("Future status cannot report Active"),
		EvaluateServerHealth(Health, true, true, 0.02, 0.1));
	Health.bHasControlCommand = false;
	for (EServerHealthStatus Status : {EServerHealthStatus::AwaitingReset, EServerHealthStatus::AwaitingControl})
	{
		Health.Status = Status;
		Display = EvaluateServerHealth(Health, true, true, 0.02, 0.1);
		bSuccess &= TestTrue(TEXT("Server startup waits are authoritative but never green"),
			Display.bAuthoritative && Display.Color == FColor(255, 205, 80)
			&& Display.Status != TEXT("Active"));
	}
	Health.bHasControlCommand = true;
	for (EServerHealthStatus Status : {EServerHealthStatus::SafeStop,
		EServerHealthStatus::ReconnectRequired, EServerHealthStatus::EstopLatched})
	{
		Health.Status = Status;
		Display = EvaluateServerHealth(Health, true, true, 0.02, 0.1);
		bSuccess &= TestTrue(TEXT("Authoritative safety states are red with server reason"),
			Display.bAuthoritative && Display.Color == FColor(255, 80, 80)
			&& Display.Reason == Health.Message);
	}
	Health.Status = EServerHealthStatus::SafeStop;
	Display = EvaluateServerHealth(Health, true, true, 0.02, 0.1);
	bSuccess &= TestEqual(TEXT("Soft timeout reports SafeStop"), Display.Status, FString(TEXT("SafeStop")));
	Health.Status = EServerHealthStatus::Active;
	Display = EvaluateServerHealth(Health, true, true, 0.02, 0.1);
	bSuccess &= TestTrue(TEXT("Fresh accepted command recovers from SafeStop without a client display latch"),
		Display.bAuthoritative && Display.Status == TEXT("Active") && Display.Color == ActiveColor);
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreDebugHudVehicleStateTest,
	"DriveIntegration.Diagnostics.DebugHudVehicleState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreDebugHudVehicleStateTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	bool bSuccess = true;
	const FString MapChecksum = TEXT("fnv1a64:0123456789abcdef");
	auto* Client = NewObject<USimCoreClientComponent>();
	auto Socket = MakeShared<FHealthTestSocket>();
	Client->Socket = Socket;
	Client->ConnectionState = ESimCoreConnectionState::Connected;
	Client->SocketGeneration = 7;
	Client->bProtocolHandshakeComplete = true;
	Client->bMapHandshakeComplete = true;
	Client->MapPackageChecksum = MapChecksum;

	Client->PlaySessionId = TEXT("debug-hud-play");
	const auto InstallAcceptedSnapshot = [&](uint64 Sequence, const FString& Status,
		bool bHealthPresent, float SpeedMps, EVehicleGear Gear, float SteeringRadians,
		float YawRateRadians)
	{
		Client->ApplyBinaryMessage(7, MakeVehicleHudEnvelope(Sequence, Status,
			bHealthPresent, SpeedMps, Gear, SteeringRadians, YawRateRadians), true, false);
		return Client->bHasState && Client->LatestState.Sequence == Sequence;
	};

	bSuccess &= TestTrue(TEXT("Actual wire frame enters the accepted Hello/map/play/generation/sequence fence"),
		InstallAcceptedSnapshot(40, TEXT("active"), true,
			12.5f, EVehicleGear::Drive, 0.25f, 0.3f));
	bSuccess &= TestTrue(TEXT("Additive authoritative crash fields cross the manual UE wire adapter"),
		Client->LatestState.DamagePercent == 25.0f
		&& Client->LatestState.LastImpactImpulseNs == 9'125.0f
		&& Client->LatestState.DamageZone == EVehicleDamageZone::Front
		&& Client->LatestState.CollisionEventSequence == 40);
	FString Text = Client->BuildDebugStatusText();
	bSuccess &= TestTrue(TEXT("HUD labels the 50 Hz-class telemetry value as a network rate, not vehicle speed"),
		Text.Contains(TEXT("network state rate="))
		&& !Text.Contains(TEXT("\nstate ")));
	bSuccess &= TestTrue(TEXT("HUD converts authoritative units and shows gear, actual rack angle, and yaw rate"),
		Text.Contains(TEXT("vehicle speed=45.0 km/h (12.5 m/s) gear=D steering=14.3 deg yaw=17.2 deg/s")));

	bSuccess &= TestTrue(TEXT("A fresh accepted SafeStop frame remains useful while braking"),
		InstallAcceptedSnapshot(41, TEXT("safe_stop"), true,
			-5.0f, EVehicleGear::Reverse, -0.1f, -0.2f));
	Text = Client->BuildDebugStatusText();
	bSuccess &= TestTrue(TEXT("Signed reverse speed and steering remain unambiguous"),
		Text.Contains(TEXT("vehicle speed=-18.0 km/h (-5.0 m/s) gear=R steering=-5.7 deg yaw=-11.5 deg/s")));

	Client->LatestStateReceiveTimeSeconds = FPlatformTime::Seconds()
		- Client->HealthStateStaleTimeoutSeconds - 0.001;
	Text = Client->BuildDebugStatusText();
	bSuccess &= TestTrue(TEXT("A snapshot just beyond the shared freshness deadline exposes no vehicle values"),
		Text.Contains(TEXT("vehicle speed=-- km/h (-- m/s) gear=-- steering=-- deg yaw=-- deg/s")));

	bSuccess &= TestTrue(TEXT("A newer schema-v2 snapshot may legitimately omit Health"),
		InstallAcceptedSnapshot(42, TEXT(""), false,
			50.0f, EVehicleGear::Drive, 0.5f, 0.75f));
	Text = Client->BuildDebugStatusText();
	bSuccess &= TestTrue(TEXT("Unknown/absent Health cannot make cached vehicle values look current"),
		Text.Contains(TEXT("vehicle speed=-- km/h (-- m/s) gear=-- steering=-- deg yaw=-- deg/s")));

	bSuccess &= TestTrue(TEXT("Fresh authoritative values recover on the next accepted frame"),
		InstallAcceptedSnapshot(43, TEXT("active"), true,
			1.0f, EVehicleGear::Neutral, 0.0f, 0.0f));
	Socket->bConnected = false;
	Text = Client->BuildDebugStatusText();
	bSuccess &= TestTrue(TEXT("A disconnected transport hides the last accepted vehicle values"),
		Text.Contains(TEXT("vehicle speed=-- km/h (-- m/s) gear=-- steering=-- deg yaw=-- deg/s")));

	Socket->bConnected = true;
	Client->ApplyBinaryMessage(7, MakeVehicleHudEnvelope(44, TEXT("estop_latched"), true,
		50.0f, EVehicleGear::Drive, 0.5f, 1.0f, TEXT("other-play")), true, false);
	bSuccess &= TestTrue(TEXT("Verified host-wide EStop supersedes Health but fails the current-play pose fence"),
		Client->GlobalEstopHealthCache.HasEstopHealth()
		&& Client->LatestState.Sequence == 43);
	Text = Client->BuildDebugStatusText();
	bSuccess &= TestTrue(TEXT("Cross-play EStop displays safety Health but never another play's pose values"),
		Text.Contains(TEXT("serverHealth=EStopLatched (global)"))
		&& Text.Contains(TEXT("vehicle speed=-- km/h (-- m/s) gear=-- steering=-- deg yaw=-- deg/s")));

	Client->ResetReceivedState();
	Text = Client->BuildDebugStatusText();
	bSuccess &= TestTrue(TEXT("Reset clears vehicle diagnostics instead of retaining the last speed"),
		Text.Contains(TEXT("vehicle speed=-- km/h (-- m/s) gear=-- steering=-- deg yaw=-- deg/s")));
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreCrossPlayEstopHealthTest,
	"DriveIntegration.Diagnostics.CrossPlayEstopHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreCrossPlayEstopHealthTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	using namespace SimCoreClientDiagnostics;
	bool bSuccess = true;
	const FString MapChecksum = TEXT("fnv1a64:0123456789abcdef");
	FGlobalEstopHealthCache Cache;
	FVehicleState Snapshot;
	Snapshot.PlaySessionId = TEXT("previous-pie-lifetime");
	Snapshot.MapPackageChecksum = MapChecksum;
	Snapshot.Sequence = 10;
	Snapshot.ServerHealth.bPresent = true;
	Snapshot.ServerHealth.Status = EServerHealthStatus::EstopLatched;
	Snapshot.ServerHealth.Message = TEXT("EStop latched; restart server to clear");
	bSuccess &= TestTrue(TEXT("Verified current socket may expose host-wide EStop from previous PIE"),
		Cache.Observe(Snapshot, 7, 7, true, MapChecksum, 123.0));
	bSuccess &= TestTrue(TEXT("Cache contains only global EStop health, not a current-play pose"),
		Cache.HasEstopHealth() && Cache.GetHealth().Status == EServerHealthStatus::EstopLatched
		&& !Cache.CanUsePlaySnapshot(9));
	FHealthDisplay Display = EvaluateServerHealth(Cache.GetHealth(), true,
		Cache.HasEstopHealth(), 123.02 - Cache.GetReceiveTimeSeconds(), 0.1);
	bSuccess &= TestTrue(TEXT("Cross-play EStop displays the authoritative restart reason"),
		Display.bAuthoritative && Display.Status == TEXT("EStopLatched")
		&& Display.Color == FColor(255, 80, 80)
		&& Display.Reason == Snapshot.ServerHealth.Message);

	Snapshot.Sequence = 1000;
	bSuccess &= TestFalse(TEXT("Old socket generation cannot refresh global health"),
		Cache.Observe(Snapshot, 6, 7, true, MapChecksum, 124.0));
	bSuccess &= TestFalse(TEXT("Unverified Hello cannot install global health"),
		Cache.Observe(Snapshot, 7, 7, false, MapChecksum, 124.0));
	Snapshot.MapPackageChecksum = TEXT("fnv1a64:different-map");
	bSuccess &= TestFalse(TEXT("Wrong map cannot install or advance global health"),
		Cache.Observe(Snapshot, 7, 7, true, MapChecksum, 124.0));
	Snapshot.MapPackageChecksum = MapChecksum;
	bSuccess &= TestTrue(TEXT("Rejected high sequences did not poison or renew accepted health"),
		Cache.GetSequence() == 10 && Cache.GetReceiveTimeSeconds() == 123.0);
	Snapshot.Sequence = 10;
	bSuccess &= TestFalse(TEXT("Duplicate sequence cannot extend EStop freshness"),
		Cache.Observe(Snapshot, 7, 7, true, MapChecksum, 124.0));
	Snapshot.Sequence = 9;
	bSuccess &= TestFalse(TEXT("Older sequence cannot extend EStop freshness"),
		Cache.Observe(Snapshot, 7, 7, true, MapChecksum, 124.0));
	Display = EvaluateServerHealth(Cache.GetHealth(), true, Cache.HasEstopHealth(),
		123.101 - Cache.GetReceiveTimeSeconds(), 0.1);
	bSuccess &= TestTrue(TEXT("Cross-play cache uses the same 100ms stale policy"),
		!Display.bAuthoritative && Display.Status == TEXT("Stale"));
	Snapshot.Sequence = 11;
	bSuccess &= TestTrue(TEXT("Next genuine EStop snapshot refreshes normally after invalid frames"),
		Cache.Observe(Snapshot, 7, 7, true, MapChecksum, 123.12));
	Snapshot.Sequence = 12;
	Snapshot.ServerHealth.Status = EServerHealthStatus::Active;
	Snapshot.ServerHealth.bHasControlCommand = true;
	bSuccess &= TestTrue(TEXT("Newer non-EStop frame invalidates global EStop cache"),
		Cache.Observe(Snapshot, 7, 7, true, MapChecksum, 123.14));
	Display = EvaluateServerHealth(Cache.GetHealth(), true, Cache.HasEstopHealth(), 0.01, 0.1);
	bSuccess &= TestTrue(TEXT("Mismatched-play Active is never cached or displayed"),
		!Cache.HasEstopHealth() && !Cache.GetHealth().bPresent
		&& Display.Status == TEXT("Unknown") && !Display.bAuthoritative
		&& !Cache.CanUsePlaySnapshot(11));

	Snapshot.ServerHealth.Status = EServerHealthStatus::EstopLatched;
	Snapshot.Sequence = 1;
	bSuccess &= TestFalse(TEXT("Generation replacement requires cache reset"),
		Cache.Observe(Snapshot, 8, 8, true, MapChecksum, 124.0));
	Cache.Reset();
	bSuccess &= TestTrue(TEXT("Disconnect/reset removes old EStop and sequence"),
		!Cache.HasEstopHealth() && Cache.GetSequence() == 0 && !Cache.CanUsePlaySnapshot(12));
	Snapshot.PlaySessionId.Reset();
	bSuccess &= TestTrue(TEXT("Fresh generation accepts pre-PIE EStop with an empty play id"),
		Cache.Observe(Snapshot, 8, 8, true, MapChecksum, 124.0) && Cache.HasEstopHealth());
	Snapshot.Sequence = 2;
	bSuccess &= TestFalse(TEXT("Callback from replaced generation cannot alter the fresh cache"),
		Cache.Observe(Snapshot, 7, 8, true, MapChecksum, 124.01));
	Cache.Reset();
	Snapshot.Sequence = 0;
	bSuccess &= TestFalse(TEXT("Sequence zero cannot establish global authority"),
		Cache.Observe(Snapshot, 8, 8, true, MapChecksum, 124.0));
	return bSuccess;
}

#endif // WITH_DEV_AUTOMATION_TESTS
