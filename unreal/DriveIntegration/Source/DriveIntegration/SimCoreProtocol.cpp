#include "SimCoreProtocol.h"

namespace SimCoreProtocol
{
namespace
{
	constexpr int32 MaxWorldStateEntities = 256;

	void WriteVarint(TArray<uint8>& Out, uint64 Value)
	{
		while (Value >= 0x80)
		{
			Out.Add(static_cast<uint8>(Value) | 0x80);
			Value >>= 7;
		}
		Out.Add(static_cast<uint8>(Value));
	}

	void WriteTag(TArray<uint8>& Out, uint32 Field, uint8 WireType)
	{
		WriteVarint(Out, (static_cast<uint64>(Field) << 3) | WireType);
	}

	void WriteFixed32(TArray<uint8>& Out, float Value)
	{
		uint32 Bits = 0;
		static_assert(sizeof(Bits) == sizeof(Value));
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Out.Add(static_cast<uint8>(Bits >> (Index * 8)));
		}
	}

	void WriteBytes(TArray<uint8>& Out, uint32 Field, TArrayView<const uint8> Bytes)
	{
		WriteTag(Out, Field, 2);
		WriteVarint(Out, Bytes.Num());
		Out.Append(Bytes.GetData(), Bytes.Num());
	}

	void WriteString(TArray<uint8>& Out, uint32 Field, const FString& Value)
	{
		FTCHARToUTF8 Utf8(*Value);
		WriteTag(Out, Field, 2);
		WriteVarint(Out, Utf8.Length());
		Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	class FReader
	{
	public:
		explicit FReader(TArrayView<const uint8> InData) : Data(InData) {}

		bool AtEnd() const { return Offset == Data.Num(); }

		bool ReadVarint(uint64& Out)
		{
			Out = 0;
			for (uint32 Shift = 0; Shift < 64 && Offset < Data.Num(); Shift += 7)
			{
				const uint8 Byte = Data[Offset++];
				if (Shift == 63 && (Byte & 0xfe) != 0)
				{
					return false;
				}
				Out |= static_cast<uint64>(Byte & 0x7f) << Shift;
				if ((Byte & 0x80) == 0) return true;
			}
			return false;
		}

		bool ReadTag(uint32& Field, uint8& WireType)
		{
			uint64 Tag = 0;
			if (!ReadVarint(Tag) || Tag == 0) return false;
			const uint64 FieldValue = Tag >> 3;
			if (FieldValue == 0 || FieldValue > 0x1fffffffULL) return false;
			Field = static_cast<uint32>(FieldValue);
			WireType = static_cast<uint8>(Tag & 7);
			return true;
		}

		bool ReadFixed32(float& Out)
		{
			if (Offset + 4 > Data.Num()) return false;
			uint32 Bits = 0;
			for (int32 Index = 0; Index < 4; ++Index) Bits |= static_cast<uint32>(Data[Offset++]) << (Index * 8);
			FMemory::Memcpy(&Out, &Bits, sizeof(Out));
			return true;
		}

		bool ReadFixed64(double& Out)
		{
			if (Offset + 8 > Data.Num()) return false;
			uint64 Bits = 0;
			for (int32 Index = 0; Index < 8; ++Index) Bits |= static_cast<uint64>(Data[Offset++]) << (Index * 8);
			FMemory::Memcpy(&Out, &Bits, sizeof(Out));
			return true;
		}

		bool ReadMessage(TArrayView<const uint8>& Out)
		{
			uint64 Length = 0;
			if (!ReadVarint(Length) || Length > static_cast<uint64>(Data.Num() - Offset)) return false;
			Out = Data.Slice(Offset, static_cast<int32>(Length));
			Offset += static_cast<int32>(Length);
			return true;
		}

		bool ReadString(FString& Out, int32 MaxBytes = MAX_int32)
		{
			TArrayView<const uint8> Bytes;
			if (!ReadMessage(Bytes) || Bytes.Num() > MaxBytes) return false;
			if (Bytes.IsEmpty())
			{
				Out.Reset();
				return true;
			}

			FUTF8ToTCHAR Converted(
				reinterpret_cast<const ANSICHAR*>(Bytes.GetData()),
				Bytes.Num());
			Out = FString(Converted.Length(), Converted.Get());
			return true;
		}

		bool Skip(uint8 WireType)
		{
			switch (WireType)
			{
			case 0: { uint64 Ignored; return ReadVarint(Ignored); }
			case 1: if (Offset + 8 > Data.Num()) return false; Offset += 8; return true;
			case 2: { TArrayView<const uint8> Ignored; return ReadMessage(Ignored); }
			case 5: if (Offset + 4 > Data.Num()) return false; Offset += 4; return true;
			default: return false;
			}
		}

	private:
		TArrayView<const uint8> Data;
		int32 Offset = 0;
	};

	bool IsFiniteVector(const FVector3d& Vector)
	{
		return FMath::IsFinite(Vector.X)
			&& FMath::IsFinite(Vector.Y)
			&& FMath::IsFinite(Vector.Z);
	}

	bool IsBoundedVector(const FVector3d& Vector, double MaximumMagnitude)
	{
		return IsFiniteVector(Vector)
			&& FMath::Abs(Vector.X) <= MaximumMagnitude
			&& FMath::Abs(Vector.Y) <= MaximumMagnitude
			&& FMath::Abs(Vector.Z) <= MaximumMagnitude;
	}

	bool ParseStructureVector(TArrayView<const uint8> Data, FVector3d& Vector)
	{
		if (Data.Num() > 128) return false;
		FReader Reader(Data);
		uint32 Seen = 0;
		while (!Reader.AtEnd())
		{
			uint32 Field; uint8 Wire;
			if (!Reader.ReadTag(Field, Wire)) return false;
			if (Field >= 1 && Field <= 3)
			{
				const uint32 Mask = 1u << Field;
				if (Seen & Mask) return false;
				Seen |= Mask;
				double& Value = Field == 1 ? Vector.X : Field == 2 ? Vector.Y : Vector.Z;
				if (Wire != 1 || !Reader.ReadFixed64(Value)) return false;
			}
			else if (!Reader.Skip(Wire)) return false;
		}
		return IsFiniteVector(Vector);
	}

	bool ParseStructure(TArrayView<const uint8> Data, FStructureState& State)
	{
		if (Data.Num() > 1024) return false;
		FReader Reader(Data);
		uint32 Seen = 0;
		while (!Reader.AtEnd())
		{
			uint32 Field; uint8 Wire; uint64 Integer = 0;
			if (!Reader.ReadTag(Field, Wire)) return false;
			if (Field <= 15)
			{
				const uint32 Mask = 1u << Field;
				if (Seen & Mask) return false;
				Seen |= Mask;
			}
			switch (Field)
			{
			case 1:
				if (Wire != 2 || !Reader.ReadString(State.ColliderId, 128)) return false;
				break;
			case 2:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer < 1 || Integer > 2) return false;
				State.Kind = static_cast<EStructureKind>(Integer);
				break;
			case 3:
			case 5:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > MAX_uint32) return false;
				(Field == 3 ? State.SignalId : State.EventSequence) = static_cast<uint32>(Integer);
				break;
			case 4:
				if (Wire != 5 || !Reader.ReadFixed32(State.DamagePercent)) return false;
				break;
			case 6:
			case 7:
			case 8:
			case 11:
			{
				TArrayView<const uint8> VectorData;
				FVector3d& Vector = Field == 6 ? State.ImpactPointEnu
					: Field == 7 ? State.ImpactNormalEnu
					: Field == 8 ? State.BasePositionEnu : State.FallDirectionEnu;
				if (Wire != 2 || !Reader.ReadMessage(VectorData)
					|| !ParseStructureVector(VectorData, Vector)) return false;
				break;
			}
			case 9:
				if (Wire != 1 || !Reader.ReadFixed64(State.HeadingRadians)) return false;
				break;
			case 10:
				if (Wire != 5 || !Reader.ReadFixed32(State.FallAngleRadians)) return false;
				break;
			case 12:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > 1) return false;
				State.bDisabled = Integer != 0;
				break;
			case 13: if (Wire != 5 || !Reader.ReadFixed32(State.ImpactHalfWidthMeters)) return false; break;
			case 14: if (Wire != 5 || !Reader.ReadFixed32(State.ImpactHalfHeightMeters)) return false; break;
			case 15: if (Wire != 5 || !Reader.ReadFixed32(State.ImpactSeverity)) return false; break;
			default:
				if (!Reader.Skip(Wire)) return false;
				break;
			}
		}
		// Kind has no proto3 zero/default meaning. Origin/heading may be omitted.
		return (Seen & (1u << 2)) != 0 && IsValidStructureState(State);
	}

	bool ParseTrafficSignal(TArrayView<const uint8> Data, FTrafficSignalState& State)
	{
		// Bound the complete message, including otherwise forward-compatible fields.
		if (Data.Num() > 512) return false;
		FReader Reader(Data);
		uint32 Seen = 0;
		while (!Reader.AtEnd())
		{
			uint32 Field; uint8 Wire; uint64 Integer = 0;
			if (!Reader.ReadTag(Field, Wire)) return false;
			if (Field <= 9)
			{
				const uint32 Mask = 1u << Field;
				if (Seen & Mask) return false;
				Seen |= Mask;
			}
			switch (Field)
			{
			case 1:
			case 2:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer == 0 || Integer > MAX_uint32) return false;
				(Field == 1 ? State.SignalId : State.GroupId) = static_cast<uint32>(Integer);
				break;
			case 3:
				if (Wire != 0 || !Reader.ReadVarint(Integer)
					|| Integer > static_cast<uint8>(ETrafficSignalAspect::Green)) return false;
				State.Aspect = static_cast<ETrafficSignalAspect>(Integer);
				break;
			case 4:
			{
				TArrayView<const uint8> Vector;
				if (Wire != 2 || !Reader.ReadMessage(Vector) || Vector.Num() > 128) return false;
				FReader VectorReader(Vector);
				uint32 SeenCoordinates = 0;
				while (!VectorReader.AtEnd())
				{
					uint32 Coordinate; uint8 CoordinateWire;
					if (!VectorReader.ReadTag(Coordinate, CoordinateWire)) return false;
					if (Coordinate >= 1 && Coordinate <= 3)
					{
						const uint32 Mask = 1u << Coordinate;
						if (SeenCoordinates & Mask) return false;
						SeenCoordinates |= Mask;
						double& Value = Coordinate == 1 ? State.PositionEnu.X
							: Coordinate == 2 ? State.PositionEnu.Y : State.PositionEnu.Z;
						if (CoordinateWire != 1 || !VectorReader.ReadFixed64(Value)) return false;
					}
					else if (!VectorReader.Skip(CoordinateWire)) return false;
				}
				break;
			}
			case 5:
				if (Wire != 5 || !Reader.ReadFixed32(State.HeadingDegrees)) return false;
				break;
			case 6:
				if (Wire != 5 || !Reader.ReadFixed32(State.RemainingSeconds)) return false;
				break;
			case 7:
				if (Wire != 0 || !Reader.ReadVarint(Integer)
					|| Integer == 0 || Integer > MAX_uint32) return false;
				State.ControllerId = static_cast<uint32>(Integer);
				break;
			case 8:
				if (Wire != 0 || !Reader.ReadVarint(Integer)
					|| Integer < static_cast<uint8>(ETrafficSignalKind::Vehicle)
					|| Integer > static_cast<uint8>(ETrafficSignalKind::Pedestrian)) return false;
				State.Kind = static_cast<ETrafficSignalKind>(Integer);
				break;
			case 9:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > 1) return false;
				State.bOutOfService = Integer != 0;
				break;
			default:
				if (!Reader.Skip(Wire)) return false;
				break;
			}
		}
		// Proto3 may omit zero-valued coordinates, heading, countdown, and UNKNOWN.
		return IsValidTrafficSignalState(State);
	}

	bool IsValidEntityState(const FVehicleState& State)
	{
		constexpr double MaxWorldCoordinateMeters = 10'000'000.0;
		constexpr double MaxVelocityMetersPerSecond = 100'000.0;
		constexpr double MaxAngularVelocityRadPerSecond = 10'000.0;
		constexpr float MaxCollisionExtentMeters = 1'000.0f;
		constexpr float MaxImpactImpulseNs = 100'000'000.0f;
		if (State.EntityId == 0
			|| !FMath::IsFinite(State.Timestamp)
			|| !FMath::IsFinite(State.Latitude)
			|| !FMath::IsFinite(State.Longitude)
			|| !FMath::IsFinite(State.Altitude)
			|| !FMath::IsFinite(State.HeadingDegrees)
			|| !FMath::IsFinite(State.PitchDegrees)
			|| !FMath::IsFinite(State.RollDegrees)
			|| !FMath::IsFinite(State.SpeedMps)
			|| !FMath::IsFinite(State.AccelMps2)
			|| !FMath::IsFinite(State.FuelPercent)
			|| !FMath::IsFinite(State.EngineRpm)
			|| !FMath::IsFinite(State.EastMeters)
			|| !FMath::IsFinite(State.NorthMeters)
			|| !FMath::IsFinite(State.YawRateRad)
			|| !FMath::IsFinite(State.SteeringAngleRad)
			|| FMath::Abs(State.EastMeters) > MaxWorldCoordinateMeters
			|| FMath::Abs(State.NorthMeters) > MaxWorldCoordinateMeters
			|| FMath::Abs(State.Altitude) > MaxWorldCoordinateMeters
			|| !IsBoundedVector(State.PositionEnu, MaxWorldCoordinateMeters)
			|| !IsBoundedVector(
				State.LinearVelocityBody, MaxVelocityMetersPerSecond)
			|| !IsBoundedVector(
				State.AngularVelocityBody, MaxAngularVelocityRadPerSecond)
			|| !IsBoundedVector(
				State.LinearVelocityEnu, MaxVelocityMetersPerSecond)
			|| !FMath::IsFinite(State.CollisionHalfLengthMeters)
			|| !FMath::IsFinite(State.CollisionHalfWidthMeters)
			|| !FMath::IsFinite(State.CollisionHalfHeightMeters)
			|| !FMath::IsFinite(State.CollisionRadiusMeters)
			|| !FMath::IsFinite(State.DamagePercent)
			|| !FMath::IsFinite(State.LastImpactImpulseNs)
			|| State.CollisionHalfLengthMeters < 0.0f
			|| State.CollisionHalfWidthMeters < 0.0f
			|| State.CollisionHalfHeightMeters < 0.0f
			|| State.CollisionRadiusMeters < 0.0f
			|| State.CollisionHalfLengthMeters > MaxCollisionExtentMeters
			|| State.CollisionHalfWidthMeters > MaxCollisionExtentMeters
			|| State.CollisionHalfHeightMeters > MaxCollisionExtentMeters
			|| State.CollisionRadiusMeters > MaxCollisionExtentMeters
			|| State.DamagePercent < 0.0f
			|| State.DamagePercent > 100.0f
			|| State.LastImpactImpulseNs < 0.0f
			|| State.LastImpactImpulseNs > MaxImpactImpulseNs
			|| static_cast<uint8>(State.RuntimeRecoveryPhase) > static_cast<uint8>(ERuntimeRecoveryPhase::Disabled)
			|| static_cast<uint8>(State.TurnIndicator) > 2
			|| static_cast<uint8>(State.RuntimeVehicleClass)
				> static_cast<uint8>(ERuntimeVehicleClass::Motorcycle)
			|| (State.EntityKind != EEntityKind::NpcVehicle
				&& State.HornEventSequence != 0)
			|| ((State.EntityKind != EEntityKind::NpcVehicle
				&& State.EntityKind != EEntityKind::EgoVehicle)
				&& State.RuntimeVehicleClass != ERuntimeVehicleClass::Unspecified)
			|| ((State.bPedestrianDowned || State.bPedestrianAirborne) && State.EntityKind != EEntityKind::Pedestrian)
			|| (State.bPedestrianAirborne && !State.bPedestrianDowned)
			|| !IsBoundedVector(State.ImpactDirectionEnu, 1.001)
			|| (!State.ImpactDirectionEnu.IsZero() && FMath::Abs(State.ImpactDirectionEnu.SizeSquared() - 1.0) > .001)
			|| static_cast<uint8>(State.DamageZone)
				> static_cast<uint8>(EVehicleDamageZone::Underbody))
		{
			return false;
		}

		if (static_cast<uint8>(State.Gear)
			> static_cast<uint8>(EVehicleGear::Reverse))
		{
			return false;
		}
		if (State.DentPatches.Num() > 16 || (State.EntityKind == EEntityKind::Pedestrian && !State.DentPatches.IsEmpty())) return false;
		for (const auto& Patch : State.DentPatches) {
			if (Patch.Position.ContainsNaN() || Patch.Inward.ContainsNaN()
				|| FMath::Abs(Patch.Position.X) > 1.001 || FMath::Abs(Patch.Position.Y) > 1.001
				|| FMath::Max(FMath::Abs(Patch.Position.X),FMath::Abs(Patch.Position.Y)) < .99
				|| FMath::Abs(Patch.Inward.Size()-1.0) >= .01
				|| FVector2D::DotProduct(Patch.Position,Patch.Inward) >= -.05
				|| !FMath::IsFinite(Patch.RadiusMeters) || Patch.RadiusMeters < .15f || Patch.RadiusMeters > .95f
				|| !FMath::IsFinite(Patch.DepthMeters) || Patch.DepthMeters <= 0.0f || Patch.DepthMeters > .28f) return false;
		}
		for (const FVehicleState::FWheelState& Wheel : State.Wheels)
		{
			if (!FMath::IsFinite(Wheel.SteeringAngleRad)
				|| !FMath::IsFinite(Wheel.AngularSpeedRad)
				|| !FMath::IsFinite(Wheel.NormalLoadN)
				|| !FMath::IsFinite(Wheel.LongitudinalSlip)
				|| !FMath::IsFinite(Wheel.SlipAngleRad)
				|| !FMath::IsFinite(Wheel.LongitudinalForceN)
				|| !FMath::IsFinite(Wheel.LateralForceN)
				|| !IsFiniteVector(Wheel.ContactPointEnu)
				|| !IsFiniteVector(Wheel.ContactNormalEnu))
			{
				return false;
			}
		}

		switch (State.EntityKind)
		{
		case EEntityKind::Unspecified:
		case EEntityKind::EgoVehicle:
			return true;
		case EEntityKind::NpcVehicle:
			return State.CollisionHalfLengthMeters > 0.0f
				&& State.CollisionHalfWidthMeters > 0.0f
				&& State.CollisionHalfHeightMeters > 0.0f;
		case EEntityKind::Pedestrian:
			if (State.bPedestrianDowned)
				return State.CollisionRadiusMeters == 0.0f && State.CollisionHalfLengthMeters > 0.0f
					&& State.CollisionHalfWidthMeters > 0.0f && State.CollisionHalfHeightMeters > 0.0f;
			return State.CollisionRadiusMeters > 0.0f
				&& State.CollisionHalfHeightMeters
					>= State.CollisionRadiusMeters;
		default:
			return false;
		}
	}

	bool ParseEntity(TArrayView<const uint8> Data, FVehicleState& State)
	{
		auto ParseVector = [](TArrayView<const uint8> VectorData, FVector3d& Out) {
			FReader VectorReader(VectorData);
			while (!VectorReader.AtEnd()) {
				uint32 VectorField; uint8 VectorWire;
				if (!VectorReader.ReadTag(VectorField, VectorWire)) return false;
				double* Value = VectorField == 1 ? &Out.X : VectorField == 2 ? &Out.Y : VectorField == 3 ? &Out.Z : nullptr;
				if (Value) { if (VectorWire != 1 || !VectorReader.ReadFixed64(*Value)) return false; }
				else if (!VectorReader.Skip(VectorWire)) return false;
			}
			return true;
		};
		auto ParseWheel = [&ParseVector](
			TArrayView<const uint8> WheelData,
			FVehicleState::FWheelState& Wheel) {
			FReader WheelReader(WheelData);
			while (!WheelReader.AtEnd()) {
				uint32 WheelField; uint8 WheelWire; uint64 Integer = 0;
				if (!WheelReader.ReadTag(WheelField, WheelWire)) return false;
				switch (WheelField) {
				case 1: if (WheelWire != 0 || !WheelReader.ReadVarint(Integer) || Integer > MAX_uint32) return false; Wheel.WheelIndex = static_cast<uint32>(Integer); break;
				case 2: if (WheelWire != 0 || !WheelReader.ReadVarint(Integer)) return false; Wheel.bInContact = Integer != 0; break;
				case 3: if (WheelWire != 5 || !WheelReader.ReadFixed32(Wheel.SteeringAngleRad)) return false; break;
				case 4: if (WheelWire != 5 || !WheelReader.ReadFixed32(Wheel.AngularSpeedRad)) return false; break;
				case 5: if (WheelWire != 5 || !WheelReader.ReadFixed32(Wheel.NormalLoadN)) return false; break;
				case 6: if (WheelWire != 5 || !WheelReader.ReadFixed32(Wheel.LongitudinalSlip)) return false; break;
				case 7: if (WheelWire != 5 || !WheelReader.ReadFixed32(Wheel.SlipAngleRad)) return false; break;
				case 8: if (WheelWire != 5 || !WheelReader.ReadFixed32(Wheel.LongitudinalForceN)) return false; break;
				case 9: if (WheelWire != 5 || !WheelReader.ReadFixed32(Wheel.LateralForceN)) return false; break;
				case 10:
				case 11: {
					if (WheelWire != 2) return false;
					TArrayView<const uint8> VectorData;
					if (!WheelReader.ReadMessage(VectorData)) return false;
					FVector3d& Vector = WheelField == 10
						? Wheel.ContactPointEnu
						: Wheel.ContactNormalEnu;
					if (!ParseVector(VectorData, Vector)) return false;
					break;
				}
				default: if (!WheelReader.Skip(WheelWire)) return false; break;
				}
			}
			return true;
		};
		FReader Reader(Data);
		while (!Reader.AtEnd())
		{
			uint32 Field; uint8 Wire;
			if (!Reader.ReadTag(Field, Wire)) return false;
			uint64 Integer = 0;
			switch (Field)
			{
			case 1: if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > MAX_uint32) return false; State.EntityId = static_cast<uint32>(Integer); break;
			case 2: if (Wire != 1 || !Reader.ReadFixed64(State.Timestamp)) return false; break;
			case 3: if (Wire != 1 || !Reader.ReadFixed64(State.Latitude)) return false; break;
			case 4: if (Wire != 1 || !Reader.ReadFixed64(State.Longitude)) return false; break;
			case 5: if (Wire != 1 || !Reader.ReadFixed64(State.Altitude)) return false; break;
			case 6: if (Wire != 5 || !Reader.ReadFixed32(State.HeadingDegrees)) return false; break;
			case 7: if (Wire != 5 || !Reader.ReadFixed32(State.PitchDegrees)) return false; break;
			case 8: if (Wire != 5 || !Reader.ReadFixed32(State.RollDegrees)) return false; break;
			case 9: if (Wire != 5 || !Reader.ReadFixed32(State.SpeedMps)) return false; break;
			case 10: if (Wire != 5 || !Reader.ReadFixed32(State.AccelMps2)) return false; break;
			case 11: if (Wire != 5 || !Reader.ReadFixed32(State.FuelPercent)) return false; break;
			case 12: if (Wire != 5 || !Reader.ReadFixed32(State.EngineRpm)) return false; break;
			case 13: if (Wire != 1 || !Reader.ReadFixed64(State.EastMeters)) return false; break;
			case 14: if (Wire != 1 || !Reader.ReadFixed64(State.NorthMeters)) return false; break;
			case 15: if (Wire != 5 || !Reader.ReadFixed32(State.YawRateRad)) return false; break;
			case 16: if (Wire != 5 || !Reader.ReadFixed32(State.SteeringAngleRad)) return false; break;
			case 17: if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > static_cast<uint8>(EVehicleGear::Reverse)) return false; State.Gear = static_cast<EVehicleGear>(Integer); break;
			case 18:
			case 19:
			case 20: {
				if (Wire != 2) return false; TArrayView<const uint8> VectorData;
				if (!Reader.ReadMessage(VectorData)) return false;
				FVector3d& Vector = Field == 18 ? State.PositionEnu : Field == 19 ? State.LinearVelocityBody : State.AngularVelocityBody;
				if (!ParseVector(VectorData, Vector)) return false; break;
			}
			case 21: {
				if (State.Wheels.Num() >= 32) return false;
				if (Wire != 2) return false; TArrayView<const uint8> WheelData;
				if (!Reader.ReadMessage(WheelData)) return false;
				FVehicleState::FWheelState Wheel;
				if (!ParseWheel(WheelData, Wheel)) return false;
				State.Wheels.Add(Wheel); break;
			}
			case 22:
				if (Wire != 0 || !Reader.ReadVarint(Integer)
					|| Integer > static_cast<uint8>(EEntityKind::Pedestrian)) return false;
				State.EntityKind = static_cast<EEntityKind>(Integer);
				break;
			case 23: {
				if (Wire != 2) return false;
				TArrayView<const uint8> VectorData;
				if (!Reader.ReadMessage(VectorData)
					|| !ParseVector(VectorData, State.LinearVelocityEnu)) return false;
				break;
			}
			case 24: if (Wire != 5 || !Reader.ReadFixed32(State.CollisionHalfLengthMeters)) return false; break;
			case 25: if (Wire != 5 || !Reader.ReadFixed32(State.CollisionHalfWidthMeters)) return false; break;
			case 26: if (Wire != 5 || !Reader.ReadFixed32(State.CollisionHalfHeightMeters)) return false; break;
			case 27: if (Wire != 5 || !Reader.ReadFixed32(State.CollisionRadiusMeters)) return false; break;
			case 28: if (Wire != 5 || !Reader.ReadFixed32(State.DamagePercent)) return false; break;
			case 29: if (Wire != 5 || !Reader.ReadFixed32(State.LastImpactImpulseNs)) return false; break;
			case 30:
				if (Wire != 0 || !Reader.ReadVarint(Integer)
					|| Integer > static_cast<uint8>(EVehicleDamageZone::Underbody)) return false;
				State.DamageZone = static_cast<EVehicleDamageZone>(Integer);
				break;
			case 31:
				if (Wire != 0 || !Reader.ReadVarint(Integer)
					|| Integer > MAX_uint32) return false;
				State.CollisionEventSequence = static_cast<uint32>(Integer);
				break;
			case 32:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > 4) return false;
				State.RuntimeRecoveryPhase = static_cast<ERuntimeRecoveryPhase>(Integer);
				break;
			case 33: {
				TArrayView<const uint8> VectorData;
				if (Wire != 2 || !Reader.ReadMessage(VectorData)
					|| !ParseStructureVector(VectorData, State.ImpactDirectionEnu)) return false;
				break;
			}
			case 34:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > 1) return false;
				State.bPedestrianDowned = Integer != 0;
				break;
			case 35:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > 1) return false;
				State.bPedestrianAirborne = Integer != 0;
				break;
			case 36:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > 2) return false;
				State.TurnIndicator = static_cast<ETurnIndicator>(Integer);
				break;
			case 37: {
				TArrayView<const uint8> PatchData;
				if (Wire != 2 || State.DentPatches.Num() >= 16 || !Reader.ReadMessage(PatchData)) return false;
				FReader PatchReader(PatchData);
				float Values[6] = {};
				while (!PatchReader.AtEnd()) {
					uint32 PatchField; uint8 PatchWire;
					if (!PatchReader.ReadTag(PatchField,PatchWire)) return false;
					if (PatchField >= 1 && PatchField <= 6) {
						if (PatchWire != 5 || !PatchReader.ReadFixed32(Values[PatchField-1])) return false;
					} else if (!PatchReader.Skip(PatchWire)) return false;
				}
				State.DentPatches.Add({FVector2D(Values[0],Values[1]),FVector2D(Values[2],Values[3]),Values[4],Values[5]});
				break;
			}
			case 38:
				if (Wire != 0 || !Reader.ReadVarint(Integer)
					|| Integer > MAX_uint32) return false;
				State.HornEventSequence = static_cast<uint32>(Integer);
				break;
			case 39:
				if (Wire != 0 || !Reader.ReadVarint(Integer)
					|| Integer > static_cast<uint8>(ERuntimeVehicleClass::Motorcycle)) return false;
				State.RuntimeVehicleClass = static_cast<ERuntimeVehicleClass>(Integer);
				break;
			default: if (!Reader.Skip(Wire)) return false; break;
			}
		}
		return IsValidEntityState(State);
	}

	bool ParseHealth(TArrayView<const uint8> Data, FServerHealth& Health)
	{
		FReader Reader(Data);
		FString Status;
		while (!Reader.AtEnd())
		{
			uint32 Field; uint8 Wire; uint64 Integer = 0;
			if (!Reader.ReadTag(Field, Wire)) return false;
			switch (Field)
			{
			case 1:
				if (Wire != 2 || !Reader.ReadString(Status, 64)) return false;
				break;
			case 2:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > MAX_uint32) return false;
				Health.TickOverrunCount = static_cast<uint32>(Integer);
				break;
			case 3:
				if (Wire != 0 || !Reader.ReadVarint(Health.LastCommandAgeNs)) return false;
				break;
			case 4:
				if (Wire != 2 || !Reader.ReadString(Health.Message, 512)) return false;
				break;
			case 5:
				if (Wire != 0 || !Reader.ReadVarint(Integer) || Integer > 1) return false;
				Health.bHasControlCommand = Integer != 0;
				break;
			default:
				if (!Reader.Skip(Wire)) return false;
				break;
			}
		}
		// Keep future statuses backward compatible, but never interpret them as
		// Active. Only these exact, case-sensitive values are authoritative states.
		if (Status.Equals(TEXT("awaiting_reset"), ESearchCase::CaseSensitive)) Health.Status = EServerHealthStatus::AwaitingReset;
		else if (Status.Equals(TEXT("awaiting_control"), ESearchCase::CaseSensitive)) Health.Status = EServerHealthStatus::AwaitingControl;
		else if (Status.Equals(TEXT("active"), ESearchCase::CaseSensitive)) Health.Status = EServerHealthStatus::Active;
		else if (Status.Equals(TEXT("safe_stop"), ESearchCase::CaseSensitive)) Health.Status = EServerHealthStatus::SafeStop;
		else if (Status.Equals(TEXT("reconnect_required"), ESearchCase::CaseSensitive)) Health.Status = EServerHealthStatus::ReconnectRequired;
		else if (Status.Equals(TEXT("estop_latched"), ESearchCase::CaseSensitive)) Health.Status = EServerHealthStatus::EstopLatched;
		// Prevent an untrusted reason from inserting extra HUD lines.
		for (TCHAR& Character : Health.Message)
		{
			if (Character < TEXT(' ') || Character == 127) Character = TEXT(' ');
		}
		Health.bPresent = true;
		return true;
	}

	bool ParseWorldState(TArrayView<const uint8> Data, uint32 TargetEntityId,
		FVehicleState& State, TArray<FVehicleState>& Entities, FString& OutError)
	{
		FReader Reader(Data);
		bool bFoundTarget = false;
		FServerHealth Health;
		TSet<uint32> EntityIds;
		TArray<FTrafficSignalState> Signals;
		TSet<uint32> SignalIds;
		TArray<FStructureState> Structures;
		TSet<FString> StructureIds;
		FString TrafficChecksum;
		bool bHasTrafficChecksum = false;
		while (!Reader.AtEnd())
		{
			uint32 Field; uint8 Wire;
			if (!Reader.ReadTag(Field, Wire)) return false;
			if (Field == 1 && Wire == 2)
			{
				if (Entities.Num() >= MaxWorldStateEntities)
				{
					OutError = FString::Printf(
						TEXT("WorldState exceeds the %d entity limit"),
						MaxWorldStateEntities);
					return false;
				}
				TArrayView<const uint8> Entity;
				FVehicleState Candidate;
				if (!Reader.ReadMessage(Entity) || !ParseEntity(Entity, Candidate))
				{
					OutError = TEXT("WorldState contains an invalid entity");
					return false;
				}
				if (EntityIds.Contains(Candidate.EntityId))
				{
					OutError = FString::Printf(
						TEXT("WorldState contains duplicate entity ID %u"),
						Candidate.EntityId);
					return false;
				}
				EntityIds.Add(Candidate.EntityId);
				const bool bIsTarget = !bFoundTarget
					&& (TargetEntityId == 0 || Candidate.EntityId == TargetEntityId);
				Entities.Add(MoveTemp(Candidate));
				if (bIsTarget)
				{
					State = Entities.Last();
					bFoundTarget = true;
				}
				continue;
			}
			if (Field == 2)
			{
				TArrayView<const uint8> HealthData;
				if (Wire != 2 || Health.bPresent || !Reader.ReadMessage(HealthData)
					|| !ParseHealth(HealthData, Health))
				{
					OutError = TEXT("WorldState contains invalid or duplicate Health");
					return false;
				}
				continue;
			}
			if (Field == 3)
			{
				TArrayView<const uint8> SignalData;
				FTrafficSignalState Signal;
				if (Signals.Num() >= MaxWorldStateTrafficSignals || Wire != 2
					|| !Reader.ReadMessage(SignalData) || !ParseTrafficSignal(SignalData, Signal)
					|| SignalIds.Contains(Signal.SignalId))
				{
					OutError = TEXT("WorldState contains invalid, duplicate, or too many traffic signals");
					return false;
				}
				SignalIds.Add(Signal.SignalId);
				Signals.Add(MoveTemp(Signal));
				continue;
			}
			if (Field == 4)
			{
				if (Wire != 2 || bHasTrafficChecksum || !Reader.ReadString(TrafficChecksum, 24)
					|| (!TrafficChecksum.IsEmpty() && !IsValidTrafficNetworkChecksum(TrafficChecksum)))
				{
					OutError = TEXT("WorldState contains an invalid or duplicate traffic network checksum");
					return false;
				}
				bHasTrafficChecksum = true;
				continue;
			}
			if (Field == 5)
			{
				TArrayView<const uint8> StructureData;
				FStructureState Structure;
				if (Structures.Num() >= MaxWorldStateStructures || Wire != 2
					|| !Reader.ReadMessage(StructureData) || !ParseStructure(StructureData, Structure)
					|| StructureIds.Contains(Structure.ColliderId))
				{
					OutError = TEXT("WorldState contains invalid, duplicate, or too many structures");
					return false;
				}
				StructureIds.Add(Structure.ColliderId);
				Structures.Add(MoveTemp(Structure));
				continue;
			}
			if (!Reader.Skip(Wire)) return false;
		}
		if (!Signals.IsEmpty() && !IsValidTrafficNetworkChecksum(TrafficChecksum))
		{
			OutError = TEXT("WorldState traffic signals require a valid network checksum");
			return false;
		}
		// Controller identity is additive: legacy v1 heads default to controller 1.
		// Group consistency and mutually-exclusive permissions are controller-local.
		TMap<uint64, const FTrafficSignalState*> GroupStates;
		TMap<uint32, TSet<uint32>> PermissiveGroupsByController;
		TSet<uint32> ControllersPermittingVehicles;
		for (const FTrafficSignalState& Signal : Signals)
		{
			const uint64 GroupKey = (static_cast<uint64>(Signal.ControllerId) << 32)
				| static_cast<uint64>(Signal.GroupId);
			if (const FTrafficSignalState* const* Existing = GroupStates.Find(GroupKey))
			{
				if ((*Existing)->Aspect != Signal.Aspect
					|| FMath::Abs((*Existing)->RemainingSeconds - Signal.RemainingSeconds) > 0.001f)
				{
					OutError = TEXT("WorldState traffic signal group disagrees on aspect or countdown");
					return false;
				}
			}
			else GroupStates.Add(GroupKey, &Signal);
			if (Signal.Aspect == ETrafficSignalAspect::Green || Signal.Aspect == ETrafficSignalAspect::Yellow)
			{
				PermissiveGroupsByController.FindOrAdd(Signal.ControllerId).Add(Signal.GroupId);
				if (Signal.Kind != ETrafficSignalKind::Pedestrian)
					ControllersPermittingVehicles.Add(Signal.ControllerId);
			}
		}
		for (uint32 Controller : ControllersPermittingVehicles)
		{
			if (PermissiveGroupsByController.FindChecked(Controller).Num() > 1)
			{
				OutError = TEXT("WorldState contains conflicting permissive traffic signal groups");
				return false;
			}
		}
		TSet<uint32> PoleSignalIds;
		for (const FStructureState& Structure : Structures)
		{
			if (Structure.Kind != EStructureKind::SignalPole) continue;
			const FTrafficSignalState* Signal = Signals.FindByPredicate(
				[&](const FTrafficSignalState& Value) { return Value.SignalId == Structure.SignalId; });
			if (!Signal || Signal->bOutOfService != Structure.bDisabled
				|| PoleSignalIds.Contains(Structure.SignalId))
			{
				OutError = TEXT("WorldState damaged pole does not match one current signal head");
				return false;
			}
			PoleSignalIds.Add(Structure.SignalId);
		}
		for (const FTrafficSignalState& Signal : Signals)
		{
			if (!Signal.bOutOfService) continue;
			const bool bUnsafeController = Signals.ContainsByPredicate(
				[&](const FTrafficSignalState& Other) {
					return Other.ControllerId == Signal.ControllerId
						&& (Other.Aspect != ETrafficSignalAspect::Red || Other.RemainingSeconds != 0.0f);
				});
			if (!PoleSignalIds.Contains(Signal.SignalId) || bUnsafeController)
			{
				OutError = TEXT("WorldState broken head requires damaged pole and all-red controller");
				return false;
			}
		}
		State.ServerHealth = Health;
		State.TrafficSignals = Signals;
		State.TrafficNetworkChecksum = TrafficChecksum;
		State.Structures = Structures;
		for (FVehicleState& Entity : Entities)
		{
			Entity.ServerHealth = Health;
			Entity.TrafficSignals = Signals;
			Entity.TrafficNetworkChecksum = TrafficChecksum;
			Entity.Structures = Structures;
		}
		return bFoundTarget;
	}
}

bool IsValidTrafficSignalState(const FTrafficSignalState& State)
{
	return State.SignalId != 0 && State.GroupId >= 1 && State.GroupId <= 4096
		&& State.ControllerId >= 1 && State.ControllerId <= 64
		&& static_cast<uint8>(State.Aspect) <= static_cast<uint8>(ETrafficSignalAspect::Green)
		&& static_cast<uint8>(State.Kind) >= static_cast<uint8>(ETrafficSignalKind::Vehicle)
		&& static_cast<uint8>(State.Kind) <= static_cast<uint8>(ETrafficSignalKind::Pedestrian)
		&& IsBoundedVector(State.PositionEnu, 1'000'000.0)
		&& FMath::IsFinite(State.HeadingDegrees)
		&& State.HeadingDegrees >= 0.0f && State.HeadingDegrees < 360.0f
		&& FMath::IsFinite(State.RemainingSeconds)
		&& State.RemainingSeconds >= 0.0f
		&& State.RemainingSeconds <= MaxTrafficSignalCountdownSeconds
		&& (!State.bOutOfService
			|| (State.Aspect == ETrafficSignalAspect::Red && State.RemainingSeconds == 0.0f));
}

bool IsValidStructureState(const FStructureState& State)
{
	const bool bBuilding = State.Kind == EStructureKind::Building;
	const bool bPole = State.Kind == EStructureKind::SignalPole;
	if (State.ColliderId.IsEmpty() || State.ColliderId.Len() > 128) return false;
	for (TCHAR Character : State.ColliderId)
	{
		if (Character < 33 || Character > 126) return false;
	}
	return (bBuilding || bPole) && FMath::IsFinite(State.DamagePercent)
		&& State.DamagePercent > 0.0f && State.DamagePercent <= 100.0f && State.EventSequence != 0
		&& FMath::IsFinite(State.ImpactHalfWidthMeters) && State.ImpactHalfWidthMeters >= 0.0f && State.ImpactHalfWidthMeters <= 3.0f
		&& FMath::IsFinite(State.ImpactHalfHeightMeters) && State.ImpactHalfHeightMeters >= 0.0f && State.ImpactHalfHeightMeters <= 3.0f
		&& FMath::IsFinite(State.ImpactSeverity) && State.ImpactSeverity >= 0.0f && State.ImpactSeverity <= 1.0f
		&& IsBoundedVector(State.ImpactPointEnu, 1'000'000.0)
		&& IsBoundedVector(State.BasePositionEnu, 1'000'000.0)
		&& IsBoundedVector(State.ImpactNormalEnu, 1.001)
		&& FMath::Abs(State.ImpactNormalEnu.SizeSquared() - 1.0) <= .001
		&& FMath::IsFinite(State.HeadingRadians) && FMath::Abs(State.HeadingRadians) <= 2.0 * UE_DOUBLE_PI
		&& FMath::IsFinite(State.FallAngleRadians)
		&& State.FallAngleRadians >= 0.0f && State.FallAngleRadians <= static_cast<float>(UE_DOUBLE_PI / 2.0)
		&& IsBoundedVector(State.FallDirectionEnu, 1.001) && State.FallDirectionEnu.Z == 0.0
		&& (!bBuilding || (State.SignalId == 0 && State.FallAngleRadians == 0.0f && !State.bDisabled))
		&& (!bPole || (State.SignalId != 0 && FMath::Abs(State.FallDirectionEnu.SizeSquared() - 1.0) <= .001
			&& (State.FallAngleRadians == 0.0f || State.bDisabled)));
}

bool IsValidTrafficNetworkChecksum(const FString& Checksum)
{
	if (Checksum.Len() != 24 || !Checksum.StartsWith(TEXT("fnv1a64:"), ESearchCase::CaseSensitive)) return false;
	for (int32 Index = 8; Index < Checksum.Len(); ++Index)
	{
		const TCHAR Character = Checksum[Index];
		if (!((Character >= TEXT('0') && Character <= TEXT('9'))
			|| (Character >= TEXT('a') && Character <= TEXT('f')))) return false;
	}
	return true;
}

TArray<uint8> SerializeControlEnvelope(const FControlCommand& Command, uint64 Sequence,
	const FString& SourceId, const FString& SessionId, const FString& MapChecksum)
{
	TArray<uint8> Control;
	WriteTag(Control, 1, 0); WriteVarint(Control, Command.bEstop ? 2 : 0);
	WriteTag(Control, 2, 5); WriteFixed32(Control, FMath::Clamp(Command.Throttle, 0.0f, 1.0f));
	WriteTag(Control, 3, 5); WriteFixed32(Control, FMath::Clamp(Command.Brake, 0.0f, 1.0f));
	WriteTag(Control, 4, 5); WriteFixed32(Control, FMath::Clamp(Command.Steering, -1.0f, 1.0f));
	WriteTag(Control, 5, 0); WriteVarint(Control, Command.bHandbrake ? 1 : 0);
	WriteTag(Control, 6, 0); WriteVarint(Control, static_cast<uint8>(Command.Gear));
	WriteTag(Control, 7, 0); WriteVarint(Control, Command.bEstop ? 1 : 0);
	WriteTag(Control, 8, 0); WriteVarint(Control, Command.ClientTimeNs);

	TArray<uint8> Envelope;
	WriteTag(Envelope, 1, 0); WriteVarint(Envelope, SchemaVersion);
	WriteTag(Envelope, 2, 0); WriteVarint(Envelope, Sequence);
	WriteString(Envelope, 4, SourceId);
	WriteString(Envelope, 5, MapChecksum);
	WriteString(Envelope, 6, SessionId);
	WriteBytes(Envelope, 11, Control);
	return Envelope;
}

TArray<uint8> SerializeSimulationResetEnvelope(
	const FString& PlaySessionId,
	uint64 ClientTimeNs,
	ERuntimeVehicleClass RequestedVehicleClass,
	uint64 Sequence,
	const FString& SourceId,
	const FString& ConnectionSessionId,
	const FString& MapChecksum)
{
	TArray<uint8> Reset;
	WriteString(Reset, 1, PlaySessionId);
	WriteTag(Reset, 2, 0); WriteVarint(Reset, ClientTimeNs);
	WriteTag(Reset, 3, 0); WriteVarint(Reset,
		static_cast<uint8>(RequestedVehicleClass) <= static_cast<uint8>(ERuntimeVehicleClass::Motorcycle)
			? static_cast<uint8>(RequestedVehicleClass)
			: static_cast<uint8>(ERuntimeVehicleClass::Sedan));

	TArray<uint8> Envelope;
	WriteTag(Envelope, 1, 0); WriteVarint(Envelope, SchemaVersion);
	WriteTag(Envelope, 2, 0); WriteVarint(Envelope, Sequence);
	WriteString(Envelope, 4, SourceId);
	WriteString(Envelope, 5, MapChecksum);
	WriteString(Envelope, 6, ConnectionSessionId);
	WriteString(Envelope, 7, PlaySessionId);
	WriteBytes(Envelope, 14, Reset);
	return Envelope;
}

TArray<uint8> SerializeHelloEnvelope(
	uint64 Sequence,
	const FString& SourceId,
	const FString& SessionId,
	const FString& MapChecksum,
	const FString& Build,
	const TArray<FString>& Capabilities)
{
	TArray<uint8> Hello;
	WriteString(Hello, 1, Build);
	WriteString(Hello, 2, SchemaName);
	for (const FString& Capability : Capabilities)
	{
		WriteString(Hello, 3, Capability);
	}

	TArray<uint8> Envelope;
	WriteTag(Envelope, 1, 0); WriteVarint(Envelope, SchemaVersion);
	WriteTag(Envelope, 2, 0); WriteVarint(Envelope, Sequence);
	WriteString(Envelope, 4, SourceId);
	WriteString(Envelope, 5, MapChecksum);
	WriteString(Envelope, 6, SessionId);
	WriteBytes(Envelope, 10, Hello);
	return Envelope;
}

bool TryParseHelloEnvelope(
	TArrayView<const uint8> Data,
	FHelloInfo& OutHello,
	bool& bOutIsHello,
	FString& OutError)
{
	OutHello = {};
	bOutIsHello = false;
	OutError.Reset();

	FReader Reader(Data);
	uint32 Version = 0;
	uint32 LastPayloadField = 0;
	TArrayView<const uint8> HelloPayload;
	while (!Reader.AtEnd())
	{
		uint32 Field = 0;
		uint8 Wire = 0;
		if (!Reader.ReadTag(Field, Wire))
		{
			OutError = TEXT("Invalid protobuf tag");
			return false;
		}
		uint64 Integer = 0;
		if (Field == 1 && Wire == 0)
		{
			if (!Reader.ReadVarint(Integer) || Integer > MAX_uint32)
			{
				OutError = TEXT("Invalid schema version");
				return false;
			}
			Version = static_cast<uint32>(Integer);
		}
		else if (Field == 2 && Wire == 0)
		{
			if (!Reader.ReadVarint(OutHello.Sequence))
			{
				OutError = TEXT("Invalid envelope sequence");
				return false;
			}
		}
		else if (Field == 4 && Wire == 2)
		{
			if (!Reader.ReadString(OutHello.SourceId))
			{
				OutError = TEXT("Invalid source ID");
				return false;
			}
		}
		else if (Field == 5 && Wire == 2)
		{
			if (!Reader.ReadString(OutHello.MapPackageChecksum))
			{
				OutError = TEXT("Invalid map package checksum");
				return false;
			}
		}
		else if (Field == 6 && Wire == 2)
		{
			if (!Reader.ReadString(OutHello.SessionId))
			{
				OutError = TEXT("Invalid session ID");
				return false;
			}
		}
		else if (Field >= 10 && Field <= 14)
		{
			if (Wire != 2)
			{
				OutError = TEXT("Invalid Envelope payload wire type");
				return false;
			}
			TArrayView<const uint8> Payload;
			if (!Reader.ReadMessage(Payload))
			{
				OutError = TEXT("Invalid Envelope payload");
				return false;
			}
			LastPayloadField = Field;
			if (Field == 10)
			{
				HelloPayload = Payload;
			}
		}
		else if (!Reader.Skip(Wire))
		{
			OutError = TEXT("Unsupported protobuf wire value");
			return false;
		}
	}

	if (Version != SchemaVersion)
	{
		OutError = FString::Printf(
			TEXT("Schema version mismatch: expected %u, got %u"),
			SchemaVersion,
			Version);
		return false;
	}
	if (LastPayloadField != 10)
	{
		return true;
	}

	FReader HelloReader(HelloPayload);
	while (!HelloReader.AtEnd())
	{
		uint32 Field = 0;
		uint8 Wire = 0;
		if (!HelloReader.ReadTag(Field, Wire))
		{
			OutError = TEXT("Invalid Hello protobuf tag");
			return false;
		}
		if (Field == 1 && Wire == 2)
		{
			if (!HelloReader.ReadString(OutHello.Build))
			{
				OutError = TEXT("Invalid Hello build");
				return false;
			}
		}
		else if (Field == 2 && Wire == 2)
		{
			if (!HelloReader.ReadString(OutHello.Schema))
			{
				OutError = TEXT("Invalid Hello schema");
				return false;
			}
		}
		else if (Field == 3 && Wire == 2)
		{
			if (OutHello.Capabilities.Num() >= 32)
			{
				OutError = TEXT("Hello advertises more than 32 capabilities");
				return false;
			}
			FString Capability;
			if (!HelloReader.ReadString(Capability))
			{
				OutError = TEXT("Invalid Hello capability");
				return false;
			}
			OutHello.Capabilities.Add(MoveTemp(Capability));
		}
		else if (!HelloReader.Skip(Wire))
		{
			OutError = TEXT("Unsupported Hello protobuf wire value");
			return false;
		}
	}

	bOutIsHello = true;
	return true;
}

bool ParseWorldStateEnvelope(TArrayView<const uint8> Data, uint32 TargetEntityId,
	FVehicleState& OutState, FString& OutError)
{
	TArray<FVehicleState> IgnoredEntities;
	return ParseWorldStateEnvelope(
		Data, TargetEntityId, OutState, IgnoredEntities, OutError);
}

bool ParseWorldStateEnvelope(TArrayView<const uint8> Data, uint32 TargetEntityId,
	FVehicleState& OutState, TArray<FVehicleState>& OutEntities, FString& OutError)
{
	OutError.Reset();
	OutState = {};
	OutEntities.Reset();
	FReader Reader(Data);
	uint32 Version = 0;
	uint64 Sequence = 0;
	uint64 SimulationTimeNs = 0;
	FString MapPackageChecksum;
	FString PlaySessionId;
	FVehicleState ParsedState;
	TArrayView<const uint8> WorldStatePayload;
	uint32 LastPayloadField = 0;
	while (!Reader.AtEnd())
	{
		uint32 Field; uint8 Wire;
		if (!Reader.ReadTag(Field, Wire)) { OutError = TEXT("Invalid protobuf tag"); return false; }
		uint64 Integer = 0;
		if (Field == 1 && Wire == 0)
		{
			if (!Reader.ReadVarint(Integer) || Integer > MAX_uint32) { OutError = TEXT("Invalid schema version"); return false; }
			Version = static_cast<uint32>(Integer);
		}
		else if (Field == 2 && Wire == 0)
		{
			if (!Reader.ReadVarint(Sequence)) { OutError = TEXT("Invalid envelope sequence"); return false; }
		}
		else if (Field == 3 && Wire == 0)
		{
			if (!Reader.ReadVarint(SimulationTimeNs)) { OutError = TEXT("Invalid simulation time"); return false; }
		}
		else if (Field == 5 && Wire == 2)
		{
			if (!Reader.ReadString(MapPackageChecksum)) { OutError = TEXT("Invalid map package checksum"); return false; }
		}
		else if (Field == 7 && Wire == 2)
		{
			if (!Reader.ReadString(PlaySessionId)) { OutError = TEXT("Invalid play session ID"); return false; }
		}
		else if (Field >= 10 && Field <= 14)
		{
			if (Wire != 2)
			{
				OutError = TEXT("Invalid Envelope payload wire type");
				return false;
			}
			TArrayView<const uint8> Payload;
			if (!Reader.ReadMessage(Payload))
			{
				OutError = TEXT("Invalid Envelope payload");
				return false;
			}
			LastPayloadField = Field;
			if (Field == 12)
			{
				WorldStatePayload = Payload;
			}
		}
		else if (!Reader.Skip(Wire)) { OutError = TEXT("Unsupported protobuf wire value"); return false; }
	}
	if (Version != SchemaVersion) { OutError = FString::Printf(TEXT("Schema version mismatch: expected %u, got %u"), SchemaVersion, Version); return false; }
	if (MapPackageChecksum.IsEmpty() || MapPackageChecksum == TEXT("unset")) { OutError = TEXT("WorldState is missing a valid map package checksum"); return false; }
	if (LastPayloadField != 12) { OutError = TEXT("Envelope does not contain WorldState as its active payload"); return false; }
	if (!ParseWorldState(
		WorldStatePayload, TargetEntityId, ParsedState, OutEntities, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("WorldState does not contain entity %u"), TargetEntityId);
		}
		OutEntities.Reset();
		return false;
	}
	for (FVehicleState& Entity : OutEntities)
	{
		Entity.Sequence = Sequence;
		Entity.SimulationTimeNs = SimulationTimeNs;
		Entity.MapPackageChecksum = MapPackageChecksum;
		Entity.PlaySessionId = PlaySessionId;
		if (Entity.EntityId == ParsedState.EntityId)
		{
			ParsedState = Entity;
		}
	}
	OutState = MoveTemp(ParsedState);
	return true;
}
}
