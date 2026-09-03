#include "BuildSedanVisualCommandlet.h"

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
constexpr const TCHAR* AuthorKey = TEXT("SimCore.GeneratedVisual");
constexpr const TCHAR* LegacyAuthorValue = TEXT("SelfAuthoredSedanV1");
constexpr const TCHAR* AuthorValue = TEXT("SelfAuthoredSedanV2");
constexpr float MaxDentDepthCm = 12.0f;
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

void BuildMaterialGraph(
	UMaterial* Material,
	const FFinish& Finish,
	const bool bCreateBaseProperties)
{
	Material->MaxWorldPositionOffsetDisplacement = MaxDentDepthCm;
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
	if (!ValidateDentMaterial(Material) || !SaveAuthoredAsset(Material))
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
	return Curve(X, {{-228.5, 66}, {-216, 80}, {-188, 88}, {-130, 90}, {25, 90}, {135, 90}, {178, 85}, {193, 77}, {201.5, 65}});
}

double Deck(double X)
{
	return Curve(X, {{-228.5, 9}, {-216, 27}, {-185, 37}, {-115, 41}, {40, 38}, {130, 34}, {178, 29}, {193, 21}, {201.5, 9}});
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

FVector LampPoint(bool bFront, double Side, double U, double V, double Lift)
{
	const double X = bFront ? FMath::Lerp(196.0, 182.0, V) - 4.0 * U
		: FMath::Lerp(-226.0, -209.0, V) + 3.0 * U;
	const double Across = Side * FMath::Lerp(0.36, 0.96, U);
	// Keep the thin lens above the tessellated deck everywhere, including its curved nose.
	return BodyDeckPoint(X, Across) + FVector(0, 0, Lift);
}

void BuildBody(FAuthor& M)
{
	// Continuous fender/door skin with real openings around both wheel centers.
	for (double Side : {-1.0, 1.0})
	{
		M.Patch([&](double U, double V)
		{
			const double X = FMath::Lerp(-228.5, 201.5, U);
			return BodySidePoint(X, Side, V);
		}, [&](double, double) { return FVector(0, Side, 0); }, 180, 6, Paint);
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
		TArray<FVector> Belt;
		for (int32 I = 0; I <= 60; ++I)
		{
			const double X = FMath::Lerp(-204.0, 179.0, I / 60.0);
			Belt.Add(FVector(X, Side * (Width(X) - 1.0), Deck(X) - 10.0));
		}
		M.Polyline(Belt, 0.24, Paint);
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
	const auto CabinZ = [](double X)
	{
		return Curve(X, {{-135, 42}, {-82, 83}, {-60, 92}, {-6, 94}, {17, 86}, {66, 41}});
	};
	const auto CabinWidth = [](double X)
	{
		return Curve(X, {{-135, 76}, {-82, 62}, {-60, 61}, {-6, 61}, {17, 64}, {66, 77}});
	};
	M.Patch([&](double U, double V)
	{
		const double X = FMath::Lerp(-135.0, 66.0, U);
		const double Across = V * 2 - 1;
		return FVector(X, Across * CabinWidth(X), CabinZ(X) + 2.0 * (1 - Across * Across));
	}, [](double, double) { return FVector::UpVector; }, 48, 12, Paint);
	for (double Side : {-1.0, 1.0})
	{
		M.Patch([&](double U, double V)
		{
			const double X = FMath::Lerp(-135.0, 66.0, U);
			return FVector(X, Side * FMath::Lerp(78.0, CabinWidth(X), V), FMath::Lerp(42.0, CabinZ(X), V));
		}, [&](double, double) { return FVector(0, Side, 0); }, 64, 5, Paint);
		// Side glazing is inset from the A/C pillars and split by an actual broad black B pillar.
		auto SidePoint = [&](double X, double V)
		{
			return FVector(X, Side * (FMath::Lerp(78.0, CabinWidth(X), V) + 0.45), FMath::Lerp(42.0, CabinZ(X), V));
		};
		for (const FVector2D Range : {FVector2D(-120, -49), FVector2D(-43, 54)})
		{
			M.Patch([&](double U, double V)
			{
				const double X = FMath::Lerp(Range.X, Range.Y, U);
				return SidePoint(X, FMath::Lerp(0.12, 0.91, V));
			}, [&](double, double) { return FVector(0, Side, 0.2); }, 22, 3, Glass);
			TArray<FVector> Lower, Upper;
			for (int32 I = 0; I <= 22; ++I)
			{
				const double X = FMath::Lerp(Range.X, Range.Y, I / 22.0);
				Lower.Add(SidePoint(X, 0.12));
				Upper.Add(SidePoint(X, 0.91));
			}
			M.Polyline(Lower, 0.7, Chrome);
			M.Polyline(Upper, 1.0, Black);
			M.Tube(Lower[0], Upper[0], 1.0, Black);
			M.Tube(Lower.Last(), Upper.Last(), 1.0, Black);
		}
		M.Quad(SidePoint(-49, 0.10), SidePoint(-43, 0.10), SidePoint(-43, 0.94), SidePoint(-49, 0.94), FVector(0, Side, 0), Black);
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
			M.Ellipsoid(FVector(X, Side * 89.0, 24.0), FVector(6.3, 1.3, 1.4), Chrome);
		}
		M.Tube(FVector(45, Side * 79, 48), FVector(43, Side * 96, 47), 1.6, Black);
		M.Ellipsoid(FVector(44, Side * 100, 49), FVector(8.2, 6.4, 4.5), Paint);
		M.Quad(FVector(37.0, Side * 95.5, 46), FVector(37.0, Side * 104.0, 46),
			FVector(37.0, Side * 104.0, 51.5), FVector(37.0, Side * 95.5, 51.5), FVector(-1, 0, 0), Glass);
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
				return LampPoint(bFront, Side, FMath::Lerp(0.035, 0.965, U), FMath::Lerp(0.15, 0.85, V), 1.0);
			}, [](double, double) { return FVector::UpVector; }, 12, 3, bFront ? Headlamp : TailLamp);
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
	UE_LOG(LogBuildSedanVisual, Display, TEXT("Validated %s: triangles=%d bounds=%s materials=%d"),
		*Mesh->GetPathName(), Triangles, *Box.ToString(), Mesh->GetStaticMaterials().Num());
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
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bFastBuild = false;
	BuildParams.bCommitMeshDescription = true;
	const TArray<const FMeshDescription*> Descriptions = { &Author.Mesh };
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, BuildParams)) { return false; }
	if (!bExisting) { FAssetRegistryModule::AssetCreated(Mesh); }
	if (!ValidateMesh(Mesh, bWheel) || !SaveAuthoredAsset(Mesh)) { return false; }
	return true;
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
	if (!MakeMesh(false, Materials, bValidateOnly, bRegenerate) || !MakeMesh(true, Materials, bValidateOnly, bRegenerate)) { return 1; }
	UE_LOG(LogBuildSedanVisual, Display,
		TEXT("Sedan visual %s: authored curved body and double-sided alloy wheel, centimeter coordinates, no external assets or physics SDK. Existing unrelated content preserved."),
		bValidateOnly ? TEXT("validation passed") : TEXT("generation passed"));
	return 0;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSedanGeometryIntegrityTest,
	"DriveIntegration.SedanVisual.GeometryIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSedanGeometryIntegrityTest::RunTest(const FString& Parameters)
{
	FAuthor Body;
	FAuthor Wheel;
	BuildBody(Body);
	BuildWheel(Wheel);
	TestFalse(TEXT("Body triangles are finite"), Body.bInvalidTriangle);
	TestFalse(TEXT("Wheel triangles are finite"), Wheel.bInvalidTriangle);
	TestTrue(TEXT("Assembled sedan stays below 40000 triangles"), Body.TriangleCount + 4 * Wheel.TriangleCount < 40000);
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
	for (FAuthor* Author : { &Body, &Wheel })
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
	AddInfo(FString::Printf(TEXT("Sedan body=%d, wheel=%d, assembled=%d triangles"),
		Body.TriangleCount, Wheel.TriangleCount, Body.TriangleCount + 4 * Wheel.TriangleCount));
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
	}
	return bOk;
}
#endif
