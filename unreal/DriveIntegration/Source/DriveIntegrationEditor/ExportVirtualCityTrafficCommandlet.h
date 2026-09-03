#pragma once

#include "Commandlets/Commandlet.h"
#include "ExportVirtualCityTrafficCommandlet.generated.h"

/** Read the existing map, never save/bake it; create traffic JSON once or compare it. */
UCLASS()
class UExportVirtualCityTrafficCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UExportVirtualCityTrafficCommandlet();
	virtual int32 Main(const FString& Params) override;
};
