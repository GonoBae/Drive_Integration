#pragma once

#include "CoreMinimal.h"

namespace SimCoreProtocol
{
	inline constexpr uint32 SchemaVersion = 2;
	inline constexpr const TCHAR* SchemaName = TEXT("simcore-envelope-v2");

	struct FHelloInfo
	{
		uint64 Sequence = 0;
		FString SourceId;
		FString SessionId;
		FString MapPackageChecksum;
		FString Build;
		FString Schema;
		TArray<FString> Capabilities;
	};

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

	enum class ERuntimeVehicleClass : uint8
	{
		Unspecified = 0,
		Sedan = 1,
		Compact = 2,
		Truck = 3,
		Motorcycle = 4,
	};

	enum class EVehicleDamageZone : uint8
	{
		None = 0,
		Front = 1,
		Rear = 2,
		Left = 3,
		Right = 4,
		Roof = 5,
		Underbody = 6,
	};

	inline constexpr int32 MaxWorldStateTrafficSignals = 32;
	inline constexpr int32 MaxWorldStateStructures = 256;
	// Must cover the bounded format-v2 plan cycle. Version 1 still publishes
	// its original <=30-second countdowns.
	inline constexpr float MaxTrafficSignalCountdownSeconds = 3600.0f;

	enum class ETrafficSignalAspect : uint8
	{
		Unknown = 0,
		Red = 1,
		Yellow = 2,
		Green = 3,
	};

	enum class ETrafficSignalKind : uint8
	{
		Unspecified = 0,
		Vehicle = 1,
		Pedestrian = 2,
	};

	/** Server authority only. Position is the pole base; heading is traffic travel direction. */
	struct FTrafficSignalState
	{
		uint32 SignalId = 0;
		uint32 GroupId = 0;
		// Additive multi-intersection identity. An omitted v1 field means 1.
		uint32 ControllerId = 1;
		ETrafficSignalAspect Aspect = ETrafficSignalAspect::Unknown;
		// Omitted legacy fields are vehicle heads.
		ETrafficSignalKind Kind = ETrafficSignalKind::Vehicle;
		FVector3d PositionEnu = FVector3d::ZeroVector;
		float HeadingDegrees = 0.0f;
		float RemainingSeconds = 0.0f;
		bool bOutOfService = false;
	};

	enum class EStructureKind : uint8
	{
		Building = 1,
		SignalPole = 2,
	};

	/** Damaged map structure. Empty world list means the original authored map. */
	struct FStructureState
	{
		FString ColliderId;
		EStructureKind Kind = EStructureKind::Building;
		uint32 SignalId = 0;
		float DamagePercent = 0.0f;
		uint32 EventSequence = 0;
		FVector3d ImpactPointEnu = FVector3d::ZeroVector;
		FVector3d ImpactNormalEnu = FVector3d::ZeroVector;
		FVector3d BasePositionEnu = FVector3d::ZeroVector;
		double HeadingRadians = 0.0;
		float FallAngleRadians = 0.0f;
		FVector3d FallDirectionEnu = FVector3d::ZeroVector;
		bool bDisabled = false;
		float ImpactHalfWidthMeters = 0.0f;
		float ImpactHalfHeightMeters = 0.0f;
		float ImpactSeverity = 0.0f;
	};

	DRIVEINTEGRATION_API bool IsValidStructureState(const FStructureState& State);

	DRIVEINTEGRATION_API bool IsValidTrafficSignalState(const FTrafficSignalState& State);
	DRIVEINTEGRATION_API bool IsValidTrafficNetworkChecksum(const FString& Checksum);

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

	enum class EServerHealthStatus : uint8
	{
		Unknown,
		AwaitingReset,
		AwaitingControl,
		Active,
		SafeStop,
		ReconnectRequired,
		EstopLatched,
	};

	/** Authoritative host health captured atomically with a WorldState. */
	struct FServerHealth
	{
		// An omitted Health in an older schema-v2 server is not an active lease.
		bool bPresent = false;
		EServerHealthStatus Status = EServerHealthStatus::Unknown;
		uint32 TickOverrunCount = 0;
		uint64 LastCommandAgeNs = 0;
		bool bHasControlCommand = false;
		FString Message;
	};

	enum class ERuntimeRecoveryPhase : uint8
	{
		Driving = 0, Settling = 1, Holding = 2, Recovering = 3, Disabled = 4,
	};
	enum class ETurnIndicator : uint8 { Off = 0, Left = 1, Right = 2 };

	struct FVehicleDentPatch
	{
		FVector2D Position = FVector2D::ZeroVector; // normalized forward/left shell coordinates
		FVector2D Inward = FVector2D::ZeroVector; // FLU unit force direction
		float RadiusMeters = 0.0f;
		float DepthMeters = 0.0f;
		bool operator==(const FVehicleDentPatch& Other) const
		{
			return Position == Other.Position && Inward == Other.Inward
				&& RadiusMeters == Other.RadiusMeters && DepthMeters == Other.DepthMeters;
		}
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
		float PitchDegrees = 0.0f; // schema-v2 Euler, nose-up positive
		float RollDegrees = 0.0f; // schema-v2 Euler, left-up positive
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
		FServerHealth ServerHealth;
		// World-level data travels atomically through the same map/play/sequence fence.
		TArray<FTrafficSignalState> TrafficSignals;
		FString TrafficNetworkChecksum;
		TArray<FStructureState> Structures;
		FVector3d PositionEnu = FVector3d::ZeroVector; // map_enu polar, E/N/U meters
		FVector3d LinearVelocityBody = FVector3d::ZeroVector; // base_link polar FLU, m/s
		FVector3d AngularVelocityBody = FVector3d::ZeroVector; // base_link axial FLU, rad/s
		FVector3d LinearVelocityEnu = FVector3d::ZeroVector; // map_enu polar, m/s
		float CollisionHalfLengthMeters = 0.0f;
		float CollisionHalfWidthMeters = 0.0f;
		float CollisionHalfHeightMeters = 0.0f;
		float CollisionRadiusMeters = 0.0f;
		float DamagePercent = 0.0f;
		float LastImpactImpulseNs = 0.0f;
		EVehicleDamageZone DamageZone = EVehicleDamageZone::None;
		uint32 CollisionEventSequence = 0;
		TArray<FVehicleDentPatch> DentPatches;
		ERuntimeRecoveryPhase RuntimeRecoveryPhase = ERuntimeRecoveryPhase::Driving;
		FVector3d ImpactDirectionEnu = FVector3d::ZeroVector;
		bool bPedestrianDowned = false;
		bool bPedestrianAirborne = false;
		ETurnIndicator TurnIndicator = ETurnIndicator::Off;
		// NPC-only monotonic event sequence; zero before the first horn this Play.
		uint32 HornEventSequence = 0;
		ERuntimeVehicleClass RuntimeVehicleClass = ERuntimeVehicleClass::Unspecified;
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
		ERuntimeVehicleClass RequestedVehicleClass,
		uint64 Sequence,
		const FString& SourceId,
		const FString& ConnectionSessionId,
		const FString& MapChecksum);

	DRIVEINTEGRATION_API TArray<uint8> SerializeHelloEnvelope(
		uint64 Sequence,
		const FString& SourceId,
		const FString& SessionId,
		const FString& MapChecksum,
		const FString& Build,
		const TArray<FString>& Capabilities);

	/**
	 * Parses an Envelope far enough to identify an application Hello. A valid
	 * non-Hello envelope returns true with bOutIsHello=false so the caller can
	 * continue with its payload-specific parser.
	 */
	DRIVEINTEGRATION_API bool TryParseHelloEnvelope(
		TArrayView<const uint8> Data,
		FHelloInfo& OutHello,
		bool& bOutIsHello,
		FString& OutError);

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
