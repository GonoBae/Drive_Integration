#include "SimCoreNpcPresentationActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SimCorePresentation.h"
#include "SimCoreDeformableBody.h"
#include "SimCoreDriverPresentation.h"
#include "SimCoreTurnSignals.h"
#include "SimCoreSedanVisualContract.h"
#include "SimCoreTrafficSignalActor.h"
#include "SimCoreVehicleHorn.h"
#include "UObject/ConstructorHelpers.h"

bool SimCoreNpcPresentation::BuildAuthoredModelOffset(
	const FBox& Bounds, float ObbHalfHeightMeters, FVector& OutOffsetCm)
{
	OutOffsetCm = FVector::ZeroVector;
	if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()
		|| !FMath::IsFinite(ObbHalfHeightMeters) || ObbHalfHeightMeters <= 0.0f
		|| Bounds.GetSize().GetMin() <= KINDA_SMALL_NUMBER) return false;
	const FVector Centre = Bounds.GetCenter();
	OutOffsetCm = FVector(-Centre.X, -Centre.Y,
		-ObbHalfHeightMeters * 100.0 - ObbBottomAboveGroundCm - Bounds.Min.Z);
	return !OutOffsetCm.ContainsNaN();
}

ASimCoreNpcPresentationActor::ASimCoreNpcPresentationActor()
{
	PrimaryActorTick.bCanEverTick = false; // The owning client ticks accepted snapshots.
	bReplicates = false;
	SetActorEnableCollision(false);
	Tags.Add(TEXT("SimCoreRuntimeEntity"));
	Tags.Add(TEXT("SimCoreRuntimeNpc"));
	NpcRoot = CreateDefaultSubobject<USceneComponent>(TEXT("NpcRoot"));
	SetRootComponent(NpcRoot);
	NpcRoot->SetMobility(EComponentMobility::Movable);
	ModelRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ModelRoot"));
	ModelRoot->SetupAttachment(NpcRoot);
	ModelRoot->SetMobility(EComponentMobility::Movable);
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(ModelRoot);
	DeformableBody = CreateDefaultSubobject<USimCoreDeformableBody>(TEXT("DeformableBody"));
	DeformableBody->SetupAttachment(ModelRoot);
	TurnSignals = CreateDefaultSubobject<USimCoreTurnSignals>(TEXT("TurnSignals"));
	TurnSignals->SetupAttachment(ModelRoot);
	TurnSignals->BindBodies(Body, DeformableBody);
	VehicleHorn = CreateDefaultSubobject<USimCoreVehicleHornComponent>(TEXT("VehicleHorn"));
	VehicleHorn->SetupAttachment(ModelRoot);
	VehicleHorn->SetRelativeLocation(FVector(185.0, 0.0, 20.0));
	DriverPresentation = CreateDefaultSubobject<USimCoreDriverPresentation>(TEXT("DriverPresentation"));
	DriverPresentation->SetupAttachment(ModelRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* SedanDriverDoor = nullptr;
	if (FPackageName::DoesPackageExist(
		SimCoreSedanVisualContract::BodyPackagePath()))
	{
		SedanBodyMesh = LoadObject<UStaticMesh>(
			nullptr, SimCoreSedanVisualContract::BodyObjectPath());
	}
	if (FPackageName::DoesPackageExist(
		SimCoreSedanVisualContract::DriverDoorPackagePath()))
	{
		SedanDriverDoor = LoadObject<UStaticMesh>(
			nullptr, SimCoreSedanVisualContract::DriverDoorObjectPath());
	}
	if (FPackageName::DoesPackageExist(
		SimCoreSedanVisualContract::WheelPackagePath()))
	{
		SharedWheelMesh = LoadObject<UStaticMesh>(
			nullptr, SimCoreSedanVisualContract::WheelObjectPath());
	}
	auto LoadFleetMesh = [](const TCHAR* Name) -> UStaticMesh*
	{
		const FString PackagePath = FString(TEXT("/Game/Vehicles/NpcFleet/")) + Name;
		return FPackageName::DoesPackageExist(PackagePath)
			? LoadObject<UStaticMesh>(nullptr, *(PackagePath + TEXT(".") + Name))
			: nullptr;
	};
	CompactBodyMesh = LoadFleetMesh(TEXT("SM_CompactBody"));
	TruckBodyMesh = LoadFleetMesh(TEXT("SM_TruckBody"));
	MotorcycleBodyMesh = LoadFleetMesh(TEXT("SM_MotorcycleBody"));
	bHasAuthoredSedan = SedanBodyMesh && SedanDriverDoor && SharedWheelMesh;
	bHasAuthoredFleet = CompactBodyMesh && TruckBodyMesh && MotorcycleBodyMesh;
	Body->SetStaticMesh(bHasAuthoredSedan ? SedanBodyMesh.Get() : Cube.Object.Get());
	DriverPresentation->SetPresentationEnabled(bHasAuthoredSedan);
	Body->SetEvaluateWorldPositionOffset(true);
	Body->SetWorldPositionOffsetDisableDistance(0);
	// These are the same CG-local centimetre-space axle origins as the authored
	// Ego model. Conservative server OBB extents never stretch the 270 cm rig.
	const TConstArrayView<FVector> WheelOrigins =
		SimCoreSedanVisualContract::WheelOriginsCm();
	if (bHasAuthoredSedan)
	{
		AuthoredBounds = SedanBodyMesh->GetBoundingBox();
		// The left mirror and complete front door now belong to the hinged mesh.
		// Include its closed-pose bounds so centering remains symmetric.
		AuthoredBounds += SedanDriverDoor->GetBoundingBox();
		TireRadiusMeters = static_cast<float>(SharedWheelMesh->GetBoundingBox().GetExtent().Z * 0.01);
	}
	for (int32 Index = 0; Index < WheelOrigins.Num(); ++Index)
	{
		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("Wheel%d"), Index));
		Wheel->SetupAttachment(ModelRoot);
		Wheel->SetRelativeLocation(WheelOrigins[Index]);
		Wheel->SetStaticMesh(SharedWheelMesh);
		Wheel->SetVisibility(bHasAuthoredSedan);
		Wheels.Add(Wheel);
		if (bHasAuthoredSedan)
		{
			const FBox WheelBounds = SharedWheelMesh->GetBoundingBox();
			AuthoredBounds += FBox(WheelBounds.Min + WheelOrigins[Index], WheelBounds.Max + WheelOrigins[Index]);
		}
	}
	TArray<UStaticMeshComponent*> Meshes = {Body.Get()};
	for (UStaticMeshComponent* Wheel : Wheels) Meshes.Add(Wheel);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetSimulatePhysics(false);
	}
}

void ASimCoreNpcPresentationActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	const bool bGameWorld = GetWorld() && GetWorld()->IsGameWorld();
	SetActorHiddenInGame(!bGameWorld);
#if WITH_EDITOR
	SetIsTemporarilyHiddenInEditor(!bGameWorld);
#endif
}

void ASimCoreNpcPresentationActor::BeginPlay()
{
	Super::BeginPlay();
	VehicleHorn->ResetHornState();
	bHornSequenceInitialized = false;
	HornPlaySessionId.Reset();
	HornMapPackageChecksum.Reset();
	VehicleHorn->Start();
}

void ASimCoreNpcPresentationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	VehicleHorn->ResetHornState();
	VehicleHorn->Stop();
	Super::EndPlay(EndPlayReason);
}

bool ASimCoreNpcPresentationActor::ConfigureVehicleClass(
	SimCoreProtocol::ERuntimeVehicleClass VehicleClass)
{
	if (VehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Unspecified)
	{
		VehicleClass = SimCoreProtocol::ERuntimeVehicleClass::Sedan;
	}
	UStaticMesh* TargetBody = nullptr;
	switch (VehicleClass)
	{
	case SimCoreProtocol::ERuntimeVehicleClass::Sedan: TargetBody = SedanBodyMesh; break;
	case SimCoreProtocol::ERuntimeVehicleClass::Compact: TargetBody = CompactBodyMesh; break;
	case SimCoreProtocol::ERuntimeVehicleClass::Truck: TargetBody = TruckBodyMesh; break;
	case SimCoreProtocol::ERuntimeVehicleClass::Motorcycle: TargetBody = MotorcycleBodyMesh; break;
	default: return false;
	}
	if (!TargetBody || !SharedWheelMesh) return false;
	if (RuntimeVehicleClass == VehicleClass && Body->GetStaticMesh() == TargetBody)
	{
		return true;
	}

	Body->EmptyOverrideMaterials();
	Body->SetStaticMesh(TargetBody);
	Body->SetRelativeTransform(FTransform::Identity);
	Body->SetVisibility(true);
	DeformableBody->ClearAllMeshSections();
	DeformableBody->SetVisibility(false);
	DamageMaterials.Reset();
	DamageAccumulator = {};

	TArray<FVector, TInlineAllocator<4>> WheelLocations;
	TArray<FVector, TInlineAllocator<4>> WheelScales;
	TArray<FVector, TInlineAllocator<4>> LampPositions;
	const TConstArrayView<FVector> SedanWheels = SimCoreSedanVisualContract::WheelOriginsCm();
	if (VehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Sedan)
	{
		PresentationHalfHeightMeters = 0.75f;
		WheelLocations.Append(SedanWheels.GetData(), SedanWheels.Num());
		for (int32 Index = 0; Index < 4; ++Index) WheelScales.Add(FVector::OneVector);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			LampPositions.Add(SimCoreSedanVisualContract::TurnSignalLensPointCm(
				Index < 2, Index % 2 == 0, 0.5, 0.5));
		}
	}
	else if (VehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Compact)
	{
		PresentationHalfHeightMeters = 0.70f;
		for (const FVector& Origin : SedanWheels)
		{
			WheelLocations.Add(FVector(Origin.X * 0.79, Origin.Y * 0.90,
				Origin.Z * 0.92 - 1.5));
			WheelScales.Add(FVector(0.86, 0.82, 0.86));
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector SedanLamp = SimCoreSedanVisualContract::TurnSignalLensPointCm(
				Index < 2, Index % 2 == 0, 0.5, 0.5);
			LampPositions.Add(FVector(SedanLamp.X * 0.79 - 4.0,
				SedanLamp.Y * 0.90, SedanLamp.Z * 0.92 - 1.5));
		}
	}
	else if (VehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Truck)
	{
		PresentationHalfHeightMeters = 1.25f;
		WheelLocations = {FVector(190, -102, -28), FVector(190, 102, -28),
			FVector(-195, -102, -28), FVector(-195, 102, -28)};
		for (int32 Index = 0; Index < 4; ++Index) WheelScales.Add(FVector(1.22, 1.08, 1.22));
		LampPositions = {FVector(240, -96, 58), FVector(240, 96, 58),
			FVector(-294, -96, 64), FVector(-294, 96, 64)};
	}
	else
	{
		PresentationHalfHeightMeters = 0.68f;
		WheelLocations = {FVector(99, 0, -8), FVector::ZeroVector,
			FVector(-82, 0, -8), FVector::ZeroVector};
		WheelScales = {FVector(1.05, 0.42, 1.05), FVector::OneVector,
			FVector(1.05, 0.42, 1.05), FVector::OneVector};
		LampPositions = {FVector(93, -29, 61), FVector(93, 29, 61),
			FVector(-94, -27, 47), FVector(-94, 27, 47)};
	}

	AuthoredBounds = TargetBody->GetBoundingBox();
	for (int32 Index = 0; Index < Wheels.Num(); ++Index)
	{
		const bool bVisible = VehicleClass != SimCoreProtocol::ERuntimeVehicleClass::Motorcycle
			|| Index == 0 || Index == 2;
		Wheels[Index]->SetVisibility(bVisible);
		Wheels[Index]->SetRelativeLocation(WheelLocations[Index]);
		Wheels[Index]->SetRelativeScale3D(WheelScales[Index]);
		Wheels[Index]->SetRelativeRotation(FRotator::ZeroRotator);
		if (bVisible)
		{
			AuthoredBounds += SharedWheelMesh->GetBoundingBox().TransformBy(
				FTransform(FQuat::Identity, WheelLocations[Index], WheelScales[Index]));
		}
	}
	TireRadiusMeters = static_cast<float>(SharedWheelMesh->GetBoundingBox().GetExtent().Z
		* WheelScales[0].Z * 0.01);
	VehicleHorn->SetRelativeLocation(FVector(AuthoredBounds.Max.X - 18.0, 0.0,
		FMath::Clamp(AuthoredBounds.GetCenter().Z, 18.0, 80.0)));
	TurnSignals->SetLampPositions(LampPositions);
	DriverPresentation->SetRelativeTransform(FTransform::Identity);
	DriverPresentation->SetPresentationEnabled(
		VehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Sedan && bHasAuthoredSedan);
	RuntimeVehicleClass = VehicleClass;
	return AuthoredBounds.IsValid != 0;
}

bool ASimCoreNpcPresentationActor::ApplySnapshot(const SimCoreProtocol::FVehicleState& State,
	float SnapshotAgeSeconds, float DeltaSeconds, bool bMotionAllowed,
	float MaxExtrapolationSeconds, const FVector& PresentationOffsetCm)
{
	const bool bValid = GetWorld() && GetWorld()->IsGameWorld()
		&& State.EntityKind == SimCoreProtocol::EEntityKind::NpcVehicle && State.EntityId != 0
		&& FMath::IsFinite(SnapshotAgeSeconds) && SnapshotAgeSeconds >= 0.0f
		&& SnapshotAgeSeconds <= SimCoreTrafficSignals::SnapshotFreshnessSeconds
		&& FMath::IsFinite(DeltaSeconds) && DeltaSeconds >= 0.0f
		&& FMath::IsFinite(MaxExtrapolationSeconds) && MaxExtrapolationSeconds >= 0.0f;
	SimCorePresentation::FRuntimeEntityPresentationSample Sample;
	// Tilt and support height are one server-authored grounded pose. Predicting
	// angle without the matching ground-height solve can put the roof/wheels
	// through the ground between snapshots. Use that complete accepted pose.
	const bool bTumbling = FMath::Abs(State.PitchDegrees) > 0.01f
		|| FMath::Abs(State.RollDegrees) > 0.01f
		|| FMath::Abs(State.AngularVelocityBody.X) > 0.001
		|| FMath::Abs(State.AngularVelocityBody.Y) > 0.001;
	if (!bValid || !SimCorePresentation::BuildRuntimeEntitySample(State,
		bMotionAllowed && !bTumbling ? SnapshotAgeSeconds : 0.0f,
		MaxExtrapolationSeconds, PresentationOffsetCm, Sample))
	{
		SetActorHiddenInGame(true);
		return false;
	}
	if (!ConfigureVehicleClass(State.RuntimeVehicleClass))
	{
		SetActorHiddenInGame(true);
		return false;
	}
	FVector ModelOffset = FVector::ZeroVector;
	if (!SimCoreNpcPresentation::BuildAuthoredModelOffset(
		AuthoredBounds, PresentationHalfHeightMeters, ModelOffset))
	{
		SetActorHiddenInGame(true);
		return false;
	}
	EntityId = State.EntityId;
	SetActorHiddenInGame(false);
	SetActorLocationAndRotation(Sample.ActorLocation, Sample.ActorRotation, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorScale3D(FVector::OneVector);
	ModelRoot->SetRelativeLocation(ModelOffset);
	ModelRoot->SetRelativeScale3D(FVector::OneVector);
	UpdateDamage(State);
	DriverPresentation->ApplyAuthoritativeState(State);
	TurnSignals->UpdateSignal(State.TurnIndicator, State.SimulationTimeNs * 1.0e-9
		+ (bMotionAllowed ? SnapshotAgeSeconds : 0.0f),
		State.RuntimeRecoveryPhase == SimCoreProtocol::ERuntimeRecoveryPhase::Disabled);
	const bool bNewHornLifecycle = !bHornSequenceInitialized
		|| HornPlaySessionId != State.PlaySessionId
		|| HornMapPackageChecksum != State.MapPackageChecksum;
	if (bNewHornLifecycle)
	{
		VehicleHorn->ResetHornState();
		VehicleHorn->BaselineAuthoritativeEvent(State.HornEventSequence);
		HornPlaySessionId = State.PlaySessionId;
		HornMapPackageChecksum = State.MapPackageChecksum;
		bHornSequenceInitialized = true;
	}
	else
	{
		VehicleHorn->ObserveAuthoritativeEvent(State.HornEventSequence);
	}
	if (SharedWheelMesh && bMotionAllowed
		&& FMath::Abs(State.PitchDegrees) < 20.0f && FMath::Abs(State.RollDegrees) < 20.0f)
	{
		const double Heading = FMath::DegreesToRadians(static_cast<double>(State.HeadingDegrees));
		const double SignedForwardSpeed = State.LinearVelocityEnu.X * FMath::Sin(Heading)
			+ State.LinearVelocityEnu.Y * FMath::Cos(Heading);
		const float AngularSpeed = static_cast<float>(SignedForwardSpeed / FMath::Max(TireRadiusMeters, 0.01f));
		WheelSpinDegrees = SimCorePresentation::AdvanceWheelSpinDegrees(
			WheelSpinDegrees, AngularSpeed, FMath::Min(DeltaSeconds, 0.1f));
		for (UStaticMeshComponent* Wheel : Wheels)
		{
			if (Wheel->IsVisible())
			{
				Wheel->SetRelativeRotation(
					SimCorePresentation::BuildWheelSpinRelativeRotation(WheelSpinDegrees));
			}
		}
	}
	return true;
}

void ASimCoreNpcPresentationActor::UpdateDamage(const SimCoreProtocol::FVehicleState& State)
{
	if (!bHasAuthoredSedan) return;
	if (DamageMaterials.IsEmpty())
	{
		for (int32 Index = 0; Index < Body->GetNumMaterials(); ++Index)
		{
			if (auto* Material = Body->CreateDynamicMaterialInstance(Index)) DamageMaterials.Add(Material);
		}
		DamageAccumulator = {};
	}
	if (!SimCoreDamagePresentation::Advance(DamageAccumulator, State)) return;
	DeformableBody->ApplyDamage(Body, DamageAccumulator.Weights);
	DriverPresentation->ApplyDoorDamage(DamageAccumulator.Weights);
	TurnSignals->SetBodyDamage(DamageAccumulator.Weights);
	for (UMaterialInstanceDynamic* Material : DamageMaterials)
	{
		if (!Material) continue;
		for (uint8 Index = 1; Index <= static_cast<uint8>(SimCoreProtocol::EVehicleDamageZone::Underbody); ++Index)
		{
			const auto Zone = static_cast<SimCoreProtocol::EVehicleDamageZone>(Index);
			Material->SetScalarParameterValue(SimCoreDamagePresentation::MaterialParameterName(Zone),
				DamageAccumulator.Weights.bContactLocal ? 0.0f : DamageAccumulator.Weights.Get(Zone));
		}
	}
}

UStaticMeshComponent* ASimCoreNpcPresentationActor::GetWheel(int32 Index) const
{
	return Wheels.IsValidIndex(Index) ? Wheels[Index].Get() : nullptr;
}

int32 ASimCoreNpcPresentationActor::GetVisibleWheelCount() const
{
	int32 Count = 0;
	for (const UStaticMeshComponent* Wheel : Wheels)
	{
		if (Wheel && Wheel->IsVisible()) ++Count;
	}
	return Count;
}
