#if WITH_DEV_AUTOMATION_TESTS

#include "SimCoreVehicleHorn.h"

#include "ExternalVehiclePawn.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "SimCoreNpcPresentationActor.h"
#include "SimCoreProtocol.h"

namespace
{
void HornVarint(TArray<uint8>& Out, uint64 Value)
{
	while (Value >= 128)
	{
		Out.Add(static_cast<uint8>(Value) | 128);
		Value >>= 7;
	}
	Out.Add(static_cast<uint8>(Value));
}

void HornInteger(TArray<uint8>& Out, const uint32 Field, const uint64 Value)
{
	HornVarint(Out, static_cast<uint64>(Field) << 3);
	HornVarint(Out, Value);
}

void HornMessage(TArray<uint8>& Out, const uint32 Field, const TArray<uint8>& Value)
{
	HornVarint(Out, (static_cast<uint64>(Field) << 3) | 2);
	HornVarint(Out, Value.Num());
	Out.Append(Value);
}

void HornText(TArray<uint8>& Out, const uint32 Field, const FString& Value)
{
	FTCHARToUTF8 Encoded(*Value);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
	HornMessage(Out, Field, Bytes);
}

void HornFloat(TArray<uint8>& Out, const uint32 Field, const float Value)
{
	HornVarint(Out, (static_cast<uint64>(Field) << 3) | 5);
	uint32 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	for (uint32 Index = 0; Index < sizeof(Bits); ++Index)
	{
		Out.Add(static_cast<uint8>(Bits >> (Index * 8)));
	}
}

TArray<uint8> HornEnvelope(
	const SimCoreProtocol::EEntityKind Kind, const uint32 EventSequence)
{
	using namespace SimCoreProtocol;
	TArray<uint8> Entity;
	HornInteger(Entity, 1, 1001);
	HornInteger(Entity, 22, static_cast<uint8>(Kind));
	if (Kind == EEntityKind::NpcVehicle)
	{
		HornFloat(Entity, 24, 2.2f);
		HornFloat(Entity, 25, 1.0f);
		HornFloat(Entity, 26, 0.75f);
	}
	else
	{
		HornFloat(Entity, 26, 0.9f);
		HornFloat(Entity, 27, 0.35f);
	}
	HornInteger(Entity, 38, EventSequence);

	TArray<uint8> World;
	HornMessage(World, 1, Entity);
	TArray<uint8> Envelope;
	HornInteger(Envelope, 1, SchemaVersion);
	HornInteger(Envelope, 2, 17);
	HornInteger(Envelope, 3, 900000);
	HornText(Envelope, 5, TEXT("fnv1a64:0123456789abcdef"));
	HornText(Envelope, 7, TEXT("horn-protocol-test"));
	HornMessage(Envelope, 12, World);
	return Envelope;
}

SimCoreProtocol::FVehicleState HornNpcState(
	const FString& PlaySessionId, const uint32 EventSequence)
{
	using namespace SimCoreProtocol;
	FVehicleState State;
	State.EntityId = 1001;
	State.EntityKind = EEntityKind::NpcVehicle;
	State.PlaySessionId = PlaySessionId;
	State.MapPackageChecksum = TEXT("fnv1a64:0123456789abcdef");
	State.CollisionHalfLengthMeters = 2.2f;
	State.CollisionHalfWidthMeters = 1.0f;
	State.CollisionHalfHeightMeters = 0.75f;
	State.PositionEnu = FVector3d(10.0, 20.0, 0.85);
	State.HornEventSequence = EventSequence;
	return State;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreVehicleHornEnvelopeAndGateTest,
	"DriveIntegration.Presentation.VehicleHorn.EnvelopeAndAntiSpam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleHornEnvelopeAndGateTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreVehicleHorn;
	bool bOk = true;
	bOk &= TestEqual(TEXT("pre-pulse is silent"), EvaluateEnvelope(-0.01f), 0.0f);
	bOk &= TestEqual(TEXT("pulse starts click-free"), EvaluateEnvelope(0.0f), 0.0f);
	bOk &= TestTrue(TEXT("dual tone has a sustained body"), EvaluateEnvelope(0.10f) > 0.99f);
	bOk &= TestTrue(TEXT("release tapers before the boundary"),
		EvaluateEnvelope(PulseDurationSeconds - 0.02f) < 0.5f);
	bOk &= TestEqual(TEXT("post-pulse is silent"),
		EvaluateEnvelope(PulseDurationSeconds), 0.0f);

	FTriggerGate Gate;
	bOk &= TestTrue(TEXT("first local request is accepted"), Gate.TryManual(0.0));
	bOk &= TestFalse(TEXT("key repeat cannot chatter"), Gate.TryManual(0.10));
	bOk &= TestTrue(TEXT("new press works after cooldown"),
		Gate.TryManual(MinimumTriggerIntervalSeconds));
	bOk &= TestFalse(TEXT("zero means no authoritative pulse"),
		Gate.ObserveAuthoritativeEvent(0, 2.0));
	bOk &= TestTrue(TEXT("new non-zero server token sounds once"),
		Gate.ObserveAuthoritativeEvent(7, 2.0));
	bOk &= TestFalse(TEXT("repeated snapshots cannot retrigger one NPC pulse"),
		Gate.ObserveAuthoritativeEvent(7, 3.0));
	bOk &= TestFalse(TEXT("malformed backward time is rejected"),
		Gate.TryManual(-1.0));
	Gate.Reset();
	Gate.BaselineAuthoritativeEvent(11);
	bOk &= TestFalse(TEXT("lifecycle baseline does not replay a historical event"),
		Gate.ObserveAuthoritativeEvent(11, 0.0));
	Gate.Reset();
	Gate.BaselineAuthoritativeEvent(0);
	bOk &= TestTrue(TEXT("new Play sequence one triggers after its zero baseline"),
		Gate.ObserveAuthoritativeEvent(1, 0.0));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreVehicleHornInputAndSpatializationTest,
	"DriveIntegration.Presentation.VehicleHorn.InputAndSpatialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleHornInputAndSpatializationTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	const UInputSettings* Settings = UInputSettings::GetInputSettings();
	if (!TestNotNull(TEXT("project input settings"), Settings)) return false;
	TArray<FInputActionKeyMapping> HornMappings;
	Settings->GetActionMappingByName(TEXT("Horn"), HornMappings);
	bOk &= TestTrue(TEXT("H maps to the Horn action"), HornMappings.ContainsByPredicate(
		[](const FInputActionKeyMapping& Mapping) { return Mapping.Key == EKeys::H; }));

	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
		.SetTransactional(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("HornInputQa")),
		GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("isolated horn input world"), World)) return false;
	AExternalVehiclePawn* Pawn = World->SpawnActor<AExternalVehiclePawn>();
	USimCoreVehicleHornComponent* Horn = Pawn
		? Pawn->FindComponentByClass<USimCoreVehicleHornComponent>() : nullptr;
	if (!TestNotNull(TEXT("asset-free horn component"), Horn))
	{
		World->DestroyWorld(false);
		return false;
	}
	bOk &= TestTrue(TEXT("horn is a mono positional source"),
		Horn->GetRenderedChannelCount() == 1 && Horn->bAllowSpatialization
		&& Horn->bOverrideAttenuation && Horn->AttenuationOverrides.bAttenuate
		&& Horn->AttenuationOverrides.bSpatialize);
	bOk &= TestTrue(TEXT("horn range is bounded to nearby traffic"),
		Horn->AttenuationOverrides.AttenuationShape == EAttenuationShape::Sphere
		&& Horn->AttenuationOverrides.AttenuationShapeExtents.X == 200.0
		&& Horn->AttenuationOverrides.FalloffDistance == 6000.0f);
	UInputComponent* Input = NewObject<UInputComponent>(Pawn);
	Pawn->SetupPlayerInputComponent(Input);
	bool bExecuted = false;
	for (int32 Index = 0; Index < Input->GetNumActionBindings(); ++Index)
	{
		FInputActionBinding& Binding = Input->GetActionBinding(Index);
		if (Binding.GetActionName() == TEXT("Horn") && Binding.KeyEvent == IE_Pressed)
		{
			Binding.ActionDelegate.Execute(EKeys::H);
			Binding.ActionDelegate.Execute(EKeys::H);
			bExecuted = true;
			break;
		}
	}
	bOk &= TestTrue(TEXT("pawn binds H press to the horn component"), bExecuted);
	bOk &= TestEqual(TEXT("immediate repeat is suppressed"),
		Horn->GetAcceptedTriggerCount(), 1u);
	World->DestroyWorld(false);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreVehicleHornProtocolAndLifecycleTest,
	"DriveIntegration.Presentation.VehicleHorn.ProtocolAndPlayLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreVehicleHornProtocolAndLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	bool bOk = true;
	FVehicleState Parsed;
	FString Error;
	bOk &= TestTrue(TEXT("field 38 carries the exact NPC horn event sequence"),
		ParseWorldStateEnvelope(HornEnvelope(EEntityKind::NpcVehicle, 7),
			1001, Parsed, Error)
		&& Parsed.HornEventSequence == 7);
	bOk &= TestFalse(TEXT("non-vehicle entities cannot publish horn events"),
		ParseWorldStateEnvelope(HornEnvelope(EEntityKind::Pedestrian, 1),
			1001, Parsed, Error));

	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
		.SetTransactional(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("HornLifecycleQa")),
		GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("isolated horn lifecycle world"), World)) return false;
	ASimCoreNpcPresentationActor* Npc = World->SpawnActor<ASimCoreNpcPresentationActor>();
	USimCoreVehicleHornComponent* Horn = Npc
		? Npc->FindComponentByClass<USimCoreVehicleHornComponent>() : nullptr;
	if (!TestNotNull(TEXT("NPC owns positional horn component"), Horn))
	{
		World->DestroyWorld(false);
		return false;
	}

	auto State = HornNpcState(TEXT("play-a"), 9);
	bOk &= TestTrue(TEXT("historical sequence snapshot applies"),
		Npc->ApplySnapshot(State, 0.0f, 0.0f, true, 0.05f, FVector::ZeroVector));
	bOk &= TestEqual(TEXT("actor baselines history instead of false-triggering"),
		Horn->GetAcceptedTriggerCount(), 0u);
	Npc->ApplySnapshot(State, 0.0f, 0.0f, true, 0.05f, FVector::ZeroVector);
	bOk &= TestEqual(TEXT("repeated snapshot remains silent"),
		Horn->GetAcceptedTriggerCount(), 0u);
	State.HornEventSequence = 10;
	Npc->ApplySnapshot(State, 0.0f, 0.0f, true, 0.05f, FVector::ZeroVector);
	bOk &= TestEqual(TEXT("new event sounds exactly once"),
		Horn->GetAcceptedTriggerCount(), 1u);
	State = HornNpcState(TEXT("play-b"), 0);
	Npc->ApplySnapshot(State, 0.0f, 0.0f, true, 0.05f, FVector::ZeroVector);
	State.HornEventSequence = 1;
	Npc->ApplySnapshot(State, 0.0f, 0.0f, true, 0.05f, FVector::ZeroVector);
	bOk &= TestEqual(TEXT("new Play resets cooldown and accepts sequence one"),
		Horn->GetAcceptedTriggerCount(), 2u);
	World->DestroyWorld(false);
	return bOk;
}

#endif
