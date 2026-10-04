#include "SimCoreLiveGroundPatch.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreLiveGroundProtocolTest,
    "DriveIntegration.LiveGround.Protocol", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreLiveGroundProtocolTest::RunTest(const FString& Parameters)
{
    using namespace SimCoreProtocol;
    const auto Number=[](TArray<uint8>& Out, uint8 Tag, double Value) {
        Out.Add(Tag); uint64 Bits; FMemory::Memcpy(&Bits,&Value,sizeof(Bits));
        for(int I=0;I<8;++I) Out.Add(static_cast<uint8>(Bits>>(I*8)));
    };
    const auto RequestBytes=[&](FVector3d Step) {
        TArray<uint8> Bytes{8,1,16,1,32,1,42,0}; Number(Bytes,57,32);
        TArray<uint8> Vector; Number(Vector,9,Step.X); Number(Vector,17,Step.Y); Number(Vector,25,Step.Z);
        Bytes.Add(66); Bytes.Add(static_cast<uint8>(Vector.Num())); Bytes.Append(Vector); return Bytes;
    };
    FGroundQueryBatch Batch;
    const auto Bytes=RequestBytes({.5,2,0});
    TestTrue(TEXT("live grid bootstrap parses"),ParseGroundQueryBatch(Bytes,Batch));
    TestTrue(TEXT("independent grid spacings preserved"),Batch.PatchStepEnu.Equals(FVector3d(.5,2,0),0));
    for(const FVector3d Step : {FVector3d(.49,.5,0),FVector3d(.5,2.01,0),FVector3d(.5,.5,.01)})
        TestFalse(TEXT("invalid patch spacing rejected"),ParseGroundQueryBatch(RequestBytes(Step),Batch));
    auto Duplicate=Bytes; Duplicate.Append(Bytes.GetData()+17,Bytes.Num()-17);
    TestFalse(TEXT("duplicate patch spacing rejected"),ParseGroundQueryBatch(Duplicate,Batch));
    const auto WorldParses=[&](uint8 Field,const TArray<uint8>& Query) {
        TArray<uint8> World{10,2,8,1,Field,static_cast<uint8>(Query.Num())}; World.Append(Query);
        TArray<uint8> Envelope{8,2,16,1,42,3,'m','a','p',58,4,'p','l','a','y',98,static_cast<uint8>(World.Num())};
        Envelope.Append(World); FVehicleState State; TArray<FVehicleState> Entities; FString Error;
        const bool bParsed=ParseWorldStateEnvelope(Envelope,1,State,Entities,Error);
        return bParsed && (Field != 74 || (State.NpcGroundTile.IsSet() && State.NpcGroundTile->WorldRevision == 1));
    };
    TestTrue(TEXT("v2 live world request"),WorldParses(58,Bytes));
    TestFalse(TEXT("grid metadata not diagnostic queries"),WorldParses(50,Bytes));
    auto Legacy=Bytes; Legacy.SetNum(17);
    TestFalse(TEXT("v1 live request rejected"),WorldParses(58,Legacy));
    TestTrue(TEXT("exact-depth diagnostics remain valid"),WorldParses(50,Legacy));
    auto Revised=Bytes; Revised.Append({72,7});
    TestTrue(TEXT("world revision parses"),ParseGroundQueryBatch(Revised,Batch));
    TestEqual(TEXT("world revision preserved"),Batch.WorldRevision,uint64(7));
    Revised.Append({72,8});
    TestFalse(TEXT("duplicate world revision rejected"),ParseGroundQueryBatch(Revised,Batch));
    Legacy.Append({72,7});
    TestFalse(TEXT("diagnostic query cannot carry live revision"),ParseGroundQueryBatch(Legacy,Batch));
    auto NpcBytes=RequestBytes({.5,.5,0}); NpcBytes.Append({72,1});
    TestTrue(TEXT("NPC tile copied through world-state target"),WorldParses(74,NpcBytes));
    TestFalse(TEXT("NPC tile without world revision rejected"),WorldParses(74,RequestBytes({.5,.5,0})));
    auto WideNpc=RequestBytes({.5,2,0}); WideNpc.Append({72,1});
    TestFalse(TEXT("NPC tile fixed spacing contract enforced"),WorldParses(74,WideNpc));
    TestFalse(TEXT("revision notice serializes"),SerializeGroundWorldRevision(7,true,3,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map")).IsEmpty());
    TestTrue(TEXT("zero revision notice rejected"),SerializeGroundWorldRevision(0,true,3,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map")).IsEmpty());
    FGroundQueryResult Result; Result.RequestId=1; Result.EnvironmentEpoch=1; Result.WorldRevision=1;
    for(int I=0;I<625;++I) Result.Probes.Add({EGroundProbeStatus::NoHit,{},{},0});
    Result.Boundaries.Add({12*24+12,{.25,0,.1},{-1,0,0}});
    const auto Encode=[&]() { return SerializeGroundQueryResult(Result,3,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map"),true); };
    TestFalse(TEXT("measured divider serializes"),Encode().IsEmpty());
    TestFalse(TEXT("NPC tile result serializes separately"),SerializeGroundQueryResult(Result,3,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map"),true,true).IsEmpty());
    TestTrue(TEXT("NPC result cannot use diagnostic shape"),SerializeGroundQueryResult(Result,3,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map"),false,true).IsEmpty());
    const auto DuplicateBoundary=Result.Boundaries[0]; Result.Boundaries.Add(DuplicateBoundary);
    TestTrue(TEXT("duplicate divider rejected"),Encode().IsEmpty()); Result.Boundaries.Pop();
    Result.Boundaries[0].NormalEnu={0,0,1};
    TestTrue(TEXT("nonhorizontal divider rejected"),Encode().IsEmpty());
    Result.Boundaries[0].NormalEnu={-1,0,0}; Result.Boundaries[0].CellIndex=576;
    TestTrue(TEXT("out of grid divider rejected"),Encode().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreLiveGroundPatchTest,
    "DriveIntegration.LiveGround.Measurement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreLiveGroundPatchTest::RunTest(const FString& Parameters)
{
    using namespace SimCoreProtocol;
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
        .EnableTraceCollision(true).SetTransactional(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,
        MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("LiveGroundQa")),
        GetTransientPackage(),true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("isolated world"),World)) return false;
    AActor* Actor=World->SpawnActor<AActor>();
    auto* Floor=NewObject<UBoxComponent>(Actor,NAME_None,RF_Transient);
    Actor->SetRootComponent(Floor); Actor->AddInstanceComponent(Floor);
    Floor->InitBoxExtent(FVector(1000,1000,50));
    Floor->SetMobility(EComponentMobility::Movable);
    const FVector Offset(100,-200,0);
    Floor->SetWorldLocation(Offset+FVector(0,0,-30));
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Floor->SetCollisionObjectType(ECC_WorldStatic);
    Floor->SetCollisionResponseToAllChannels(ECR_Block); Floor->RegisterComponent();
    FGroundQueryBatch Request;
    Request.RequestId=1; Request.EnvironmentEpoch=1; Request.EntityId=1;
    Request.OriginsEnu={{0,0,8.6}}; Request.QueryDistancesMeters={32};
    Request.PatchStepEnu={.5,.5,0};
    SimCoreLiveGroundPatch::FSampler Sampler;
    FString FixtureDir;
    FParse::Value(FCommandLine::Get(),TEXT("LiveGroundFixtureDir="),FixtureDir);
    for(int Phase=0;Phase<3;++Phase)
    {
        if(Phase==1) Floor->SetWorldLocation(Offset+FVector(0,0,30)); // ground .2 -> .8m, no Bake
        if(Phase==2) Floor->SetWorldRotation(FRotator(15,0,0));
        Request.RequestId=Phase+1;
        Sampler.PollWorld(World,nullptr,Offset); Request.WorldRevision=Sampler.GetWorldRevision();
        for(int Frame=0;Frame<5;++Frame)
            TestNull(TEXT("partial scan must not be transmitted"),Sampler.Tick(World,nullptr,Offset,Request));
        const auto* Result=Sampler.Tick(World,nullptr,Offset,Request);
        if(!TestNotNull(TEXT("sixth frame completes grid and boundary pass"),Result)) break;
        TestEqual(TEXT("complete grid"),Result->Probes.Num(),625);
        TestTrue(TEXT("centre measured in ENU"),Result->Probes[312].PointEnu.Equals(
            FVector3d(0,0,Phase==0?.2:Phase==1?.8:Result->Probes[312].PointEnu.Z),.001));
        for(const auto& P:Result->Probes) TestTrue(TEXT("valid floor contact"),P.Status==EGroundProbeStatus::Hit);
        // Standalone host fixture cases each bootstrap revision one; the runtime
        // test above retains the real monotonic revisions between world edits.
        auto Fixture=*Result; Fixture.WorldRevision=1;
        const auto Bytes=SerializeGroundQueryResult(Fixture,Phase+3,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map"),true);
        TestTrue(TEXT("response below existing 64KiB server limit"),Bytes.Num()>0 && Bytes.Num()<65536);
        if(!FixtureDir.IsEmpty()) {
            const FString Path=FPaths::Combine(FixtureDir,FString::Printf(TEXT("patch-%d.bin"),Phase));
            TestTrue(TEXT("write requested cross-language test fixture"),FFileHelper::SaveArrayToFile(Bytes,*Path));
            if (Phase==0) TestTrue(TEXT("write NPC tile cross-language fixture"),FFileHelper::SaveArrayToFile(
                SerializeGroundQueryResult(Fixture,9,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map"),true,true),
                *FPaths::Combine(FixtureDir,TEXT("npc-tile-0.bin"))));
        }
        TestNull(TEXT("one response per request"),Sampler.Tick(World,nullptr,Offset,Request));
    }
    Sampler.Reset();
    Request.RequestId=99;
    Sampler.PollWorld(World,Actor,Offset); Request.WorldRevision=Sampler.GetWorldRevision();
    for(int Frame=0;Frame<5;++Frame) Sampler.Tick(World,Actor,Offset,Request);
    const auto* Ignored=Sampler.Tick(World,Actor,Offset,Request);
    TestTrue(TEXT("self-excluded surface yields measured NoHit"),Ignored && Ignored->Probes[312].Status==EGroundProbeStatus::NoHit);

    Floor->SetWorldRotation(FRotator::ZeroRotator); Floor->SetWorldLocation(Offset+FVector(0,0,-50));
    AActor* CurbActor=World->SpawnActor<AActor>();
    auto* Curb=NewObject<UBoxComponent>(CurbActor,NAME_None,RF_Transient);
    CurbActor->SetRootComponent(Curb); CurbActor->AddInstanceComponent(Curb);
    Curb->InitBoxExtent(FVector(1000,487.5,10)); Curb->SetMobility(EComponentMobility::Movable);
    Curb->SetWorldLocation(Offset+FVector(0,512.5,10)); // east >= .25m, top .2m
    Curb->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Curb->SetCollisionObjectType(ECC_WorldStatic);
    Curb->SetCollisionResponseToAllChannels(ECR_Block); Curb->RegisterComponent();
    const auto Finish=[&]() {
        Sampler.PollWorld(World,nullptr,Offset); Request.WorldRevision=Sampler.GetWorldRevision();
        const FGroundQueryResult* Result=nullptr;
        for(int Frame=0;Frame<16 && !Result;++Frame) Result=Sampler.Tick(World,nullptr,Offset,Request);
        return Result;
    };
    Request.RequestId=4;
    if(const auto* CurbResult=Finish())
    {
        TestEqual(TEXT("one measured boundary in every curb row"),CurbResult->Boundaries.Num(),24);
        for(const auto& Boundary:CurbResult->Boundaries)
        {
            TestTrue(TEXT("measured edge is .25m, not a grid midpoint assumption"),FMath::Abs(Boundary.PointEnu.X-.25)<.001);
            TestTrue(TEXT("side normal points from high to low"),Boundary.NormalEnu.Equals(FVector3d(-1,0,0),.001));
        }
        auto Fixture=*CurbResult; Fixture.WorldRevision=1;
        const auto Bytes=SerializeGroundQueryResult(Fixture,6,TEXT("ue"),TEXT("conn"),TEXT("play"),TEXT("map"),true);
        TestTrue(TEXT("curb result within frame limit"),Bytes.Num()>0 && Bytes.Num()<65536);
        if(!FixtureDir.IsEmpty()) TestTrue(TEXT("write measured curb fixture"),FFileHelper::SaveArrayToFile(Bytes,
            *FPaths::Combine(FixtureDir,TEXT("patch-3.bin"))));
    }
    else AddError(TEXT("curb scan did not finish within bounded frames"));
    // Move the face away from the grid midpoint: the returned divider must move with it.
    Curb->SetWorldLocation(Offset+FVector(0,524.5,10)); Request.RequestId=5;
    if(const auto* Moved=Finish())
    {
        TestEqual(TEXT("moved curb boundaries"),Moved->Boundaries.Num(),24);
        for(const auto& Boundary:Moved->Boundaries)
            TestTrue(TEXT("actual .37m curb position retained"),FMath::Abs(Boundary.PointEnu.X-.37)<.001);
    }
    else AddError(TEXT("moved curb scan did not finish"));
    // A low surface ending before the high surface is a gap, not a curb.
    Floor->SetBoxExtent(FVector(1000,487.5,50)); Floor->SetWorldLocation(Offset+FVector(0,-562.5,-50));
    Request.RequestId=6;
    if(const auto* Gap=Finish())
    {
        TestTrue(TEXT("gap has measured empty nodes"),Gap->Probes[312].Status==EGroundProbeStatus::NoHit);
        TestTrue(TEXT("gap does not fabricate a step boundary"),Gap->Boundaries.IsEmpty());
    }
    else AddError(TEXT("gap scan did not finish"));
    // Anisotropic spacing is applied independently on both axes.
    Curb->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Floor->SetBoxExtent(FVector(4000,4000,50)); Floor->SetWorldLocation(Offset+FVector(0,0,-50));
    Request.RequestId=7; Request.PatchStepEnu={2,.75,0};
    if(const auto* Wide=Finish())
        TestTrue(TEXT("measured extent uses negotiated grid spacings"),Wide->Probes[0].PointEnu.Equals(FVector3d(-24,-9,0),.001));
    else AddError(TEXT("wide scan did not finish"));
    Floor->SetWorldRotation(FRotator(4,0,0)); Request.RequestId=8;
    Sampler.PollWorld(World,nullptr,Offset); Request.WorldRevision=Sampler.GetWorldRevision();
    for(int Frame=0;Frame<5;++Frame) TestNull(TEXT("gentle plane scan remains incremental"),Sampler.Tick(World,nullptr,Offset,Request));
    const auto* Gentle=Sampler.Tick(World,nullptr,Offset,Request);
    TestTrue(TEXT("coherent gentle slope needs no sidewall trace frames"),Gentle && Gentle->Boundaries.IsEmpty());
    // A 2% longitudinal road grade changes raw corner heights by 4cm in a
    // wide cell, but the parallel curb remains a distinct 20cm surface level.
    const FRotator RoadGrade(FMath::RadiansToDegrees(FMath::Atan(.02)),0,0);
    Floor->SetWorldLocationAndRotation(Offset+RoadGrade.RotateVector(FVector(0,0,-50)),RoadGrade);
    Curb->SetBoxExtent(FVector(4000,2000,10));
    Curb->SetWorldLocationAndRotation(Offset+RoadGrade.RotateVector(FVector(0,2025,10)),RoadGrade);
    Curb->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Request.RequestId=9; Request.PatchStepEnu={2,2,0};
    if(const auto* Graded=Finish())
    {
        TestTrue(TEXT("graded fixture has distinct same-surface corner heights"),
            FMath::Abs(Graded->Probes[337].PointEnu.Z-Graded->Probes[312].PointEnu.Z)>.039);
        TestEqual(TEXT("wide graded curb measured in every row"),Graded->Boundaries.Num(),24);
        for(const auto& Boundary:Graded->Boundaries)
        {
            TestTrue(TEXT("graded curb retains measured east boundary"),FMath::Abs(Boundary.PointEnu.X-.25)<.001);
            TestTrue(TEXT("graded curb divider points from high to low"),Boundary.NormalEnu.Equals(FVector3d(-1,0,0),.001));
        }
    }
    else AddError(TEXT("graded curb scan did not finish"));
    World->DestroyWorld(false);
    return true;
}
#endif
