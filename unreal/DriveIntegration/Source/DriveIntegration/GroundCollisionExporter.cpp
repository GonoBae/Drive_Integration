#include "GroundCollisionExporter.h"

#include "SimCoreMapPackage.h"
#include "SimCoreStaticCollider.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSimCoreGroundExporter, Log, All);

namespace
{
// Bound editor bake size and package churn. The server's adaptive grid index
// keeps wheel queries local, but the R1 package still intentionally targets a
// compact development corridor rather than an unbounded world export.
constexpr int64 MaxExportedTriangleCount = 20000;
constexpr int32 MaxExportedStaticColliderCount = 4096;

struct FGroundCollisionSample
{
	FVector ImpactPoint = FVector::ZeroVector;
	bool bValid = false;
};

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

bool MatchesGroundActor(const FHitResult& Hit, const AActor* GroundActor)
{
	if (GroundActor == nullptr)
	{
		return true;
	}

	const AActor* HitActor = Hit.GetActor();
	return HitActor == GroundActor
		|| (HitActor != nullptr && HitActor->IsOwnedBy(GroundActor))
		|| (HitActor != nullptr && GroundActor->IsOwnedBy(HitActor));
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

void ShowExportError(const FString& Message)
{
	UE_LOG(LogSimCoreGroundExporter, Error, TEXT("%s"), *Message);
#if WITH_EDITOR
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
#endif
}
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
	return;
#else
	if (!IsValid(GroundActor))
	{
		ShowExportError(TEXT("Ground bounds fit failed: assign Ground Actor first."));
		return;
	}
	if (GroundActor == this)
	{
		ShowExportError(TEXT("Ground bounds fit failed: Ground Actor cannot be the exporter itself."));
		return;
	}

	// Actor-local component bounds are transformed explicitly rather than using
	// a world AABB. Aligning the sampler to the Ground Actor's yaw therefore
	// avoids the unnecessarily large box produced for a rotated Landscape while
	// still containing pitched, rolled, scaled, or component-offset geometry.
	const FBox GroundLocalBounds =
		GroundActor->CalculateComponentsBoundingBoxInLocalSpace(false, true);
	if (!GroundLocalBounds.IsValid
		|| !IsFiniteVector(GroundLocalBounds.Min)
		|| !IsFiniteVector(GroundLocalBounds.Max))
	{
		ShowExportError(TEXT("Ground bounds fit failed: Ground Actor has no valid colliding component bounds."));
		return;
	}

	const FTransform GroundTransform = GroundActor->GetActorTransform();
	const double SamplingYawDegrees =
		FRotator::NormalizeAxis(GroundActor->GetActorRotation().Yaw);
	const FQuat SamplingYaw(FRotator(0.0, SamplingYawDegrees, 0.0));
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
		const FVector WorldCorner = GroundTransform.TransformPosition(GroundLocalCorner);
		if (!IsFiniteVector(WorldCorner))
		{
			ShowExportError(TEXT("Ground bounds fit failed: Ground Actor transform produced a non-finite world bound."));
			return;
		}

		const FVector SamplingCorner = SamplingYaw.UnrotateVector(WorldCorner);
		SamplingMinimum.X = FMath::Min(SamplingMinimum.X, SamplingCorner.X);
		SamplingMinimum.Y = FMath::Min(SamplingMinimum.Y, SamplingCorner.Y);
		SamplingMinimum.Z = FMath::Min(SamplingMinimum.Z, SamplingCorner.Z);
		SamplingMaximum.X = FMath::Max(SamplingMaximum.X, SamplingCorner.X);
		SamplingMaximum.Y = FMath::Max(SamplingMaximum.Y, SamplingCorner.Y);
		SamplingMaximum.Z = FMath::Max(SamplingMaximum.Z, SamplingCorner.Z);
	}

	const double Padding = FMath::Max(
		static_cast<double>(GroundBoundsPaddingCm),
		0.0);
	const FVector SamplingCenter =
		(SamplingMinimum + SamplingMaximum) * 0.5;
	const FVector SamplingHalfExtent =
		(SamplingMaximum - SamplingMinimum) * 0.5;
	if (!IsFiniteVector(SamplingCenter)
		|| !IsFiniteVector(SamplingHalfExtent))
	{
		ShowExportError(TEXT("Ground bounds fit failed: computed sampling bounds are not finite."));
		return;
	}

	FVector WorldCenter = SamplingYaw.RotateVector(SamplingCenter);
	// A yaw-only transform leaves Z unchanged, but assigning it explicitly keeps
	// this contract clear if the projection above changes in the future.
	WorldCenter.Z = SamplingCenter.Z;

	Modify();
	HorizontalExtentCm = FVector2D(
		FMath::Max(SamplingHalfExtent.X + Padding, 100.0),
		FMath::Max(SamplingHalfExtent.Y + Padding, 100.0));
	// Keep a deliberately larger author-provided trace corridor, but expand it
	// when necessary so the fitted preview volume contains the complete actor.
	const double RequiredVerticalExtent =
		FMath::Max(SamplingHalfExtent.Z + Padding, 100.0);
	TraceAboveCm = static_cast<float>(FMath::Max(
		static_cast<double>(TraceAboveCm),
		RequiredVerticalExtent));
	TraceBelowCm = static_cast<float>(FMath::Max(
		static_cast<double>(TraceBelowCm),
		RequiredVerticalExtent));
	SetActorLocationAndRotation(
		WorldCenter,
		FRotator(0.0, SamplingYawDegrees, 0.0));
	UpdateBoundsVisualization();
	MarkPackageDirty();

	const FString Result = FString::Printf(
		TEXT("Sampling bounds fitted to Ground Actor '%s'.\nCenter: %.1f, %.1f, %.1f cm\nYaw: %.2f deg\nHorizontal half extent: %.1f x %.1f cm\nSafety padding: %.1f cm"),
		*GroundActor->GetActorNameOrLabel(),
		WorldCenter.X,
		WorldCenter.Y,
		WorldCenter.Z,
		SamplingYawDegrees,
		HorizontalExtentCm.X,
		HorizontalExtentCm.Y,
		Padding);
	UE_LOG(LogSimCoreGroundExporter, Display, TEXT("%s"), *Result);
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result));
#endif
}

void AGroundCollisionExporter::ExportGroundSurface()
{
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

	const double ExtentX = FMath::Max(HorizontalExtentCm.X, 100.0);
	const double ExtentY = FMath::Max(HorizontalExtentCm.Y, 100.0);
	const double Spacing = FMath::Max(static_cast<double>(SampleSpacingCm), 25.0);
	// A named Landscape (or other explicitly selected ground actor) is a single
	// authored surface. Applying a raw corner-height cutoff to it turns a valid
	// 36-45 degree grade into a hole whose exact threshold depends on grid
	// direction. Retain the cutoff only for unfiltered WorldStatic sampling,
	// where it still prevents unrelated floors and walls from being bridged.
	const bool bApplyHeightDiscontinuityFilter = GroundActor == nullptr;
	const int32 QuadsX = FMath::CeilToInt(ExtentX * 2.0 / Spacing);
	const int32 QuadsY = FMath::CeilToInt(ExtentY * 2.0 / Spacing);
	const int64 PotentialTriangleCount = static_cast<int64>(QuadsX) * QuadsY * 2;
	if (QuadsX < 1 || QuadsY < 1 || PotentialTriangleCount > MaxExportedTriangleCount)
	{
		ShowExportError(FString::Printf(
			TEXT("Ground export rejected: this grid can produce %lld triangles; the limit is %lld. Increase Sample Spacing or reduce Horizontal Extent."),
			PotentialTriangleCount,
			MaxExportedTriangleCount));
		return;
	}

	const int32 Columns = QuadsX + 1;
	const int32 Rows = QuadsY + 1;
	const double StepX = ExtentX * 2.0 / QuadsX;
	const double StepY = ExtentY * 2.0 / QuadsY;
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

	TArray<FGroundCollisionSample> Samples;
	Samples.SetNum(Columns * Rows);
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCoreGroundExport), true);
	QueryParams.AddIgnoredActor(this);

	int32 ValidSampleCount = 0;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const FVector LocalOffset(
				-ExtentX + StepX * Column,
				-ExtentY + StepY * Row,
				0.0);
			const FVector PlanarOffset = YawRotation.RotateVector(LocalOffset);
			const FVector TraceStart(Center.X + PlanarOffset.X, Center.Y + PlanarOffset.Y, StartZ);
			const FVector TraceEnd(TraceStart.X, TraceStart.Y, EndZ);

			TArray<FHitResult> Hits;
			World->LineTraceMultiByObjectType(
				Hits,
				TraceStart,
				TraceEnd,
				ObjectQuery,
				QueryParams);

			const FHitResult* BestHit = nullptr;
			for (const FHitResult& Hit : Hits)
			{
				if (!Hit.bBlockingHit
					|| Hit.ImpactNormal.Z < MinimumGroundNormalZ
					|| !MatchesGroundActor(Hit, GroundActor))
				{
					continue;
				}
				if (BestHit == nullptr || Hit.Distance < BestHit->Distance)
				{
					BestHit = &Hit;
				}
			}

			if (BestHit != nullptr)
			{
				FGroundCollisionSample& Sample = Samples[Row * Columns + Column];
				Sample.ImpactPoint = BestHit->ImpactPoint;
				Sample.bValid = true;
				++ValidSampleCount;
			}
		}
	}

	if (ValidSampleCount == 0)
	{
		ShowExportError(TEXT("Ground export found no matching WorldStatic collision. Check the Landscape collision, Ground Actor filter, and sampling box."));
		return;
	}

	FString Csv;
	Csv.Reserve(static_cast<int32>(FMath::Min<int64>(
		PotentialTriangleCount * 190,
		MAX_int32)));
	Csv += TEXT("# Generated by SimCore Ground Collision Exporter.\n");
	Csv += FString::Printf(TEXT("# level=%s source_actor=%s spacing_cm=%.3f\n"),
		*World->GetMapName(),
		GroundActor != nullptr ? *GroundActor->GetName() : TEXT("any_world_static"),
		SampleSpacingCm);
	Csv += FString::Printf(TEXT("# map_origin_world_cm=%.3f,%.3f,%.3f\n"),
		MapOriginWorldCm.X,
		MapOriginWorldCm.Y,
		MapOriginWorldCm.Z);
	Csv += FString::Printf(TEXT("# height_discontinuity_filter=%s max_delta_cm=%.3f\n"),
		bApplyHeightDiscontinuityFilter ? TEXT("enabled") : TEXT("disabled_explicit_ground_actor"),
		MaxCellHeightDeltaCm);
	Csv += TEXT("# coordinate_mapping=east:UE_Y/100,north:UE_X/100,up:UE_Z/100\n");
	Csv += TEXT("surface_id,e0,n0,u0,e1,n1,u1,e2,n2,u2\n");

	int32 ExportedTriangleCount = 0;
	int32 SkippedMissingCells = 0;
	int32 SkippedDiscontinuousCells = 0;
	bool bMapOriginCoveredByExportedCell = false;
	const auto AppendTriangle = [
		&Csv,
		&SurfaceName,
		&ExportedTriangleCount,
		this](
		const FVector& A,
		const FVector& B,
		const FVector& C)
	{
		const FVector RelativeA = A - MapOriginWorldCm;
		const FVector RelativeB = B - MapOriginWorldCm;
		const FVector RelativeC = C - MapOriginWorldCm;
		Csv += FString::Printf(
			TEXT("%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"),
			*SurfaceName,
			RelativeA.Y / 100.0, RelativeA.X / 100.0, RelativeA.Z / 100.0,
			RelativeB.Y / 100.0, RelativeB.X / 100.0, RelativeB.Z / 100.0,
			RelativeC.Y / 100.0, RelativeC.X / 100.0, RelativeC.Z / 100.0);
		++ExportedTriangleCount;
	};

	for (int32 Row = 0; Row < QuadsY; ++Row)
	{
		for (int32 Column = 0; Column < QuadsX; ++Column)
		{
			const FGroundCollisionSample& P00 = Samples[Row * Columns + Column];
			const FGroundCollisionSample& P10 = Samples[Row * Columns + Column + 1];
			const FGroundCollisionSample& P01 = Samples[(Row + 1) * Columns + Column];
			const FGroundCollisionSample& P11 = Samples[(Row + 1) * Columns + Column + 1];
			if (!P00.bValid || !P10.bValid || !P01.bValid || !P11.bValid)
			{
				++SkippedMissingCells;
				continue;
			}

			const double MinimumHeight = FMath::Min(
				FMath::Min(P00.ImpactPoint.Z, P10.ImpactPoint.Z),
				FMath::Min(P01.ImpactPoint.Z, P11.ImpactPoint.Z));
			const double MaximumHeight = FMath::Max(
				FMath::Max(P00.ImpactPoint.Z, P10.ImpactPoint.Z),
				FMath::Max(P01.ImpactPoint.Z, P11.ImpactPoint.Z));
			if (bApplyHeightDiscontinuityFilter
				&& MaximumHeight - MinimumHeight > MaxCellHeightDeltaCm)
			{
				++SkippedDiscontinuousCells;
				continue;
			}

			AppendTriangle(P00.ImpactPoint, P10.ImpactPoint, P11.ImpactPoint);
			AppendTriangle(P00.ImpactPoint, P11.ImpactPoint, P01.ImpactPoint);

			const double CellMinimumX = -ExtentX + StepX * Column;
			const double CellMaximumX = CellMinimumX + StepX;
			const double CellMinimumY = -ExtentY + StepY * Row;
			const double CellMaximumY = CellMinimumY + StepY;
			constexpr double CoverageToleranceCm = 0.01;
			if (MapOriginLocalOffset.X >= CellMinimumX - CoverageToleranceCm
				&& MapOriginLocalOffset.X <= CellMaximumX + CoverageToleranceCm
				&& MapOriginLocalOffset.Y >= CellMinimumY - CoverageToleranceCm
				&& MapOriginLocalOffset.Y <= CellMaximumY + CoverageToleranceCm)
			{
				bMapOriginCoveredByExportedCell = true;
			}
		}
	}

	if (ExportedTriangleCount == 0)
	{
		ShowExportError(TEXT("Ground export produced no valid four-corner cells. Reduce Sample Spacing or fix missing collision samples."));
		return;
	}
	if (!bMapOriginCoveredByExportedCell)
	{
		ShowExportError(TEXT("Ground export rejected: no continuous exported ground cell covers Map Origin World Cm. The vehicle spawns at ENU (0, 0); move the sampling box onto the start area or repair collision near the map origin."));
		return;
	}

	FString StaticCollisionCsv;
	FString StaticCollisionError;
	int32 ExportedStaticColliderCount = 0;
	if (!BuildStaticColliderCsv(
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

	const FString GroundOutputPath = FPaths::Combine(
		ResolvedPackageDirectory,
		TEXT("ground_surface.csv"));
	const FString GroundTemporaryPath = GroundOutputPath + TEXT(".tmp");
	const FString StaticCollisionOutputPath = FPaths::Combine(
		ResolvedPackageDirectory,
		TEXT("static_colliders.csv"));
	const FString StaticCollisionTemporaryPath =
		StaticCollisionOutputPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(
		Csv,
		*GroundTemporaryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Delete(*GroundTemporaryPath, false, true);
		ShowExportError(FString::Printf(
			TEXT("Ground export failed to write temporary file: %s"),
			*GroundTemporaryPath));
		return;
	}
	if (!FFileHelper::SaveStringToFile(
		StaticCollisionCsv,
		*StaticCollisionTemporaryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Delete(*GroundTemporaryPath, false, true);
		IFileManager::Get().Delete(*StaticCollisionTemporaryPath, false, true);
		ShowExportError(FString::Printf(
			TEXT("Static collision export failed to write temporary file: %s"),
			*StaticCollisionTemporaryPath));
		return;
	}

	// Stage both payloads before replacing either one. manifest.cfg remains the
	// final commit marker, so a failure between payload moves leaves the old
	// identity mismatched and both runtimes fail closed instead of accepting a
	// mixed ground/static snapshot.
	if (!IFileManager::Get().Move(
		*GroundOutputPath,
		*GroundTemporaryPath,
		true,
		true))
	{
		IFileManager::Get().Delete(*GroundTemporaryPath, false, true);
		IFileManager::Get().Delete(*StaticCollisionTemporaryPath, false, true);
		ShowExportError(FString::Printf(
			TEXT("Ground export failed to replace: %s"),
			*GroundOutputPath));
		return;
	}
	if (!IFileManager::Get().Move(
		*StaticCollisionOutputPath,
		*StaticCollisionTemporaryPath,
		true,
		true))
	{
		IFileManager::Get().Delete(*StaticCollisionTemporaryPath, false, true);
		ShowExportError(FString::Printf(
			TEXT("Ground CSV was replaced, but static collision commit failed. The package is intentionally unusable until it is baked again.\n\n%s"),
			*StaticCollisionOutputPath));
		return;
	}

	// The collision payloads are committed first and manifest.cfg is committed
	// last. If export is interrupted between those operations, the old manifest
	// cannot validate the new payload and both runtimes fail closed.
	const TArray<FString> CollisionFiles{
		TEXT("ground_surface.csv"),
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
			TEXT("Collision CSVs were replaced, but manifest commit failed. The package is intentionally unusable until it is baked again.\n\n%s"),
			*ManifestError));
		return;
	}

	const FString Result = FString::Printf(
		TEXT("Exported %d ground triangles from %d/%d collision samples.\nExported %d explicit static OBB colliders.\nMap origin coverage: OK\nHeight discontinuity filter: %s\nSkipped cells: missing=%d, discontinuous=%d\nCollision identity: %s\n\n%s\n%s\n\nSTOP the current SimCore process, then restart it with --map-package %s"),
		ExportedTriangleCount,
		ValidSampleCount,
		Columns * Rows,
		ExportedStaticColliderCount,
		bApplyHeightDiscontinuityFilter ? TEXT("enabled") : TEXT("disabled for explicit Ground Actor"),
		SkippedMissingCells,
		SkippedDiscontinuousCells,
		*CollisionChecksum,
		*GroundOutputPath,
		*StaticCollisionOutputPath,
		*ResolvedPackageDirectory);
	UE_LOG(LogSimCoreGroundExporter, Display, TEXT("%s"), *Result);
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result));
#endif
}
