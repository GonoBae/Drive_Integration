#include "SimCoreCoordinateFrames.h"
#include "SimCorePresentation.h"
#include "SimCoreProtocol.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "ExternalVehiclePawn.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "SimCoreClientComponent.h"

namespace SimCoreSteeringContractTests
{
constexpr float TireRadiusMeters = 0.32f;
constexpr float FullLockRackRadians = 0.61086524f;
const FString MapChecksum = TEXT("fnv1a64:0123456789abcdef");

// Independent, small protobuf fixtures: do not reuse production serialization
// to compute the expected steering wire value or the incoming wheel angles.
void AppendVarint(TArray<uint8>& Out, uint64 Value)
{
	while (Value >= 128) { Out.Add(static_cast<uint8>(Value) | 128); Value >>= 7; }
	Out.Add(static_cast<uint8>(Value));
}

void AppendInteger(TArray<uint8>& Out, uint32 Field, uint64 Value)
{
	AppendVarint(Out, static_cast<uint64>(Field) << 3);
	AppendVarint(Out, Value);
}

void AppendMessage(TArray<uint8>& Out, uint32 Field, const TArray<uint8>& Value)
{
	AppendVarint(Out, (static_cast<uint64>(Field) << 3) | 2);
	AppendVarint(Out, Value.Num());
	Out.Append(Value);
}

void AppendText(TArray<uint8>& Out, uint32 Field, const FString& Value)
{
	FTCHARToUTF8 Encoded(*Value);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
	AppendMessage(Out, Field, Bytes);
}

template <typename T> void AppendNumber(TArray<uint8>& Out, uint32 Field, T Value)
{
	static_assert(sizeof(T) == 4 || sizeof(T) == 8);
	AppendVarint(Out, (static_cast<uint64>(Field) << 3) | (sizeof(T) == 4 ? 5 : 1));
	uint64 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(T));
	for (uint32 Index = 0; Index < sizeof(T); ++Index)
	{
		Out.Add(static_cast<uint8>(Bits >> (8 * Index)));
	}
}

void AppendVector(TArray<uint8>& Out, uint32 Field, const FVector3d& Value)
{
	TArray<uint8> Vector;
	AppendNumber(Vector, 1, Value.X);
	AppendNumber(Vector, 2, Value.Y);
	AppendNumber(Vector, 3, Value.Z);
	AppendMessage(Out, Field, Vector);
}

TArray<uint8> ExpectedControlEnvelope(float ExpectedSteering)
{
	TArray<uint8> Control, Envelope;
	AppendInteger(Control, 1, 0); // Manual mode.
	AppendNumber(Control, 2, 0.5f);
	AppendNumber(Control, 3, 0.125f);
	AppendNumber(Control, 4, ExpectedSteering);
	AppendInteger(Control, 5, 0);
	AppendInteger(Control, 6, 1); // Drive.
	AppendInteger(Control, 7, 0);
	AppendInteger(Control, 8, 123456);
	AppendInteger(Envelope, 1, 2);
	AppendInteger(Envelope, 2, 55);
	AppendText(Envelope, 4, TEXT("steering-contract"));
	AppendText(Envelope, 5, MapChecksum);
	AppendText(Envelope, 6, TEXT("steering-test-session"));
	AppendMessage(Envelope, 11, Control);
	return Envelope;
}

float ExpectedWheelAngle(uint32 WheelIndex, float TurnSign, bool bFullLock = false)
{
	if (WheelIndex >= 2) return 0.0f;
	// Distinct inner/outer values must never be replaced by the central 0.33 rad
	// rack value. A right turn swaps which front wheel is the inner wheel.
	const bool bInside = TurnSign > 0.0f ? WheelIndex == 0 : WheelIndex == 1;
	if (bFullLock)
	{
		// Golden sedan geometry: 2.70 m wheelbase, 1.58 m front track, 35-degree
		// central rack lock. This is a wire/display fixture, not a physics solve.
		const double CentreRadius = 2.70 / FMath::Tan(static_cast<double>(FullLockRackRadians));
		return TurnSign * static_cast<float>(FMath::Atan(2.70 / (CentreRadius + (bInside ? -0.79 : 0.79))));
	}
	return TurnSign * (bInside ? 0.42f : 0.28f);
}

TArray<uint8> WheelWorldEnvelope(float SpeedMps, float TurnSign, const FVector3d& NormalEnu,
	bool bFullLock = false, uint8 ContactMask = 0x0f)
{
	TArray<uint8> Entity, World, Envelope;
	AppendInteger(Entity, 1, 1);
	AppendNumber(Entity, 2, 123.5);
	AppendNumber(Entity, 9, SpeedMps);
	AppendNumber(Entity, 16, TurnSign * (bFullLock ? FullLockRackRadians : 0.33f));
	AppendVector(Entity, 18, FVector3d(10.0, 20.0, 0.65));
	AppendVector(Entity, 19, FVector3d(SpeedMps, 0.0, 0.0));
	AppendInteger(Entity, 22, 1); // Ego vehicle.
	// Deliberately not axle/index order: display must use wheel_index, not the
	// repeated-field position. Every contact point lies on the fixture's plane.
	for (uint32 WheelIndex : {2u, 0u, 3u, 1u})
	{
		const double EastOffset = WheelIndex % 2 == 0 ? -0.8 : 0.8;
		const double NorthOffset = WheelIndex < 2 ? 1.4 : -1.4;
		const double ContactUp = -(NormalEnu.X * EastOffset + NormalEnu.Y * NorthOffset) / NormalEnu.Z;
		TArray<uint8> Wheel;
		AppendInteger(Wheel, 1, WheelIndex);
		AppendInteger(Wheel, 2, (ContactMask & (1u << WheelIndex)) != 0 ? 1 : 0);
		AppendNumber(Wheel, 3, ExpectedWheelAngle(WheelIndex, TurnSign, bFullLock));
		AppendNumber(Wheel, 4, SpeedMps / TireRadiusMeters);
		AppendNumber(Wheel, 5, 3400.0f);
		AppendVector(Wheel, 10, FVector3d(10.0 + EastOffset, 20.0 + NorthOffset, ContactUp));
		AppendVector(Wheel, 11, NormalEnu);
		AppendMessage(Entity, 21, Wheel);
	}
	AppendMessage(World, 1, Entity);
	AppendInteger(Envelope, 1, 2);
	AppendInteger(Envelope, 2, 80);
	AppendInteger(Envelope, 3, 16'666'667);
	AppendText(Envelope, 5, MapChecksum);
	AppendText(Envelope, 7, TEXT("steering-test-play"));
	AppendMessage(Envelope, 12, World);
	return Envelope;
}

struct FIsolatedSteeringWorld
{
	UWorld* World = nullptr;
	FIsolatedSteeringWorld()
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues()
			.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
			.SetTransactional(false).CreateFXSystem(false);
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("SimCoreSteeringQa")),
			GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	}
	~FIsolatedSteeringWorld() { if (World) World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreSteeringInputWireContractTest,
	"DriveIntegration.Steering.InputWireContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSteeringInputWireContractTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreSteeringContractTests;
	struct FInputCase { float RightPositiveAxis; float Canonical; float Wire; };
	const FInputCase Cases[] = {
		{-2.0f, 2.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, {-0.25f, 0.25f, 0.25f},
		{0.0f, -0.0f, -0.0f}, {0.25f, -0.25f, -0.25f},
		{1.0f, -1.0f, -1.0f}, {2.0f, -2.0f, -1.0f},
	};
	bool bSuccess = true;
	for (const FInputCase& Case : Cases)
	{
		const float Canonical = SimCoreCoordinateFrames::InputAxisToCanonicalSteering(Case.RightPositiveAxis);
		bSuccess &= TestEqual(TEXT("device steering crosses the sign boundary exactly once"), Canonical, Case.Canonical);
		SimCoreProtocol::FControlCommand Command;
		Command.Throttle = 0.5f;
		Command.Brake = 0.125f;
		Command.Steering = Canonical;
		Command.ClientTimeNs = 123456;
		const TArray<uint8> Actual = SimCoreProtocol::SerializeControlEnvelope(
			Command, 55, TEXT("steering-contract"), TEXT("steering-test-session"), MapChecksum);
		bSuccess &= TestTrue(TEXT("control field 4 preserves steering magnitude/sign with only the unit clamp"),
			Actual == ExpectedControlEnvelope(Case.Wire));
	}
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreSteeringWheelPresentationContractTest,
	"DriveIntegration.Steering.WheelPresentationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSteeringWheelPresentationContractTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreSteeringContractTests;
	bool bSuccess = true;
	// No world, BeginPlay, sockets, input leases or physics mutation: this tests
	// the public parser/presentation path, not the server's rack dynamics.
	for (bool bSloped : {false, true})
	{
		const FVector3d NormalEnu = bSloped
			? FVector3d(0.2, -0.3, 1.0).GetSafeNormal() : FVector3d::UpVector;
		// Independent golden ENU -> UE mapping for a North-facing level actor.
		const FVector ExpectedNormal(NormalEnu.Y, NormalEnu.X, NormalEnu.Z);
		for (float TurnSign : {-1.0f, 1.0f})
		{
			TStaticArray<FQuat, SimCorePresentation::VehicleWheelCount> StationaryPivots;
			TStaticArray<FVector, SimCorePresentation::VehicleWheelCount> StationaryCenters;
			for (float SpeedMps : {0.0f, 3.0f, 10.0f, 25.0f, 50.0f})
			{
				SimCoreProtocol::FVehicleState State;
				FString Error;
				if (!TestTrue(TEXT("per-wheel steering WorldState fixture parses"),
					SimCoreProtocol::ParseWorldStateEnvelope(
						WheelWorldEnvelope(SpeedMps, TurnSign, NormalEnu), 1, State, Error)))
				{
					AddError(Error);
					return false;
				}
				bSuccess &= TestEqual(TEXT("wire speed reaches presentation unchanged"), State.SpeedMps, SpeedMps);
				bSuccess &= TestEqual(TEXT("body velocity reaches presentation unchanged"), State.LinearVelocityBody.X, static_cast<double>(SpeedMps));
				bSuccess &= TestEqual(TEXT("central rack angle remains distinct from wheel angles"), State.SteeringAngleRad, TurnSign * 0.33f);
				if (!TestEqual(TEXT("fixture retains all four indexed wheels"), State.Wheels.Num(), 4)) return false;
				const auto Sample = SimCorePresentation::BuildVehicleSample(
					State, 0.025f, 0.05f, 0.1f, 0.05f, TireRadiusMeters, FVector(100.0, -50.0, 7.0));
				bSuccess &= TestFalse(TEXT("fixture snapshot is fresh"), Sample.bStateStale);
				TSet<uint32> SeenWheelIndices;
				for (const auto& Wheel : State.Wheels)
				{
					if (!TestTrue(TEXT("all four wire wheel indices remain unique and in range"),
						Wheel.WheelIndex < SimCorePresentation::VehicleWheelCount
						&& !SeenWheelIndices.Contains(Wheel.WheelIndex))) return false;
					SeenWheelIndices.Add(Wheel.WheelIndex);
					const float ExpectedAngle = ExpectedWheelAngle(Wheel.WheelIndex, TurnSign);
					bSuccess &= TestEqual(TEXT("parser preserves each authoritative wheel angle"), Wheel.SteeringAngleRad, ExpectedAngle);
					const auto& Ground = Sample.Wheels[Wheel.WheelIndex];
					bSuccess &= TestTrue(TEXT("indexed wheel retains valid contact"), Ground.bHasGroundContact);
					bSuccess &= TestTrue(TEXT("contact normal has the independent golden basis"),
						Ground.RelativeContactNormal.Equals(ExpectedNormal, 1.0e-6));
					const FRotator Rotation = SimCorePresentation::BuildWheelPivotRelativeRotation(Wheel, Ground.RelativeContactNormal);
					const FQuat Pivot = Rotation.Quaternion();
					const FVector Forward = Pivot.RotateVector(FVector::ForwardVector);
					const FVector UnsteeredForward = (FVector::ForwardVector
						- FVector::DotProduct(FVector::ForwardVector, ExpectedNormal) * ExpectedNormal).GetSafeNormal();
					const double SignedAngle = FMath::Atan2(
						FVector::DotProduct(ExpectedNormal, FVector::CrossProduct(UnsteeredForward, Forward)),
						FVector::DotProduct(UnsteeredForward, Forward));
					bSuccess &= TestTrue(TEXT("wheel applies the full negative canonical angle in its contact plane"),
						FMath::Abs(SignedAngle + ExpectedAngle) < 1.0e-5);
					bSuccess &= TestTrue(TEXT("wheel remains aligned to its contact normal"),
						Pivot.RotateVector(FVector::UpVector).Equals(ExpectedNormal, 1.0e-6));
					if (!bSloped)
					{
						bSuccess &= TestTrue(TEXT("flat-ground yaw is exactly the signed road-wheel angle"),
							FMath::Abs(FMath::FindDeltaAngleDegrees(
								-static_cast<double>(FMath::RadiansToDegrees(ExpectedAngle)), Rotation.Yaw)) < 0.001);
					}
					if (SpeedMps == 0.0f)
					{
						StationaryPivots[Wheel.WheelIndex] = Pivot;
						StationaryCenters[Wheel.WheelIndex] = Ground.RelativeCenterLocationCm;
					}
					else
					{
						bSuccess &= TestTrue(TEXT("road speed cannot attenuate the displayed steering angle"),
							Pivot.Equals(StationaryPivots[Wheel.WheelIndex], 1.0e-6));
						bSuccess &= TestTrue(TEXT("extrapolation carries wheel contact with the chassis, without local drift"),
							Ground.RelativeCenterLocationCm.Equals(StationaryCenters[Wheel.WheelIndex], 1.0e-6));
					}
				}
			}
		}
	}
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreSteeringPawnPresentationTest,
	"DriveIntegration.Steering.PawnTickFullLockAndContactLoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreSteeringPawnPresentationTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreSteeringContractTests;
	bool bSuccess = true;
	FIsolatedSteeringWorld Scene;
	if (!TestNotNull(TEXT("isolated steering world"), Scene.World)) return false;
	AExternalVehiclePawn* Pawn = Scene.World->SpawnActor<AExternalVehiclePawn>();
	if (!TestNotNull(TEXT("real vehicle Pawn fixture"), Pawn)) return false;
	USimCoreClientComponent* Client = Pawn->FindComponentByClass<USimCoreClientComponent>();
	if (!TestNotNull(TEXT("real Pawn client component"), Client)) return false;
	USceneComponent* Pivots[SimCorePresentation::VehicleWheelCount] = {};
	UStaticMeshComponent* Meshes[SimCorePresentation::VehicleWheelCount] = {};
	TArray<USceneComponent*> Components;
	Pawn->GetComponents(Components);
	for (int32 Index = 0; Index < SimCorePresentation::VehicleWheelCount; ++Index)
	{
		const FName PivotName(*FString::Printf(TEXT("WheelPivot%d"), Index));
		const FName MeshName(*FString::Printf(TEXT("Wheel%d"), Index));
		for (USceneComponent* Component : Components)
		{
			if (Component->GetFName() == PivotName) Pivots[Index] = Component;
			if (Component->GetFName() == MeshName) Meshes[Index] = Cast<UStaticMeshComponent>(Component);
		}
		if (!TestNotNull(TEXT("real indexed steering pivot"), Pivots[Index])
			|| !TestNotNull(TEXT("real indexed rolling mesh"), Meshes[Index])) return false;
	}
	// Narrow test friendship supplies an already-accepted wire snapshot only.
	// No BeginPlay, component tick, socket, real input or server control lease is
	// used. These results demonstrate Pawn presentation, not vehicle dynamics.
	const auto ApplyFixture = [&](float SpeedMps, float TurnSign, const FVector3d& NormalEnu,
		bool bFullLock, uint8 ContactMask)
	{
		SimCoreProtocol::FVehicleState Snapshot;
		FString Error;
		if (!TestTrue(TEXT("Pawn fixture parses before installation"),
			SimCoreProtocol::ParseWorldStateEnvelope(
				WheelWorldEnvelope(SpeedMps, TurnSign, NormalEnu, bFullLock, ContactMask), 1, Snapshot, Error)))
		{
			AddError(Error);
			return false;
		}
		Client->LatestState = MoveTemp(Snapshot);
		Client->LatestStateReceiveTimeSeconds = FPlatformTime::Seconds();
		Client->bHasState = true;
		Pawn->Tick(1.0f / 60.0f);
		return true;
	};
	for (bool bSloped : {false, true})
	{
		const FVector3d NormalEnu = bSloped
			? FVector3d(0.2, -0.3, 1.0).GetSafeNormal() : FVector3d::UpVector;
		const FVector NormalActor(NormalEnu.Y, NormalEnu.X, NormalEnu.Z);
		for (float TurnSign : {-1.0f, 1.0f})
		{
			FQuat StationaryPivots[] = {FQuat::Identity, FQuat::Identity};
			for (float SpeedMps : {0.0f, 25.0f, 50.0f})
			{
				// Multiple rolling phases cover the composed mesh transform, not
				// merely the steering helper's return value or a scalar Euler yaw.
				for (int32 Frame = 0; Frame < 4; ++Frame)
				{
					if (!ApplyFixture(SpeedMps, TurnSign, NormalEnu, true, 0x0f)) return false;
					for (int32 Index = 0; Index < 2; ++Index)
					{
						SimCoreProtocol::FVehicleState::FWheelState ExpectedWheel;
						ExpectedWheel.SteeringAngleRad = ExpectedWheelAngle(Index, TurnSign, true);
						const FQuat ExpectedPivot = SimCorePresentation::BuildWheelPivotRelativeRotation(
							ExpectedWheel, NormalActor).Quaternion();
						const FQuat ActualPivot = Pivots[Index]->GetRelativeRotation().Quaternion();
						bSuccess &= TestTrue(TEXT("Pawn Tick applies full Ackermann steering at every speed"),
							ActualPivot.Equals(ExpectedPivot, 1.0e-6));
						const FVector UnsteeredForward = (FVector::ForwardVector
							- FVector::DotProduct(FVector::ForwardVector, NormalActor) * NormalActor).GetSafeNormal();
						const FVector ActualForward = ActualPivot.RotateVector(FVector::ForwardVector);
						const double ActualAngle = FMath::Atan2(
							FVector::DotProduct(NormalActor, FVector::CrossProduct(UnsteeredForward, ActualForward)),
							FVector::DotProduct(UnsteeredForward, ActualForward));
						bSuccess &= TestTrue(TEXT("actual full-lock angle matches Ackermann golden geometry, not a shared helper result"),
							FMath::Abs(ActualAngle + ExpectedWheel.SteeringAngleRad) < 1.0e-5);
						if (SpeedMps == 0.0f) StationaryPivots[Index] = ActualPivot;
						else bSuccess &= TestTrue(TEXT("actual Pawn pivot is speed independent through 50 m/s"),
							ActualPivot.Equals(StationaryPivots[Index], 1.0e-6));
						const FVector ExpectedAxleWorld = Pawn->GetActorQuat().RotateVector(
							ExpectedPivot.RotateVector(FVector::RightVector));
						bSuccess &= TestTrue(TEXT("rolling mesh spin never changes the steered axle direction"),
							Meshes[Index]->GetComponentQuat().RotateVector(FVector::RightVector)
								.Equals(ExpectedAxleWorld, 1.0e-6));
					}
				}
				AddInfo(FString::Printf(TEXT("PawnTick contact=all speed=%.1fm/s rack=%.3fdeg front=[%.3f,%.3f]deg surface=%s"),
					SpeedMps, FMath::RadiansToDegrees(TurnSign * FullLockRackRadians),
					FMath::RadiansToDegrees(ExpectedWheelAngle(0, TurnSign, true)),
					FMath::RadiansToDegrees(ExpectedWheelAngle(1, TurnSign, true)),
					bSloped ? TEXT("sloped") : TEXT("flat")));
			}
			// Seed a visibly different angle, then lose front contact while the
			// server changes steering. Terrain contact may gate wheel position,
			// but must not freeze a steering rack that remains authoritative.
			if (!ApplyFixture(50.0f, -TurnSign, NormalEnu, false, 0x0f)) return false;
			const FVector PreviousCenters[] = {Pivots[0]->GetRelativeLocation(), Pivots[1]->GetRelativeLocation()};
			const FQuat PreviousPivots[] = {Pivots[0]->GetRelativeRotation().Quaternion(), Pivots[1]->GetRelativeRotation().Quaternion()};
			if (!ApplyFixture(50.0f, TurnSign, NormalEnu, true, 0x0c)) return false;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				SimCoreProtocol::FVehicleState::FWheelState ExpectedWheel;
				ExpectedWheel.SteeringAngleRad = ExpectedWheelAngle(Index, TurnSign, true);
				const FQuat ExpectedAirbornePivot = SimCorePresentation::BuildWheelPivotRelativeRotation(
					ExpectedWheel, FVector::UpVector).Quaternion();
				const FQuat ActualPivot = Pivots[Index]->GetRelativeRotation().Quaternion();
				bSuccess &= TestTrue(TEXT("contact loss preserves last supported wheel center"),
					Pivots[Index]->GetRelativeLocation().Equals(PreviousCenters[Index], 1.0e-6));
				bSuccess &= TestFalse(TEXT("contact loss must not retain the previous steering pivot"),
					ActualPivot.Equals(PreviousPivots[Index], 1.0e-6));
				bSuccess &= TestTrue(TEXT("airborne front wheel still displays the newest authoritative steering angle"),
					ActualPivot.Equals(ExpectedAirbornePivot, 1.0e-6));
				bSuccess &= TestTrue(TEXT("airborne angle matches the independent full-lock golden yaw"),
					FMath::Abs(FMath::FindDeltaAngleDegrees(
						-static_cast<double>(FMath::RadiansToDegrees(ExpectedWheel.SteeringAngleRad)), ActualPivot.Rotator().Yaw)) < 0.001);
				AddInfo(FString::Printf(TEXT("PawnTick contact=front-lost speed=50m/s wheel=%d expectedYaw=%.3f actualYaw=%.3f surface=%s"),
					Index, ExpectedAirbornePivot.Rotator().Yaw, ActualPivot.Rotator().Yaw,
					bSloped ? TEXT("sloped") : TEXT("flat")));
			}
		}
	}
	bSuccess &= TestFalse(TEXT("isolated Pawn never began play"), Pawn->HasActorBegunPlay());
	bSuccess &= TestFalse(TEXT("isolated presentation test never opens a socket"), Client->Socket.IsValid());
	return bSuccess;
}

#endif // WITH_DEV_AUTOMATION_TESTS
