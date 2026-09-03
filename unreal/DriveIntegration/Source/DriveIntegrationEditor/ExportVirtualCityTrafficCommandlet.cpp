#include "ExportVirtualCityTrafficCommandlet.h"

#include "SimCoreMapPackage.h"
#include "SimCoreSignalCityLayout.h"
#include "SimCoreSignalCityTrafficLayout.h"
#include "SimCoreVirtualCityLayout.h"
#include "SimCoreVirtualCityTrafficLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogExportVirtualCityTraffic, Log, All);

namespace
{
	const TCHAR* MapAsset = TEXT("/Game/VirtualCity/Maps/L_VirtualCity");
	const TCHAR* PackageRelative = TEXT("../../map_packages/virtual_city_v1");
	const TCHAR* ExpectedMapId = TEXT("virtual_city_v1");
	const TCHAR* GroundName = TEXT("VirtualCityGround");
	FVector WorldCm(const FVector& Enu) { return FVector(Enu.Y, Enu.X, Enu.Z) * 100.0; }

	bool VerifySavedGround(UWorld* World, const SimCoreVirtualCity::FLayout& Layout,
		AActor*& OutGround, FString& Error)
	{
		OutGround = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetFName() == GroundName) { OutGround = *It; break; }
		}
		if (!OutGround) { Error = TEXT("Saved map has no canonical city ground actor."); return false; }
		TArray<UStaticMeshComponent*> Components;
		OutGround->GetComponents(Components);
		int32 ExpectedCount = 0;
		for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
		{
			if (!Box.bGround) { continue; }
			++ExpectedCount;
			UStaticMeshComponent* const* Found = Components.FindByPredicate(
				[&Box](const auto* Component) { return Component->GetFName() == Box.Id; });
			const FTransform Expected(FRotator(Box.PitchDegrees, Box.HeadingDegrees, 0), WorldCm(Box.CenterEnuM), Box.SizeM);
			if (!Found || !(*Found)->GetStaticMesh() || !(*Found)->IsQueryCollisionEnabled()
				|| !(*Found)->GetComponentTransform().Equals(Expected, 0.01)
				|| (Box.Palette == SimCoreVirtualCity::EPalette::Road
					&& !(*Found)->ComponentHasTag(TEXT("SimCore.Surface.Asphalt"))))
			{
				Error = FString::Printf(TEXT("Saved ground differs from traffic authoring source: %s. Refusing stale geometry."), *Box.Id.ToString());
				return false;
			}
		}
		if (Components.Num() != ExpectedCount)
		{
			Error = TEXT("Saved ground component count differs from traffic authoring source."); return false;
		}
		return true;
	}

	template<typename TTrafficLayout>
	bool CheckAndAlignCollision(UWorld* World, AActor* Ground,
		TTrafficLayout& Traffic, FString& Error)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(VirtualCityTrafficValidation), false);
		// Read only the road/base actor: decorative sidewalk/markings and no-collision
		// SimCore marker boxes are not ground authority. No actors are changed/spawned.
		for (TActorIterator<AActor> It(World); It; ++It) { if (*It != Ground) { Query.AddIgnoredActor(*It); } }
		FString SurfaceFailure;
		auto Surface = [&](const FVector& Position, bool bRoad, double& OutZ)
		{
			SurfaceFailure.Reset();
			const FVector Start = WorldCm(Position);
			FHitResult Hit;
			if (!World->LineTraceSingleByObjectType(Hit, Start + FVector(0,0,5000), Start - FVector(0,0,5000),
				FCollisionObjectQueryParams(ECC_WorldStatic), Query))
			{
				SurfaceFailure = TEXT("no WorldStatic hit");
				return false;
			}
			if (Hit.GetActor() != Ground || !Hit.GetComponent())
			{
				SurfaceFailure = FString::Printf(TEXT("unexpected actor=%s component=%s"),
					*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()));
				return false;
			}
			FString Tags;
			for (const FName Tag : Hit.GetComponent()->ComponentTags)
			{
				if (!Tags.IsEmpty()) { Tags += TEXT(","); }
				Tags += Tag.ToString();
			}
			const bool bAsphalt = Hit.GetComponent()->ComponentHasTag(TEXT("SimCore.Surface.Asphalt"));
			if (Hit.ImpactNormal.Z < 0.9 || (bRoad && !bAsphalt))
			{
				SurfaceFailure = FString::Printf(
					TEXT("component=%s impact_z=%.3f normal_z=%.3f asphalt=%s tags=[%s]"),
					*GetNameSafe(Hit.GetComponent()), Hit.ImpactPoint.Z / 100.0,
					Hit.ImpactNormal.Z, bAsphalt ? TEXT("true") : TEXT("false"), *Tags);
				return false;
			}
			OutZ = Hit.ImpactPoint.Z / 100.0;
			if (FMath::Abs(OutZ - Position.Z) > 0.08)
			{
				SurfaceFailure = FString::Printf(
					TEXT("component=%s impact_z=%.3f authored_z=%.3f tags=[%s]"),
					*GetNameSafe(Hit.GetComponent()), OutZ, Position.Z, *Tags);
				return false;
			}
			return true;
		};
		for (auto& Lane : Traffic.Lanes)
		{
			for (FVector& Position : Lane.PointsEnuM)
			{
				double Z = 0;
				if (!Surface(Position, true, Z))
				{
					Error = FString::Printf(
						TEXT("Lane %u center fails saved asphalt collision at %.3f,%.3f: %s."),
						Lane.Id, Position.X, Position.Y, *SurfaceFailure);
					return false;
				}
				Position.Z = static_cast<double>(FMath::RoundToInt64(Z * 1.e6)) / 1.e6;
			}
			for (int32 Index = 1; Index < Lane.PointsEnuM.Num(); ++Index)
			{
				const FVector A = Lane.PointsEnuM[Index-1], B = Lane.PointsEnuM[Index];
				const FVector Delta = B-A;
				const FVector Right = FVector(Delta.Y, -Delta.X, 0).GetSafeNormal();
				const int32 Samples = FMath::Max(1, FMath::CeilToInt(Delta.Size()));
				for (int32 Sample = 0; Sample <= Samples; ++Sample)
				{
					const FVector Center = FMath::Lerp(A, B, static_cast<double>(Sample)/Samples);
					for (double Side : {-0.5, 0.0, 0.5})
					{
						double Z = 0;
						if (!Surface(Center + Right * (Side * Lane.WidthM), true, Z))
						{
							Error = FString::Printf(
								TEXT("Lane %u full-width corridor leaves saved asphalt at %.3f,%.3f: %s."),
								Lane.Id, (Center + Right * (Side * Lane.WidthM)).X,
								(Center + Right * (Side * Lane.WidthM)).Y, *SurfaceFailure);
							return false;
						}
					}
				}
			}
		}
		for (auto& Signal : Traffic.Signals)
		{
			double Z = 0;
			if (!Surface(Signal.PositionEnuM, false, Z)) { Error = TEXT("Signal pole base has no matching saved ground."); return false; }
			Signal.PositionEnuM.Z = static_cast<double>(FMath::RoundToInt64(Z * 1.e6)) / 1.e6;
		}
		return true;
	}
}

UExportVirtualCityTrafficCommandlet::UExportVirtualCityTrafficCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UExportVirtualCityTrafficCommandlet::Main(const FString& Params)
{
	const bool bSignalCity = FParse::Param(*Params, TEXT("SignalCity"));
	if (bSignalCity)
	{
		MapAsset = TEXT("/Game/SignalCity/Maps/L_SignalCity");
		PackageRelative = TEXT("../../map_packages/signal_city_v2");
		ExpectedMapId = TEXT("signal_city_v2");
		GroundName = TEXT("SignalCityGround");
	}
	else
	{
		MapAsset = TEXT("/Game/VirtualCity/Maps/L_VirtualCity");
		PackageRelative = TEXT("../../map_packages/virtual_city_v1");
		ExpectedMapId = TEXT("virtual_city_v1");
		GroundName = TEXT("VirtualCityGround");
	}
	const bool bValidateOnly = FParse::Param(*Params, TEXT("ValidateOnly"));
	const bool bUpdateExisting = FParse::Param(*Params, TEXT("UpdateExisting"));
	if (bValidateOnly && bUpdateExisting)
	{
		UE_LOG(LogExportVirtualCityTraffic, Error,
			TEXT("-ValidateOnly and -UpdateExisting are mutually exclusive. No file was changed."));
		return 1;
	}
	if (!FParse::Param(FCommandLine::Get(), TEXT("nowrite")) || FParse::Param(*Params, TEXT("Replace")))
	{
		UE_LOG(LogExportVirtualCityTraffic, Error,
			TEXT("Use -nowrite to prevent config writes. -Replace, map saving and rebaking are intentionally unsupported; use -UpdateExisting only for the traffic JSON."));
		return 1;
	}
	SimCoreMapPackage::FManifest Manifest;
	FString Error;
	if (!SimCoreMapPackage::LoadAndVerifyManifest(PackageRelative, Manifest, Error)
		|| Manifest.MapId != ExpectedMapId)
	{
		UE_LOG(LogExportVirtualCityTraffic, Error,
			TEXT("A verified existing %s package is required: %s"), ExpectedMapId, *Error);
		return 1;
	}
	const FString Output = FPaths::Combine(Manifest.PackageDirectory, TEXT("traffic_network.json"));
	const bool bExists = IFileManager::Get().FileExists(*Output);
	const bool bRequiresExisting = bValidateOnly || bUpdateExisting;
	if (bExists != bRequiresExisting)
	{
		UE_LOG(LogExportVirtualCityTraffic, Error,
			TEXT("Initial export requires an absent traffic_network.json; -ValidateOnly and -UpdateExisting require an existing file. Nothing was changed: %s"),
			*Output);
		return 1;
	}
	const FString Filename = FPackageName::LongPackageNameToFilename(MapAsset, FPackageName::GetMapPackageExtension());
	if (!GEditor || !IFileManager::Get().FileExists(*Filename))
	{
		UE_LOG(LogExportVirtualCityTraffic, Error, TEXT("The existing versioned city map is required; no map is created.")); return 1;
	}
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Filename);
	if (!World) { return 1; }
	World->UpdateWorldComponents(true, false);
	AActor* Ground = nullptr;
	FString Json;
	int32 LaneCount = 0;
	int32 SignalCount = 0;
	if (bSignalCity)
	{
		auto Traffic = SimCoreSignalCity::BuildTrafficLayout();
		const auto SourceLayout = SimCoreSignalCity::BuildLayout();
		if (!SimCoreSignalCity::ValidateTrafficLayout(Traffic, Error)
			|| !VerifySavedGround(World, SourceLayout, Ground, Error)
			|| !CheckAndAlignCollision(World, Ground, Traffic, Error)
			|| !SimCoreSignalCity::SerializeTrafficLayout(
				Traffic, Manifest.CollisionChecksum, Json, Error))
		{
			UE_LOG(LogExportVirtualCityTraffic, Error,
				TEXT("SignalCity traffic/map alignment or serialization failed: %s"), *Error);
			return 1;
		}
		LaneCount = Traffic.Lanes.Num();
		SignalCount = Traffic.Signals.Num();
	}
	else
	{
		auto Traffic = SimCoreVirtualCity::BuildTrafficLayout();
		const auto SourceLayout = SimCoreVirtualCity::BuildLayout();
		if (!SimCoreVirtualCity::ValidateTrafficLayout(Traffic, Error)
			|| !VerifySavedGround(World, SourceLayout, Ground, Error)
			|| !CheckAndAlignCollision(World, Ground, Traffic, Error)
			|| !SimCoreVirtualCity::SerializeTrafficLayout(
				Traffic, Manifest.CollisionChecksum, Json, Error))
		{
			UE_LOG(LogExportVirtualCityTraffic, Error,
				TEXT("VirtualCity traffic/map alignment or serialization failed: %s"), *Error);
			return 1;
		}
		LaneCount = Traffic.Lanes.Num();
		SignalCount = Traffic.Signals.Num();
	}
	if (Json.IsEmpty())
	{
		UE_LOG(LogExportVirtualCityTraffic, Error, TEXT("Traffic serialization produced no bytes: %s"), *Error);
		return 1;
	}
	if (bValidateOnly)
	{
		TArray<uint8> Existing;
		const FTCHARToUTF8 Expected(*Json);
		if (!FFileHelper::LoadFileToArray(Existing, *Output) || Existing.Num() != Expected.Length()
			|| FMemory::Memcmp(Existing.GetData(), Expected.Get(), Expected.Length()) != 0)
		{
			UE_LOG(LogExportVirtualCityTraffic, Error, TEXT("Existing traffic JSON differs from canonical saved-map export. No file was changed.")); return 1;
		}
	}
	else if (bUpdateExisting)
	{
		const FString TemporaryOutput = FPaths::CreateTempFilename(
			*Manifest.PackageDirectory, TEXT("traffic_network."), TEXT(".json.tmp"));
		if (!FFileHelper::SaveStringToFile(Json, *TemporaryOutput,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_NoReplaceExisting))
		{
			IFileManager::Get().Delete(*TemporaryOutput, false, false, true);
			UE_LOG(LogExportVirtualCityTraffic, Error,
				TEXT("Canonical traffic JSON temporary write failed; the existing file was preserved: %s"), *Output);
			return 1;
		}
		if (!IFileManager::Get().Move(*Output, *TemporaryOutput,
			true, false, false, true))
		{
			IFileManager::Get().Delete(*TemporaryOutput, false, false, true);
			UE_LOG(LogExportVirtualCityTraffic, Error,
				TEXT("Canonical traffic JSON replacement failed: %s"), *Output);
			return 1;
		}
	}
	else if (!FFileHelper::SaveStringToFile(Json, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_NoReplaceExisting))
	{
		UE_LOG(LogExportVirtualCityTraffic, Error, TEXT("Exclusive initial traffic JSON creation failed: %s"), *Output); return 1;
	}
	UE_LOG(LogExportVirtualCityTraffic, Display,
		TEXT("Traffic %s: lanes=%d heads=%d checksum=%s file=%s. Map, ground package and static collision unchanged; runtime heads are NoCollision and logical stop lines align to authored markings."),
		bValidateOnly ? TEXT("validated") : (bUpdateExisting ? TEXT("updated") : TEXT("created")),
		LaneCount, SignalCount,
		*Manifest.CollisionChecksum, *Output);
	return 0;
}
