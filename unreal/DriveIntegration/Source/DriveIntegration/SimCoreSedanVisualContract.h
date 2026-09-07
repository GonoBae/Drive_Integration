#pragma once

#include "CoreMinimal.h"

namespace SimCoreSedanVisualContract
{
	inline constexpr int32 WheelCount = 4;

	/** Stable authored-asset identities shared by Ego, NPC and visual QA. */
	DRIVEINTEGRATION_API const TCHAR* BodyPackagePath();
	DRIVEINTEGRATION_API const TCHAR* BodyObjectPath();
	DRIVEINTEGRATION_API const TCHAR* DriverDoorPackagePath();
	DRIVEINTEGRATION_API const TCHAR* DriverDoorObjectPath();
	DRIVEINTEGRATION_API const TCHAR* WheelPackagePath();
	DRIVEINTEGRATION_API const TCHAR* WheelObjectPath();

	/** SimCore CG-local wheel centres in centimetres: FL, FR, RL, RR. */
	DRIVEINTEGRATION_API TConstArrayView<FVector> WheelOriginsCm();
	/** Shared curved-deck/lamp coordinates; runtime lights use the authored lens, not guessed boxes. */
	DRIVEINTEGRATION_API double BodyHalfWidthCm(double X);
	DRIVEINTEGRATION_API double BodyDeckHeightCm(double X);
	DRIVEINTEGRATION_API FVector LampLensPointCm(bool bFront, bool bLeft, double U, double V, double Lift);
	DRIVEINTEGRATION_API FVector TurnSignalLensPointCm(bool bFront, bool bLeft, double U, double V);
	inline const FName SignalLeftParameter(TEXT("SignalLeftOn"));
	inline const FName SignalRightParameter(TEXT("SignalRightOn"));
	inline const FName SignalMaterialName(TEXT("M_Sedan_Amber"));
}
