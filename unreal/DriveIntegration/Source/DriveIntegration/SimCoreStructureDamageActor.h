#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCoreProtocol.h"
#include "SimCoreStructureDamageActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;

namespace SimCoreStructureDamage
{
	inline constexpr int32 MaxDebrisFragments = 24;
	inline constexpr int32 MaxDamageMarks = 8;
	inline constexpr int32 MaxCracksPerMark = 24;
	inline constexpr float DebrisLifetimeSeconds = 3.2f;
	/** Contact-local presentation. The original map collision is never edited. */
	DRIVEINTEGRATION_API bool BuildImpactTransform(const SimCoreProtocol::FStructureState& Structure,
		const FVector& PresentationOffsetCm, FTransform& OutTransform);
}

/** Bounded, transient facade cracks/chips. The original building remains a server collider. */
UCLASS(NotPlaceable, Transient)
class DRIVEINTEGRATION_API ASimCoreStructureDamageActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCoreStructureDamageActor();
	void ApplyAuthoritativeDamage(const SimCoreProtocol::FStructureState& Structure,
		const FVector& PresentationOffsetCm = FVector::ZeroVector);
	virtual void Tick(float DeltaSeconds) override;
	uint64 GetLastEventSequence() const { return LastEventSequence; }
	uint32 GetPresentedEventCount() const { return PresentedEventCount; }
	int32 GetDebrisCount() const;
	int32 GetCrackCount() const;
	int32 GetDamageMarkCount() const { return DamageMarks.Num(); }
	FVector GetDamageMarkLocation(int32 Index) const;
	FVector2D GetDamageMarkHalfExtentCm(int32 Index) const;
	bool GetDebrisWorldTransform(int32 Index, FTransform& Transform) const;

protected:
	virtual void PostInitializeComponents() override;

private:
	void BuildDamageMarks();
	void SpawnDebris(const SimCoreProtocol::FStructureState& Structure, const FTransform& ImpactFrame);
	void InitializeMaterials();

	UPROPERTY(VisibleAnywhere, Category="SimCore|Structure Damage")
	TObjectPtr<USceneComponent> DamageRoot;
	UPROPERTY(VisibleAnywhere, Category="SimCore|Structure Damage")
	TObjectPtr<UInstancedStaticMeshComponent> Cracks;
	UPROPERTY(VisibleAnywhere, Category="SimCore|Structure Damage")
	TObjectPtr<UInstancedStaticMeshComponent> Debris;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ColorMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CrackMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ChipMaterial;

	struct FFragment
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FRotator AngularVelocity = FRotator::ZeroRotator;
		FVector Scale = FVector::OneVector;
		bool bResting = false;
	};
	struct FDamageMark
	{
		FTransform Frame = FTransform::Identity;
		FVector2D HalfExtentCm = FVector2D::ZeroVector;
		float Severity = 0.0f;
		uint32 EventSequence = 0;
	};
	TArray<FFragment> Fragments;
	TArray<FDamageMark> DamageMarks;
	FString ColliderId;
	uint64 LastEventSequence = 0;
	uint32 PresentedEventCount = 0;
	float DebrisAgeSeconds = 0.0f;
	float PresentedDamagePercent = 0.0f;
	FVector PresentedOffsetCm = FVector::ZeroVector;
	double DebrisGroundHeightCm = 0.0;
	bool bHasPresentedDamage = false;
};
