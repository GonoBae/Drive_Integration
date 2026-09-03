#include "SimCoreSteeringInput.h"

namespace SimCoreSteeringInput
{
float AdvanceKeyboardCommand(
	const float CurrentCommand,
	const bool bLeftPressed,
	const bool bRightPressed,
	const float DeltaSeconds,
	const float RiseRatePerSecond,
	const float ReturnRatePerSecond)
{
	const float SafeCurrent = FMath::IsFinite(CurrentCommand)
		? FMath::Clamp(CurrentCommand, -1.0f, 1.0f)
		: 0.0f;
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return SafeCurrent;

	const float Target = bLeftPressed == bRightPressed
		? 0.0f
		: (bLeftPressed ? 1.0f : -1.0f);
	const float RequestedRate = FMath::IsNearlyZero(Target)
		? ReturnRatePerSecond
		: RiseRatePerSecond;
	const float SafeRate = FMath::IsFinite(RequestedRate)
		? FMath::Max(RequestedRate, 0.0f)
		: 0.0f;
	return FMath::FInterpConstantTo(SafeCurrent, Target, DeltaSeconds, SafeRate);
}

float ResolveCommand(
	const float KeyboardCommand,
	const bool bKeyboardOwnsInput,
	const float AnalogCommand)
{
	const float SafeKeyboard = FMath::IsFinite(KeyboardCommand)
		? FMath::Clamp(KeyboardCommand, -1.0f, 1.0f)
		: 0.0f;
	const float SafeAnalog = FMath::IsFinite(AnalogCommand)
		? FMath::Clamp(AnalogCommand, -1.0f, 1.0f)
		: 0.0f;
	// A keyboard key is binary, but a short tap must still mean a small rack
	// movement. Keep the existing frame-independent time-to-full-lock while
	// giving the first half of the key ramp a finer response. This shaping is
	// deliberately speed-neutral and never touches throttle or brake input.
	const float ShapedKeyboard = FMath::Sign(SafeKeyboard)
		* SafeKeyboard * SafeKeyboard;
	return bKeyboardOwnsInput ? ShapedKeyboard : SafeAnalog;
}
}
