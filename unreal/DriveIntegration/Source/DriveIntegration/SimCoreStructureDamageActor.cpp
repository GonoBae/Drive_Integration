#include "SimCoreStructureDamageActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SimCoreCoordinateFrames.h"
#include "UObject/ConstructorHelpers.h"

bool SimCoreStructureDamage::BuildImpactTransform(const SimCoreProtocol::FStructureState& Structure,
	const FVector& PresentationOffsetCm, FTransform& OutTransform)
{
	OutTransform = FTransform::Identity;
	if (!SimCoreProtocol::IsValidStructureState(Structure)
		|| Structure.Kind != SimCoreProtocol::EStructureKind::Building
		|| Structure.DamagePercent <= 0.0f || PresentationOffsetCm.ContainsNaN()) return false;
	const FVector Normal = FVector(SimCoreCoordinateFrames::MapEnuPolarVectorToUnrealWorld(
		Structure.ImpactNormalEnu)).GetSafeNormal();
	if (Normal.ContainsNaN() || Normal.IsNearlyZero()) return false;
	const FVector Reference = FMath::Abs(Normal.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector;
	const FVector Tangent = FVector::CrossProduct(Reference, Normal).GetSafeNormal();
	// The geometry lives outside the facade, not coplanar with its triangles.
	const FVector Origin = SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(
		Structure.ImpactPointEnu, PresentationOffsetCm) + Normal * 1.5;
	OutTransform = FTransform(FRotationMatrix::MakeFromXZ(Tangent, Normal).ToQuat(), Origin);
	return true;
}

ASimCoreStructureDamageActor::ASimCoreStructureDamageActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = false;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	Tags.Add(TEXT("SimCoreRuntimeStructureDamage"));
	DamageRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DamageRoot"));
	SetRootComponent(DamageRoot);
	DamageRoot->SetMobility(EComponentMobility::Movable);
	Cracks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FacadeCracks"));
	Debris = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FacadeChips"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	ColorMaterial = Material.Object;
	for (UInstancedStaticMeshComponent* Mesh : {Cracks.Get(), Debris.Get()})
	{
		Mesh->SetupAttachment(DamageRoot);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube.Object);
		Mesh->SetMaterial(0, ColorMaterial);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
	}
}

void ASimCoreStructureDamageActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	InitializeMaterials();
	const bool bGameWorld = GetWorld() && GetWorld()->IsGameWorld();
	SetActorHiddenInGame(!bGameWorld);
#if WITH_EDITOR
	SetIsTemporarilyHiddenInEditor(!bGameWorld);
#endif
}

void ASimCoreStructureDamageActor::InitializeMaterials()
{
	if (!ColorMaterial || CrackMaterial) return;
	CrackMaterial = UMaterialInstanceDynamic::Create(ColorMaterial, this);
	ChipMaterial = UMaterialInstanceDynamic::Create(ColorMaterial, this);
	if (CrackMaterial)
	{
		CrackMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.018f, 0.012f, 0.009f));
		Cracks->SetMaterial(0, CrackMaterial);
	}
	if (ChipMaterial)
	{
		ChipMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.58f, 0.55f, 0.49f));
		Debris->SetMaterial(0, ChipMaterial);
	}
}

void ASimCoreStructureDamageActor::BuildDamageMarks()
{
	Cracks->ClearInstances();
	for (const FDamageMark& Mark : DamageMarks)
	{
		FRandomStream Random(static_cast<int32>(GetTypeHash(ColliderId) ^ Mark.EventSequence * 2654435761u));
		const double Width = Mark.HalfExtentCm.X;
		const double Height = Mark.HalfExtentCm.Y;
		for (int32 Ray = 0; Ray < 6; ++Ray)
		{
			FVector Start(0.0, 0.0, 0.0);
			const double BaseAngle = Ray * UE_DOUBLE_PI / 3.0 + Random.FRandRange(-0.3f, 0.3f);
			for (int32 Segment = 0; Segment < 3; ++Segment)
			{
				const double Angle = BaseAngle + Random.FRandRange(-0.22f, 0.22f);
				const double Distance = (Segment + 1) / 3.0;
				const FVector End(FMath::Cos(Angle) * Width * Distance, FMath::Sin(Angle) * Height * Distance, 0.0);
				const FVector Delta = End - Start;
				const double Thickness = 0.8 + (3 - Segment) * (0.4 + Mark.Severity * 0.35);
				const FTransform Local(FRotator(0, FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)), 0),
					(Start + End) * 0.5, FVector(Delta.Size() / 100.0, Thickness / 100.0, 0.015));
				Cracks->AddInstance(Local * Mark.Frame, true);
				Start = End;
			}
		}
		// Wide, irregular shallow spall marks remain visible from the chase
		// camera. They cover only this bumper/side/corner footprint, not a fixed
		// building-wide radius; the map's solid shell remains untouched.
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const FTransform Local(FRotator(0, Random.FRandRange(-25.0f, 25.0f), 0),
				FVector(Random.FRandRange(-0.45f, 0.45f) * Width, Random.FRandRange(-0.4f, 0.4f) * Height, 0.1),
				FVector(Width * Random.FRandRange(0.25f, 0.48f) / 100.0,
					Height * Random.FRandRange(0.2f, 0.45f) / 100.0, 0.02));
			Cracks->AddInstance(Local * Mark.Frame, true);
		}
	}
}

void ASimCoreStructureDamageActor::SpawnDebris(const SimCoreProtocol::FStructureState& Structure,
	const FTransform& Frame)
{
	Debris->ClearInstances();
	Fragments.Reset();
	DebrisAgeSeconds = 0.0f;
	FRandomStream Random(static_cast<int32>(GetTypeHash(ColliderId) ^ uint32(Structure.EventSequence)));
	const float Severity = DamageMarks.Last().Severity;
	const int32 Count = FMath::Clamp(10 + FMath::FloorToInt(Severity * 14.0f),
		10, SimCoreStructureDamage::MaxDebrisFragments);
	const FVector2D Extent = DamageMarks.Last().HalfExtentCm;
	DebrisGroundHeightCm = SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(
		Structure.BasePositionEnu, PresentedOffsetCm).Z;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FFragment Fragment;
		Fragment.Position = Frame.TransformPosition(FVector(Random.FRandRange(-0.7f, 0.7f) * Extent.X,
			Random.FRandRange(-0.4f, 0.6f) * Extent.Y, 12.0));
		Fragment.Velocity = Frame.TransformVectorNoScale(FVector(Random.FRandRange(-160.0f, 160.0f),
			Random.FRandRange(-30.0f, 30.0f), Random.FRandRange(180.0f, 300.0f) + Severity * 220.0f))
			+ FVector::UpVector * Random.FRandRange(230.0f, 440.0f);
		Fragment.Rotation = FRotator(Random.FRandRange(-90.0f, 90.0f), Random.FRandRange(-90.0f, 90.0f), 0);
		Fragment.AngularVelocity = FRotator(120.0f + Index * 7.0f, 40.0f, 90.0f);
		Fragment.Scale = FVector(Random.FRandRange(0.07f, 0.15f), Random.FRandRange(0.055f, 0.11f), 0.055);
		Fragment.Position.Z = FMath::Max(Fragment.Position.Z, DebrisGroundHeightCm + 8.0);
		Debris->AddInstance(FTransform(Fragment.Rotation, Fragment.Position, Fragment.Scale), true);
		Fragments.Add(Fragment);
	}
	SetActorTickEnabled(!Fragments.IsEmpty());
}

void ASimCoreStructureDamageActor::ApplyAuthoritativeDamage(
	const SimCoreProtocol::FStructureState& Structure, const FVector& PresentationOffsetCm)
{
	const bool bGameWorld = GetWorld() && GetWorld()->IsGameWorld();
	// EditorPreview does not necessarily call PostInitializeComponents. Revoke
	// visibility here too, before the non-game-world early return.
	SetActorHiddenInGame(!bGameWorld);
#if WITH_EDITOR
	SetIsTemporarilyHiddenInEditor(!bGameWorld);
#endif
	FTransform Frame;
	if (!bGameWorld
		|| !SimCoreStructureDamage::BuildImpactTransform(Structure, PresentationOffsetCm, Frame)
		|| (bHasPresentedDamage && (ColliderId != Structure.ColliderId || Structure.EventSequence < LastEventSequence))) return;
	// This actor remains anchored at its first hit. Marks and fragments are
	// world-space; a later hit on the opposite facade cannot drag earlier
	// damage (or already flying chips) around the building.
	if (!bHasPresentedDamage) SetActorTransform(Frame);
	else if (!PresentationOffsetCm.Equals(PresentedOffsetCm, 0.0001))
	{
		const FVector Rebase = PresentationOffsetCm - PresentedOffsetCm;
		AddActorWorldOffset(Rebase);
		for (FDamageMark& Mark : DamageMarks) Mark.Frame.AddToTranslation(Rebase);
		for (FFragment& Fragment : Fragments) Fragment.Position += Rebase;
		DebrisGroundHeightCm += Rebase.Z;
	}
	PresentedOffsetCm = PresentationOffsetCm;
	const float Severity = Structure.ImpactSeverity > 0.0f
		? Structure.ImpactSeverity : FMath::Clamp(Structure.DamagePercent / 100.0f, 0.0f, 1.0f);
	const FVector2D ContactExtent(
		Structure.ImpactHalfWidthMeters > 0.0f ? Structure.ImpactHalfWidthMeters * 100.0 : 55.0,
		Structure.ImpactHalfHeightMeters > 0.0f ? Structure.ImpactHalfHeightMeters * 100.0 : 45.0);
	const FVector2D MarkExtent(ContactExtent.X * (0.75 + 0.25 * Severity),
		ContactExtent.Y * (0.45 + 0.55 * Severity));
	if (bHasPresentedDamage && Structure.EventSequence == LastEventSequence)
	{
		FDamageMark& Mark = DamageMarks.Last();
		const float GrownSeverity = FMath::Max(Mark.Severity, Severity);
		const FVector2D GrownExtent(FMath::Max(Mark.HalfExtentCm.X, MarkExtent.X),
			FMath::Max(Mark.HalfExtentCm.Y, MarkExtent.Y));
		if (!FMath::IsNearlyEqual(GrownSeverity, Mark.Severity, 0.0001f)
			|| !Mark.HalfExtentCm.Equals(GrownExtent, 0.01))
		{
			PresentedDamagePercent = Structure.DamagePercent;
			Mark.Severity = GrownSeverity;
			Mark.HalfExtentCm = GrownExtent;
			BuildDamageMarks();
		}
		return;
	}
	ColliderId = Structure.ColliderId;
	LastEventSequence = Structure.EventSequence;
	bHasPresentedDamage = true;
	PresentedDamagePercent = Structure.DamagePercent;
	++PresentedEventCount;
	InitializeMaterials();
	if (DamageMarks.Num() >= SimCoreStructureDamage::MaxDamageMarks) DamageMarks.RemoveAt(0);
	DamageMarks.Add({Frame, MarkExtent, Severity, Structure.EventSequence});
	BuildDamageMarks();
	SpawnDebris(Structure, Frame);
}

void ASimCoreStructureDamageActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f || Fragments.IsEmpty()) return;
	DebrisAgeSeconds += DeltaSeconds;
	if (DebrisAgeSeconds >= SimCoreStructureDamage::DebrisLifetimeSeconds)
	{
		Debris->ClearInstances();
		Fragments.Reset();
		SetActorTickEnabled(false);
		return;
	}
	const float Step = FMath::Min(DeltaSeconds, 0.1f);
	const double Fade = FMath::Clamp(double((SimCoreStructureDamage::DebrisLifetimeSeconds - DebrisAgeSeconds) / 0.35f), 0.0, 1.0);
	for (int32 Index = 0; Index < Fragments.Num(); ++Index)
	{
		FFragment& Fragment = Fragments[Index];
		if (!Fragment.bResting)
		{
			const FVector Start = Fragment.Position;
			FVector End = Start + Fragment.Velocity * Step + FVector(0, 0, -490.0 * Step * Step);
			Fragment.Velocity.Z -= 980.0 * Step;
			Fragment.Rotation += Fragment.AngularVelocity * Step;
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(SimCoreVisualDebris), false, this);
			if (GetOwner()) Query.AddIgnoredActor(GetOwner());
			const bool bHit = GetWorld() && GetWorld()->GetPhysicsScene()
				&& GetWorld()->LineTraceSingleByObjectType(Hit, Start, End,
					FCollisionObjectQueryParams::AllStaticObjects, Query);
			const double Radius = 4.0;
			if (bHit)
			{
				End = Hit.ImpactPoint + Hit.ImpactNormal * Radius;
				const double Toward = FVector::DotProduct(Fragment.Velocity, Hit.ImpactNormal);
				if (Toward < 0.0) Fragment.Velocity -= Hit.ImpactNormal * (1.35 * Toward);
				Fragment.Velocity *= 0.6;
				Fragment.bResting = Fragment.Velocity.SizeSquared() < 45.0 * 45.0;
			}
			else if (End.Z < DebrisGroundHeightCm + Radius)
			{
				// A bounded fallback for an unloaded floor / NullRHI QA world.
				// It never feeds forces or position corrections back to SimCore.
				End.Z = DebrisGroundHeightCm + Radius;
				Fragment.Velocity.Z = FMath::Abs(Fragment.Velocity.Z) * 0.3;
				Fragment.Velocity.X *= 0.55;
				Fragment.Velocity.Y *= 0.55;
				Fragment.bResting = Fragment.Velocity.SizeSquared() < 45.0 * 45.0;
			}
			Fragment.Position = End;
		}
		Debris->UpdateInstanceTransform(Index, FTransform(Fragment.Rotation, Fragment.Position,
			Fragment.Scale * Fade), true, Index == Fragments.Num() - 1, true);
	}
}

int32 ASimCoreStructureDamageActor::GetDebrisCount() const { return Debris->GetInstanceCount(); }
int32 ASimCoreStructureDamageActor::GetCrackCount() const { return Cracks->GetInstanceCount(); }
FVector ASimCoreStructureDamageActor::GetDamageMarkLocation(int32 Index) const
{
	return DamageMarks.IsValidIndex(Index) ? DamageMarks[Index].Frame.GetLocation() : FVector::ZeroVector;
}
FVector2D ASimCoreStructureDamageActor::GetDamageMarkHalfExtentCm(int32 Index) const
{
	return DamageMarks.IsValidIndex(Index) ? DamageMarks[Index].HalfExtentCm : FVector2D::ZeroVector;
}
bool ASimCoreStructureDamageActor::GetDebrisWorldTransform(int32 Index, FTransform& Transform) const
{
	return Debris->GetInstanceTransform(Index, Transform, true);
}
