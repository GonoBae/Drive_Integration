#include "SimCoreInstrumentCluster.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "ExternalVehiclePawn.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "SimCoreVehicleVisualProfile.h"

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
	if (const AExternalVehiclePawn* Vehicle = Cast<AExternalVehiclePawn>(Pawn)) DrawGarage(*Vehicle);
}

void ASimCoreInstrumentClusterHud::DrawGarage(const AExternalVehiclePawn& Vehicle)
{
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	UFont* TitleFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Font || !TitleFont || Canvas->SizeX <= 0 || Canvas->SizeY <= 0) return;
	const FLinearColor White(.92f, .96f, 1.f);
	const FLinearColor Muted(.60f, .69f, .76f);
	const FLinearColor Cyan(.08f, .76f, 1.f);
	if (!Vehicle.IsGarageOpen())
	{
		const auto Cluster = SimCoreInstrumentCluster::BuildLayout(Canvas->SizeX, Canvas->SizeY);
		const float S = Cluster.Scale;
		const float X = Cluster.Position.X;
		const float Y = FMath::Max(0.f, static_cast<float>(Cluster.Position.Y) - 31.f * S);
		DrawRect(FLinearColor(.012f, .018f, .03f, .62f), X, Y, 118.f * S, 25.f * S);
		DrawGarageText(TEXT("G  Garage"), Muted, X + 9.f * S, Y + 3.f * S,
			100.f * S, Font, .92f * S, 20.f * S);
		return;
	}

	// A fixed logical layout scales as a unit so the panel, footer and cluster
	// stay on screen together even in a small editor viewport.
	const float S = FMath::Min3(Canvas->SizeX / 1920.f, Canvas->SizeY / 1080.f, 1.25f);
	const float Width = 1100.f * S;
	const float Height = 720.f * S;
	const float X = (Canvas->SizeX - Width) * .5f;
	const float Y = 48.f * S;
	const float Padding = 28.f * S;
	DrawRect(FLinearColor(.015f, .023f, .035f, .97f), X, Y, Width, Height);
	DrawRect(Cyan, X, Y, Width, 3.f * S);
	DrawGarageText(TEXT("PLAYER GARAGE"), White, X + Padding, Y + 23.f * S,
		Width - 2.f * Padding, TitleFont, .85f * S, 36.f * S);
	DrawGarageText(TEXT("Apply resets the world. Same-class NPCs share parts; NPC suspension stays simplified."), Muted,
		X + Padding, Y + 69.f * S, Width - 2.f * Padding, Font, 1.05f * S, 25.f * S);
	DrawRect(FLinearColor(.15f, .21f, .27f), X + Padding, Y + 108.f * S,
		Width - 2.f * Padding, S);
	const bool bPartsMode = Vehicle.IsGaragePartsMode();
	const float ListWidth = (bPartsMode ? 400.f : 300.f) * S;
	const float DetailsX = X + (bPartsMode ? 458.f : 358.f) * S;
	const float DetailsWidth = Width - (DetailsX - X) - Padding;
	DrawGarageText(bPartsMode ? TEXT("PART SLOTS") : TEXT("LOADOUTS"), Muted, X + Padding, Y + 125.f * S,
		ListWidth, Font, S, 24.f * S);
	DrawGarageText(bPartsMode ? TEXT("SELECTED PART") : TEXT("PARTS PREVIEW"), Muted, DetailsX, Y + 125.f * S,
		DetailsWidth, Font, S, 24.f * S);
	const auto& Choices = Vehicle.GetGarageChoices();
	const int32 Selected = Choices.IsEmpty() ? INDEX_NONE
		: FMath::Clamp(Vehicle.GetGarageSelection(), 0, Choices.Num() - 1);
	if (bPartsMode)
	{
		const int32 SelectedSlot = FMath::Clamp(Vehicle.GetGaragePartSlot(), 0,
			SimCoreVehicleVisualProfile::PartsSlotCount - 1);
		const auto& PartNames = Vehicle.GetGaragePartNames();
		for (int32 Slot = 0; Slot < SimCoreVehicleVisualProfile::PartsSlotCount; ++Slot)
		{
			const float RowY = Y + (162.f + Slot * 49.f) * S;
			const bool bSelectedSlot = Slot == SelectedSlot;
			DrawRect(bSelectedSlot ? FLinearColor(.035f, .18f, .25f) : FLinearColor(.035f, .05f, .07f),
				X + Padding, RowY, ListWidth, 46.f * S);
			if (bSelectedSlot) DrawRect(Cyan, X + Padding, RowY, 3.f * S, 46.f * S);
			DrawGarageText(SimCoreVehicleVisualProfile::PartSlotName(Slot), bSelectedSlot ? Cyan : Muted,
				X + Padding + 12.f * S, RowY + 3.f * S, ListWidth - 24.f * S, Font, .90f * S, 20.f * S);
			DrawGarageText(PartNames.IsValidIndex(Slot) ? PartNames[Slot] : FString(TEXT("No selection")), White,
				X + Padding + 12.f * S, RowY + 23.f * S, ListWidth - 24.f * S, Font, S, 21.f * S);
		}
		const FString BaseName = Choices.IsValidIndex(Selected)
			? (Choices[Selected].Name.IsEmpty() ? FString(TEXT("Vehicle default")) : Choices[Selected].Name)
			: FString(TEXT("Vehicle default"));
		DrawGarageText(TEXT("Base: ") + BaseName, Muted, DetailsX, Y + 162.f * S,
			DetailsWidth, Font, 1.02f * S, 23.f * S, 2);
		DrawGarageText(SimCoreVehicleVisualProfile::PartSlotName(SelectedSlot), Cyan,
			DetailsX, Y + 222.f * S, DetailsWidth, Font, 1.20f * S, 26.f * S);
		const auto& PartChoices = Vehicle.GetGaragePartChoices();
		const int32 PartIndex = PartChoices.IsEmpty() ? INDEX_NONE
			: FMath::Clamp(Vehicle.GetGaragePartChoice(), 0, PartChoices.Num() - 1);
		if (PartChoices.IsValidIndex(PartIndex))
		{
			const auto& Part = PartChoices[PartIndex];
			DrawGarageText(Part.Name.IsEmpty() ? FString(TEXT("Vehicle baseline")) : Part.Name,
				White, DetailsX, Y + 261.f * S, DetailsWidth, Font, 1.18f * S, 28.f * S, 2);
			DrawGarageText(Part.Summary, White, DetailsX, Y + 328.f * S,
				DetailsWidth, Font, 1.06f * S, 27.f * S, 6);
			const FString OptionText = PartChoices.Num() == 1 ? FString(TEXT("Only one catalog option"))
				: FString::Printf(TEXT("%d / %d options; Left/Right changes part"), PartIndex + 1, PartChoices.Num());
			DrawGarageText(OptionText, Muted, DetailsX, Y + 518.f * S,
				DetailsWidth, Font, 1.02f * S, 23.f * S, 2);
		}
		else DrawGarageText(TEXT("No catalog options available for this slot"), Muted,
			DetailsX, Y + 261.f * S, DetailsWidth, Font, 1.06f * S, 25.f * S, 2);
	}
	else
	{
		const int32 VisibleRows = 4;
		const int32 First = FMath::Clamp(Selected - VisibleRows + 1, 0, FMath::Max(0, Choices.Num() - VisibleRows));
		for (int32 Index = First; Index < FMath::Min(First + VisibleRows, Choices.Num()); ++Index)
		{
			const float RowY = Y + (162.f + (Index - First) * 84.f) * S;
			const bool bSelected = Index == Selected;
			DrawRect(bSelected ? FLinearColor(.035f, .18f, .25f) : FLinearColor(.035f, .05f, .07f),
				X + Padding, RowY, ListWidth, 74.f * S);
			if (bSelected) DrawRect(Cyan, X + Padding, RowY, 3.f * S, 74.f * S);
			const FString Name = Choices[Index].Name.IsEmpty()
				? (Choices[Index].Id.IsEmpty() ? FString(TEXT("Vehicle default")) : Choices[Index].Id)
				: Choices[Index].Name;
			DrawGarageText(Name, bSelected ? White : Muted,
				X + Padding + 15.f * S, RowY + 12.f * S, ListWidth - 30.f * S,
				Font, 1.15f * S, 25.f * S, 2);
		}
		if (Choices.IsValidIndex(Selected))
		{
			DrawGarageText(FString::Printf(TEXT("%d / %d  -  Up/Down to choose"), Selected + 1, Choices.Num()),
				Muted, X + Padding, Y + 528.f * S, ListWidth, Font, .95f * S, 23.f * S, 2);
			const auto& Details = Choices[Selected].Profile.ModuleDetails;
			const int32 VisibleDetails = FMath::Min(Details.Num(), 8);
			for (int32 Index = 0; Index < VisibleDetails; ++Index)
			{
				const float RowY = Y + (162.f + Index * 49.f) * S;
				int32 MetadataStart = INDEX_NONE;
				// Keep weight and tire radius visible even if a long part name must
				// be shortened. Catalog details put these values in a final suffix.
				if (Details[Index].EndsWith(TEXT(")")) && Details[Index].FindLastChar(TEXT('('), MetadataStart))
				{
					DrawGarageText(Details[Index].Left(MetadataStart).TrimEnd(), White,
						DetailsX, RowY, DetailsWidth, Font, 1.05f * S, 23.f * S);
					DrawGarageText(Details[Index].Mid(MetadataStart), Muted, DetailsX, RowY + 23.f * S,
						DetailsWidth, Font, .98f * S, 23.f * S);
				}
				else DrawGarageText(Details[Index], White, DetailsX, RowY, DetailsWidth,
					Font, 1.05f * S, 23.f * S, 2);
			}
			if (Details.IsEmpty()) DrawGarageText(TEXT("Vehicle baseline parts"), Muted,
				DetailsX, Y + 162.f * S, DetailsWidth, Font, 1.05f * S, 25.f * S);
		}
		else DrawGarageText(TEXT("No loadouts available"), Muted, X + Padding,
			Y + 162.f * S, ListWidth, Font, 1.05f * S, 25.f * S, 2);
	}

	DrawRect(FLinearColor(.025f, .065f, .09f), X + Padding, Y + 577.f * S,
		Width - 2.f * Padding, 65.f * S);
	DrawGarageText(Vehicle.GetGarageStatusText(), White, X + Padding + 12.f * S, Y + 585.f * S,
		Width - 2.f * Padding - 24.f * S, Font, 1.12f * S, 25.f * S, 2);
	if (bPartsMode)
	{
		DrawGarageText(TEXT("Up/Down: slot   Left/Right: change part   Enter: apply at spawn"), Muted,
			X + Padding, Y + 659.f * S, Width - 2.f * Padding, Font, 1.02f * S, 23.f * S);
		DrawGarageText(TEXT("Tab: back (discards draft)   G: close"), Muted,
			X + Padding, Y + 686.f * S, Width - 2.f * Padding, Font, 1.02f * S, 23.f * S);
	}
	else DrawGarageText(TEXT("Up/Down: choose   Tab: parts   Enter: apply at spawn   G: close"), Muted,
		X + Padding, Y + 667.f * S, Width - 2.f * Padding, Font, 1.05f * S, 23.f * S, 2);
}

void ASimCoreInstrumentClusterHud::DrawGarageText(const FString& Text, const FLinearColor& Color,
	const float X, const float Y, const float Width, UFont* Font, const float Scale,
	const float LineHeight, const int32 MaxLines)
{
	if (Width <= 0.f || Scale <= 0.f || MaxLines <= 0) return;
	FString Remaining = Text.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\t"), TEXT(" "));
	Remaining.TrimStartAndEndInline();
	for (int32 Line = 0; Line < MaxLines && !Remaining.IsEmpty(); ++Line)
	{
		float MeasuredWidth = 0.f, MeasuredHeight = 0.f;
		GetTextSize(Remaining, MeasuredWidth, MeasuredHeight, Font, Scale);
		if (MeasuredWidth <= Width)
		{
			DrawText(Remaining, Color, X, Y + Line * LineHeight, Font, Scale, false);
			return;
		}
		const bool bLastLine = Line == MaxLines - 1;
		const FString Suffix = bLastLine ? TEXT("...") : TEXT("");
		int32 Low = 0, High = Remaining.Len();
		while (Low < High)
		{
			const int32 Mid = (Low + High + 1) / 2;
			GetTextSize(Remaining.Left(Mid) + Suffix, MeasuredWidth, MeasuredHeight, Font, Scale);
			if (MeasuredWidth <= Width) Low = Mid;
			else High = Mid - 1;
		}
		if (Low <= 0) return;
		int32 LastSpace = INDEX_NONE;
		if (!bLastLine && Remaining.Left(Low).FindLastChar(TEXT(' '), LastSpace) && LastSpace > 0) Low = LastSpace;
		DrawText(Remaining.Left(Low).TrimEnd() + Suffix, Color, X, Y + Line * LineHeight, Font, Scale, false);
		Remaining = Remaining.Mid(Low).TrimStart();
	}
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
