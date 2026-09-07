#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SimCoreClientComponent.h"
#include "SimCoreProtocol.h"
#include "SimCoreInstrumentCluster.generated.h"

namespace SimCoreInstrumentCluster
{
	struct FLayout
	{
		FVector2D Position = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		float Scale = 1.0f;
	};
	DRIVEINTEGRATION_API FLayout BuildLayout(float ViewWidth, float ViewHeight);

	/** Pure presentation model. Invalid or stale telemetry is never retained. */
	struct FDisplayState
	{
		bool bAuthoritative = false;
		bool bSideBrakeRequested = false;
		bool bHazardLightsRequested = false;
		SimCoreProtocol::ETurnIndicator ManualIndicator =
			SimCoreProtocol::ETurnIndicator::Off;
		FString StatusText = TEXT("OFFLINE");
		FLinearColor StatusColor = FLinearColor(0.90f, 0.20f, 0.16f, 1.0f);
		FString SpeedText = TEXT("---");
		FString RpmText = TEXT("----");
		FString GearText = TEXT("-");
		FString FuelText = TEXT("--");
		float SpeedKph = 0.0f;
		float EngineRpm = 0.0f;
		float FuelPercent = 0.0f;
	};

	DRIVEINTEGRATION_API FDisplayState BuildDisplayState(
		ESimCoreConnectionState ConnectionState,
		bool bHasState,
		const SimCoreProtocol::FVehicleState& State,
		double StateAgeSeconds,
		double StaleTimeoutSeconds,
		bool bSideBrakeRequested,
		SimCoreProtocol::ETurnIndicator ManualIndicator =
			SimCoreProtocol::ETurnIndicator::Off,
		bool bHazardLightsRequested = false);
}

/**
 * Code-native, presentation-only instrument cluster. It is installed by the
 * game mode, needs no Widget Blueprint, and never participates in simulation.
 */
UCLASS(NotBlueprintable, Transient)
class DRIVEINTEGRATION_API ASimCoreInstrumentClusterHud final : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawCluster(const SimCoreInstrumentCluster::FDisplayState& Display);
	void DrawCenteredText(
		const FString& Text,
		const FLinearColor& Color,
		float CenterX,
		float Y,
		UFont* Font,
		float Scale);
	void DrawArc(
		const FVector2D& Center,
		float Radius,
		float StartDegrees,
		float EndDegrees,
		const FLinearColor& Color,
		float Thickness,
		int32 Segments = 32);
};
