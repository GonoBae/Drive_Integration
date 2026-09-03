#include "SimCoreCoordinateFrames.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <cmath>

namespace
{
constexpr double VectorTolerance = 1.0e-8;
constexpr double AngleToleranceDegrees = 1.0e-7;
constexpr double QuaternionToleranceRadians = 2.0e-10;

bool TestVectorNear(
	FAutomationTestBase& Test,
	const TCHAR* What,
	const FVector3d& Actual,
	const FVector3d& Expected,
	double Tolerance = VectorTolerance)
{
	return Test.TestTrue(
		What,
		FMath::Abs(Actual.X - Expected.X) <= Tolerance
			&& FMath::Abs(Actual.Y - Expected.Y) <= Tolerance
			&& FMath::Abs(Actual.Z - Expected.Z) <= Tolerance);
}

double QuaternionAngularDistance(const FQuat4d& LeftInput, const FQuat4d& RightInput)
{
	const auto Normalize = [](const FQuat4d& Input)
	{
		const double Length = std::hypot(
			std::hypot(Input.X, Input.Y),
			std::hypot(Input.Z, Input.W));
		return FQuat4d(
			Input.X / Length,
			Input.Y / Length,
			Input.Z / Length,
			Input.W / Length);
	};
	const FQuat4d Left = Normalize(LeftInput);
	const FQuat4d Right = Normalize(RightInput);
	const double Dot = FMath::Clamp(FMath::Abs(
		Left.X * Right.X + Left.Y * Right.Y + Left.Z * Right.Z
			+ Left.W * Right.W), 0.0, 1.0);
	return 2.0 * FMath::Acos(Dot);
}

double WrappedDegreeError(double Actual, double Expected)
{
	return FMath::Abs(FMath::FindDeltaAngleDegrees(Expected, Actual));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCoreGeoTransformContractTest,
	"DriveIntegration.Coordinates.GeoTransformContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreGeoTransformContractTest::RunTest(const FString& Parameters)
{
	using namespace SimCoreCoordinateFrames;
	bool bSuccess = true;

	const FVector OriginCentimeters(1000.0, -250.0, 50.0);
	const FVector3d PositionEnuMeters(12.345, -6.789, 1.25);
	const FVector PositionUnrealCentimeters =
		MapEnuPositionMetersToUnrealCentimeters(
			PositionEnuMeters,
			OriginCentimeters);
	bSuccess &= TestVectorNear(
		*this,
		TEXT("map_enu position golden mapping"),
		PositionUnrealCentimeters,
		FVector3d(321.1, 984.5, 175.0));
	bSuccess &= TestVectorNear(
		*this,
		TEXT("map_enu position round-trip"),
		UnrealPositionCentimetersToMapEnuMeters(
			PositionUnrealCentimeters,
			OriginCentimeters),
		PositionEnuMeters);

	const FVector3d VelocityEnuMetersPerSecond(2.0, -3.0, 0.5);
	const FVector3d VelocityUnrealCentimetersPerSecond =
		MapEnuVelocityMetersPerSecondToUnrealCentimetersPerSecond(
			VelocityEnuMetersPerSecond);
	bSuccess &= TestVectorNear(
		*this,
		TEXT("map_enu velocity golden mapping"),
		VelocityUnrealCentimetersPerSecond,
		FVector3d(-300.0, 200.0, 50.0));
	bSuccess &= TestVectorNear(
		*this,
		TEXT("map_enu velocity round-trip"),
		UnrealVelocityCentimetersPerSecondToMapEnuMetersPerSecond(
			VelocityUnrealCentimetersPerSecond),
		VelocityEnuMetersPerSecond);

	const FVector3d AccelerationEnuMetersPerSecondSquared(-1.25, 4.5, -9.81);
	const FVector3d AccelerationUnreal =
		MapEnuAccelerationMetersPerSecondSquaredToUnrealCentimetersPerSecondSquared(
			AccelerationEnuMetersPerSecondSquared);
	bSuccess &= TestVectorNear(
		*this,
		TEXT("map_enu acceleration golden mapping"),
		AccelerationUnreal,
		FVector3d(450.0, -125.0, -981.0));
	bSuccess &= TestVectorNear(
		*this,
		TEXT("map_enu acceleration round-trip"),
		UnrealAccelerationCentimetersPerSecondSquaredToMapEnuMetersPerSecondSquared(
			AccelerationUnreal),
		AccelerationEnuMetersPerSecondSquared);

	const FVector3d AngularVelocityEnu(1.0, 2.0, 3.0);
	const FVector3d AngularVelocityUnreal =
		MapEnuAxialAngularVelocityToUnrealWorld(AngularVelocityEnu);
	bSuccess &= TestVectorNear(
		*this,
		TEXT("world axial angular velocity includes determinant"),
		AngularVelocityUnreal,
		FVector3d(-2.0, -1.0, -3.0),
		0.0);
	bSuccess &= TestVectorNear(
		*this,
		TEXT("world axial angular velocity round-trip"),
		UnrealWorldAxialAngularVelocityToMapEnu(AngularVelocityUnreal),
		AngularVelocityEnu,
		0.0);
	bSuccess &= TestVectorNear(
		*this,
		TEXT("FLU Actor polar basis"),
		BodyFluPolarVectorToUnrealActor(FVector3d(1.0, 2.0, 3.0)),
		FVector3d(1.0, -2.0, 3.0),
		0.0);
	bSuccess &= TestVectorNear(
		*this,
		TEXT("FLU Actor axial basis"),
		BodyFluAxialAngularVelocityToUnrealActor(FVector3d(1.0, 2.0, 3.0)),
		FVector3d(-1.0, 2.0, -3.0),
		0.0);

	bSuccess &= TestTrue(
		TEXT("North heading is +90 degree ENU yaw"),
		FMath::Abs(NavigationHeadingDegreesToEnuYawRadians(0.0)
			- 0.5 * UE_DOUBLE_PI) <= 1.0e-14);
	bSuccess &= TestTrue(
		TEXT("East heading is zero ENU yaw"),
		FMath::Abs(NavigationHeadingDegreesToEnuYawRadians(90.0)) <= 1.0e-14);
	bSuccess &= TestTrue(
		TEXT("heading/yaw round-trip"),
		WrappedDegreeError(
			EnuYawRadiansToNavigationHeadingDegrees(
				NavigationHeadingDegreesToEnuYawRadians(327.125)),
			327.125) <= AngleToleranceDegrees);

	const double RootHalf = FMath::Sqrt(0.5);
	const FQuat4d NorthMap = CanonicalAttitudeToMapEnuQuaternion({0.0, 0.0, 0.0});
	bSuccess &= TestTrue(
		TEXT("level North map quaternion golden value"),
		FMath::Abs(NorthMap.X) <= 1.0e-14
			&& FMath::Abs(NorthMap.Y) <= 1.0e-14
			&& FMath::Abs(NorthMap.Z - RootHalf) <= 1.0e-14
			&& FMath::Abs(NorthMap.W - RootHalf) <= 1.0e-14);
	bSuccess &= TestTrue(
		TEXT("level North Unreal quaternion is identity"),
		QuaternionAngularDistance(
			MapEnuQuaternionToUnrealActorQuaternion(NorthMap),
			FQuat4d(0.0, 0.0, 0.0, 1.0)) <= QuaternionToleranceRadians);

	const FQuat4d EastMap = CanonicalAttitudeToMapEnuQuaternion({90.0, 0.0, 0.0});
	bSuccess &= TestTrue(
		TEXT("level East map quaternion is identity"),
		QuaternionAngularDistance(
			EastMap,
			FQuat4d(0.0, 0.0, 0.0, 1.0)) <= QuaternionToleranceRadians);
	bSuccess &= TestTrue(
		TEXT("level East Unreal quaternion is +90 yaw"),
		QuaternionAngularDistance(
			MapEnuQuaternionToUnrealActorQuaternion(EastMap),
			FQuat4d(0.0, 0.0, RootHalf, RootHalf))
			<= QuaternionToleranceRadians);

	const FQuat4d PitchedMap = CanonicalAttitudeToMapEnuQuaternion({0.0, 30.0, 0.0});
	bSuccess &= TestVectorNear(
		*this,
		TEXT("positive pitch raises a North-facing canonical nose"),
		PitchedMap.RotateVector(FVector3d::ForwardVector),
		FVector3d(0.0, FMath::Sqrt(0.75), 0.5));
	bSuccess &= TestVectorNear(
		*this,
		TEXT("positive pitch raises an Unreal Actor nose"),
		MapEnuQuaternionToUnrealActorQuaternion(PitchedMap)
			.RotateVector(FVector3d::ForwardVector),
		FVector3d(FMath::Sqrt(0.75), 0.0, 0.5));

	const FCanonicalAttitudeDegrees Attitudes[] = {
		{0.0, 0.0, 0.0},
		{90.0, 0.0, 0.0},
		{180.0, 0.0, 0.0},
		{270.0, 0.0, 0.0},
		{21.2, 12.4, -8.1},
		{281.3, -41.2, 47.5},
		{359.4, 57.0, -68.0},
	};
	for (const FCanonicalAttitudeDegrees& Expected : Attitudes)
	{
		const FQuat4d Map = CanonicalAttitudeToMapEnuQuaternion(Expected);
		const FCanonicalAttitudeDegrees Decoded =
			MapEnuQuaternionToCanonicalAttitude(Map);
		bSuccess &= TestTrue(
			TEXT("canonical attitude quaternion round-trip"),
			WrappedDegreeError(Decoded.HeadingClockwise, Expected.HeadingClockwise)
				<= AngleToleranceDegrees
				&& FMath::Abs(Decoded.PitchNoseUp - Expected.PitchNoseUp)
					<= AngleToleranceDegrees
				&& WrappedDegreeError(Decoded.RollLeftUp, Expected.RollLeftUp)
					<= AngleToleranceDegrees);

		const FQuat4d Unreal = MapEnuQuaternionToUnrealActorQuaternion(Map);
		const FQuat4d Restored = UnrealActorQuaternionToMapEnuQuaternion(Unreal);
		bSuccess &= TestTrue(
			TEXT("map_enu/FLU Unreal/Actor quaternion basis round-trip"),
			QuaternionAngularDistance(Map, Restored)
				<= QuaternionToleranceRadians);
	}

	const float SnapshotHeading = 123.25f;
	const float SnapshotPitch = 17.5f;
	const float SnapshotRoll = -22.0f;
	const FQuat4d SnapshotRotation = BuildUnrealActorRotation(
		SnapshotHeading,
		SnapshotPitch,
		SnapshotRoll,
		0.0f,
		FVector3d::ZeroVector,
		0.0).Quaternion();
	bSuccess &= TestTrue(
		TEXT("presentation rotation uses the explicit quaternion contract"),
		QuaternionAngularDistance(
			SnapshotRotation,
			CanonicalAttitudeToUnrealActorQuaternion({
				SnapshotHeading,
				SnapshotPitch,
				SnapshotRoll})) <= QuaternionToleranceRadians);

	constexpr double PredictionSeconds = 0.1;
	constexpr float BodyYawRateRadiansPerSecond = 0.2f;
	const FQuat4d PredictedRotation = BuildUnrealActorRotation(
		0.0f,
		0.0f,
		0.0f,
		BodyYawRateRadiansPerSecond,
		FVector3d(0.0, 0.0, BodyYawRateRadiansPerSecond),
		PredictionSeconds).Quaternion();
	bSuccess &= TestTrue(
		TEXT("left-positive body yaw predicts decreasing clockwise heading"),
		QuaternionAngularDistance(
			PredictedRotation,
			CanonicalAttitudeToUnrealActorQuaternion({
				-FMath::RadiansToDegrees(
					BodyYawRateRadiansPerSecond * PredictionSeconds),
				0.0,
				0.0})) <= QuaternionToleranceRadians);

	return bSuccess;
}

#endif // WITH_DEV_AUTOMATION_TESTS
