#include "SimCoreDriverPresentation.h"

#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "ReferenceSkeleton.h"
#include "SimCoreCoordinateFrames.h"

bool SimCoreDriverPresentation::ShouldEjectRider(
	const SimCoreProtocol::FVehicleState& State, const float PreviousSpeedMps)
{
	if (State.RuntimeVehicleClass != SimCoreProtocol::ERuntimeVehicleClass::Motorcycle
		|| State.CollisionEventSequence == 0 || !FMath::IsFinite(State.LastImpactImpulseNs)
		|| !FMath::IsFinite(PreviousSpeedMps) || !FMath::IsFinite(State.SpeedMps)
		|| !FMath::IsFinite(State.RollDegrees) || !FMath::IsFinite(State.PitchDegrees)) return false;
	const float Speed = FMath::Max(FMath::Abs(State.SpeedMps), PreviousSpeedMps);
	const bool bHardImpact = State.LastImpactImpulseNs >= RiderEjectionImpulseNs && Speed >= 2.5f;
	const bool bOverturned = State.LastImpactImpulseNs >= 250.0f
		&& (FMath::Abs(State.RollDegrees) >= 65.0f || FMath::Abs(State.PitchDegrees) >= 65.0f);
	return bHardImpact || bOverturned;
}

void USimCoreDriverPresentation::ResetRider()
{
	bRiderEjected = false;
	bRiderGrounded = false;
	RiderFlightSeconds = 0.0f;
	RiderVelocityCmPerSecond = FVector::ZeroVector;
	PreviousVehicleVelocityCmPerSecond = FVector::ZeroVector;
	if (DriverMesh)
	{
		DriverMesh->SetAbsolute(false, false, false);
		if (DriverSkeletalMesh)
		{
			const FReferenceSkeleton& Skeleton = DriverSkeletalMesh->GetRefSkeleton();
			for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
				DriverMesh->ResetBoneTransformByName(Skeleton.GetBoneName(Index));
		}
		ApplyPose();
	}
}

void USimCoreDriverPresentation::UpdateRiderState(const SimCoreProtocol::FVehicleState& State)
{
	if (PresentationVehicleClass != SimCoreProtocol::ERuntimeVehicleClass::Motorcycle
		|| !DriverMesh || !DriverSkeletalMesh || State.LinearVelocityEnu.ContainsNaN()) return;
	const bool bNewSession = !bRiderStateInitialized || RiderPlaySessionId != State.PlaySessionId
		|| RiderMapChecksum != State.MapPackageChecksum || State.SimulationTimeNs < RiderSimulationTimeNs
		|| State.CollisionEventSequence < RiderCollisionSequence;
	if (bNewSession)
	{
		ResetRider();
		RiderCollisionSequence = 0;
		RiderSimulationTimeNs = State.SimulationTimeNs;
		RiderPlaySessionId = State.PlaySessionId;
		RiderMapChecksum = State.MapPackageChecksum;
		bRiderStateInitialized = true;
	}
	const double Elapsed = (State.SimulationTimeNs - RiderSimulationTimeNs) * 1.e-9;
	const bool bSimulationActive = !State.ServerHealth.bPresent
		|| State.ServerHealth.Status == SimCoreProtocol::EServerHealthStatus::Active;
	if (bRiderEjected && bSimulationActive)
	{
		// The host clock may keep advancing during SafeStop. Gate on accepted
		// health as well as simulation time; never catch up that paused interval.
		AdvanceEjectedRider(static_cast<float>(FMath::Min(Elapsed, 0.25)));
	}
	else if (!bRiderEjected && bSimulationActive
		&& (State.CollisionEventSequence > RiderCollisionSequence
		|| FMath::Abs(State.RollDegrees) >= 65.0f || FMath::Abs(State.PitchDegrees) >= 65.0f)
		&& SimCoreDriverPresentation::ShouldEjectRider(State,
			static_cast<float>(PreviousVehicleVelocityCmPerSecond.Size() * 0.01)))
	{
		EjectRider(State);
	}
	RiderCollisionSequence = State.CollisionEventSequence;
	RiderSimulationTimeNs = State.SimulationTimeNs;
	PreviousVehicleVelocityCmPerSecond =
		SimCoreCoordinateFrames::MapEnuVelocityMetersPerSecondToUnrealCentimetersPerSecond(
			State.LinearVelocityEnu);
}

void USimCoreDriverPresentation::EjectRider(const SimCoreProtocol::FVehicleState& State)
{
	bRiderEjected = true;
	bRiderGrounded = false;
	RiderFlightSeconds = 0.0f;
	RiderPelvisWorldCm = DriverMesh->GetBoneLocation(TEXT("pelvis"));
	RiderInitialRotation = DriverMesh->GetComponentQuat();
	RiderScale = DriverMesh->GetComponentScale();
	const FVector CurrentVelocity =
		SimCoreCoordinateFrames::MapEnuVelocityMetersPerSecondToUnrealCentimetersPerSecond(
			State.LinearVelocityEnu);
	// Inherit pre-impact travel when the bike has already lost speed in the
	// authoritative collision solve. Do not launch opposite to that momentum.
	RiderVelocityCmPerSecond = PreviousVehicleVelocityCmPerSecond.SizeSquared()
		> CurrentVelocity.SizeSquared() ? PreviousVehicleVelocityCmPerSecond : CurrentVelocity;
	RiderVelocityCmPerSecond = RiderVelocityCmPerSecond.GetClampedToMaxSize(5500.0);
	const FVector Side = GetComponentTransform().GetUnitAxis(EAxis::Y);
	const double SideSign = State.DamageZone == SimCoreProtocol::EVehicleDamageZone::Left
		? 1.0 : State.DamageZone == SimCoreProtocol::EVehicleDamageZone::Right ? -1.0 : 0.0;
	RiderVelocityCmPerSecond += Side * (SideSign * 90.0);
	RiderVelocityCmPerSecond.Z = FMath::Max(RiderVelocityCmPerSecond.Z, 80.0)
		+ FMath::Clamp(static_cast<double>(State.LastImpactImpulseNs) * 0.035, 0.0, 170.0);
	DriverMesh->SetAbsolute(true, true, true);
	ApplyEjectedRiderPose();
	UpdateDriverOwnerVisibility();
}

void USimCoreDriverPresentation::ApplyEjectedRiderPose()
{
	if (!bRiderEjected || !DriverMesh || !DriverSkeletalMesh) return;
	const FReferenceSkeleton& Skeleton = DriverSkeletalMesh->GetRefSkeleton();
	for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
		DriverMesh->ResetBoneTransformByName(Skeleton.GetBoneName(Index));
	// Release the handlebars and pegs. A loose asymmetric pose rotates around
	// the pelvis; the vehicle attachment no longer contributes a transform.
	for (const bool bLeft : {true, false})
	{
		const FName UpperArm(bLeft ? TEXT("upperarm_l") : TEXT("upperarm_r"));
		const FName Thigh(bLeft ? TEXT("thigh_l") : TEXT("thigh_r"));
		FTransform Arm = DriverMesh->GetBoneTransformByName(UpperArm, EBoneSpaces::ComponentSpace);
		Arm.SetRotation((FQuat(FVector::ForwardVector,
			FMath::DegreesToRadians(bLeft ? 25.0 : -40.0)) * Arm.GetRotation()).GetNormalized());
		DriverMesh->SetBoneTransformByName(UpperArm, Arm, EBoneSpaces::ComponentSpace);
		FTransform Leg = DriverMesh->GetBoneTransformByName(Thigh, EBoneSpaces::ComponentSpace);
		Leg.SetRotation((FQuat(FVector::RightVector,
			FMath::DegreesToRadians(bLeft ? -14.0 : 22.0)) * Leg.GetRotation()).GetNormalized());
		DriverMesh->SetBoneTransformByName(Thigh, Leg, EBoneSpaces::ComponentSpace);
	}
	const double Pitch = bRiderGrounded ? 90.0
		: FMath::Min(static_cast<double>(RiderFlightSeconds) * 160.0, 90.0);
	const FQuat Rotation = (RiderInitialRotation * FQuat(FVector::ForwardVector,
		FMath::DegreesToRadians(Pitch))).GetNormalized();
	const FVector PelvisModel = DriverMesh->GetBoneTransformByName(
		TEXT("pelvis"), EBoneSpaces::ComponentSpace).GetLocation();
	DriverMesh->SetWorldTransform(FTransform(Rotation,
		RiderPelvisWorldCm - Rotation.RotateVector(PelvisModel * RiderScale), RiderScale));
	DriverMesh->RefreshBoneTransforms();
}

void USimCoreDriverPresentation::AdvanceEjectedRider(const float DeltaSeconds)
{
	if (!bRiderEjected || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f || !GetWorld()) return;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCoreRiderGround), false, GetOwner());
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	float Remaining = FMath::Min(DeltaSeconds, 0.25f);
	while (Remaining > UE_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Remaining, 1.0f / 60.0f);
		Remaining -= Step;
		RiderFlightSeconds += Step;
		bRiderGrounded = false;
		const FVector PreviousPelvis = RiderPelvisWorldCm;
		RiderVelocityCmPerSecond.Z -= 981.0 * Step;
		FVector NextPelvis = PreviousPelvis + RiderVelocityCmPerSecond * Step;
		FHitResult Hit;
		if (GetWorld()->SweepSingleByObjectType(Hit, PreviousPelvis, NextPelvis,
			FQuat::Identity, Objects, FCollisionShape::MakeSphere(18.0f), Params))
		{
			NextPelvis = Hit.Location + Hit.Normal * 1.0;
			const double IntoSurface = FVector::DotProduct(RiderVelocityCmPerSecond, Hit.Normal);
			if (IntoSurface < 0.0) RiderVelocityCmPerSecond -= Hit.Normal * IntoSurface;
			RiderVelocityCmPerSecond *= 0.75;
		}
		RiderPelvisWorldCm = NextPelvis;
		ApplyEjectedRiderPose();
		FHitResult Ground;
		if (GetWorld()->LineTraceSingleByObjectType(Ground, RiderPelvisWorldCm,
			RiderPelvisWorldCm - FVector(0, 0, 240), Objects, Params) && Ground.Normal.Z > 0.4
			&& Ground.ImpactPoint.Z <= FMath::Max(PreviousPelvis.Z, RiderPelvisWorldCm.Z) - 17.0)
		{
			double Bottom = RiderPelvisWorldCm.Z - 18.0;
			for (const FName Bone : {FName(TEXT("head")), FName(TEXT("hand_l")), FName(TEXT("hand_r")),
				FName(TEXT("foot_l")), FName(TEXT("foot_r")), FName(TEXT("spine_03"))})
				Bottom = FMath::Min(Bottom, DriverMesh->GetBoneLocation(Bone).Z - 10.0);
			if (Bottom <= Ground.ImpactPoint.Z + 2.0)
			{
				bRiderGrounded = true;
				RiderFlightSeconds = FMath::Max(RiderFlightSeconds, 90.0f / 160.0f);
				ApplyEjectedRiderPose();
				Bottom = RiderPelvisWorldCm.Z - 18.0;
				for (const FName Bone : {FName(TEXT("head")), FName(TEXT("hand_l")), FName(TEXT("hand_r")),
					FName(TEXT("foot_l")), FName(TEXT("foot_r")), FName(TEXT("spine_03"))})
					Bottom = FMath::Min(Bottom, DriverMesh->GetBoneLocation(Bone).Z - 10.0);
				RiderPelvisWorldCm.Z += Ground.ImpactPoint.Z + 2.0 - Bottom;
				RiderVelocityCmPerSecond.Z = 0.0;
				const FVector Horizontal(RiderVelocityCmPerSecond.X, RiderVelocityCmPerSecond.Y, 0.0);
				RiderVelocityCmPerSecond = Horizontal.GetSafeNormal()
					* FMath::Max(0.0, Horizontal.Size() - 0.7 * 981.0 * Step);
				ApplyEjectedRiderPose();
			}
		}
	}
}
