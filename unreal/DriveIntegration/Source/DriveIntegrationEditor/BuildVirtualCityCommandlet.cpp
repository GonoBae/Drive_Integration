#include "BuildVirtualCityCommandlet.h"

#include "GroundCollisionExporter.h"
#include "SimCoreMapPackage.h"
#include "SimCoreSignalCityLayout.h"
#include "SimCoreStaticCollider.h"
#include "SimCoreVirtualCityLayout.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogBuildVirtualCity, Log, All);

namespace
{
const TCHAR* MapAsset = TEXT("/Game/VirtualCity/Maps/L_VirtualCity");
const TCHAR* PackageRelative = TEXT("../../map_packages/virtual_city_v1");
const TCHAR* ExpectedMapId = TEXT("virtual_city_v1");
const TCHAR* GroundName = TEXT("VirtualCityGround");
const TCHAR* ExporterName = TEXT("VirtualCityGroundExporter");
const TCHAR* GeneratedTag = TEXT("SimCore.VirtualCity.Generated.v1");
const TCHAR* GameModeClassPath = TEXT("/Script/DriveIntegration.VirtualCityGameMode");
const TCHAR* GameModeObjectName = TEXT("VirtualCityGameMode");
const TCHAR* PlayerStartName = TEXT("VirtualCityPlayerStart");
// Fifty centimetres resolves the two-metre grade curb segments and narrow
// road-to-sidewalk transitions used by the production tire-support contract.
// The resulting 401 x 481 lattice remains well below the exporter hard cap.
constexpr float HeightfieldSampleSpacingCm = 50.0f;
// Maps authored before the curb-clearance correction placed generated sidewalk
// support at 16 cm while the curb top was 24 cm. This exact delta is accepted
// only by the targeted migration below; ordinary validation remains strict.
constexpr double LegacySidewalkToCurbDeltaCm = 8.0;
const FName ThreeLaneTag(TEXT("SimCore.SignalCity.ThreeLane.v1"));
const FName SafeCrossingsTag(TEXT("SimCore.SignalCity.SidewalkCrossings.v1"));
const FName CompleteLaneMarkingsTag(TEXT("SimCore.SignalCity.CompleteLaneMarkings.v1"));
const FName NaturalLaneMarkingsV2Tag(TEXT("SimCore.SignalCity.NaturalLaneMarkings.v2"));
const FName AlignedCollectorMarkingsTag(TEXT("SimCore.SignalCity.AlignedCollectorMarkings.v3"));

FVector ToWorldCm(const FVector& EnuM)
{
	return FVector(EnuM.Y, EnuM.X, EnuM.Z) * 100.0;
}

FTransform BoxTransform(const SimCoreVirtualCity::FBox& Box)
{
	return FTransform(FRotator(Box.PitchDegrees, Box.HeadingDegrees, 0.0),
		ToWorldCm(Box.CenterEnuM), Box.SizeM);
}

FTransform LegacySidewalkTransform(const SimCoreVirtualCity::FBox& Box)
{
	FTransform Transform = BoxTransform(Box);
	Transform.AddToTranslation(FVector(0.0, 0.0, -LegacySidewalkToCurbDeltaCm));
	return Transform;
}

const TCHAR* PaletteName(SimCoreVirtualCity::EPalette Palette)
{
	using SimCoreVirtualCity::EPalette;
	switch (Palette)
	{
	case EPalette::Ground: return TEXT("Ground");
	case EPalette::Road: return TEXT("Road");
	case EPalette::Sidewalk: return TEXT("Sidewalk");
	case EPalette::Curb: return TEXT("Curb");
	case EPalette::Building: return TEXT("Building");
	case EPalette::Glass: return TEXT("Glass");
	case EPalette::Marking: return TEXT("Marking");
	case EPalette::Yellow: return TEXT("Yellow");
	case EPalette::Barrier: return TEXT("Barrier");
	case EPalette::Grass: return TEXT("Grass");
	}
	return TEXT("Ground");
}

FLinearColor PaletteColor(SimCoreVirtualCity::EPalette Palette)
{
	using SimCoreVirtualCity::EPalette;
	switch (Palette)
	{
	case EPalette::Ground: return FLinearColor(0.20f, 0.23f, 0.22f);
	case EPalette::Road: return FLinearColor(0.045f, 0.055f, 0.065f);
	case EPalette::Sidewalk: return FLinearColor(0.48f, 0.49f, 0.47f);
	case EPalette::Curb: return FLinearColor(0.70f, 0.68f, 0.62f);
	case EPalette::Building: return FLinearColor(0.33f, 0.42f, 0.49f);
	case EPalette::Glass: return FLinearColor(0.07f, 0.19f, 0.25f);
	case EPalette::Marking: return FLinearColor(0.92f, 0.92f, 0.82f);
	case EPalette::Yellow: return FLinearColor(1.0f, 0.61f, 0.025f);
	case EPalette::Barrier: return FLinearColor(0.93f, 0.24f, 0.055f);
	case EPalette::Grass: return FLinearColor(0.10f, 0.25f, 0.115f);
	}
	return FLinearColor::White;
}

UMaterial* CreatePaletteMaterial(SimCoreVirtualCity::EPalette Palette)
{
	const FString Name = FString(TEXT("M_VC_")) + PaletteName(Palette);
	const FString Path = FString(TEXT("/Game/VirtualCity/Materials/")) + Name;
	if (FPackageName::DoesPackageExist(Path))
	{
		UMaterial* Existing = LoadObject<UMaterial>(nullptr, *(Path + TEXT(".") + Name));
		if (!Existing)
		{
			UE_LOG(LogBuildVirtualCity, Error, TEXT("Existing material asset cannot be loaded; refusing replacement: %s"), *Path);
		}
		return Existing;
	}
	UPackage* Package = CreatePackage(*Path);
	UMaterial* Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone);
	UMaterialExpressionConstant3Vector* Color = CastChecked<UMaterialExpressionConstant3Vector>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material,
			UMaterialExpressionConstant3Vector::StaticClass(), -250, -80));
	Color->Constant = PaletteColor(Palette);
	UMaterialEditingLibrary::ConnectMaterialProperty(Color, TEXT(""), MP_BaseColor);
	UMaterialExpressionConstant* Roughness = CastChecked<UMaterialExpressionConstant>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material,
			UMaterialExpressionConstant::StaticClass(), -250, 80));
	Roughness->R = Palette == SimCoreVirtualCity::EPalette::Glass ? 0.3f : 0.82f;
	UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);
	UMaterialEditingLibrary::RecompileMaterial(Material);
	FAssetRegistryModule::AssetCreated(Material);
	Material->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(Path,
		FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Material, *Filename, SaveArgs))
	{
		UE_LOG(LogBuildVirtualCity, Error, TEXT("Unable to save material %s"), *Filename);
		return nullptr;
	}
	return Material;
}

template<typename T>
T* SpawnNamed(UWorld* World, FName Name)
{
	FActorSpawnParameters Params;
	Params.Name = Name;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	T* Actor = World->SpawnActor<T>(T::StaticClass(), FTransform::Identity, Params);
	if (Actor)
	{
		Actor->SetActorLabel(Name.ToString());
		Actor->Tags.Add(GeneratedTag);
	}
	return Actor;
}

void ConfigureMesh(UStaticMeshComponent* Component, UStaticMesh* Cube,
	UMaterialInterface* Material, const SimCoreVirtualCity::FBox& Box)
{
	Component->SetMobility(EComponentMobility::Static);
	Component->SetStaticMesh(Cube);
	Component->SetMaterial(0, Material);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCollisionProfileName(TEXT("BlockAll"));
	Component->SetCollisionObjectType(ECC_WorldStatic);
	Component->SetCollisionEnabled(Box.bGround || Box.bStaticCollider
		? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Component->SetCanEverAffectNavigation(false);
	if (Box.bGround)
	{
		Component->ComponentTags.Add(Box.Palette == SimCoreVirtualCity::EPalette::Road
			? TEXT("SimCore.Surface.Asphalt") : TEXT("SimCore.Surface.Rough"));
	}
}

bool BuildScene(UWorld* World, const SimCoreVirtualCity::FLayout& Layout)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UClass* GameMode = LoadClass<AGameModeBase>(nullptr, GameModeClassPath);
	if (!Cube || !GameMode)
	{
		UE_LOG(LogBuildVirtualCity, Error, TEXT("Required cube or map-local GameMode is missing."));
		return false;
	}
	TMap<SimCoreVirtualCity::EPalette, UMaterial*> Materials;
	for (const auto& Box : Layout.Boxes)
	{
		if (!Materials.Contains(Box.Palette))
		{
			UMaterial* Material = CreatePaletteMaterial(Box.Palette);
			if (!Material) { return false; }
			Materials.Add(Box.Palette, Material);
		}
	}
	World->GetWorldSettings()->DefaultGameMode = GameMode;
	AActor* Ground = SpawnNamed<AActor>(World, GroundName);
	if (FString(ExpectedMapId) == TEXT("signal_city_v2"))
	{
		Ground->Tags.Add(ThreeLaneTag);
		Ground->Tags.Add(SafeCrossingsTag);
		Ground->Tags.Add(CompleteLaneMarkingsTag);
		Ground->Tags.Add(NaturalLaneMarkingsV2Tag);
		Ground->Tags.Add(AlignedCollectorMarkingsTag);
	}
	USceneComponent* Root = NewObject<USceneComponent>(Ground, TEXT("GroundRoot"), RF_Transactional);
	Ground->SetRootComponent(Root);
	Ground->AddInstanceComponent(Root);
	Root->SetMobility(EComponentMobility::Static);
	Root->RegisterComponent();
	for (const auto& Box : Layout.Boxes)
	{
		if (Box.bGround)
		{
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Ground, Box.Id, RF_Transactional);
			Ground->AddInstanceComponent(Component);
			Component->SetupAttachment(Root);
			ConfigureMesh(Component, Cube, Materials[Box.Palette], Box);
			Component->SetRelativeTransform(BoxTransform(Box));
			Component->RegisterComponent();
		}
		else
		{
			AStaticMeshActor* Actor = SpawnNamed<AStaticMeshActor>(World, Box.Id);
			ConfigureMesh(Actor->GetStaticMeshComponent(), Cube, Materials[Box.Palette], Box);
			Actor->SetActorTransform(BoxTransform(Box));
			Actor->SetFolderPath(Box.bStaticCollider ? TEXT("VirtualCity/CollisionGeometry")
				: TEXT("VirtualCity/Decoration"));
		}
		if (Box.bStaticCollider)
		{
			ASimCoreStaticCollider* Marker = SpawnNamed<ASimCoreStaticCollider>(World,
				FName(*(FString(TEXT("Collider_")) + Box.Id.ToString())));
			Marker->ColliderId = Box.Id.ToString();
			Marker->Semantic = Box.Palette == SimCoreVirtualCity::EPalette::Curb
				? ESimCoreStaticColliderSemantic::Curb
				: (Box.Palette == SimCoreVirtualCity::EPalette::Barrier
					? ESimCoreStaticColliderSemantic::Barrier : ESimCoreStaticColliderSemantic::Wall);
			Marker->CollisionBounds->SetBoxExtent(Box.SizeM * 50.0);
			Marker->SetActorLocationAndRotation(ToWorldCm(Box.CenterEnuM),
				FRotator(0.0, Box.HeadingDegrees, 0.0));
			Marker->SetFolderPath(TEXT("VirtualCity/SimCoreMarkers"));
		}
	}
	Ground->SetFolderPath(TEXT("VirtualCity/Ground"));
	AGroundCollisionExporter* Exporter = SpawnNamed<AGroundCollisionExporter>(World, ExporterName);
	Exporter->ConfigureForAuthoring(Ground, PackageRelative, FVector::ZeroVector,
		ToWorldCm(Layout.GroundCenterEnuM), Layout.GroundHalfExtentNorthEastM * 100.0,
		HeightfieldSampleSpacingCm, 0.0f);
	Exporter->SetFolderPath(TEXT("VirtualCity/SimCoreMarkers"));
	APlayerStart* Start = SpawnNamed<APlayerStart>(World, PlayerStartName);
	const SimCoreVirtualCity::FCheckpoint* StartPoint = Layout.DriveRoute.IsEmpty()
		? nullptr : &Layout.DriveRoute[0];
	const bool bSignalCity = FString(ExpectedMapId) == TEXT("signal_city_v2");
	Start->SetActorLocationAndRotation(
		bSignalCity ? ToWorldCm(FVector(2.0, 0.0, 0.0)) + FVector(0.0, 0.0, 100.0)
			: StartPoint ? ToWorldCm(StartPoint->PositionEnuM) + FVector(0.0, 0.0, 100.0)
			: FVector(0.0, 0.0, 100.0),
		FRotator(0.0, bSignalCity ? 0.0
			: StartPoint ? StartPoint->HeadingDegrees : 90.0, 0.0));
	ADirectionalLight* Sun = SpawnNamed<ADirectionalLight>(World, TEXT("VirtualCitySun"));
	Sun->SetActorRotation(FRotator(-38.0, -32.0, 0.0));
	UDirectionalLightComponent* SunComponent = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
	SunComponent->SetMobility(EComponentMobility::Movable);
	SunComponent->SetIntensity(50000.0f);
	SunComponent->SetAtmosphereSunLight(true);
	SpawnNamed<ASkyAtmosphere>(World, TEXT("VirtualCityAtmosphere"));
	ASkyLight* Sky = SpawnNamed<ASkyLight>(World, TEXT("VirtualCitySkyLight"));
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->SetIntensity(1.0f);
	Sky->GetLightComponent()->bRealTimeCapture = true;
	Sky->GetLightComponent()->bLowerHemisphereIsBlack = false;
	World->UpdateWorldComponents(true, false);
	return true;
}

bool SyncGeneratedGroundComponents(UWorld* World, const SimCoreVirtualCity::FLayout& Layout)
{
	AActor* Ground = nullptr;
	TMap<FName, AActor*> Actors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		Actors.Add(It->GetFName(), *It);
		if (It->GetFName() == GroundName)
		{
			Ground = *It;
		}
	}
	if (!Ground || !Ground->GetRootComponent()
		|| !Ground->Tags.Contains(GeneratedTag))
	{
		UE_LOG(LogBuildVirtualCity, Error,
			TEXT("Ground sync requires the generated VirtualCityGround actor; no map data was changed."));
		return false;
	}

	TArray<UStaticMeshComponent*> ExistingGroundComponents;
	Ground->GetComponents<UStaticMeshComponent>(ExistingGroundComponents);
	TSet<FName> ExistingGroundNames;
	for (UStaticMeshComponent* Component : ExistingGroundComponents)
	{
		ExistingGroundNames.Add(Component->GetFName());
	}

	TArray<AActor*> MigratedActors;
	for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
	{
		if (!Box.bGround || ExistingGroundNames.Contains(Box.Id))
		{
			continue;
		}
		AActor* const* SourceActorPtr = Actors.Find(Box.Id);
		AActor* SourceActor = SourceActorPtr ? *SourceActorPtr : nullptr;
		UStaticMeshComponent* Source = SourceActor
			? SourceActor->FindComponentByClass<UStaticMeshComponent>() : nullptr;
		if (!SourceActor || !Source
			|| !SourceActor->Tags.Contains(GeneratedTag)
			|| !Source->GetStaticMesh() || !Source->GetMaterial(0)
			|| !Source->GetComponentTransform().Equals(BoxTransform(Box), 0.1))
		{
			UE_LOG(LogBuildVirtualCity, Error,
				TEXT("Ground sync refused non-generated or edited geometry: %s. No actors were deleted."),
				*Box.Id.ToString());
			return false;
		}

		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(
			Ground, Box.Id, RF_Transactional);
		Ground->AddInstanceComponent(Component);
		Component->SetupAttachment(Ground->GetRootComponent());
		ConfigureMesh(Component, Source->GetStaticMesh(), Source->GetMaterial(0), Box);
		Component->SetRelativeTransform(BoxTransform(Box));
		Component->RegisterComponent();
		ExistingGroundNames.Add(Box.Id);
		MigratedActors.Add(SourceActor);
	}

	for (AActor* Actor : MigratedActors)
	{
		if (!World->DestroyActor(Actor, false, true))
		{
			UE_LOG(LogBuildVirtualCity, Error,
				TEXT("Ground sync could not retire generated actor %s."), *Actor->GetName());
			return false;
		}
	}
	Ground->MarkPackageDirty();
	World->UpdateWorldComponents(true, false);
	UE_LOG(LogBuildVirtualCity, Display,
		TEXT("Ground sync migrated %d generated curb/sidewalk components into VirtualCityGround."),
		MigratedActors.Num());
	return true;
}

bool SyncHeightfieldResolution(UWorld* World, const SimCoreVirtualCity::FLayout& Layout)
{
	AActor* Ground = nullptr;
	AGroundCollisionExporter* Exporter = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetFName() == GroundName) { Ground = *It; }
		if (It->GetFName() == ExporterName)
		{
			Exporter = Cast<AGroundCollisionExporter>(*It);
		}
	}
	if (!Ground || !Exporter)
	{
		UE_LOG(LogBuildVirtualCity, Error,
			TEXT("Heightfield sync requires the canonical ground and exporter actors."));
		return false;
	}
	Exporter->ConfigureForAuthoring(Ground, PackageRelative, FVector::ZeroVector,
		ToWorldCm(Layout.GroundCenterEnuM), Layout.GroundHalfExtentNorthEastM * 100.0,
		HeightfieldSampleSpacingCm, 0.0f);
	Exporter->MarkPackageDirty();
	World->UpdateWorldComponents(true, false);
	UE_LOG(LogBuildVirtualCity, Display,
		TEXT("Heightfield sync set the canonical VirtualCity sampling spacing to %.1f cm."),
		HeightfieldSampleSpacingCm);
	return true;
}

bool ValidateScene(UWorld* World, const SimCoreVirtualCity::FLayout& Layout,
	bool bAllowLegacySidewalkHeight = false)
{
	AActor* Ground = nullptr;
	AGroundCollisionExporter* Exporter = nullptr;
	TMap<FName, AActor*> Actors;
	int32 MarkerCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		Actors.Add(It->GetFName(), *It);
		if (It->GetFName() == GroundName) { Ground = *It; }
		if (AGroundCollisionExporter* Candidate = Cast<AGroundCollisionExporter>(*It)) { Exporter = Candidate; }
		MarkerCount += It->IsA<ASimCoreStaticCollider>() ? 1 : 0;
	}
	if (!Ground || !Exporter || !World->GetWorldSettings()->DefaultGameMode
		|| World->GetWorldSettings()->DefaultGameMode->GetName() != GameModeObjectName)
	{
		UE_LOG(LogBuildVirtualCity, Error, TEXT("Saved map is missing ground, exporter or map-local game mode."));
		return false;
	}
	TArray<UStaticMeshComponent*> GroundComponents;
	Ground->GetComponents<UStaticMeshComponent>(GroundComponents);
	int32 ExpectedMarkers = 0;
	int32 ExpectedGroundComponents = 0;
	for (const auto& Box : Layout.Boxes)
	{
		ExpectedMarkers += Box.bStaticCollider ? 1 : 0;
		ExpectedGroundComponents += Box.bGround ? 1 : 0;
		UStaticMeshComponent* Component = nullptr;
		if (Box.bGround)
		{
			for (UStaticMeshComponent* Candidate : GroundComponents)
			{
				if (Candidate->GetFName() == Box.Id) { Component = Candidate; break; }
			}
		}
		else if (AActor** Actor = Actors.Find(Box.Id))
		{
			Component = (*Actor)->FindComponentByClass<UStaticMeshComponent>();
		}
		const bool bMatchesCanonicalTransform = Component
			&& Component->GetComponentTransform().Equals(BoxTransform(Box), 0.1);
		const bool bMatchesLegacySidewalkTransform = Component
			&& bAllowLegacySidewalkHeight && Box.bGround
			&& Box.Palette == SimCoreVirtualCity::EPalette::Sidewalk
			&& Component->GetComponentTransform().Equals(LegacySidewalkTransform(Box), 0.1);
		if (!Component || !Component->GetStaticMesh() || !Component->GetMaterial(0)
			|| (!bMatchesCanonicalTransform && !bMatchesLegacySidewalkTransform))
		{
			UE_LOG(LogBuildVirtualCity, Error, TEXT("Saved geometry validation failed: %s"), *Box.Id.ToString());
			return false;
		}
		if (Box.bStaticCollider)
		{
			const FName MarkerName(*(FString(TEXT("Collider_")) + Box.Id.ToString()));
			AActor** Actor = Actors.Find(MarkerName);
			ASimCoreStaticCollider* Marker = Actor ? Cast<ASimCoreStaticCollider>(*Actor) : nullptr;
			const ESimCoreStaticColliderSemantic ExpectedSemantic = Box.Palette == SimCoreVirtualCity::EPalette::Curb
				? ESimCoreStaticColliderSemantic::Curb
				: (Box.Palette == SimCoreVirtualCity::EPalette::Barrier
					? ESimCoreStaticColliderSemantic::Barrier : ESimCoreStaticColliderSemantic::Wall);
			const FTransform ExpectedTransform(FRotator(0.0, Box.HeadingDegrees, 0.0), ToWorldCm(Box.CenterEnuM));
			if (!Marker || !Marker->CollisionBounds || !Marker->bExportEnabled
				|| Marker->ColliderId != Box.Id.ToString() || Marker->Semantic != ExpectedSemantic
				|| !Marker->CollisionBounds->GetComponentTransform().Equals(ExpectedTransform, 0.1)
				|| !Marker->CollisionBounds->GetScaledBoxExtent().Equals(Box.SizeM * 50.0, 0.1))
			{
				UE_LOG(LogBuildVirtualCity, Error, TEXT("Saved collider identity/pose/dimensions validation failed: %s"), *Box.Id.ToString());
				return false;
			}
		}
	}
	if (MarkerCount != ExpectedMarkers || GroundComponents.Num() != ExpectedGroundComponents)
	{
		UE_LOG(LogBuildVirtualCity, Error, TEXT("Saved component/collider counts do not match layout."));
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(VirtualCityRouteValidation), false);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (*It != Ground) { Query.AddIgnoredActor(*It); }
	}
	for (int32 Index = 0; Index < Layout.DriveRoute.Num(); ++Index)
	{
		const FVector Position = ToWorldCm(Layout.DriveRoute[Index].PositionEnuM);
		FHitResult Hit;
		if (!World->LineTraceSingleByObjectType(Hit, Position + FVector(0, 0, 5000),
			Position - FVector(0, 0, 5000), FCollisionObjectQueryParams(ECC_WorldStatic), Query)
			|| Hit.GetActor() != Ground || Hit.ImpactNormal.Z < 0.9
			|| FMath::Abs(Hit.ImpactPoint.Z - Position.Z) > 8.0
			|| !Hit.GetComponent() || !Hit.GetComponent()->ComponentHasTag(TEXT("SimCore.Surface.Asphalt")))
		{
			UE_LOG(LogBuildVirtualCity, Error, TEXT("Route checkpoint %d lacks gentle asphalt collision at the authored height."), Index);
			return false;
		}
	}
	UE_LOG(LogBuildVirtualCity, Display,
		TEXT("Saved VirtualCity verified: %d boxes, %d ground components, %d static colliders, %d route checkpoints."),
		Layout.Boxes.Num(), GroundComponents.Num(), MarkerCount, Layout.DriveRoute.Num());
	return true;
}

bool SyncSidewalkCurbHeight(UWorld* World, const SimCoreVirtualCity::FLayout& Layout)
{
	AActor* Ground = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetFName() == GroundName)
		{
			Ground = *It;
			break;
		}
	}
	if (!Ground || !Ground->Tags.Contains(GeneratedTag))
	{
		UE_LOG(LogBuildVirtualCity, Error,
			TEXT("Sidewalk-height sync requires the generated VirtualCityGround actor; no geometry was changed."));
		return false;
	}

	TArray<UStaticMeshComponent*> GroundComponents;
	Ground->GetComponents<UStaticMeshComponent>(GroundComponents);
	TMap<FName, UStaticMeshComponent*> ComponentsByName;
	for (UStaticMeshComponent* Component : GroundComponents)
	{
		if (!Component || ComponentsByName.Contains(Component->GetFName()))
		{
			UE_LOG(LogBuildVirtualCity, Error,
				TEXT("Sidewalk-height sync found a null or duplicate ground component; no geometry was changed."));
			return false;
		}
		ComponentsByName.Add(Component->GetFName(), Component);
	}

	TArray<UStaticMeshComponent*> LegacySidewalks;
	int32 CanonicalSidewalkCount = 0;
	for (const SimCoreVirtualCity::FBox& Box : Layout.Boxes)
	{
		if (!Box.bGround || Box.Palette != SimCoreVirtualCity::EPalette::Sidewalk)
		{
			continue;
		}
		UStaticMeshComponent* const* ComponentPtr = ComponentsByName.Find(Box.Id);
		UStaticMeshComponent* Component = ComponentPtr ? *ComponentPtr : nullptr;
		if (!Component || !Component->GetStaticMesh() || !Component->GetMaterial(0))
		{
			UE_LOG(LogBuildVirtualCity, Error,
				TEXT("Sidewalk-height sync is missing generated component %s; no geometry was changed."),
				*Box.Id.ToString());
			return false;
		}
		if (Component->GetComponentTransform().Equals(BoxTransform(Box), 0.1))
		{
			++CanonicalSidewalkCount;
			continue;
		}
		if (!Component->GetComponentTransform().Equals(LegacySidewalkTransform(Box), 0.1))
		{
			UE_LOG(LogBuildVirtualCity, Error,
				TEXT("Sidewalk-height sync refused edited component %s; no geometry was changed."),
				*Box.Id.ToString());
			return false;
		}
		LegacySidewalks.Add(Component);
	}

	// Mutation begins only after every sidewalk has matched one of the two exact
	// generated transforms, making preflight failure non-destructive.
	for (UStaticMeshComponent* Component : LegacySidewalks)
	{
		const SimCoreVirtualCity::FBox* Box = Layout.Boxes.FindByPredicate(
			[Component](const SimCoreVirtualCity::FBox& Candidate)
			{
				return Candidate.Id == Component->GetFName();
			});
		check(Box);
		Component->Modify();
		Component->SetWorldTransform(BoxTransform(*Box));
		Component->MarkRenderTransformDirty();
	}
	Ground->MarkPackageDirty();
	World->UpdateWorldComponents(true, false);
	UE_LOG(LogBuildVirtualCity, Display,
		TEXT("Sidewalk-height sync raised %d legacy generated sidewalks by %.1f cm; %d were already canonical."),
		LegacySidewalks.Num(), LegacySidewalkToCurbDeltaCm, CanonicalSidewalkCount);
	return true;
}

bool SyncSignalCityTrafficLanes(UWorld* World, const SimCoreVirtualCity::FLayout& Layout,
	const FString& MapFilename)
{
	TMap<FName, AActor*> Actors;
	for (TActorIterator<AActor> It(World); It; ++It) Actors.Add(It->GetFName(), *It);
	AActor* Ground = Actors.FindRef(GroundName);
	if (!Ground || !Ground->Tags.Contains(GeneratedTag)) return false;
	if (Ground->Tags.Contains(AlignedCollectorMarkingsTag)) return ValidateScene(World, Layout);
	const auto Legacy = Ground->Tags.Contains(NaturalLaneMarkingsV2Tag)
		? SimCoreSignalCity::BuildNaturalLaneMarkingsV2Layout()
		: (Ground->Tags.Contains(CompleteLaneMarkingsTag)
		? SimCoreSignalCity::BuildRaisedLaneMarkingsLayout()
		: (Ground->Tags.Contains(SafeCrossingsTag)
			? SimCoreSignalCity::BuildIncompleteLaneMarkingsLayout()
			: (Ground->Tags.Contains(ThreeLaneTag)
				? SimCoreSignalCity::BuildInitialThreeLaneLayout()
				: SimCoreSignalCity::BuildLegacySingleLaneLayout())));
	// This migration accepts only the exact last generated layout. User-edited
	// roads, colliders or name collisions are refused before backup/mutation.
	if (!ValidateScene(World, Legacy)) return false;
	TArray<UStaticMeshComponent*> GroundComponents;
	Ground->GetComponents(GroundComponents);
	TMap<FName, UStaticMeshComponent*> GroundByName;
	for (UStaticMeshComponent* Component : GroundComponents) GroundByName.Add(Component->GetFName(), Component);
	TSet<FName> DesiredActorNames;
	for (const auto& Box : Layout.Boxes)
	{
		if (!Box.bGround) DesiredActorNames.Add(Box.Id);
	}
	TArray<AActor*> RetiredGeneratedActors;
	for (const auto& Box : Legacy.Boxes)
	{
		if (Box.bGround || DesiredActorNames.Contains(Box.Id)) continue;
		AActor* Stale = Actors.FindRef(Box.Id);
		if (!Stale || !Stale->Tags.Contains(GeneratedTag))
		{
			UE_LOG(LogBuildVirtualCity, Error,
				TEXT("Traffic-lane sync refused stale unowned geometry: %s"),
				*Box.Id.ToString());
			return false;
		}
		RetiredGeneratedActors.Add(Stale);
	}
	for (const auto& Box : Layout.Boxes)
	{
		AActor* Existing = Actors.FindRef(Box.Id);
		if ((!Box.bGround && Existing && !Existing->Tags.Contains(GeneratedTag))
			|| (Box.bGround && !GroundByName.Contains(Box.Id)))
		{
			UE_LOG(LogBuildVirtualCity, Error, TEXT("Traffic-lane sync refused unowned/missing geometry: %s"), *Box.Id.ToString());
			return false;
		}
		if (Box.bStaticCollider)
		{
			AActor* Marker = Actors.FindRef(FName(*(TEXT("Collider_") + Box.Id.ToString())));
			if (!Marker || !Marker->Tags.Contains(GeneratedTag)) return false;
		}
	}
	const FString Backup = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Backups"))
		/ (TEXT("SignalCityTraffic-") + FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))
			+ TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	if (!IFileManager::Get().MakeDirectory(*Backup, true)
		|| IFileManager::Get().Copy(*(Backup / TEXT("L_SignalCity.umap")), *MapFilename, false) != COPY_OK) return false;
	const FString PackageDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), PackageRelative);
	for (const TCHAR* Name : {TEXT("manifest.cfg"), TEXT("static_colliders.csv"), TEXT("ground_surface.csv"),
		TEXT("ground_heightfield.bin"), TEXT("drive_route.csv"), TEXT("traffic_network.json")})
	{
		const FString Source = PackageDirectory / Name;
		if (IFileManager::Get().FileExists(*Source)
			&& IFileManager::Get().Copy(*(Backup / Name), *Source, false) != COPY_OK) return false;
	}
	UE_LOG(LogBuildVirtualCity, Display, TEXT("Original map and map-package backup: %s"), *Backup);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) return false;
	for (const auto& Box : Layout.Boxes)
	{
		if (Box.bGround)
		{
			UStaticMeshComponent* Component = GroundByName.FindRef(Box.Id);
			Component->Modify();
			Component->SetWorldTransform(BoxTransform(Box));
		}
		else
		{
			AStaticMeshActor* Actor = Cast<AStaticMeshActor>(Actors.FindRef(Box.Id));
			if (!Actor)
			{
				Actor = SpawnNamed<AStaticMeshActor>(World, Box.Id);
				UMaterial* Material = CreatePaletteMaterial(Box.Palette);
				if (!Actor || !Material) return false;
				ConfigureMesh(Actor->GetStaticMeshComponent(), Cube, Material, Box);
				Actor->SetFolderPath(Box.Id.ToString().StartsWith(TEXT("Backdrop_"))
					? TEXT("SignalCity/DistantBackdrop") : TEXT("SignalCity/LaneMarkings"));
			}
			Actor->Modify();
			Actor->SetActorTransform(BoxTransform(Box));
		}
		if (Box.bStaticCollider)
		{
			auto* Marker = Cast<ASimCoreStaticCollider>(Actors.FindRef(FName(*(TEXT("Collider_") + Box.Id.ToString()))));
			if (!Marker) return false;
			Marker->Modify();
			Marker->CollisionBounds->SetBoxExtent(Box.SizeM * 50.0);
			Marker->SetActorLocationAndRotation(ToWorldCm(Box.CenterEnuM), FRotator(0, Box.HeadingDegrees, 0));
		}
	}
	for (AActor* Actor : RetiredGeneratedActors)
	{
		if (!World->DestroyActor(Actor, false, true))
		{
			UE_LOG(LogBuildVirtualCity, Error,
				TEXT("Traffic-lane sync could not retire generated marking %s."),
				*Actor->GetName());
			return false;
		}
	}
	Ground->Tags.AddUnique(ThreeLaneTag);
	Ground->Tags.AddUnique(SafeCrossingsTag);
	Ground->Tags.AddUnique(CompleteLaneMarkingsTag);
	Ground->Tags.AddUnique(NaturalLaneMarkingsV2Tag);
	Ground->Tags.AddUnique(AlignedCollectorMarkingsTag);
	Ground->MarkPackageDirty();
	World->UpdateWorldComponents(true, false);
	UE_LOG(LogBuildVirtualCity, Display,
		TEXT("Synced aligned collector paint, natural road paint, 10m collectors and visual-only distant backdrop; retired %d obsolete generated marking actors."),
		RetiredGeneratedActors.Num());
	return ValidateScene(World, Layout);
}

bool WriteRoute(const SimCoreVirtualCity::FLayout& Layout)
{
	FString Csv = TEXT("# Authoring QA checkpoints only; not a LaneGraph, navigation or traffic implementation.\neast_m,north_m,up_m,heading_deg\n");
	for (int32 Index = 0; Index < Layout.DriveRoute.Num(); ++Index)
	{
		const auto& Checkpoint = Layout.DriveRoute[Index];
		Csv += FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f\n"),
			Checkpoint.PositionEnuM.X, Checkpoint.PositionEnuM.Y, Checkpoint.PositionEnuM.Z,
			Checkpoint.HeadingDegrees);
	}
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(),
		FString(PackageRelative) + TEXT("/drive_route.csv"));
	return FFileHelper::SaveStringToFile(Csv, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

UBuildVirtualCityCommandlet::UBuildVirtualCityCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UBuildVirtualCityCommandlet::Main(const FString& Params)
{
	const bool bSignalCity = FParse::Param(*Params, TEXT("SignalCity"));
	if (bSignalCity)
	{
		MapAsset = TEXT("/Game/SignalCity/Maps/L_SignalCity");
		PackageRelative = TEXT("../../map_packages/signal_city_v2");
		ExpectedMapId = TEXT("signal_city_v2");
		GroundName = TEXT("SignalCityGround");
		ExporterName = TEXT("SignalCityGroundExporter");
		GeneratedTag = TEXT("SimCore.SignalCity.Generated.v2");
		GameModeClassPath = TEXT("/Script/DriveIntegration.SignalCityGameMode");
		GameModeObjectName = TEXT("SignalCityGameMode");
		PlayerStartName = TEXT("SignalCityPlayerStart");
	}
	else
	{
		MapAsset = TEXT("/Game/VirtualCity/Maps/L_VirtualCity");
		PackageRelative = TEXT("../../map_packages/virtual_city_v1");
		ExpectedMapId = TEXT("virtual_city_v1");
		GroundName = TEXT("VirtualCityGround");
		ExporterName = TEXT("VirtualCityGroundExporter");
		GeneratedTag = TEXT("SimCore.VirtualCity.Generated.v1");
		GameModeClassPath = TEXT("/Script/DriveIntegration.VirtualCityGameMode");
		GameModeObjectName = TEXT("VirtualCityGameMode");
		PlayerStartName = TEXT("VirtualCityPlayerStart");
	}
	const bool bValidateOnly = FParse::Param(*Params, TEXT("ValidateOnly"));
	const bool bBakeOnly = FParse::Param(*Params, TEXT("BakeOnly"));
	const bool bSyncGeneratedGround = FParse::Param(*Params, TEXT("SyncGeneratedGround"));
	const bool bSyncHeightfieldResolution =
		FParse::Param(*Params, TEXT("SyncHeightfieldResolution"));
	const bool bSyncSidewalkCurbHeight =
		FParse::Param(*Params, TEXT("SyncSidewalkCurbHeight"));
	const bool bSyncTrafficLanes = FParse::Param(*Params, TEXT("SyncTrafficLanes"));
	const int32 ExplicitModeCount = static_cast<int32>(bValidateOnly)
		+ static_cast<int32>(bBakeOnly) + static_cast<int32>(bSyncGeneratedGround)
		+ static_cast<int32>(bSyncHeightfieldResolution)
		+ static_cast<int32>(bSyncSidewalkCurbHeight) + static_cast<int32>(bSyncTrafficLanes);
	if (ExplicitModeCount > 1 || FParse::Param(*Params, TEXT("Replace"))
		|| (bSyncTrafficLanes && !bSignalCity)
		|| (bSignalCity && (bSyncGeneratedGround || bSyncHeightfieldResolution
			|| bSyncSidewalkCurbHeight)))
	{
		UE_LOG(LogBuildVirtualCity, Error,
			TEXT("Choose one mode. -SignalCity -SyncTrafficLanes is a guarded migration with backup; legacy -Sync* modes apply to virtual_city_v1. General replacement is unsupported."));
		return 1;
	}
	const FString MapFilename = FPackageName::LongPackageNameToFilename(MapAsset,
		FPackageName::GetMapPackageExtension());
	const bool bMapExists = IFileManager::Get().FileExists(*MapFilename);
	const bool bExistingMapMode = bValidateOnly || bBakeOnly
		|| bSyncGeneratedGround || bSyncHeightfieldResolution
		|| bSyncSidewalkCurbHeight || bSyncTrafficLanes;
	if (!GEditor || ((!bExistingMapMode) && bMapExists)
		|| (bExistingMapMode && !bMapExists))
	{
		UE_LOG(LogBuildVirtualCity, Error,
			TEXT("Creation requires an absent target map; validation/bake require an existing one. No map was overwritten: %s"), *MapFilename);
		return 1;
	}
	const SimCoreVirtualCity::FLayout Layout = bSignalCity
		? SimCoreSignalCity::BuildLayout() : SimCoreVirtualCity::BuildLayout();
	UWorld* World = nullptr;
	if (bExistingMapMode)
	{
		World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
	}
	else
	{
		World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
		if (!World || !BuildScene(World, Layout)
			|| !UEditorLoadingAndSavingUtils::SaveMap(World, MapAsset)) { return 1; }
		World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
	}
	if (!World) { return 1; }
	if (bSyncTrafficLanes)
	{
		if (!SyncSignalCityTrafficLanes(World, Layout, MapFilename)
			|| !UEditorLoadingAndSavingUtils::SaveMap(World, MapAsset)) return 1;
		World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
		if (!World) return 1;
	}
	if (bSyncGeneratedGround)
	{
		if (!SyncGeneratedGroundComponents(World, Layout)
			|| !UEditorLoadingAndSavingUtils::SaveMap(World, MapAsset))
		{
			return 1;
		}
		World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
		if (!World) { return 1; }
	}
	if (bSyncHeightfieldResolution)
	{
		// This mode is a targeted exporter-property migration, not a geometry
		// replacement. Validate the saved authored scene before mutating it.
		if (!ValidateScene(World, Layout)
			|| !SyncHeightfieldResolution(World, Layout)
			|| !UEditorLoadingAndSavingUtils::SaveMap(World, MapAsset))
		{
			return 1;
		}
		World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
		if (!World) { return 1; }
	}
	if (bSyncSidewalkCurbHeight)
	{
		// Accept only the exact generated 16 cm legacy state (or an already
		// canonical component for idempotent recovery), then mutate sidewalks
		// alone. All other scene geometry and collider identity remain strict.
		if (!ValidateScene(World, Layout, true)
			|| !SyncSidewalkCurbHeight(World, Layout)
			|| !UEditorLoadingAndSavingUtils::SaveMap(World, MapAsset))
		{
			return 1;
		}
		World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
		if (!World) { return 1; }
	}
	World->UpdateWorldComponents(true, false);
	if (!bBakeOnly && !ValidateScene(World, Layout)) { return 1; }
	if (!bValidateOnly)
	{
		AGroundCollisionExporter* Exporter = nullptr;
		for (TActorIterator<AGroundCollisionExporter> It(World); It; ++It)
		{
			if (It->GetFName() == ExporterName) { Exporter = *It; break; }
		}
		if (!Exporter || !Exporter->ExportGroundSurfaceUnattended()) { return 1; }
		if (!bBakeOnly && !WriteRoute(Layout)) { return 1; }
	}
	SimCoreMapPackage::FManifest Manifest;
	FString Error;
	if (!SimCoreMapPackage::LoadAndVerifyManifest(PackageRelative, Manifest, Error)
		|| Manifest.MapId != ExpectedMapId)
	{
		UE_LOG(LogBuildVirtualCity, Error,
			TEXT("MapPackage validation failed or map identity mismatched: expected=%s actual=%s error=%s"),
			ExpectedMapId, *Manifest.MapId, *Error);
		return 1;
	}
	UE_LOG(LogBuildVirtualCity, Display, TEXT("Versioned city ready: map=%s package=%s checksum=%s mode=%s"),
		MapAsset, *Manifest.MapId, *Manifest.CollisionChecksum,
		bValidateOnly ? TEXT("validate") : (bBakeOnly ? TEXT("bake-existing")
			: (bSyncGeneratedGround ? TEXT("sync-ground-and-bake")
				: (bSyncHeightfieldResolution ? TEXT("sync-heightfield-and-bake")
					: (bSyncSidewalkCurbHeight ? TEXT("sync-sidewalk-height-and-bake")
						: (bSyncTrafficLanes ? TEXT("sync-traffic-lanes-and-bake")
							: TEXT("create")))))));
	return 0;
}
