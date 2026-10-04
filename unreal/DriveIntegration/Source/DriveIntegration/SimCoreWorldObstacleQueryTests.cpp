#include "SimCoreWorldObstacleQuery.h"
#include "SimCoreWorldCollisionFilter.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "SimCoreSignalCityLayout.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreWorldObstacleQueryTest,
    "DriveIntegration.LiveGround.WorldObstacles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreWorldObstacleQueryTest::RunTest(const FString& Parameters)
{
    using namespace SimCoreProtocol;
    const auto EncodeRequest=[](FVector3d Extent) {
        TArray<uint8> Bytes{8,1,16,1,32,1,42,0};
        TArray<uint8> Vector;
        const auto Number=[&](uint8 Tag,double Value) {
            Vector.Add(Tag); uint64 Bits; FMemory::Memcpy(&Bits,&Value,sizeof(Bits));
            for (int I=0; I<8; ++I) Vector.Add(static_cast<uint8>(Bits>>(I*8)));
        };
        Number(9,Extent.X); Number(17,Extent.Y); Number(25,Extent.Z);
        Bytes.Add(50); Bytes.Add(static_cast<uint8>(Vector.Num())); Bytes.Append(Vector); return Bytes;
    };
    FWorldObstacleRequest Parsed;
    auto RequestBytes=EncodeRequest({6,24,8});
    TestTrue(TEXT("bounded obstacle request parses"),ParseWorldObstacleRequest(RequestBytes,Parsed));
    TestTrue(TEXT("obstacle extent preserved"),Parsed.HalfExtentEnu.Equals(FVector3d(6,24,8),0));
    for (const FVector3d Extent : {FVector3d(5,6,8),FVector3d(6,25,8),FVector3d(6,6,9)})
        TestFalse(TEXT("outside negotiated obstacle volume rejected"),ParseWorldObstacleRequest(EncodeRequest(Extent),Parsed));
    RequestBytes.Append({32,2});
    TestFalse(TEXT("duplicate obstacle revision rejected"),ParseWorldObstacleRequest(RequestBytes,Parsed));
    RequestBytes=EncodeRequest({6,24,8});
    TArray<uint8> WorldBytes{10,2,8,1,66,static_cast<uint8>(RequestBytes.Num())}; WorldBytes.Append(RequestBytes);
    TArray<uint8> Envelope{8,2,16,1,42,3,'m','a','p',58,4,'p','l','a','y',98,static_cast<uint8>(WorldBytes.Num())};
    Envelope.Append(WorldBytes); FVehicleState State; TArray<FVehicleState> Entities; FString Error;
    TestTrue(TEXT("world state carries obstacle query to player"),ParseWorldStateEnvelope(Envelope,1,State,Entities,Error));
    TestTrue(TEXT("obstacle query retained through target copy"),State.WorldObstacles.IsSet());
    Envelope.Append({146,1,0}); // envelope field18 becomes active payload
    TestFalse(TEXT("obstacle result cannot masquerade as active WorldState"),ParseWorldStateEnvelope(Envelope,1,State,Entities,Error));
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
        .EnableTraceCollision(true).SetTransactional(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,
        MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("WorldObstacleQa")),
        GetTransientPackage(),true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("isolated world"),World)) return false;
    if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    if (UClass* DebugClass=FindObject<UClass>(nullptr,TEXT("/Script/GameplayDebugger.GameplayDebuggerRenderingComponent"))) {
        auto* DebugComponent=NewObject<UPrimitiveComponent>(GetTransientPackage(),DebugClass);
        TestTrue(TEXT("render-only gameplay helper has no collision geometry"),SimCoreWorldQuery::IsRenderOnlyDebugComponent(DebugComponent));
    }
    const FVector Offset(100,-200,0);
    FWorldObstacleRequest Request; Request.RequestId=1; Request.EnvironmentEpoch=1; Request.WorldRevision=1;
    Request.CentreEnu={0,0,0}; Request.HalfExtentEnu={6,6,8};
    const auto Measure=[&]() { return SimCoreWorldObstacleQuery::Measure(World,nullptr,Offset,Request); };
    auto Result=Measure();
    TestTrue(TEXT("measured empty volume is complete"),Result.Status==EWorldObstacleStatus::Complete && Result.Colliders.IsEmpty());
    const auto AddBox=[&](AActor* Owner,FName Name,FVector Centre,FVector Extent) {
        auto* Box=NewObject<UBoxComponent>(Owner,Name,RF_Transient);
        if (!Owner->GetRootComponent()) Owner->SetRootComponent(Box);
        Owner->AddInstanceComponent(Box); Box->InitBoxExtent(Extent);
        Box->SetMobility(EComponentMobility::Movable); Box->SetWorldLocation(Offset+Centre);
        Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent(); return Box;
    };
    auto* Actor=World->SpawnActor<AActor>();
    auto* Box=AddBox(Actor,TEXT("Barrier"),FVector(200,300,100),FVector(100,20,100));
    TestFalse(TEXT("real boxes never use debug helper exclusion"),SimCoreWorldQuery::IsRenderOnlyDebugComponent(Box));
    Box->SetWorldRotation(FRotator(0,30,0)); Box->SetWorldScale3D(FVector(2,1,1));
    Result=Measure();
    TestTrue(TEXT("upright physical box complete"),Result.Status==EWorldObstacleStatus::Complete && Result.Colliders.Num()==1);
    if (Result.Colliders.Num()==1) {
        const auto& Shape=Result.Colliders[0];
        TestTrue(TEXT("collision centre uses map offset and ENU"),Shape.CentreEnu.Equals(FVector3d(3,2,1),.00001));
        TestTrue(TEXT("dimensions come from actual collision shape and scale"),Shape.HalfExtentEnu.Equals(FVector3d(2,.2,1),.00001));
        TestTrue(TEXT("heading is clockwise from north"),FMath::Abs(Shape.HeadingRad-PI/6)<.00001);
        TestTrue(TEXT("unregistered box receives stable logsafe id"),Shape.ColliderId.StartsWith(TEXT("ue_")));
        TestEqual(TEXT("stable component identity"),Measure().Colliders[0].ColliderId,Shape.ColliderId);
        const auto Bytes=SerializeWorldObstacleResult(Result,4,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map"));
        TestTrue(TEXT("obstacle result below existing frame bound"),Bytes.Num()>0 && Bytes.Num()<65536);
        FString FixtureDir; FParse::Value(FCommandLine::Get(),TEXT("LiveGroundFixtureDir="),FixtureDir);
        if (!FixtureDir.IsEmpty()) TestTrue(TEXT("write measured obstacle fixture"),FFileHelper::SaveArrayToFile(Bytes,*FPaths::Combine(FixtureDir,TEXT("obstacles-0.bin"))));
    }
    TestTrue(TEXT("player-owned collision excluded"),SimCoreWorldObstacleQuery::Measure(World,Actor,Offset,Request).Colliders.IsEmpty());
    Box->SetWorldRotation(FRotator(10,30,0)); Result=Measure();
    TestTrue(TEXT("tilted collision is unsupported, not silently flattened"),Result.Status==EWorldObstacleStatus::Unsupported && Result.Colliders.IsEmpty());
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision); Result=Measure();
    TestTrue(TEXT("disabled collision removed"),Result.Status==EWorldObstacleStatus::Complete && Result.Colliders.IsEmpty());
    auto* MeshActor=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<UStaticMeshComponent>(MeshActor,NAME_None,RF_Transient);
    MeshActor->SetRootComponent(Mesh); MeshActor->AddInstanceComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable); Mesh->SetWorldLocation(Offset);
    Mesh->SetCollisionObjectType(ECC_WorldStatic); Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"))); Mesh->RegisterComponent();
    Result=Measure(); TestTrue(TEXT("actual basic cube body accepted"),Result.Status==EWorldObstacleStatus::Complete && Result.Colliders.Num()==1);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    Result=Measure(); TestTrue(TEXT("unsupported convex invalidates whole volume"),Result.Status==EWorldObstacleStatus::Unsupported && Result.Colliders.IsEmpty());
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    auto* Ground=World->SpawnActor<AActor>();
    Ground->Tags={TEXT("SimCore.SignalCity.Generated.v2"),TEXT("SimCore.SignalCity.KoreanNorthDistrict.v8")};
    FName RoadId,CurbId;
    for (const auto& Authored : SimCoreSignalCity::BuildLayout().Boxes) {
        if (RoadId.IsNone() && Authored.bGround && !Authored.bStaticCollider) RoadId=Authored.Id;
        if (CurbId.IsNone() && Authored.bGround && Authored.bStaticCollider
            && Authored.Palette==SimCoreVirtualCity::EPalette::Curb) CurbId=Authored.Id;
    }
    auto* Road=AddBox(Ground,RoadId,FVector(0,0,-50),FVector(1000,1000,50));
    auto* Curb=AddBox(Ground,CurbId,FVector(300,0,10),FVector(100,10,10));
    Road->ComponentTags.Add(TEXT("SimCore.Surface.Rough")); Curb->ComponentTags.Add(TEXT("SimCore.Surface.Rough"));
    Result=Measure();
    TestTrue(TEXT("registry excludes road support but retains curb"),Result.Status==EWorldObstacleStatus::Complete && Result.Colliders.Num()==1);
    if (Result.Colliders.Num()==1) {
        TestEqual(TEXT("curb id matches server authored identity"),Result.Colliders[0].ColliderId,CurbId.ToString());
        TestEqual(TEXT("curb semantic beats surface tag"),Result.Colliders[0].Semantic,uint32(1));
        Curb->SetBoxExtent(FVector(150,25,12)); Result=Measure();
        TestTrue(TEXT("registry does not substitute authored dimensions"),Result.Colliders[0].HalfExtentEnu.Equals(FVector3d(1.5,.25,.12),.00001));
    }
    Ground->Destroy();
    auto* Many=World->SpawnActor<AActor>();
    for (int32 I=0; I<129; ++I) AddBox(Many,*FString::Printf(TEXT("Box_%d"),I),FVector(0,0,100),FVector(10));
    Result=Measure();
    TestTrue(TEXT("overflow does not return a truncated clear-space claim"),Result.Status==EWorldObstacleStatus::Overflow && Result.Colliders.IsEmpty());
    TestFalse(TEXT("incomplete volume still sends a result"),SerializeWorldObstacleResult(Result,5,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map")).IsEmpty());
    Result.Colliders.Add({});
    TestTrue(TEXT("incomplete result cannot carry partial shapes"),SerializeWorldObstacleResult(Result,5,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map")).IsEmpty());
    World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World);
    return true;
}
#endif
