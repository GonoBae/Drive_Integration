#include "SimCoreWorldRevision.h"
#include "SimCoreWorldCollisionFilter.h"
#include "Components/PrimitiveComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ActorEditorUtils.h"

namespace {
// Hash individual values, not padding in engine structs. Sorting component
// signatures keeps scene enumeration order irrelevant.
struct FSignature {
    uint64 Value = 14695981039346656037ull;
    template<typename T> void Add(const T& Item) {
        const auto* Bytes = reinterpret_cast<const uint8*>(&Item);
        for (SIZE_T I=0; I<sizeof(T); ++I) { Value ^= Bytes[I]; Value *= 1099511628211ull; }
    }
    void Object(const UObject* Item) { const UPTRINT Address = reinterpret_cast<UPTRINT>(Item); Add(Address); }
    void Vector(const FVector& Item) { Add(Item.X); Add(Item.Y); Add(Item.Z); }
    void Transform(const FTransform& Item) {
        Vector(Item.GetTranslation()); Vector(Item.GetScale3D());
        const auto Rotation = Item.GetRotation();
        Add(Rotation.X); Add(Rotation.Y); Add(Rotation.Z); Add(Rotation.W);
    }
    void Material(const UPhysicalMaterial* Item) {
        Object(Item); if (Item) {
            Add(static_cast<uint8>(Item->SurfaceType)); Add(Item->Friction); Add(Item->Restitution);
        }
    }
    void Tags(TArray<FName> Values) {
        Values.Sort(FNameLexicalLess()); Add(Values.Num());
        for (const FName TagName : Values) {
            const FString Text=TagName.ToString(); Add(Text.Len());
            for (TCHAR Character : Text) Add(Character);
        }
    }
};
}

bool SimCoreLiveGroundPatch::FWorldRevision::Poll(UWorld* World, const AActor* IgnoredActor,
    const FVector& MapOriginCm)
{
    TArray<uint64> Current;
    bool bCurrentReady = IsInGameThread() && IsValid(World) && !MapOriginCm.ContainsNaN();
    int32 ActorsVisited = 0, ComponentsVisited = 0, Primitives = 0, Instances = 0, Materials = 0, Tags = 0;
    if (bCurrentReady) for (const ULevel* Level : World->GetLevels())
    {
        if (!IsValid(Level)) continue;
        for (AActor* Actor : Level->Actors)
        {
            if (++ActorsVisited > 32768) { bCurrentReady = false; break; }
            if (!IsValid(Actor) || Actor == IgnoredActor || FActorEditorUtils::IsABuilderBrush(Actor)) continue;
            for (UActorComponent* Component : Actor->GetComponents())
            {
                if (++ComponentsVisited > 131072) { bCurrentReady = false; break; }
                auto* Primitive = Cast<UPrimitiveComponent>(Component);
                if (!IsValid(Primitive) || !Primitive->IsRegistered()
                    || Primitive->GetCollisionObjectType() != ECC_WorldStatic
                    || !Primitive->IsQueryCollisionEnabled()) continue;
                if (SimCoreWorldQuery::IsRenderOnlyDebugComponent(Primitive)) continue;
                if (++Primitives > PrimitiveLimit) { bCurrentReady = false; break; }
                if (!Primitive->IsPhysicsStateCreated() || Primitive->IsAsyncCreatePhysicsStateRunning()
                    || Primitive->IsAsyncDestroyPhysicsStateRunning()) { bCurrentReady = false; break; }
                FSignature Signature;
                Signature.Object(Primitive); Signature.Add(Primitive->GetUniqueID());
                Tags += Actor->Tags.Num()+Primitive->ComponentTags.Num();
                if (Tags > 65536) { bCurrentReady = false; break; }
                Signature.Tags(Actor->Tags); Signature.Tags(Primitive->ComponentTags);
                Signature.Transform(Primitive->GetComponentTransform());
                Signature.Vector(Primitive->Bounds.Origin); Signature.Vector(Primitive->Bounds.BoxExtent);
                Signature.Add(static_cast<uint8>(Primitive->GetCollisionEnabled()));
                Signature.Add(Actor->GetActorEnableCollision());
                Signature.Add(Primitive->IsPhysicsStateCreated());
                for (int32 Channel=0; Channel<ECC_MAX; ++Channel)
                    Signature.Add(static_cast<uint8>(Primitive->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Channel))));
                UBodySetup* Setup = Primitive->GetBodySetup();
                Signature.Object(Setup);
                if (Setup) {
                    Signature.Add(Setup->BodySetupGuid.A); Signature.Add(Setup->BodySetupGuid.B);
                    Signature.Add(Setup->BodySetupGuid.C); Signature.Add(Setup->BodySetupGuid.D);
                    Signature.Add(static_cast<uint8>(Setup->CollisionTraceFlag));
                    Signature.Material(Setup->PhysMaterial);
                }
                Signature.Material(Primitive->BodyInstance.GetPhysMaterialOverride());
                if (auto* Mesh = Cast<UStaticMeshComponent>(Primitive)) Signature.Object(Mesh->GetStaticMesh());
                const int32 MaterialCount = Primitive->GetNumMaterials();
                Materials += MaterialCount;
                if (Materials > 65536) { bCurrentReady = false; break; }
                Signature.Add(MaterialCount);
                for (int32 I=0; I<MaterialCount; ++I) {
                    const auto* Material = Primitive->GetMaterial(I);
                    Signature.Object(Material);
                    Signature.Material(Material ? Material->GetPhysicalMaterial() : nullptr);
                }
                if (auto* Instanced = Cast<UInstancedStaticMeshComponent>(Primitive)) {
                    const int32 Count = Instanced->GetInstanceCount(); Instances += Count;
                    if (Instances > InstanceLimit) { bCurrentReady = false; break; }
                    Signature.Add(Count);
                    for (int32 I=0; I<Count; ++I) {
                        FTransform Transform;
                        if (!Instanced->GetInstanceTransform(I, Transform)) { bCurrentReady = false; break; }
                        Signature.Transform(Transform);
                    }
                }
                if (!bCurrentReady) break;
                Current.Add(Signature.Value);
            }
            if (!bCurrentReady) break;
        }
        if (!bCurrentReady) break;
    }
    if (bCurrentReady) Current.Sort();
    else Current.Reset(); // never treat a truncated fingerprint as a valid scene
    const bool bChanged = !Revision || bExplicitChange || bReady != bCurrentReady
        || PreviousWorld.Get() != World || PreviousOrigin != MapOriginCm || Previous != Current;
    if (bChanged) {
        ++Revision; bReady = bCurrentReady; Previous = MoveTemp(Current);
        PreviousWorld = World; PreviousOrigin = MapOriginCm; bExplicitChange = false;
    }
    return bChanged;
}
