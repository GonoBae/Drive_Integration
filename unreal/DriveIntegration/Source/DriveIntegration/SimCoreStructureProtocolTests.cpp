#include "SimCoreProtocol.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
using namespace SimCoreProtocol;

void Varint(TArray<uint8>& Out, uint64 Value)
{
	while (Value >= 128) { Out.Add(static_cast<uint8>(Value) | 128); Value >>= 7; }
	Out.Add(static_cast<uint8>(Value));
}
void Integer(TArray<uint8>& Out, uint32 Field, uint64 Value)
{
	Varint(Out, static_cast<uint64>(Field) << 3); Varint(Out, Value);
}
void Message(TArray<uint8>& Out, uint32 Field, const TArray<uint8>& Value)
{
	Varint(Out, (static_cast<uint64>(Field) << 3) | 2); Varint(Out, Value.Num()); Out.Append(Value);
}
void Text(TArray<uint8>& Out, uint32 Field, const FString& Value)
{
	FTCHARToUTF8 Utf8(*Value); TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()); Message(Out, Field, Bytes);
}
template <typename T> void Number(TArray<uint8>& Out, uint32 Field, T Value)
{
	Varint(Out, (static_cast<uint64>(Field) << 3) | (sizeof(T) == 4 ? 5 : 1));
	uint64 Bits = 0; FMemory::Memcpy(&Bits, &Value, sizeof(T));
	for (uint32 Index = 0; Index < sizeof(T); ++Index) Out.Add(static_cast<uint8>(Bits >> (8 * Index)));
}
void Vector(TArray<uint8>& Out, uint32 Field, const FVector3d& Value)
{
	TArray<uint8> Bytes; Number(Bytes, 1, Value.X); Number(Bytes, 2, Value.Y); Number(Bytes, 3, Value.Z);
	Message(Out, Field, Bytes);
}
FStructureState Building()
{
	FStructureState State;
	State.ColliderId = TEXT("building-1"); State.Kind = EStructureKind::Building;
	State.DamagePercent = 25.0f; State.EventSequence = 4;
	State.ImpactPointEnu = FVector3d(10, 20, 1.1); State.ImpactNormalEnu = FVector3d(-1, 0, 0);
	State.BasePositionEnu = FVector3d(12, 20, .16); State.HeadingRadians = -UE_DOUBLE_PI;
	return State;
}
FStructureState Pole()
{
	auto State = Building(); State.ColliderId = TEXT("signal-pole-7"); State.Kind = EStructureKind::SignalPole;
	State.SignalId = 7; State.DamagePercent = 100.0f; State.bDisabled = true;
	State.FallAngleRadians = static_cast<float>(UE_DOUBLE_PI / 2.0); State.FallDirectionEnu = FVector3d(1, 0, 0);
	return State;
}
TArray<uint8> StructureBytes(const FStructureState& State, bool bIncludeKind = true)
{
	TArray<uint8> Out; Text(Out, 1, State.ColliderId);
	if (bIncludeKind) Integer(Out, 2, static_cast<uint8>(State.Kind));
	Integer(Out, 3, State.SignalId); Number(Out, 4, State.DamagePercent); Integer(Out, 5, State.EventSequence);
	Vector(Out, 6, State.ImpactPointEnu); Vector(Out, 7, State.ImpactNormalEnu); Vector(Out, 8, State.BasePositionEnu);
	Number(Out, 9, State.HeadingRadians); Number(Out, 10, State.FallAngleRadians); Vector(Out, 11, State.FallDirectionEnu);
	Integer(Out, 12, State.bDisabled ? 1 : 0);
	Number(Out, 13, State.ImpactHalfWidthMeters); Number(Out, 14, State.ImpactHalfHeightMeters);
	Number(Out, 15, State.ImpactSeverity); return Out;
}
TArray<uint8> SignalBytes(uint32 Id = 7, bool bOutOfService = true,
	ETrafficSignalAspect Aspect = ETrafficSignalAspect::Red, uint32 Controller = 1)
{
	TArray<uint8> Out; Integer(Out, 1, Id); Integer(Out, 2, Id);
	Integer(Out, 3, static_cast<uint8>(Aspect)); Integer(Out, 7, Controller);
	Integer(Out, 9, bOutOfService ? 1 : 0); return Out;
}
TArray<uint8> Envelope(const TArray<TArray<uint8>>& Structures,
	const TArray<TArray<uint8>>& Signals = {}, bool bStructuresFirst = false,
	const TArray<uint8>& WorldExtra = {})
{
	TArray<uint8> World, Out;
	const auto AddStructures = [&]() { for (const auto& Value : Structures) Message(World, 5, Value); };
	if (bStructuresFirst) AddStructures();
	for (uint32 Id : {1u, 2u}) { TArray<uint8> Entity; Integer(Entity, 1, Id); Message(World, 1, Entity); }
	for (const auto& Value : Signals) Message(World, 3, Value);
	if (!Signals.IsEmpty()) Text(World, 4, TEXT("fnv1a64:0123456789abcdef"));
	if (!bStructuresFirst) AddStructures();
	World.Append(WorldExtra);
	Integer(Out, 1, SchemaVersion); Integer(Out, 2, 45); Integer(Out, 3, 900000);
	Text(Out, 5, TEXT("fnv1a64:fedcba9876543210")); Text(Out, 7, TEXT("damage-play")); Message(Out, 12, World);
	return Out;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreStructureProtocolTest,
	"DriveIntegration.Protocol.StructureDamage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreStructureProtocolTest::RunTest(const FString& Parameters)
{
	bool Ok = true; FVehicleState State; TArray<FVehicleState> Entities; FString Error;
	auto Footprint = Building(); Footprint.ImpactHalfWidthMeters = 1.2f;
	Footprint.ImpactHalfHeightMeters = .45f; Footprint.ImpactSeverity = .7f;
	Ok &= TestTrue(TEXT("contact footprint and event severity roundtrip"),
		ParseWorldStateEnvelope(Envelope({StructureBytes(Footprint)}),1,State,Error)
		&& State.Structures.Num() == 1 && State.Structures[0].ImpactHalfWidthMeters == 1.2f
		&& State.Structures[0].ImpactHalfHeightMeters == .45f && State.Structures[0].ImpactSeverity == .7f);
	Footprint.ImpactHalfWidthMeters = 3.1f;
	Ok &= TestFalse(TEXT("oversized contact footprint rejected atomically"),
		ParseWorldStateEnvelope(Envelope({StructureBytes(Footprint)}),1,State,Error));
	Footprint.ImpactHalfWidthMeters = 1.f; Footprint.ImpactSeverity = std::numeric_limits<float>::quiet_NaN();
	Ok &= TestFalse(TEXT("nonfinite impact severity rejected"),
		ParseWorldStateEnvelope(Envelope({StructureBytes(Footprint)}),1,State,Error));
	for (bool First : {false, true})
	{
		Ok &= TestTrue(TEXT("Building and pole damage parse before or after entity/signal lists"),
			ParseWorldStateEnvelope(Envelope({StructureBytes(Building()), StructureBytes(Pole())}, {SignalBytes()}, First),
				2, State, Entities, Error));
		Ok &= TestTrue(TEXT("Structure list shares pose, map, play, and sequence on every entity"),
			State.Structures.Num() == 2 && State.Sequence == 45 && State.PlaySessionId == TEXT("damage-play")
			&& Entities.Num() == 2 && Entities[0].Structures.Num() == 2 && Entities[1].Structures.Num() == 2);
		if (State.Structures.Num() == 2)
		{
			const auto& Wall = State.Structures[0]; const auto& Post = State.Structures[1];
			Ok &= TestTrue(TEXT("Building identity and impact values remain exact"),
				Wall.ColliderId == TEXT("building-1") && Wall.Kind == EStructureKind::Building
				&& Wall.DamagePercent == 25.f && Wall.EventSequence == 4 && Wall.ImpactPointEnu.Z == 1.1
				&& Wall.ImpactNormalEnu.X == -1 && Wall.BasePositionEnu.Z == .16 && Wall.HeadingRadians == -UE_DOUBLE_PI);
			Ok &= TestTrue(TEXT("Fallen pole retains hinge pose and signal disable latch"),
				Post.Kind == EStructureKind::SignalPole && Post.SignalId == 7 && Post.bDisabled
				&& Post.FallAngleRadians == static_cast<float>(UE_DOUBLE_PI / 2.0) && Post.FallDirectionEnu.X == 1
				&& State.TrafficSignals[0].bOutOfService && State.TrafficSignals[0].Aspect == ETrafficSignalAspect::Red);
		}
	}
	Ok &= TestTrue(TEXT("Legacy empty structure list clears damage without altering normal signals"),
		ParseWorldStateEnvelope(Envelope({}, {SignalBytes(7, false, ETrafficSignalAspect::Green)}), 1, State, Error)
		&& State.Structures.IsEmpty() && !State.TrafficSignals[0].bOutOfService);
	TArray<uint8> Future = StructureBytes(Building()); Integer(Future, 99, 42);
	TArray<uint8> WorldFuture; Integer(WorldFuture, 99, 42);
	Ok &= TestTrue(TEXT("Unknown additive fields remain forward-compatible"),
		ParseWorldStateEnvelope(Envelope({Future}, {}, false, WorldFuture), 1, State, Error));
	auto Westbound = Building(); Westbound.HeadingRadians = 1.5 * UE_DOUBLE_PI;
	Ok &= TestTrue(TEXT("Unwrapped 270-degree navigation heading remains valid"),
		ParseWorldStateEnvelope(Envelope({StructureBytes(Westbound)}), 1, State, Error));
	Ok &= TestTrue(TEXT("An unrelated signal controller may remain green"),
		ParseWorldStateEnvelope(Envelope({StructureBytes(Pole())},
			{SignalBytes(), SignalBytes(8, false, ETrafficSignalAspect::Green, 2)}), 1, State, Error));
	TArray<TArray<uint8>> Maximum;
	for (int32 Index = 0; Index < MaxWorldStateStructures; ++Index)
	{
		auto Value = Building(); Value.ColliderId = FString::Printf(TEXT("building-%d"), Index);
		Maximum.Add(StructureBytes(Value));
	}
	Ok &= TestTrue(TEXT("Exactly 256 unique structures are accepted"),
		ParseWorldStateEnvelope(Envelope(Maximum), 1, State, Error) && State.Structures.Num() == 256);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreStructureProtocolValidationTest,
	"DriveIntegration.Protocol.StructureDamageValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCoreStructureProtocolValidationTest::RunTest(const FString& Parameters)
{
	bool Ok = true; FVehicleState State; TArray<FVehicleState> Entities; FString Error;
	const auto Reject = [&](const TCHAR* Why, const TArray<uint8>& Bytes)
	{
		State.Structures = {Pole()};
		return TestFalse(Why, ParseWorldStateEnvelope(Bytes, 1, State, Entities, Error))
			&& TestTrue(TEXT("Invalid world cannot leak partial entities, damage, or permissive signals"),
				State.Structures.IsEmpty() && State.TrafficSignals.IsEmpty() && Entities.IsEmpty());
	};
	for (int32 Invalid = 0; Invalid < 19; ++Invalid)
	{
		auto Bad = Building();
		if (Invalid == 0) Bad.ColliderId.Reset();
		if (Invalid == 1) Bad.ColliderId = FString::ChrN(129, TEXT('a'));
		if (Invalid == 2) Bad.ColliderId = TEXT("bad\nidentity");
		if (Invalid == 3) Bad.Kind = static_cast<EStructureKind>(99);
		if (Invalid == 4) Bad.DamagePercent = std::numeric_limits<float>::quiet_NaN();
		if (Invalid == 5) Bad.DamagePercent = 101.f;
		if (Invalid == 6) Bad.DamagePercent = 0.f;
		if (Invalid == 7) Bad.EventSequence = 0;
		if (Invalid == 8) Bad.ImpactPointEnu.X = 1'000'001;
		if (Invalid == 9) Bad.ImpactNormalEnu = FVector3d::ZeroVector;
		if (Invalid == 10) Bad.BasePositionEnu.Z = std::numeric_limits<double>::infinity();
		if (Invalid == 11) Bad.HeadingRadians = 2.0 * UE_DOUBLE_PI + .01;
		if (Invalid == 12) Bad.FallAngleRadians = .1f;
		if (Invalid == 13) Bad.SignalId = 7;
		if (Invalid == 14) Bad.bDisabled = true;
		if (Invalid == 15) Bad.FallDirectionEnu.Z = .1;
		if (Invalid == 16) Bad.ImpactNormalEnu.X = 2;
		if (Invalid == 17) Bad.FallAngleRadians = std::numeric_limits<float>::quiet_NaN();
		if (Invalid == 18) Bad.HeadingRadians = std::numeric_limits<double>::infinity();
		Ok &= Reject(TEXT("Invalid building snapshot"), Envelope({StructureBytes(Bad)}));
	}
	for (int32 Invalid = 0; Invalid < 7; ++Invalid)
	{
		auto Bad = Pole();
		if (Invalid == 0) Bad.SignalId = 0;
		if (Invalid == 1) Bad.SignalId = 99;
		if (Invalid == 2) Bad.FallAngleRadians = -.1f;
		if (Invalid == 3) Bad.FallAngleRadians = 2.f;
		if (Invalid == 4) Bad.FallDirectionEnu.X = 2;
		if (Invalid == 5) Bad.FallDirectionEnu.X = std::numeric_limits<double>::quiet_NaN();
		if (Invalid == 6) Bad.bDisabled = false;
		Ok &= Reject(TEXT("Invalid or mismatched pole snapshot"), Envelope({StructureBytes(Bad)}, {SignalBytes()}));
	}
	Ok &= Reject(TEXT("Missing kind cannot silently become building"), Envelope({StructureBytes(Building(), false)}));
	Ok &= Reject(TEXT("Duplicate structure ID"), Envelope({StructureBytes(Building()), StructureBytes(Building())}));
	auto AnotherPole = Pole(); AnotherPole.ColliderId = TEXT("another-pole");
	Ok &= Reject(TEXT("One signal cannot own two pole snapshots"),
		Envelope({StructureBytes(Pole()), StructureBytes(AnotherPole)}, {SignalBytes()}));
	Ok &= Reject(TEXT("Missing referenced signal"), Envelope({StructureBytes(Pole())}));
	Ok &= Reject(TEXT("Missing damaged pole for outage"), Envelope({}, {SignalBytes()}));
	Ok &= Reject(TEXT("Outage cannot be green"), Envelope({StructureBytes(Pole())}, {SignalBytes(7, true, ETrafficSignalAspect::Green)}));
	Ok &= Reject(TEXT("Outage forces all groups in its controller red"), Envelope({StructureBytes(Pole())},
		{SignalBytes(), SignalBytes(8, false, ETrafficSignalAspect::Green)}));
	TArray<uint8> Duplicate = StructureBytes(Building()); Integer(Duplicate, 5, 5);
	Ok &= Reject(TEXT("Duplicate known field fails closed"), Envelope({Duplicate}));
	TArray<uint8> Huge = StructureBytes(Building()); TArray<uint8> Padding; Padding.SetNumZeroed(1025); Message(Huge, 99, Padding);
	Ok &= Reject(TEXT("Unknown fields cannot bypass bounded structure message size"), Envelope({Huge}));
	TArray<uint8> WrongWire; Integer(WrongWire, 5, 1);
	Ok &= Reject(TEXT("Structure message with scalar wire is invalid"), Envelope({}, {}, false, WrongWire));
	TArray<TArray<uint8>> Overflow;
	for (int32 Index = 0; Index <= MaxWorldStateStructures; ++Index)
	{
		auto Value = Building(); Value.ColliderId = FString::Printf(TEXT("building-%d"), Index); Overflow.Add(StructureBytes(Value));
	}
	Ok &= Reject(TEXT("257 structures exceed the snapshot resource bound"), Envelope(Overflow));
	return Ok;
}

#endif
