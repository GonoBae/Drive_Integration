#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "SimCoreDamagePresentation.h"
#include "SimCoreDeformableBody.generated.h"

class UStaticMeshComponent;

/** Event-driven CPU panel deformation. No collision or second physics authority. */
UCLASS()
class DRIVEINTEGRATION_API USimCoreDeformableBody : public UProceduralMeshComponent
{
	GENERATED_BODY()
public:
	USimCoreDeformableBody(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	bool ApplyDamage(UStaticMeshComponent* Source, const SimCoreDamagePresentation::FZoneWeights& Weights);
	void ResetDeformation();
	static FVector DeformVertex(const FVector& Rest, const SimCoreDamagePresentation::FZoneWeights& Weights);
	int32 GetDeformedVertexCount() const { return DeformedVertexCount; }
	float GetMaximumDisplacementCm() const { return MaximumDisplacementCm; }
private:
	struct FRestSection
	{
		TArray<FVector> Positions;
		TArray<FVector> Normals;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Indices;
		TArray<FVector2D> UVs;
	};
	TArray<FRestSection> RestSections;
	int32 DeformedVertexCount = 0;
	float MaximumDisplacementCm = 0.0f;
};
