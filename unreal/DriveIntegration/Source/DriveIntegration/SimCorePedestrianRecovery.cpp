#include "SimCorePedestrianPresentationActor.h"

#include "Animation/AnimSequence.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

namespace
{
float Stage(const float Begin, const float End, const float Value)
{
	return FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp((Value - Begin) / (End - Begin), 0.0f, 1.0f));
}

void PlantFoot(UPoseableMeshComponent* Mesh, const bool bLeft, const FVector& Target)
{
	const FName ThighName(bLeft ? TEXT("thigh_l") : TEXT("thigh_r"));
	const FName CalfName(bLeft ? TEXT("calf_l") : TEXT("calf_r"));
	const FName FootName(bLeft ? TEXT("foot_l") : TEXT("foot_r"));
	FTransform Thigh = Mesh->GetBoneTransformByName(ThighName, EBoneSpaces::ComponentSpace);
	FTransform Calf = Mesh->GetBoneTransformByName(CalfName, EBoneSpaces::ComponentSpace);
	FTransform Foot = Mesh->GetBoneTransformByName(FootName, EBoneSpaces::ComponentSpace);
	const FVector Hip = Thigh.GetLocation();
	const double UpperLength = FVector::Distance(Hip, Calf.GetLocation());
	const double LowerLength = FVector::Distance(Calf.GetLocation(), Foot.GetLocation());
	if (UpperLength < 1.0 || LowerLength < 1.0) return;
	const FVector Along = (Target - Hip).GetSafeNormal();
	const double Distance = FMath::Clamp(FVector::Distance(Hip, Target),
		FMath::Abs(UpperLength - LowerLength) + 0.1, UpperLength + LowerLength - 0.1);
	const double AlongLength = (UpperLength * UpperLength - LowerLength * LowerLength
		+ Distance * Distance) / (2.0 * Distance);
	const double BendLength = FMath::Sqrt(FMath::Max(0.0,
		UpperLength * UpperLength - AlongLength * AlongLength));
	const FVector Bend = (FVector::RightVector - Along * FVector::DotProduct(
		FVector::RightVector, Along)).GetSafeNormal(); // Authored mannequin faces +Y.
	const FVector Knee = Hip + Along * AlongLength + Bend * BendLength;
	const FVector OldUpper = (Calf.GetLocation() - Hip).GetSafeNormal();
	const FVector OldLower = (Foot.GetLocation() - Calf.GetLocation()).GetSafeNormal();
	Thigh.SetRotation((FQuat::FindBetweenNormals(OldUpper, (Knee - Hip).GetSafeNormal())
		* Thigh.GetRotation()).GetNormalized());
	Calf.SetLocation(Knee);
	Calf.SetRotation((FQuat::FindBetweenNormals(OldLower, (Target - Knee).GetSafeNormal())
		* Calf.GetRotation()).GetNormalized());
	Foot.SetLocation(Target);
	Mesh->SetBoneTransformByName(ThighName, Thigh, EBoneSpaces::ComponentSpace);
	Mesh->SetBoneTransformByName(CalfName, Calf, EBoneSpaces::ComponentSpace);
	Mesh->SetBoneTransformByName(FootName, Foot, EBoneSpaces::ComponentSpace);
}
}

void ASimCorePedestrianPresentationActor::BeginGetUpPose()
{
	USkeletalMesh* MeshAsset = CharacterMesh->GetSkeletalMeshAsset();
	if (!MeshAsset) return;
	const FReferenceSkeleton& Skeleton = MeshAsset->GetRefSkeleton();
	TArray<FTransform> FallenWorld;
	for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
		FallenWorld.Add(CharacterMesh->GetSocketTransform(Skeleton.GetBoneName(Index), RTS_World));
	CharacterMesh->SetAllBodiesSimulatePhysics(false);
	CharacterMesh->SetAllBodiesPhysicsBlendWeight(0.0f);
	CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CharacterMesh->SetRelativeTransform(StandingModelTransform);
	CharacterMesh->PlayAnimation(IdleAnimation, true);
	CharacterMesh->SetPosition(0.0f, false);
	CharacterMesh->SetPlayRate(0.0f);
	CharacterMesh->RefreshBoneTransforms();
	RecoveryAnchor = CharacterMesh->GetComponentTransform();
	RecoveryStartBones.Reset();
	RecoveryStandingBones.Reset();
	for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
	{
		RecoveryStartBones.Add(FallenWorld[Index].GetRelativeTransform(RecoveryAnchor));
		RecoveryStandingBones.Add(CharacterMesh->GetSocketTransform(Skeleton.GetBoneName(Index), RTS_Component));
	}
	if (!RecoveryMesh)
	{
		RecoveryMesh = NewObject<UPoseableMeshComponent>(this, TEXT("GetUpPose"));
		RecoveryMesh->SetupAttachment(PedestrianRoot);
		RecoveryMesh->SetMobility(EComponentMobility::Movable);
		RecoveryMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RecoveryMesh->SetCanEverAffectNavigation(false);
		AddInstanceComponent(RecoveryMesh);
		RecoveryMesh->RegisterComponent();
	}
	RecoveryMesh->SetSkinnedAssetAndUpdate(MeshAsset, true);
	RecoveryMesh->SetAbsolute(true, true, true);
	RecoveryMesh->SetWorldTransform(RecoveryAnchor);
	for (int32 Slot = 0; Slot < CharacterMesh->GetNumMaterials(); ++Slot)
		RecoveryMesh->SetMaterial(Slot, CharacterMesh->GetMaterial(Slot));
	RecoveryMesh->SetVisibility(true);
	CharacterMesh->SetVisibility(false);
	UpdateGetUpPose(0.0f);
}

void ASimCorePedestrianPresentationActor::UpdateGetUpPose(const float Alpha)
{
	if (!RecoveryMesh || !RecoveryMesh->GetSkinnedAsset()
		|| RecoveryStartBones.Num() != RecoveryStandingBones.Num()) return;
	const FReferenceSkeleton& Skeleton = RecoveryMesh->GetSkinnedAsset()->GetRefSkeleton();
	const int32 PelvisIndex = Skeleton.FindBoneIndex(TEXT("pelvis"));
	const int32 SpineIndex = Skeleton.FindBoneIndex(TEXT("spine_01"));
	if (!RecoveryStandingBones.IsValidIndex(PelvisIndex)) return;
	const FVector StandingPelvis = RecoveryStandingBones[PelvisIndex].GetLocation();
	const FVector CrouchDrop(0.0, 12.0, -43.0);
	const FQuat ForwardBend(FVector::ForwardVector, FMath::DegreesToRadians(-27.0));
	for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
	{
		const FTransform& Standing = RecoveryStandingBones[Index];
		FTransform Crouched = Standing;
		if (Index == PelvisIndex || Skeleton.BoneIsChildOf(Index, PelvisIndex))
		{
			Crouched.AddToTranslation(CrouchDrop);
			const FVector Delta = Standing.GetLocation() - StandingPelvis;
			if (SpineIndex != INDEX_NONE
				&& (Index == SpineIndex || Skeleton.BoneIsChildOf(Index, SpineIndex)))
			{
				Crouched.SetLocation(StandingPelvis + CrouchDrop + ForwardBend.RotateVector(Delta));
				Crouched.SetRotation((ForwardBend * Standing.GetRotation()).GetNormalized());
			}
		}
		FTransform Pose;
		if (Alpha < 0.52f) Pose.Blend(RecoveryStartBones[Index], Crouched, Stage(0.0f, 0.52f, Alpha));
		else Pose.Blend(Crouched, Standing, Stage(0.52f, 1.0f, Alpha));
		RecoveryMesh->SetBoneTransformByName(Skeleton.GetBoneName(Index), Pose, EBoneSpaces::ComponentSpace);
	}
	// Feet come under the pelvis during kneeling, then remain planted while the
	// knees extend. This is a reduced get-up pose, not an authored animation clip.
	if (Alpha > 0.35f)
	{
		for (const bool bLeft : {true, false})
		{
			const FName FootName(bLeft ? TEXT("foot_l") : TEXT("foot_r"));
			const int32 Index = Skeleton.FindBoneIndex(FootName);
			if (!RecoveryStandingBones.IsValidIndex(Index)) continue;
			const FVector Current = RecoveryMesh->GetBoneTransformByName(FootName, EBoneSpaces::ComponentSpace).GetLocation();
			PlantFoot(RecoveryMesh, bLeft, FMath::Lerp(Current,
				RecoveryStandingBones[Index].GetLocation(), Stage(0.35f, 0.55f, Alpha)));
		}
	}
	RecoveryMesh->RefreshBoneTransforms();
}

void ASimCorePedestrianPresentationActor::EndGetUpPose()
{
	if (RecoveryMesh) RecoveryMesh->SetVisibility(false);
	if (CharacterMesh) CharacterMesh->SetVisibility(true);
	RecoveryStartBones.Reset();
	RecoveryStandingBones.Reset();
}
