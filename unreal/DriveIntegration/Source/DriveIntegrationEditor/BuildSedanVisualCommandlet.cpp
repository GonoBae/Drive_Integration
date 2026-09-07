#include "BuildSedanVisualCommandlet.h"
#include "SimCoreSedanVisualContract.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "UObject/MetaData.h"
#include "UObject/SavePackage.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogBuildSedanVisual, Log, All);

namespace
{
constexpr const TCHAR* AssetRoot = TEXT("/Game/Vehicles/Sedan/");
constexpr const TCHAR* FleetAssetRoot = TEXT("/Game/Vehicles/NpcFleet/");
constexpr const TCHAR* AuthorKey = TEXT("SimCore.GeneratedVisual");
constexpr const TCHAR* LegacyAuthorValue = TEXT("SelfAuthoredSedanV1");
constexpr const TCHAR* AuthorValue = TEXT("SelfAuthoredSedanV2");
constexpr float MaxDentDepthCm = 12.0f;
constexpr float GlassOpacity = 0.24f;
constexpr const TCHAR* GlassOpacityDescription = TEXT("Sedan transparent glazing opacity");
constexpr double DriverDoorRearX = -46.0;
constexpr double DriverDoorFrontX = 63.0;
constexpr double DriverDoorLowerV = 0.07;
const FName DentParameterNames[] = {
	TEXT("DentFront"),
	TEXT("DentRear"),
	TEXT("DentLeft"),
	TEXT("DentRight"),
	TEXT("DentRoof"),
	TEXT("DentUnderbody"),
};
enum EFinish : int32 { Paint, Glass, Rubber, Alloy, Black, Headlamp, TailLamp, Amber, Chrome, Plate, FinishCount };

struct FFinish
{
	const TCHAR* Name;
	FLinearColor Color;
	float Metallic;
	float Roughness;
	float Emission;
};

const FFinish Finishes[] = {
	{ TEXT("M_Sedan_Paint"), FLinearColor(0.018f, 0.105f, 0.27f), 0.82f, 0.24f, 0.0f },
	{ TEXT("M_Sedan_Glass"), FLinearColor(0.018f, 0.035f, 0.052f), 0.38f, 0.12f, 0.0f },
	{ TEXT("M_Sedan_Rubber"), FLinearColor(0.016f, 0.018f, 0.021f), 0.0f, 0.82f, 0.0f },
	{ TEXT("M_Sedan_Alloy"), FLinearColor(0.53f, 0.56f, 0.60f), 0.90f, 0.25f, 0.0f },
	{ TEXT("M_Sedan_BlackTrim"), FLinearColor(0.007f, 0.009f, 0.012f), 0.12f, 0.47f, 0.0f },
	{ TEXT("M_Sedan_Headlamp"), FLinearColor(0.84f, 0.91f, 1.0f), 0.22f, 0.19f, 0.32f },
	{ TEXT("M_Sedan_TailLamp"), FLinearColor(0.42f, 0.004f, 0.008f), 0.15f, 0.21f, 0.18f },
	{ TEXT("M_Sedan_Amber"), FLinearColor(0.94f, 0.24f, 0.008f), 0.10f, 0.25f, 0.14f },
	{ TEXT("M_Sedan_Chrome"), FLinearColor(0.73f, 0.76f, 0.81f), 0.96f, 0.16f, 0.0f },
	{ TEXT("M_Sedan_Plate"), FLinearColor(0.72f, 0.75f, 0.73f), 0.03f, 0.57f, 0.0f }
};

bool IsAuthoredAsset(const UObject* Asset)
{
	if (!Asset || !Asset->GetOutermost())
	{
		return false;
	}
	const FString Value = Asset->GetOutermost()->GetMetaData().GetValue(
		Asset, AuthorKey);
	return Value == AuthorValue || Value == LegacyAuthorValue;
}

bool SaveAuthoredAsset(UObject* Asset)
{
	UPackage* Package = Asset->GetOutermost();
	Package->GetMetaData().SetValue(Asset, AuthorKey, AuthorValue);
	Asset->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(),
		FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

bool ValidateDentMaterial(const UMaterial* Material)
{
	if (!Material
		|| !FMath::IsNearlyEqual(
			Material->MaxWorldPositionOffsetDisplacement,
			MaxDentDepthCm,
			KINDA_SMALL_NUMBER))
	{
		return false;
	}
	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterIds;
	Material->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
	for (const FName Name : DentParameterNames)
	{
		if (!ParameterInfos.ContainsByPredicate([Name](const FMaterialParameterInfo& Info)
		{
			return Info.Name == Name
				&& Info.Association == EMaterialParameterAssociation::GlobalParameter
				&& Info.Index == INDEX_NONE;
		}))
		{
			return false;
		}
	}
	return true;
}

bool HasAnyDentGraph(const UMaterial* Material)
{
	if (!Material)
	{
		return false;
	}
	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterIds;
	Material->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		for (const FName DentName : DentParameterNames)
		{
			if (Info.Name == DentName)
			{
				return true;
			}
		}
	}
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
		if (Custom && Custom->Description == TEXT("Bounded body-local collision dents"))
		{
			return true;
		}
	}
	return false;
}

bool ValidateIndicatorMaterial(const UMaterial* Material)
{
	if (!Material) return false;
	float Value = 0.0f;
	return Material->GetScalarParameterValue(FMaterialParameterInfo(SimCoreSedanVisualContract::SignalLeftParameter), Value)
		&& Material->GetScalarParameterValue(FMaterialParameterInfo(SimCoreSedanVisualContract::SignalRightParameter), Value);
}

bool ValidateTransparentGlass(const UMaterial* Material)
{
	if (!Material || Material->GetBlendMode() != BLEND_Translucent
		|| !Material->IsTwoSided())
	{
		return false;
	}
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		const auto* Opacity = Cast<UMaterialExpressionConstant>(Expression);
		if (Opacity && Opacity->Desc == GlassOpacityDescription
			&& FMath::IsNearlyEqual(Opacity->R, GlassOpacity, KINDA_SMALL_NUMBER))
		{
			return true;
		}
	}
	return false;
}

void ConfigureTransparentGlass(UMaterial* Material)
{
	Material->Modify();
	Material->BlendMode = BLEND_Translucent;
	Material->TwoSided = true;
	Material->SetShadingModel(MSM_DefaultLit);
	UMaterialExpressionConstant* Opacity = nullptr;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		auto* Candidate = Cast<UMaterialExpressionConstant>(Expression);
		if (Candidate && Candidate->Desc == GlassOpacityDescription)
		{
			Opacity = Candidate;
			break;
		}
	}
	if (!Opacity)
	{
		Opacity = CastChecked<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionConstant::StaticClass(), -300, 300));
		Opacity->Desc = GlassOpacityDescription;
	}
	Opacity->R = GlassOpacity;
	UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);
}

bool AddIndicatorGraph(UMaterial* Material)
{
	if (ValidateIndicatorMaterial(Material)) return true;
	// Only the owned Amber emission input changes. Keep its base finish and six
	// damage parameters, and refuse to append over a partially authored graph.
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		const auto* Parameter = Cast<UMaterialExpressionScalarParameter>(Expression);
		if (Parameter && (Parameter->ParameterName == SimCoreSedanVisualContract::SignalLeftParameter
			|| Parameter->ParameterName == SimCoreSedanVisualContract::SignalRightParameter)) return false;
	}
	Material->Modify();
	auto* WorldPosition = CastChecked<UMaterialExpressionWorldPosition>(UMaterialEditingLibrary::CreateMaterialExpression(
		Material, UMaterialExpressionWorldPosition::StaticClass(), -1100, -600));
	WorldPosition->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;
	auto* LocalPosition = CastChecked<UMaterialExpressionTransformPosition>(UMaterialEditingLibrary::CreateMaterialExpression(
		Material, UMaterialExpressionTransformPosition::StaticClass(), -900, -600));
	LocalPosition->Input.Expression = WorldPosition;
	LocalPosition->TransformSourceType = TRANSFORMPOSSOURCE_World;
	LocalPosition->TransformType = TRANSFORMPOSSOURCE_Local;
	auto* Emission = CastChecked<UMaterialExpressionCustom>(UMaterialEditingLibrary::CreateMaterialExpression(
		Material, UMaterialExpressionCustom::StaticClass(), -300, -600));
	Emission->Description = TEXT("Authored left/right turn signal lenses");
	Emission->OutputType = CMOT_Float3;
	Emission->Code = TEXT("float On = LocalPosition.y < 0.0 ? SignalLeftOn : SignalRightOn;\nreturn float3(1.0, 0.22, 0.003) * (0.03 + 8.0 * saturate(On));");
	Emission->Inputs.SetNum(3);
	Emission->Inputs[0].InputName = TEXT("LocalPosition");
	Emission->Inputs[0].Input.Expression = LocalPosition;
	const FName SignalNames[] = { SimCoreSedanVisualContract::SignalLeftParameter, SimCoreSedanVisualContract::SignalRightParameter };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		auto* Parameter = CastChecked<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionScalarParameter::StaticClass(), -700, -500 + Index * 100));
		Parameter->ParameterName = SignalNames[Index];
		Parameter->DefaultValue = 0.0f;
		Parameter->SliderMin = 0.0f;
		Parameter->SliderMax = 1.0f;
		Parameter->Group = TEXT("Turn Signals");
		Emission->Inputs[Index + 1].InputName = SignalNames[Index];
		Emission->Inputs[Index + 1].Input.Expression = Parameter;
	}
	UMaterialEditingLibrary::ConnectMaterialProperty(Emission, TEXT(""), MP_EmissiveColor);
	UMaterialEditingLibrary::RecompileMaterial(Material);
	return ValidateIndicatorMaterial(Material);
}

void BuildMaterialGraph(
	UMaterial* Material,
	const FFinish& Finish,
	const bool bCreateBaseProperties)
{
	Material->MaxWorldPositionOffsetDisplacement = MaxDentDepthCm;
	if (FName(Finish.Name) == FName(Finishes[Glass].Name))
	{
		ConfigureTransparentGlass(Material);
	}
	if (bCreateBaseProperties)
	{
		UMaterialExpressionConstant3Vector* Color = CastChecked<UMaterialExpressionConstant3Vector>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material,
				UMaterialExpressionConstant3Vector::StaticClass(), -300, -150));
		Color->Constant = Finish.Color;
		UMaterialEditingLibrary::ConnectMaterialProperty(Color, TEXT(""), MP_BaseColor);
		auto Scalar = [Material](float Value, EMaterialProperty Property, int32 Y)
		{
			UMaterialExpressionConstant* Expression = CastChecked<UMaterialExpressionConstant>(
				UMaterialEditingLibrary::CreateMaterialExpression(Material,
					UMaterialExpressionConstant::StaticClass(), -300, Y));
			Expression->R = Value;
			UMaterialEditingLibrary::ConnectMaterialProperty(Expression, TEXT(""), Property);
		};
		Scalar(Finish.Metallic, MP_Metallic, 0);
		Scalar(Finish.Roughness, MP_Roughness, 100);
		if (Finish.Emission > 0.0f)
		{
			UMaterialExpressionConstant3Vector* Emissive = CastChecked<UMaterialExpressionConstant3Vector>(
				UMaterialEditingLibrary::CreateMaterialExpression(Material,
					UMaterialExpressionConstant3Vector::StaticClass(), -300, 200));
			Emissive->Constant = Finish.Color * Finish.Emission;
			UMaterialEditingLibrary::ConnectMaterialProperty(Emissive, TEXT(""), MP_EmissiveColor);
		}
	}

	UMaterialExpressionWorldPosition* WorldPosition =
		CastChecked<UMaterialExpressionWorldPosition>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionWorldPosition::StaticClass(), -1100, 420));
	WorldPosition->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;
	UMaterialExpressionTransformPosition* LocalPosition =
		CastChecked<UMaterialExpressionTransformPosition>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionTransformPosition::StaticClass(), -880, 420));
	LocalPosition->Input.Expression = WorldPosition;
	LocalPosition->TransformSourceType = TRANSFORMPOSSOURCE_World;
	LocalPosition->TransformType = TRANSFORMPOSSOURCE_Local;

	UMaterialExpressionCustom* LocalDent = CastChecked<UMaterialExpressionCustom>(
		UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionCustom::StaticClass(), -300, 420));
	LocalDent->Description = TEXT("Bounded body-local collision dents");
	LocalDent->OutputType = CMOT_Float3;
	LocalDent->Code = TEXT(
		"float3 Offset = float3(0.0, 0.0, 0.0);\n"
		"float3 Q = (LocalPosition - float3(200.0, 0.0, 0.0)) / float3(105.0, 115.0, 65.0);\n"
		"float F = saturate(1.0 - dot(Q, Q)); F = F * F * (3.0 - 2.0 * F);\n"
		"Offset += float3(-12.0 * DentFront * F, 0.0, 0.0);\n"
		"Q = (LocalPosition - float3(-228.0, 0.0, -2.0)) / float3(105.0, 115.0, 65.0);\n"
		"F = saturate(1.0 - dot(Q, Q)); F = F * F * (3.0 - 2.0 * F);\n"
		"Offset += float3(12.0 * DentRear * F, 0.0, 0.0);\n"
		"Q = (LocalPosition - float3(0.0, -90.0, 5.0)) / float3(155.0, 35.0, 70.0);\n"
		"F = saturate(1.0 - dot(Q, Q)); F = F * F * (3.0 - 2.0 * F);\n"
		"Offset += float3(0.0, 12.0 * DentLeft * F, 0.0);\n"
		"Q = (LocalPosition - float3(0.0, 90.0, 5.0)) / float3(155.0, 35.0, 70.0);\n"
		"F = saturate(1.0 - dot(Q, Q)); F = F * F * (3.0 - 2.0 * F);\n"
		"Offset += float3(0.0, -12.0 * DentRight * F, 0.0);\n"
		"Q = (LocalPosition - float3(-25.0, 0.0, 93.0)) / float3(125.0, 82.0, 38.0);\n"
		"F = saturate(1.0 - dot(Q, Q)); F = F * F * (3.0 - 2.0 * F);\n"
		"Offset += float3(0.0, 0.0, -12.0 * DentRoof * F);\n"
		"Q = (LocalPosition - float3(-15.0, 0.0, -28.0)) / float3(155.0, 72.0, 24.0);\n"
		"F = saturate(1.0 - dot(Q, Q)); F = F * F * (3.0 - 2.0 * F);\n"
		"Offset += float3(0.0, 0.0, 12.0 * DentUnderbody * F);\n"
		"float Magnitude = length(Offset);\n"
		"return Magnitude > 12.0 ? Offset * (12.0 / Magnitude) : Offset;");
	LocalDent->Inputs.SetNum(1 + UE_ARRAY_COUNT(DentParameterNames));
	LocalDent->Inputs[0].InputName = TEXT("LocalPosition");
	LocalDent->Inputs[0].Input.Expression = LocalPosition;
	for (int32 ParameterIndex = 0;
		ParameterIndex < UE_ARRAY_COUNT(DentParameterNames);
		++ParameterIndex)
	{
		UMaterialExpressionScalarParameter* Parameter =
			CastChecked<UMaterialExpressionScalarParameter>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material,
					UMaterialExpressionScalarParameter::StaticClass(),
					-650,
					600 + ParameterIndex * 90));
		Parameter->ParameterName = DentParameterNames[ParameterIndex];
		Parameter->DefaultValue = 0.0f;
		Parameter->SliderMin = 0.0f;
		Parameter->SliderMax = 1.0f;
		Parameter->Group = TEXT("Collision Dent");
		Parameter->SortPriority = ParameterIndex;
		LocalDent->Inputs[ParameterIndex + 1].InputName =
			DentParameterNames[ParameterIndex];
		LocalDent->Inputs[ParameterIndex + 1].Input.Expression = Parameter;
	}

	UMaterialExpressionTransform* WorldDent = CastChecked<UMaterialExpressionTransform>(
		UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionTransform::StaticClass(), 80, 420));
	WorldDent->Input.Expression = LocalDent;
	WorldDent->TransformSourceType = TRANSFORMSOURCE_Local;
	WorldDent->TransformType = TRANSFORM_World;
	UMaterialEditingLibrary::ConnectMaterialProperty(
		WorldDent, TEXT(""), MP_WorldPositionOffset);
	UMaterialEditingLibrary::RecompileMaterial(Material);
}

UMaterial* MakeMaterial(int32 Index, bool bValidateOnly, bool bRegenerate)
{
	const FFinish& Finish = Finishes[Index];
	const FString Path = FString(AssetRoot) + TEXT("Materials/") + Finish.Name;
	UMaterial* Material = nullptr;
	const bool bExisting = FPackageName::DoesPackageExist(Path);
	if (bExisting)
	{
		Material = LoadObject<UMaterial>(nullptr, *(Path + TEXT(".") + Finish.Name));
		if (!Material)
		{
			return nullptr;
		}
		if (ValidateDentMaterial(Material))
		{
			if (Index == Glass && !ValidateTransparentGlass(Material))
			{
				if (bValidateOnly || !bRegenerate || !IsAuthoredAsset(Material))
				{
					return nullptr;
				}
				ConfigureTransparentGlass(Material);
				UMaterialEditingLibrary::RecompileMaterial(Material);
				if (!ValidateTransparentGlass(Material) || !SaveAuthoredAsset(Material))
				{
					return nullptr;
				}
			}
			if (Index == Amber && !ValidateIndicatorMaterial(Material))
			{
				if (bValidateOnly || !bRegenerate || !IsAuthoredAsset(Material)
					|| !AddIndicatorGraph(Material) || !SaveAuthoredAsset(Material)) return nullptr;
			}
			return Material;
		}
		if (bValidateOnly || !bRegenerate)
		{
			return nullptr;
		}
		if (!IsAuthoredAsset(Material))
		{
			UE_LOG(LogBuildSedanVisual, Error,
				TEXT("Refusing to replace non-authored material: %s"), *Path);
			return nullptr;
		}
		if (HasAnyDentGraph(Material))
		{
			UE_LOG(LogBuildSedanVisual, Error,
				TEXT("Refusing to append over an incomplete dent graph: %s"), *Path);
			return nullptr;
		}
	}
	else
	{
		if (bValidateOnly)
		{
			return nullptr;
		}
		UPackage* Package = CreatePackage(*Path);
		Material = NewObject<UMaterial>(
			Package, Finish.Name, RF_Public | RF_Standalone);
	}

	BuildMaterialGraph(Material, Finish, !bExisting);
	if ((Index == Amber && !AddIndicatorGraph(Material))
		|| (Index == Glass && !ValidateTransparentGlass(Material))
		|| !ValidateDentMaterial(Material) || !SaveAuthoredAsset(Material))
	{
		return nullptr;
	}
	if (!bExisting)
	{
		FAssetRegistryModule::AssetCreated(Material);
	}
	return Material;
}

/** Explicit smooth normals preserve the authored shape; no Chaos/physics geometry is generated. */
struct FAuthor
{
	FMeshDescription Mesh;
	FStaticMeshAttributes Attributes;
	TArray<FPolygonGroupID> Groups;
	int32 TriangleCount = 0;
	bool bInvalidTriangle = false;

	FAuthor() : Attributes(Mesh)
	{
		Attributes.Register();
		Attributes.GetVertexInstanceUVs().SetNumChannels(1);
		for (int32 Index = 0; Index < FinishCount; ++Index)
		{
			const FPolygonGroupID Group = Mesh.CreatePolygonGroup();
			Attributes.GetPolygonGroupMaterialSlotNames()[Group] = FName(Finishes[Index].Name);
			Groups.Add(Group);
		}
	}

	void Triangle(FVector A, FVector B, FVector C, FVector NA, FVector NB, FVector NC, int32 Finish)
	{
		// MeshDescription uses a left-handed frame with counter-clockwise faces:
		// StaticMeshOperations computes the geometric normal as (C-A) cross (B-A).
		FVector Cross = FVector::CrossProduct(C - A, B - A);
		if (Cross.SizeSquared() < 1.e-9) { return; }
		if (Cross.ContainsNaN() || A.ContainsNaN() || B.ContainsNaN() || C.ContainsNaN())
		{
			bInvalidTriangle = true;
			return;
		}
		if (FVector::DotProduct(Cross, NA + NB + NC) < 0.0)
		{
			Swap(B, C);
			Swap(NB, NC);
		}
		const FVector Points[] = { A, B, C };
		const FVector Normals[] = { NA.GetSafeNormal(), NB.GetSafeNormal(), NC.GetSafeNormal() };
		TArray<FVertexInstanceID, TInlineAllocator<3>> Instances;
		for (int32 I = 0; I < 3; ++I)
		{
			const FVertexID Vertex = Mesh.CreateVertex();
			Attributes.GetVertexPositions()[Vertex] = FVector3f(Points[I]);
			const FVertexInstanceID Instance = Mesh.CreateVertexInstance(Vertex);
			Attributes.GetVertexInstanceNormals()[Instance] = FVector3f(Normals[I]);
			FVector Tangent = FVector::CrossProduct(FVector::UpVector, Normals[I]).GetSafeNormal();
			if (Tangent.IsNearlyZero()) { Tangent = FVector::ForwardVector; }
			Attributes.GetVertexInstanceTangents()[Instance] = FVector3f(Tangent);
			Attributes.GetVertexInstanceBinormalSigns()[Instance] = 1.0f;
			Attributes.GetVertexInstanceUVs().Set(Instance, 0,
				FVector2f(static_cast<float>(Points[I].X / 100.0), static_cast<float>(Points[I].Z / 100.0)));
			Attributes.GetVertexInstanceColors()[Instance] = FVector4f(1, 1, 1, 1);
			Instances.Add(Instance);
		}
		Mesh.CreateTriangle(Groups[Finish], Instances);
		++TriangleCount;
	}

	void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D,
		const FVector& Outward, int32 Finish)
	{
		FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (FVector::DotProduct(Normal, Outward) < 0) { Normal *= -1; }
		Triangle(A, B, C, Normal, Normal, Normal, Finish);
		Triangle(A, C, D, Normal, Normal, Normal, Finish);
	}

	void Box(const FVector& Center, const FVector& Extent, int32 Finish)
	{
		const FVector P000 = Center + FVector(-Extent.X, -Extent.Y, -Extent.Z);
		const FVector P001 = Center + FVector(-Extent.X, -Extent.Y,  Extent.Z);
		const FVector P010 = Center + FVector(-Extent.X,  Extent.Y, -Extent.Z);
		const FVector P011 = Center + FVector(-Extent.X,  Extent.Y,  Extent.Z);
		const FVector P100 = Center + FVector( Extent.X, -Extent.Y, -Extent.Z);
		const FVector P101 = Center + FVector( Extent.X, -Extent.Y,  Extent.Z);
		const FVector P110 = Center + FVector( Extent.X,  Extent.Y, -Extent.Z);
		const FVector P111 = Center + FVector( Extent.X,  Extent.Y,  Extent.Z);
		Quad(P100, P110, P111, P101, FVector::ForwardVector, Finish);
		Quad(P000, P001, P011, P010, -FVector::ForwardVector, Finish);
		Quad(P010, P011, P111, P110, FVector::RightVector, Finish);
		Quad(P000, P100, P101, P001, -FVector::RightVector, Finish);
		Quad(P001, P101, P111, P011, FVector::UpVector, Finish);
		Quad(P000, P010, P110, P100, -FVector::UpVector, Finish);
	}

	void Transform(const FVector& Scale, const FVector& Offset)
	{
		if (Scale.GetMin() <= KINDA_SMALL_NUMBER || Scale.ContainsNaN()
			|| Offset.ContainsNaN())
		{
			bInvalidTriangle = true;
			return;
		}
		for (const FVertexID Vertex : Mesh.Vertices().GetElementIDs())
		{
			const FVector Point(Attributes.GetVertexPositions()[Vertex]);
			Attributes.GetVertexPositions()[Vertex] = FVector3f(
				Point.X * Scale.X + Offset.X,
				Point.Y * Scale.Y + Offset.Y,
				Point.Z * Scale.Z + Offset.Z);
		}
		for (const FVertexInstanceID Instance : Mesh.VertexInstances().GetElementIDs())
		{
			const FVector Normal(Attributes.GetVertexInstanceNormals()[Instance]);
			const FVector ScaledNormal(
				Normal.X / Scale.X, Normal.Y / Scale.Y, Normal.Z / Scale.Z);
			const FVector UnitNormal = ScaledNormal.GetSafeNormal();
			Attributes.GetVertexInstanceNormals()[Instance] = FVector3f(UnitNormal);
			FVector Tangent = FVector::CrossProduct(FVector::UpVector, UnitNormal).GetSafeNormal();
			if (Tangent.IsNearlyZero()) Tangent = FVector::ForwardVector;
			Attributes.GetVertexInstanceTangents()[Instance] = FVector3f(Tangent);
		}
	}

	void Patch(TFunctionRef<FVector(double, double)> Sample,
		TFunctionRef<FVector(double, double)> Outward, int32 StepsU, int32 StepsV, int32 Finish)
	{
		auto Normal = [&](double U, double V)
		{
			const FVector DU = Sample(FMath::Min(1.0, U + 0.0002), V) - Sample(FMath::Max(0.0, U - 0.0002), V);
			const FVector DV = Sample(U, FMath::Min(1.0, V + 0.0002)) - Sample(U, FMath::Max(0.0, V - 0.0002));
			FVector N = FVector::CrossProduct(DU, DV).GetSafeNormal();
			if (FVector::DotProduct(N, Outward(U, V)) < 0) { N *= -1; }
			return N.IsNearlyZero() ? Outward(U, V).GetSafeNormal() : N;
		};
		for (int32 I = 0; I < StepsU; ++I)
		{
			const double U0 = static_cast<double>(I) / StepsU;
			const double U1 = static_cast<double>(I + 1) / StepsU;
			for (int32 J = 0; J < StepsV; ++J)
			{
				const double V0 = static_cast<double>(J) / StepsV;
				const double V1 = static_cast<double>(J + 1) / StepsV;
				const FVector A = Sample(U0, V0), B = Sample(U1, V0), C = Sample(U1, V1), D = Sample(U0, V1);
				const FVector NA = Normal(U0, V0), NB = Normal(U1, V0), NC = Normal(U1, V1), ND = Normal(U0, V1);
				Triangle(A, B, C, NA, NB, NC, Finish);
				Triangle(A, C, D, NA, NC, ND, Finish);
			}
		}
	}

	void Tube(const FVector& A, const FVector& B, double Radius, int32 Finish, int32 Sides = 8)
	{
		const FVector Axis = (B - A).GetSafeNormal();
		FVector U, V;
		Axis.FindBestAxisVectors(U, V);
		Patch([&](double T, double Angle)
		{
			return FMath::Lerp(A, B, T) + Radius * (U * FMath::Cos(Angle * 2 * PI) + V * FMath::Sin(Angle * 2 * PI));
		}, [&](double, double Angle)
		{
			return U * FMath::Cos(Angle * 2 * PI) + V * FMath::Sin(Angle * 2 * PI);
		}, 1, Sides, Finish);
	}

	void Polyline(const TArray<FVector>& Points, double Radius, int32 Finish)
	{
		for (int32 I = 1; I < Points.Num(); ++I) { Tube(Points[I - 1], Points[I], Radius, Finish, 6); }
	}

	void Ellipsoid(const FVector& Center, const FVector& Size, int32 Finish, int32 Slices = 16, int32 Stacks = 8)
	{
		Patch([&](double U, double V)
		{
			const double Azimuth = 2 * PI * U;
			const double Elevation = PI * V;
			return Center + FVector(Size.X * FMath::Sin(Elevation) * FMath::Cos(Azimuth),
				Size.Y * FMath::Sin(Elevation) * FMath::Sin(Azimuth), Size.Z * FMath::Cos(Elevation));
		}, [&](double U, double V)
		{
			return FVector(FMath::Sin(PI * V) * FMath::Cos(2 * PI * U) / Size.X,
				FMath::Sin(PI * V) * FMath::Sin(2 * PI * U) / Size.Y, FMath::Cos(PI * V) / Size.Z);
		}, Slices, Stacks, Finish);
	}
};

double Curve(double X, std::initializer_list<FVector2D> Keys)
{
	const FVector2D* It = Keys.begin();
	FVector2D Previous = *It++;
	for (; It != Keys.end(); ++It)
	{
		if (X <= It->X)
		{
			const double T = FMath::Clamp((X - Previous.X) / (It->X - Previous.X), 0.0, 1.0);
			// Cubic interpolation rounds each body station rather than stacking cuboids.
			return FMath::Lerp(Previous.Y, It->Y, T * T * (3 - 2 * T));
		}
		Previous = *It;
	}
	return Previous.Y;
}

double Width(double X)
{
	return SimCoreSedanVisualContract::BodyHalfWidthCm(X);
}

double Deck(double X)
{
	return SimCoreSedanVisualContract::BodyDeckHeightCm(X);
}

double ArchCut(double X)
{
	double Low = -29.0;
	for (double Axle : {-148.5, 121.5})
	{
		const double DX = X - Axle;
		if (FMath::Abs(DX) <= 38.5)
		{
			Low = FMath::Max(Low, -23.0 + FMath::Sqrt(38.5 * 38.5 - DX * DX));
		}
	}
	return Low;
}

FVector BodySidePoint(double X, double Side, double V)
{
	const double Z = FMath::Lerp(ArchCut(X), Deck(X) - 2, V);
	const double Y = Width(X) - 3.5 + 4.0 * FMath::Sin(V * PI) - 4.0 * V * V;
	return FVector(X, Side * Y, Z);
}

FVector BodyDeckPoint(double X, double Across)
{
	return FVector(X, Across * (Width(X) - 7.5), Deck(X) - 2 + 3.0 * (1 - Across * Across));
}

FVector BodyEndPoint(double X, double Across, double V)
{
	// The same boundary curves as the side skin and upper deck close every corner.
	// An independently bowed/constant-width fascia leaves visible unjoined edges.
	const double HalfWidth = BodySidePoint(X, 1.0, V).Y;
	return FVector(X, Across * HalfWidth, FMath::Lerp(ArchCut(X), BodyDeckPoint(X, Across).Z, V));
}

double CabinZ(double X)
{
	return Curve(X, {{-135, 42}, {-82, 83}, {-60, 92}, {-6, 94}, {17, 86}, {66, 41}});
}

double CabinWidth(double X)
{
	return Curve(X, {{-135, 76}, {-82, 62}, {-60, 61}, {-6, 61}, {17, 64}, {66, 77}});
}

FVector CabinSidePoint(double X, double Side, double V)
{
	return FVector(X, Side * (FMath::Lerp(78.0, CabinWidth(X), V) + 0.45),
		FMath::Lerp(42.0, CabinZ(X), V));
}

FVector LampPoint(bool bFront, double Side, double U, double V, double Lift)
{
	return SimCoreSedanVisualContract::LampLensPointCm(bFront, Side < 0, U, V, Lift);
}

void BuildBody(FAuthor& M)
{
	// Continuous right/fender skin with real wheel openings. The left-front
	// door aperture is split out of this body mesh and is filled only by the
	// separately authored SM_SedanDoorLeft when it is closed.
	for (double Side : {-1.0, 1.0})
	{
		auto SideSkin = [&](const double StartX, const double EndX,
			const double StartV, const double EndV, const int32 StepsX,
			const int32 StepsV)
		{
			M.Patch([&](double U, double V)
			{
				const double X = FMath::Lerp(StartX, EndX, U);
				return BodySidePoint(X, Side, FMath::Lerp(StartV, EndV, V));
			}, [&](double, double) { return FVector(0, Side, 0); },
				StepsX, StepsV, Paint);
		};
		if (Side < 0.0)
		{
			SideSkin(-228.5, DriverDoorRearX, 0.0, 1.0, 76, 6);
			SideSkin(DriverDoorFrontX, 201.5, 0.0, 1.0, 58, 6);
			// A narrow fixed sill remains under the opening; no painted door skin
			// remains behind the moving panel.
			SideSkin(DriverDoorRearX, DriverDoorFrontX, 0.0,
				DriverDoorLowerV, 44, 1);
		}
		else
		{
			SideSkin(-228.5, 201.5, 0.0, 1.0, 180, 6);
		}
		// Rolled, thin fender lip follows the exact open wheel cutout, not a solid disc.
		for (double Axle : {-148.5, 121.5})
		{
			M.Patch([&](double U, double V)
			{
				const double Angle = PI * U;
				const double Radius = 38.5 + V * 1.6;
				const double X = Axle + Radius * FMath::Cos(Angle);
				return FVector(X, Side * (Width(X) - 2.8 + 0.5 * FMath::Sin(V * PI)), -23 + Radius * FMath::Sin(Angle));
			}, [&](double, double) { return FVector(0, Side, 0); }, 48, 2, Paint);
		}
		// Longitudinal character line and modest rocker trim stop before both wheel openings.
		auto AddBelt = [&](const double StartX, const double EndX,
			const int32 Steps)
		{
			TArray<FVector> Belt;
			for (int32 I = 0; I <= Steps; ++I)
			{
				const double X = FMath::Lerp(StartX, EndX,
					static_cast<double>(I) / Steps);
				Belt.Add(FVector(X, Side * (Width(X) - 1.0),
					Deck(X) - 10.0));
			}
			M.Polyline(Belt, 0.24, Paint);
		};
		if (Side < 0.0)
		{
			AddBelt(-204.0, DriverDoorRearX, 25);
			AddBelt(DriverDoorFrontX, 179.0, 19);
		}
		else
		{
			AddBelt(-204.0, 179.0, 60);
		}
		M.Tube(FVector(-106, Side * 87, -24), FVector(79, Side * 87, -24), 2.0, Black);
	}
	// Curved upper body deck: gently crowned hood/trunk and fender shoulders.
	M.Patch([&](double U, double V)
	{
		const double X = FMath::Lerp(-228.5, 201.5, U);
		const double Across = V * 2 - 1;
		return BodyDeckPoint(X, Across);
	}, [](double, double) { return FVector::UpVector; }, 120, 12, Paint);
	// Front/rear rounded fascia follow the narrower terminal cross sections.
	for (double X : {-228.5, 201.5})
	{
		const double Direction = X > 0 ? 1.0 : -1.0;
		M.Patch([&](double U, double V)
		{
			const double Across = U * 2 - 1;
			return BodyEndPoint(X, Across, V);
		}, [&](double, double) { return FVector(Direction, 0, 0); }, 24, 6, Paint);
	}
	M.Quad(FVector(-228.5, -61, -29), FVector(201.5, -61, -29), FVector(201.5, 61, -29), FVector(-228.5, 61, -29), FVector::DownVector, Black);
	for (const FVector2D Range : {FVector2D(-228.5, -188.0), FVector2D(161.0, 201.5)})
	{
		for (double Side : {-1.0, 1.0})
		{
			M.Patch([&](double U, double V)
			{
				const double X = FMath::Lerp(Range.X, Range.Y, U);
				return FVector(X, Side * FMath::Lerp(61.0, Width(X) - 3.5, V), -29.0);
			}, [](double, double) { return FVector::DownVector; }, 12, 1, Black);
		}
	}

	// Greenhouse: a tapered loft, including a convex roof, separate glass and structural pillars.
	// The opaque roof occupies only the metal panel between the front and rear
	// glazing. Earlier versions skinned the complete greenhouse in Paint and
	// merely overlaid glass, so a translucent material still revealed solid blue.
	M.Patch([&](double U, double V)
	{
		const double X = FMath::Lerp(-82.0, 17.0, U);
		const double Across = V * 2 - 1;
		return FVector(X, Across * CabinWidth(X), CabinZ(X) + 2.0 * (1 - Across * Across));
	}, [](double, double) { return FVector::UpVector; }, 28, 12, Paint);
	for (double Side : {-1.0, 1.0})
	{
		// Belt and roof rails surround the window openings; glass has no opaque backing.
		auto AddLowerRail = [&](const double StartX, const double EndX,
			const int32 Steps)
		{
			M.Patch([&](double U, double V)
			{
				const double X = FMath::Lerp(StartX, EndX, U);
				const double RailV = FMath::Lerp(0.0, 0.105, V);
				return FVector(X, Side * FMath::Lerp(78.0, CabinWidth(X), RailV),
					FMath::Lerp(42.0, CabinZ(X), RailV));
			}, [&](double, double) { return FVector(0, Side, 0); },
				Steps, 1, Paint);
		};
		if (Side < 0.0)
		{
			AddLowerRail(-135.0, DriverDoorRearX, 28);
			AddLowerRail(DriverDoorFrontX, 66.0, 1);
		}
		else
		{
			AddLowerRail(-135.0, 66.0, 64);
		}
		M.Patch([&](double U, double V)
		{
			const double X = FMath::Lerp(-135.0, 66.0, U);
			const double RailV = FMath::Lerp(0.92, 1.0, V);
			return FVector(X, Side * FMath::Lerp(78.0, CabinWidth(X), RailV),
				FMath::Lerp(42.0, CabinZ(X), RailV));
		}, [&](double, double) { return FVector(0, Side, 0); }, 64, 1, Paint);
		// Side glazing is inset from the A/C pillars and split by an actual broad black B pillar.
		for (const FVector2D Range : {FVector2D(-120, -49), FVector2D(-43, 54)})
		{
			if (Side < 0.0 && Range.X > DriverDoorRearX)
			{
				continue;
			}
			M.Patch([&](double U, double V)
			{
				const double X = FMath::Lerp(Range.X, Range.Y, U);
				return CabinSidePoint(X, Side, FMath::Lerp(0.12, 0.91, V));
			}, [&](double, double) { return FVector(0, Side, 0.2); }, 22, 3, Glass);
			TArray<FVector> Lower, Upper;
			for (int32 I = 0; I <= 22; ++I)
			{
				const double X = FMath::Lerp(Range.X, Range.Y, I / 22.0);
				Lower.Add(CabinSidePoint(X, Side, 0.12));
				Upper.Add(CabinSidePoint(X, Side, 0.91));
			}
			M.Polyline(Lower, 0.7, Chrome);
			M.Polyline(Upper, 1.0, Black);
			M.Tube(Lower[0], Upper[0], 1.0, Black);
			M.Tube(Lower.Last(), Upper.Last(), 1.0, Black);
		}
		for (const FVector2D Pillar : {
			FVector2D(-135.0, -120.0), FVector2D(-49.0, -43.0), FVector2D(54.0, 66.0)})
		{
			M.Patch([&](double U, double V)
			{
				return CabinSidePoint(FMath::Lerp(Pillar.X, Pillar.Y, U), Side,
					FMath::Lerp(0.10, 0.94, V));
			}, [&](double, double) { return FVector(0, Side, 0); }, 4, 3, Black);
		}
		M.Quad(CabinSidePoint(-49, Side, 0.10), CabinSidePoint(-43, Side, 0.10),
			CabinSidePoint(-43, Side, 0.94), CabinSidePoint(-49, Side, 0.94),
			FVector(0, Side, 0), Black);
		// Door shut lines and pull handles: dark geometric seams, never painted onto a box.
		for (double X : {-114.0, -46.0, 63.0})
		{
			const double Bottom = FMath::Max(-22.0, ArchCut(X) + 3.0);
			TArray<FVector> Seam;
			for (int32 I = 0; I <= 8; ++I)
			{
				const double V = FMath::Lerp(0.07, 0.95, I / 8.0);
				const double Z = FMath::Lerp(Bottom, Deck(X) - 3, V);
				const double T = (Z - ArchCut(X)) / (Deck(X) - 2 - ArchCut(X));
				const double Y = Width(X) - 3.5 + 4 * FMath::Sin(T * PI) - 4 * T * T + 0.13;
				Seam.Add(FVector(X, Side * Y, Z));
			}
			M.Polyline(Seam, 0.30, Black);
		}
		for (double X : {-88.0, 1.0})
		{
			if (Side < 0.0 && X > DriverDoorRearX)
			{
				continue;
			}
			M.Ellipsoid(FVector(X, Side * 89.0, 24.0), FVector(6.3, 1.3, 1.4), Chrome);
		}
		if (Side > 0.0)
		{
			M.Tube(FVector(45, Side * 79, 48), FVector(43, Side * 96, 47), 1.6, Black);
			M.Ellipsoid(FVector(44, Side * 100, 49), FVector(8.2, 6.4, 4.5), Paint);
			M.Quad(FVector(37.0, Side * 95.5, 46), FVector(37.0, Side * 104.0, 46),
				FVector(37.0, Side * 104.0, 51.5), FVector(37.0, Side * 95.5, 51.5), FVector(-1, 0, 0), Glass);
		}
	}
	// Windshield and rear glazing follow the actual curved cabin loft, not flat floating planes.
	for (const FVector2D Range : {FVector2D(-128, -85), FVector2D(21, 61)})
	{
		M.Patch([&](double U, double V)
		{
			const double X = FMath::Lerp(Range.X, Range.Y, U);
			const double Across = (V * 2 - 1) * 0.93;
			return FVector(X, Across * CabinWidth(X), CabinZ(X) + 2 * (1 - Across * Across) + 0.28);
		}, [](double, double) { return FVector::UpVector; }, 20, 18, Glass);
	}
	// Hood and trunk panel gaps terminate before the greenhouse.
	for (const FVector2D Range : {FVector2D(70, 185), FVector2D(-211, -139)})
	{
		for (double Side : {-1.0, 1.0})
		{
			TArray<FVector> Gap;
			for (int32 I = 0; I <= 20; ++I)
			{
				const double X = FMath::Lerp(Range.X, Range.Y, I / 20.0);
				Gap.Add(FVector(X, Side * (Width(X) - 7.5) * 0.73, Deck(X) - 2 + 3 * (1 - 0.73 * 0.73) + 0.18));
			}
			M.Polyline(Gap, 0.24, Black);
		}
	}
	// Front grille, lower intake, split LED headlamp housings, and a small unbranded nose badge.
	M.Quad(FVector(203.1, -49, -13), FVector(203.1, 49, -13), FVector(203.1, 43, 5), FVector(203.1, -43, 5), FVector::ForwardVector, Black);
	for (int32 I = -5; I <= 5; ++I)
	{
		M.Tube(FVector(203.45, I * 7.6, -11), FVector(203.45, I * 7.6, 3.5), 0.65, Chrome, 6);
	}
	M.Polyline({FVector(202.6, -54, -21), FVector(203.3, 0, -23), FVector(202.6, 54, -21)}, 2.0, Black);
	M.Ellipsoid(FVector(203.45, 0, 6), FVector(0.9, 3.1, 1.8), Chrome);
	for (double Side : {-1.0, 1.0})
	{
		for (bool bFront : {true, false})
		{
			// Planar lamp quads intersected the rolled hood/trunk and appeared as floating
			// rectangles plus isolated triangle strips. Sample the same deck instead.
			M.Patch([&](double U, double V) { return LampPoint(bFront, Side, U, V, 0.65); },
				[](double, double) { return FVector::UpVector; }, 12, 4, Black);
			M.Patch([&](double U, double V)
			{
				return LampPoint(bFront, Side, FMath::Lerp(0.035, 0.965, U), FMath::Lerp(0.37, 0.85, V), 1.0);
			}, [](double, double) { return FVector::UpVector; }, 12, 3, bFront ? Headlamp : TailLamp);
			// The strip is part of the curved lamp lens, not an attached cube. Leave
			// a small black housing gap between the white/red and amber sections.
			M.Patch([&](double U, double V)
			{
				return SimCoreSedanVisualContract::TurnSignalLensPointCm(bFront, Side < 0, U, V);
			}, [](double, double) { return FVector::UpVector; }, 12, 2, Amber);
		}
		M.Ellipsoid(FVector(-216, Side * 54, -24), FVector(5, 7, 2.6), Black);
		M.Tube(FVector(-219, Side * 60, -22), FVector(-224, Side * 60, -22), 2.9, Chrome, 12);
	}
	M.Quad(FVector(-230.1, -21, -11), FVector(-230.1, 21, -11), FVector(-230.1, 21, 0), FVector(-230.1, -21, 0), FVector(-1, 0, 0), Black);
	M.Quad(FVector(-230.3, -18, -9), FVector(-230.3, 18, -9), FVector(-230.3, 18, -1), FVector(-230.3, -18, -1), FVector(-1, 0, 0), Plate);
	// Abstract bars stand in for a non-personal fictional registration, no brand/logo required.
	for (int32 I = -4; I <= 4; ++I)
	{
		M.Tube(FVector(-230.55, I * 3.1, -7), FVector(-230.55, I * 3.1, -3), 0.55, Black, 6);
	}
}

void BuildDriverDoor(FAuthor& M)
{
	constexpr double Side = -1.0;
	// The exterior reuses the exact body curve removed by BuildBody, so the
	// closed door restores the original silhouette without an overlay or mask.
	M.Patch([&](double U, double V)
	{
		const double X = FMath::Lerp(DriverDoorRearX, DriverDoorFrontX, U);
		return BodySidePoint(X, Side,
			FMath::Lerp(DriverDoorLowerV, 1.0, V));
	}, [](double, double) { return FVector(0, -1, 0); }, 44, 6, Paint);

	// A thin, body-conforming inner trim is part of the moving door itself. It
	// replaces the old fixed black aperture slab and exposes the real cabin when
	// the door opens.
	auto InnerPoint = [](const double X, const double V)
	{
		FVector Point = BodySidePoint(X, -1.0, V);
		Point.Y += 2.4;
		return Point;
	};
	M.Patch([&](double U, double V)
	{
		const double X = FMath::Lerp(DriverDoorRearX, DriverDoorFrontX, U);
		return InnerPoint(X, FMath::Lerp(DriverDoorLowerV, 1.0, V));
	}, [](double, double) { return FVector(0, 1, 0); }, 44, 6, Black);

	// Close the thin door shell along the two jambs, sill and belt edge.
	for (double X : {DriverDoorRearX, DriverDoorFrontX})
	{
		M.Patch([&](double U, double V)
		{
			const double BodyV = FMath::Lerp(DriverDoorLowerV, 1.0, U);
			return FMath::Lerp(BodySidePoint(X, Side, BodyV),
				InnerPoint(X, BodyV), V);
		}, [&](double, double) { return FVector(X < 0.0 ? -1.0 : 1.0, 0, 0); },
			6, 1, Black);
	}
	for (double BodyV : {DriverDoorLowerV, 1.0})
	{
		M.Patch([&](double U, double V)
		{
			const double X = FMath::Lerp(DriverDoorRearX, DriverDoorFrontX, U);
			return FMath::Lerp(BodySidePoint(X, Side, BodyV),
				InnerPoint(X, BodyV), V);
		}, [&](double, double) { return BodyV < 0.5 ? FVector::DownVector : FVector::UpVector; },
			44, 1, Black);
	}

	TArray<FVector> CharacterLine;
	for (int32 I = 0; I <= 22; ++I)
	{
		const double X = FMath::Lerp(DriverDoorRearX, DriverDoorFrontX,
			static_cast<double>(I) / 22.0);
		CharacterLine.Add(FVector(X, -(Width(X) - 1.0), Deck(X) - 10.0));
	}
	M.Polyline(CharacterLine, 0.24, Paint);

	// Lower window rail belongs to the door; the fixed roof rail and A/B pillars
	// remain on the body.
	M.Patch([&](double U, double V)
	{
		const double X = FMath::Lerp(DriverDoorRearX, DriverDoorFrontX, U);
		const FVector Lower = BodySidePoint(X, Side, 1.0);
		const FVector Upper(X, -78.0, 42.0);
		return FMath::Lerp(Lower, Upper, V);
	}, [](double, double) { return FVector(0, -1, 0.35); }, 44, 1, Paint);
	M.Patch([&](double U, double V)
	{
		const double X = FMath::Lerp(DriverDoorRearX, DriverDoorFrontX, U);
		const double RailV = FMath::Lerp(0.0, 0.105, V);
		return FVector(X, -FMath::Lerp(78.0, CabinWidth(X), RailV),
			FMath::Lerp(42.0, CabinZ(X), RailV));
	}, [](double, double) { return FVector(0, -1, 0); }, 44, 1, Paint);

	constexpr double GlassRearX = -43.0;
	constexpr double GlassFrontX = 54.0;
	M.Patch([&](double U, double V)
	{
		const double X = FMath::Lerp(GlassRearX, GlassFrontX, U);
		return CabinSidePoint(X, Side, FMath::Lerp(0.12, 0.91, V));
	}, [](double, double) { return FVector(0, -1, 0.2); }, 22, 3, Glass);
	TArray<FVector> LowerFrame;
	TArray<FVector> UpperFrame;
	for (int32 I = 0; I <= 22; ++I)
	{
		const double X = FMath::Lerp(GlassRearX, GlassFrontX,
			static_cast<double>(I) / 22.0);
		LowerFrame.Add(CabinSidePoint(X, Side, 0.12));
		UpperFrame.Add(CabinSidePoint(X, Side, 0.91));
	}
	M.Polyline(LowerFrame, 0.7, Chrome);
	M.Polyline(UpperFrame, 1.0, Black);
	M.Tube(LowerFrame[0], UpperFrame[0], 1.0, Black);
	M.Tube(LowerFrame.Last(), UpperFrame.Last(), 1.0, Black);

	// Exterior handle and mirror move with the door instead of remaining on the
	// body after the hinge rotates.
	M.Ellipsoid(FVector(1.0, -89.0, 24.0), FVector(6.3, 1.3, 1.4), Chrome);
	M.Tube(FVector(45, -79, 48), FVector(43, -96, 47), 1.6, Black);
	M.Ellipsoid(FVector(44, -100, 49), FVector(8.2, 6.4, 4.5), Paint);
	M.Quad(FVector(37.0, -95.5, 46), FVector(37.0, -104.0, 46),
		FVector(37.0, -104.0, 51.5), FVector(37.0, -95.5, 51.5),
		FVector(-1, 0, 0), Glass);
}

void BuildWheel(FAuthor& M)
{
	// Tire: smooth-normal revolved cross-section, 32cm rolling radius and 22cm nominal width.
	const FVector2D TireProfile[] = {{-11, 24}, {-11, 27}, {-9.4, 30.3}, {-6, 32}, {6, 32}, {9.4, 30.3}, {11, 27}, {11, 24}};
	for (int32 I = 1; I < UE_ARRAY_COUNT(TireProfile); ++I)
	{
		M.Patch([&](double U, double V)
		{
			const FVector2D P = FMath::Lerp(TireProfile[I - 1], TireProfile[I], V);
			return FVector(P.Y * FMath::Cos(U * 2 * PI), P.X, P.Y * FMath::Sin(U * 2 * PI));
		}, [&](double U, double V)
		{
			const double Y = FMath::Lerp(TireProfile[I - 1].X, TireProfile[I].X, V);
			return FVector(FMath::Cos(U * 2 * PI), Y / 8.0, FMath::Sin(U * 2 * PI));
		}, 48, 2, Rubber);
	}
	// Four recessed-looking circumferential tread channels, plus short diagonal siping.
	for (double Y : {-5.0, -1.7, 1.7, 5.0})
	{
		M.Patch([&](double U, double V)
		{
			return FVector(32.035 * FMath::Cos(U * 2 * PI), Y + (V - 0.5) * 0.48, 32.035 * FMath::Sin(U * 2 * PI));
		}, [](double U, double) { return FVector(FMath::Cos(U * 2 * PI), 0, FMath::Sin(U * 2 * PI)); }, 48, 1, Black);
	}
	for (int32 I = 0; I < 48; ++I)
	{
		for (double Side : {-1.0, 1.0})
		{
			const double A = I * 2 * PI / 48;
			M.Patch([&](double U, double V)
			{
				const double Angle = A + U * 0.055 + V * 0.009;
				return FVector(32.055 * FMath::Cos(Angle), Side * (0.5 + U * 5.8), 32.055 * FMath::Sin(Angle));
			}, [&](double U, double V)
			{
				const double Angle = A + U * 0.055 + V * 0.009;
				return FVector(FMath::Cos(Angle), 0, FMath::Sin(Angle));
			}, 2, 1, Black);
		}
	}
	for (double Side : {-1.0, 1.0})
	{
		// Symmetric styling on both faces keeps left/right wheels visually identical.
		for (const FVector2D RadiusRange : {FVector2D(22.0, 24.0), FVector2D(5.0, 6.8)})
		{
			M.Patch([&](double U, double V)
			{
				const double R = FMath::Lerp(RadiusRange.X, RadiusRange.Y, V);
				return FVector(R * FMath::Cos(U * 2 * PI), Side * (11.05 + 0.7 * FMath::Sin(V * PI)), R * FMath::Sin(U * 2 * PI));
			}, [&](double, double) { return FVector(0, Side, 0); }, 48, 2, Alloy);
		}
		M.Patch([&](double U, double V)
		{
			const double R = V * 21.5;
			return FVector(R * FMath::Cos(U * 2 * PI), Side * 7.4, R * FMath::Sin(U * 2 * PI));
		}, [&](double, double) { return FVector(0, Side, 0); }, 48, 1, Black);
		M.Patch([&](double U, double V)
		{
			const double R = V * 18.8;
			return FVector(R * FMath::Cos(U * 2 * PI), Side * 7.7, R * FMath::Sin(U * 2 * PI));
		}, [&](double, double) { return FVector(0, Side, 0); }, 48, 1, Alloy);
		// Ten curved split spokes with actual gaps, brake disc visible behind them.
		for (int32 Spoke = 0; Spoke < 10; ++Spoke)
		{
			const double BaseAngle = (Spoke / 2) * 2 * PI / 5 + (Spoke % 2 ? 0.095 : -0.095);
			M.Patch([&](double U, double V)
			{
				const double R = FMath::Lerp(5.6, 22.8, U);
				const double A = BaseAngle + U * 0.13 + (V - 0.5) * FMath::Lerp(0.23, 0.085, U);
				return FVector(R * FMath::Cos(A), Side * (10.6 + 0.8 * FMath::Sin(U * PI)), R * FMath::Sin(A));
			}, [&](double, double) { return FVector(0, Side, 0); }, 6, 2, Alloy);
		}
		M.Ellipsoid(FVector(0, Side * 11.0, 0), FVector(5.6, 1.05, 5.6), Alloy);
		for (int32 Bolt = 0; Bolt < 5; ++Bolt)
		{
			const double Angle = Bolt * 2 * PI / 5;
			M.Ellipsoid(FVector(3.7 * FMath::Cos(Angle), Side * 12, 3.7 * FMath::Sin(Angle)), FVector(0.65, 0.35, 0.65), Black, 8, 4);
		}
	}
}

void BuildCompactBody(FAuthor& M)
{
	// Reuse the curved, closed sedan surface at a genuinely smaller wheelbase.
	// The separate driver door is merged into this NPC-only body because fleet
	// variants do not run the sedan exit-door presentation.
	BuildBody(M);
	BuildDriverDoor(M);
	M.Transform(FVector(0.79, 0.90, 0.92), FVector(-4.0, 0.0, -1.5));
}

void BuildTruckBody(FAuthor& M)
{
	// A medium box truck: separate chassis, forward cab and tall cargo body make
	// its silhouette unambiguous at traffic-camera distance.
	M.Box(FVector(-15, 0, -8), FVector(310, 103, 18), Black);
	M.Box(FVector(-138, 0, 72), FVector(145, 101, 92), Paint);
	// Keep the lower cab, but leave its upper volume empty for the shared driver
	// rig. Thin walls, pillars and a roof surround real window openings.
	M.Box(FVector(94, 0, 33), FVector(72, 99, 41), Paint);
	M.Box(FVector(100, 0, 72), FVector(75, 95, 2.5), Black);
	M.Box(FVector(24, 0, 117), FVector(2, 99, 43), Paint);
	M.Box(FVector(100.5, 0, 163.5), FVector(78.5, 99, 3.5), Paint);
	M.Box(FVector(176.5, 0, 86), FVector(2.5, 99, 12), Paint);
	for (double Side : {-1.0, 1.0})
	{
		M.Box(FVector(100, Side * 97, 86), FVector(75, 2, 12), Paint);
		M.Box(FVector(29, Side * 97, 129), FVector(3, 2, 31), Paint);
		M.Box(FVector(175, Side * 97, 129), FVector(4, 2, 31), Paint);
		M.Quad(FVector(32, Side * 98, 98), FVector(171, Side * 98, 98),
			FVector(171, Side * 98, 160), FVector(32, Side * 98, 160),
			FVector(0, Side, 0), Glass);
	}
	M.Quad(FVector(178, -95, 98), FVector(178, 95, 98),
		FVector(178, 95, 160), FVector(178, -95, 160),
		FVector::ForwardVector, Glass);
	M.Box(FVector(194, 0, 31), FVector(43, 96, 35), Paint);
	M.Box(FVector(236, 0, 13), FVector(8, 105, 9), Chrome);
	M.Box(FVector(-291, 0, 6), FVector(8, 105, 9), Chrome);
	for (double Side : {-1.0, 1.0})
	{
		M.Box(FVector(238, Side * 72, 45), FVector(2.5, 19, 10), Headlamp);
		M.Box(FVector(-292, Side * 76, 53), FVector(2.5, 18, 11), TailLamp);
		M.Box(FVector(240, Side * 96, 58), FVector(3.0, 7.0, 6.0), Amber);
		M.Box(FVector(-294, Side * 96, 64), FVector(3.0, 7.0, 6.0), Amber);
	}
	M.Box(FVector(-292, 0, 22), FVector(2.0, 31, 11), Plate);
}

bool ValidateTruckCabinGeometry(const FMeshDescription& Mesh, const bool bLogFailure = true)
{
	const FStaticMeshConstAttributes Attributes(Mesh);
	const auto Positions = Attributes.GetVertexPositions();
	const auto Slots = Attributes.GetPolygonGroupMaterialSlotNames();
	// Both the seated driver's face and steering wheel must be visible through
	// each side window and the windshield, without an opaque backing surface.
	for (const FVector& Inside : {FVector(85, -34, 145), FVector(124, -34, 116)})
	{
		const FVector OutsidePoints[] = {
			FVector(Inside.X, -160, Inside.Z), FVector(Inside.X, 160, Inside.Z),
			FVector(240, Inside.Y, Inside.Z)};
		for (const FVector& Outside : OutsidePoints)
		{
			bool bCrossesGlass = false;
			bool bCrossesOpaque = false;
			const FVector Direction = Outside - Inside;
			for (const FTriangleID Triangle : Mesh.Triangles().GetElementIDs())
			{
				const auto Instances = Mesh.GetTriangleVertexInstances(Triangle);
				const FVector A(Positions[Mesh.GetVertexInstanceVertex(Instances[0])]);
				const FVector B(Positions[Mesh.GetVertexInstanceVertex(Instances[1])]);
				const FVector C(Positions[Mesh.GetVertexInstanceVertex(Instances[2])]);
				const FVector Edge1 = B - A;
				const FVector Edge2 = C - A;
				const FVector Cross = FVector::CrossProduct(Direction, Edge2);
				const double Determinant = FVector::DotProduct(Edge1, Cross);
				if (FMath::Abs(Determinant) < 1.e-8) continue;
				const FVector FromA = Inside - A;
				const double U = FVector::DotProduct(FromA, Cross) / Determinant;
				const FVector Q = FVector::CrossProduct(FromA, Edge1);
				const double V = FVector::DotProduct(Direction, Q) / Determinant;
				const double T = FVector::DotProduct(Edge2, Q) / Determinant;
				if (U < -1.e-6 || V < -1.e-6 || U + V > 1.0 + 1.e-6
					|| T <= 0.0 || T >= 1.0) continue;
				if (Slots[Mesh.GetTrianglePolygonGroup(Triangle)] == FName(Finishes[Glass].Name))
					bCrossesGlass = true;
				else
					bCrossesOpaque = true;
			}
			if (!bCrossesGlass || bCrossesOpaque)
			{
				if (bLogFailure)
				{
					UE_LOG(LogBuildSedanVisual, Error,
						TEXT("Truck cab sightline %s to %s requires glass without opaque backing."),
						*Inside.ToString(), *Outside.ToString());
				}
				return false;
			}
		}
	}
	return true;
}

void BuildMotorcycleBody(FAuthor& M)
{
	// Two-wheel road bike with a visible frame, tank, saddle, fork and lamps.
	M.Tube(FVector(-82, 0, -7), FVector(20, 0, 47), 4.2, Black, 10);
	M.Tube(FVector(20, 0, 47), FVector(91, 0, -8), 4.2, Black, 10);
	M.Tube(FVector(-82, 0, -7), FVector(91, 0, -8), 3.6, Chrome, 10);
	M.Ellipsoid(FVector(20, 0, 47), FVector(49, 34, 29), Paint, 20, 10);
	M.Box(FVector(-40, 0, 53), FVector(44, 29, 8), Black);
	M.Ellipsoid(FVector(-77, 0, 33), FVector(29, 25, 19), Paint, 16, 8);
	M.Tube(FVector(80, -7, 66), FVector(99, -7, -6), 2.8, Chrome, 10);
	M.Tube(FVector(80, 7, 66), FVector(99, 7, -6), 2.8, Chrome, 10);
	M.Tube(FVector(75, -43, 70), FVector(75, 43, 70), 2.6, Black, 10);
	M.Ellipsoid(FVector(103, 0, 62), FVector(10, 15, 10), Headlamp, 14, 7);
	M.Ellipsoid(FVector(-103, 0, 48), FVector(7, 13, 8), TailLamp, 14, 7);
	for (double Side : {-1.0, 1.0})
	{
		M.Tube(FVector(82, Side * 16, 66), FVector(91, Side * 27, 62), 1.6, Black, 8);
		M.Ellipsoid(FVector(93, Side * 29, 61), FVector(5, 5, 5), Amber, 10, 5);
		M.Tube(FVector(-76, Side * 13, 45), FVector(-91, Side * 24, 47), 1.4, Black, 8);
		M.Ellipsoid(FVector(-94, Side * 27, 47), FVector(4.5, 4.5, 4.5), Amber, 10, 5);
	}
}

bool ValidateIndicatorLenses(const FMeshDescription& Mesh)
{
	const FStaticMeshConstAttributes Attributes(Mesh);
	const auto Slots = Attributes.GetPolygonGroupMaterialSlotNames();
	const auto Positions = Attributes.GetVertexPositions();
	int32 CornerTriangleCounts[4] = {};
	for (FTriangleID Triangle : Mesh.Triangles().GetElementIDs())
	{
		if (Slots[Mesh.GetTrianglePolygonGroup(Triangle)] != FName(Finishes[Amber].Name)) continue;
		int32 Corner = INDEX_NONE;
		for (FVertexInstanceID Instance : Mesh.GetTriangleVertexInstances(Triangle))
		{
			const FVector Point(Positions[Mesh.GetVertexInstanceVertex(Instance)]);
			const bool bFront = Point.X > 0;
			const int32 VertexCorner = (bFront ? 0 : 2) + (Point.Y < 0 ? 0 : 1);
			if (Corner != INDEX_NONE && Corner != VertexCorner) return false;
			Corner = VertexCorner;
			const double Across = Point.Y / (Width(Point.X) - 7.5);
			const double U = (FMath::Abs(Across) - 0.36) / 0.6;
			const double V = bFront ? (196.0 - 4.0 * U - Point.X) / 14.0
				: (Point.X + 226.0 - 3.0 * U) / 17.0;
			if (U < 0.0349 || U > 0.9651 || V < 0.1499 || V > 0.3301
				|| !FMath::IsNearlyEqual(Point.Z - BodyDeckPoint(Point.X, Across).Z, 1.0, 0.0001)) return false;
		}
		if (Corner == INDEX_NONE) return false;
		++CornerTriangleCounts[Corner];
	}
	for (int32 Count : CornerTriangleCounts) if (Count != 48) return false;
	return true;
}

bool ValidateTransparentGlazingGeometry(const FMeshDescription& Mesh,
	const int32 MinimumGlassTriangles = 1700)
{
	const FStaticMeshConstAttributes Attributes(Mesh);
	const auto Slots = Attributes.GetPolygonGroupMaterialSlotNames();
	const auto Positions = Attributes.GetVertexPositions();
	int32 GlassTriangles = 0;
	int32 OpaqueBackingTriangles = 0;
	for (FTriangleID Triangle : Mesh.Triangles().GetElementIDs())
	{
		const FName Slot = Slots[Mesh.GetTrianglePolygonGroup(Triangle)];
		if (Slot == FName(Finishes[Glass].Name))
		{
			++GlassTriangles;
			continue;
		}
		if (Slot != FName(Finishes[Paint].Name))
		{
			continue;
		}
		FVector Points[3];
		int32 PointIndex = 0;
		for (FVertexInstanceID Instance : Mesh.GetTriangleVertexInstances(Triangle))
		{
			Points[PointIndex++] = FVector(
				Positions[Mesh.GetVertexInstanceVertex(Instance)]);
		}
		const FVector Centre = (Points[0] + Points[1] + Points[2]) / 3.0;
		const FVector Normal = FVector::CrossProduct(
			Points[2] - Points[0], Points[1] - Points[0]).GetSafeNormal();
		const bool bFrontOrRearGlassX = (Centre.X > -127.5 && Centre.X < -85.5)
			|| (Centre.X > 21.5 && Centre.X < 60.5);
		const bool bSideGlassX = (Centre.X > -119.5 && Centre.X < -49.5)
			|| (Centre.X > -42.5 && Centre.X < 53.5);
		const bool bOpaqueUnderWindshield = bFrontOrRearGlassX
			&& Centre.Z > 50.0 && FMath::Abs(Centre.Y) < 56.0;
		const double CabinTop = Curve(Centre.X,
			{{-135, 42}, {-82, 83}, {-60, 92}, {-6, 94}, {17, 86}, {66, 41}});
		const double SideV = (Centre.Z - 42.0) / FMath::Max(1.0, CabinTop - 42.0);
		const bool bOpaqueUnderSideGlass = bSideGlassX
			&& SideV > 0.16 && SideV < 0.87
			&& FMath::Abs(Centre.Y) > 55.0 && FMath::Abs(Centre.Y) < 85.0
			&& FMath::Abs(Normal.Y) > 0.55;
		if (bOpaqueUnderWindshield || bOpaqueUnderSideGlass)
		{
			++OpaqueBackingTriangles;
			if (OpaqueBackingTriangles <= 3)
			{
				UE_LOG(LogBuildSedanVisual, Warning,
					TEXT("Opaque glass-backing candidate centre=%s normal=%s sideV=%.3f wind=%d side=%d"),
					*Centre.ToString(), *Normal.ToString(), SideV,
					bOpaqueUnderWindshield, bOpaqueUnderSideGlass);
			}
		}
	}
	if (GlassTriangles < MinimumGlassTriangles || OpaqueBackingTriangles > 0)
	{
		UE_LOG(LogBuildSedanVisual, Warning,
			TEXT("Glazing validation glassTriangles=%d opaqueBackingTriangles=%d"),
			GlassTriangles, OpaqueBackingTriangles);
	}
	return GlassTriangles >= MinimumGlassTriangles && OpaqueBackingTriangles == 0;
}

bool ValidateDriverDoorApertureGeometry(const FMeshDescription& Mesh)
{
	const FStaticMeshConstAttributes Attributes(Mesh);
	const auto Slots = Attributes.GetPolygonGroupMaterialSlotNames();
	const auto Positions = Attributes.GetVertexPositions();
	for (FTriangleID Triangle : Mesh.Triangles().GetElementIDs())
	{
		FVector Points[3];
		int32 PointIndex = 0;
		for (FVertexInstanceID Instance : Mesh.GetTriangleVertexInstances(Triangle))
		{
			Points[PointIndex++] = FVector(
				Positions[Mesh.GetVertexInstanceVertex(Instance)]);
		}
		const FVector Centre = (Points[0] + Points[1] + Points[2]) / 3.0;
		const FVector Normal = FVector::CrossProduct(
			Points[2] - Points[0], Points[1] - Points[0]).GetSafeNormal();
		const FName Slot = Slots[Mesh.GetTrianglePolygonGroup(Triangle)];
		const bool bFixedPaintBehindDoor = Slot == FName(Finishes[Paint].Name)
			&& Centre.X > DriverDoorRearX + 0.5
			&& Centre.X < DriverDoorFrontX - 0.5
			&& Centre.Y < -80.0 && Centre.Z > -21.0 && Centre.Z < 45.0
			&& FMath::Abs(Normal.Y) > 0.5;
		const bool bFixedFrontDoorGlass = Slot == FName(Finishes[Glass].Name)
			&& Centre.X > -42.5 && Centre.X < 53.5
			&& Centre.Y < -50.0 && FMath::Abs(Normal.Y) > 0.55;
		if (bFixedPaintBehindDoor || bFixedFrontDoorGlass)
		{
			UE_LOG(LogBuildSedanVisual, Error,
				TEXT("Fixed body geometry remains behind left-front door at %s slot=%s"),
				*Centre.ToString(), *Slot.ToString());
			return false;
		}
	}
	return true;
}

bool ValidateDriverDoorGeometry(const FMeshDescription& Mesh)
{
	const FStaticMeshConstAttributes Attributes(Mesh);
	const auto Slots = Attributes.GetPolygonGroupMaterialSlotNames();
	const auto Positions = Attributes.GetVertexPositions();
	int32 ExteriorPaintTriangles = 0;
	int32 SideGlassTriangles = 0;
	int32 InnerTrimTriangles = 0;
	for (FTriangleID Triangle : Mesh.Triangles().GetElementIDs())
	{
		FVector Points[3];
		int32 PointIndex = 0;
		for (FVertexInstanceID Instance : Mesh.GetTriangleVertexInstances(Triangle))
		{
			Points[PointIndex++] = FVector(
				Positions[Mesh.GetVertexInstanceVertex(Instance)]);
		}
		const FVector Centre = (Points[0] + Points[1] + Points[2]) / 3.0;
		const FVector Normal = FVector::CrossProduct(
			Points[2] - Points[0], Points[1] - Points[0]).GetSafeNormal();
		const FName Slot = Slots[Mesh.GetTrianglePolygonGroup(Triangle)];
		if (Slot == FName(Finishes[Paint].Name)
			&& Centre.X > DriverDoorRearX && Centre.X < DriverDoorFrontX
			&& Centre.Y < -80.0 && Centre.Z < 45.0
			&& Normal.Y < -0.5)
		{
			++ExteriorPaintTriangles;
		}
		if (Slot == FName(Finishes[Glass].Name)
			&& Centre.X > -43.0 && Centre.X < 54.0
			&& Centre.Y < -50.0 && FMath::Abs(Normal.Y) > 0.55)
		{
			++SideGlassTriangles;
		}
		if (Slot == FName(Finishes[Black].Name)
			&& Centre.X > DriverDoorRearX && Centre.X < DriverDoorFrontX
			&& Centre.Y > -90.0 && Centre.Z < 42.0 && Normal.Y > 0.5)
		{
			++InnerTrimTriangles;
		}
	}
	if (ExteriorPaintTriangles < 400 || SideGlassTriangles < 100
		|| InnerTrimTriangles < 300)
	{
		UE_LOG(LogBuildSedanVisual, Error,
			TEXT("Driver door separation invalid: exterior=%d glass=%d inner=%d"),
			ExteriorPaintTriangles, SideGlassTriangles, InnerTrimTriangles);
		return false;
	}
	return ValidateTransparentGlazingGeometry(Mesh, 100);
}

bool ValidateMesh(UStaticMesh* Mesh, bool bWheel)
{
	if (!Mesh || Mesh->GetNumLODs() != 1 || Mesh->GetStaticMaterials().Num() != FinishCount || !Mesh->GetMeshDescription(0))
	{
		UE_LOG(LogBuildSedanVisual, Error, TEXT("Missing mesh, LOD, material slots or editable source description."));
		return false;
	}
	const FBox Box = Mesh->GetBoundingBox();
	const int32 Triangles = Mesh->GetMeshDescription(0)->Triangles().Num();
	const bool bBounds = bWheel
		? (Box.Min.X < -31.9 && Box.Max.X > 31.9 && Box.Min.Z < -31.9 && Box.Max.Z > 31.9 && Box.Max.Y < 13 && Box.Min.Y > -13)
		: (Box.Min.X < -228 && Box.Max.X > 201 && Box.Max.X < 206 && Box.Min.X > -233 && Box.Max.Z > 90 && Box.Max.Z < 100 && Box.Max.Y < 108 && Box.Min.Y > -108);
	if (!bBounds || Triangles < 1000 || Triangles > 40000)
	{
		UE_LOG(LogBuildSedanVisual, Error, TEXT("Invalid sedan bounds/triangle count: %s, triangles=%d"), *Box.ToString(), Triangles);
		return false;
	}
	for (int32 Index = 0; Index < FinishCount; ++Index)
	{
		const FStaticMaterial& Slot = Mesh->GetStaticMaterials()[Index];
		if (!Slot.MaterialInterface || Slot.MaterialSlotName != FName(Finishes[Index].Name)) { return false; }
	}
	if (!bWheel && (!Mesh->bAllowCPUAccess
		|| !ValidateIndicatorLenses(*Mesh->GetMeshDescription(0))
		|| !ValidateTransparentGlazingGeometry(*Mesh->GetMeshDescription(0))
		|| !ValidateDriverDoorApertureGeometry(*Mesh->GetMeshDescription(0))))
	{
		UE_LOG(LogBuildSedanVisual, Error,
			TEXT("Body needs CPU-readable indicators, true glazing and a real left-front door aperture; run -UpdateDriverDoor."));
		return false;
	}
	UE_LOG(LogBuildSedanVisual, Display, TEXT("Validated %s: triangles=%d bounds=%s materials=%d"),
		*Mesh->GetPathName(), Triangles, *Box.ToString(), Mesh->GetStaticMaterials().Num());
	return true;
}

bool ValidateDriverDoorMesh(UStaticMesh* Mesh)
{
	if (!Mesh || Mesh->GetNumLODs() != 1
		|| Mesh->GetStaticMaterials().Num() != FinishCount
		|| !Mesh->GetMeshDescription(0) || !Mesh->bAllowCPUAccess)
	{
		UE_LOG(LogBuildSedanVisual, Error,
			TEXT("Missing driver-door mesh, LOD, material slots or editable source description."));
		return false;
	}
	const FBox Box = Mesh->GetBoundingBox();
	const int32 Triangles = Mesh->GetMeshDescription(0)->Triangles().Num();
	// Includes the exterior mirror at Y=-106.4 and the inward-tapered upper
	// frame at Y=-62.0. Tight two-sided ranges catch a misplaced/full-body mesh
	// without rejecting the authored greenhouse taper.
	const bool bBounds = Box.Min.X >= DriverDoorRearX - 2.0
		&& Box.Min.X <= DriverDoorRearX + 2.0
		&& Box.Max.X >= DriverDoorFrontX - 2.0
		&& Box.Max.X <= DriverDoorFrontX + 2.0
		&& Box.Min.Y >= -108.0 && Box.Min.Y <= -104.0
		&& Box.Max.Y >= -65.0 && Box.Max.Y <= -59.0
		&& Box.Min.Z >= -27.0 && Box.Min.Z <= -22.0
		&& Box.Max.Z >= 88.0 && Box.Max.Z <= 93.0;
	if (!bBounds || Triangles < 2400 || Triangles > 3400)
	{
		UE_LOG(LogBuildSedanVisual, Error,
			TEXT("Invalid driver-door bounds/triangle count: %s, triangles=%d"),
			*Box.ToString(), Triangles);
		return false;
	}
	for (int32 Index = 0; Index < FinishCount; ++Index)
	{
		const FStaticMaterial& Slot = Mesh->GetStaticMaterials()[Index];
		if (!Slot.MaterialInterface
			|| Slot.MaterialSlotName != FName(Finishes[Index].Name))
		{
			return false;
		}
	}
	if (!ValidateDriverDoorGeometry(*Mesh->GetMeshDescription(0)))
	{
		return false;
	}
	UE_LOG(LogBuildSedanVisual, Display,
		TEXT("Validated %s: triangles=%d bounds=%s materials=%d"),
		*Mesh->GetPathName(), Triangles, *Box.ToString(),
		Mesh->GetStaticMaterials().Num());
	return true;
}

bool MakeMesh(bool bWheel, const TArray<UMaterial*>& Materials, bool bValidateOnly, bool bRegenerate)
{
	const FString Name = bWheel ? TEXT("SM_SedanWheel") : TEXT("SM_SedanBody");
	const FString Path = FString(AssetRoot) + Name;
	UStaticMesh* Mesh = nullptr;
	bool bExisting = FPackageName::DoesPackageExist(Path);
	if (bExisting)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, *(Path + TEXT(".") + Name));
		if (!Mesh) { return false; }
		if (!bRegenerate || bValidateOnly) { return ValidateMesh(Mesh, bWheel); }
		if (!IsAuthoredAsset(Mesh))
		{
			UE_LOG(LogBuildSedanVisual, Error, TEXT("Refusing to replace non-authored asset: %s"), *Path);
			return false;
		}
	}
	else if (bValidateOnly) { return false; }
	else { Mesh = NewObject<UStaticMesh>(CreatePackage(*Path), *Name, RF_Public | RF_Standalone); }
	FAuthor Author;
	if (bWheel) { BuildWheel(Author); } else { BuildBody(Author); }
	if (Author.bInvalidTriangle || Author.TriangleCount > 40000) { return false; }
	Mesh->SetNumSourceModels(1);
	FMeshBuildSettings& Build = Mesh->GetSourceModel(0).BuildSettings;
	Build.bRecomputeNormals = false;
	Build.bRecomputeTangents = false;
	Build.bGenerateLightmapUVs = false;
	Build.bRemoveDegenerates = true;
	Mesh->GetStaticMaterials().Reset();
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Materials[Index], FName(Finishes[Index].Name), FName(Finishes[Index].Name)));
	}
	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	Mesh->bAllowCPUAccess = !bWheel;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bFastBuild = false;
	BuildParams.bCommitMeshDescription = true;
	const TArray<const FMeshDescription*> Descriptions = { &Author.Mesh };
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, BuildParams)) { return false; }
	if (!bExisting) { FAssetRegistryModule::AssetCreated(Mesh); }
	if (!ValidateMesh(Mesh, bWheel) || !SaveAuthoredAsset(Mesh)) { return false; }
	return true;
}

bool MakeDriverDoorMesh(const TArray<UMaterial*>& Materials,
	const bool bValidateOnly, const bool bRegenerate)
{
	const FString Name = TEXT("SM_SedanDoorLeft");
	const FString Path = SimCoreSedanVisualContract::DriverDoorPackagePath();
	UStaticMesh* Mesh = nullptr;
	const bool bExisting = FPackageName::DoesPackageExist(Path);
	if (bExisting)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr,
			SimCoreSedanVisualContract::DriverDoorObjectPath());
		if (!Mesh)
		{
			return false;
		}
		if (!bRegenerate || bValidateOnly)
		{
			return ValidateDriverDoorMesh(Mesh);
		}
		if (!IsAuthoredAsset(Mesh))
		{
			UE_LOG(LogBuildSedanVisual, Error,
				TEXT("Refusing to replace non-authored asset: %s"), *Path);
			return false;
		}
	}
	else if (bValidateOnly)
	{
		return false;
	}
	else
	{
		Mesh = NewObject<UStaticMesh>(CreatePackage(*Path), *Name,
			RF_Public | RF_Standalone);
	}
	FAuthor Author;
	BuildDriverDoor(Author);
	if (Author.bInvalidTriangle || Author.TriangleCount > 6000)
	{
		return false;
	}
	Mesh->SetNumSourceModels(1);
	FMeshBuildSettings& Build = Mesh->GetSourceModel(0).BuildSettings;
	Build.bRecomputeNormals = false;
	Build.bRecomputeTangents = false;
	Build.bGenerateLightmapUVs = false;
	Build.bRemoveDegenerates = true;
	Mesh->GetStaticMaterials().Reset();
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Materials[Index],
			FName(Finishes[Index].Name), FName(Finishes[Index].Name)));
	}
	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	Mesh->bAllowCPUAccess = true;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bFastBuild = false;
	BuildParams.bCommitMeshDescription = true;
	const TArray<const FMeshDescription*> Descriptions = { &Author.Mesh };
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, BuildParams))
	{
		return false;
	}
	if (!bExisting)
	{
		FAssetRegistryModule::AssetCreated(Mesh);
	}
	return ValidateDriverDoorMesh(Mesh) && SaveAuthoredAsset(Mesh);
}

bool ValidateFleetMesh(UStaticMesh* Mesh, const TCHAR* Name)
{
	if (!Mesh || !IsAuthoredAsset(Mesh) || !Mesh->bAllowCPUAccess
		|| Mesh->GetNumSourceModels() != 1 || !Mesh->GetMeshDescription(0)
		|| Mesh->GetStaticMaterials().Num() != FinishCount)
	{
		return false;
	}
	const int32 Triangles = Mesh->GetMeshDescription(0)->Triangles().Num();
	const FBox Box = Mesh->GetBoundingBox();
	const FVector Size = Box.GetSize();
	if (Triangles < 80 || Triangles > 35000 || !Box.IsValid
		|| Size.ContainsNaN() || Size.X < 150.0 || Size.X > 750.0
		|| Size.Y < 35.0 || Size.Y > 300.0 || Size.Z < 35.0 || Size.Z > 350.0)
	{
		return false;
	}
	for (int32 Index = 0; Index < FinishCount; ++Index)
	{
		if (Mesh->GetStaticMaterials()[Index].MaterialSlotName
			!= FName(Finishes[Index].Name)) return false;
	}
	if (FName(Name) == FName(TEXT("SM_TruckBody"))
		&& !ValidateTruckCabinGeometry(*Mesh->GetMeshDescription(0))) return false;
	UE_LOG(LogBuildSedanVisual, Display,
		TEXT("Validated fleet mesh %s: triangles=%d bounds=%s"),
		Name, Triangles, *Box.ToString());
	return true;
}

bool MakeFleetMesh(const TCHAR* Name, TFunctionRef<void(FAuthor&)> Builder,
	const TArray<UMaterial*>& Materials, const bool bValidateOnly)
{
	const FString Path = FString(FleetAssetRoot) + Name;
	UStaticMesh* Mesh = nullptr;
	const bool bExisting = FPackageName::DoesPackageExist(Path);
	if (bExisting)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, *(Path + TEXT(".") + Name));
		if (!Mesh) return false;
		if (bValidateOnly) return ValidateFleetMesh(Mesh, Name);
		if (!IsAuthoredAsset(Mesh))
		{
			UE_LOG(LogBuildSedanVisual, Error,
				TEXT("Refusing to replace non-authored fleet mesh: %s"), *Path);
			return false;
		}
	}
	else if (bValidateOnly)
	{
		return false;
	}
	else
	{
		Mesh = NewObject<UStaticMesh>(CreatePackage(*Path), Name,
			RF_Public | RF_Standalone);
	}

	FAuthor Author;
	Builder(Author);
	if (Author.bInvalidTriangle || Author.TriangleCount < 80
		|| Author.TriangleCount > 35000) return false;
	if (FName(Name) == FName(TEXT("SM_TruckBody"))
		&& !ValidateTruckCabinGeometry(Author.Mesh)) return false;
	Mesh->SetNumSourceModels(1);
	FMeshBuildSettings& Build = Mesh->GetSourceModel(0).BuildSettings;
	Build.bRecomputeNormals = false;
	Build.bRecomputeTangents = false;
	Build.bGenerateLightmapUVs = false;
	Build.bRemoveDegenerates = true;
	Mesh->GetStaticMaterials().Reset();
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Materials[Index],
			FName(Finishes[Index].Name), FName(Finishes[Index].Name)));
	}
	Mesh->bAllowCPUAccess = true;
	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bFastBuild = false;
	BuildParams.bCommitMeshDescription = true;
	const TArray<const FMeshDescription*> Descriptions = {&Author.Mesh};
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, BuildParams)) return false;
	if (!bExisting) FAssetRegistryModule::AssetCreated(Mesh);
	return SaveAuthoredAsset(Mesh) && ValidateFleetMesh(Mesh, Name);
}
}

UBuildSedanVisualCommandlet::UBuildSedanVisualCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UBuildSedanVisualCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("UpdateNpcFleet")))
	{
		const bool bValidateOnly = FParse::Param(*Params, TEXT("ValidateOnly"));
		const bool bTruckOnly = FParse::Param(*Params, TEXT("TruckOnly"));
		TArray<UMaterial*> Materials;
		for (int32 Index = 0; Index < FinishCount; ++Index)
		{
			UMaterial* Material = MakeMaterial(Index, true, false);
			if (!Material)
			{
				UE_LOG(LogBuildSedanVisual, Error,
					TEXT("Fleet generation requires the validated sedan material set."));
				return 1;
			}
			Materials.Add(Material);
		}
		const bool bOk = (bTruckOnly || MakeFleetMesh(TEXT("SM_CompactBody"), BuildCompactBody,
			Materials, bValidateOnly))
			&& MakeFleetMesh(TEXT("SM_TruckBody"), BuildTruckBody,
				Materials, bValidateOnly)
			&& (bTruckOnly || MakeFleetMesh(TEXT("SM_MotorcycleBody"), BuildMotorcycleBody,
				Materials, bValidateOnly));
		if (bOk)
		{
			UE_LOG(LogBuildSedanVisual, Display,
				TEXT("NPC fleet %s: %s authored meshes."),
				bValidateOnly ? TEXT("validation") : TEXT("update"),
				bTruckOnly ? TEXT("truck") : TEXT("compact, truck and motorcycle"));
		}
		else
		{
			UE_LOG(LogBuildSedanVisual, Error,
				TEXT("NPC fleet %s failed."),
				bValidateOnly ? TEXT("validation") : TEXT("update"));
		}
		return bOk ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("UpdateDriverDoor")))
	{
		// Scoped migration: derive the moving door from the existing owned body
		// materials, create/validate it first, then cut only its matching aperture
		// from the body. Wheels and materials are not regenerated.
		UStaticMesh* Body = LoadObject<UStaticMesh>(nullptr,
			SimCoreSedanVisualContract::BodyObjectPath());
		if (!Body || !IsAuthoredAsset(Body)
			|| Body->GetStaticMaterials().Num() != FinishCount)
		{
			UE_LOG(LogBuildSedanVisual, Error,
				TEXT("Driver-door update requires the existing self-authored sedan body."));
			return 1;
		}
		TArray<UMaterial*> Materials;
		for (int32 Index = 0; Index < FinishCount; ++Index)
		{
			const FStaticMaterial& Slot = Body->GetStaticMaterials()[Index];
			auto* Material = Cast<UMaterial>(Slot.MaterialInterface);
			if (!Material || Slot.MaterialSlotName != FName(Finishes[Index].Name)
				|| !ValidateDentMaterial(Material))
			{
				UE_LOG(LogBuildSedanVisual, Error,
					TEXT("Driver-door update refused: body material slot %d was replaced or invalid."),
					Index);
				return 1;
			}
			Materials.Add(Material);
		}
		const bool bValidateOnly = FParse::Param(*Params, TEXT("ValidateOnly"));
		if (bValidateOnly)
		{
			return ValidateMesh(Body, false)
				&& MakeDriverDoorMesh(Materials, true, false) ? 0 : 1;
		}
		if (!MakeDriverDoorMesh(Materials, false, true)
			|| !MakeMesh(false, Materials, false, true))
		{
			return 1;
		}
		UE_LOG(LogBuildSedanVisual, Display,
			TEXT("Driver-door update passed: body aperture + SM_SedanDoorLeft; wheel and materials unchanged."));
		return 0;
	}
	if (FParse::Param(*Params, TEXT("UpdateGlass")))
	{
		// Scoped migration: preserve the wheel and every owned material except
		// M_Sedan_Glass, then rebuild the generated body to cut window openings.
		UStaticMesh* Body = LoadObject<UStaticMesh>(nullptr,
			SimCoreSedanVisualContract::BodyObjectPath());
		if (!Body || !IsAuthoredAsset(Body)
			|| Body->GetStaticMaterials().Num() != FinishCount)
		{
			UE_LOG(LogBuildSedanVisual, Error,
				TEXT("Glass update requires the existing self-authored sedan body."));
			return 1;
		}
		TArray<UMaterial*> Materials;
		for (int32 Index = 0; Index < FinishCount; ++Index)
		{
			const FStaticMaterial& Slot = Body->GetStaticMaterials()[Index];
			auto* Material = Cast<UMaterial>(Slot.MaterialInterface);
			if (!Material || Slot.MaterialSlotName != FName(Finishes[Index].Name)
				|| !ValidateDentMaterial(Material))
			{
				UE_LOG(LogBuildSedanVisual, Error,
					TEXT("Glass update refused: body material slot %d was replaced or invalid."), Index);
				return 1;
			}
			Materials.Add(Material);
		}
		const FString GlassPath = FString(AssetRoot) + TEXT("Materials/")
			+ Finishes[Glass].Name + TEXT(".") + Finishes[Glass].Name;
		if (!IsAuthoredAsset(Materials[Glass])
			|| Materials[Glass]->GetPathName() != GlassPath)
		{
			return 1;
		}
		const bool bValidateOnly = FParse::Param(*Params, TEXT("ValidateOnly"));
		if (bValidateOnly)
		{
			return ValidateTransparentGlass(Materials[Glass])
				&& ValidateMesh(Body, false)
				&& MakeDriverDoorMesh(Materials, true, false) ? 0 : 1;
		}
		ConfigureTransparentGlass(Materials[Glass]);
		UMaterialEditingLibrary::RecompileMaterial(Materials[Glass]);
		if (!ValidateTransparentGlass(Materials[Glass])
			|| !SaveAuthoredAsset(Materials[Glass])
			|| !MakeDriverDoorMesh(Materials, false, true)
			|| !MakeMesh(false, Materials, false, true))
		{
			return 1;
		}
		UE_LOG(LogBuildSedanVisual, Display,
			TEXT("Glass update passed: body + driver door + M_Sedan_Glass; true apertures, 24%% translucent two-sided glazing; wheel unchanged."));
		return 0;
	}
	if (FParse::Param(*Params, TEXT("UpdateIndicators")))
	{
		// Scoped migration: never regenerate wheels, replace unrelated materials,
		// or overwrite a body without the project's generated-asset marker.
		UStaticMesh* Body = LoadObject<UStaticMesh>(nullptr, SimCoreSedanVisualContract::BodyObjectPath());
		if (!Body || !IsAuthoredAsset(Body) || Body->GetStaticMaterials().Num() != FinishCount)
		{
			UE_LOG(LogBuildSedanVisual, Error, TEXT("Indicator update requires the existing self-authored sedan body."));
			return 1;
		}
		TArray<UMaterial*> Materials;
		for (int32 Index = 0; Index < FinishCount; ++Index)
		{
			const FStaticMaterial& Slot = Body->GetStaticMaterials()[Index];
			auto* Material = Cast<UMaterial>(Slot.MaterialInterface);
			if (!Material || Slot.MaterialSlotName != FName(Finishes[Index].Name)
				|| !ValidateDentMaterial(Material))
			{
				UE_LOG(LogBuildSedanVisual, Error, TEXT("Indicator update refused: body material slot %d was replaced or is invalid."), Index);
				return 1;
			}
			Materials.Add(Material);
		}
		const FString AmberPath = FString(AssetRoot) + TEXT("Materials/") + Finishes[Amber].Name
			+ TEXT(".") + Finishes[Amber].Name;
		if (!IsAuthoredAsset(Materials[Amber]) || Materials[Amber]->GetPathName() != AmberPath) return 1;
		const bool bValidateOnly = FParse::Param(*Params, TEXT("ValidateOnly"));
		if (bValidateOnly) return ValidateIndicatorMaterial(Materials[Amber])
			&& ValidateMesh(Body, false)
			&& MakeDriverDoorMesh(Materials, true, false) ? 0 : 1;
		Materials[Amber] = MakeMaterial(Amber, false, true);
		if (!Materials[Amber]
			|| !MakeDriverDoorMesh(Materials, false, true)
			|| !MakeMesh(false, Materials, false, true)) return 1;
		UE_LOG(LogBuildSedanVisual, Display,
			TEXT("Indicator update passed: body + Amber only; 4 curved lens strips, CPU access enabled; other materials/wheel unchanged."));
		return 0;
	}
	if (FParse::Param(*Params, TEXT("EnableDentCpuAccess")))
	{
		UStaticMesh* Body = LoadObject<UStaticMesh>(nullptr, SimCoreSedanVisualContract::BodyObjectPath());
		if (!Body || !IsAuthoredAsset(Body)) return 1;
		Body->bAllowCPUAccess = true;
		if (!SaveAuthoredAsset(Body)) return 1;
		const TCHAR* LampPath = TEXT("/Game/Vehicles/Sedan/Materials/M_Sedan_TurnIndicator");
		if (FPackageName::DoesPackageExist(LampPath))
		{
			return IsAuthoredAsset(LoadObject<UMaterial>(nullptr,
				TEXT("/Game/Vehicles/Sedan/Materials/M_Sedan_TurnIndicator.M_Sedan_TurnIndicator"))) ? 0 : 1;
		}
		UMaterial* Lamp = NewObject<UMaterial>(CreatePackage(LampPath), TEXT("M_Sedan_TurnIndicator"), RF_Public | RF_Standalone);
		Lamp->SetShadingModel(MSM_Unlit);
		auto* Emission = CastChecked<UMaterialExpressionConstant3Vector>(
			UMaterialEditingLibrary::CreateMaterialExpression(Lamp, UMaterialExpressionConstant3Vector::StaticClass()));
		Emission->Constant = FLinearColor(8.0f, 1.8f, 0.025f);
		UMaterialEditingLibrary::ConnectMaterialProperty(Emission, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::RecompileMaterial(Lamp);
		FAssetRegistryModule::AssetCreated(Lamp);
		return SaveAuthoredAsset(Lamp) ? 0 : 1;
	}
	const bool bValidateOnly = FParse::Param(*Params, TEXT("ValidateOnly"));
	const bool bRegenerate = FParse::Param(*Params, TEXT("Regenerate"));
	TArray<UMaterial*> Materials;
	for (int32 Index = 0; Index < FinishCount; ++Index)
	{
		UMaterial* Material = MakeMaterial(Index, bValidateOnly, bRegenerate);
		if (!Material)
		{
			UE_LOG(LogBuildSedanVisual, Error, TEXT("Material missing or invalid: %s; existing assets are never silently replaced."), Finishes[Index].Name);
			return 1;
		}
		Materials.Add(Material);
	}
	if (!MakeDriverDoorMesh(Materials, bValidateOnly, bRegenerate)
		|| !MakeMesh(false, Materials, bValidateOnly, bRegenerate)
		|| !MakeMesh(true, Materials, bValidateOnly, bRegenerate)) { return 1; }
	UE_LOG(LogBuildSedanVisual, Display,
		TEXT("Sedan visual %s: authored curved body and double-sided alloy wheel, centimeter coordinates, no external assets or physics SDK. Existing unrelated content preserved."),
		bValidateOnly ? TEXT("validation passed") : TEXT("generation passed"));
	return 0;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTruckCabinVisibilityTest,
	"DriveIntegration.NpcFleet.TruckCabinVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTruckCabinVisibilityTest::RunTest(const FString& Parameters)
{
	FAuthor Truck;
	BuildTruckBody(Truck);
	bool bOk = TestFalse(TEXT("Truck shell triangles are finite"), Truck.bInvalidTriangle);
	bOk &= TestTrue(TEXT("Driver and wheel are visible through all three cab windows"),
		ValidateTruckCabinGeometry(Truck.Mesh));
	FBox Bounds(ForceInit);
	for (const FVertexID Vertex : Truck.Mesh.Vertices().GetElementIDs())
	{
		Bounds += FVector(Truck.Attributes.GetVertexPositions()[Vertex]);
	}
	bOk &= TestTrue(TEXT("Hollow cab preserves the existing truck body bounds"),
		Bounds.Min.Equals(FVector(-325, -105, -26), 0.001)
		&& Bounds.Max.Equals(FVector(295, 105, 167), 0.001));
	// Reintroducing the old upper cab must fail even though its glass remains.
	Truck.Ellipsoid(FVector(103, 0, 105), FVector(76, 98, 62), Paint, 20, 10);
	bOk &= TestFalse(TEXT("Opaque backing behind transparent windows is rejected"),
		ValidateTruckCabinGeometry(Truck.Mesh, false));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSedanGeometryIntegrityTest,
	"DriveIntegration.SedanVisual.GeometryIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSedanGeometryIntegrityTest::RunTest(const FString& Parameters)
{
	FAuthor Body;
	FAuthor DriverDoor;
	FAuthor Wheel;
	BuildBody(Body);
	BuildDriverDoor(DriverDoor);
	BuildWheel(Wheel);
	TestFalse(TEXT("Body triangles are finite"), Body.bInvalidTriangle);
	TestFalse(TEXT("Driver door triangles are finite"), DriverDoor.bInvalidTriangle);
	TestTrue(TEXT("Four amber strips occupy the actual curved head/tail lenses"), ValidateIndicatorLenses(Body.Mesh));
	TestTrue(TEXT("Glass panels have true openings with no opaque paint backing"),
		ValidateTransparentGlazingGeometry(Body.Mesh));
	TestTrue(TEXT("Left-front body has a real aperture instead of fixed paint/glass"),
		ValidateDriverDoorApertureGeometry(Body.Mesh));
	TestTrue(TEXT("Separate left-front door owns curved paint, glazing and inner trim"),
		ValidateDriverDoorGeometry(DriverDoor.Mesh));
	TestFalse(TEXT("Wheel triangles are finite"), Wheel.bInvalidTriangle);
	TestTrue(TEXT("Assembled sedan stays below 40000 triangles"),
		Body.TriangleCount + DriverDoor.TriangleCount + 4 * Wheel.TriangleCount < 40000);
	TestTrue(TEXT("Both axle openings clear a 35.5cm circle"), ArchCut(-148.5) >= 12.5 && ArchCut(121.5) >= 12.5);
	bool bBodyClear = true;
	bool bUnitNormals = true;
	bool bFacingMatchesNormals = true;
	for (FVertexID Vertex : Body.Mesh.Vertices().GetElementIDs())
	{
		const FVector Point(Body.Attributes.GetVertexPositions()[Vertex]);
		if (FMath::Abs(Point.Y) < 68.0 || FMath::Abs(Point.Y) > 92.0) { continue; }
		for (double Axle : {-148.5, 121.5})
		{
			if (FMath::Square(Point.X - Axle) + FMath::Square(Point.Z + 23.0) < FMath::Square(35.5))
			{
				bBodyClear = false;
			}
		}
	}
	for (FAuthor* Author : { &Body, &DriverDoor, &Wheel })
	{
		for (FVertexInstanceID Instance : Author->Mesh.VertexInstances().GetElementIDs())
		{
			const FVector3f Normal = Author->Attributes.GetVertexInstanceNormals()[Instance];
			if (Normal.ContainsNaN() || !FMath::IsNearlyEqual(Normal.SizeSquared(), 1.0f, 0.002f)) { bUnitNormals = false; }
		}
		for (FTriangleID Triangle : Author->Mesh.Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexInstanceID> Instances = Author->Mesh.GetTriangleVertexInstances(Triangle);
			FVector Points[3];
			FVector NormalSum = FVector::ZeroVector;
			for (int32 I = 0; I < 3; ++I)
			{
				Points[I] = FVector(Author->Attributes.GetVertexPositions()[Author->Mesh.GetVertexInstanceVertex(Instances[I])]);
				NormalSum += FVector(Author->Attributes.GetVertexInstanceNormals()[Instances[I]]);
			}
			if (FVector::DotProduct(FVector::CrossProduct(Points[2] - Points[0], Points[1] - Points[0]), NormalSum) < -0.0001)
			{
				bFacingMatchesNormals = false;
			}
		}
	}
	TestTrue(TEXT("No body vertices fill the wheel openings"), bBodyClear);
	TestTrue(TEXT("All authored vertex normals are finite unit vectors"), bUnitNormals);
	TestTrue(TEXT("MeshDescription front-face winding agrees with authored normals"), bFacingMatchesNormals);
	bool bClosedFasciaEdges = true;
	for (double X : {-228.5, 201.5})
	{
		for (int32 I = 0; I <= 48; ++I)
		{
			const double V = I / 48.0;
			const double Across = V * 2 - 1;
			bClosedFasciaEdges &= BodyEndPoint(X, Across, 1).Equals(BodyDeckPoint(X, Across), 0.00001);
			for (double Side : {-1.0, 1.0})
			{
				bClosedFasciaEdges &= BodyEndPoint(X, Side, V).Equals(BodySidePoint(X, Side, V), 0.00001);
			}
		}
	}
	TestTrue(TEXT("Front/rear fascia share exactly the side and deck boundary curves"), bClosedFasciaEdges);
	bool bConformingLamps = true;
	for (bool bFront : {true, false})
	{
		for (double Side : {-1.0, 1.0})
		{
			for (int32 I = 0; I <= 12; ++I)
			{
				for (int32 J = 0; J <= 8; ++J)
				{
					const FVector Lamp = LampPoint(bFront, Side, I / 12.0, J / 8.0, 1.0);
					const double Across = Lamp.Y / (Width(Lamp.X) - 7.5);
					const double HeightAboveDeck = Lamp.Z - BodyDeckPoint(Lamp.X, Across).Z;
					bConformingLamps &= FMath::Abs(Across) < 1.0 && FMath::IsNearlyEqual(HeightAboveDeck, 1.0, 0.00001);
				}
			}
		}
	}
	TestTrue(TEXT("Lamp lenses follow the curved body without rectangular protrusions"), bConformingLamps);
	AddInfo(FString::Printf(TEXT("Sedan body=%d, driverDoor=%d, wheel=%d, assembled=%d triangles"),
		Body.TriangleCount, DriverDoor.TriangleCount, Wheel.TriangleCount,
		Body.TriangleCount + DriverDoor.TriangleCount + 4 * Wheel.TriangleCount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSedanDamageMaterialIntegrityTest,
	"DriveIntegration.SedanVisual.DamageMaterialIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSedanDamageMaterialIntegrityTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const FFinish& Finish : Finishes)
	{
		const FString Path = FString(AssetRoot) + TEXT("Materials/")
			+ Finish.Name + TEXT(".") + Finish.Name;
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *Path);
		bOk &= TestNotNull(*FString::Printf(TEXT("%s exists"), Finish.Name), Material);
		if (!Material)
		{
			continue;
		}
		bOk &= TestTrue(
			*FString::Printf(TEXT("%s exposes all bounded dent parameters"), Finish.Name),
			ValidateDentMaterial(Material));
		bool bHasLocalDentNode = false;
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			const UMaterialExpressionCustom* Custom =
				Cast<UMaterialExpressionCustom>(Expression);
			bHasLocalDentNode |= Custom
				&& Custom->Description == TEXT("Bounded body-local collision dents")
				&& Custom->OutputType == CMOT_Float3;
		}
		bOk &= TestTrue(
			*FString::Printf(TEXT("%s owns the body-local dent graph"), Finish.Name),
			bHasLocalDentNode);
		if (FName(Finish.Name) == FName(Finishes[Glass].Name))
		{
			bOk &= TestTrue(TEXT("Glass is translucent and two-sided"),
				ValidateTransparentGlass(Material));
		}
		if (FName(Finish.Name) == SimCoreSedanVisualContract::SignalMaterialName)
			bOk &= TestTrue(TEXT("Amber material exposes independent left/right turn signals"), ValidateIndicatorMaterial(Material));
	}
	return bOk;
}
#endif
