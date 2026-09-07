#include "SimCoreInstrumentCluster.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "ExternalVehiclePawn.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include <limits>

namespace
{
FString GearText(const SimCoreProtocol::EVehicleGear Gear)
{
	switch (Gear)
	{
	case SimCoreProtocol::EVehicleGear::Neutral: return TEXT("N");
	case SimCoreProtocol::EVehicleGear::Drive: return TEXT("D");
	case SimCoreProtocol::EVehicleGear::Reverse: return TEXT("R");
	default: return TEXT("-");
	}
}

void SetUnavailableStatus(
	SimCoreInstrumentCluster::FDisplayState& Display,
	const ESimCoreConnectionState ConnectionState,
	const bool bHasState,
	const bool bStateStale)
{
	switch (ConnectionState)
	{
	case ESimCoreConnectionState::Connecting:
	case ESimCoreConnectionState::Handshaking:
		Display.StatusText = TEXT("CONNECTING");
		Display.StatusColor = FLinearColor(0.25f, 0.65f, 1.0f, 1.0f);
		break;
	case ESimCoreConnectionState::WaitingToReconnect:
		Display.StatusText = TEXT("RECONNECTING");
		Display.StatusColor = FLinearColor(1.0f, 0.58f, 0.08f, 1.0f);
		break;
	case ESimCoreConnectionState::Incompatible:
		Display.StatusText = TEXT("INCOMPATIBLE");
		Display.StatusColor = FLinearColor(1.0f, 0.16f, 0.12f, 1.0f);
		break;
	case ESimCoreConnectionState::Connected:
		Display.StatusText = bStateStale ? TEXT("STALE") : (bHasState ? TEXT("INVALID") : TEXT("WAITING"));
		Display.StatusColor = FLinearColor(1.0f, 0.58f, 0.08f, 1.0f);
		break;
	case ESimCoreConnectionState::Stopping:
		Display.StatusText = TEXT("STOPPING");
		Display.StatusColor = FLinearColor(1.0f, 0.58f, 0.08f, 1.0f);
		break;
	case ESimCoreConnectionState::Disconnected:
	default:
		Display.StatusText = TEXT("OFFLINE");
		Display.StatusColor = FLinearColor(0.90f, 0.20f, 0.16f, 1.0f);
		break;
	}
}
}

SimCoreInstrumentCluster::FDisplayState SimCoreInstrumentCluster::BuildDisplayState(
	const ESimCoreConnectionState ConnectionState,
	const bool bHasState,
	const SimCoreProtocol::FVehicleState& State,
	const double StateAgeSeconds,
	const double StaleTimeoutSeconds,
	const bool bSideBrakeRequested,
	const SimCoreProtocol::ETurnIndicator ManualIndicator,
	const bool bHazardLightsRequested)
{
	FDisplayState Display;
	Display.bSideBrakeRequested = bSideBrakeRequested;
	Display.ManualIndicator = ManualIndicator;
	Display.bHazardLightsRequested = bHazardLightsRequested;

	const bool bAgeValid = FMath::IsFinite(StateAgeSeconds)
		&& FMath::IsFinite(StaleTimeoutSeconds)
		&& StateAgeSeconds >= 0.0
		&& StaleTimeoutSeconds > 0.0;
	const bool bStateStale = bHasState && (!bAgeValid || StateAgeSeconds > StaleTimeoutSeconds);
	const bool bWorldVelocityFinite = FMath::IsFinite(State.LinearVelocityEnu.X)
		&& FMath::IsFinite(State.LinearVelocityEnu.Y)
		&& FMath::IsFinite(State.LinearVelocityEnu.Z);
	const bool bTelemetryFinite = FMath::IsFinite(State.SpeedMps)
		&& bWorldVelocityFinite
		&& FMath::IsFinite(State.EngineRpm)
		&& FMath::IsFinite(State.FuelPercent);
	const FString ParsedGear = GearText(State.Gear);

	if (ConnectionState != ESimCoreConnectionState::Connected
		|| !bHasState
		|| bStateStale
		|| !bTelemetryFinite
		|| ParsedGear == TEXT("-"))
	{
		SetUnavailableStatus(Display, ConnectionState, bHasState, bStateStale);
		return Display;
	}

	Display.bAuthoritative = true;
	Display.StatusText = TEXT("LIVE");
	Display.StatusColor = FLinearColor(0.10f, 0.88f, 0.55f, 1.0f);
	const double WorldSpeedMps = State.LinearVelocityEnu.Length();
	// A real speedometer shows path speed, not only body-forward velocity.
	// Fall back for older snapshots that omitted the additive ENU velocity.
	const double RoadSpeedMps = WorldSpeedMps > 1.0e-3 || FMath::Abs(State.SpeedMps) <= 1.0e-3f
		? WorldSpeedMps
		: FMath::Abs(State.SpeedMps);
	Display.SpeedKph = static_cast<float>(RoadSpeedMps * 3.6);
	Display.EngineRpm = FMath::Max(State.EngineRpm, 0.0f);
	Display.FuelPercent = FMath::Clamp(State.FuelPercent, 0.0f, 100.0f);
	Display.SpeedText = FString::Printf(TEXT("%03d"), FMath::Clamp(FMath::RoundToInt(Display.SpeedKph), 0, 999));
	Display.RpmText = FString::Printf(TEXT("%04d"), FMath::Clamp(FMath::RoundToInt(Display.EngineRpm), 0, 9999));
	Display.GearText = ParsedGear;
	Display.FuelText = FString::Printf(TEXT("%d"), FMath::RoundToInt(Display.FuelPercent));
	return Display;
}

void ASimCoreInstrumentClusterHud::DrawHUD()
{
	Super::DrawHUD();
	if (Canvas == nullptr)
	{
		return;
	}

	ESimCoreConnectionState ConnectionState = ESimCoreConnectionState::Disconnected;
	SimCoreProtocol::FVehicleState State;
	float StateAgeSeconds = std::numeric_limits<float>::infinity();
	float StaleTimeoutSeconds = 0.1f;
	bool bHasState = false;
	bool bSideBrakeRequested = false;
	SimCoreProtocol::ETurnIndicator ManualIndicator =
		SimCoreProtocol::ETurnIndicator::Off;
	bool bHazardLightsRequested = false;

	const APlayerController* Controller = GetOwningPlayerController();
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	const USimCoreClientComponent* Client = Pawn != nullptr
		? Pawn->FindComponentByClass<USimCoreClientComponent>()
		: nullptr;
	if (Client != nullptr)
	{
		ConnectionState = Client->GetConnectionState();
		bHasState = Client->GetLatestState(State, StateAgeSeconds);
		StaleTimeoutSeconds = Client->HealthStateStaleTimeoutSeconds;
		bSideBrakeRequested = Client->IsSideBrakeRequested();
	}
	if (const AExternalVehiclePawn* Vehicle = Cast<AExternalVehiclePawn>(Pawn))
	{
		ManualIndicator = Vehicle->GetManualIndicator();
		bHazardLightsRequested = Vehicle->AreHazardLightsEnabled();
	}

	DrawCluster(SimCoreInstrumentCluster::BuildDisplayState(
		ConnectionState,
		bHasState,
		State,
		StateAgeSeconds,
		StaleTimeoutSeconds,
		bSideBrakeRequested,
		ManualIndicator,
		bHazardLightsRequested));
}

void ASimCoreInstrumentClusterHud::DrawCluster(const SimCoreInstrumentCluster::FDisplayState& Display)
{
	const float Scale = FMath::Clamp(FMath::Min(Canvas->SizeX / 1920.0f, Canvas->SizeY / 1080.0f), 0.55f, 1.35f);
	const float Width = 850.0f * Scale;
	const float Height = 220.0f * Scale;
	const float X = (Canvas->SizeX - Width) * 0.5f;
	const float Y = Canvas->SizeY - Height - 26.0f * Scale;
	const float CenterX = X + Width * 0.5f;
	const FLinearColor Panel(0.012f, 0.018f, 0.030f, 0.91f);
	const FLinearColor PanelEdge(0.11f, 0.20f, 0.31f, 0.95f);
	const FLinearColor White(0.90f, 0.96f, 1.0f, 1.0f);
	const FLinearColor Muted(0.36f, 0.48f, 0.58f, 1.0f);
	const FLinearColor Cyan(0.08f, 0.76f, 1.0f, 1.0f);
	const FLinearColor Amber(1.0f, 0.58f, 0.08f, 1.0f);
	const FLinearColor HazardRed(1.0f, 0.16f, 0.08f, 1.0f);

	DrawRect(Panel, X, Y, Width, Height);
	DrawRect(PanelEdge, X, Y, Width, 2.0f * Scale);
	DrawRect(PanelEdge, X, Y + Height - 2.0f * Scale, Width, 2.0f * Scale);
	DrawRect(PanelEdge, X, Y, 2.0f * Scale, Height);
	DrawRect(PanelEdge, X + Width - 2.0f * Scale, Y, 2.0f * Scale, Height);

	UFont* Small = GEngine != nullptr ? GEngine->GetSmallFont() : nullptr;
	UFont* Medium = GEngine != nullptr ? GEngine->GetMediumFont() : nullptr;
	UFont* Large = GEngine != nullptr ? GEngine->GetLargeFont() : nullptr;
	if (Small == nullptr || Medium == nullptr || Large == nullptr)
	{
		return;
	}

	const float StatusWidth = 112.0f * Scale;
	DrawRect(FLinearColor(Display.StatusColor.R, Display.StatusColor.G, Display.StatusColor.B, 0.15f),
		CenterX - StatusWidth * 0.5f, Y + 10.0f * Scale, StatusWidth, 22.0f * Scale);
	DrawCenteredText(Display.StatusText, Display.StatusColor, CenterX, Y + 12.0f * Scale, Small, 0.82f * Scale);
	const bool bLeftSelected = Display.bHazardLightsRequested
		|| Display.ManualIndicator == SimCoreProtocol::ETurnIndicator::Left;
	const bool bRightSelected = Display.bHazardLightsRequested
		|| Display.ManualIndicator == SimCoreProtocol::ETurnIndicator::Right;
	const float IndicatorY = Y + 14.0f * Scale;
	const float LeftIndicatorX = CenterX - 92.0f * Scale;
	const float RightIndicatorX = CenterX + 92.0f * Scale;
	DrawRect(FLinearColor(Amber.R, Amber.G, Amber.B, bLeftSelected ? 0.24f : 0.04f),
		LeftIndicatorX - 25.0f * Scale, Y + 9.0f * Scale, 50.0f * Scale, 25.0f * Scale);
	DrawRect(FLinearColor(Amber.R, Amber.G, Amber.B, bRightSelected ? 0.24f : 0.04f),
		RightIndicatorX - 25.0f * Scale, Y + 9.0f * Scale, 50.0f * Scale, 25.0f * Scale);
	DrawCenteredText(TEXT("< Q"), bLeftSelected ? Amber : Muted,
		LeftIndicatorX, IndicatorY, Small, 0.82f * Scale);
	DrawCenteredText(TEXT("E >"), bRightSelected ? Amber : Muted,
		RightIndicatorX, IndicatorY, Small, 0.82f * Scale);
	if (Display.bHazardLightsRequested)
	{
		DrawCenteredText(TEXT("X HAZARD"), HazardRed, CenterX,
			Y + 34.0f * Scale, Small, 0.70f * Scale);
	}

	// Left: an RPM sweep with a thin redline segment. The raw number remains authoritative.
	const FVector2D TachCenter(X + 170.0f * Scale, Y + 133.0f * Scale);
	DrawArc(TachCenter, 82.0f * Scale, 145.0f, 395.0f, Muted, 3.0f * Scale, 40);
	const float RpmFraction = Display.bAuthoritative ? FMath::Clamp(Display.EngineRpm / 8000.0f, 0.0f, 1.0f) : 0.0f;
	DrawArc(TachCenter, 82.0f * Scale, 145.0f, 145.0f + 250.0f * RpmFraction, Cyan, 5.0f * Scale, 40);
	DrawArc(TachCenter, 82.0f * Scale, 355.0f, 395.0f, FLinearColor(1.0f, 0.13f, 0.10f, 1.0f), 5.0f * Scale, 8);
	DrawCenteredText(Display.RpmText, Display.bAuthoritative ? White : Muted,
		TachCenter.X, Y + 104.0f * Scale, Medium, 1.0f * Scale);
	DrawCenteredText(TEXT("RPM"), Muted, TachCenter.X, Y + 135.0f * Scale, Small, 0.72f * Scale);

	// Centre: large digital speed and gear, legible at 720p through 4K.
	DrawCenteredText(Display.SpeedText, Display.bAuthoritative ? White : Muted,
		CenterX, Y + 50.0f * Scale, Large, 1.72f * Scale);
	DrawCenteredText(TEXT("KM/H"), Muted, CenterX, Y + 117.0f * Scale, Small, 0.78f * Scale);
	DrawCenteredText(Display.GearText, Display.bAuthoritative ? Cyan : Muted,
		CenterX, Y + 143.0f * Scale, Large, 1.18f * Scale);

	// Right: fuel percentage plus a segmented horizontal gauge.
	const float FuelX = X + Width - 270.0f * Scale;
	DrawCenteredText(TEXT("FUEL"), Muted, FuelX + 100.0f * Scale, Y + 66.0f * Scale, Small, 0.76f * Scale);
	DrawCenteredText(Display.FuelText + TEXT("%"), Display.bAuthoritative ? White : Muted,
		FuelX + 100.0f * Scale, Y + 91.0f * Scale, Medium, 1.0f * Scale);
	const float SegmentWidth = 15.0f * Scale;
	for (int32 Segment = 0; Segment < 10; ++Segment)
	{
		const bool bFilled = Display.bAuthoritative && Display.FuelPercent >= (Segment + 1) * 10.0f;
		const FLinearColor FuelColor = Display.FuelPercent <= 15.0f ? Amber : Cyan;
		DrawRect(bFilled ? FuelColor : FLinearColor(0.10f, 0.16f, 0.21f, 1.0f),
			FuelX + Segment * (SegmentWidth + 4.0f * Scale), Y + 132.0f * Scale,
			SegmentWidth, 9.0f * Scale);
	}

	const FLinearColor BrakeColor = Display.bSideBrakeRequested ? Amber : Muted;
	DrawRect(FLinearColor(BrakeColor.R, BrakeColor.G, BrakeColor.B,
		Display.bSideBrakeRequested ? 0.18f : 0.06f),
		FuelX + 43.0f * Scale, Y + 166.0f * Scale, 114.0f * Scale, 24.0f * Scale);
	DrawCenteredText(Display.bSideBrakeRequested ? TEXT("SIDE BRAKE") : TEXT("SIDE BRAKE OFF"),
		BrakeColor, FuelX + 100.0f * Scale, Y + 170.0f * Scale, Small, 0.72f * Scale);
	DrawCenteredText(TEXT("Q LEFT   X HAZARD   E RIGHT"), Muted,
		CenterX, Y + Height - 22.0f * Scale, Small, 0.60f * Scale);
}

void ASimCoreInstrumentClusterHud::DrawCenteredText(
	const FString& Text,
	const FLinearColor& Color,
	const float CenterX,
	const float Y,
	UFont* Font,
	const float Scale)
{
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	GetTextSize(Text, TextWidth, TextHeight, Font, Scale);
	DrawText(Text, Color, CenterX - TextWidth * 0.5f, Y, Font, Scale, false);
}

void ASimCoreInstrumentClusterHud::DrawArc(
	const FVector2D& Center,
	const float Radius,
	const float StartDegrees,
	const float EndDegrees,
	const FLinearColor& Color,
	const float Thickness,
	const int32 Segments)
{
	if (EndDegrees <= StartDegrees || Segments <= 0)
	{
		return;
	}

	FVector2D Previous = FVector2D::ZeroVector;
	for (int32 Index = 0; Index <= Segments; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / Segments;
		const float Radians = FMath::DegreesToRadians(FMath::Lerp(StartDegrees, EndDegrees, Alpha));
		const FVector2D Point = Center + FVector2D(FMath::Cos(Radians), FMath::Sin(Radians)) * Radius;
		if (Index > 0)
		{
			DrawLine(Previous.X, Previous.Y, Point.X, Point.Y, Color, Thickness);
		}
		Previous = Point;
	}
}
