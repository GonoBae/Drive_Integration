#include "SimCoreTrafficSignalActor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SimCoreClientComponent.h"
#include "IWebSocket.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
using namespace SimCoreProtocol;
const FString TrafficChecksum = TEXT("fnv1a64:0123456789abcdef");
const FString MapChecksum = TEXT("fnv1a64:fedcba9876543210");

class FFragmentTestSocket final : public IWebSocket
{
public:
	void Connect() override { bConnected = true; }
	void Close(int32 Code, const FString& Reason) override { bConnected = false; }
	bool IsConnected() override { return bConnected; }
	void Send(const FString& Data) override {}
	void Send(const void* Data, SIZE_T Size, bool bIsBinary) override {}
	void SetTextMessageMemoryLimit(uint64 Limit) override {}
	FWebSocketConnectedEvent& OnConnected() override { return Connected; }
	FWebSocketConnectionErrorEvent& OnConnectionError() override { return ConnectionError; }
	FWebSocketClosedEvent& OnClosed() override { return Closed; }
	FWebSocketMessageEvent& OnMessage() override { return MessageEvent; }
	FWebSocketBinaryMessageEvent& OnBinaryMessage() override { return Binary; }
	FWebSocketRawMessageEvent& OnRawMessage() override { return Raw; }
	FWebSocketMessageSentEvent& OnMessageSent() override { return Sent; }
	void DeliverFrame(const void* Data, SIZE_T Size, bool bFinal)
	{
		// Each individual frame can have zero raw bytes remaining even while
		// the complete WebSocket message continues in subsequent frames.
		Raw.Broadcast(Data, Size, 0);
		Binary.Broadcast(Data, Size, bFinal);
	}
	void DeliverClosed() { bConnected = false; Closed.Broadcast(1000, TEXT("test close"), true); }
	bool bConnected = true;
private:
	DECLARE_DERIVED_EVENT(FFragmentTestSocket, IWebSocket::FWebSocketBinaryMessageEvent, FBinaryEvent);
	DECLARE_DERIVED_EVENT(FFragmentTestSocket, IWebSocket::FWebSocketRawMessageEvent, FRawEvent);
	DECLARE_DERIVED_EVENT(FFragmentTestSocket, IWebSocket::FWebSocketClosedEvent, FClosedEvent);
	FBinaryEvent Binary;
	FRawEvent Raw;
	FClosedEvent Closed;
	FWebSocketConnectedEvent Connected;
	FWebSocketConnectionErrorEvent ConnectionError;
	FWebSocketMessageEvent MessageEvent;
	FWebSocketMessageSentEvent Sent;
};

void Varint(TArray<uint8>& Out, uint64 Value)
{
	while (Value >= 128) { Out.Add(static_cast<uint8>(Value) | 128); Value >>= 7; }
	Out.Add(static_cast<uint8>(Value));
}
void Integer(TArray<uint8>& Out, uint32 Field, uint64 Value)
{
	Varint(Out, static_cast<uint64>(Field) << 3); Varint(Out, Value);
}
void Message(TArray<uint8>& Out, uint32 Field, const TArray<uint8>& Value)
{
	Varint(Out, (static_cast<uint64>(Field) << 3) | 2); Varint(Out, Value.Num()); Out.Append(Value);
}
void Text(TArray<uint8>& Out, uint32 Field, const FString& Value)
{
	FTCHARToUTF8 Encoded(*Value);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
	Message(Out, Field, Bytes);
}
template <typename T> void Number(TArray<uint8>& Out, uint32 Field, T Value)
{
	Varint(Out, (static_cast<uint64>(Field) << 3) | (sizeof(T) == 4 ? 5 : 1));
	uint64 Bits = 0; FMemory::Memcpy(&Bits, &Value, sizeof(T));
	for (uint32 Index = 0; Index < sizeof(T); ++Index) Out.Add(static_cast<uint8>(Bits >> (8 * Index)));
}
FTrafficSignalState Signal(uint32 Id = 1, uint32 Group = 1,
	ETrafficSignalAspect Aspect = ETrafficSignalAspect::Green,
	uint32 Controller = 1)
{
	FTrafficSignalState State;
	State.SignalId = Id; State.GroupId = Group; State.ControllerId = Controller; State.Aspect = Aspect;
	State.PositionEnu = FVector3d(-8.0, 137.0, -0.08);
	State.HeadingDegrees = 90.0f; State.RemainingSeconds = 12.5f;
	return State;
}
TArray<uint8> SignalBytes(const FTrafficSignalState& State)
{
	TArray<uint8> Out, Position;
	Integer(Out, 1, State.SignalId); Integer(Out, 2, State.GroupId);
	Integer(Out, 3, static_cast<uint8>(State.Aspect));
	Number(Position, 1, State.PositionEnu.X); Number(Position, 2, State.PositionEnu.Y); Number(Position, 3, State.PositionEnu.Z);
	Message(Out, 4, Position); Number(Out, 5, State.HeadingDegrees); Number(Out, 6, State.RemainingSeconds);
	Integer(Out, 7, State.ControllerId);
	Integer(Out, 8, static_cast<uint8>(State.Kind));
	Integer(Out, 9, State.bOutOfService ? 1 : 0);
	return Out;
}

TArray<uint8> FragmentedDamageWorldExtra()
{
	TArray<uint8> World;
	const auto Vector = [](TArray<uint8>& Out, uint32 Field, const FVector3d& Value)
	{
		TArray<uint8> Bytes; Number(Bytes, 1, Value.X); Number(Bytes, 2, Value.Y); Number(Bytes, 3, Value.Z);
		Message(Out, Field, Bytes);
	};
	for (int32 Index = 0; Index <= 40; ++Index)
	{
		const bool bPole = Index == 40;
		TArray<uint8> Structure;
		Text(Structure, 1, bPole ? TEXT("signal-pole-1") : FString::Printf(TEXT("building-fragment-%d"), Index));
		Integer(Structure, 2, static_cast<uint8>(bPole ? EStructureKind::SignalPole : EStructureKind::Building));
		if (bPole) Integer(Structure, 3, 1);
		Number(Structure, 4, bPole ? 100.0f : 25.0f); Integer(Structure, 5, Index + 1);
		Vector(Structure, 6, FVector3d(10, 20, 1)); Vector(Structure, 7, FVector3d(-1, 0, 0));
		Vector(Structure, 8, FVector3d(12, 20, .24));
		if (bPole)
		{
			Number(Structure, 10, 1.0f); Vector(Structure, 11, FVector3d(1, 0, 0)); Integer(Structure, 12, 1);
		}
		Message(World, 5, Structure);
	}
	return World;
}
TArray<uint8> WorldEnvelope(const TArray<TArray<uint8>>& Signals,
	const FString& Checksum = TrafficChecksum, bool bSignalsFirst = false,
	const TArray<uint8>& WorldExtra = {}, uint64 Sequence = 20,
	const FString& Play = TEXT("traffic-test-play"), const FString& Map = MapChecksum)
{
	TArray<uint8> World, Envelope;
	const auto AddSignals = [&]()
	{
		for (const auto& Value : Signals) Message(World, 3, Value);
		if (!Checksum.IsEmpty()) Text(World, 4, Checksum);
	};
	if (bSignalsFirst) AddSignals();
	for (uint32 Id : {1u, 2u})
	{
		TArray<uint8> Entity; Integer(Entity, 1, Id); Message(World, 1, Entity);
	}
	if (!bSignalsFirst) AddSignals();
	World.Append(WorldExtra);
	Integer(Envelope, 1, SchemaVersion); Integer(Envelope, 2, Sequence); Integer(Envelope, 3, 900000);
	Text(Envelope, 5, Map); Text(Envelope, 7, Play); Message(Envelope, 12, World);
	return Envelope;
}
struct FTrafficQaWorld
{
	UWorld* World = nullptr;
	explicit FTrafficQaWorld(EWorldType::Type Type = EWorldType::Game)
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues()
			.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
			.SetTransactional(false).CreateFXSystem(false);
		World = UWorld::CreateWorld(Type, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("SimCoreTrafficQa")),
			GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	}
	~FTrafficQaWorld() { if (World) World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreTrafficSignalProtocolTest,
	"DriveIntegration.Protocol.TrafficSignals", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreTrafficSignalProtocolTest::RunTest(const FString& Parameters)
{
	bool Ok = true; FVehicleState State; TArray<FVehicleState> Entities; FString Error;
	for (bool First : {false, true})
	{
		const auto Bytes = WorldEnvelope({SignalBytes(Signal()), SignalBytes(Signal(2)),
			SignalBytes(Signal(3, 2, ETrafficSignalAspect::Red))}, TrafficChecksum, First);
		Ok &= TestTrue(TEXT("World signals parse before or after entities"), ParseWorldStateEnvelope(Bytes, 2, State, Entities, Error));
		Ok &= TestTrue(TEXT("World signals and identity remain atomic on all entities"),
			State.TrafficSignals.Num() == 3 && State.TrafficNetworkChecksum == TrafficChecksum
			&& State.Sequence == 20 && State.PlaySessionId == TEXT("traffic-test-play")
			&& Entities.Num() == 2 && Entities[0].TrafficSignals.Num() == 3
			&& Entities[1].TrafficNetworkChecksum == TrafficChecksum);
		if (State.TrafficSignals.Num() == 3)
		{
			Ok &= TestTrue(TEXT("Position, heading and server countdown preserve wire values"),
				State.TrafficSignals[0].PositionEnu.Equals(FVector3d(-8, 137, -0.08))
				&& State.TrafficSignals[0].HeadingDegrees == 90.0f && State.TrafficSignals[0].RemainingSeconds == 12.5f
				&& State.TrafficSignals[0].ControllerId == 1);
		}
	}
	Ok &= TestTrue(TEXT("Legacy schema v2 requires no new capability or signal fields"),
		ParseWorldStateEnvelope(WorldEnvelope({}, TEXT("")), 1, State, Error)
		&& State.TrafficSignals.IsEmpty() && State.TrafficNetworkChecksum.IsEmpty());
	TArray<uint8> Minimal; Integer(Minimal, 1, 1); Integer(Minimal, 2, 1);
	Ok &= TestTrue(TEXT("Proto3 omitted zeros remain UNKNOWN, never green"),
		ParseWorldStateEnvelope(WorldEnvelope({Minimal}), 1, State, Error)
		&& State.TrafficSignals.Num() == 1 && State.TrafficSignals[0].Aspect == ETrafficSignalAspect::Unknown
		&& State.TrafficSignals[0].ControllerId == 1);
	auto Pedestrian = Signal(); Pedestrian.SignalId = 101;
	Pedestrian.Kind = ETrafficSignalKind::Pedestrian;
	Ok &= TestTrue(TEXT("Pedestrian head kind is additive and preserved"),
		ParseWorldStateEnvelope(WorldEnvelope({SignalBytes(Pedestrian)}), 1, State, Error)
		&& State.TrafficSignals.Num() == 1
		&& State.TrafficSignals[0].Kind == ETrafficSignalKind::Pedestrian);
	auto PedestrianOtherGroup = Pedestrian;
	PedestrianOtherGroup.SignalId = 102; PedestrianOtherGroup.GroupId = 2;
	Ok &= TestTrue(TEXT("exclusive all-WALK interval permits separate pedestrian groups"),
		ParseWorldStateEnvelope(WorldEnvelope({SignalBytes(Pedestrian), SignalBytes(PedestrianOtherGroup)}), 1, State, Error));
	for (bool bVehicleFirst : {false, true})
	{
		TArray<TArray<uint8>> Mixed = {SignalBytes(Pedestrian), SignalBytes(PedestrianOtherGroup)};
		if (bVehicleFirst) Mixed.Insert(SignalBytes(Signal(200)), 0);
		else Mixed.Add(SignalBytes(Signal(200)));
		Ok &= TestFalse(TEXT("vehicle cannot enter a multi-group all-WALK interval, independent of wire order"),
			ParseWorldStateEnvelope(WorldEnvelope(Mixed),1,State,Error));
	}
	Ok &= TestTrue(TEXT("Different controllers may be simultaneously permissive"),
		ParseWorldStateEnvelope(WorldEnvelope({SignalBytes(Signal(1, 1)),
			SignalBytes(Signal(2, 2, ETrafficSignalAspect::Green, 2))}), 1, State, Error)
		&& State.TrafficSignals.Num() == 2 && State.TrafficSignals[1].ControllerId == 2);
	Ok &= TestTrue(TEXT("Maximum controller and group IDs are accepted and preserved"),
		ParseWorldStateEnvelope(WorldEnvelope({SignalBytes(Signal(
			1, 4096, ETrafficSignalAspect::Green, 64))}), 1, State, Error)
		&& State.TrafficSignals[0].ControllerId == 64 && State.TrafficSignals[0].GroupId == 4096);
	TArray<uint8> Future = SignalBytes(Signal()); Integer(Future, 99, 42);
	Ok &= TestTrue(TEXT("Small additive signal fields remain compatible"),
		ParseWorldStateEnvelope(WorldEnvelope({Future}), 1, State, Error));
	TArray<TArray<uint8>> Maximum;
	for (uint32 Id = 1; Id <= 32; ++Id) Maximum.Add(SignalBytes(Signal(Id)));
	Ok &= TestTrue(TEXT("Exactly 32 heads are accepted"),
		ParseWorldStateEnvelope(WorldEnvelope(Maximum), 1, State, Error) && State.TrafficSignals.Num() == 32);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreTrafficSignalValidationTest,
	"DriveIntegration.Protocol.TrafficSignalsValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreTrafficSignalValidationTest::RunTest(const FString& Parameters)
{
	bool Ok = true; FVehicleState State; TArray<FVehicleState> Entities; FString Error;
	const auto Reject = [&](const TCHAR* Why, const TArray<uint8>& Bytes)
	{
		State.TrafficSignals = {Signal()}; State.TrafficNetworkChecksum = TrafficChecksum;
		return TestFalse(Why, ParseWorldStateEnvelope(Bytes, 1, State, Entities, Error))
			&& TestTrue(TEXT("Failed parse cannot retain a permissive output or partial entities"),
				State.TrafficSignals.IsEmpty() && State.TrafficNetworkChecksum.IsEmpty() && Entities.IsEmpty());
	};
	for (uint32 Id : {0u}) { auto S = Signal(Id); Ok &= Reject(TEXT("Zero signal ID"), WorldEnvelope({SignalBytes(S)})); }
	for (uint32 Group : {0u, 4097u, MAX_uint32}) { auto S = Signal(1, Group); Ok &= Reject(TEXT("Invalid signal group"), WorldEnvelope({SignalBytes(S)})); }
	for (uint32 Controller : {0u, 65u, MAX_uint32}) { auto S = Signal(1, 1, ETrafficSignalAspect::Green, Controller); Ok &= Reject(TEXT("Invalid controller ID"), WorldEnvelope({SignalBytes(S)})); }
	for (uint8 Aspect : {uint8(4), uint8(255)}) { auto S = Signal(); S.Aspect = static_cast<ETrafficSignalAspect>(Aspect); Ok &= Reject(TEXT("Undefined aspect"), WorldEnvelope({SignalBytes(S)})); }
	for (uint8 Kind : {uint8(0), uint8(3), uint8(255)}) { auto S = Signal(); S.Kind = static_cast<ETrafficSignalKind>(Kind); Ok &= Reject(TEXT("Undefined signal kind"), WorldEnvelope({SignalBytes(S)})); }
	for (double Value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 1000001.0})
	{
		auto S = Signal(); S.PositionEnu.Y = Value;
		Ok &= Reject(TEXT("Nonfinite or out of range ENU"), WorldEnvelope({SignalBytes(S)}));
	}
	for (float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0f, 360.0f})
	{
		auto S = Signal(); S.HeadingDegrees = Value; Ok &= Reject(TEXT("Invalid heading"), WorldEnvelope({SignalBytes(S)}));
	}
	auto LongCountdown = Signal(); LongCountdown.RemainingSeconds = MaxTrafficSignalCountdownSeconds;
	Ok &= TestTrue(TEXT("Bounded one-hour v2 countdown is accepted"),
		ParseWorldStateEnvelope(WorldEnvelope({SignalBytes(LongCountdown)}), 1, State, Entities, Error));
	for (float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
		-0.1f, MaxTrafficSignalCountdownSeconds + 0.25f})
	{
		auto S = Signal(); S.RemainingSeconds = Value; Ok &= Reject(TEXT("Invalid countdown"), WorldEnvelope({SignalBytes(S)}));
	}
	for (const FString& Checksum : {FString(), FString(TEXT("unset")), FString(TEXT("fnv1a64:0123456789abcdeF")),
		FString(TEXT("fnv1a64:0123456789abcdeg")), FString::ChrN(25, TEXT('a'))})
	{
		Ok &= Reject(TEXT("Missing or malformed network checksum"), WorldEnvelope({SignalBytes(Signal())}, Checksum));
	}
	Ok &= Reject(TEXT("Duplicate head ID"), WorldEnvelope({SignalBytes(Signal()), SignalBytes(Signal())}));
	Ok &= Reject(TEXT("Same controller conflicting groups green/green"), WorldEnvelope({SignalBytes(Signal()), SignalBytes(Signal(2, 2))}));
	Ok &= Reject(TEXT("Same controller conflicting groups green/yellow"), WorldEnvelope({SignalBytes(Signal()), SignalBytes(Signal(2, 2, ETrafficSignalAspect::Yellow))}));
	Ok &= Reject(TEXT("Same group contradictory aspect"), WorldEnvelope({SignalBytes(Signal()), SignalBytes(Signal(2, 1, ETrafficSignalAspect::Red))}));
	auto DifferentTime = Signal(2); DifferentTime.RemainingSeconds += 0.01f;
	Ok &= Reject(TEXT("Same group contradictory countdown"), WorldEnvelope({SignalBytes(Signal()), SignalBytes(DifferentTime)}));
	Ok &= TestTrue(TEXT("Same numeric group in different controllers is independent"),
		ParseWorldStateEnvelope(WorldEnvelope({SignalBytes(Signal()),
			SignalBytes(Signal(2, 1, ETrafficSignalAspect::Red, 2))}), 1, State, Entities, Error));
	Ok &= TestTrue(TEXT("Different controllers can both be green"),
		ParseWorldStateEnvelope(WorldEnvelope({SignalBytes(Signal()),
			SignalBytes(Signal(2, 2, ETrafficSignalAspect::Green, 2))}), 1, State, Entities, Error));
	Ok &= Reject(TEXT("Signal IDs stay globally unique across controllers"),
		WorldEnvelope({SignalBytes(Signal()), SignalBytes(Signal(1, 2, ETrafficSignalAspect::Red, 2))}));
	TArray<TArray<uint8>> TooMany;
	for (uint32 Id = 1; Id <= 33; ++Id) TooMany.Add(SignalBytes(Signal(Id)));
	Ok &= Reject(TEXT("33 head budget"), WorldEnvelope(TooMany));
	TArray<uint8> Duplicate = SignalBytes(Signal()); Integer(Duplicate, 3, 1);
	Ok &= Reject(TEXT("Duplicate known signal field"), WorldEnvelope({Duplicate}));
	TArray<uint8> DuplicateController = SignalBytes(Signal()); Integer(DuplicateController, 7, 1);
	Ok &= Reject(TEXT("Duplicate controller field"), WorldEnvelope({DuplicateController}));
	TArray<uint8> DuplicateKind = SignalBytes(Signal()); Integer(DuplicateKind, 8, 1);
	Ok &= Reject(TEXT("Duplicate signal kind field"), WorldEnvelope({DuplicateKind}));
	TArray<uint8> Overflow; Integer(Overflow, 1, static_cast<uint64>(MAX_uint32) + 1); Integer(Overflow, 2, 1);
	Ok &= Reject(TEXT("uint32 ID overflow"), WorldEnvelope({Overflow}));
	TArray<uint8> ControllerOverflow; Integer(ControllerOverflow, 1, 1); Integer(ControllerOverflow, 2, 1);
	Integer(ControllerOverflow, 7, static_cast<uint64>(MAX_uint32) + 1);
	Ok &= Reject(TEXT("uint32 controller overflow"), WorldEnvelope({ControllerOverflow}));
	TArray<uint8> BadVarint; Varint(BadVarint, 8); for (int32 I = 0; I < 9; ++I) BadVarint.Add(0xff); BadVarint.Add(2);
	Ok &= Reject(TEXT("uint64 varint overflow"), WorldEnvelope({BadVarint}));
	for (uint32 Field = 1; Field <= 8; ++Field)
	{
		TArray<uint8> BadWire; Message(BadWire, Field, {});
		if (Field == 4) { BadWire.Reset(); Integer(BadWire, Field, 1); }
		Ok &= Reject(TEXT("Wrong known signal wire type"), WorldEnvelope({BadWire}));
	}
	TArray<uint8> Truncated = SignalBytes(Signal()); Truncated.Pop();
	Ok &= Reject(TEXT("Truncated signal"), WorldEnvelope({Truncated}));
	TArray<uint8> Oversized = SignalBytes(Signal()); Text(Oversized, 99, FString::ChrN(513, TEXT('x')));
	Ok &= Reject(TEXT("Signal byte budget includes unknown fields"), WorldEnvelope({Oversized}));
	TArray<uint8> WorldExtra; Integer(WorldExtra, 3, 1);
	Ok &= Reject(TEXT("Wrong repeated signal wire"), WorldEnvelope({}, TrafficChecksum, false, WorldExtra));
	WorldExtra.Reset(); Integer(WorldExtra, 4, 1);
	Ok &= Reject(TEXT("Wrong checksum wire"), WorldEnvelope({}, TEXT(""), false, WorldExtra));
	WorldExtra.Reset(); Text(WorldExtra, 4, TrafficChecksum);
	Ok &= Reject(TEXT("Duplicate checksum"), WorldEnvelope({}, TrafficChecksum, false, WorldExtra));
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreTrafficSignalFreshnessTest,
	"DriveIntegration.TrafficSignals.FreshnessAndOneHot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreTrafficSignalFreshnessTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreTrafficSignals;
	bool Ok = true;
	for (auto Aspect : {ETrafficSignalAspect::Unknown, ETrafficSignalAspect::Red, ETrafficSignalAspect::Yellow, ETrafficSignalAspect::Green})
	{
		const auto S = Signal(1, 1, Aspect);
		for (bool Ready : {false, true}) for (bool Accepted : {false, true})
		for (double Age : {-0.01, 0.0, 0.05, 0.1, 0.100001, 100.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
		{
			const auto D = EvaluateDisplay(S, Ready, Accepted, Age);
			Ok &= TestEqual(TEXT("Exactly one physical lamp is active, including UNKNOWN"), int32(D.bRed) + int32(D.bYellow) + int32(D.bGreen), 1);
			const bool Verified = Ready && Accepted && FMath::IsFinite(Age) && Age >= 0.0 && Age <= 0.1 && Aspect != ETrafficSignalAspect::Unknown;
			Ok &= TestEqual(TEXT("Permission requires ready + accepted + finite fresh sample"), D.bVerified, Verified);
			if (!Verified) Ok &= TestTrue(TEXT("No false green or yellow while unverified/stale"), D.bRed && !D.bGreen && !D.bYellow && D.Aspect == ETrafficSignalAspect::Unknown);
			else Ok &= TestEqual(TEXT("Countdown is server data, never locally decremented"), D.RemainingSeconds, S.RemainingSeconds);
		}
	}
	auto Invalid = Signal(); Invalid.Aspect = static_cast<ETrafficSignalAspect>(255);
	Ok &= TestFalse(TEXT("Invalid in-memory aspect cannot bypass wire validation"), EvaluateDisplay(Invalid, true, true, 0.0).bGreen);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreTrafficSignalActorTest,
	"DriveIntegration.TrafficSignals.ActorPresentation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreTrafficSignalActorTest::RunTest(const FString& Parameters)
{
	bool Ok = true;
	FTrafficQaWorld Scene;
	if (!TestNotNull(TEXT("isolated game world without BeginPlay/socket"), Scene.World)) return false;
	FActorSpawnParameters Spawn; Spawn.ObjectFlags |= RF_Transient;
	ASimCoreTrafficSignalActor* Actor = Scene.World->SpawnActor<ASimCoreTrafficSignalActor>(ASimCoreTrafficSignalActor::StaticClass(), FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("runtime traffic head"), Actor)) return false;
	Actor->SetFailSafe();
	Ok &= TestTrue(TEXT("initial state is unverified red"), Actor->GetDisplayState().bRed && !Actor->GetDisplayState().bVerified);
	Ok &= TestTrue(TEXT("presentation actor is transient and noncolliding"), Actor->HasAnyFlags(RF_Transient) && !Actor->GetActorEnableCollision());
	TArray<UStaticMeshComponent*> Meshes; Actor->GetComponents(Meshes);
	Ok &= TestEqual(TEXT("pole, head and three lamps only"), Meshes.Num(), 5);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		Ok &= TestTrue(TEXT("no physical or navigation participation"), Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision
			&& !Mesh->GetGenerateOverlapEvents() && !Mesh->IsSimulatingPhysics() && !Mesh->CanEverAffectNavigation());
	}
	for (float Heading : {0.0f, 90.0f, 180.0f, 270.0f})
	{
		auto S = Signal(); S.HeadingDegrees = Heading;
		Actor->ApplyAuthoritativeSignal(S, true, true, 0.0);
		Ok &= TestTrue(TEXT("ENU pole base converts E/N/U meters into N/E/U centimeters"), Actor->GetActorLocation().Equals(FVector(13700, -800, -8), 0.001));
		const FVector Travel(FMath::Cos(FMath::DegreesToRadians(Heading)), FMath::Sin(FMath::DegreesToRadians(Heading)), 0.0);
		Ok &= TestTrue(TEXT("lenses face approaching driver, opposite travel heading"), Actor->GetLensFacingDirection().Equals(-Travel, 0.0001));
		Ok &= TestTrue(TEXT("lenses lie on the driver-facing head surface"), Actor->GetLamp(2)->GetRelativeLocation().X < 0);
	}
	for (auto Aspect : {ETrafficSignalAspect::Red, ETrafficSignalAspect::Yellow, ETrafficSignalAspect::Green})
	{
		Actor->ApplyAuthoritativeSignal(Signal(1, 1, Aspect), true, true, 0.03);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FLinearColor Color = FLinearColor::Black;
			UMaterialInterface* Material = Actor->GetLamp(Index)->GetMaterial(0);
			Ok &= TestTrue(TEXT("lamp Color parameter exists on dynamic engine material"), Material && Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color));
			const bool Active = Index + 1 == static_cast<int32>(Aspect);
			Ok &= TestEqual(TEXT("one-hot shader color follows authoritative aspect"), FMath::Max3(Color.R, Color.G, Color.B) > 0.5f, Active);
		}
	}
	Actor->ApplyAuthoritativeSignal(Signal(), true, true, 0.101);
	Ok &= TestTrue(TEXT("stale red is visibly labelled rather than a silent inferred phase"), Actor->GetDisplayState().bRed && Actor->GetStatusText().Contains(TEXT("unverified/stale")));
	Actor->ApplyAuthoritativeSignal(Signal(), true, true, 0.0);
	auto Pedestrian = Signal(); Pedestrian.SignalId = 101;
	Pedestrian.Kind = ETrafficSignalKind::Pedestrian;
	Actor->ApplyAuthoritativeSignal(Pedestrian, true, true, 0.0);
	Ok &= TestTrue(TEXT("pedestrian green is WALK with a two-lamp head"),
		Actor->GetStatusText().Contains(TEXT("WALK"))
		&& !Actor->GetLamp(1)->IsVisible() && Actor->GetLamp(2)->IsVisible());
	Actor->ApplyAuthoritativeSignal(Pedestrian, true, true, 0.101);
	Ok &= TestTrue(TEXT("stale pedestrian authority fails to DON'T WALK"),
		Actor->GetDisplayState().bRed
		&& Actor->GetStatusText().Contains(TEXT("DON'T WALK")));
	Actor->SetFailSafe();
	Ok &= TestTrue(TEXT("explicit invalidation clears green immediately"), Actor->GetDisplayState().bRed && !Actor->GetDisplayState().bGreen);
	Actor->Destroy();
	Ok &= TestTrue(TEXT("runtime cleanup destroys the transient actor"), Actor->IsActorBeingDestroyed());
	FTrafficQaWorld EditorScene(EWorldType::EditorPreview);
	if (EditorScene.World)
	{
		auto* Preview = EditorScene.World->SpawnActor<ASimCoreTrafficSignalActor>();
		if (Preview)
		{
			Preview->ApplyAuthoritativeSignal(Signal(), true, true, 0.0);
			Ok &= TestTrue(TEXT("non-game worlds cannot show a permissive signal"), Preview->IsHidden() && !Preview->GetDisplayState().bGreen);
		}
		else Ok = false;
	}
	else Ok = false;
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreTrafficSignalLifecycleTest,
	"DriveIntegration.TrafficSignals.ClientLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreTrafficSignalLifecycleTest::RunTest(const FString& Parameters)
{
	bool Ok = true;
	FTrafficQaWorld Scene;
	if (!TestNotNull(TEXT("isolated lifecycle world"), Scene.World)) return false;
	auto* Owner = Scene.World->SpawnActor<AActor>();
	if (!Owner) return false;
	auto* Client = NewObject<USimCoreClientComponent>(Owner);
	Client->ConnectionState = ESimCoreConnectionState::Connected;
	Client->SocketGeneration = 7; Client->bProtocolHandshakeComplete = true; Client->bMapHandshakeComplete = true;
	Client->PlaySessionId = TEXT("traffic-test-play"); Client->MapPackageChecksum = MapChecksum;
	Client->ApplyBinaryMessage(7, WorldEnvelope({SignalBytes(Signal())}), true, false);
	Ok &= TestTrue(TEXT("only accepted envelope installs atomic traffic snapshot"), Client->bHasState && Client->bTrafficSnapshotAccepted
		&& Client->LatestState.TrafficSignals.Num() == 1 && Client->LatestState.TrafficNetworkChecksum == TrafficChecksum);
	const double Arrival = Client->LatestStateReceiveTimeSeconds;
	Client->ApplyBinaryMessage(6, WorldEnvelope({SignalBytes(Signal(1, 1, ETrafficSignalAspect::Red))}, TrafficChecksum, false, {}, 21), true, false);
	Client->ApplyBinaryMessage(7, WorldEnvelope({SignalBytes(Signal(1, 1, ETrafficSignalAspect::Red))}, TrafficChecksum, false, {}, 19), true, false);
	Ok &= TestTrue(TEXT("old generation/sequence cannot replace or refresh accepted signals"), Client->LatestState.Sequence == 20
		&& Client->LatestStateReceiveTimeSeconds == Arrival && Client->LatestState.TrafficSignals[0].Aspect == ETrafficSignalAspect::Green);
	FActorSpawnParameters Spawn; Spawn.ObjectFlags |= RF_Transient;
	auto* Head = Scene.World->SpawnActor<ASimCoreTrafficSignalActor>(ASimCoreTrafficSignalActor::StaticClass(), FTransform::Identity, Spawn);
	if (!Head) return false;
	Client->TrafficSignalActors.Add(1, Head);
	Head->ApplyAuthoritativeSignal(Signal(), true, true, 0.0);
	AddExpectedMessage(TEXT("Ignored SimCore packet:"), ELogVerbosity::Warning);
	Client->ApplyBinaryMessage(7, WorldEnvelope({SignalBytes(Signal()), SignalBytes(Signal())}, TrafficChecksum, false, {}, 21), true, false);
	Ok &= TestTrue(TEXT("malformed packet revokes green immediately without replacing pose"), !Client->bTrafficSnapshotAccepted
		&& Head->GetDisplayState().bRed && Client->LatestState.Sequence == 20);
	Client->ApplyBinaryMessage(7, WorldEnvelope({SignalBytes(Signal())}, TrafficChecksum, false, {}, 22), true, false);
	Ok &= TestTrue(TEXT("next accepted packet can restore eligibility"), Client->bTrafficSnapshotAccepted && Client->LatestState.Sequence == 22);
	Client->ApplyBinaryMessage(7, WorldEnvelope({SignalBytes(Signal())}, TrafficChecksum, false, {}, 23, TEXT("old-play")), true, false);
	Ok &= TestTrue(TEXT("wrong play cannot refresh or restore permissive state"), !Client->bTrafficSnapshotAccepted && Client->LatestState.Sequence == 22 && Head->GetDisplayState().bRed);
	auto MovedSignal = Signal(); MovedSignal.PositionEnu.X += 10.0;
	const FString NewNetwork = TEXT("fnv1a64:aaaaaaaaaaaaaaaa");
	Client->ApplyBinaryMessage(7, WorldEnvelope({SignalBytes(MovedSignal)}, NewNetwork, false, {}, 24), true, false);
	Ok &= TestTrue(TEXT("accepted network replacement resynchronizes reused IDs and discards old geometry"),
		Head->IsActorBeingDestroyed() && Client->PresentedTrafficNetworkChecksum == NewNetwork
		&& Client->LatestState.TrafficSignals[0].PositionEnu.X == MovedSignal.PositionEnu.X);
	auto* Replacement = Scene.World->SpawnActor<ASimCoreTrafficSignalActor>();
	if (!Replacement) return false;
	Client->TrafficSignalActors.Add(1, Replacement);
	Replacement->ApplyAuthoritativeSignal(MovedSignal, true, true, 0.0);
	AddExpectedMessage(TEXT("MapPackage mismatch;"), ELogVerbosity::Error);
	Client->ApplyBinaryMessage(7, WorldEnvelope({SignalBytes(Signal())}, NewNetwork, false, {}, 25,
		TEXT("traffic-test-play"), TrafficChecksum), true, false);
	Ok &= TestTrue(TEXT("map mismatch revokes all signals and destroys transient heads"),
		!Client->bHasState && !Client->bTrafficSnapshotAccepted && Client->TrafficSignalActors.IsEmpty()
		&& Replacement->IsActorBeingDestroyed());
	Client->ResetReceivedState();
	Ok &= TestTrue(TEXT("reset/disconnect clears traffic identity, actors and eligibility"), !Client->bHasState && !Client->bTrafficSnapshotAccepted
		&& Client->LatestState.TrafficSignals.IsEmpty() && Client->TrafficSignalActors.IsEmpty()
		&& Client->PresentedTrafficNetworkChecksum.IsEmpty() && Head->IsActorBeingDestroyed());
	Client->ConnectionState = ESimCoreConnectionState::Connected;
	Client->bProtocolHandshakeComplete = true; Client->bMapHandshakeComplete = true;
	Client->ApplyBinaryMessage(Client->SocketGeneration, WorldEnvelope({}, TEXT(""), false, {}, 26), true, false);
	Ok &= TestTrue(TEXT("old server / empty newer snapshot leaves no heads"), Client->LatestState.TrafficSignals.IsEmpty() && Client->TrafficSignalActors.IsEmpty());
	Client->Disconnect();
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreBinaryMessageFramingTest,
	"DriveIntegration.TrafficSignals.BinaryMessageFragmentAssembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreBinaryMessageFramingTest::RunTest(const FString& Parameters)
{
	FTrafficQaWorld Scene;
	if (!TestNotNull(TEXT("fragment QA world"), Scene.World)) return false;
	auto* Owner = Scene.World->SpawnActor<AActor>();
	if (!Owner) return false;
	auto* Client = NewObject<USimCoreClientComponent>(Owner);
	const auto Socket = MakeShared<FFragmentTestSocket>();
	Client->Socket = Socket;
	Client->ConnectionState = ESimCoreConnectionState::Connected;
	Client->SocketGeneration = 7; Client->bProtocolHandshakeComplete = true; Client->bMapHandshakeComplete = true;
	Client->PlaySessionId = TEXT("traffic-test-play"); Client->MapPackageChecksum = MapChecksum;
	Client->BindSocketDelegates(7);
	bool Ok = TestTrue(TEXT("bind true binary FIN only, not per-frame raw completion"),
		Socket->OnBinaryMessage().IsBound() && !Socket->OnRawMessage().IsBound());
	auto FallenSignal = Signal(1, 1, ETrafficSignalAspect::Red);
	FallenSignal.bOutOfService = true; FallenSignal.RemainingSeconds = 0.0f;
	const auto Packet = WorldEnvelope({SignalBytes(FallenSignal)}, TrafficChecksum, false, FragmentedDamageWorldExtra());
	if (!TestTrue(TEXT("valid signal/structure damage packet crosses a 4KiB frame"), Packet.Num() > 4096)) return false;
	Socket->DeliverFrame(Packet.GetData(), 4096, false);
	Ok &= TestTrue(TEXT("frame end is not message end: partial protobuf is never parsed"),
		!Client->bHasState && Client->IncomingMessage.Num() == 4096
		&& Client->ConnectionState == ESimCoreConnectionState::Connected);
	Socket->DeliverFrame(Packet.GetData() + 4096, Packet.Num() - 4096, true);
	Ok &= TestTrue(TEXT("true FIN accepts one intact damaged signal/structure snapshot"),
		Client->bHasState && Client->LatestState.Sequence == 20 && Client->LatestState.Structures.Num() == 41
		&& Client->LatestState.TrafficSignals.Num() == 1 && Client->LatestState.TrafficSignals[0].bOutOfService
		&& Client->IncomingMessage.IsEmpty() && Client->ConnectionState == ESimCoreConnectionState::Connected);
	const auto Next = WorldEnvelope({SignalBytes(Signal())}, TrafficChecksum, false, {}, 21);
	Socket->DeliverFrame(Next.GetData(), Next.Num(), true);
	Ok &= TestTrue(TEXT("consecutive complete messages cannot concatenate or retain prior damage"),
		Client->LatestState.Sequence == 21 && Client->LatestState.Structures.IsEmpty()
		&& !Client->LatestState.TrafficSignals[0].bOutOfService && Client->IncomingMessage.IsEmpty());
	Client->Disconnect();
	Ok &= TestFalse(TEXT("shutdown removes the binary delegate"), Socket->OnBinaryMessage().IsBound());
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreBinaryMessageBoundarySafetyTest,
	"DriveIntegration.TrafficSignals.BinaryMessageBoundarySafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreBinaryMessageBoundarySafetyTest::RunTest(const FString& Parameters)
{
	FTrafficQaWorld Scene;
	if (!TestNotNull(TEXT("boundary QA world"), Scene.World)) return false;
	auto* Owner = Scene.World->SpawnActor<AActor>();
	if (!Owner) return false;
	auto* Client = NewObject<USimCoreClientComponent>(Owner);
	const auto Socket = MakeShared<FFragmentTestSocket>();
	const auto Arm = [&]
	{
		Client->Socket = Socket; Socket->bConnected = true;
		Client->ConnectionState = ESimCoreConnectionState::Connected;
		Client->bProtocolHandshakeComplete = true; Client->bMapHandshakeComplete = true;
		Client->PlaySessionId = TEXT("traffic-test-play"); Client->MapPackageChecksum = MapChecksum;
	};
	Client->SocketGeneration = 7; Arm(); Client->BindSocketDelegates(7);
	Client->bAutoReconnectEnabled = true;
	const auto Packet = WorldEnvelope({SignalBytes(Signal())});
	Socket->DeliverFrame(Packet.GetData(), 12, false);
	// AddExpectedMessage interprets Contains patterns as regex by default; an
	// unescaped '(' cannot match this real callback warning. Match literal text.
	AddExpectedMessagePlain(TEXT("Connection closed (1000, clean=true): test close"), ELogVerbosity::Warning);
	Socket->DeliverClosed();
	bool Ok = TestTrue(TEXT("remote close immediately discards partial message"),
		Client->IncomingMessage.IsEmpty() && !Client->bDiscardIncomingMessage);
	Ok &= TestTrue(TEXT("real close callback transitions the disconnected transport to reconnect"),
		!Socket->IsConnected() && Client->ConnectionState == ESimCoreConnectionState::WaitingToReconnect);
	Client->UnbindSocketDelegates();
	++Client->SocketGeneration; Client->ResetConnectionSession(); Arm();
	Client->BindSocketDelegates(Client->SocketGeneration);
	Client->ApplyBinaryMessage(7, Packet, true, false);
	Ok &= TestTrue(TEXT("delayed old-generation final frame cannot contaminate reconnect"),
		!Client->bHasState && Client->IncomingMessage.IsEmpty());
	Socket->DeliverFrame(Packet.GetData(), Packet.Num(), true);
	Ok &= TestTrue(TEXT("fresh generation starts at a clean complete message"), Client->LatestState.Sequence == 20);
	TArray<uint8> Large;
	Large.SetNumZeroed(Client->MaxIncomingMessageBytes);
	Socket->DeliverFrame(Large.GetData(), Large.Num(), false);
	Ok &= TestEqual(TEXT("partial message can reach, but cannot exceed, the existing memory cap"),
		Client->IncomingMessage.Num(), Client->MaxIncomingMessageBytes);
	const uint8 Extra = 1;
	AddExpectedMessage(TEXT("Rejected oversized SimCore message"), ELogVerbosity::Error);
	Socket->DeliverFrame(&Extra, 1, false);
	Ok &= TestTrue(TEXT("overflow drops accumulated bytes and discards the rest of that message"),
		Client->IncomingMessage.IsEmpty() && Client->bDiscardIncomingMessage && Client->LatestState.Sequence == 20);
	Socket->DeliverFrame(Packet.GetData(), Packet.Num(), false);
	Ok &= TestTrue(TEXT("non-final fragment cannot end oversized-message discard"), Client->bDiscardIncomingMessage);
	Socket->DeliverFrame(nullptr, 0, true);
	Ok &= TestFalse(TEXT("true FIN completes oversized-message discard"), Client->bDiscardIncomingMessage);
	const auto Next = WorldEnvelope({SignalBytes(Signal())}, TrafficChecksum, false, {}, 21);
	Socket->DeliverFrame(Next.GetData(), Next.Num(), true);
	Ok &= TestTrue(TEXT("next independent message remains valid after overflow"), Client->LatestState.Sequence == 21);
	Socket->DeliverFrame(Packet.GetData(), 12, false);
	Client->Disconnect();
	Ok &= TestTrue(TEXT("explicit shutdown clears partial bytes and discard state"),
		Client->IncomingMessage.IsEmpty() && !Client->bDiscardIncomingMessage && !Socket->OnBinaryMessage().IsBound());
	return Ok;
}

#endif
