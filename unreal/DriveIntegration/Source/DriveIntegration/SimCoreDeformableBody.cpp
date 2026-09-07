#include "SimCoreDeformableBody.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StaticMeshResources.h"

USimCoreDeformableBody::USimCoreDeformableBody(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetVisibility(false);
}

void USimCoreDeformableBody::ResetDeformation()
{
	ClearAllMeshSections();
	RestSections.Reset();
	DeformedVertexCount = 0;
	MaximumDisplacementCm = 0.0f;
	SetVisibility(false);
}

FVector USimCoreDeformableBody::DeformVertex(
	const FVector& Rest, const SimCoreDamagePresentation::FZoneWeights& Weights)
{
	if (Rest.ContainsNaN()) return Rest;
	FVector Offset = FVector::ZeroVector;
	if (Weights.bContactLocal) {
		for (const auto& Patch : Weights.Dents) {
			const FVector Center(Patch.Position.X * (Patch.Position.X >= 0 ? 203.0 : 230.0),
				-Patch.Position.Y * 90.0, 8.0);
			const FVector Inward(Patch.Inward.X,-Patch.Inward.Y,0.0);
			const FVector Tangent(-Inward.Y,Inward.X,0.0);
			const FVector Delta = Rest-Center;
			const double Radius = Patch.RadiusMeters*100.0;
			const double Across = FVector::DotProduct(Delta,Tangent)/Radius;
			const double Depth = FVector::DotProduct(Delta,Inward)/38.0;
			const double Height = Delta.Z/FMath::Max(32.0,Radius*.85);
			const double Q = Across*Across+Depth*Depth+Height*Height;
			if (Q >= 1.0) continue;
			const double Falloff = FMath::Square(1.0-Q);
			const double Fold = .90+.10*FMath::Cos(Across*13.0+Height*9.0);
			Offset += Inward*(Patch.DepthMeters*100.0*Falloff*Fold);
		}
		return Rest+Offset.GetClampedToMaxSize(28.0);
	}
	auto Panel = [&](float Weight, const FVector& Center, const FVector& Radius, const FVector& Inward)
	{
		if (!FMath::IsFinite(Weight) || Weight <= 0.0f) return;
		const FVector Q = (Rest - Center) / Radius;
		const double Falloff = FMath::Clamp(1.0 - Q.SizeSquared(), 0.0, 1.0);
		// A broad crush plus small folds changes the silhouette and panel normals.
		// Deterministic rest-space evaluation preserves earlier dents without drift.
		const double Fold = 0.86 + 0.14 * FMath::Cos(Rest.X * 0.14 + Rest.Y * 0.19 + Rest.Z * 0.23);
		Offset += Inward * (28.0 * FMath::Sqrt(FMath::Clamp(Weight, 0.0f, 1.0f))
			* Falloff * (2.0 - Falloff) * Fold);
	};
	Panel(Weights.Front, FVector(201, 0, 5), FVector(115, 125, 85), FVector(-1, 0, 0));
	Panel(Weights.Rear, FVector(-228, 0, 5), FVector(115, 125, 85), FVector(1, 0, 0));
	Panel(Weights.Left, FVector(-15, -90, 12), FVector(175, 48, 85), FVector(0, 1, 0));
	Panel(Weights.Right, FVector(-15, 90, 12), FVector(175, 48, 85), FVector(0, -1, 0));
	Panel(Weights.Roof, FVector(-25, 0, 93), FVector(135, 90, 50), FVector(0, 0, -1));
	Panel(Weights.Underbody, FVector(-15, 0, -28), FVector(175, 85, 38), FVector(0, 0, 1));
	return Rest + Offset.GetClampedToMaxSize(28.0);
}

bool USimCoreDeformableBody::ApplyDamage(UStaticMeshComponent* Source,
	const SimCoreDamagePresentation::FZoneWeights& Weights)
{
	if (!Source || !Source->GetStaticMesh()) return false;
	if ((Weights.bContactLocal && Weights.Dents.IsEmpty())
		|| (!Weights.bContactLocal && Weights.IsNearlyZero()))
	{
		SetVisibility(false);
		Source->SetVisibility(true);
		DeformedVertexCount = 0;
		MaximumDisplacementCm = 0.0f;
		return true;
	}
	if (RestSections.IsEmpty())
	{
		UStaticMesh* Mesh = Source->GetStaticMesh();
		if (!Mesh->bAllowCPUAccess || !Mesh->GetRenderData()
			|| Mesh->GetRenderData()->LODResources.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("Dent mesh requires CPU-readable LOD0: %s. Run BuildSedanVisual -EnableDentCpuAccess."), *Mesh->GetPathName());
			return false;
		}
		const auto& Sections = Mesh->GetRenderData()->LODResources[0].Sections;
		RestSections.SetNum(Sections.Num());
		for (int32 Index = 0; Index < Sections.Num(); ++Index)
		{
			auto& Rest = RestSections[Index];
			UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(Mesh, 0, Index,
				Rest.Positions, Rest.Indices, Rest.Normals, Rest.UVs, Rest.Tangents);
			CreateMeshSection(Index, Rest.Positions, Rest.Indices, Rest.Normals, Rest.UVs, {}, Rest.Tangents, false);
			// A separate MID prevents applying WPO on top of the vertex deformation.
			if (UMaterialInterface* Original = Source->GetMaterial(Sections[Index].MaterialIndex))
			{
				UMaterialInterface* Parent = Original;
				while (auto* DynamicParent = Cast<UMaterialInstanceDynamic>(Parent)) Parent = DynamicParent->Parent;
				auto* Material = UMaterialInstanceDynamic::Create(Parent, this);
				Material->CopyMaterialUniformParameters(Original);
				for (uint8 Zone = 1; Zone <= 6; ++Zone)
					Material->SetScalarParameterValue(SimCoreDamagePresentation::MaterialParameterName(
						static_cast<SimCoreProtocol::EVehicleDamageZone>(Zone)), 0.0f);
				SetMaterial(Index, Material);
			}
		}
	}
	DeformedVertexCount = 0;
	MaximumDisplacementCm = 0.0f;
	for (int32 Index = 0; Index < RestSections.Num(); ++Index)
	{
		const auto& Rest = RestSections[Index];
		TArray<FVector> Positions;
		TArray<FVector> Normals;
		TArray<FProcMeshTangent> Tangents;
		Positions.Reserve(Rest.Positions.Num());
		Normals.Reserve(Rest.Positions.Num());
		Tangents.Reserve(Rest.Positions.Num());
		for (int32 VertexIndex = 0; VertexIndex < Rest.Positions.Num(); ++VertexIndex)
		{
			const FVector& Position = Rest.Positions[VertexIndex];
			const FVector Deformed = DeformVertex(Position, Weights);
			Positions.Add(Deformed);
			// Transform the original tangent frame with the deformation derivative.
			// O(vertices), preserving authored hard edges (no quadratic overlap search).
			const FVector T = Rest.Tangents[VertexIndex].TangentX.GetSafeNormal();
			const FVector B = FVector::CrossProduct(Rest.Normals[VertexIndex], T).GetSafeNormal();
			const FVector DT = (DeformVertex(Position + T * 0.1, Weights) - Deformed).GetSafeNormal();
			const FVector DB = (DeformVertex(Position + B * 0.1, Weights) - Deformed).GetSafeNormal();
			Normals.Add(FVector::CrossProduct(DT, DB).GetSafeNormal());
			Tangents.Emplace(DT, Rest.Tangents[VertexIndex].bFlipTangentY);
			const float Displacement = FVector::Distance(Position, Deformed);
			DeformedVertexCount += Displacement > 0.01f ? 1 : 0;
			MaximumDisplacementCm = FMath::Max(MaximumDisplacementCm, Displacement);
		}
		UpdateMeshSection(Index, Positions, Normals, Rest.UVs, {}, Tangents);
	}
	SetRelativeTransform(Source->GetRelativeTransform());
	SetVisibility(DeformedVertexCount > 0);
	Source->SetVisibility(DeformedVertexCount == 0);
	return DeformedVertexCount > 0;
}
