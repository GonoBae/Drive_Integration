#include "SimCoreNpcPresentationActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "SimCorePresentation.h"
#include "SimCoreSedanVisualContract.h"
#include "SimCoreTrafficSignalActor.h"
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* SedanBody = nullptr;
	UStaticMesh* SedanWheel = nullptr;
	if (FPackageName::DoesPackageExist(
		SimCoreSedanVisualContract::BodyPackagePath()))
	{
		SedanBody = LoadObject<UStaticMesh>(
			nullptr, SimCoreSedanVisualContract::BodyObjectPath());
	}
	if (FPackageName::DoesPackageExist(
		SimCoreSedanVisualContract::WheelPackagePath()))
	{
		SedanWheel = LoadObject<UStaticMesh>(
			nullptr, SimCoreSedanVisualContract::WheelObjectPath());
	}
	bHasAuthoredSedan = SedanBody && SedanWheel;
	Body->SetStaticMesh(bHasAuthoredSedan ? SedanBody : Cube.Object.Get());
	// These are the same CG-local centimetre-space axle origins as the authored
	// Ego model. Conservative server OBB extents never stretch the 270 cm rig.
	const TConstArrayView<FVector> WheelOrigins =
		SimCoreSedanVisualContract::WheelOriginsCm();
	if (bHasAuthoredSedan)
	{
		AuthoredBounds = SedanBody->GetBoundingBox();
		TireRadiusMeters = static_cast<float>(SedanWheel->GetBoundingBox().GetExtent().Z * 0.01);
	}
	for (int32 Index = 0; Index < WheelOrigins.Num(); ++Index)
	{
		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("Wheel%d"), Index));
		Wheel->SetupAttachment(ModelRoot);
		Wheel->SetRelativeLocation(WheelOrigins[Index]);
		Wheel->SetStaticMesh(SedanWheel);
		Wheel->SetVisibility(bHasAuthoredSedan);
		Wheels.Add(Wheel);
		if (bHasAuthoredSedan)
		{
			const FBox WheelBounds = SedanWheel->GetBoundingBox();
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
	if (!bValid || !SimCorePresentation::BuildRuntimeEntitySample(State,
		bMotionAllowed ? SnapshotAgeSeconds : 0.0f, MaxExtrapolationSeconds, PresentationOffsetCm, Sample))
	{
		SetActorHiddenInGame(true);
		return false;
	}
	FVector ModelOffset = FVector::ZeroVector;
	if (bHasAuthoredSedan && !SimCoreNpcPresentation::BuildAuthoredModelOffset(
		AuthoredBounds, State.CollisionHalfHeightMeters, ModelOffset))
	{
		SetActorHiddenInGame(true);
		return false;
	}
	EntityId = State.EntityId;
	SetActorHiddenInGame(false);
	SetActorLocationAndRotation(Sample.ActorLocation, Sample.ActorRotation, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorScale3D(FVector::OneVector);
	ModelRoot->SetRelativeLocation(ModelOffset);
	ModelRoot->SetRelativeScale3D(bHasAuthoredSedan ? FVector::OneVector : Sample.ActorScale);
	if (bHasAuthoredSedan && bMotionAllowed)
	{
		const double Heading = FMath::DegreesToRadians(static_cast<double>(State.HeadingDegrees));
		const double SignedForwardSpeed = State.LinearVelocityEnu.X * FMath::Sin(Heading)
			+ State.LinearVelocityEnu.Y * FMath::Cos(Heading);
		const float AngularSpeed = static_cast<float>(SignedForwardSpeed / FMath::Max(TireRadiusMeters, 0.01f));
		WheelSpinDegrees = SimCorePresentation::AdvanceWheelSpinDegrees(
			WheelSpinDegrees, AngularSpeed, FMath::Min(DeltaSeconds, 0.1f));
		for (UStaticMeshComponent* Wheel : Wheels)
		{
			Wheel->SetRelativeRotation(SimCorePresentation::BuildWheelSpinRelativeRotation(WheelSpinDegrees));
		}
	}
	return true;
}

UStaticMeshComponent* ASimCoreNpcPresentationActor::GetWheel(int32 Index) const
{
	return Wheels.IsValidIndex(Index) ? Wheels[Index].Get() : nullptr;
}
