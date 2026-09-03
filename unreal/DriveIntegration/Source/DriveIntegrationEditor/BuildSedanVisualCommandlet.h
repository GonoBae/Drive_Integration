#pragma once

#include "Commandlets/Commandlet.h"
#include "BuildSedanVisualCommandlet.generated.h"

/** Authors the project's own sedan visual assets without downloading third-party art. */
UCLASS()
class UBuildSedanVisualCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UBuildSedanVisualCommandlet();
	virtual int32 Main(const FString& Params) override;
};
