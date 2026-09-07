#include "SimCoreDriverPresentation.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Math/RotationMatrix.h"
#include "ProceduralMeshComponent.h"
#include "ReferenceSkeleton.h"
#include "SimCoreDamagePresentation.h"
#include "SimCoreDeformableBody.h"
#include "SimCoreSedanVisualContract.h"
#include "SimCoreVehicleVisualProfile.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float DriverScale = 0.78f;
	// Keep a full hand-width below the authored roof. The pelvis still sits just
	// above the 20 cm cushion top, so lowering the whole body cannot intersect
	// the cabin floor while preventing the head from clipping through the glass.
	const FVector DriverPelvisCm(-31.0, -34.0, 22.0);
	const FVector SteeringCentreCm(18.0, -34.0, 54.0);
	const FVector DoorHingeCm(63.0, -88.0, 25.0);
	constexpr float DoorOpenDegrees = 68.0f;
	constexpr float ExitSequencePerSecond = 0.65f;

	bool IsFiniteState(const SimCoreProtocol::FVehicleState& State)
	{
		return FMath::IsFinite(State.DamagePercent)
			&& FMath::IsFinite(State.LastImpactImpulseNs);
	}

	FTransform ReferenceComponentTransform(
		const FReferenceSkeleton& Skeleton, const int32 BoneIndex)
	{
		FTransform Result = Skeleton.GetRefBonePose()[BoneIndex];
		for (int32 Parent = Skeleton.GetParentIndex(BoneIndex);
			Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
		{
			Result *= Skeleton.GetRefBonePose()[Parent];
		}
		return Result;
	}

	FTransform RotatedAround(
		const FTransform& Transform, const FVector& Pivot, const FQuat& Rotation)
	{
		FTransform Result = Transform;
		Result.SetLocation(Pivot + Rotation.RotateVector(Transform.GetLocation() - Pivot));
		Result.SetRotation((Rotation * Transform.GetRotation()).GetNormalized());
		return Result;
	}

	void AddTorus(
		TArray<FVector>& Vertices, TArray<int32>& Triangles,
		TArray<FVector>& Normals, TArray<FVector2D>& Uvs)
	{
		constexpr int32 MajorSegments = 32;
		constexpr int32 MinorSegments = 8;
		constexpr float MajorRadius = 15.5f;
		constexpr float MinorRadius = 1.7f;
		for (int32 Major = 0; Major <= MajorSegments; ++Major)
		{
			const float U = static_cast<float>(Major) / MajorSegments;
			const float A = U * UE_TWO_PI;
			for (int32 Minor = 0; Minor <= MinorSegments; ++Minor)
			{
				const float V = static_cast<float>(Minor) / MinorSegments;
				const float B = V * UE_TWO_PI;
				const FVector Normal(
					FMath::Cos(A) * FMath::Cos(B),
					FMath::Sin(A) * FMath::Cos(B),
					FMath::Sin(B));
				Vertices.Add(FVector(
					FMath::Cos(A) * (MajorRadius + MinorRadius * FMath::Cos(B)),
					FMath::Sin(A) * (MajorRadius + MinorRadius * FMath::Cos(B)),
					MinorRadius * FMath::Sin(B)));
				Normals.Add(Normal);
				Uvs.Add(FVector2D(U, V));
			}
		}
		for (int32 Major = 0; Major < MajorSegments; ++Major)
		{
			for (int32 Minor = 0; Minor < MinorSegments; ++Minor)
			{
				const int32 A = Major * (MinorSegments + 1) + Minor;
				const int32 B = A + MinorSegments + 1;
				Triangles.Add(A);
				Triangles.Add(B);
				Triangles.Add(A + 1);
				Triangles.Add(A + 1);
				Triangles.Add(B);
				Triangles.Add(B + 1);
			}
		}
	}

	void SolveArm(
		UPoseableMeshComponent* Mesh,
		const FReferenceSkeleton& Skeleton,
		const FQuat& UpperBodyRotation,
		const FVector& Pelvis,
		const FTransform& DriverTransform,
		const bool bLeft,
		const FVector& HandVehicle,
		const FVector& ElbowHintVehicle)
	{
		const FName UpperName(bLeft ? TEXT("upperarm_l") : TEXT("upperarm_r"));
		const FName LowerName(bLeft ? TEXT("lowerarm_l") : TEXT("lowerarm_r"));
		const FName HandName(bLeft ? TEXT("hand_l") : TEXT("hand_r"));
		const int32 UpperIndex = Skeleton.FindBoneIndex(UpperName);
		const int32 LowerIndex = Skeleton.FindBoneIndex(LowerName);
		const int32 HandIndex = Skeleton.FindBoneIndex(HandName);
		if (UpperIndex == INDEX_NONE || LowerIndex == INDEX_NONE || HandIndex == INDEX_NONE)
		{
			return;
		}

		FTransform Upper = RotatedAround(
			ReferenceComponentTransform(Skeleton, UpperIndex), Pelvis, UpperBodyRotation);
		FTransform Lower = RotatedAround(
			ReferenceComponentTransform(Skeleton, LowerIndex), Pelvis, UpperBodyRotation);
		FTransform Hand = RotatedAround(
			ReferenceComponentTransform(Skeleton, HandIndex), Pelvis, UpperBodyRotation);
		const FVector Shoulder = Upper.GetLocation();
		const float UpperLength = FVector::Distance(Upper.GetLocation(), Lower.GetLocation());
		const float LowerLength = FVector::Distance(Lower.GetLocation(), Hand.GetLocation());
		const FVector TargetHand = DriverTransform.InverseTransformPosition(HandVehicle);
		FVector ToHand = TargetHand - Shoulder;
		float Distance = ToHand.Size();
		if (Distance <= UE_SMALL_NUMBER || UpperLength <= UE_SMALL_NUMBER || LowerLength <= UE_SMALL_NUMBER)
		{
			return;
		}
		const FVector Along = ToHand / Distance;
		Distance = FMath::Clamp(Distance, FMath::Abs(UpperLength - LowerLength) + 0.1f,
			UpperLength + LowerLength - 0.1f);
		const float AlongDistance = (UpperLength * UpperLength - LowerLength * LowerLength
			+ Distance * Distance) / (2.0f * Distance);
		const float BendDistance = FMath::Sqrt(FMath::Max(
			0.0f, UpperLength * UpperLength - AlongDistance * AlongDistance));
		// Use an authored elbow plane in vehicle space. A fixed model-space vector
		// made one elbow tuck through the torso after Manny was rotated into +X.
		FVector BendPreference = DriverTransform.InverseTransformPosition(
			ElbowHintVehicle) - Shoulder;
		BendPreference = (BendPreference - Along * FVector::DotProduct(BendPreference, Along))
			.GetSafeNormal(UE_SMALL_NUMBER,
				bLeft ? FVector(0.0, -1.0, -0.2).GetSafeNormal()
					: FVector(0.0, 1.0, -0.2).GetSafeNormal());
		const FVector Elbow = Shoulder + Along * AlongDistance + BendPreference * BendDistance;

		const FVector OldUpperDirection = (Lower.GetLocation() - Shoulder).GetSafeNormal();
		const FVector NewUpperDirection = (Elbow - Shoulder).GetSafeNormal();
		const FQuat UpperDelta = FQuat::FindBetweenNormals(OldUpperDirection, NewUpperDirection);
		Upper.SetRotation((UpperDelta * Upper.GetRotation()).GetNormalized());
		Lower.SetLocation(Elbow);
		const FVector OldLowerDirection = (Hand.GetLocation() - Lower.GetLocation()).GetSafeNormal();
		const FVector NewLowerDirection = (TargetHand - Elbow).GetSafeNormal();
		const FQuat LowerDelta = FQuat::FindBetweenNormals(OldLowerDirection, NewLowerDirection);
		Lower.SetRotation((LowerDelta * Lower.GetRotation()).GetNormalized());
		Hand.SetLocation(TargetHand);
		Hand.SetRotation((LowerDelta * Hand.GetRotation()).GetNormalized());

		Mesh->SetBoneTransformByName(UpperName, Upper, EBoneSpaces::ComponentSpace);
		Mesh->SetBoneTransformByName(LowerName, Lower, EBoneSpaces::ComponentSpace);
		Mesh->SetBoneTransformByName(HandName, Hand, EBoneSpaces::ComponentSpace);
	}

	void SolveLeg(
		UPoseableMeshComponent* Mesh,
		const FReferenceSkeleton& Skeleton,
		const FTransform& DriverTransform,
		const bool bLeft,
		const FVector& FootVehicle,
		const FVector& KneeHintVehicle,
		const FVector& FootNormalVehicle)
	{
		const FName ThighName(bLeft ? TEXT("thigh_l") : TEXT("thigh_r"));
		const FName CalfName(bLeft ? TEXT("calf_l") : TEXT("calf_r"));
		const FName FootName(bLeft ? TEXT("foot_l") : TEXT("foot_r"));
		const int32 ThighIndex = Skeleton.FindBoneIndex(ThighName);
		const int32 CalfIndex = Skeleton.FindBoneIndex(CalfName);
		const int32 FootIndex = Skeleton.FindBoneIndex(FootName);
		if (ThighIndex == INDEX_NONE || CalfIndex == INDEX_NONE || FootIndex == INDEX_NONE)
		{
			return;
		}

		FTransform Thigh = ReferenceComponentTransform(Skeleton, ThighIndex);
		FTransform Calf = ReferenceComponentTransform(Skeleton, CalfIndex);
		FTransform Foot = ReferenceComponentTransform(Skeleton, FootIndex);
		const FVector Hip = Thigh.GetLocation();
		const float ThighLength = FVector::Distance(Hip, Calf.GetLocation());
		const float CalfLength = FVector::Distance(Calf.GetLocation(), Foot.GetLocation());
		const FVector RequestedFoot = DriverTransform.InverseTransformPosition(FootVehicle);
		FVector ToFoot = RequestedFoot - Hip;
		float Distance = ToFoot.Size();
		if (Distance <= UE_SMALL_NUMBER || ThighLength <= UE_SMALL_NUMBER
			|| CalfLength <= UE_SMALL_NUMBER)
		{
			return;
		}
		const FVector Along = ToFoot / Distance;
		Distance = FMath::Clamp(Distance, FMath::Abs(ThighLength - CalfLength) + 0.1f,
			ThighLength + CalfLength - 0.1f);
		const FVector TargetFoot = Hip + Along * Distance;
		const float AlongDistance = (ThighLength * ThighLength - CalfLength * CalfLength
			+ Distance * Distance) / (2.0f * Distance);
		const float BendDistance = FMath::Sqrt(FMath::Max(
			0.0f, ThighLength * ThighLength - AlongDistance * AlongDistance));
		FVector Bend = DriverTransform.InverseTransformPosition(KneeHintVehicle) - Hip;
		Bend = Bend - Along * FVector::DotProduct(Bend, Along);
		Bend = Bend.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		const FVector Knee = Hip + Along * AlongDistance + Bend * BendDistance;

		const FVector OldThighDirection = (Calf.GetLocation() - Hip).GetSafeNormal();
		const FVector NewThighDirection = (Knee - Hip).GetSafeNormal();
		Thigh.SetRotation((FQuat::FindBetweenNormals(OldThighDirection,
			NewThighDirection) * Thigh.GetRotation()).GetNormalized());
		const FVector OldCalfDirection = (Foot.GetLocation() - Calf.GetLocation()).GetSafeNormal();
		Calf.SetLocation(Knee);
		const FVector NewCalfDirection = (TargetFoot - Knee).GetSafeNormal();
		const FQuat CalfDelta = FQuat::FindBetweenNormals(OldCalfDirection,
			NewCalfDirection);
		Calf.SetRotation((CalfDelta * Calf.GetRotation()).GetNormalized());
		Foot.SetLocation(TargetFoot);
		Foot.SetRotation((CalfDelta * Foot.GetRotation()).GetNormalized());
		const FVector TargetFootNormal = DriverTransform.InverseTransformVectorNoScale(
			FootNormalVehicle).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector ReferenceFootNormal = Foot.GetRotation().GetUpVector().GetSafeNormal();
		Foot.SetRotation((FQuat::FindBetweenNormals(ReferenceFootNormal,
			TargetFootNormal) * Foot.GetRotation()).GetNormalized());
		Mesh->SetBoneTransformByName(ThighName, Thigh, EBoneSpaces::ComponentSpace);
		Mesh->SetBoneTransformByName(CalfName, Calf, EBoneSpaces::ComponentSpace);
		Mesh->SetBoneTransformByName(FootName, Foot, EBoneSpaces::ComponentSpace);
	}

	float SmoothRange(const float Start, const float End, const float Value)
	{
		return FMath::SmoothStep(0.0f, 1.0f,
			FMath::Clamp((Value - Start) / FMath::Max(End - Start, UE_SMALL_NUMBER),
				0.0f, 1.0f));
	}

	FVector ExitPelvisPosition(const float ExitAlpha, const float StandingPelvisZCm)
	{
		// Two arcs keep the body inside the open doorway until the pelvis has
		// cleared the sill; a single seat-to-pavement lerp cuts through the body.
		const FVector SillPelvisCm(-22.0, -98.0, 60.0);
		const FVector StandingPelvisCm(-20.0, -145.0, StandingPelvisZCm);
		if (ExitAlpha < 0.56f)
		{
			return FMath::Lerp(DriverPelvisCm, SillPelvisCm,
				SmoothRange(0.0f, 0.56f, ExitAlpha));
		}
		return FMath::Lerp(SillPelvisCm, StandingPelvisCm,
			SmoothRange(0.56f, 1.0f, ExitAlpha));
	}

	FVector ExitFootPosition(const bool bLeft, const float ExitAlpha)
	{
		const FVector Seated = bLeft
			? FVector(18.0, -49.0, 5.0) : FVector(24.0, -22.0, 5.0);
		const FVector Outside = bLeft
			? FVector(-5.0, -116.0, -62.0) : FVector(-17.0, -133.0, -62.0);
		const FVector Standing = bLeft
			? FVector(-4.0, -158.0, -62.0) : FVector(-30.0, -133.0, -62.0);
		const float Start = bLeft ? 0.04f : 0.34f;
		const float OutsideAt = bLeft ? 0.54f : 0.82f;
		if (ExitAlpha <= Start)
		{
			return Seated;
		}
		if (ExitAlpha < OutsideAt)
		{
			const float Step = SmoothRange(Start, OutsideAt, ExitAlpha);
			FVector Result = FMath::Lerp(Seated, Outside, Step);
			Result.Z += FMath::Sin(Step * UE_PI) * 18.0f;
			return Result;
		}
		const float Plant = SmoothRange(OutsideAt, 1.0f, ExitAlpha);
		FVector Result = FMath::Lerp(Outside, Standing, Plant);
		Result.Z += FMath::Sin(Plant * UE_PI) * (bLeft ? 8.0f : 4.0f);
		return Result;
	}

	bool PlaceFootOnWorldStatic(USceneComponent* DriverComponent,
		const AActor* Owner, FVector& InOutFootVehicle,
		FVector& OutSurfaceVehicle, FVector& OutNormalVehicle)
	{
		OutSurfaceVehicle = InOutFootVehicle;
		OutNormalVehicle = FVector::UpVector;
		if (!DriverComponent || !DriverComponent->GetWorld())
		{
			return false;
		}
		const FTransform ComponentToWorld = DriverComponent->GetComponentTransform();
		const FVector Probe = ComponentToWorld.TransformPosition(InOutFootVehicle);
		const float ClearanceCm = FMath::Max(0.0f, InOutFootVehicle.Z + 62.0f);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(SimCoreDriverExitGround), false);
		if (Owner)
		{
			Query.AddIgnoredActor(Owner);
		}
		TArray<FHitResult> Hits;
		if (!DriverComponent->GetWorld()->LineTraceMultiByObjectType(
			Hits, Probe + FVector(0.0, 0.0, 150.0),
			Probe - FVector(0.0, 0.0, 250.0), Objects, Query))
		{
			return false;
		}
		for (const FHitResult& Hit : Hits)
		{
			const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
			if (!Hit.bBlockingHit || Hit.ImpactPoint.ContainsNaN()
				|| Normal.ContainsNaN() || Normal.Z < 0.35f)
			{
				continue;
			}
			OutSurfaceVehicle = ComponentToWorld.InverseTransformPosition(Hit.ImpactPoint);
			OutNormalVehicle = ComponentToWorld.InverseTransformVectorNoScale(Normal)
				.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
			InOutFootVehicle = ComponentToWorld.InverseTransformPosition(
				Hit.ImpactPoint + FVector::UpVector * ClearanceCm + Normal * 1.0f);
			return true;
		}
		return false;
	}
}

namespace SimCoreDriverPresentation
{
FTarget BuildTarget(const SimCoreProtocol::FVehicleState& State)
{
	FTarget Target;
	if (!IsFiniteState(State) || State.DamagePercent < 0.0f
		|| State.LastImpactImpulseNs < 0.0f)
	{
		return Target;
	}
	const float Damage = FMath::Clamp((State.DamagePercent - 40.0f) / 45.0f, 0.0f, 1.0f);
	const float Impact = State.CollisionEventSequence > 0
		? FMath::Clamp((State.LastImpactImpulseNs - 5000.0f) / 5000.0f, 0.0f, 1.0f)
		: 0.0f;
	Target.InjuryAlpha = FMath::Max(Damage, Impact);
	if (State.RuntimeRecoveryPhase == SimCoreProtocol::ERuntimeRecoveryPhase::Disabled)
	{
		const bool bSevereDamage = State.DamagePercent >= 65.0f
			|| State.LastImpactImpulseNs >= 7500.0f
			|| FMath::Abs(State.PitchDegrees) >= 45.0f
			|| FMath::Abs(State.RollDegrees) >= 45.0f;
		if (bSevereDamage)
		{
			Target.InjuryAlpha = FMath::Max(Target.InjuryAlpha, 0.75f);
		}
	}
	const float SideSign = State.DamageZone == SimCoreProtocol::EVehicleDamageZone::Left
		? -1.0f : State.DamageZone == SimCoreProtocol::EVehicleDamageZone::Right ? 1.0f : 0.0f;
	Target.LateralLeanDegrees = SideSign * 10.0f * Target.InjuryAlpha;
	const bool bModerateNpcCrash = State.EntityKind == SimCoreProtocol::EEntityKind::NpcVehicle
		&& State.CollisionEventSequence > 0
		&& State.LastImpactImpulseNs >= 1200.0f
		&& State.LastImpactImpulseNs < 5000.0f
		&& State.DamagePercent < 40.0f
		&& State.RuntimeRecoveryPhase == SimCoreProtocol::ERuntimeRecoveryPhase::Holding;
	Target.ProtestAlpha = bModerateNpcCrash ? 1.0f : 0.0f;
	return Target;
}

float AdvanceTowards(const float Current, const float Target,
	const float DeltaSeconds, const float ResponsePerSecond)
{
	if (!FMath::IsFinite(Current) || !FMath::IsFinite(Target)
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f
		|| !FMath::IsFinite(ResponsePerSecond) || ResponsePerSecond <= 0.0f)
	{
		return FMath::IsFinite(Current) ? Current : 0.0f;
	}
	const float Alpha = 1.0f - FMath::Exp(-DeltaSeconds * ResponsePerSecond);
	return FMath::Lerp(Current, Target, FMath::Clamp(Alpha, 0.0f, 1.0f));
}

FExitSequencePose EvaluateExitSequence(const float SequenceAlpha)
{
	FExitSequencePose Pose;
	if (!FMath::IsFinite(SequenceAlpha))
	{
		return Pose;
	}
	const float Alpha = FMath::Clamp(SequenceAlpha, 0.0f, 1.0f);
	if (Alpha < 0.08f)
	{
		Pose.DoorOpenAlpha = 0.0f;
	}
	else if (Alpha < 0.20f)
	{
		Pose.DoorOpenAlpha = SmoothRange(0.08f, 0.20f, Alpha);
	}
	else if (Alpha < 0.74f)
	{
		Pose.DoorOpenAlpha = 1.0f;
	}
	else
	{
		Pose.DoorOpenAlpha = 1.0f - SmoothRange(0.74f, 0.89f, Alpha);
	}
	Pose.DriverExitAlpha = SmoothRange(0.20f, 0.68f, Alpha);
	Pose.ProtestGestureAlpha = SmoothRange(0.94f, 1.0f, Alpha);
	return Pose;
}
}

USimCoreDriverPresentation::USimCoreDriverPresentation()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DoorPaint(
		TEXT("/Game/Vehicles/Sedan/Materials/M_Sedan_Paint.M_Sedan_Paint"));
	DriverSkeletalMesh = Manny.Object;
	InteriorBaseMaterial = Material.Object;
	DoorPaintMaterial = DoorPaint.Object;
	// The door package is optional until BuildSedanVisual has generated it.
	// Check existence before loading to avoid an error on the component CDO.
	if (FPackageName::DoesPackageExist(
		SimCoreSedanVisualContract::DriverDoorPackagePath()))
	{
		DriverDoorStaticMesh = LoadObject<UStaticMesh>(nullptr,
			SimCoreSedanVisualContract::DriverDoorObjectPath());
	}
}

void USimCoreDriverPresentation::OnRegister()
{
	Super::OnRegister();
	EnsureVisualComponents();
	ApplyPose();
}

void USimCoreDriverPresentation::ApplyAuthoritativeState(
	const SimCoreProtocol::FVehicleState& State)
{
	UpdateRiderState(State);
	const SimCoreDriverPresentation::FTarget Target =
		SimCoreDriverPresentation::BuildTarget(State);
	TargetInjuryAlpha = Target.InjuryAlpha;
	TargetLateralLeanDegrees = Target.LateralLeanDegrees;
	// Only the sedan has an authored opening door and a matching exit path.
	TargetProtestAlpha = PresentationVehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Sedan
		? Target.ProtestAlpha : 0.0f;
}

void USimCoreDriverPresentation::ApplyDoorDamage(
	const SimCoreDamagePresentation::FZoneWeights& Weights)
{
	if (!bPresentationEnabled
		|| PresentationVehicleClass != SimCoreProtocol::ERuntimeVehicleClass::Sedan)
	{
		// ApplyDamage restores its source mesh even for an empty/reset payload.
		// Never let that resurrect a sedan door on a bike or a merged fleet body.
		if (DriverDoorDeformableBody) DriverDoorDeformableBody->ResetDeformation();
		if (DriverDoorPanel) DriverDoorPanel->SetVisibility(false);
		return;
	}
	for (UMaterialInstanceDynamic* Material : DriverDoorDamageMaterials)
	{
		if (!Material)
		{
			continue;
		}
		for (uint8 Index = 1;
			Index <= static_cast<uint8>(SimCoreProtocol::EVehicleDamageZone::Underbody);
			++Index)
		{
			const auto Zone = static_cast<SimCoreProtocol::EVehicleDamageZone>(Index);
			// Match the body contract: contact-local patches use CPU vertices and
			// disable broad WPO; legacy zone damage uses the authored WPO scalar.
			Material->SetScalarParameterValue(
				SimCoreDamagePresentation::MaterialParameterName(Zone),
				Weights.bContactLocal ? 0.0f : Weights.Get(Zone));
		}
	}
	if (DriverDoorDeformableBody && DriverDoorPanel)
	{
		DriverDoorDeformableBody->ApplyDamage(DriverDoorPanel, Weights);
	}
}

void USimCoreDriverPresentation::SetPresentationEnabled(const bool bEnabled)
{
	if (bPresentationEnabled != bEnabled && DriverDoorDeformableBody)
	{
		DriverDoorDeformableBody->ResetDeformation();
	}
	bPresentationEnabled = bEnabled;
	UpdatePartVisibility();
	SetComponentTickEnabled(bEnabled);
}

void USimCoreDriverPresentation::ConfigureVehicleClass(
	SimCoreProtocol::ERuntimeVehicleClass VehicleClass)
{
	if (VehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Unspecified)
	{
		VehicleClass = SimCoreProtocol::ERuntimeVehicleClass::Sedan;
	}
	if (PresentationVehicleClass != VehicleClass)
	{
		ResetRider();
		bRiderStateInitialized = false;
		if (DriverDoorDeformableBody) DriverDoorDeformableBody->ResetDeformation();
		TargetInjuryAlpha = CurrentInjuryAlpha = 0.0f;
		TargetLateralLeanDegrees = CurrentLateralLeanDegrees = 0.0f;
		TargetProtestAlpha = CurrentProtestAlpha = 0.0f;
		ProtestAnimationTime = 0.0f;
	}
	PresentationVehicleClass = VehicleClass;
	SimCoreVehicleVisualProfile::FProfile Profile;
	SimCoreVehicleVisualProfile::Resolve(VehicleClass, Profile);
	SetRelativeTransform(Profile.DriverTransform);
	SetPresentationEnabled(true);
	ApplyPose();
}

void USimCoreDriverPresentation::UpdatePartVisibility()
{
	const bool bRider = PresentationVehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Motorcycle;
	const bool bTruck = PresentationVehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Truck;
	const bool bDoor = bPresentationEnabled
		&& PresentationVehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Sedan;
	SetVisibility(bPresentationEnabled);
	if (DriverMesh) DriverMesh->SetVisibility(bPresentationEnabled);
	if (SteeringWheel) SteeringWheel->SetVisibility(bPresentationEnabled && !bRider);
	for (UStaticMeshComponent* Panel : InteriorPanels)
	{
		if (!Panel || Panel == DriverDoorPanel) continue;
		Panel->SetVisibility(bPresentationEnabled && !bRider
			&& !(bTruck && Panel->GetName().StartsWith(TEXT("SimCoreRearSeat"))));
	}
	if (DriverDoorPivot) DriverDoorPivot->SetVisibility(bDoor);
	const bool bDeformed = DriverDoorDeformableBody
		&& DriverDoorDeformableBody->GetDeformedVertexCount() > 0;
	if (DriverDoorPanel) DriverDoorPanel->SetVisibility(bDoor && !bDeformed);
	if (DriverDoorDeformableBody) DriverDoorDeformableBody->SetVisibility(bDoor && bDeformed);
	UpdateDriverOwnerVisibility();
}

void USimCoreDriverPresentation::SetDriverViewActive(const bool bActive)
{
	bDriverViewActive = bActive;
	UpdateDriverOwnerVisibility();
}

void USimCoreDriverPresentation::UpdateDriverOwnerVisibility()
{
	if (DriverMesh)
	{
		// Hide only the seated driver's body from its owning first-person camera.
		// The cabin and an exited driver remain visible.
		DriverMesh->SetOwnerNoSee(bDriverViewActive && !bRiderEjected && CurrentDriverExitAlpha < 0.70f);
	}
}

bool USimCoreDriverPresentation::HasDriverAssets() const
{
	return DriverSkeletalMesh && DriverMesh && SteeringWheel
		&& DriverMesh->GetSkinnedAsset() == DriverSkeletalMesh;
}

void USimCoreDriverPresentation::TickComponent(const float DeltaTime,
	const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvancePresentation(DeltaTime);
}

void USimCoreDriverPresentation::AdvancePresentation(const float DeltaTime)
{
	if (!bPresentationEnabled || !HasDriverAssets())
	{
		return;
	}
	const float PreviousInjury = CurrentInjuryAlpha;
	const float PreviousLean = CurrentLateralLeanDegrees;
	const float PreviousProtest = CurrentProtestAlpha;
	CurrentInjuryAlpha = SimCoreDriverPresentation::AdvanceTowards(
		CurrentInjuryAlpha, TargetInjuryAlpha, DeltaTime, PoseResponsePerSecond);
	CurrentLateralLeanDegrees = SimCoreDriverPresentation::AdvanceTowards(
		CurrentLateralLeanDegrees, TargetLateralLeanDegrees, DeltaTime, PoseResponsePerSecond);
	if (FMath::IsFinite(DeltaTime) && DeltaTime > 0.0f)
	{
		CurrentProtestAlpha = FMath::FInterpConstantTo(CurrentProtestAlpha,
			TargetProtestAlpha, DeltaTime, ExitSequencePerSecond);
	}
	const SimCoreDriverPresentation::FExitSequencePose ExitPose =
		SimCoreDriverPresentation::EvaluateExitSequence(CurrentProtestAlpha);
	CurrentDoorOpenAlpha = ExitPose.DoorOpenAlpha;
	CurrentDriverExitAlpha = ExitPose.DriverExitAlpha;
	CurrentGestureAlpha = ExitPose.ProtestGestureAlpha;
	if (CurrentGestureAlpha > 0.02f)
	{
		ProtestAnimationTime += DeltaTime;
	}
	else
	{
		ProtestAnimationTime = 0.0f;
	}
	if (!FMath::IsNearlyEqual(PreviousInjury, CurrentInjuryAlpha, 0.0005f)
		|| !FMath::IsNearlyEqual(PreviousLean, CurrentLateralLeanDegrees, 0.01f)
		|| !FMath::IsNearlyEqual(PreviousProtest, CurrentProtestAlpha, 0.0005f)
		|| CurrentProtestAlpha > 0.02f)
	{
		ApplyPose();
	}
}

void USimCoreDriverPresentation::ConfigureInteriorMesh(UStaticMeshComponent* Mesh,
	const bool bApplyInteriorMaterial) const
{
	if (!Mesh)
	{
		return;
	}
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(true);
	if (bApplyInteriorMaterial && InteriorMaterial)
	{
		Mesh->SetMaterial(0, InteriorMaterial);
	}
}

void USimCoreDriverPresentation::EnsureVisualComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner || DriverMesh)
	{
		return;
	}
	InteriorMaterial = InteriorBaseMaterial
		? UMaterialInstanceDynamic::Create(InteriorBaseMaterial, this) : nullptr;
	DoorMaterial = InteriorBaseMaterial
		? UMaterialInstanceDynamic::Create(InteriorBaseMaterial, this) : nullptr;
	if (InteriorMaterial)
	{
		InteriorMaterial->SetVectorParameterValue(TEXT("Color"),
			FLinearColor(0.012f, 0.014f, 0.018f));
	}
	if (DoorMaterial)
	{
		DoorMaterial->SetVectorParameterValue(TEXT("Color"),
			FLinearColor(0.022f, 0.070f, 0.125f));
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Engine/BasicShapes/Cube.Cube"));

	DriverMesh = NewObject<UPoseableMeshComponent>(Owner, TEXT("SimCoreDriverMesh"));
	DriverMesh->SetupAttachment(this);
	DriverMesh->SetMobility(EComponentMobility::Movable);
	DriverMesh->SetSkinnedAssetAndUpdate(DriverSkeletalMesh, true);
	DriverMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DriverMesh->SetGenerateOverlapEvents(false);
	DriverMesh->SetCanEverAffectNavigation(false);
	DriverMesh->SetCastShadow(true);
	Owner->AddInstanceComponent(DriverMesh);
	DriverMesh->RegisterComponent();

	auto MakeBox = [this, Owner, Cube](const FName Name, const FVector& Location,
		const FVector& Scale) -> UStaticMeshComponent*
	{
		auto* Mesh = NewObject<UStaticMeshComponent>(Owner, Name);
		Mesh->SetupAttachment(this);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetRelativeLocation(Location);
		Mesh->SetRelativeScale3D(Scale);
		ConfigureInteriorMesh(Mesh);
		Owner->AddInstanceComponent(Mesh);
		Mesh->RegisterComponent();
		InteriorPanels.Add(Mesh);
		return Mesh;
	};
	SeatCushion = MakeBox(TEXT("SimCoreDriverSeatCushion"),
		FVector(-35.0, -34.0, 8.0), FVector(0.44, 0.42, 0.12));
	SeatBack = MakeBox(TEXT("SimCoreDriverSeatBack"),
		FVector(-50.0, -34.0, 34.0), FVector(0.11, 0.42, 0.52));
	Dashboard = MakeBox(TEXT("SimCoreDashboard"),
		FVector(35.0, 0.0, 35.0), FVector(0.22, 1.42, 0.10));
	MakeBox(TEXT("SimCorePassengerSeatCushion"),
		FVector(-35.0, 34.0, 8.0), FVector(0.44, 0.42, 0.12));
	MakeBox(TEXT("SimCorePassengerSeatBack"),
		FVector(-50.0, 34.0, 34.0), FVector(0.11, 0.42, 0.52));
	MakeBox(TEXT("SimCoreRearSeatCushion"),
		FVector(-105.0, 0.0, 10.0), FVector(0.43, 1.30, 0.12));
	MakeBox(TEXT("SimCoreRearSeatBack"),
		FVector(-126.0, 0.0, 37.0), FVector(0.10, 1.30, 0.50));
	MakeBox(TEXT("SimCoreCabinFloor"),
		FVector(-35.0, 0.0, 1.0), FVector(1.35, 1.45, 0.05));
	MakeBox(TEXT("SimCoreCenterConsole"),
		FVector(-16.0, 0.0, 17.0), FVector(0.72, 0.12, 0.15));
	DriverDoorPivot = NewObject<USceneComponent>(Owner, TEXT("SimCoreDriverDoorPivot"));
	DriverDoorPivot->SetupAttachment(this);
	DriverDoorPivot->SetMobility(EComponentMobility::Movable);
	DriverDoorPivot->SetRelativeLocation(DoorHingeCm);
	Owner->AddInstanceComponent(DriverDoorPivot);
	DriverDoorPivot->RegisterComponent();
	DriverDoorPanel = NewObject<UStaticMeshComponent>(Owner, TEXT("SimCoreLeftDoorPanel"));
	DriverDoorPanel->SetupAttachment(DriverDoorPivot);
	DriverDoorPanel->SetStaticMesh(DriverDoorStaticMesh ? DriverDoorStaticMesh.Get() : Cube);
	if (DriverDoorStaticMesh)
	{
		// The authored vertices use vehicle-local coordinates. This offset makes
		// the existing front-edge pivot the mesh origin for the hinge transform.
		DriverDoorPanel->SetRelativeLocation(-DoorHingeCm);
		DriverDoorPanel->SetRelativeScale3D(FVector::OneVector);
	}
	else
	{
		// Use a placeholder panel until SM_SedanDoorLeft has been generated.
		DriverDoorPanel->SetRelativeLocation(FVector(-54.5, -3.0, 6.0));
		DriverDoorPanel->SetRelativeScale3D(FVector(1.09, 0.035, 0.54));
	}
	ConfigureInteriorMesh(DriverDoorPanel, !DriverDoorStaticMesh);
	if (!DriverDoorStaticMesh && (DoorPaintMaterial || DoorMaterial))
	{
		DriverDoorPanel->SetMaterial(0,
			DoorPaintMaterial ? DoorPaintMaterial.Get() : DoorMaterial.Get());
	}
	Owner->AddInstanceComponent(DriverDoorPanel);
	DriverDoorPanel->RegisterComponent();
	if (DriverDoorStaticMesh)
	{
		DriverDoorPanel->SetEvaluateWorldPositionOffset(true);
		DriverDoorPanel->SetWorldPositionOffsetDisableDistance(0);
		DriverDoorDamageMaterials.Reset();
		for (int32 MaterialIndex = 0;
			MaterialIndex < DriverDoorPanel->GetNumMaterials(); ++MaterialIndex)
		{
			if (UMaterialInstanceDynamic* Material =
				DriverDoorPanel->CreateDynamicMaterialInstance(MaterialIndex))
			{
				DriverDoorDamageMaterials.Add(Material);
			}
		}
		DriverDoorDeformableBody = NewObject<USimCoreDeformableBody>(
			Owner, TEXT("SimCoreDriverDoorDeformableBody"));
		DriverDoorDeformableBody->SetupAttachment(DriverDoorPivot);
		Owner->AddInstanceComponent(DriverDoorDeformableBody);
		DriverDoorDeformableBody->RegisterComponent();
	}
	InteriorPanels.Add(DriverDoorPanel);
	// No fixed aperture mask: the authored body contains a real opening, while
	// the door asset itself carries a thin black interior trim surface.
	DriverDoorAperture = nullptr;
	MakeBox(TEXT("SimCoreRightDoorPanel"),
		FVector(-35.0, 76.0, 32.0), FVector(1.10, 0.05, 0.28));
	MakeBox(TEXT("SimCoreInstrumentBinnacle"),
		FVector(27.0, -34.0, 48.0), FVector(0.16, 0.34, 0.08));

	SteeringWheel = NewObject<UProceduralMeshComponent>(Owner, TEXT("SimCoreSteeringWheel"));
	SteeringWheel->SetupAttachment(this);
	SteeringWheel->SetMobility(EComponentMobility::Movable);
	SteeringWheel->SetRelativeLocation(SteeringCentreCm);
	SteeringWheel->SetRelativeRotation(FRotator(78.0, 0.0, 0.0));
	SteeringWheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SteeringWheel->SetGenerateOverlapEvents(false);
	SteeringWheel->SetCanEverAffectNavigation(false);
	SteeringWheel->SetCastShadow(true);
	Owner->AddInstanceComponent(SteeringWheel);
	SteeringWheel->RegisterComponent();
	BuildSteeringWheel();
	SetPresentationEnabled(bPresentationEnabled && DriverSkeletalMesh != nullptr);
}

void USimCoreDriverPresentation::BuildSteeringWheel()
{
	if (!SteeringWheel)
	{
		return;
	}
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> Uvs;
	AddTorus(Vertices, Triangles, Normals, Uvs);
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	SteeringWheel->CreateMeshSection_LinearColor(
		0, Vertices, Triangles, Normals, Uvs, Colors, Tangents, false);
	if (InteriorMaterial)
	{
		SteeringWheel->SetMaterial(0, InteriorMaterial);
	}
}

void USimCoreDriverPresentation::ApplyPose()
{
	if (bRiderEjected) return;
	if (!DriverMesh || !DriverSkeletalMesh)
	{
		return;
	}
	const FReferenceSkeleton& Skeleton = DriverSkeletalMesh->GetRefSkeleton();
	const bool bRider = PresentationVehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Motorcycle;
	const float PresentedDriverScale = bRider ? 0.95f : DriverScale;
	const int32 PelvisIndex = Skeleton.FindBoneIndex(TEXT("pelvis"));
	const int32 SpineIndex = Skeleton.FindBoneIndex(TEXT("spine_01"));
	const int32 NeckIndex = Skeleton.FindBoneIndex(TEXT("neck_01"));
	if (PelvisIndex == INDEX_NONE || SpineIndex == INDEX_NONE || NeckIndex == INDEX_NONE)
	{
		SetPresentationEnabled(false);
		return;
	}
	const FTransform ReferencePelvis = ReferenceComponentTransform(Skeleton, PelvisIndex);
	const SimCoreDriverPresentation::FExitSequencePose ExitPose =
		SimCoreDriverPresentation::EvaluateExitSequence(CurrentProtestAlpha);
	CurrentDoorOpenAlpha = ExitPose.DoorOpenAlpha;
	CurrentDriverExitAlpha = ExitPose.DriverExitAlpha;
	CurrentGestureAlpha = ExitPose.ProtestGestureAlpha;
	if (DriverDoorPivot)
	{
		DriverDoorPivot->SetRelativeRotation(FRotator(
			0.0f, DoorOpenDegrees * CurrentDoorOpenAlpha, 0.0f));
	}
	const FQuat SeatedFacing = FRotator(0.0, -90.0, 0.0).Quaternion();
	FVector StandingForwardWorld = GetComponentTransform().TransformVectorNoScale(
		FVector(0.0, 1.0, 0.0));
	StandingForwardWorld.Z = 0.0f;
	StandingForwardWorld = StandingForwardWorld.GetSafeNormal(
		UE_SMALL_NUMBER, FVector(0.0, 1.0, 0.0));
	const FQuat WorldStandingFacing = FRotationMatrix::MakeFromYZ(
		StandingForwardWorld, FVector::UpVector).ToQuat();
	const FQuat StandingFacing = (GetComponentQuat().Inverse()
		* WorldStandingFacing).GetNormalized();
	const float FacingAlpha = SmoothRange(0.12f, 0.82f, CurrentDriverExitAlpha);
	const FQuat Facing = FQuat::Slerp(SeatedFacing, StandingFacing,
		FacingAlpha).GetNormalized();
	FVector LeftFoot = bRider ? FVector(0.0, -38.0, 16.0) : ExitFootPosition(true, CurrentDriverExitAlpha);
	FVector RightFoot = bRider ? FVector(0.0, 38.0, 16.0) : ExitFootPosition(false, CurrentDriverExitAlpha);
	FVector LeftSurface = LeftFoot;
	FVector RightSurface = RightFoot;
	FVector LeftFootNormal = FVector::UpVector;
	FVector RightFootNormal = FVector::UpVector;
	const bool bLeftGround = CurrentDriverExitAlpha > 0.04f
		&& PlaceFootOnWorldStatic(this, GetOwner(), LeftFoot,
			LeftSurface, LeftFootNormal);
	const bool bRightGround = CurrentDriverExitAlpha > 0.34f
		&& PlaceFootOnWorldStatic(this, GetOwner(), RightFoot,
			RightSurface, RightFootNormal);
	float StandingGroundZCm = -62.0f;
	if (bLeftGround && bRightGround)
	{
		StandingGroundZCm = 0.5f * (LeftSurface.Z + RightSurface.Z);
	}
	else if (bLeftGround)
	{
		StandingGroundZCm = LeftSurface.Z;
	}
	else if (bRightGround)
	{
		StandingGroundZCm = RightSurface.Z;
	}
	const float StandingPelvisZCm = StandingGroundZCm + 67.0f;
	const FVector StandingPelvisCm(-20.0, -145.0, StandingPelvisZCm);
	FVector PresentedPelvisCm = bRider ? FVector(-12.0, 0.0, 75.0)
		: ExitPelvisPosition(CurrentDriverExitAlpha, StandingPelvisZCm);
	const float LeftStep = SmoothRange(0.04f, 0.54f, CurrentDriverExitAlpha);
	const float RightStep = SmoothRange(0.34f, 0.82f, CurrentDriverExitAlpha);
	PresentedPelvisCm.Z += 2.0f * (FMath::Sin(LeftStep * UE_PI)
		+ FMath::Sin(RightStep * UE_PI));
	const FVector Translation = PresentedPelvisCm
		- Facing.RotateVector(ReferencePelvis.GetLocation() * PresentedDriverScale);
	DriverModelTransform = FTransform(Facing, Translation, FVector(PresentedDriverScale));
	DriverMesh->SetRelativeTransform(DriverModelTransform);

	const FVector Pelvis = ReferencePelvis.GetLocation();
	const float PresentedInjuryAlpha = CurrentInjuryAlpha * (1.0f - CurrentDriverExitAlpha);
	const float ExitCrouch = FMath::Sin(CurrentDriverExitAlpha * UE_PI);
	const float StandingSway = CurrentGestureAlpha
		* (0.65f * FMath::Sin(ProtestAnimationTime * 1.35f)
			+ 0.20f * FMath::Sin(ProtestAnimationTime * 2.35f + 0.7f));
	const float ForwardLeanDegrees = (bRider ? -40.0f : FMath::Lerp(-7.0f, 0.0f, CurrentDriverExitAlpha))
		- PresentedInjuryAlpha * 35.0f - ExitCrouch * 9.0f;
	const FQuat ForwardLean(FVector::ForwardVector,
		FMath::DegreesToRadians(ForwardLeanDegrees));
	const FQuat SideLean(FVector::RightVector, FMath::DegreesToRadians(
		CurrentLateralLeanDegrees * (1.0f - CurrentDriverExitAlpha) + StandingSway * 1.8f));
	const FQuat UpperBodyRotation = (SideLean * ForwardLean).GetNormalized();
	FTransform Spine = RotatedAround(
		ReferenceComponentTransform(Skeleton, SpineIndex), Pelvis, UpperBodyRotation);
	DriverMesh->SetBoneTransformByName(TEXT("spine_01"), Spine, EBoneSpaces::ComponentSpace);

	FTransform Neck = RotatedAround(
		ReferenceComponentTransform(Skeleton, NeckIndex), Pelvis, UpperBodyRotation);
	Neck.AddToTranslation(FVector(
		0.0f, 8.0f * PresentedInjuryAlpha, -18.0f * PresentedInjuryAlpha));
	DriverMesh->SetBoneTransformByName(TEXT("neck_01"), Neck, EBoneSpaces::ComponentSpace);
	const float Gesture = FMath::Sin(ProtestAnimationTime * 2.35f);
	const float GestureLift = FMath::Sin(ProtestAnimationTime * 1.18f + 0.45f);
	// Wheel-local 9-and-3 grip targets follow the steering-column transform.
	const FTransform WheelToVehicle = SteeringWheel
		? SteeringWheel->GetRelativeTransform()
		: FTransform(FRotator(78.0, 0.0, 0.0), SteeringCentreCm);
	const FVector LeftWheelHand = bRider ? FVector(75.0, -40.0, 70.0)
		: WheelToVehicle.TransformPosition(FVector(0.0, -14.2, 0.0));
	const FVector RightWheelHand = bRider ? FVector(75.0, 40.0, 70.0)
		: WheelToVehicle.TransformPosition(FVector(0.0, 14.2, 0.0));
	FVector DoorHandleHand(1.0, -89.0, 24.0);
	if (DriverDoorPivot)
	{
		DoorHandleHand = DriverDoorPivot->GetRelativeTransform().TransformPosition(
			FVector(-62.0, -1.0, -1.0));
	}
	const float OpeningReachAlpha = SmoothRange(0.0f, 0.08f, CurrentProtestAlpha)
		* (1.0f - SmoothRange(0.20f, 0.28f, CurrentProtestAlpha));
	const float ClosingReachAlpha = SmoothRange(0.68f, 0.74f, CurrentProtestAlpha)
		* (1.0f - SmoothRange(0.89f, 0.94f, CurrentProtestAlpha));
	const FVector LeftSeatedHand = FMath::Lerp(
		LeftWheelHand, DoorHandleHand, OpeningReachAlpha);
	const FVector LeftStandingHand = FMath::Lerp(
		StandingPelvisCm + FVector(18.0, 0.0, 31.0),
		DoorHandleHand, ClosingReachAlpha);
	const FVector RightStandingHand = StandingPelvisCm + FVector(-18.0, 0.0, 34.0);
	const FVector RightWaveHand = StandingPelvisCm
		+ FVector(-27.0 - 5.0 * Gesture, 12.0, 64.0 + 3.0 * GestureLift);
	const FVector LeftHand = FMath::Lerp(
		LeftSeatedHand, LeftStandingHand, CurrentDriverExitAlpha);
	const FVector RightExitHand = FMath::Lerp(
		RightWheelHand, RightStandingHand, CurrentDriverExitAlpha);
	const FVector RightHand = FMath::Lerp(
		RightExitHand, RightWaveHand, CurrentGestureAlpha);
	const float LeftReach = FMath::Max(OpeningReachAlpha,
		FMath::Max(ClosingReachAlpha, CurrentDriverExitAlpha));
	const FVector LeftElbowHint = bRider ? FVector(38.0, -54.0, 75.0) : FMath::Lerp(
		FVector(-10.0, -55.0, 45.0),
		LeftHand + FVector(-16.0, -7.0, -10.0), LeftReach);
	const FVector RightElbowHint = bRider ? FVector(38.0, 54.0, 75.0) : FMath::Lerp(
		FVector(-10.0, -13.0, 45.0),
		RightHand + FVector(-16.0, 7.0, -10.0), CurrentDriverExitAlpha);
	SolveArm(DriverMesh, Skeleton, UpperBodyRotation, Pelvis,
		DriverModelTransform, true, LeftHand, LeftElbowHint);
	SolveArm(DriverMesh, Skeleton, UpperBodyRotation, Pelvis,
		DriverModelTransform, false, RightHand, RightElbowHint);
	SolveLeg(DriverMesh, Skeleton, DriverModelTransform, true, LeftFoot,
		bRider ? FVector(32.0, -36.0, 38.0) : FMath::Lerp(FVector(8.0, -47.0, 37.0),
			FVector(10.0, -158.0, LeftFoot.Z + 38.0f),
			CurrentDriverExitAlpha), LeftFootNormal);
	SolveLeg(DriverMesh, Skeleton, DriverModelTransform, false, RightFoot,
		bRider ? FVector(32.0, 36.0, 38.0) : FMath::Lerp(FVector(18.0, -22.0, 37.0),
			FVector(-17.0, -133.0, RightFoot.Z + 38.0f),
			CurrentDriverExitAlpha), RightFootNormal);
	if (CurrentGestureAlpha > 0.01f)
	{
		FTransform Hand = DriverMesh->GetBoneTransformByName(
			TEXT("hand_r"), EBoneSpaces::ComponentSpace);
		const FQuat WristWave(FVector::ForwardVector,
			FMath::DegreesToRadians(18.0f * Gesture * CurrentGestureAlpha));
		Hand.SetRotation((WristWave * Hand.GetRotation()).GetNormalized());
		DriverMesh->SetBoneTransformByName(
			TEXT("hand_r"), Hand, EBoneSpaces::ComponentSpace);
	}
	DriverMesh->RefreshBoneTransforms();
	UpdateDriverOwnerVisibility();
}
