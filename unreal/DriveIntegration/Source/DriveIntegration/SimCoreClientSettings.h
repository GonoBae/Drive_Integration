#pragma once

#include "CoreMinimal.h"

namespace SimCoreClientSettings
{
struct FSettings
{
	FString ServerUrl;
	FString MapPackageDirectory;
	FString LoadedConfigPath;
};

// R1 has no authentication or remote-server security contract: keep loopback-only.
bool NormalizeServerUrl(const FString& Value, FString& OutUrl, FString& OutError);
bool ParseConfig(const FString& Text, const FString& ConfigDirectory,
	FSettings& InOutSettings, FString& OutError);

// Serialized settings < optional ProjectDir/SimCoreClient.ini < CLI URL.
// Explicit config paths are relative to ProjectDir, map paths to the config file.
// An explicit missing file or an invalid present file never silently falls back.
bool Load(const FSettings& Defaults, const FString& CommandLine,
	const FString& ProjectDirectory, FSettings& OutSettings, FString& OutError);
}
