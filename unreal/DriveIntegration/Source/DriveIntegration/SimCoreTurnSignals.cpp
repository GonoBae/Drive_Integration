#include "SimCoreTurnSignals.h"
#include "SimCoreDeformableBody.h"
#include "SimCoreSedanVisualContract.h"

#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

double SimCoreTurnSignals::FPhaseClock::Update(SimCoreProtocol::ETurnIndicator Direction,
	double TimeSeconds, bool bHazard)
{
	if (bHazard) Direction = SimCoreProtocol::ETurnIndicator::Off;
	const bool bSelected = bHazard || Direction == SimCoreProtocol::ETurnIndicator::Left
		|| Direction == SimCoreProtocol::ETurnIndicator::Right;
	if (!FMath::IsFinite(TimeSeconds) || TimeSeconds < 0.0 || !bSelected)
	{
		Reset();
		ElapsedSeconds = FMath::IsFinite(TimeSeconds) && TimeSeconds >= 0.0 ? 0.0 : -1.0;
		return ElapsedSeconds;
	}
	const bool bClockRestarted = TimeSeconds < LastTimeSeconds - 0.25;
	if (!bClockRestarted) TimeSeconds = FMath::Max(TimeSeconds, LastTimeSeconds);
	if (SelectionTimeSeconds < 0.0 || Direction != SelectedDirection
		|| bHazard != bSelectedHazard || bClockRestarted)
	{
		SelectionTimeSeconds = TimeSeconds;
	}
	SelectedDirection = Direction;
	bSelectedHazard = bHazard;
	LastTimeSeconds = TimeSeconds;
	ElapsedSeconds = TimeSeconds - SelectionTimeSeconds;
	return ElapsedSeconds;
}

void SimCoreTurnSignals::FPhaseClock::Reset()
{
	SelectedDirection = SimCoreProtocol::ETurnIndicator::Off;
	bSelectedHazard = false;
	SelectionTimeSeconds = LastTimeSeconds = -1.0;
	ElapsedSeconds = 0.0;
}

bool SimCoreTurnSignals::FAutoCancel::Update(SimCoreProtocol::ETurnIndicator Direction,
	bool bHazard, const SimCoreProtocol::FVehicleState& State, bool bFresh)
{
	using SimCoreProtocol::ETurnIndicator;
	if (bHazard || (Direction != ETurnIndicator::Left && Direction != ETurnIndicator::Right)
		|| !bFresh || State.PlaySessionId.IsEmpty()
		|| !FMath::IsFinite(State.SteeringAngleRad) || !FMath::IsFinite(State.HeadingDegrees)
		|| !FMath::IsFinite(State.LinearVelocityBody.X))
	{
		Reset();
		return false;
	}
	if (Direction != SelectedDirection || State.PlaySessionId != PlaySessionId
		|| (bHasSample && State.SimulationTimeNs < LastSimulationTimeNs))
	{
		Reset();
		SelectedDirection = Direction;
		PlaySessionId = State.PlaySessionId;
	}
	// Repeated render frames or pause cannot add turn evidence from one packet.
	if (bHasSample && State.SimulationTimeNs == LastSimulationTimeNs) return false;
	const double DeltaSeconds = bHasSample
		? static_cast<double>(State.SimulationTimeNs - LastSimulationTimeNs) * 1.e-9 : 0.0;
	const double HeadingChange = bHasSample
		? FMath::FindDeltaAngleDegrees(LastHeadingDegrees, static_cast<double>(State.HeadingDegrees)) : 0.0;
	LastSimulationTimeNs = State.SimulationTimeNs;
	LastHeadingDegrees = State.HeadingDegrees;
	bHasSample = true;
	if (DeltaSeconds > 0.25 || FMath::Abs(HeadingChange) > 45.0)
	{
		Phase = EPhase::WaitingForTurn;
		bSteeringSeen = false;
		SelectedTurnDegrees = 0.0;
		return false;
	}
	// Stationary steering, reverse manoeuvres and parked heading corrections
	// are not a completed forward turn. A previously armed latch waits for motion.
	if (State.LinearVelocityBody.X < 0.5) return false;
	const double Side = Direction == ETurnIndicator::Left ? 1.0 : -1.0;
	const double Steering = Side * State.SteeringAngleRad;
	constexpr double TurnSteeringRad = 0.12;
	constexpr double CentreSteeringRad = 0.035;
	if (Phase == EPhase::WaitingForTurn)
	{
		if (Steering < -CentreSteeringRad)
		{
			bSteeringSeen = false;
			SelectedTurnDegrees = 0.0;
			return false;
		}
		bSteeringSeen |= Steering >= TurnSteeringRad;
		// ENU compass heading increases right; the rack is FLU left-positive.
		if (bSteeringSeen) SelectedTurnDegrees = FMath::Max(0.0, SelectedTurnDegrees - Side * HeadingChange);
		if (bSteeringSeen && SelectedTurnDegrees >= 8.0) Phase = EPhase::WaitingForCentre;
	}
	if (Phase == EPhase::WaitingForCentre && FMath::Abs(Steering) <= CentreSteeringRad)
	{
		Reset();
		return true;
	}
	return false;
}

void SimCoreTurnSignals::FAutoCancel::Reset()
{
	*this = FAutoCancel{};
}

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
	return bSelected && FMath::Fmod(TimeSeconds + 1.e-9, SimCoreTurnSignals::PeriodSeconds) < SimCoreTurnSignals::OnSeconds;
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
	const double PhaseTime = PhaseClock.Update(Direction, TimeSeconds, bHazard);
	const bool bLeft = IsLit(Direction, true, PhaseTime, bHazard);
	const bool bRight = IsLit(Direction, false, PhaseTime, bHazard);
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
