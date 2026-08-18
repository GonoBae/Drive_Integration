#pragma once

#include "CoreMinimal.h"

namespace SimCoreProtocol
{
	inline constexpr uint32 SchemaVersion = 1;

	enum class EVehicleGear : uint8
	{
		Neutral = 0,
		Drive = 1,
		Reverse = 2,
	};

	struct FControlCommand
	{
		float Throttle = 0.0f;
		float Brake = 0.0f;
		float Steering = 0.0f;
		bool bHandbrake = false;
		EVehicleGear Gear = EVehicleGear::Drive;
		bool bEstop = false;
		uint64 ClientTimeNs = 0;
	};

	struct FVehicleState
	{
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
		double EastMeters = 0.0;
		double NorthMeters = 0.0;
		float YawRateRad = 0.0f;
		float SteeringAngleRad = 0.0f;
		EVehicleGear Gear = EVehicleGear::Drive;
		uint64 Sequence = 0;
		uint64 SimulationTimeNs = 0;
	};

	DRIVEINTEGRATION_API TArray<uint8> SerializeControlEnvelope(
		const FControlCommand& Command,
		uint64 Sequence,
		const FString& SourceId,
		const FString& MapChecksum);

	DRIVEINTEGRATION_API bool ParseWorldStateEnvelope(
		TArrayView<const uint8> Data,
		FVehicleState& OutState,
		FString& OutError);
}
