#include "SimCoreTurnSignals.h"
#include "SimCoreDeformableBody.h"
#include "SimCoreSedanVisualContract.h"

#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

USimCoreTurnSignals::USimCoreTurnSignals()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

void USimCoreTurnSignals::BindBodies(UMeshComponent* Source, UMeshComponent* Deformed)
{
	SourceBody = Source;
	DeformedBody = Deformed;
}

bool USimCoreTurnSignals::SetLampPositions(const TConstArrayView<FVector> PositionsCm)
{
	if (PositionsCm.Num() != 4)
	{
		return false;
	}
	for (const FVector& Position : PositionsCm)
	{
		if (Position.ContainsNaN())
		{
			return false;
		}
	}
	RestLampPositions.Reset(4);
	RestLampPositions.Append(PositionsCm.GetData(), PositionsCm.Num());
	for (int32 Index = 0; Index < Lights.Num() && Index < RestLampPositions.Num(); ++Index)
	{
		Lights[Index]->SetRelativeLocation(RestLampPositions[Index] + FVector(0.0, 0.0, 2.0));
	}
	return true;
}

bool USimCoreTurnSignals::IsLit(SimCoreProtocol::ETurnIndicator Direction,
	bool bLeft, double TimeSeconds, bool bHazard)
{
	if (!FMath::IsFinite(TimeSeconds) || TimeSeconds < 0.0) return false;
	const bool bSelected = bHazard || (bLeft ? Direction == SimCoreProtocol::ETurnIndicator::Left
		: Direction == SimCoreProtocol::ETurnIndicator::Right);
	return bSelected && FMath::Fmod(TimeSeconds, 0.72) < 0.44;
}

void USimCoreTurnSignals::OnRegister()
{
	Super::OnRegister();
	if (!Lights.IsEmpty() || !GetOwner()) return;
	if (RestLampPositions.Num() != 4)
	{
		RestLampPositions.Reset(4);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			RestLampPositions.Add(SimCoreSedanVisualContract::TurnSignalLensPointCm(
				Index < 2, Index % 2 == 0, 0.5, 0.5));
		}
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		auto* Light = NewObject<UPointLightComponent>(GetOwner());
		Light->SetupAttachment(this);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetLightColor(FLinearColor(1.0f, 0.42f, 0.015f));
		Light->SetIntensityUnits(ELightUnits::Candelas);
		// The authored emissive lens identifies the exact lamp while this small,
		// shadowless local light makes each flash readable in daylight and from a
		// chase camera. It remains tightly bounded to the vehicle vicinity.
		Light->SetIntensity(3600.0f);
		Light->SetAttenuationRadius(450.0f);
		Light->SetSourceRadius(7.0f);
		Light->SetSoftSourceRadius(18.0f);
		Light->SetCastShadows(false);
		Light->SetVolumetricScatteringIntensity(0.25f);
		Light->MaxDrawDistance = 6000.0f;
		Light->SetCanEverAffectNavigation(false);
		Light->SetRelativeLocation(RestLampPositions[Index] + FVector(0,0,2));
		Light->SetVisibility(false);
		GetOwner()->AddInstanceComponent(Light);
		Light->RegisterComponent();
		Lights.Add(Light);
	}
}

void USimCoreTurnSignals::UpdateSignal(SimCoreProtocol::ETurnIndicator Direction,
	double TimeSeconds, bool bHazard)
{
	const bool bLeft = IsLit(Direction, true, TimeSeconds, bHazard);
	const bool bRight = IsLit(Direction, false, TimeSeconds, bHazard);
	bool bHasAuthoredLens = false;
	// DeformableBody owns separate MIDs to avoid double WPO. Update its current
	// materials as well as the source's; never replace the damage owner's MID.
	for (UMeshComponent* Body : {SourceBody.Get(), DeformedBody.Get()})
	{
		if (!Body) continue;
		for (int32 Index = 0; Index < Body->GetNumMaterials(); ++Index)
		{
			UMaterialInterface* Material = Body->GetMaterial(Index);
			if (!Material || !Material->GetMaterial()
				|| Material->GetMaterial()->GetFName() != SimCoreSedanVisualContract::SignalMaterialName) continue;
			float LeftDefault, RightDefault;
			if (!Material->GetScalarParameterValue(FMaterialParameterInfo(SimCoreSedanVisualContract::SignalLeftParameter), LeftDefault)
				|| !Material->GetScalarParameterValue(FMaterialParameterInfo(SimCoreSedanVisualContract::SignalRightParameter), RightDefault)) continue;
			auto* Dynamic = Cast<UMaterialInstanceDynamic>(Material);
			if (!Dynamic) Dynamic = Body->CreateDynamicMaterialInstance(Index);
			if (!Dynamic) continue;
			const float LeftOn = bLeft ? 1.0f : 0.0f;
			const float RightOn = bRight ? 1.0f : 0.0f;
			if (LeftDefault != LeftOn) Dynamic->SetScalarParameterValue(SimCoreSedanVisualContract::SignalLeftParameter, LeftOn);
			if (RightDefault != RightOn) Dynamic->SetScalarParameterValue(SimCoreSedanVisualContract::SignalRightParameter, RightOn);
			bHasAuthoredLens = true;
		}
	}
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
		Lights[Index]->SetVisibility(bHasAuthoredLens && (Index % 2 == 0 ? bLeft : bRight));
}

void USimCoreTurnSignals::SetBodyDamage(const SimCoreDamagePresentation::FZoneWeights& Weights)
{
	const FTransform SourceToLocal = SourceBody
		? SourceBody->GetComponentTransform().GetRelativeTransform(GetComponentTransform()) : FTransform::Identity;
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
		Lights[Index]->SetRelativeLocation(SourceToLocal.TransformPosition(
			USimCoreDeformableBody::DeformVertex(RestLampPositions[Index], Weights) + FVector(0,0,2)));
}
