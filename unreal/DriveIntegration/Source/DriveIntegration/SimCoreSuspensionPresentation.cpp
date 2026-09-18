#include "SimCoreSuspensionPresentation.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

bool SimCoreSuspensionPresentation::LinkTransform(const FVector& Start, const FVector& End,
	const float RadiusCm, FTransform& OutTransform)
{
	const FVector Direction = End - Start;
	if (Start.ContainsNaN() || End.ContainsNaN() || !FMath::IsFinite(RadiusCm)
		|| RadiusCm <= 0.0f || Direction.SizeSquared() < 0.01) return false;
	OutTransform = FTransform(FRotationMatrix::MakeFromZ(Direction).ToQuat(),
		(Start + End) * 0.5, FVector(RadiusCm / 50.0, RadiusCm / 50.0, Direction.Size() / 100.0));
	return true;
}

USimCoreSuspensionPresentation::USimCoreSuspensionPresentation()
{
	SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Cylinder = Mesh.Object;
	BaseMaterial = Material.Object;
	WheelHubs.SetNum(4);
	SimCoreVehicleVisualProfile::Resolve(SimCoreProtocol::ERuntimeVehicleClass::Sedan, Profile);
}

void USimCoreSuspensionPresentation::EnsureParts()
{
	if (!GetOwner() || !Cylinder || !Links.IsEmpty()) return;
	auto* Steel = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	auto* Damper = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (Steel) Steel->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.11f, 0.13f, 0.15f));
	if (Damper) Damper->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.34f, 0.09f, 0.025f));
	for (int32 Index = 0; Index < 18; ++Index)
	{
		auto* Part = NewObject<UStaticMeshComponent>(GetOwner(),
			*FString::Printf(TEXT("SuspensionLink%d"), Index));
		Part->SetupAttachment(this);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetStaticMesh(Cylinder);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetMaterial(0, Index >= 4 && (Index - 4) % 3 == 2 ? Damper : Steel);
		GetOwner()->AddInstanceComponent(Part);
		Part->RegisterComponent();
		Links.Add(Part);
	}
}

void USimCoreSuspensionPresentation::Configure(const SimCoreProtocol::ERuntimeVehicleClass VehicleClass)
{
	if (!SimCoreVehicleVisualProfile::Resolve(VehicleClass, Profile)) return;
	EnsureParts();
	UpdateLinks();
}

void USimCoreSuspensionPresentation::SetWheelHub(const int32 Index, USceneComponent* Hub)
{
	if (WheelHubs.IsValidIndex(Index)) WheelHubs[Index] = Hub;
}

void USimCoreSuspensionPresentation::PlaceLink(const int32 Index, const FVector& Start,
	const FVector& End, const float RadiusCm)
{
	if (!Links.IsValidIndex(Index)) return;
	FTransform Transform;
	const bool bValid = SimCoreSuspensionPresentation::LinkTransform(Start, End, RadiusCm, Transform);
	Links[Index]->SetVisibility(bValid);
	if (bValid) Links[Index]->SetRelativeTransform(Transform);
}

void USimCoreSuspensionPresentation::UpdateLinks()
{
	if (Links.IsEmpty()) return;
	for (UStaticMeshComponent* Part : Links) Part->SetVisibility(false);
	const bool bBike = Profile.VehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Motorcycle;
	const bool bTruck = Profile.VehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Truck;
	FVector Hub[4];
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Hub[Index] = WheelHubs[Index]
			? GetComponentTransform().InverseTransformPosition(WheelHubs[Index]->GetComponentLocation())
			: Profile.WheelOriginsCm[Index];
	}
	if (bBike)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const double Across = Side == 0 ? -7.0 : 7.0;
			PlaceLink(Side, FVector(78, Across, 61), Hub[0] + FVector(0, Across, 0), 2.8f);
			PlaceLink(2 + Side, FVector(17, Across, 20), Hub[2] + FVector(0, Across, 0), 3.2f);
		}
		PlaceLink(4, FVector(-20, 0, 49), FMath::Lerp(FVector(17, 0, 20), Hub[2], 0.6), 4.5f);
		return;
	}
	const double RailY = FMath::Abs(Profile.WheelOriginsCm[0].Y) * (bTruck ? 0.62 : 0.57);
	const double RailZ = Profile.WheelOriginsCm[0].Z + 18.0;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const double Across = Side == 0 ? -RailY : RailY;
		PlaceLink(Side, FVector(Profile.WheelOriginsCm[0].X, Across, RailZ),
			FVector(Profile.WheelOriginsCm[2].X, Across, RailZ), bTruck ? 6.0f : 3.5f);
	}
	PlaceLink(2, FVector(Profile.WheelOriginsCm[0].X, -RailY, RailZ),
		FVector(Profile.WheelOriginsCm[0].X, RailY, RailZ), bTruck ? 5.0f : 3.5f);
	PlaceLink(3, FVector(Profile.WheelOriginsCm[2].X, -RailY, RailZ),
		FVector(Profile.WheelOriginsCm[2].X, RailY, RailZ), bTruck ? 5.0f : 3.5f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector Rest = Profile.WheelOriginsCm[Index];
		const double Side = FMath::Sign(Rest.Y);
		const FVector Inner(Rest.X, Side * RailY, RailZ);
		const FVector Knuckle = Hub[Index] + FVector(0, -Side * 7.0, 0);
		PlaceLink(4 + Index * 3, Inner + FVector(-16, 0, -7), Knuckle, bTruck ? 4.0f : 2.6f);
		PlaceLink(5 + Index * 3, Inner + FVector(16, 0, -7), Knuckle, bTruck ? 4.0f : 2.6f);
		PlaceLink(6 + Index * 3, FVector(Rest.X, Rest.Y * 0.82, Rest.Z + (bTruck ? 54.0 : 38.0)),
			Knuckle, bTruck ? 5.5f : 4.0f);
	}
	if (bTruck)
	{
		PlaceLink(16, Hub[0], Hub[1], 6.0f);
		PlaceLink(17, Hub[2], Hub[3], 7.0f);
	}
}

int32 USimCoreSuspensionPresentation::GetVisibleLinkCount() const
{
	int32 Count = 0;
	for (UStaticMeshComponent* Part : Links) if (Part && Part->IsVisible()) ++Count;
	return Count;
}
