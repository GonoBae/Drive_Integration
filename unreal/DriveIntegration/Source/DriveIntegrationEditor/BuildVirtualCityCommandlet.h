#pragma once

#include "Commandlets/Commandlet.h"
#include "BuildVirtualCityCommandlet.generated.h"

/** Creates only the dedicated VirtualCity map; never mutates the user's NewMap. */
UCLASS()
class UBuildVirtualCityCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UBuildVirtualCityCommandlet();
	virtual int32 Main(const FString& Params) override;
};
