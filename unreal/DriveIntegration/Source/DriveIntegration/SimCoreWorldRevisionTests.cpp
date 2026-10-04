#include "SimCoreWorldRevision.h"
#include "SimCoreLiveGroundPatch.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreLiveWorldRevisionTest,
    "DriveIntegration.LiveGround.WorldRevision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreLiveWorldRevisionTest::RunTest(const FString& Parameters)
{
    using namespace SimCoreLiveGroundPatch;
    using namespace SimCoreProtocol;
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
        .EnableTraceCollision(true).SetTransactional(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,
        MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("WorldRevisionQa")),
        GetTransientPackage(),true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("isolated world"),World)) return false;
    if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    const auto AddBox=[&]() {
        auto* Actor=World->SpawnActor<AActor>();
        auto* Box=NewObject<UBoxComponent>(Actor,NAME_None,RF_Transient);
        Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
        Box->InitBoxExtent(FVector(1000,1000,50)); Box->SetMobility(EComponentMobility::Movable);
        Box->SetWorldLocation(FVector(0,0,-50));
        Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
        return Box;
    };
    UBoxComponent* Floor=AddBox();
    FWorldRevision Revision;
    const auto Poll=[&]() { return Revision.Poll(World,nullptr,FVector::ZeroVector); };
    TestTrue(TEXT("initial scene creates revision"),Poll());
    TestEqual(TEXT("initial revision"),Revision.GetRevision(),uint64(1));
    TestTrue(TEXT("initial scene measurable"),Revision.IsReady());
    TestFalse(TEXT("unchanged scene retains revision"),Poll());
    const auto Changed=[&](const TCHAR* Label) {
        const uint64 Before=Revision.GetRevision(); TestTrue(Label,Poll());
        TestEqual(TEXT("one monotonic increment"),Revision.GetRevision(),Before+1);
        TestFalse(TEXT("change not repeated"),Poll());
    };
    Floor->SetWorldLocation(FVector(0,0,0)); Changed(TEXT("transform detected"));
    Floor->SetBoxExtent(FVector(1200,1000,50)); Changed(TEXT("shape extent detected"));
    Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision); Changed(TEXT("collision disabled detected"));
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Changed(TEXT("collision enabled detected"));
    Floor->GetOwner()->SetActorEnableCollision(false); Changed(TEXT("actor collision disabled detected"));
    Floor->GetOwner()->SetActorEnableCollision(true); Changed(TEXT("actor collision enabled detected"));
    Floor->SetCollisionObjectType(ECC_WorldDynamic); Changed(TEXT("ground channel removal detected"));
    Floor->SetCollisionObjectType(ECC_WorldStatic); Changed(TEXT("ground channel addition detected"));
    Floor->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore); Changed(TEXT("response change detected"));
    auto* Extra=AddBox(); Changed(TEXT("new actor component detected"));
    Extra->GetOwner()->Destroy(); Changed(TEXT("removed actor detected"));
    if (auto* Body=Floor->GetBodySetup()) {
        Body->BodySetupGuid=FGuid::NewGuid(); Changed(TEXT("collision body revision detected"));
    } else AddError(TEXT("fixture must expose a body setup"));
    auto* PhysicalMaterial=NewObject<UPhysicalMaterial>(GetTransientPackage());
    Floor->SetPhysMaterialOverride(PhysicalMaterial); Changed(TEXT("physical material replacement detected"));
    PhysicalMaterial->SurfaceType=SurfaceType2; Changed(TEXT("physical surface change detected"));
    Floor->ComponentTags.Add(TEXT("SimCore.Surface.Asphalt")); Changed(TEXT("surface tag added detected"));
    Floor->ComponentTags[0]=TEXT("SimCore.Surface.Rough"); Changed(TEXT("tag-only physical surface change detected"));
    Floor->GetOwner()->Tags.Add(TEXT("SimCore.SignalCity.Generated.v2")); Changed(TEXT("actor registry tag change detected"));
    Floor->ComponentTags.Add(TEXT("Other.Tag")); Poll();
    Floor->ComponentTags.Swap(0,1); TestFalse(TEXT("tag order alone is not a scene change"),Poll());
    PhysicalMaterial->Friction=.23f; Changed(TEXT("obstacle friction change detected"));
    Revision.MarkChanged(); Changed(TEXT("explicit in-place geometry invalidation detected"));
    FWorldRevision Limited(0);
    Limited.Poll(World,nullptr,FVector::ZeroVector);
    TestFalse(TEXT("primitive budget exhaustion is not a partial scene"),Limited.IsReady());
    TestFalse(TEXT("stable overflow does not flood revision notices"),Limited.Poll(World,nullptr,FVector::ZeroVector));

    auto* MeshActor=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<UInstancedStaticMeshComponent>(MeshActor,NAME_None,RF_Transient);
    MeshActor->SetRootComponent(Mesh); MeshActor->AddInstanceComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable); Mesh->SetCollisionObjectType(ECC_WorldStatic);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (TestNotNull(TEXT("cube fixture"),Cube) && TestNotNull(TEXT("sphere fixture"),Sphere)) {
        Mesh->SetStaticMesh(Cube); Mesh->AddInstance(FTransform(FVector(0,0,-1000))); Mesh->RegisterComponent();
        Changed(TEXT("instanced body addition detected"));
        Mesh->SetStaticMesh(Sphere); Changed(TEXT("mesh replacement detected"));
        Mesh->UpdateInstanceTransform(0,FTransform(FVector(200,0,-1000)),false,true,true);
        Changed(TEXT("individual instance movement detected"));
        Mesh->AddInstance(FTransform(FVector(400,0,-1000))); Changed(TEXT("new instance detected"));
        FWorldRevision InstanceLimited(8192,1); InstanceLimited.Poll(World,nullptr,FVector::ZeroVector);
        TestFalse(TEXT("instance budget exhaustion fails closed"),InstanceLimited.IsReady());
        Mesh->RemoveInstance(1); InstanceLimited.Poll(World,nullptr,FVector::ZeroVector);
        TestTrue(TEXT("return within instance budget recovers"),InstanceLimited.IsReady());
        Mesh->GetOwner()->Destroy(); Poll();
    }

    FSampler Sampler;
    FGroundQueryBatch Request; Request.RequestId=1; Request.EnvironmentEpoch=1; Request.EntityId=1;
    Request.OriginsEnu={{0,0,8}}; Request.QueryDistancesMeters={32}; Request.PatchStepEnu={.5,.5,0};
    TestNull(TEXT("bootstrap revision cannot measure"),Sampler.Tick(World,nullptr,FVector::ZeroVector,Request));
    Request.WorldRevision=Sampler.GetWorldRevision();
    for (int32 I=0; I<3; ++I) TestNull(TEXT("partial scan"),Sampler.Tick(World,nullptr,FVector::ZeroVector,Request));
    Floor->SetWorldLocation(FVector(0,0,50));
    TestNull(TEXT("world change discards mixed-height scan"),Sampler.Tick(World,nullptr,FVector::ZeroVector,Request));
    TestTrue(TEXT("sampler revision changed"),Sampler.GetWorldRevision()>Request.WorldRevision);
    for (int32 I=0; I<8; ++I) TestNull(TEXT("old server request stays fenced"),Sampler.Tick(World,nullptr,FVector::ZeroVector,Request));
    Request.RequestId=2; Request.WorldRevision=Sampler.GetWorldRevision();
    for (int32 I=0; I<5; ++I) TestNull(TEXT("full rescan after acknowledgement"),Sampler.Tick(World,nullptr,FVector::ZeroVector,Request));
    const auto* Result=Sampler.Tick(World,nullptr,FVector::ZeroVector,Request);
    if (TestNotNull(TEXT("revised complete scan"),Result)) {
        TestEqual(TEXT("result carries acknowledged revision"),Result->WorldRevision,Request.WorldRevision);
        for (const auto& Probe:Result->Probes) TestTrue(TEXT("no stale low-height samples"),FMath::Abs(Probe.PointEnu.Z-1)<.001);
    }
    const uint64 BeforeReset=Sampler.GetWorldRevision(); Sampler.Reset();
    TestEqual(TEXT("stale reset preserves monotonic revision"),Sampler.GetWorldRevision(),BeforeReset);
    Sampler.NotifyWorldChanged();
    TestNull(TEXT("explicit invalidation also cancels scan"),Sampler.Tick(World,nullptr,FVector::ZeroVector,Request));
    TestEqual(TEXT("explicit invalidation increments revision"),Sampler.GetWorldRevision(),BeforeReset+1);
    FSampler NpcSampler;
    Request.RequestId=3; Request.WorldRevision=Sampler.GetWorldRevision();
    for (int32 I=0; I<3; ++I) TestNull(TEXT("NPC job has independent partial scan"),
        NpcSampler.TickObservedRevision(World,nullptr,FVector::ZeroVector,Request,Request.WorldRevision,true));
    TestEqual(TEXT("NPC observation does not poll another scene tracker"),NpcSampler.GetWorldRevision(),uint64(0));
    Sampler.NotifyWorldChanged(); Sampler.PollWorld(World,nullptr,FVector::ZeroVector);
    TestNull(TEXT("shared world change invalidates NPC partial scan"),NpcSampler.TickObservedRevision(
        World,nullptr,FVector::ZeroVector,Request,Sampler.GetWorldRevision(),true));
    Request.RequestId=4; Request.WorldRevision=Sampler.GetWorldRevision();
    for (int32 I=0; I<5; ++I) TestNull(TEXT("NPC rescan stays within per-frame budget"),
        NpcSampler.TickObservedRevision(World,nullptr,FVector::ZeroVector,Request,Request.WorldRevision,true));
    TestNotNull(TEXT("NPC scan finishes against shared revision"),NpcSampler.TickObservedRevision(
        World,nullptr,FVector::ZeroVector,Request,Request.WorldRevision,true));
    TestNull(TEXT("world not ready suspends NPC scan"),NpcSampler.TickObservedRevision(
        World,nullptr,FVector::ZeroVector,Request,Request.WorldRevision,false));
    World->DestroyWorld(false);
    if (GEngine) GEngine->DestroyWorldContext(World);
    return true;
}
#endif
