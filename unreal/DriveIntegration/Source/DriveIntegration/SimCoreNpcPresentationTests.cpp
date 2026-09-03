#include "SimCoreNpcPresentationActor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "SimCoreClientComponent.h"
#include "SimCoreTrafficSignalActor.h"
#include <limits>

namespace SimCoreNpcPresentationTests
{
using namespace SimCoreProtocol;
const FString MapChecksum = TEXT("fnv1a64:0123456789abcdef");
const FString Play = TEXT("npc-presentation-test");

struct FTestWorld
{
	UWorld* World = nullptr;
	explicit FTestWorld(EWorldType::Type Type = EWorldType::Game)
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues()
			.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
			.SetTransactional(false).CreateFXSystem(false);
		World = UWorld::CreateWorld(Type, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("SimCoreNpcQa")),
			GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	}
	~FTestWorld() { if (World) World->DestroyWorld(false); }
};

FVehicleState Npc(float Speed = 5.0f, float Heading = 0.0f)
{
	FVehicleState State;
	State.EntityId = 1001; State.EntityKind = EEntityKind::NpcVehicle;
	State.PositionEnu = FVector3d(10.0, 20.0, 0.85);
	State.CollisionHalfLengthMeters = 2.2f;
	State.CollisionHalfWidthMeters = 1.0f;
	State.CollisionHalfHeightMeters = 0.75f;
	State.HeadingDegrees = Heading; State.SpeedMps = Speed;
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Heading));
	State.LinearVelocityEnu = FVector3d(Speed * FMath::Sin(Radians), Speed * FMath::Cos(Radians), 0.0);
	return State;
}

// Socket-free fixture. No BeginPlay, live connection, world physics, or server
// is involved; these tests verify display/lifecycle, not NPC route dynamics.
class FConnectedSocket final : public IWebSocket
{
public:
	bool bConnected = true;
	int32 SendCount = 0;
	void Connect() override { bConnected = true; }
	void Close(int32 Code = 1000, const FString& Reason = FString()) override { bConnected = false; }
	bool IsConnected() override { return bConnected; }
	void Send(const FString& Data) override { ++SendCount; }
	void Send(const void* Data, SIZE_T Size, bool bIsBinary = false) override { ++SendCount; }
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

void Varint(TArray<uint8>& Out, uint64 Value)
{
	while (Value >= 128) { Out.Add(static_cast<uint8>(Value) | 128); Value >>= 7; }
	Out.Add(static_cast<uint8>(Value));
}
void Integer(TArray<uint8>& Out, uint32 Field, uint64 Value)
{
	Varint(Out, static_cast<uint64>(Field) << 3); Varint(Out, Value);
}
void Message(TArray<uint8>& Out, uint32 Field, const TArray<uint8>& Bytes)
{
	Varint(Out, (static_cast<uint64>(Field) << 3) | 2); Varint(Out, Bytes.Num()); Out.Append(Bytes);
}
void Text(TArray<uint8>& Out, uint32 Field, const FString& Value)
{
	FTCHARToUTF8 Encoded(*Value); TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length()); Message(Out, Field, Bytes);
}
template <typename T> void Number(TArray<uint8>& Out, uint32 Field, T Value)
{
	Varint(Out, (static_cast<uint64>(Field) << 3) | (sizeof(T) == 4 ? 5 : 1));
	uint64 Bits = 0; FMemory::Memcpy(&Bits, &Value, sizeof(T));
	for (uint32 Index = 0; Index < sizeof(T); ++Index) Out.Add(static_cast<uint8>(Bits >> (Index * 8)));
}
void Vector(TArray<uint8>& Out, uint32 Field, const FVector3d& Value)
{
	TArray<uint8> Bytes; Number(Bytes, 1, Value.X); Number(Bytes, 2, Value.Y); Number(Bytes, 3, Value.Z);
	Message(Out, Field, Bytes);
}
TArray<uint8> Envelope(uint64 Sequence, const TArray<FVehicleState>& Entities,
	const FString& Status = TEXT("active"), const FString& Session = Play,
	const FString& Map = MapChecksum)
{
	TArray<uint8> World, Ego, Out;
	Integer(Ego, 1, 1); Integer(Ego, 22, static_cast<uint8>(EEntityKind::EgoVehicle)); Message(World, 1, Ego);
	for (const auto& Entity : Entities)
	{
		TArray<uint8> Bytes;
		Integer(Bytes, 1, Entity.EntityId); Integer(Bytes, 22, static_cast<uint8>(Entity.EntityKind));
		Number(Bytes, 6, Entity.HeadingDegrees); Number(Bytes, 9, Entity.SpeedMps);
		Number(Bytes, 15, Entity.YawRateRad); Vector(Bytes, 18, Entity.PositionEnu);
		Vector(Bytes, 23, Entity.LinearVelocityEnu);
		Number(Bytes, 24, Entity.CollisionHalfLengthMeters); Number(Bytes, 25, Entity.CollisionHalfWidthMeters);
		Number(Bytes, 26, Entity.CollisionHalfHeightMeters); Number(Bytes, 27, Entity.CollisionRadiusMeters);
		Message(World, 1, Bytes);
	}
	if (!Status.IsEmpty())
	{
		TArray<uint8> Health; Text(Health, 1, Status); Integer(Health, 5, 1); Message(World, 2, Health);
	}
	Integer(Out, 1, SchemaVersion); Integer(Out, 2, Sequence); Integer(Out, 3, 900000);
	Text(Out, 5, Map); Text(Out, 7, Session); Message(Out, 12, World);
	return Out;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreNpcActorPresentationTest,
	"DriveIntegration.NpcPresentation.ActorGeometryAndMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreNpcActorPresentationTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreNpcPresentationTests;
	bool Ok = true;
	FVector Offset;
	Ok &= TestTrue(TEXT("CG-local authored bottom and OBB centre map to the server ground anchor"),
		SimCoreNpcPresentation::BuildAuthoredModelOffset(
			FBox(FVector(-228.5, -100, -55), FVector(201.5, 100, 100)), 0.75f, Offset)
		&& Offset.Equals(FVector(13.5, 0, -30), 0.001));
	Ok &= TestFalse(TEXT("Invalid source bounds cannot be displayed"),
		SimCoreNpcPresentation::BuildAuthoredModelOffset(FBox(ForceInit), 0.75f, Offset));
	FTestWorld Scene;
	if (!TestNotNull(TEXT("Isolated game world"), Scene.World)) return false;
	FActorSpawnParameters Spawn; Spawn.ObjectFlags |= RF_Transient;
	auto* Actor = Scene.World->SpawnActor<ASimCoreNpcPresentationActor>(
		ASimCoreNpcPresentationActor::StaticClass(), FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("Actual transient NPC actor"), Actor)) return false;
	Ok &= TestTrue(TEXT("Current project reuses the authored Sedan body and all four wheels"), Actor->HasAuthoredSedan()
		&& Actor->GetBody()->GetStaticMesh()->GetPathName() == TEXT("/Game/Vehicles/Sedan/SM_SedanBody.SM_SedanBody"));
	if (!Actor->HasAuthoredSedan()) return false;
	Ok &= TestTrue(TEXT("NPC is transient, nonreplicated, and noncolliding"),
		Actor->HasAnyFlags(RF_Transient) && !Actor->GetIsReplicated() && !Actor->GetActorEnableCollision());
	TArray<UStaticMeshComponent*> Meshes; Actor->GetComponents(Meshes);
	Ok &= TestEqual(TEXT("Body plus four real wheel components"), Meshes.Num(), 5);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		Ok &= TestTrue(TEXT("Every NPC mesh is visual-only and movable"),
			Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Mesh->GetGenerateOverlapEvents()
			&& !Mesh->CanEverAffectNavigation() && !Mesh->IsSimulatingPhysics()
			&& Mesh->Mobility == EComponentMobility::Movable);
	}
	Ok &= TestTrue(TEXT("Original 270 cm wheelbase is preserved without OBB scaling"),
		FMath::IsNearlyEqual(Actor->GetWheel(0)->GetRelativeLocation().X - Actor->GetWheel(2)->GetRelativeLocation().X, 270.0, 0.001));
	const FVector VisualOffset(17.0, -23.0, 41.0);
	for (float Heading : {0.0f, 90.0f, 180.0f, 270.0f})
	{
		const auto State = Npc(5.0f, Heading);
		Ok &= TestTrue(TEXT("Authoritative empty-wheel NPC snapshot is presentable"),
			Actor->ApplySnapshot(State, 0.025f, 0.0f, true, 0.05f, VisualOffset));
		const FVector3d Predicted = State.PositionEnu + State.LinearVelocityEnu * 0.025;
		const FVector Expected(Predicted.Y * 100, Predicted.X * 100, Predicted.Z * 100);
		const double Radians = FMath::DegreesToRadians(static_cast<double>(Heading));
		Ok &= TestTrue(TEXT("ENU position, nav heading and presentation offset are preserved"),
			Actor->GetActorLocation().Equals(Expected + VisualOffset, 0.01)
			&& Actor->GetActorForwardVector().Equals(FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0), 0.0001));
		Ok &= TestTrue(TEXT("Authored rig has unit scale; tire bottom meets server ground, not OBB bottom"),
			Actor->GetActorScale3D().Equals(FVector::OneVector)
			&& Actor->GetModelRoot()->GetRelativeScale3D().Equals(FVector::OneVector)
			&& FMath::IsNearlyEqual(Actor->GetActorLocation().Z + Actor->GetModelRoot()->GetRelativeLocation().Z
				+ Actor->GetAuthoredBounds().Min.Z, VisualOffset.Z, 0.01));
	}
	const auto Moving = Npc();
	const float Radius = static_cast<float>(Actor->GetWheel(0)->GetStaticMesh()->GetBoundingBox().GetExtent().Z * 0.01);
	Actor->ApplySnapshot(Moving, 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	const float FirstSpin = Actor->GetWheelSpinDegrees();
	Ok &= TestTrue(TEXT("Rolling is proportional to signed forward speed and actual tire radius"),
		FMath::IsNearlyEqual(FirstSpin, FMath::RadiansToDegrees(5.0f / Radius) * 0.02f, 0.001f));
	Actor->ApplySnapshot(Npc(10), 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Double speed doubles rolling increment"), FMath::IsNearlyEqual(Actor->GetWheelSpinDegrees(), FirstSpin * 3, 0.001f));
	Actor->ApplySnapshot(Npc(-10), 0.0f, 0.02f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Reverse velocity reverses wheel rotation"), FMath::IsNearlyEqual(Actor->GetWheelSpinDegrees(), FirstSpin, 0.001f));
	Actor->ApplySnapshot(Moving, 0.08f, 0.02f, false, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Nonactive lease freezes spin and uses the unpredicted authoritative pose"),
		FMath::IsNearlyEqual(Actor->GetWheelSpinDegrees(), FirstSpin, 0.001f)
		&& Actor->GetActorLocation().Equals(FVector(2000, 1000, 85), 0.01));
	Actor->ApplySnapshot(Moving, 0.08f, 0.0f, true, 0.05f, FVector::ZeroVector);
	Ok &= TestTrue(TEXT("Prediction never exceeds configured 50 ms horizon"),
		Actor->GetActorLocation().Equals(FVector(2025, 1000, 85), 0.01));
	for (float Age : {-0.001f, 0.101f, std::numeric_limits<float>::quiet_NaN()})
	{
		Ok &= TestFalse(TEXT("Invalid/stale age cannot show a moving NPC"),
			Actor->ApplySnapshot(Moving, Age, 0.02f, true, 0.05f, FVector::ZeroVector));
		Ok &= TestTrue(TEXT("Rejected display is hidden"), Actor->IsHidden());
	}
	FTestWorld EditorScene(EWorldType::Editor);
	if (!EditorScene.World) return false;
	auto* EditorActor = EditorScene.World->SpawnActor<ASimCoreNpcPresentationActor>();
	Ok &= TestTrue(TEXT("NPC presentation is forbidden outside a game world"), EditorActor
		&& !EditorActor->ApplySnapshot(Moving, 0.0f, 0.01f, true, 0.05f, FVector::ZeroVector)
		&& EditorActor->IsHidden());
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreNpcClientLifecycleTest,
	"DriveIntegration.NpcPresentation.AcceptedSnapshotLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreNpcClientLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreNpcPresentationTests;
	bool Ok = true;
	FTestWorld Scene;
	if (!Scene.World) return false;
	auto* Owner = Scene.World->SpawnActor<AActor>();
	if (!Owner) return false;
	auto* Client = NewObject<USimCoreClientComponent>(Owner);
	auto Socket = MakeShared<FConnectedSocket>();
	Client->Socket = Socket; Client->ConnectionState = ESimCoreConnectionState::Connected;
	Client->SocketGeneration = 7; Client->bProtocolHandshakeComplete = true; Client->bMapHandshakeComplete = true;
	Client->PlaySessionId = Play; Client->MapPackageChecksum = MapChecksum;
	const auto Apply = [&](uint64 Sequence, const TArray<FVehicleState>& Entities, const FString& Health = TEXT("active"))
	{
		Client->ApplyRawMessage(7, Envelope(Sequence, Entities, Health), 0, false);
	};
	const auto SetAge = [&](double Age)
	{
		Client->RuntimeEntityReceiveTimeSeconds = FPlatformTime::Seconds() - Age;
		Client->LatestStateReceiveTimeSeconds = Client->RuntimeEntityReceiveTimeSeconds;
	};
	Apply(20, {Npc()});
	auto* Actor = Cast<ASimCoreNpcPresentationActor>(Client->RuntimeEntityActors.FindRef(1001).Get());
	if (!TestNotNull(TEXT("Accepted wire snapshot creates a real Sedan actor"), Actor)) return false;
	Ok &= TestTrue(TEXT("Only the NPC is spawned, never the controlled Ego"),
		Client->RuntimeEntityActors.Num() == 1 && Client->RuntimeEntityStates.Num() == 1
		&& Actor->GetOwner() == Owner && Actor->GetEntityId() == 1001);
	const double Arrival = Client->RuntimeEntityReceiveTimeSeconds;
	Client->ApplyRawMessage(6, Envelope(21, {}), 0, false);
	Apply(19, {});
	Ok &= TestTrue(TEXT("Old generation/sequence cannot refresh or remove accepted NPCs"),
		Client->RuntimeEntityReceiveTimeSeconds == Arrival && Client->LatestState.Sequence == 20
		&& Client->RuntimeEntityActors.FindRef(1001).Get() == Actor);
	SetAge(0.025); Client->TickRuntimeProxyActors(0.01f);
	Ok &= TestTrue(TEXT("Per-frame motion updates without waiting for another packet"),
		Actor->GetActorLocation().X > 2012.0 && Actor->GetActorLocation().X < 2025.1
		&& Actor->GetWheelSpinDegrees() > 0.0f);
	for (const FString& Status : {FString(TEXT("safe_stop")), FString(TEXT("estop_latched")), FString()})
	{
		Apply(Client->LatestState.Sequence + 1, {Npc()}, Status);
		const float Spin = Actor->GetWheelSpinDegrees();
		SetAge(0.025); Client->TickRuntimeProxyActors(0.02f);
		Ok &= TestTrue(TEXT("SafeStop/EStop/missing Health never extrapolates or spins stale velocity"),
			Actor->GetActorLocation().Equals(FVector(2000, 1000, 85), 0.01)
			&& Actor->GetWheelSpinDegrees() == Spin);
	}
	Apply(30, {Npc()});
	Client->ApplyRawMessage(7, Envelope(31, {}, TEXT("active"), TEXT("other-play")), 0, false);
	const float SpinBeforeWrongPlay = Actor->GetWheelSpinDegrees();
	SetAge(0.025); Client->TickRuntimeProxyActors(0.02f);
	Ok &= TestTrue(TEXT("Another play cannot replace NPC geometry or grant motion via host-wide health"),
		Client->LatestState.Sequence == 30 && Client->RuntimeEntityActors.FindRef(1001).Get() == Actor
		&& Actor->GetWheelSpinDegrees() == SpinBeforeWrongPlay);
	Apply(32, {Npc()});
	SetAge(0.101); Client->TickRuntimeProxyActors(0.01f);
	Ok &= TestTrue(TEXT("No packet for 100 ms destroys the actual actor and clears cached entities"),
		Actor->IsActorBeingDestroyed() && Client->RuntimeEntityActors.IsEmpty() && Client->RuntimeEntityStates.IsEmpty());
	Apply(31, {Npc()}); Client->TickRuntimeProxyActors(0.01f);
	Ok &= TestTrue(TEXT("An old packet cannot resurrect an expired NPC"), Client->RuntimeEntityActors.IsEmpty());
	Apply(33, {Npc()});
	Actor = Cast<ASimCoreNpcPresentationActor>(Client->RuntimeEntityActors.FindRef(1001).Get());
	if (!Actor) return false;
	Apply(34, {});
	Ok &= TestTrue(TEXT("Removal in an accepted snapshot destroys the old actor immediately"),
		Actor->IsActorBeingDestroyed() && Client->RuntimeEntityActors.IsEmpty());
	Apply(35, {Npc()});
	Actor = Cast<ASimCoreNpcPresentationActor>(Client->RuntimeEntityActors.FindRef(1001).Get());
	if (!Actor) return false;
	auto Pedestrian = Npc(); Pedestrian.EntityKind = EEntityKind::Pedestrian; Pedestrian.CollisionRadiusMeters = 0.3f;
	Apply(36, {Pedestrian});
	Ok &= TestTrue(TEXT("Entity kind replacement destroys Sedan and preserves pedestrian fallback"),
		Actor->IsActorBeingDestroyed() && Cast<AStaticMeshActor>(Client->RuntimeEntityActors.FindRef(1001).Get()) != nullptr);
	Apply(37, {Npc()});
	Actor = Cast<ASimCoreNpcPresentationActor>(Client->RuntimeEntityActors.FindRef(1001).Get());
	if (!Actor) return false;
	Socket->bConnected = false; Client->TickRuntimeProxyActors(0.01f);
	Ok &= TestTrue(TEXT("Socket closure removes NPCs before age expiry"), Actor->IsActorBeingDestroyed() && Client->RuntimeEntityActors.IsEmpty());
	Socket->bConnected = true; Apply(38, {Npc()});
	Actor = Cast<ASimCoreNpcPresentationActor>(Client->RuntimeEntityActors.FindRef(1001).Get());
	if (!Actor) return false;
	Client->bShowRuntimeEntities = false; Client->TickRuntimeProxyActors(0.01f);
	Ok &= TestTrue(TEXT("Display toggle cleans up existing runtime actors"), Actor->IsActorBeingDestroyed() && Client->RuntimeEntityStates.IsEmpty());
	Client->bShowRuntimeEntities = true; Apply(39, {Npc()});
	Actor = Cast<ASimCoreNpcPresentationActor>(Client->RuntimeEntityActors.FindRef(1001).Get());
	if (!Actor) return false;
	AddExpectedMessage(TEXT("MapPackage mismatch;"), ELogVerbosity::Error);
	Client->ApplyRawMessage(7, Envelope(40, {Npc()}, TEXT("active"), Play, TEXT("fnv1a64:fedcba9876543210")), 0, false);
	Ok &= TestTrue(TEXT("Map mismatch revokes snapshot and destroys presentation"),
		!Client->bHasState && Actor->IsActorBeingDestroyed() && Client->RuntimeEntityActors.IsEmpty());
	Client->Disconnect();
	Ok &= TestTrue(TEXT("Disconnect/reset leaves no runtime cache"),
		Client->RuntimeEntityStates.IsEmpty() && Client->RuntimeEntityActors.IsEmpty());
	Ok &= TestEqual(TEXT("Presentation and fixture acceptance never send controls or open a real connection"), Socket->SendCount, 0);
	return Ok;
}

#endif
