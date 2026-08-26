#include "SimCoreStaticCollider.h"

#include "Components/BoxComponent.h"

ASimCoreStaticCollider::ASimCoreStaticCollider()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;
	SetActorEnableCollision(false);

	CollisionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBounds"));
	SetRootComponent(CollisionBounds);
	CollisionBounds->InitBoxExtent(FVector(200.0, 25.0, 100.0));
	CollisionBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionBounds->SetGenerateOverlapEvents(false);
	CollisionBounds->SetCanEverAffectNavigation(false);
	CollisionBounds->SetHiddenInGame(true);
	UpdatePreview();
}

void ASimCoreStaticCollider::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePreview();
}

#if WITH_EDITOR
void ASimCoreStaticCollider::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdatePreview();
}
#endif

void ASimCoreStaticCollider::UpdatePreview()
{
	if (CollisionBounds == nullptr)
	{
		return;
	}

	switch (Semantic)
	{
	case ESimCoreStaticColliderSemantic::Wall:
		CollisionBounds->ShapeColor = FColor(220, 60, 60);
		break;
	case ESimCoreStaticColliderSemantic::Curb:
		CollisionBounds->ShapeColor = FColor(240, 160, 30);
		break;
	case ESimCoreStaticColliderSemantic::Barrier:
		CollisionBounds->ShapeColor = FColor(150, 80, 230);
		break;
	}
	CollisionBounds->MarkRenderStateDirty();
}
