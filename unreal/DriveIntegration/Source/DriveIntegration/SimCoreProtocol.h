#pragma once

#include "CoreMinimal.h"

namespace SimCoreProtocol
{
	inline constexpr uint32 SchemaVersion = 2;

	enum class EVehicleGear : uint8
	{
		Neutral = 0,
		Drive = 1,
		Reverse = 2,
	};

	enum class EEntityKind : uint8
	{
		Unspecified = 0,
		EgoVehicle = 1,
		NpcVehicle = 2,
		Pedestrian = 3,
	};

	struct FControlCommand
	{
		float Throttle = 0.0f;
		float Brake = 0.0f;
		float Steering = 0.0f; // -1=right, +1=left
		bool bHandbrake = false;
		EVehicleGear Gear = EVehicleGear::Drive;
		bool bEstop = false;
		uint64 ClientTimeNs = 0;
	};

	struct FVehicleState
	{
		struct FWheelState
		{
			uint32 WheelIndex = 0;
			bool bInContact = false;
			float SteeringAngleRad = 0.0f; // positive=left
			float AngularSpeedRad = 0.0f;
			float NormalLoadN = 0.0f;
			float LongitudinalSlip = 0.0f;
			float SlipAngleRad = 0.0f;
			float LongitudinalForceN = 0.0f;
			float LateralForceN = 0.0f;
			FVector3d ContactPointEnu = FVector3d::ZeroVector;
			FVector3d ContactNormalEnu = FVector3d::ZeroVector;
		};

		uint32 EntityId = 0;
		double Timestamp = 0.0;
		double Latitude = 0.0;
		double Longitude = 0.0;
		double Altitude = 0.0;
		float HeadingDegrees = 0.0f;
		float PitchDegrees = 0.0f;
		float RollDegrees = 0.0f;
		float SpeedMps = 0.0f;
		float AccelMps2 = 0.0f;
		float FuelPercent = 0.0f;
		float EngineRpm = 0.0f;
		double EastMeters = 0.0;
		double NorthMeters = 0.0;
		float YawRateRad = 0.0f; // FLU body yaw, positive=left
		float SteeringAngleRad = 0.0f; // positive=left
		EVehicleGear Gear = EVehicleGear::Drive;
		EEntityKind EntityKind = EEntityKind::Unspecified;
		uint64 Sequence = 0;
		uint64 SimulationTimeNs = 0;
		FString MapPackageChecksum;
		FString PlaySessionId;
		FVector3d PositionEnu = FVector3d::ZeroVector;
		FVector3d LinearVelocityBody = FVector3d::ZeroVector;
		FVector3d AngularVelocityBody = FVector3d::ZeroVector;
		FVector3d LinearVelocityEnu = FVector3d::ZeroVector;
		float CollisionHalfLengthMeters = 0.0f;
		float CollisionHalfWidthMeters = 0.0f;
		float CollisionHalfHeightMeters = 0.0f;
		float CollisionRadiusMeters = 0.0f;
		TArray<FWheelState> Wheels;
	};

	DRIVEINTEGRATION_API TArray<uint8> SerializeControlEnvelope(
		const FControlCommand& Command,
		uint64 Sequence,
		const FString& SourceId,
		const FString& SessionId,
		const FString& MapChecksum);

	DRIVEINTEGRATION_API TArray<uint8> SerializeSimulationResetEnvelope(
		const FString& PlaySessionId,
		uint64 ClientTimeNs,
		uint64 Sequence,
		const FString& SourceId,
		const FString& ConnectionSessionId,
		const FString& MapChecksum);

	DRIVEINTEGRATION_API bool ParseWorldStateEnvelope(
		TArrayView<const uint8> Data,
		uint32 TargetEntityId,
		FVehicleState& OutState,
		FString& OutError);

	DRIVEINTEGRATION_API bool ParseWorldStateEnvelope(
		TArrayView<const uint8> Data,
		uint32 TargetEntityId,
		FVehicleState& OutState,
		TArray<FVehicleState>& OutEntities,
		FString& OutError);
}
