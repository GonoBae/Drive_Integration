#include "SimCoreGroundSnapshot.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace SimCoreGroundSnapshot
{
namespace
{
	constexpr uint32 SnapshotVersion = 2;
	constexpr uint32 HeaderSizeBytes = 80;
	constexpr uint32 SampleStrideBytes = 20;
	constexpr uint32 CellStrideBytes = 12;
	constexpr uint32 ValidFlag = 1u << 0;
	constexpr uint32 DrivableFlag = 1u << 0;
	constexpr double GridCoordinateEpsilon = 1.0e-9;
	constexpr float MinimumFrictionMultiplier = 0.05f;
	constexpr float MaximumFrictionMultiplier = 4.0f;

	static const FName DefaultSurfaceTag(TEXT("SimCore.Surface.Default"));
	static const FName AsphaltSurfaceTag(TEXT("SimCore.Surface.Asphalt"));
	static const FName LowFrictionSurfaceTag(TEXT("SimCore.Surface.LowFriction"));
	static const FName RoughSurfaceTag(TEXT("SimCore.Surface.Rough"));

	struct FGroundSample
	{
		uint32 Flags = 0;
		float UpM = 0.0f;
		float NormalEast = 0.0f;
		float NormalNorth = 0.0f;
		float NormalUp = 0.0f;
		ESurfaceMaterialId SurfaceMaterialId = ESurfaceMaterialId::Default;
		float FrictionMultiplier = 1.0f;
	};

	struct FGroundCell
	{
		uint32 Flags = 0;
		ESurfaceMaterialId SurfaceMaterialId = ESurfaceMaterialId::Default;
		float FrictionMultiplier = 1.0f;
	};

	struct FSurfaceProfile
	{
		ESurfaceMaterialId Id = ESurfaceMaterialId::Default;
		float FrictionMultiplier = 1.0f;
	};

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool MatchesGroundActor(const FHitResult& Hit, const AActor* GroundActor)
	{
		if (GroundActor == nullptr)
		{
			return true;
		}

		const AActor* HitActor = Hit.GetActor();
		return HitActor == GroundActor
			|| (HitActor != nullptr && HitActor->IsOwnedBy(GroundActor));
	}

	int32 FindTaggedSurfaceProfile(const TArray<FName>& Tags)
	{
		int32 Match = INDEX_NONE;
		const auto Consider = [&Tags, &Match](const FName Tag, const int32 Index)
		{
			if (!Tags.Contains(Tag))
			{
				return true;
			}
			if (Match != INDEX_NONE)
			{
				return false;
			}
			Match = Index;
			return true;
		};
		return Consider(DefaultSurfaceTag, 0)
			&& Consider(AsphaltSurfaceTag, 1)
			&& Consider(LowFrictionSurfaceTag, 2)
			&& Consider(RoughSurfaceTag, 3)
			? Match
			: -2;
	}

	bool ClassifySurface(
		const FHitResult& Hit,
		const FBuildRequest& Request,
		FSurfaceProfile& OutProfile,
		FString& OutError)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		const AActor* Actor = Hit.GetActor();
		int32 TaggedProfile = Component != nullptr
			? FindTaggedSurfaceProfile(Component->ComponentTags)
			: INDEX_NONE;
		if (TaggedProfile == INDEX_NONE && Actor != nullptr)
		{
			TaggedProfile = FindTaggedSurfaceProfile(Actor->Tags);
		}
		if (TaggedProfile == -2)
		{
			OutError = FString::Printf(
				TEXT("ground hit '%s' has conflicting SimCore.Surface.* tags"),
				Actor != nullptr ? *Actor->GetName() : TEXT("unknown"));
			return false;
		}

		int32 ProfileIndex = TaggedProfile;
		if (ProfileIndex == INDEX_NONE)
		{
			const EPhysicalSurface PhysicalSurface =
				UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
			const uint8 PhysicalSurfaceValue =
				static_cast<uint8>(PhysicalSurface);
			if (PhysicalSurfaceValue == Request.AsphaltPhysicalSurface)
			{
				ProfileIndex = 1;
			}
			else if (PhysicalSurfaceValue == Request.LowFrictionPhysicalSurface)
			{
				ProfileIndex = 2;
			}
			else if (PhysicalSurfaceValue == Request.RoughPhysicalSurface)
			{
				ProfileIndex = 3;
			}
			else
			{
				ProfileIndex = 0;
			}
		}

		switch (ProfileIndex)
		{
		case 1:
			OutProfile = {
				ESurfaceMaterialId::Asphalt,
				Request.AsphaltFrictionMultiplier};
			break;
		case 2:
			OutProfile = {
				ESurfaceMaterialId::LowFriction,
				Request.LowFrictionFrictionMultiplier};
			break;
		case 3:
			OutProfile = {
				ESurfaceMaterialId::Rough,
				Request.RoughFrictionMultiplier};
			break;
		default:
			OutProfile = {
				ESurfaceMaterialId::Default,
				Request.DefaultFrictionMultiplier};
			break;
		}
		return true;
	}

	void AppendUint32LittleEndian(TArray<uint8>& Bytes, uint32 Value)
	{
		Bytes.Add(static_cast<uint8>(Value & 0xffu));
		Bytes.Add(static_cast<uint8>((Value >> 8) & 0xffu));
		Bytes.Add(static_cast<uint8>((Value >> 16) & 0xffu));
		Bytes.Add(static_cast<uint8>((Value >> 24) & 0xffu));
	}

	void AppendUint64LittleEndian(TArray<uint8>& Bytes, uint64 Value)
	{
		for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
		{
			Bytes.Add(static_cast<uint8>(
				(Value >> (ByteIndex * 8)) & 0xffull));
		}
	}

	void AppendFloatLittleEndian(TArray<uint8>& Bytes, float Value)
	{
		static_assert(sizeof(float) == sizeof(uint32));
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		AppendUint32LittleEndian(Bytes, Bits);
	}

	void AppendDoubleLittleEndian(TArray<uint8>& Bytes, double Value)
	{
		static_assert(sizeof(double) == sizeof(uint64));
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		AppendUint64LittleEndian(Bytes, Bits);
	}

	FVector WorldPointToEnuM(
		const FVector& WorldPointCm,
		const FVector& MapOriginWorldCm)
	{
		const FVector Relative = WorldPointCm - MapOriginWorldCm;
		return FVector(Relative.Y / 100.0, Relative.X / 100.0, Relative.Z / 100.0);
	}
}

bool Build(
	const FBuildRequest& Request,
	FBuildResult& OutResult,
	FString& OutError)
{
	OutResult = {};
	OutError.Reset();

	if (Request.World == nullptr)
	{
		OutError = TEXT("the exporter has no editor world");
		return false;
	}
	if (!IsFiniteVector(Request.SamplingCenterWorldCm)
		|| !IsFiniteVector(Request.MapOriginWorldCm)
		|| !Request.SamplingYaw.IsNormalized()
		|| !FMath::IsFinite(Request.HorizontalExtentXCm)
		|| !FMath::IsFinite(Request.HorizontalExtentYCm)
		|| !FMath::IsFinite(Request.SampleSpacingCm)
		|| !FMath::IsFinite(Request.TraceStartZCm)
		|| !FMath::IsFinite(Request.TraceEndZCm)
		|| !FMath::IsFinite(Request.MinimumGroundNormalZ)
		|| !FMath::IsFinite(Request.MaximumCellHeightDeltaCm)
		|| !FMath::IsFinite(Request.DefaultFrictionMultiplier)
		|| !FMath::IsFinite(Request.AsphaltFrictionMultiplier)
		|| !FMath::IsFinite(Request.LowFrictionFrictionMultiplier)
		|| !FMath::IsFinite(Request.RoughFrictionMultiplier))
	{
		OutError = TEXT("sampling parameters must be finite");
		return false;
	}
	if (Request.HorizontalExtentXCm <= 0.0
		|| Request.HorizontalExtentYCm <= 0.0
		|| Request.SampleSpacingCm <= 0.0
		|| Request.TraceStartZCm <= Request.TraceEndZCm
		|| Request.MinimumGroundNormalZ <= 0.0
		|| Request.MinimumGroundNormalZ > 1.0
		|| Request.MaximumCellHeightDeltaCm <= 0.0
		|| Request.DefaultFrictionMultiplier < MinimumFrictionMultiplier
		|| Request.DefaultFrictionMultiplier > MaximumFrictionMultiplier
		|| Request.AsphaltFrictionMultiplier < MinimumFrictionMultiplier
		|| Request.AsphaltFrictionMultiplier > MaximumFrictionMultiplier
		|| Request.LowFrictionFrictionMultiplier < MinimumFrictionMultiplier
		|| Request.LowFrictionFrictionMultiplier > MaximumFrictionMultiplier
		|| Request.RoughFrictionMultiplier < MinimumFrictionMultiplier
		|| Request.RoughFrictionMultiplier > MaximumFrictionMultiplier)
	{
		OutError = TEXT("sampling extents, spacing, trace corridor, and ground thresholds are invalid");
		return false;
	}
	if (Request.AsphaltPhysicalSurface == static_cast<uint8>(SurfaceType_Default)
		|| Request.LowFrictionPhysicalSurface == static_cast<uint8>(SurfaceType_Default)
		|| Request.RoughPhysicalSurface == static_cast<uint8>(SurfaceType_Default)
		|| Request.AsphaltPhysicalSurface == Request.LowFrictionPhysicalSurface
		|| Request.AsphaltPhysicalSurface == Request.RoughPhysicalSurface
		|| Request.LowFrictionPhysicalSurface == Request.RoughPhysicalSurface)
	{
		OutError = TEXT("asphalt, low-friction, and rough Physical Surface assignments must be distinct and non-default");
		return false;
	}
	if (Request.MaximumSampleCount < 4
		|| Request.MaximumSampleCount > HardMaximumSampleCount)
	{
		OutError = FString::Printf(
			TEXT("Maximum Heightfield Sample Count must be in [4, %lld]"),
			HardMaximumSampleCount);
		return false;
	}

	const double QuadsXDouble = FMath::CeilToDouble(
		Request.HorizontalExtentXCm * 2.0 / Request.SampleSpacingCm);
	const double QuadsYDouble = FMath::CeilToDouble(
		Request.HorizontalExtentYCm * 2.0 / Request.SampleSpacingCm);
	if (!FMath::IsFinite(QuadsXDouble)
		|| !FMath::IsFinite(QuadsYDouble)
		|| QuadsXDouble < 1.0
		|| QuadsYDouble < 1.0
		|| QuadsXDouble > static_cast<double>(MAX_int32 - 1)
		|| QuadsYDouble > static_cast<double>(MAX_int32 - 1))
	{
		OutError = TEXT("sampling grid dimensions are outside the supported range");
		return false;
	}

	const int32 QuadsX = static_cast<int32>(QuadsXDouble);
	const int32 QuadsY = static_cast<int32>(QuadsYDouble);
	const int32 Columns = QuadsX + 1;
	const int32 Rows = QuadsY + 1;
	const int64 SampleCount = static_cast<int64>(Columns) * Rows;
	if (SampleCount > Request.MaximumSampleCount)
	{
		OutError = FString::Printf(
			TEXT("heightfield bake requires %lld samples (%d x %d), exceeding the configured limit %lld. Increase Heightfield Sample Spacing or intentionally raise Max Heightfield Sample Count up to %lld; the exporter will not reduce resolution automatically."),
			SampleCount,
			Columns,
			Rows,
			Request.MaximumSampleCount,
			HardMaximumSampleCount);
		return false;
	}

	const double StepXCm = Request.HorizontalExtentXCm * 2.0 / QuadsX;
	const double StepYCm = Request.HorizontalExtentYCm * 2.0 / QuadsY;
	const FVector FirstLocalOffset(
		-Request.HorizontalExtentXCm,
		-Request.HorizontalExtentYCm,
		0.0);
	const FVector FirstWorldPoint = Request.SamplingCenterWorldCm
		+ Request.SamplingYaw.RotateVector(FirstLocalOffset);
	const FVector OriginEnuM = WorldPointToEnuM(
		FirstWorldPoint,
		Request.MapOriginWorldCm);
	const FVector ColumnStepWorldCm = Request.SamplingYaw.RotateVector(
		FVector(StepXCm, 0.0, 0.0));
	const FVector RowStepWorldCm = Request.SamplingYaw.RotateVector(
		FVector(0.0, StepYCm, 0.0));
	const double ColumnStepEastM = ColumnStepWorldCm.Y / 100.0;
	const double ColumnStepNorthM = ColumnStepWorldCm.X / 100.0;
	const double RowStepEastM = RowStepWorldCm.Y / 100.0;
	const double RowStepNorthM = RowStepWorldCm.X / 100.0;
	if (!IsFiniteVector(OriginEnuM)
		|| !FMath::IsFinite(ColumnStepEastM)
		|| !FMath::IsFinite(ColumnStepNorthM)
		|| !FMath::IsFinite(RowStepEastM)
		|| !FMath::IsFinite(RowStepNorthM))
	{
		OutError = TEXT("heightfield ENU grid transform is not finite");
		return false;
	}
	const double GridDeterminant =
		ColumnStepEastM * RowStepNorthM
		- ColumnStepNorthM * RowStepEastM;
	if (!FMath::IsFinite(GridDeterminant)
		|| FMath::Abs(GridDeterminant) <= UE_DOUBLE_SMALL_NUMBER)
	{
		OutError = TEXT("heightfield ENU grid transform is singular");
		return false;
	}

	TArray<FGroundSample> Samples;
	Samples.SetNumZeroed(static_cast<int32>(SampleCount));
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(SimCoreGroundSnapshot),
		true);
	QueryParams.bReturnPhysicalMaterial = true;
	if (Request.IgnoredActor != nullptr)
	{
		QueryParams.AddIgnoredActor(Request.IgnoredActor);
	}

	int32 ValidSampleCount = 0;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const FVector LocalOffset(
				-Request.HorizontalExtentXCm + StepXCm * Column,
				-Request.HorizontalExtentYCm + StepYCm * Row,
				0.0);
			const FVector PlanarOffset =
				Request.SamplingYaw.RotateVector(LocalOffset);
			const FVector TraceStart(
				Request.SamplingCenterWorldCm.X + PlanarOffset.X,
				Request.SamplingCenterWorldCm.Y + PlanarOffset.Y,
				Request.TraceStartZCm);
			const FVector TraceEnd(
				TraceStart.X,
				TraceStart.Y,
				Request.TraceEndZCm);

			FHitResult BestHit;
			bool bFoundGroundHit = false;
			FCollisionQueryParams SampleQueryParams = QueryParams;
			// Object traces stop at the first blocking WorldStatic hit. When an
			// explicit Ground Actor is assigned, retry while ignoring unrelated
			// occluders so an overhead floor/mesh cannot hide the selected
			// Landscape. The bounded loop fails closed in a pathological stack.
			constexpr int32 MaximumGroundFilterTraceAttempts = 128;
			for (int32 Attempt = 0;
				Attempt < MaximumGroundFilterTraceAttempts;
				++Attempt)
			{
				FHitResult Hit;
				if (!Request.World->LineTraceSingleByObjectType(
					Hit,
					TraceStart,
					TraceEnd,
					ObjectQuery,
					SampleQueryParams))
				{
					break;
				}

				if (MatchesGroundActor(Hit, Request.GroundActor))
				{
					if (Hit.bBlockingHit
						&& IsFiniteVector(Hit.ImpactPoint)
						&& IsFiniteVector(Hit.ImpactNormal)
						&& Hit.ImpactNormal.Z
							>= Request.MinimumGroundNormalZ)
					{
						BestHit = Hit;
						bFoundGroundHit = true;
					}
					break;
				}

				AActor* OccludingActor = Hit.GetActor();
				if (OccludingActor == nullptr)
				{
					break;
				}
				SampleQueryParams.AddIgnoredActor(OccludingActor);
			}

			if (!bFoundGroundHit)
			{
				continue;
			}

			const FVector NormalWorld = BestHit.ImpactNormal.GetSafeNormal();
			const FVector PositionEnuM = WorldPointToEnuM(
				BestHit.ImpactPoint,
				Request.MapOriginWorldCm);
			const FVector NormalEnu(
				NormalWorld.Y,
				NormalWorld.X,
				NormalWorld.Z);
			const FVector NormalizedEnu = NormalEnu.GetSafeNormal();
			if (!IsFiniteVector(PositionEnuM)
				|| !IsFiniteVector(NormalizedEnu)
				|| NormalizedEnu.Z <= 0.0)
			{
				continue;
			}
			FSurfaceProfile SurfaceProfile;
			if (!ClassifySurface(
				BestHit,
				Request,
				SurfaceProfile,
				OutError))
			{
				return false;
			}

			FGroundSample& Sample = Samples[Row * Columns + Column];
			Sample.Flags = ValidFlag;
			Sample.UpM = static_cast<float>(PositionEnuM.Z);
			Sample.NormalEast = static_cast<float>(NormalizedEnu.X);
			Sample.NormalNorth = static_cast<float>(NormalizedEnu.Y);
			Sample.NormalUp = static_cast<float>(NormalizedEnu.Z);
			Sample.SurfaceMaterialId = SurfaceProfile.Id;
			Sample.FrictionMultiplier = SurfaceProfile.FrictionMultiplier;
			if (!FMath::IsFinite(Sample.UpM)
				|| !FMath::IsFinite(Sample.NormalEast)
				|| !FMath::IsFinite(Sample.NormalNorth)
				|| !FMath::IsFinite(Sample.NormalUp)
				|| !FMath::IsFinite(Sample.FrictionMultiplier))
			{
				Sample = {};
				continue;
			}
			++ValidSampleCount;
		}
	}

	if (ValidSampleCount == 0)
	{
		OutError = TEXT("no matching WorldStatic collision was measured; check Landscape collision, Ground Actor, and sampling bounds");
		return false;
	}

	TArray<FGroundCell> Cells;
	Cells.SetNumZeroed(QuadsX * QuadsY);
	for (FGroundCell& Cell : Cells)
	{
		Cell.FrictionMultiplier = 1.0f;
	}
	int32 DrivableCellCount = 0;
	int32 DefaultMaterialCellCount = 0;
	int32 AsphaltMaterialCellCount = 0;
	int32 LowFrictionMaterialCellCount = 0;
	int32 RoughMaterialCellCount = 0;
	int32 SkippedMissingCellCount = 0;
	int32 SkippedDiscontinuousCellCount = 0;
	FVector MinimumEnuM(
		TNumericLimits<double>::Max(),
		TNumericLimits<double>::Max(),
		TNumericLimits<double>::Max());
	FVector MaximumEnuM(
		TNumericLimits<double>::Lowest(),
		TNumericLimits<double>::Lowest(),
		TNumericLimits<double>::Lowest());
	for (int32 Row = 0; Row < QuadsY; ++Row)
	{
		for (int32 Column = 0; Column < QuadsX; ++Column)
		{
			const FGroundSample& P00 = Samples[Row * Columns + Column];
			const FGroundSample& P10 = Samples[Row * Columns + Column + 1];
			const FGroundSample& P01 = Samples[(Row + 1) * Columns + Column];
			const FGroundSample& P11 = Samples[(Row + 1) * Columns + Column + 1];
			if ((P00.Flags & ValidFlag) == 0
				|| (P10.Flags & ValidFlag) == 0
				|| (P01.Flags & ValidFlag) == 0
				|| (P11.Flags & ValidFlag) == 0)
			{
				++SkippedMissingCellCount;
				continue;
			}

			const float MinimumUpM = FMath::Min(
				FMath::Min(P00.UpM, P10.UpM),
				FMath::Min(P01.UpM, P11.UpM));
			const float MaximumUpM = FMath::Max(
				FMath::Max(P00.UpM, P10.UpM),
				FMath::Max(P01.UpM, P11.UpM));
			if (Request.bApplyHeightDiscontinuityFilter
				&& (MaximumUpM - MinimumUpM) * 100.0
					> Request.MaximumCellHeightDeltaCm)
			{
				++SkippedDiscontinuousCellCount;
				continue;
			}

			// A one-metre cell can straddle a painted material boundary. Select
			// the least-grippy measured corner so the server never invents grip;
			// equal multipliers use the lower append-only ID deterministically.
			const FGroundSample* SelectedMaterial = &P00;
			const FGroundSample* MaterialCandidates[4]{&P00, &P10, &P01, &P11};
			for (const FGroundSample* Candidate : MaterialCandidates)
			{
				if (Candidate->FrictionMultiplier
						< SelectedMaterial->FrictionMultiplier
					|| (Candidate->FrictionMultiplier
							== SelectedMaterial->FrictionMultiplier
						&& static_cast<uint32>(Candidate->SurfaceMaterialId)
							< static_cast<uint32>(SelectedMaterial->SurfaceMaterialId)))
				{
					SelectedMaterial = Candidate;
				}
			}
			FGroundCell& Cell = Cells[Row * QuadsX + Column];
			Cell.Flags = DrivableFlag;
			Cell.SurfaceMaterialId = SelectedMaterial->SurfaceMaterialId;
			Cell.FrictionMultiplier =
				SelectedMaterial->FrictionMultiplier;
			switch (Cell.SurfaceMaterialId)
			{
			case ESurfaceMaterialId::Asphalt:
				++AsphaltMaterialCellCount;
				break;
			case ESurfaceMaterialId::LowFriction:
				++LowFrictionMaterialCellCount;
				break;
			case ESurfaceMaterialId::Rough:
				++RoughMaterialCellCount;
				break;
			case ESurfaceMaterialId::Default:
				++DefaultMaterialCellCount;
				break;
			}
			++DrivableCellCount;

			const auto ExpandBounds = [
				&MinimumEnuM,
				&MaximumEnuM,
				&OriginEnuM,
				ColumnStepEastM,
				ColumnStepNorthM,
				RowStepEastM,
				RowStepNorthM](
				int32 SampleColumn,
				int32 SampleRow,
				const FGroundSample& Sample)
			{
				const FVector Point(
					OriginEnuM.X
						+ ColumnStepEastM * SampleColumn
						+ RowStepEastM * SampleRow,
					OriginEnuM.Y
						+ ColumnStepNorthM * SampleColumn
						+ RowStepNorthM * SampleRow,
					Sample.UpM);
				MinimumEnuM.X = FMath::Min(MinimumEnuM.X, Point.X);
				MinimumEnuM.Y = FMath::Min(MinimumEnuM.Y, Point.Y);
				MinimumEnuM.Z = FMath::Min(MinimumEnuM.Z, Point.Z);
				MaximumEnuM.X = FMath::Max(MaximumEnuM.X, Point.X);
				MaximumEnuM.Y = FMath::Max(MaximumEnuM.Y, Point.Y);
				MaximumEnuM.Z = FMath::Max(MaximumEnuM.Z, Point.Z);
			};
			ExpandBounds(Column, Row, P00);
			ExpandBounds(Column + 1, Row, P10);
			ExpandBounds(Column, Row + 1, P01);
			ExpandBounds(Column + 1, Row + 1, P11);

		}
	}

	if (DrivableCellCount == 0)
	{
		OutError = TEXT("no valid four-corner ground cells were measured; reduce spacing or repair missing collision samples");
		return false;
	}
	// Match the server's exact cell ownership rule, including the inclusive
	// maximum outer edge. An internal edge belongs to the +column/+row cell;
	// accepting either adjacent cell here could approve a spawn that the server
	// correctly queries as a hole.
	const FVector MapOriginLocalOffset = Request.SamplingYaw.UnrotateVector(
		Request.MapOriginWorldCm - Request.SamplingCenterWorldCm);
	double MapOriginColumn =
		(MapOriginLocalOffset.X + Request.HorizontalExtentXCm) / StepXCm;
	double MapOriginRow =
		(MapOriginLocalOffset.Y + Request.HorizontalExtentYCm) / StepYCm;
	const auto SnapGridCoordinate = [](double Value)
	{
		const double NearestInteger = FMath::RoundToDouble(Value);
		return FMath::Abs(Value - NearestInteger) <= GridCoordinateEpsilon
			? NearestInteger
			: Value;
	};
	MapOriginColumn = SnapGridCoordinate(MapOriginColumn);
	MapOriginRow = SnapGridCoordinate(MapOriginRow);
	const bool bMapOriginInGrid =
		FMath::IsFinite(MapOriginColumn)
		&& FMath::IsFinite(MapOriginRow)
		&& MapOriginColumn >= -GridCoordinateEpsilon
		&& MapOriginColumn <= QuadsX + GridCoordinateEpsilon
		&& MapOriginRow >= -GridCoordinateEpsilon
		&& MapOriginRow <= QuadsY + GridCoordinateEpsilon;
	bool bMapOriginCovered = false;
	if (bMapOriginInGrid)
	{
		MapOriginColumn = FMath::Clamp(
			MapOriginColumn,
			0.0,
			static_cast<double>(QuadsX));
		MapOriginRow = FMath::Clamp(
			MapOriginRow,
			0.0,
			static_cast<double>(QuadsY));
		const int32 MapOriginCellColumn = FMath::Min(
			FMath::FloorToInt(MapOriginColumn),
			QuadsX - 1);
		const int32 MapOriginCellRow = FMath::Min(
			FMath::FloorToInt(MapOriginRow),
			QuadsY - 1);
		bMapOriginCovered =
			(Cells[MapOriginCellRow * QuadsX + MapOriginCellColumn].Flags
				& DrivableFlag) != 0;
	}
	if (!bMapOriginCovered)
	{
		OutError = TEXT("no drivable measured cell covers Map Origin World Cm; the vehicle spawn would begin outside the exported ground");
		return false;
	}

	const int64 CellCount = static_cast<int64>(QuadsX) * QuadsY;
	const int64 BinarySize = HeaderSizeBytes
		+ SampleCount * SampleStrideBytes
		+ CellCount * CellStrideBytes;
	if (BinarySize > MAX_int32)
	{
		OutError = TEXT("heightfield binary exceeds Unreal's in-memory array limit");
		return false;
	}

	TArray<uint8> Binary;
	Binary.Reserve(static_cast<int32>(BinarySize));
	static constexpr uint8 Magic[8]{
		'S', 'I', 'M', 'G', 'H', 'F', '2', 0};
	Binary.Append(Magic, UE_ARRAY_COUNT(Magic));
	AppendUint32LittleEndian(Binary, SnapshotVersion);
	AppendUint32LittleEndian(Binary, HeaderSizeBytes);
	AppendUint32LittleEndian(Binary, static_cast<uint32>(Columns));
	AppendUint32LittleEndian(Binary, static_cast<uint32>(Rows));
	AppendUint32LittleEndian(Binary, SampleStrideBytes);
	AppendUint32LittleEndian(Binary, CellStrideBytes);
	AppendDoubleLittleEndian(Binary, OriginEnuM.X);
	AppendDoubleLittleEndian(Binary, OriginEnuM.Y);
	AppendDoubleLittleEndian(Binary, ColumnStepEastM);
	AppendDoubleLittleEndian(Binary, ColumnStepNorthM);
	AppendDoubleLittleEndian(Binary, RowStepEastM);
	AppendDoubleLittleEndian(Binary, RowStepNorthM);
	check(Binary.Num() == HeaderSizeBytes);

	for (const FGroundSample& Sample : Samples)
	{
		AppendUint32LittleEndian(Binary, Sample.Flags);
		AppendFloatLittleEndian(Binary, Sample.UpM);
		AppendFloatLittleEndian(Binary, Sample.NormalEast);
		AppendFloatLittleEndian(Binary, Sample.NormalNorth);
		AppendFloatLittleEndian(Binary, Sample.NormalUp);
	}
	for (const FGroundCell& Cell : Cells)
	{
		AppendUint32LittleEndian(Binary, Cell.Flags);
		AppendUint32LittleEndian(
			Binary,
			static_cast<uint32>(Cell.SurfaceMaterialId));
		AppendFloatLittleEndian(Binary, Cell.FrictionMultiplier);
	}
	check(Binary.Num() == BinarySize);

	// SimCore reproduces the Chaos/legacy exporter diagonal: for local v <= u,
	// P00/P10/P11; otherwise P00/P11/P01. Impact normals are measured by Unreal
	// and stored separately from that deterministic interpolation topology.
	OutResult.Binary = MoveTemp(Binary);
	OutResult.Columns = Columns;
	OutResult.Rows = Rows;
	OutResult.ValidSampleCount = ValidSampleCount;
	OutResult.DrivableCellCount = DrivableCellCount;
	OutResult.DefaultMaterialCellCount = DefaultMaterialCellCount;
	OutResult.AsphaltMaterialCellCount = AsphaltMaterialCellCount;
	OutResult.LowFrictionMaterialCellCount = LowFrictionMaterialCellCount;
	OutResult.RoughMaterialCellCount = RoughMaterialCellCount;
	OutResult.SkippedMissingCellCount = SkippedMissingCellCount;
	OutResult.SkippedDiscontinuousCellCount =
		SkippedDiscontinuousCellCount;
	OutResult.StepXCm = StepXCm;
	OutResult.StepYCm = StepYCm;
	OutResult.MinimumEnuM = MinimumEnuM;
	OutResult.MaximumEnuM = MaximumEnuM;
	OutResult.bMapOriginCovered = bMapOriginCovered;
	return true;
}
}
