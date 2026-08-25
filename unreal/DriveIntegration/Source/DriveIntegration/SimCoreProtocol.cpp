#include "SimCoreProtocol.h"

namespace SimCoreProtocol
{
namespace
{
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
				Out |= static_cast<uint64>(Byte & 0x7f) << Shift;
				if ((Byte & 0x80) == 0) return true;
			}
			return false;
		}

		bool ReadTag(uint32& Field, uint8& WireType)
		{
			uint64 Tag = 0;
			if (!ReadVarint(Tag) || Tag == 0) return false;
			Field = static_cast<uint32>(Tag >> 3);
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
				case 1: if (WheelWire != 0 || !WheelReader.ReadVarint(Integer)) return false; Wheel.WheelIndex = static_cast<uint32>(Integer); break;
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
			case 1: if (Wire != 0 || !Reader.ReadVarint(Integer)) return false; State.EntityId = static_cast<uint32>(Integer); break;
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
			case 17: if (Wire != 0 || !Reader.ReadVarint(Integer)) return false; State.Gear = static_cast<EVehicleGear>(Integer); break;
			case 18:
			case 19:
			case 20: {
				if (Wire != 2) return false; TArrayView<const uint8> VectorData;
				if (!Reader.ReadMessage(VectorData)) return false;
				FVector3d& Vector = Field == 18 ? State.PositionEnu : Field == 19 ? State.LinearVelocityBody : State.AngularVelocityBody;
				if (!ParseVector(VectorData, Vector)) return false; break;
			}
			case 21: {
				if (Wire != 2) return false; TArrayView<const uint8> WheelData;
				if (!Reader.ReadMessage(WheelData)) return false;
				FVehicleState::FWheelState Wheel;
				if (!ParseWheel(WheelData, Wheel)) return false;
				State.Wheels.Add(Wheel); break;
			}
			default: if (!Reader.Skip(Wire)) return false; break;
			}
		}
		return true;
	}

	bool ParseWorldState(TArrayView<const uint8> Data, uint32 TargetEntityId,
		FVehicleState& State)
	{
		FReader Reader(Data);
		bool bFoundTarget = false;
		while (!Reader.AtEnd())
		{
			uint32 Field; uint8 Wire;
			if (!Reader.ReadTag(Field, Wire)) return false;
			if (Field == 1 && Wire == 2)
			{
				TArrayView<const uint8> Entity;
				FVehicleState Candidate;
				if (!Reader.ReadMessage(Entity) || !ParseEntity(Entity, Candidate)) return false;
				if (!bFoundTarget && (TargetEntityId == 0 || Candidate.EntityId == TargetEntityId))
				{
					State = MoveTemp(Candidate);
					bFoundTarget = true;
				}
				continue;
			}
			if (!Reader.Skip(Wire)) return false;
		}
		return bFoundTarget;
	}
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

bool ParseWorldStateEnvelope(TArrayView<const uint8> Data, uint32 TargetEntityId,
	FVehicleState& OutState, FString& OutError)
{
	OutError.Reset();
	FReader Reader(Data);
	uint32 Version = 0;
	uint64 Sequence = 0;
	uint64 SimulationTimeNs = 0;
	FVehicleState ParsedState;
	TArrayView<const uint8> WorldStatePayload;
	bool bFoundWorldState = false;
	while (!Reader.AtEnd())
	{
		uint32 Field; uint8 Wire;
		if (!Reader.ReadTag(Field, Wire)) { OutError = TEXT("Invalid protobuf tag"); return false; }
		uint64 Integer = 0;
		if (Field == 1 && Wire == 0)
		{
			if (!Reader.ReadVarint(Integer)) { OutError = TEXT("Invalid schema version"); return false; }
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
		else if (Field == 12 && Wire == 2)
		{
			if (!Reader.ReadMessage(WorldStatePayload))
			{
				OutError = TEXT("Invalid WorldState payload");
				return false;
			}
			bFoundWorldState = true;
		}
		else if (!Reader.Skip(Wire)) { OutError = TEXT("Unsupported protobuf wire value"); return false; }
	}
	if (Version != SchemaVersion) { OutError = FString::Printf(TEXT("Schema version mismatch: expected %u, got %u"), SchemaVersion, Version); return false; }
	if (!bFoundWorldState) { OutError = TEXT("Envelope does not contain WorldState"); return false; }
	if (!ParseWorldState(WorldStatePayload, TargetEntityId, ParsedState))
	{
		OutError = FString::Printf(TEXT("WorldState does not contain entity %u"), TargetEntityId);
		return false;
	}
	ParsedState.Sequence = Sequence;
	ParsedState.SimulationTimeNs = SimulationTimeNs;
	OutState = MoveTemp(ParsedState);
	return true;
}
}
