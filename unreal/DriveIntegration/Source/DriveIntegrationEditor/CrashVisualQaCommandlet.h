#pragma once

#include "Commandlets/Commandlet.h"
#include "CrashVisualQaCommandlet.generated.h"

/** Authored-state GPU presentation fixture, not a server/PIE physics acceptance test. */
UCLASS()
class UCrashVisualQaCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UCrashVisualQaCommandlet();
	virtual int32 Main(const FString& Params) override;
};
