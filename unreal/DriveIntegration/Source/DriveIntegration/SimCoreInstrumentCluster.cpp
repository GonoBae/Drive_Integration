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

SimCoreInstrumentCluster::FLayout SimCoreInstrumentCluster::BuildLayout(float ViewWidth, float ViewHeight)
{
	FLayout Layout;
	ViewWidth = FMath::IsFinite(ViewWidth) ? FMath::Max(1.0f, ViewWidth) : 1.0f;
	ViewHeight = FMath::IsFinite(ViewHeight) ? FMath::Max(1.0f, ViewHeight) : 1.0f;
	Layout.Scale = FMath::Min3(ViewWidth / 1280.0f, ViewHeight / 720.0f, 1.35f);
	Layout.Size = FVector2D(350.0f, 136.0f) * Layout.Scale;
	const float Margin = 16.0f * Layout.Scale;
	Layout.Position = FVector2D(ViewWidth, ViewHeight) - Layout.Size - FVector2D(Margin, Margin);
	return Layout;
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

	auto Display = SimCoreInstrumentCluster::BuildDisplayState(
		ConnectionState,
		bHasState,
		State,
		StateAgeSeconds,
		StaleTimeoutSeconds,
		bSideBrakeRequested,
		ManualIndicator,
		bHazardLightsRequested);
	if (Client && ConnectionState == ESimCoreConnectionState::Connected)
	{
		// Safety remains visible when the optional numeric diagnostics are off.
		const auto Health = Client->GetHealthDisplay();
		Display.StatusText = Health.Status;
		Display.StatusColor = FLinearColor(Health.Color);
	}
	DrawCluster(Display);
}

void ASimCoreInstrumentClusterHud::DrawCluster(const SimCoreInstrumentCluster::FDisplayState& Display)
{
	const SimCoreInstrumentCluster::FLayout Layout =
		SimCoreInstrumentCluster::BuildLayout(Canvas->SizeX, Canvas->SizeY);
	const float Scale = Layout.Scale;
	const float X = Layout.Position.X;
	const float Y = Layout.Position.Y;
	const FLinearColor White(0.92f, 0.96f, 1.0f);
	const FLinearColor Muted(0.52f, 0.61f, 0.68f);
	const FLinearColor Cyan(0.08f, 0.76f, 1.0f);
	const FLinearColor Amber(1.0f, 0.58f, 0.08f);
	DrawRect(FLinearColor(0.012f, 0.018f, 0.03f, 0.62f),
		X, Y, Layout.Size.X, Layout.Size.Y);
	UFont* Small = GEngine ? GEngine->GetSmallFont() : nullptr;
	UFont* Large = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Small || !Large) return;

	const FVector2D Dial(X + 76.0f * Scale, Y + 82.0f * Scale);
	DrawArc(Dial, 55.0f * Scale, 145.0f, 395.0f, Muted, 2.0f * Scale);
	const float RpmFraction = Display.bAuthoritative
		? FMath::Clamp(Display.EngineRpm / 8000.0f, 0.0f, 1.0f) : 0.0f;
	DrawArc(Dial, 55.0f * Scale, 145.0f, 145.0f + RpmFraction * 250.0f, Cyan, 3.0f * Scale);
	DrawCenteredText(Display.RpmText, White, Dial.X, Y + 61.0f * Scale, Small, Scale);
	DrawCenteredText(TEXT("RPM"), Muted, Dial.X, Y + 84.0f * Scale, Small, 0.8f * Scale);

	DrawCenteredText(Display.SpeedText, Display.bAuthoritative ? White : Muted,
		X + 205.0f * Scale, Y + 30.0f * Scale, Large, 1.40f * Scale);
	DrawCenteredText(TEXT("km/h"), Muted, X + 205.0f * Scale, Y + 84.0f * Scale, Small, Scale);
	DrawCenteredText(Display.GearText, Cyan, X + 299.0f * Scale, Y + 40.0f * Scale, Large, 1.05f * Scale);
	DrawCenteredText(Display.FuelText + TEXT("%"), Muted,
		X + 299.0f * Scale, Y + 86.0f * Scale, Small, 0.85f * Scale);
	DrawRect(Muted, X + 274.0f * Scale, Y + 108.0f * Scale, 50.0f * Scale, 3.0f * Scale);
	if (Display.bAuthoritative)
		DrawRect(Display.FuelPercent < 15.0f ? Amber : Cyan,
			X + 274.0f * Scale, Y + 108.0f * Scale,
			50.0f * Scale * Display.FuelPercent / 100.0f, 3.0f * Scale);

	const bool bBlink = GetWorld() && FMath::Fmod(GetWorld()->GetTimeSeconds(), 0.8f) < 0.4f;
	const bool bLeft = bBlink && (Display.bHazardLightsRequested
		|| Display.ManualIndicator == SimCoreProtocol::ETurnIndicator::Left);
	const bool bRight = bBlink && (Display.bHazardLightsRequested
		|| Display.ManualIndicator == SimCoreProtocol::ETurnIndicator::Right);
	DrawCenteredText(TEXT("<"), bLeft ? Amber : Muted,
		X + 28.0f * Scale, Y + 8.0f * Scale, Small, Scale);
	DrawCenteredText(TEXT(">"), bRight ? Amber : Muted,
		X + 322.0f * Scale, Y + 8.0f * Scale, Small, Scale);
	DrawCenteredText(Display.StatusText, Display.StatusColor,
		X + 175.0f * Scale, Y + 8.0f * Scale, Small, 0.90f * Scale);
	if (Display.bSideBrakeRequested)
		DrawCenteredText(TEXT("(P)"), Amber,
			X + 249.0f * Scale, Y + 111.0f * Scale, Small, 0.85f * Scale);
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
