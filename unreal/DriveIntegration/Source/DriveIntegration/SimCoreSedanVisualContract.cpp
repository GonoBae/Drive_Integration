#include "SimCoreSedanVisualContract.h"

namespace
{
	double Curve(double X, std::initializer_list<FVector2D> Keys)
	{
		const FVector2D* It = Keys.begin();
		FVector2D Previous = *It++;
		for (; It != Keys.end(); ++It)
		{
			if (X <= It->X)
			{
				const double T = FMath::Clamp((X - Previous.X) / (It->X - Previous.X), 0.0, 1.0);
				return FMath::Lerp(Previous.Y, It->Y, T*T*(3-2*T));
			}
			Previous = *It;
		}
		return Previous.Y;
	}
	const FVector WheelOrigins[] = {
		FVector(121.5, -79.0, -23.0),
		FVector(121.5, 79.0, -23.0),
		FVector(-148.5, -79.0, -23.0),
		FVector(-148.5, 79.0, -23.0),
	};
	static_assert(UE_ARRAY_COUNT(WheelOrigins) == SimCoreSedanVisualContract::WheelCount);
}

namespace SimCoreSedanVisualContract
{
const TCHAR* BodyPackagePath()
{
	return TEXT("/Game/Vehicles/Sedan/SM_SedanBody");
}

const TCHAR* BodyObjectPath()
{
	return TEXT("/Game/Vehicles/Sedan/SM_SedanBody.SM_SedanBody");
}

const TCHAR* DriverDoorPackagePath()
{
	return TEXT("/Game/Vehicles/Sedan/SM_SedanDoorLeft");
}

const TCHAR* DriverDoorObjectPath()
{
	return TEXT("/Game/Vehicles/Sedan/SM_SedanDoorLeft.SM_SedanDoorLeft");
}

const TCHAR* WheelPackagePath()
{
	return TEXT("/Game/Vehicles/Sedan/SM_SedanWheel");
}

const TCHAR* WheelObjectPath()
{
	return TEXT("/Game/Vehicles/Sedan/SM_SedanWheel.SM_SedanWheel");
}

TConstArrayView<FVector> WheelOriginsCm()
{
	return MakeArrayView(WheelOrigins);
}

double BodyHalfWidthCm(double X)
{
	return Curve(X, {{-228.5,66}, {-216,80}, {-188,88}, {-130,90}, {25,90}, {135,90}, {178,85}, {193,77}, {201.5,65}});
}

double BodyDeckHeightCm(double X)
{
	return Curve(X, {{-228.5,9}, {-216,27}, {-185,37}, {-115,41}, {40,38}, {130,34}, {178,29}, {193,21}, {201.5,9}});
}

FVector LampLensPointCm(bool bFront, bool bLeft, double U, double V, double Lift)
{
	const double X = bFront ? FMath::Lerp(196.0,182.0,V)-4.0*U : FMath::Lerp(-226.0,-209.0,V)+3.0*U;
	const double Across = (bLeft ? -1.0 : 1.0) * FMath::Lerp(0.36,0.96,U);
	return FVector(X, Across*(BodyHalfWidthCm(X)-7.5), BodyDeckHeightCm(X)-2.0+3.0*(1.0-Across*Across)+Lift);
}

FVector TurnSignalLensPointCm(bool bFront, bool bLeft, double U, double V)
{
	return LampLensPointCm(bFront, bLeft, FMath::Lerp(0.035,0.965,U), FMath::Lerp(0.15,0.33,V), 1.0);
}
}
