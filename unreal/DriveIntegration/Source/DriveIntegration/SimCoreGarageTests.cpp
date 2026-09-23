#include "SimCoreClientComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "SimCoreVehicleVisualProfile.h"
#include "ExternalVehiclePawn.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"

#include <limits>

namespace
{
void GarageVarint(TArray<uint8>& Out, uint64 Value)
{
	while (Value >= 128) { Out.Add(static_cast<uint8>(Value) | 128); Value >>= 7; }
	Out.Add(static_cast<uint8>(Value));
}

void GarageInteger(TArray<uint8>& Out, uint32 Field, uint64 Value)
{
	GarageVarint(Out, static_cast<uint64>(Field) << 3);
	GarageVarint(Out, Value);
}

void GarageMessage(TArray<uint8>& Out, uint32 Field, const TArray<uint8>& Value)
{
	GarageVarint(Out, (static_cast<uint64>(Field) << 3) | 2);
	GarageVarint(Out, Value.Num());
	Out.Append(Value);
}

void GarageText(TArray<uint8>& Out, uint32 Field, const FString& Value)
{
	const FTCHARToUTF8 Utf8(*Value);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	GarageMessage(Out, Field, Bytes);
}

TArray<uint8> GarageResetFixture(const FString& LoadoutId)
{
	TArray<uint8> Reset, Envelope;
	GarageText(Reset, 1, TEXT("garage-play"));
	GarageInteger(Reset, 2, 17);
	GarageInteger(Reset, 3, 1);
	if (!LoadoutId.IsEmpty()) GarageText(Reset, 4, LoadoutId);
	GarageInteger(Envelope, 1, 2);
	GarageInteger(Envelope, 2, 7);
	GarageText(Envelope, 4, TEXT("garage-test"));
	GarageText(Envelope, 5, TEXT("fnv1a64:0123456789abcdef"));
	GarageText(Envelope, 6, TEXT("garage-connection"));
	GarageText(Envelope, 7, TEXT("garage-play"));
	GarageMessage(Envelope, 14, Reset);
	return Envelope;
}

TArray<uint8> GarageWorldFixture(const TArray<uint8>& LoadoutFields)
{
	TArray<uint8> Entity, World, Envelope;
	GarageInteger(Entity, 1, 1);
	GarageInteger(Entity, 22, static_cast<uint8>(SimCoreProtocol::EEntityKind::EgoVehicle));
	GarageInteger(Entity, 39, static_cast<uint8>(SimCoreProtocol::ERuntimeVehicleClass::Sedan));
	Entity.Append(LoadoutFields);
	GarageMessage(World, 1, Entity);
	GarageInteger(Envelope, 1, SimCoreProtocol::SchemaVersion);
	GarageInteger(Envelope, 2, 7);
	GarageInteger(Envelope, 3, 1000);
	GarageText(Envelope, 5, TEXT("fnv1a64:0123456789abcdef"));
	GarageText(Envelope, 7, TEXT("garage-play"));
	GarageMessage(Envelope, 12, World);
	return Envelope;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreGarageLifecycleTest,
	"DriveIntegration.Garage.SelectionLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreGarageLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	USimCoreClientComponent* Client = NewObject<USimCoreClientComponent>();
	if (!TestNotNull(TEXT("isolated garage client"), Client)) return false;
	const auto Ready = [Client]()
	{
		Client->ConnectionState = ESimCoreConnectionState::Connected;
		Client->bServerSupportsLoadouts = true;
		Client->bLoadoutPending = false;
		Client->bHasState = true;
		Client->HealthStateStaleTimeoutSeconds = 1.f;
		Client->LatestStateReceiveTimeSeconds = FPlatformTime::Seconds();
		Client->LatestState = {};
		Client->LatestState.ServerHealth.bPresent = true;
		Client->LatestState.ServerHealth.Status = EServerHealthStatus::Active;
		Client->SelectedVehicleClass = ERuntimeVehicleClass::Sedan;
		Client->SelectedLoadoutId.Reset();
		Client->PlaySessionId = TEXT("garage-current-play");
	};
	FString Reason;
	Ready();
	bool Ok = TestTrue(TEXT("stopped fresh Active capable server permits selection"), Client->CanSelectLoadout(Reason));
	Client->LatestState.SpeedMps = -.21f;
	Ok &= TestFalse(TEXT("reverse motion rejects reset"), Client->CanSelectLoadout(Reason));
	Ready();
	Client->LatestState.LinearVelocityBody = FVector3d(0, .21, 0);
	Ok &= TestFalse(TEXT("lateral motion rejects reset even at zero forward speed"), Client->CanSelectLoadout(Reason));
	Ready();
	Client->LatestState.SpeedMps = std::numeric_limits<float>::quiet_NaN();
	Ok &= TestFalse(TEXT("invalid speed cannot qualify as stopped"), Client->CanSelectLoadout(Reason));
	Ready();
	Client->LatestStateReceiveTimeSeconds -= 2;
	Ok &= TestFalse(TEXT("stale stopped state rejects reset"), Client->CanSelectLoadout(Reason));
	for (const auto Status : {EServerHealthStatus::SafeStop, EServerHealthStatus::AwaitingReset,
		EServerHealthStatus::AwaitingControl, EServerHealthStatus::EstopLatched})
	{
		Ready();
		Client->LatestState.ServerHealth.Status = Status;
		Ok &= TestFalse(TEXT("non-Active health rejects reset"), Client->CanSelectLoadout(Reason));
	}
	Ready();
	Client->LatestState.ServerHealth.bPresent = false;
	Ok &= TestFalse(TEXT("missing health does not imply an Active lease"), Client->CanSelectLoadout(Reason));
	Ready();
	Client->ConnectionState = ESimCoreConnectionState::Disconnected;
	Ok &= TestFalse(TEXT("disconnected client cannot use retained state"), Client->CanSelectLoadout(Reason));
	Ready();
	Client->bHasState = false;
	Ok &= TestFalse(TEXT("missing authoritative state rejects reset"), Client->CanSelectLoadout(Reason));
	Ready();
	Client->bServerSupportsLoadouts = false;
	Ok &= TestFalse(TEXT("old server does not enable the garage"), Client->CanSelectLoadout(Reason));
	Ready();
	Client->bLoadoutPending = true;
	Client->LoadoutRequestedAtSeconds = FPlatformTime::Seconds();
	Ok &= TestFalse(TEXT("pending acknowledgement prevents a second selection"), Client->CanSelectLoadout(Reason));
	Ok &= TestFalse(TEXT("pending status explains why selection is blocked"), Reason.IsEmpty());
	Ready();
	Ok &= TestFalse(TEXT("unknown setup rejected without opening a connection"), Client->SelectLoadout(TEXT("unknown_setup"), Reason));
	Ok &= TestFalse(TEXT("malformed setup rejected without opening a connection"), Client->SelectLoadout(TEXT("Invalid-setup"), Reason));
	Ok &= TestFalse(TEXT("already applied default remains a no-op"), Client->SelectLoadout(FString(), Reason));
	Ok &= TestTrue(TEXT("rejected choices retain the current default"), Client->SelectedLoadoutId.IsEmpty());
	Ok &= TestFalse(TEXT("rejected choices never enter pending state"), Client->bLoadoutPending);
	Ok &= TestEqual(TEXT("rejected choices preserve play identity"), Client->PlaySessionId, FString(TEXT("garage-current-play")));

	const FString LoadoutId = TEXT("sedan_modular_standard");
	SimCoreVehicleVisualProfile::FProfile Profile;
	if (!TestTrue(TEXT("catalog loadout used by acknowledgement fixture exists"),
		SimCoreVehicleVisualProfile::Resolve(ERuntimeVehicleClass::Sedan, LoadoutId, Profile))) return false;
	Client->SelectedLoadoutId = LoadoutId;
	FVehicleState Confirmed;
	Confirmed.RuntimeVehicleClass = ERuntimeVehicleClass::Sedan;
	Confirmed.VehicleLoadoutId = LoadoutId;
	Ok &= TestTrue(TEXT("matching authoritative class and ID accepted"), Client->ValidateAuthoritativeLoadout(Confirmed, Reason));
	Confirmed.VehicleLoadoutId.Reset();
	Ok &= TestFalse(TEXT("previous default snapshot cannot acknowledge selected setup"), Client->ValidateAuthoritativeLoadout(Confirmed, Reason));
	Confirmed.VehicleLoadoutId = LoadoutId;
	Confirmed.RuntimeVehicleClass = ERuntimeVehicleClass::Truck;
	Ok &= TestFalse(TEXT("matching ID with wrong class rejected"), Client->ValidateAuthoritativeLoadout(Confirmed, Reason));
	Confirmed.RuntimeVehicleClass = ERuntimeVehicleClass::Sedan;
	Confirmed.VehicleLoadoutId = TEXT("unknown_setup");
	Client->SelectedLoadoutId = Confirmed.VehicleLoadoutId;
	Ok &= TestFalse(TEXT("even matching unknown IDs cannot resolve to a setup"), Client->ValidateAuthoritativeLoadout(Confirmed, Reason));
	Client->SelectedLoadoutId.Reset();
	Confirmed.VehicleLoadoutId.Reset();
	Client->bServerSupportsLoadouts = false;
	Ok &= TestTrue(TEXT("old-capability server may retain default driving"), Client->ValidateAuthoritativeLoadout(Confirmed, Reason));
	Confirmed.VehicleLoadoutId = LoadoutId;
	Ok &= TestFalse(TEXT("old-capability server cannot assert a preset"), Client->ValidateAuthoritativeLoadout(Confirmed, Reason));

	Client->MapPackageChecksum = TEXT("fnv1a64:0123456789abcdef");
	FHelloInfo Hello;
	Hello.Sequence = 1;
	Hello.SourceId = TEXT("simcore-host");
	Hello.MapPackageChecksum = Client->MapPackageChecksum;
	Hello.Build = TEXT("garage-test");
	Hello.Schema = SchemaName;
	Hello.Capabilities = {TEXT("world-state.v2"), TEXT("control.v2"), TEXT("simulation-reset.v1"),
		TEXT("player-vehicle-selection.v1"), TEXT("map-package-checksum.v1")};
	Ok &= TestTrue(TEXT("old Hello permits default setup"), Client->ValidateServerHello(Hello, Reason));
	Client->SelectedLoadoutId = LoadoutId;
	Ok &= TestFalse(TEXT("selected preset requires loadout-capable Hello"), Client->ValidateServerHello(Hello, Reason));
	Client->SelectedLoadoutId.Reset();
	Client->bLoadoutPending = true;
	Ok &= TestFalse(TEXT("pending return to default still requires acknowledgement capability"), Client->ValidateServerHello(Hello, Reason));
	Hello.Capabilities.Add(TEXT("vehicle-loadout.v1"));
	Ok &= TestTrue(TEXT("loadout-capable Hello permits pending selection"), Client->ValidateServerHello(Hello, Reason));
	const FString CustomId = TEXT("parts_v1_01_0504070401080002");
	Client->SelectedLoadoutId = CustomId;
	Ok &= TestFalse(TEXT("custom parts require their own capability"), Client->ValidateServerHello(Hello, Reason));
	Hello.Capabilities.Add(TEXT("vehicle-parts.v1"));
	Ok &= TestFalse(TEXT("custom parts cannot use checksum-optional legacy Hello"), Client->ValidateServerHello(Hello, Reason));
	FString CatalogCapability;
	Ok &= TestTrue(TEXT("custom test resolves catalog capability"), SimCoreVehicleVisualProfile::CatalogCapability(CatalogCapability, Reason));
	Hello.Capabilities.Add(CatalogCapability);
	Ok &= TestTrue(TEXT("custom parts accept explicit matching catalog and capability"), Client->ValidateServerHello(Hello, Reason));
	Hello.Capabilities.Last() = TEXT("vehicle-catalog-fnv1a64-0000000000000000");
	Ok &= TestFalse(TEXT("custom parts reject changed catalog mapping"), Client->ValidateServerHello(Hello, Reason));
	Client->bServerSupportsParts = false;
	Client->bServerMatchesCatalog = true;
	Ok &= TestFalse(TEXT("preset-only server disables individual part editing"), Client->CanEditParts(Reason));
	Ok &= TestFalse(TEXT("preset-only server cannot receive custom selection"), Client->SelectLoadout(CustomId, Reason));
	Client->bServerSupportsParts = true;
	Client->bServerMatchesCatalog = false;
	Ok &= TestFalse(TEXT("parts capability alone cannot interpret catalog indexes"), Client->CanEditParts(Reason));
	Client->bServerMatchesCatalog = true;
	Ok &= TestTrue(TEXT("parts editing enabled only after exact negotiation"), Client->CanEditParts(Reason));
	Confirmed.RuntimeVehicleClass = ERuntimeVehicleClass::Sedan;
	Confirmed.VehicleLoadoutId = CustomId;
	Client->bServerSupportsLoadouts = true;
	Ok &= TestTrue(TEXT("custom acknowledgement resolves against negotiated catalog"), Client->ValidateAuthoritativeLoadout(Confirmed, Reason));
	Client->bServerSupportsParts = false;
	Ok &= TestFalse(TEXT("custom acknowledgement cannot bypass capability gate"), Client->ValidateAuthoritativeLoadout(Confirmed, Reason));

	Ready();
	Client->bAutoReconnectEnabled = true;
	Client->bMapHandshakeComplete = true;
	Client->bLoadoutPending = true;
	Client->SelectedLoadoutId = LoadoutId;
	Client->LoadoutRequestedAtSeconds = FPlatformTime::Seconds() - 6.0;
	Client->ReconnectDelaySeconds = 1.0f;
	const uint64 Generation = Client->SocketGeneration;
	Client->TickConnection(); // No socket is installed; only schedules normal backoff.
	Ok &= TestEqual(TEXT("missing acknowledgement schedules bounded reconnect"),
		Client->ConnectionState, ESimCoreConnectionState::WaitingToReconnect);
	Ok &= TestEqual(TEXT("timed-out socket generation is fenced"), Client->SocketGeneration, Generation + 1);
	Ok &= TestTrue(TEXT("retry retains pending desired setup"), Client->bLoadoutPending && Client->SelectedLoadoutId == LoadoutId);
	Ok &= TestEqual(TEXT("retry keeps new play identity for safe server deduplication"),
		Client->PlaySessionId, FString(TEXT("garage-current-play")));
	Ok &= TestFalse(TEXT("timeout removes stale received state"), Client->bHasState);
	const double RetryAt = Client->NextReconnectTimeSeconds;
	Client->TickConnection();
	Ok &= TestEqual(TEXT("waiting tick does not postpone the retry"), Client->NextReconnectTimeSeconds, RetryAt);
	Ok &= TestEqual(TEXT("waiting tick does not repeatedly fence socket"), Client->SocketGeneration, Generation + 1);
	Ready();
	Client->bMapHandshakeComplete = true;
	Client->LoadoutRequestedAtSeconds = FPlatformTime::Seconds() - 6.0;
	Client->TickConnection();
	Ok &= TestEqual(TEXT("accepted setup never retries from an old timestamp"), Client->ConnectionState, ESimCoreConnectionState::Connected);
	Client->bLoadoutPending = true;
	Client->ConnectionState = ESimCoreConnectionState::Incompatible;
	Client->TickConnection();
	Ok &= TestEqual(TEXT("explicit incompatibility is not retried"), Client->ConnectionState, ESimCoreConnectionState::Incompatible);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreGarageProtocolTest,
	"DriveIntegration.Garage.LoadoutWireContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreGarageProtocolTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	const auto SerializeReset = [](const FString& Id)
	{
		return SerializeSimulationResetEnvelope(TEXT("garage-play"), 17, ERuntimeVehicleClass::Sedan,
			7, TEXT("garage-test"), TEXT("garage-connection"), TEXT("fnv1a64:0123456789abcdef"), Id);
	};
	bool Ok = TestTrue(TEXT("empty loadout preserves legacy reset bytes without field 4"),
		SerializeReset(FString()) == GarageResetFixture(FString()));
	const FString Id = TEXT("sedan_modular_standard");
	Ok &= TestTrue(TEXT("selected ID is encoded in SimulationReset field 4"), SerializeReset(Id) == GarageResetFixture(Id));
	const FString InvalidIds[] = {TEXT("Uppercase"), TEXT("0_leading"), TEXT("contains-hyphen"),
		TEXT("with space"), FString::ChrN(65, TEXT('a'))};
	for (const FString& Invalid : InvalidIds)
		Ok &= TestTrue(TEXT("malformed reset ID produces no outbound envelope"), SerializeReset(Invalid).IsEmpty());

	FVehicleState Parsed;
	FString Error;
	Ok &= TestTrue(TEXT("old WorldState without field 41 remains readable"),
		ParseWorldStateEnvelope(GarageWorldFixture({}), 1, Parsed, Error));
	Ok &= TestTrue(TEXT("old WorldState has the default loadout"), Parsed.VehicleLoadoutId.IsEmpty());
	TArray<uint8> Field;
	GarageText(Field, 41, Id);
	Ok &= TestTrue(TEXT("EntityState field 41 parses selected loadout"),
		ParseWorldStateEnvelope(GarageWorldFixture(Field), 1, Parsed, Error));
	Ok &= TestEqual(TEXT("authoritative loadout ID preserved exactly"), Parsed.VehicleLoadoutId, Id);
	GarageText(Field, 41, Id);
	Ok &= TestFalse(TEXT("duplicate entity loadout fields fail closed"),
		ParseWorldStateEnvelope(GarageWorldFixture(Field), 1, Parsed, Error));
	Ok &= TestTrue(TEXT("failed parse clears previously accepted loadout"), Parsed.VehicleLoadoutId.IsEmpty());
	Field.Reset();
	GarageInteger(Field, 41, 1);
	Ok &= TestFalse(TEXT("wrong loadout wire type rejected"),
		ParseWorldStateEnvelope(GarageWorldFixture(Field), 1, Parsed, Error));
	for (const FString& Invalid : InvalidIds)
	{
		Field.Reset();
		GarageText(Field, 41, Invalid);
		Ok &= TestFalse(TEXT("malformed authoritative loadout ID rejected"),
			ParseWorldStateEnvelope(GarageWorldFixture(Field), 1, Parsed, Error));
	}
	Field.Reset();
	GarageText(Field, 41, FString());
	Ok &= TestTrue(TEXT("explicit empty field 41 represents default setup"),
		ParseWorldStateEnvelope(GarageWorldFixture(Field), 1, Parsed, Error));
	Ok &= TestTrue(TEXT("explicit default cannot retain the previous ID"), Parsed.VehicleLoadoutId.IsEmpty());
	return Ok;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreGaragePartsDraftTest,
	"DriveIntegration.Garage.IndividualPartsDraftLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreGaragePartsDraftTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
		.SetTransactional(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GaragePartsQa")),
		GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("isolated parts UI world"), World)) return false;
	AExternalVehiclePawn* Pawn = World->SpawnActor<AExternalVehiclePawn>();
	if (!TestNotNull(TEXT("parts UI pawn"), Pawn)) { World->DestroyWorld(false); return false; }
	USimCoreClientComponent* Client = Pawn->SimCoreClient;
	Client->SelectedVehicleClass = ERuntimeVehicleClass::Sedan;
	Client->SelectedLoadoutId = TEXT("sedan_modular_standard");
	Client->PlaySessionId = TEXT("parts-ui-play");
	Client->bServerSupportsLoadouts = true;
	Client->bServerSupportsParts = true;
	Client->bServerMatchesCatalog = true;
	Client->ConnectionState = ESimCoreConnectionState::Connected;
	Client->bHasState = true;
	Client->LatestStateReceiveTimeSeconds = FPlatformTime::Seconds();
	Client->LatestState.ServerHealth.bPresent = true;
	Client->LatestState.ServerHealth.Status = EServerHealthStatus::Active;
	UInputComponent* Input = NewObject<UInputComponent>(Pawn);
	Pawn->SetupPlayerInputComponent(Input);
	bool Ok = true;
	for (const FName Action : {FName(TEXT("GarageParts")), FName(TEXT("GaragePartPrevious")), FName(TEXT("GaragePartNext"))})
	{
		bool Bound = false;
		for (int32 Index = 0; Index < Input->GetNumActionBindings(); ++Index)
			Bound |= Input->GetActionBinding(Index).GetActionName() == Action;
		Ok &= TestTrue(TEXT("individual parts keyboard action bound"), Bound);
	}
	Pawn->ToggleGarage();
	Ok &= TestFalse(TEXT("named preset opens existing preset list"), Pawn->IsGaragePartsMode());
	Pawn->ToggleGarageParts();
	Ok &= TestTrue(TEXT("modular preset enables parts preview"), Pawn->IsGaragePartsMode());
	Ok &= TestEqual(TEXT("draft contains all eight explicit slots"), Pawn->GaragePartsDraft.PartIds.Num(), 8);
	if (Pawn->GaragePartsDraft.PartIds.Num() != 8) { World->DestroyWorld(false); return false; }
	Ok &= TestEqual(TEXT("unchanged draft canonicalizes to named preset"), Pawn->GaragePartsLoadoutId, FString(TEXT("sedan_modular_standard")));
	for (int32 Attempts = 0; Attempts < Pawn->GaragePartChoices.Num()
		&& Pawn->GaragePartsDraft.PartIds[0] != TEXT("tire_sedan_comfort"); ++Attempts)
		Pawn->GaragePartNext();
	const FString CustomId = TEXT("parts_v1_01_0504070401080002");
	Ok &= TestEqual(TEXT("front-only preview produces cross-language golden token"), Pawn->GaragePartsLoadoutId, CustomId);
	Ok &= TestEqual(TEXT("preview does not alter committed client setup"), Client->SelectedLoadoutId, FString(TEXT("sedan_modular_standard")));
	Ok &= TestEqual(TEXT("preview does not start another play"), Client->PlaySessionId, FString(TEXT("parts-ui-play")));
	Ok &= TestFalse(TEXT("preview does not send a pending request"), Client->IsLoadoutPending());
	Ok &= TestTrue(TEXT("selected part exposes physical summary"), Pawn->GaragePartChoices.IsValidIndex(Pawn->GaragePartChoice)
		&& !Pawn->GaragePartChoices[Pawn->GaragePartChoice].Summary.IsEmpty());
	Pawn->GaragePartSlot = 4;
	Pawn->RefreshGarageParts();
	Ok &= TestEqual(TEXT("one authored engine remains one explicit option"), Pawn->GaragePartChoices.Num(), 1);
	const FString Engine = Pawn->GaragePartsDraft.PartIds[4];
	Pawn->GaragePartNext();
	Ok &= TestEqual(TEXT("single-option arrow is a no-op"), Pawn->GaragePartsDraft.PartIds[4], Engine);
	Ok &= TestTrue(TEXT("single-option limitation is explained"), Pawn->GarageMessage.Contains(TEXT("Only one")));
	Pawn->GaragePartsDraft.PartIds[0] = Engine;
	Pawn->RefreshGarageParts();
	Ok &= TestFalse(TEXT("invalid combination exposes validation reason"), Pawn->GaragePartsError.IsEmpty());
	Pawn->ApplyGarageSelection();
	Ok &= TestFalse(TEXT("invalid preview never starts a reset"), Client->IsLoadoutPending());
	Pawn->ToggleGarageParts();
	Ok &= TestFalse(TEXT("Tab returns to presets"), Pawn->IsGaragePartsMode());
	Ok &= TestTrue(TEXT("abandoned draft is removed"), Pawn->GaragePartsDraft.PartIds.IsEmpty() && Pawn->GaragePartsLoadoutId.IsEmpty());
	Pawn->GarageSelection = 0;
	Pawn->ToggleGarageParts();
	Ok &= TestFalse(TEXT("default has no fabricated modular base"), Pawn->IsGaragePartsMode());
	Ok &= TestFalse(TEXT("default explains how to choose a modular preset"), Pawn->GarageMessage.IsEmpty());
	Pawn->CloseGarage();
	Client->SelectedLoadoutId = CustomId;
	Pawn->ToggleGarage();
	Ok &= TestTrue(TEXT("committed custom setup reopens parts mode"), Pawn->IsGaragePartsMode());
	Ok &= TestEqual(TEXT("custom reopening recovers base preset"), Pawn->GaragePartsDraft.BaseLoadoutId, FString(TEXT("sedan_modular_standard")));
	Ok &= TestEqual(TEXT("custom reopening recovers exact token"), Pawn->GaragePartsLoadoutId, CustomId);
	Pawn->GaragePartNext();
	Pawn->CloseGarage();
	Pawn->ToggleGarage();
	Ok &= TestEqual(TEXT("closing discards edits and reopens committed selection"), Pawn->GaragePartsLoadoutId, CustomId);
	Client->bLoadoutPending = true;
	const auto BeforePending = Pawn->GaragePartsDraft.PartIds;
	Pawn->GaragePartNext();
	Pawn->ToggleGarageParts();
	Ok &= TestTrue(TEXT("pending request blocks draft edits and mode switch"), Pawn->IsGaragePartsMode() && Pawn->GaragePartsDraft.PartIds == BeforePending);
	Client->bLoadoutPending = false;
	Client->bServerSupportsParts = false;
	Pawn->GaragePartNext();
	Ok &= TestTrue(TEXT("unsupported server disables parts edits"), Pawn->GaragePartsDraft.PartIds == BeforePending);
	Ok &= TestTrue(TEXT("unsupported server reason is visible"), Pawn->GetGarageStatusText().Contains(TEXT("vehicle-parts.v1")));
	Pawn->CloseGarage();
	World->DestroyWorld(false);
	return Ok;
}
#endif
