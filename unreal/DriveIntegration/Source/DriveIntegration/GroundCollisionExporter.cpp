#include "GroundCollisionExporter.h"

#include "SimCoreGroundSnapshot.h"
#include "SimCoreMapPackage.h"
#include "SimCoreStaticCollider.h"
#include "SimCoreStaticCollisionSnapshot.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSimCoreGroundExporter, Log, All);

namespace
{
constexpr int32 MaxExportedStaticColliderCount = 4096;

struct FStaticColliderCsvRecord
{
	FString ColliderId;
	FString Row;
};

bool IsValidColliderId(const FString& ColliderId)
{
	if (ColliderId.IsEmpty() || ColliderId.Len() > 128)
	{
		return false;
	}
	for (const TCHAR Character : ColliderId)
	{
		const bool bAsciiLetter =
			(Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= TEXT('A') && Character <= TEXT('Z'));
		const bool bAsciiDigit =
			Character >= TEXT('0') && Character <= TEXT('9');
		if (!bAsciiLetter && !bAsciiDigit
			&& Character != TEXT('_')
			&& Character != TEXT('-')
			&& Character != TEXT('.'))
		{
			return false;
		}
	}
	return true;
}

const TCHAR* StaticColliderSemanticText(
	ESimCoreStaticColliderSemantic Semantic)
{
	switch (Semantic)
	{
	case ESimCoreStaticColliderSemantic::Wall:
		return TEXT("wall");
	case ESimCoreStaticColliderSemantic::Curb:
		return TEXT("curb");
	case ESimCoreStaticColliderSemantic::Barrier:
		return TEXT("barrier");
	}
	return nullptr;
}

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X)
		&& FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

#if WITH_EDITOR
struct FGroundActorSamplingFit
{
	FVector WorldCenter = FVector::ZeroVector;
	FQuat YawRotation = FQuat::Identity;
	double YawDegrees = 0.0;
	FVector2D HorizontalHalfExtentCm = FVector2D(100.0, 100.0);
	double VerticalHalfExtentCm = 100.0;
};

bool CalculateGroundActorSamplingFit(
	const AActor* GroundActor,
	double PaddingCm,
	FGroundActorSamplingFit& OutFit,
	FString& OutError)
{
	OutError.Reset();
	if (!IsValid(GroundActor))
	{
		OutError = TEXT("assign Ground Actor first");
		return false;
	}

	// Actor-local component bounds are transformed explicitly rather than using
	// a world AABB. Aligning the sampler to the actor's yaw avoids an oversized
	// box for rotated ground while retaining pitched, rolled, scaled, and
	// component-offset colliding geometry.
	const FBox GroundLocalBounds =
		GroundActor->CalculateComponentsBoundingBoxInLocalSpace(false, true);
	if (!GroundLocalBounds.IsValid
		|| !IsFiniteVector(GroundLocalBounds.Min)
		|| !IsFiniteVector(GroundLocalBounds.Max))
	{
		OutError = TEXT("Ground Actor has no valid colliding component bounds");
		return false;
	}

	const FTransform GroundTransform = GroundActor->GetActorTransform();
	OutFit.YawDegrees =
		FRotator::NormalizeAxis(GroundActor->GetActorRotation().Yaw);
	OutFit.YawRotation = FQuat(FRotator(0.0, OutFit.YawDegrees, 0.0));
	FVector SamplingMinimum(
		TNumericLimits<double>::Max(),
		TNumericLimits<double>::Max(),
		TNumericLimits<double>::Max());
	FVector SamplingMaximum(
		TNumericLimits<double>::Lowest(),
		TNumericLimits<double>::Lowest(),
		TNumericLimits<double>::Lowest());

	for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
	{
		const FVector GroundLocalCorner(
			(CornerIndex & 1) != 0 ? GroundLocalBounds.Max.X : GroundLocalBounds.Min.X,
			(CornerIndex & 2) != 0 ? GroundLocalBounds.Max.Y : GroundLocalBounds.Min.Y,
			(CornerIndex & 4) != 0 ? GroundLocalBounds.Max.Z : GroundLocalBounds.Min.Z);
		const FVector WorldCorner =
			GroundTransform.TransformPosition(GroundLocalCorner);
		if (!IsFiniteVector(WorldCorner))
		{
			OutError = TEXT("Ground Actor transform produced a non-finite world bound");
			return false;
		}

		const FVector SamplingCorner =
			OutFit.YawRotation.UnrotateVector(WorldCorner);
		SamplingMinimum.X = FMath::Min(SamplingMinimum.X, SamplingCorner.X);
		SamplingMinimum.Y = FMath::Min(SamplingMinimum.Y, SamplingCorner.Y);
		SamplingMinimum.Z = FMath::Min(SamplingMinimum.Z, SamplingCorner.Z);
		SamplingMaximum.X = FMath::Max(SamplingMaximum.X, SamplingCorner.X);
		SamplingMaximum.Y = FMath::Max(SamplingMaximum.Y, SamplingCorner.Y);
		SamplingMaximum.Z = FMath::Max(SamplingMaximum.Z, SamplingCorner.Z);
	}

	const FVector SamplingCenter =
		(SamplingMinimum + SamplingMaximum) * 0.5;
	const FVector SamplingHalfExtent =
		(SamplingMaximum - SamplingMinimum) * 0.5;
	if (!IsFiniteVector(SamplingCenter)
		|| !IsFiniteVector(SamplingHalfExtent))
	{
		OutError = TEXT("computed sampling bounds are not finite");
		return false;
	}

	OutFit.WorldCenter = OutFit.YawRotation.RotateVector(SamplingCenter);
	// A yaw-only transform leaves Z unchanged, but assigning it explicitly keeps
	// the vertical contract clear if the projection above changes in the future.
	OutFit.WorldCenter.Z = SamplingCenter.Z;
	const double SafePadding = FMath::Max(PaddingCm, 0.0);
	OutFit.HorizontalHalfExtentCm = FVector2D(
		FMath::Max(SamplingHalfExtent.X + SafePadding, 100.0),
		FMath::Max(SamplingHalfExtent.Y + SafePadding, 100.0));
	OutFit.VerticalHalfExtentCm =
		FMath::Max(SamplingHalfExtent.Z + SafePadding, 100.0);
	return true;
}

bool SamplingVolumeContainsGroundFit(
	const FGroundActorSamplingFit& RequiredFit,
	const FVector& CurrentCenter,
	const FQuat& CurrentYawRotation,
	double CurrentExtentX,
	double CurrentExtentY,
	double CurrentTraceAbove,
	double CurrentTraceBelow)
{
	constexpr double ContainmentToleranceCm = 0.1;
	for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
	{
		const FVector RequiredLocalCorner(
			(CornerIndex & 1) != 0
				? RequiredFit.HorizontalHalfExtentCm.X
				: -RequiredFit.HorizontalHalfExtentCm.X,
			(CornerIndex & 2) != 0
				? RequiredFit.HorizontalHalfExtentCm.Y
				: -RequiredFit.HorizontalHalfExtentCm.Y,
			(CornerIndex & 4) != 0
				? RequiredFit.VerticalHalfExtentCm
				: -RequiredFit.VerticalHalfExtentCm);
		const FVector RequiredWorldCorner =
			RequiredFit.WorldCenter
			+ RequiredFit.YawRotation.RotateVector(RequiredLocalCorner);
		const FVector CurrentLocalCorner =
			CurrentYawRotation.UnrotateVector(
				RequiredWorldCorner - CurrentCenter);
		if (FMath::Abs(CurrentLocalCorner.X)
				> CurrentExtentX + ContainmentToleranceCm
			|| FMath::Abs(CurrentLocalCorner.Y)
				> CurrentExtentY + ContainmentToleranceCm
			|| CurrentLocalCorner.Z
				> CurrentTraceAbove + ContainmentToleranceCm
			|| CurrentLocalCorner.Z
				< -CurrentTraceBelow - ContainmentToleranceCm)
		{
			return false;
		}
	}
	return true;
}

#endif

bool BuildStaticColliderCsv(
	UWorld* World,
	const FVector& MapOriginWorldCm,
	FString& OutCsv,
	int32& OutColliderCount,
	FString& OutError)
{
	OutCsv.Reset();
	OutColliderCount = 0;
	OutError.Reset();

	TArray<FStaticColliderCsvRecord> Records;
	TSet<FString> ColliderIds;
	for (TActorIterator<ASimCoreStaticCollider> It(World); It; ++It)
	{
		ASimCoreStaticCollider* Marker = *It;
		if (!IsValid(Marker) || !Marker->bExportEnabled)
		{
			continue;
		}
		if (Records.Num() >= MaxExportedStaticColliderCount)
		{
			OutError = FString::Printf(
				TEXT("Static collision export exceeds the %d collider limit"),
				MaxExportedStaticColliderCount);
			return false;
		}

		const FString& ColliderId = Marker->ColliderId;
		if (!IsValidColliderId(ColliderId))
		{
			OutError = FString::Printf(
				TEXT("Static collider '%s' requires a unique Collider Id of at most 128 ASCII letters, digits, '_', '-', or '.'"),
				*Marker->GetActorNameOrLabel());
			return false;
		}
		if (ColliderIds.Contains(ColliderId))
		{
			OutError = FString::Printf(
				TEXT("Duplicate static Collider Id: %s"),
				*ColliderId);
			return false;
		}
		ColliderIds.Add(ColliderId);

		const UBoxComponent* Bounds = Marker->CollisionBounds;
		if (Bounds == nullptr || !Bounds->IsRegistered())
		{
			OutError = FString::Printf(
				TEXT("Static collider '%s' has no registered bounds component"),
				*ColliderId);
			return false;
		}

		const FTransform BoundsTransform = Bounds->GetComponentTransform();
		const FVector CenterWorldCm = BoundsTransform.GetLocation();
		const FVector ComponentScale = BoundsTransform.GetScale3D();
		const FVector UnscaledExtent = Bounds->GetUnscaledBoxExtent();
		const FVector ExtentCm(
			FMath::Abs(UnscaledExtent.X * ComponentScale.X),
			FMath::Abs(UnscaledExtent.Y * ComponentScale.Y),
			FMath::Abs(UnscaledExtent.Z * ComponentScale.Z));
		const FRotator Rotation = BoundsTransform.Rotator();
		const double PitchDegrees = FRotator::NormalizeAxis(Rotation.Pitch);
		const double RollDegrees = FRotator::NormalizeAxis(Rotation.Roll);
		if (!IsFiniteVector(CenterWorldCm)
			|| !IsFiniteVector(ExtentCm)
			|| !FMath::IsFinite(Rotation.Yaw)
			|| !FMath::IsFinite(Marker->Friction)
			|| !FMath::IsFinite(Marker->Restitution))
		{
			OutError = FString::Printf(
				TEXT("Static collider '%s' contains a non-finite transform or material"),
				*ColliderId);
			return false;
		}
		if (FMath::Abs(PitchDegrees) > 0.01
			|| FMath::Abs(RollDegrees) > 0.01)
		{
			OutError = FString::Printf(
				TEXT("Static collider '%s' must remain upright; OBB prisms support yaw only"),
				*ColliderId);
			return false;
		}
		if (ExtentCm.X < KINDA_SMALL_NUMBER
			|| ExtentCm.Y < KINDA_SMALL_NUMBER
			|| ExtentCm.Z < KINDA_SMALL_NUMBER)
		{
			OutError = FString::Printf(
				TEXT("Static collider '%s' requires positive box half extents"),
				*ColliderId);
			return false;
		}
		if (Marker->Friction < 0.0f
			|| Marker->Restitution < 0.0f
			|| Marker->Restitution > 1.0f)
		{
			OutError = FString::Printf(
				TEXT("Static collider '%s' requires friction >= 0 and restitution in [0, 1]"),
				*ColliderId);
			return false;
		}

		const TCHAR* Semantic = StaticColliderSemanticText(Marker->Semantic);
		if (Semantic == nullptr)
		{
			OutError = FString::Printf(
				TEXT("Static collider '%s' has an unsupported semantic"),
				*ColliderId);
			return false;
		}
		double HeadingDegrees = FRotator::NormalizeAxis(Rotation.Yaw);
		if (HeadingDegrees < 0.0)
		{
			HeadingDegrees += 360.0;
		}
		const double HeadingRadians = FMath::DegreesToRadians(HeadingDegrees);
		const FVector RelativeCenter = CenterWorldCm - MapOriginWorldCm;

		FStaticColliderCsvRecord& Record = Records.AddDefaulted_GetRef();
		Record.ColliderId = ColliderId;
		Record.Row = FString::Printf(
			TEXT("%s,%s,obb,%.6f,%.6f,%.6f,%.9f,%.6f,%.6f,%.6f,%.6f,%.6f\n"),
			*ColliderId,
			Semantic,
			RelativeCenter.Y / 100.0,
			RelativeCenter.X / 100.0,
			RelativeCenter.Z / 100.0,
			HeadingRadians,
			ExtentCm.X / 100.0,
			ExtentCm.Y / 100.0,
			ExtentCm.Z / 100.0,
			static_cast<double>(Marker->Friction),
			static_cast<double>(Marker->Restitution));
	}

	Records.Sort([](
		const FStaticColliderCsvRecord& Left,
		const FStaticColliderCsvRecord& Right)
	{
		return Left.ColliderId.Compare(
			Right.ColliderId,
			ESearchCase::CaseSensitive) < 0;
	});

	OutCsv += TEXT("# Generated by SimCore Ground Collision Exporter.\n");
	OutCsv += TEXT("# Explicit editor-only OBB markers; North=0 and heading is clockwise-positive.\n");
	OutCsv += TEXT("# coordinate_mapping=east:UE_Y/100,north:UE_X/100,up:UE_Z/100\n");
	OutCsv += TEXT("collider_id,semantic,shape,center_e_m,center_n_m,center_u_m,heading_rad,half_length_m,half_width_m,half_height_m,friction,restitution\n");
	for (const FStaticColliderCsvRecord& Record : Records)
	{
		OutCsv += Record.Row;
	}
	OutColliderCount = Records.Num();
	return true;
}

bool ContainsUnsupportedSurfaceIdCharacter(const FString& Value)
{
	return Value.IsEmpty()
		|| Value.Contains(TEXT(","))
		|| Value.Contains(TEXT("#"))
		|| Value.Contains(TEXT("\r"))
		|| Value.Contains(TEXT("\n"));
}

bool ContainsReparsePointBelowRoot(
	const FString& RootDirectory,
	const FString& DescendantDirectory)
{
#if PLATFORM_WINDOWS
	FString RelativeDirectory = DescendantDirectory;
	const FString RootWithSeparator = RootDirectory + TEXT("/");
	if (!FPaths::MakePathRelativeTo(RelativeDirectory, *RootWithSeparator))
	{
		return true;
	}
	FPaths::NormalizeFilename(RelativeDirectory);
	TArray<FString> Components;
	RelativeDirectory.ParseIntoArray(Components, TEXT("/"), true);
	FString CurrentDirectory = RootDirectory;
	for (const FString& Component : Components)
	{
		CurrentDirectory = FPaths::Combine(CurrentDirectory, Component);
		const DWORD Attributes = ::GetFileAttributesW(*CurrentDirectory);
		if (Attributes != INVALID_FILE_ATTRIBUTES
			&& (Attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			return true;
		}
	}
#endif
	return false;
}

void ShowExportError(const FString& Message, bool bShowDialog = true)
{
	UE_LOG(LogSimCoreGroundExporter, Error, TEXT("%s"), *Message);
#if WITH_EDITOR
	if (bShowDialog && !IsRunningCommandlet() && !FApp::IsUnattended())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
	}
#endif
}
}

bool SimCoreStaticCollisionSnapshot::BuildCsv(
	UWorld* World,
	const FVector& MapOriginWorldCm,
	FString& OutCsv,
	int32& OutColliderCount,
	FString& OutError)
{
	return BuildStaticColliderCsv(
		World, MapOriginWorldCm, OutCsv, OutColliderCount, OutError);
}

AGroundCollisionExporter::AGroundCollisionExporter()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SamplingBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("SamplingBounds"));
	SamplingBounds->SetupAttachment(SceneRoot);
	SamplingBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SamplingBounds->SetGenerateOverlapEvents(false);
	SamplingBounds->SetCanEverAffectNavigation(false);
	SamplingBounds->SetHiddenInGame(true);
	SamplingBounds->ShapeColor = FColor(30, 200, 255);
	UpdateBoundsVisualization();
}

void AGroundCollisionExporter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateBoundsVisualization();
}

#if WITH_EDITOR
void AGroundCollisionExporter::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateBoundsVisualization();
}
#endif

void AGroundCollisionExporter::UpdateBoundsVisualization()
{
	if (SamplingBounds == nullptr)
	{
		return;
	}

	const double ExtentX = FMath::Max(HorizontalExtentCm.X, 100.0);
	const double ExtentY = FMath::Max(HorizontalExtentCm.Y, 100.0);
	const double Above = FMath::Max(static_cast<double>(TraceAboveCm), 100.0);
	const double Below = FMath::Max(static_cast<double>(TraceBelowCm), 100.0);
	SamplingBounds->SetBoxExtent(FVector(ExtentX, ExtentY, (Above + Below) * 0.5));
	SamplingBounds->SetRelativeLocation(FVector(0.0, 0.0, (Above - Below) * 0.5));
}

void AGroundCollisionExporter::CenterSamplingOnMapOrigin()
{
	FVector NewLocation = GetActorLocation();
	NewLocation.X = MapOriginWorldCm.X;
	NewLocation.Y = MapOriginWorldCm.Y;
	SetActorLocation(NewLocation);
	UE_LOG(LogSimCoreGroundExporter, Display,
		TEXT("Centered sampling bounds on map origin XY: %.3f, %.3f cm"),
		MapOriginWorldCm.X,
		MapOriginWorldCm.Y);
}

void AGroundCollisionExporter::FitSamplingBoundsToGroundActor()
{
#if !WITH_EDITOR
	UE_LOG(LogSimCoreGroundExporter, Warning,
		TEXT("Ground collision bounds fitting is available only in the Unreal Editor"));
#else
	FString Result;
	if (!FitSamplingBoundsToGroundActorInternal(true, Result))
	{
		ShowExportError(FString::Printf(
			TEXT("Ground bounds fit failed: %s."),
			*Result));
	}
#endif
}

#if WITH_EDITOR
bool AGroundCollisionExporter::FitSamplingBoundsToGroundActorInternal(
	bool bShowResultDialog,
	FString& OutResult)
{
	if (GroundActor == this)
	{
		OutResult = TEXT("Ground Actor cannot be the exporter itself");
		return false;
	}

	FGroundActorSamplingFit RequiredFit;
	if (!CalculateGroundActorSamplingFit(
		GroundActor,
		static_cast<double>(GroundBoundsPaddingCm),
		RequiredFit,
		OutResult))
	{
		return false;
	}

	Modify();
	HorizontalExtentCm = RequiredFit.HorizontalHalfExtentCm;
	// Keep a deliberately larger author-provided trace corridor, but expand it
	// when necessary so the fitted volume contains the complete actor.
	TraceAboveCm = static_cast<float>(FMath::Max(
		static_cast<double>(TraceAboveCm),
		RequiredFit.VerticalHalfExtentCm));
	TraceBelowCm = static_cast<float>(FMath::Max(
		static_cast<double>(TraceBelowCm),
		RequiredFit.VerticalHalfExtentCm));
	SetActorLocationAndRotation(
		RequiredFit.WorldCenter,
		FRotator(0.0, RequiredFit.YawDegrees, 0.0));
	UpdateBoundsVisualization();
	MarkPackageDirty();

	const double Padding = FMath::Max(
		static_cast<double>(GroundBoundsPaddingCm),
		0.0);
	OutResult = FString::Printf(
		TEXT("Sampling bounds fitted to Ground Actor '%s'.\nCenter: %.1f, %.1f, %.1f cm\nYaw: %.2f deg\nHorizontal half extent: %.1f x %.1f cm\nSafety padding: %.1f cm"),
		*GroundActor->GetActorNameOrLabel(),
		RequiredFit.WorldCenter.X,
		RequiredFit.WorldCenter.Y,
		RequiredFit.WorldCenter.Z,
		RequiredFit.YawDegrees,
		HorizontalExtentCm.X,
		HorizontalExtentCm.Y,
		Padding);
	UE_LOG(LogSimCoreGroundExporter, Display, TEXT("%s"), *OutResult);
	if (bShowResultDialog && !IsRunningCommandlet() && !FApp::IsUnattended())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(OutResult));
	}
	return true;
}
#endif

void AGroundCollisionExporter::ConfigureForAuthoring(
	AActor* InGroundActor, const FString& InMapPackageDirectory,
	const FVector& InMapOriginWorldCm, const FVector& InSamplingCenterWorldCm,
	const FVector2D& InHorizontalExtentCm, float InSampleSpacingCm,
	float InGroundBoundsPaddingCm)
{
	GroundActor = InGroundActor;
	MapPackageDirectory = InMapPackageDirectory;
	MapOriginWorldCm = InMapOriginWorldCm;
	HorizontalExtentCm = InHorizontalExtentCm;
	HeightfieldSampleSpacingCm = InSampleSpacingCm;
	GroundBoundsPaddingCm = InGroundBoundsPaddingCm;
	SetActorLocationAndRotation(InSamplingCenterWorldCm, FRotator::ZeroRotator);
	UpdateBoundsVisualization();
}

bool AGroundCollisionExporter::ExportGroundSurfaceUnattended()
{
	TGuardValue<bool> SuppressDialogs(bSuppressExportDialogs, true);
	ExportGroundSurface();
	return bLastExportSucceeded;
}

void AGroundCollisionExporter::ExportGroundSurface()
{
	bLastExportSucceeded = false;
	const auto ShowExportError = [this](const FString& Message)
	{
		::ShowExportError(Message, !bSuppressExportDialogs);
	};
#if !WITH_EDITOR
	UE_LOG(LogSimCoreGroundExporter, Warning,
		TEXT("Ground collision export is available only in the Unreal Editor"));
	return;
#else
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		ShowExportError(TEXT("Ground export failed: the exporter has no editor world."));
		return;
	}
	if (GroundActor == this)
	{
		ShowExportError(TEXT("Ground export failed: Ground Actor cannot be the exporter itself."));
		return;
	}

	const FString SurfaceName = SurfaceId.ToString();
	if (ContainsUnsupportedSurfaceIdCharacter(SurfaceName))
	{
		ShowExportError(TEXT("Ground export failed: Surface Id cannot be empty or contain comma, #, or a newline."));
		return;
	}

	bool bGroundActorBoundsAutoFitted = false;
	if (GroundActor != nullptr)
	{
		if (!IsValid(GroundActor))
		{
			ShowExportError(TEXT("Ground export preflight failed: the assigned Ground Actor is no longer valid. Reassign it before baking."));
			return;
		}

		FGroundActorSamplingFit RequiredFit;
		FString FitError;
		if (!CalculateGroundActorSamplingFit(
			GroundActor,
			static_cast<double>(GroundBoundsPaddingCm),
			RequiredFit,
			FitError))
		{
			ShowExportError(FString::Printf(
				TEXT("Ground export preflight failed: %s."),
				*FitError));
			return;
		}

		const double CurrentExtentX =
			FMath::Max(HorizontalExtentCm.X, 100.0);
		const double CurrentExtentY =
			FMath::Max(HorizontalExtentCm.Y, 100.0);
		const double CurrentTraceAbove = FMath::Max(
			static_cast<double>(TraceAboveCm),
			100.0);
		const double CurrentTraceBelow = FMath::Max(
			static_cast<double>(TraceBelowCm),
			100.0);
		const FQuat CurrentYawRotation(
			FRotator(0.0, GetActorRotation().Yaw, 0.0));
		if (!SamplingVolumeContainsGroundFit(
			RequiredFit,
			GetActorLocation(),
			CurrentYawRotation,
			CurrentExtentX,
			CurrentExtentY,
			CurrentTraceAbove,
			CurrentTraceBelow))
		{
			FString AutoFitResult;
			if (!FitSamplingBoundsToGroundActorInternal(
				false,
				AutoFitResult))
			{
				ShowExportError(FString::Printf(
					TEXT("Ground export preflight could not fit the complete Ground Actor bounds: %s."),
					*AutoFitResult));
				return;
			}
			bGroundActorBoundsAutoFitted = true;
			UE_LOG(
				LogSimCoreGroundExporter,
				Warning,
				TEXT("Bake preflight automatically expanded the sampling volume because the previous bounds clipped Ground Actor '%s'. New horizontal half extent: %.1f x %.1f cm."),
				*GroundActor->GetActorNameOrLabel(),
				HorizontalExtentCm.X,
				HorizontalExtentCm.Y);
		}
	}

	const double ExtentX = FMath::Max(HorizontalExtentCm.X, 100.0);
	const double ExtentY = FMath::Max(HorizontalExtentCm.Y, 100.0);
	const double Spacing = FMath::Max(
		static_cast<double>(HeightfieldSampleSpacingCm),
		25.0);
	// A named Landscape (or other explicitly selected ground actor) is a single
	// authored surface. Applying a raw corner-height cutoff to it turns a valid
	// 36-45 degree grade into a hole whose exact threshold depends on grid
	// direction. Retain the cutoff only for unfiltered WorldStatic sampling,
	// where it still prevents unrelated floors and walls from being bridged.
	const bool bApplyHeightDiscontinuityFilter = GroundActor == nullptr;
	const FVector Center = GetActorLocation();
	const FQuat YawRotation(FRotator(0.0, GetActorRotation().Yaw, 0.0));
	const FVector MapOriginLocalOffset =
		YawRotation.UnrotateVector(MapOriginWorldCm - Center);
	if (FMath::Abs(MapOriginLocalOffset.X) > ExtentX
		|| FMath::Abs(MapOriginLocalOffset.Y) > ExtentY)
	{
		ShowExportError(FString::Printf(
			TEXT("Ground export rejected: Map Origin World Cm (%.1f, %.1f) is outside the sampling box centered at (%.1f, %.1f). The vehicle spawns at ENU (0, 0), so press 'Center Sampling On Map Origin' or expand Horizontal Extent before baking."),
			MapOriginWorldCm.X,
			MapOriginWorldCm.Y,
			Center.X,
			Center.Y));
		return;
	}
	const double StartZ = Center.Z + FMath::Max(static_cast<double>(TraceAboveCm), 100.0);
	const double EndZ = Center.Z - FMath::Max(static_cast<double>(TraceBelowCm), 100.0);

	SimCoreGroundSnapshot::FBuildRequest SnapshotRequest;
	SnapshotRequest.World = World;
	SnapshotRequest.IgnoredActor = this;
	SnapshotRequest.GroundActor = GroundActor;
	SnapshotRequest.SamplingCenterWorldCm = Center;
	SnapshotRequest.SamplingYaw = YawRotation;
	SnapshotRequest.MapOriginWorldCm = MapOriginWorldCm;
	SnapshotRequest.HorizontalExtentXCm = ExtentX;
	SnapshotRequest.HorizontalExtentYCm = ExtentY;
	SnapshotRequest.SampleSpacingCm = Spacing;
	SnapshotRequest.TraceStartZCm = StartZ;
	SnapshotRequest.TraceEndZCm = EndZ;
	SnapshotRequest.MinimumGroundNormalZ = MinimumGroundNormalZ;
	SnapshotRequest.MaximumCellHeightDeltaCm = MaxCellHeightDeltaCm;
	SnapshotRequest.AsphaltPhysicalSurface =
		static_cast<uint8>(AsphaltPhysicalSurface.GetValue());
	SnapshotRequest.LowFrictionPhysicalSurface =
		static_cast<uint8>(LowFrictionPhysicalSurface.GetValue());
	SnapshotRequest.RoughPhysicalSurface =
		static_cast<uint8>(RoughPhysicalSurface.GetValue());
	SnapshotRequest.DefaultFrictionMultiplier = DefaultFrictionMultiplier;
	SnapshotRequest.AsphaltFrictionMultiplier = AsphaltFrictionMultiplier;
	SnapshotRequest.LowFrictionFrictionMultiplier =
		LowFrictionFrictionMultiplier;
	SnapshotRequest.RoughFrictionMultiplier = RoughFrictionMultiplier;
	SnapshotRequest.MaximumSampleCount = MaxHeightfieldSampleCount;
	SnapshotRequest.bApplyHeightDiscontinuityFilter =
		bApplyHeightDiscontinuityFilter;

	SimCoreGroundSnapshot::FBuildResult Snapshot;
	FString SnapshotError;
	if (!SimCoreGroundSnapshot::Build(
		SnapshotRequest,
		Snapshot,
		SnapshotError))
	{
		ShowExportError(FString::Printf(
			TEXT("Ground heightfield export rejected: %s."),
			*SnapshotError));
		return;
	}

	// Manifest v1 still requires ground_surface.csv. Keep the strict legacy
	// header as a row-free sentinel while ground_heightfield.bin owns terrain.
	FString GroundSurfaceSentinel;
	GroundSurfaceSentinel += TEXT("# Generated by SimCore Ground Collision Exporter.\n");
	GroundSurfaceSentinel += TEXT("# terrain_payload=ground_heightfield.bin format=SIMGHF2\n");
	GroundSurfaceSentinel += FString::Printf(
		TEXT("# level=%s source_actor=%s spacing_cm=%.3f\n"),
		*World->GetMapName(),
		GroundActor != nullptr
			? *GroundActor->GetName()
			: TEXT("any_world_static"),
		Spacing);
	GroundSurfaceSentinel += TEXT("surface_id,e0,n0,u0,e1,n1,u1,e2,n2,u2\n");

	FString StaticCollisionCsv;
	FString StaticCollisionError;
	int32 ExportedStaticColliderCount = 0;
	if (!SimCoreStaticCollisionSnapshot::BuildCsv(
		World,
		MapOriginWorldCm,
		StaticCollisionCsv,
		ExportedStaticColliderCount,
		StaticCollisionError))
	{
		ShowExportError(FString::Printf(
			TEXT("Static collision export rejected: %s"),
			*StaticCollisionError));
		return;
	}

	FString MapPackagesRoot = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir(),
		TEXT("../../map_packages"));
	FPaths::NormalizeDirectoryName(MapPackagesRoot);
	FString ResolvedPackageDirectory = FPaths::IsRelative(MapPackageDirectory)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), MapPackageDirectory)
		: FPaths::ConvertRelativePathToFull(MapPackageDirectory);
	FPaths::NormalizeDirectoryName(ResolvedPackageDirectory);
	const FString AllowedPrefix = MapPackagesRoot + TEXT("/");
	if (!ResolvedPackageDirectory.StartsWith(AllowedPrefix, ESearchCase::IgnoreCase))
	{
		ShowExportError(FString::Printf(
			TEXT("Ground export rejected: output must be a package below %s"),
			*MapPackagesRoot));
		return;
	}
	if (ContainsReparsePointBelowRoot(MapPackagesRoot, ResolvedPackageDirectory))
	{
		ShowExportError(FString::Printf(
			TEXT("Ground export rejected: output path cannot traverse a junction, symlink, or other reparse point below %s"),
			*MapPackagesRoot));
		return;
	}
	if (!IFileManager::Get().MakeDirectory(*ResolvedPackageDirectory, true))
	{
		ShowExportError(FString::Printf(
			TEXT("Ground export failed to create directory: %s"),
			*ResolvedPackageDirectory));
		return;
	}

	const FString GroundSurfaceOutputPath = FPaths::Combine(
		ResolvedPackageDirectory,
		TEXT("ground_surface.csv"));
	const FString GroundSurfaceTemporaryPath =
		GroundSurfaceOutputPath + TEXT(".tmp");
	const FString HeightfieldOutputPath = FPaths::Combine(
		ResolvedPackageDirectory,
		TEXT("ground_heightfield.bin"));
	const FString HeightfieldTemporaryPath =
		HeightfieldOutputPath + TEXT(".tmp");
	const FString StaticCollisionOutputPath = FPaths::Combine(
		ResolvedPackageDirectory,
		TEXT("static_colliders.csv"));
	const FString StaticCollisionTemporaryPath =
		StaticCollisionOutputPath + TEXT(".tmp");
	const auto DeleteTemporaryFiles = [&]()
	{
		IFileManager::Get().Delete(
			*GroundSurfaceTemporaryPath,
			false,
			true);
		IFileManager::Get().Delete(
			*HeightfieldTemporaryPath,
			false,
			true);
		IFileManager::Get().Delete(
			*StaticCollisionTemporaryPath,
			false,
			true);
	};
	if (!FFileHelper::SaveStringToFile(
		GroundSurfaceSentinel,
		*GroundSurfaceTemporaryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		DeleteTemporaryFiles();
		ShowExportError(FString::Printf(
			TEXT("Ground sentinel export failed to write temporary file: %s"),
			*GroundSurfaceTemporaryPath));
		return;
	}
	if (!FFileHelper::SaveArrayToFile(
		Snapshot.Binary,
		*HeightfieldTemporaryPath))
	{
		DeleteTemporaryFiles();
		ShowExportError(FString::Printf(
			TEXT("Ground heightfield export failed to write temporary file: %s"),
			*HeightfieldTemporaryPath));
		return;
	}
	if (!FFileHelper::SaveStringToFile(
		StaticCollisionCsv,
		*StaticCollisionTemporaryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		DeleteTemporaryFiles();
		ShowExportError(FString::Printf(
			TEXT("Static collision export failed to write temporary file: %s"),
			*StaticCollisionTemporaryPath));
		return;
	}

	// Stage all payloads before replacing any one. manifest.cfg remains the
	// final commit marker, so a failure between payload moves leaves the old
	// identity mismatched and both runtimes fail closed instead of accepting a
	// mixed ground/static snapshot. Move the new binary first: an old manifest
	// does not name it and therefore remains valid until its declared sentinel is
	// replaced.
	if (!IFileManager::Get().Move(
		*HeightfieldOutputPath,
		*HeightfieldTemporaryPath,
		true,
		true))
	{
		DeleteTemporaryFiles();
		ShowExportError(FString::Printf(
			TEXT("Ground heightfield export failed to replace: %s"),
			*HeightfieldOutputPath));
		return;
	}
	if (!IFileManager::Get().Move(
		*GroundSurfaceOutputPath,
		*GroundSurfaceTemporaryPath,
		true,
		true))
	{
		DeleteTemporaryFiles();
		ShowExportError(FString::Printf(
			TEXT("Ground heightfield was replaced, but sentinel commit failed. Bake again before using this intentionally fail-closed package.\n\n%s"),
			*GroundSurfaceOutputPath));
		return;
	}
	if (!IFileManager::Get().Move(
		*StaticCollisionOutputPath,
		*StaticCollisionTemporaryPath,
		true,
		true))
	{
		DeleteTemporaryFiles();
		ShowExportError(FString::Printf(
			TEXT("Ground heightfield and sentinel were replaced, but static collision commit failed. Bake again before using this intentionally fail-closed package.\n\n%s"),
			*StaticCollisionOutputPath));
		return;
	}

	// The collision payloads are committed first and manifest.cfg is committed
	// last. If export is interrupted between those operations, the old manifest
	// cannot validate the new payload and both runtimes fail closed.
	const TArray<FString> CollisionFiles{
		TEXT("ground_surface.csv"),
		TEXT("ground_heightfield.bin"),
		TEXT("static_colliders.csv")};
	FString CollisionChecksum;
	FString ManifestError;
	if (!SimCoreMapPackage::WriteManifestLast(
		ResolvedPackageDirectory,
		FPaths::GetCleanFilename(ResolvedPackageDirectory),
		CollisionFiles,
		CollisionChecksum,
		ManifestError))
	{
		ShowExportError(FString::Printf(
			TEXT("Collision payloads were replaced, but manifest commit failed. The package is intentionally unusable until it is baked again.\n\n%s"),
			*ManifestError));
		return;
	}

	const FString Result = FString::Printf(
		TEXT("Exported Unreal collision heightfield: %d x %d samples (%d/%lld valid), %d drivable cells.\nBinary size: %.2f MiB\nSurface cells: default=%d, asphalt=%d, low-friction=%d, rough=%d\nExported ENU bounds: E [%.2f, %.2f] m, N [%.2f, %.2f] m, U [%.2f, %.2f] m\nGround Actor bounds coverage: OK (%s)\nGround sample step: %.2f x %.2f cm (configured resolution; never auto-raised)\nExported %d explicit static OBB colliders.\nMap origin coverage: OK\nHeight discontinuity filter: %s\nSkipped cells: missing=%d, discontinuous=%d\nCollision identity: %s\n\n%s\n%s\n%s\nPackage directory: %s\n\nA running SimCore process will verify this manifest and apply it at a fixed-tick boundary. Do not restart the server; PIE briefly reconnects and sends a fresh automatic reset."),
		Snapshot.Columns,
		Snapshot.Rows,
		Snapshot.ValidSampleCount,
		static_cast<long long>(Snapshot.Columns) * Snapshot.Rows,
		Snapshot.DrivableCellCount,
		static_cast<double>(Snapshot.Binary.Num()) / (1024.0 * 1024.0),
		Snapshot.DefaultMaterialCellCount,
		Snapshot.AsphaltMaterialCellCount,
		Snapshot.LowFrictionMaterialCellCount,
		Snapshot.RoughMaterialCellCount,
		Snapshot.MinimumEnuM.X,
		Snapshot.MaximumEnuM.X,
		Snapshot.MinimumEnuM.Y,
		Snapshot.MaximumEnuM.Y,
		Snapshot.MinimumEnuM.Z,
		Snapshot.MaximumEnuM.Z,
		GroundActor == nullptr
			? TEXT("no explicit Ground Actor")
			: (bGroundActorBoundsAutoFitted
				? TEXT("auto-fitted before bake")
				: TEXT("sampling volume already contained full actor")),
		Snapshot.StepXCm,
		Snapshot.StepYCm,
		ExportedStaticColliderCount,
		bApplyHeightDiscontinuityFilter ? TEXT("enabled") : TEXT("disabled for explicit Ground Actor"),
		Snapshot.SkippedMissingCellCount,
		Snapshot.SkippedDiscontinuousCellCount,
		*CollisionChecksum,
		*GroundSurfaceOutputPath,
		*HeightfieldOutputPath,
		*StaticCollisionOutputPath,
		*ResolvedPackageDirectory);
	UE_LOG(LogSimCoreGroundExporter, Display, TEXT("%s"), *Result);
	bLastExportSucceeded = true;
	if (!bSuppressExportDialogs && !IsRunningCommandlet() && !FApp::IsUnattended())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result));
	}
#endif
}
