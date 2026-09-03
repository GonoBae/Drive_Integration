#pragma once

#include "CoreMinimal.h"

namespace SimCoreSteeringInput
{
	/**
	 * Keyboard buttons represent a steering intent, not an instant full-lock
	 * wheel angle. The ramp is frame-rate independent and remains speed-neutral;
	 * the authoritative server still owns the physical steering rack.
	 */
	DRIVEINTEGRATION_API float AdvanceKeyboardCommand(
		float CurrentCommand,
		bool bLeftPressed,
		bool bRightPressed,
		float DeltaSeconds,
		float RiseRatePerSecond,
		float ReturnRatePerSecond);

	DRIVEINTEGRATION_API float ResolveCommand(
		float KeyboardCommand,
		bool bKeyboardOwnsInput,
		float AnalogCommand);
}
