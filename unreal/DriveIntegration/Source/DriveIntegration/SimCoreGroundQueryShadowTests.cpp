#include "SimCoreGroundQueryShadow.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreGroundShadowProtocolTest,
	"DriveIntegration.GroundShadow.Protocol", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreGroundShadowProtocolTest::RunTest(const FString& Parameters)
{
	// Protobuf: request=1, epoch=1, entity=1, origin=(0,0,0), distance=8m.
	const TArray<uint8> Bytes{8,1,16,1,32,1,42,0,49,0,0,0,0,0,0,32,64};
	SimCoreProtocol::FGroundQueryBatch Batch;
	TestTrue(TEXT("bounded query parses"), SimCoreProtocol::ParseGroundQueryBatch(Bytes, Batch));
	TestEqual(TEXT("one probe"), Batch.OriginsEnu.Num(), 1);
	TestEqual(TEXT("distance"), Batch.MaxDistanceMeters, 8.0);
	auto Duplicate=Bytes; Duplicate.Append(TArray<uint8>{8,2});
	TestFalse(TEXT("duplicate request ID"), SimCoreProtocol::ParseGroundQueryBatch(Duplicate, Batch));
	auto Many=Bytes; for(int I=0; I<5; ++I) Many.Append(TArray<uint8>{42,0});
	TestFalse(TEXT("six probes rejected"), SimCoreProtocol::ParseGroundQueryBatch(Many, Batch));
	auto Truncated=Bytes; Truncated.Pop();
	TestFalse(TEXT("truncated fixed64"), SimCoreProtocol::ParseGroundQueryBatch(Truncated, Batch));
	// v2 carries 24m, matching the existing solver, rather than substituting 8m.
	TArray<uint8> SolverBytes{8,1,16,1,32,1};
	for (int I=0; I<64; ++I) SolverBytes.Append(TArray<uint8>{42,0,57,0,0,0,0,0,0,56,64});
	TestTrue(TEXT("64 exact-depth solver queries"), SimCoreProtocol::ParseGroundQueryBatch(SolverBytes, Batch));
	TestEqual(TEXT("solver depth retained"), Batch.QueryDistancesMeters.Num() ? Batch.QueryDistancesMeters[0] : 0.0, 24.0);
	auto TooMany = SolverBytes; TooMany.Append(TArray<uint8>{42,0,57,0,0,0,0,0,0,56,64});
	TestFalse(TEXT("65 queries rejected"), SimCoreProtocol::ParseGroundQueryBatch(TooMany, Batch));
	auto Partial = SolverBytes; Partial.SetNum(Partial.Num()-9);
	TestFalse(TEXT("partial depth vector rejected"), SimCoreProtocol::ParseGroundQueryBatch(Partial, Batch));
	auto Mixed = SolverBytes; Mixed.Append(TArray<uint8>{49,0,0,0,0,0,0,32,64});
	TestFalse(TEXT("ambiguous v1 and v2 depth modes rejected"), SimCoreProtocol::ParseGroundQueryBatch(Mixed, Batch));
	SimCoreProtocol::FGroundQueryResult Result;
	Result.RequestId=1; Result.EnvironmentEpoch=1;
	Result.Probes.Add({SimCoreProtocol::EGroundProbeStatus::NoHit,{},{},0});
	TestFalse(TEXT("no hit is a serializable measurement"), SimCoreProtocol::SerializeGroundQueryResult(Result,3,
		TEXT("ue"),TEXT("connection"),TEXT("play"),TEXT("map")).IsEmpty());
	Result.MeasurementDurationMs=-1;
	TestTrue(TEXT("bad measurement time rejected"), SimCoreProtocol::SerializeGroundQueryResult(Result,3,
		TEXT("ue"),TEXT("connection"),TEXT("play"),TEXT("map")).IsEmpty());
	auto Envelope = SimCoreProtocol::SerializeHelloEnvelope(1, TEXT("ue"), TEXT("connection"),
		TEXT("map"), TEXT("test"), {});
	Envelope.Append(TArray<uint8>{122, 0}); // GroundQueryResult replaces Hello in Envelope.oneof.
	SimCoreProtocol::FHelloInfo Hello;
	bool bIsHello = true;
	FString Error;
	TestTrue(TEXT("known non-Hello payload parses"),
		SimCoreProtocol::TryParseHelloEnvelope(Envelope, Hello, bIsHello, Error));
	TestFalse(TEXT("ground response is active instead of earlier Hello"), bIsHello);
	for (bool bQueryFirst : {false, true})
	{
		TArray<uint8> QueryField{50, static_cast<uint8>(Bytes.Num())};
		QueryField.Append(Bytes);
		TArray<uint8> World;
		if (bQueryFirst) World.Append(QueryField);
		World.Append(TArray<uint8>{10, 2, 8, 1, 10, 2, 8, 2}); // Two entities.
		if (!bQueryFirst) World.Append(QueryField);
		TArray<uint8> WorldEnvelope{8, 2, 16, 1, 42, 3, 'm', 'a', 'p',
			58, 4, 'p', 'l', 'a', 'y', 98, static_cast<uint8>(World.Num())};
		WorldEnvelope.Append(World);
		SimCoreProtocol::FVehicleState State;
		TArray<SimCoreProtocol::FVehicleState> Entities;
		TestTrue(TEXT("complete world envelope parses"),
			SimCoreProtocol::ParseWorldStateEnvelope(WorldEnvelope, 1, State, Entities, Error));
		TestTrue(TEXT("query survives target state copy and envelope metadata"),
			State.GroundQueryShadow.IsSet() && State.GroundQueryShadow->RequestId == 1
			&& State.Sequence == 1 && State.PlaySessionId == TEXT("play"));
		TestTrue(TEXT("NPC presentation does not receive ego query"),
			Entities.Num() == 2 && !Entities[1].GroundQueryShadow.IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreGroundShadowMeasurementTest,
	"DriveIntegration.GroundShadow.Measurement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreGroundShadowMeasurementTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreProtocol;
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
		.EnableTraceCollision(true).SetTransactional(false).CreateFXSystem(false);
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,
		MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("GroundShadowQa")),
		GetTransientPackage(),true,ERHIFeatureLevel::Num,&Values);
	if (!TestNotNull(TEXT("isolated query world"),World)) return false;
	const auto Box=[&](FVector Center,FVector Extent,FRotator Rotation=FRotator::ZeroRotator) {
		AActor* Actor=World->SpawnActor<AActor>();
		UBoxComponent* Component=NewObject<UBoxComponent>(Actor,NAME_None,RF_Transient);
		Actor->SetRootComponent(Component); Actor->AddInstanceComponent(Component);
		Component->InitBoxExtent(Extent); Component->SetWorldLocationAndRotation(Center,Rotation);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Component->SetCollisionObjectType(ECC_WorldStatic);
		Component->SetCollisionResponseToAllChannels(ECR_Block); Component->RegisterComponent();
		return Component;
	};
	const FVector Offset(100,-200,0);
	auto* Floor=Box(Offset+FVector(0,0,-50),FVector(500,500,50));
	Floor->ComponentTags.Add(TEXT("SimCore.Surface.LowFriction"));
	FGroundQueryBatch Batch;
	Batch.RequestId=9; Batch.EnvironmentEpoch=2; Batch.EntityId=1; Batch.MaxDistanceMeters=8;
	Batch.OriginsEnu={{0,0,2},{20,0,2}};
	auto Result=SimCoreGroundQueryShadow::Measure(World,nullptr,Offset,Batch);
	TestEqual(TEXT("complete response"),Result.Probes.Num(),2);
	TestTrue(TEXT("flat hit"),Result.Probes[0].Status==EGroundProbeStatus::Hit);
	TestTrue(TEXT("nonzero map origin converted"),Result.Probes[0].PointEnu.Equals(FVector3d(0,0,0),0.001));
	TestEqual(TEXT("same authored surface classification as Bake"),Result.Probes[0].SurfaceMaterialId,2u);
	TestTrue(TEXT("no ground distinct from failed"),Result.Probes[1].Status==EGroundProbeStatus::NoHit);
	Batch.MaxDistanceMeters = 0;
	Batch.QueryDistancesMeters = {1,24};
	Result=SimCoreGroundQueryShadow::Measure(World,nullptr,Offset,Batch);
	TestTrue(TEXT("individual short ray stops above floor"),Result.Probes[0].Status==EGroundProbeStatus::NoHit);
	Batch.QueryDistancesMeters = {24,24};
	Result=SimCoreGroundQueryShadow::Measure(World,nullptr,Offset,Batch);
	TestTrue(TEXT("solver depth reaches floor"),Result.Probes[0].Status==EGroundProbeStatus::Hit);
	Floor->SetWorldLocation(Offset+FVector(0,0,-30));
	Result=SimCoreGroundQueryShadow::Measure(World,nullptr,Offset,Batch);
	TestTrue(TEXT("height change without Bake"),FMath::Abs(Result.Probes[0].PointEnu.Z-0.2)<0.001);
	Result=SimCoreGroundQueryShadow::Measure(World,Floor->GetOwner(),Offset,Batch);
	TestTrue(TEXT("self excluded"),Result.Probes[0].Status==EGroundProbeStatus::NoHit);
	Floor->SetWorldRotation(FRotator(20,0,0));
	Result=SimCoreGroundQueryShadow::Measure(World,nullptr,Offset,Batch);
	TestTrue(TEXT("slope normal measured"),Result.Probes[0].Status==EGroundProbeStatus::Hit
		&& Result.Probes[0].NormalEnu.Z>0.9 && Result.Probes[0].NormalEnu.Z<0.99);
	World->DestroyWorld(false);
	return true;
}
#endif
