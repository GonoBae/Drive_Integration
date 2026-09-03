#pragma once

#include "CoreMinimal.h"

namespace SimCoreSedanVisualContract
{
	inline constexpr int32 WheelCount = 4;

	/** Stable authored-asset identities shared by Ego, NPC and visual QA. */
	DRIVEINTEGRATION_API const TCHAR* BodyPackagePath();
	DRIVEINTEGRATION_API const TCHAR* BodyObjectPath();
	DRIVEINTEGRATION_API const TCHAR* WheelPackagePath();
	DRIVEINTEGRATION_API const TCHAR* WheelObjectPath();

	/** SimCore CG-local wheel centres in centimetres: FL, FR, RL, RR. */
	DRIVEINTEGRATION_API TConstArrayView<FVector> WheelOriginsCm();
}
