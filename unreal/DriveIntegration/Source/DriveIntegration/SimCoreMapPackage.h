#pragma once

#include "CoreMinimal.h"

namespace SimCoreMapPackage
{
	inline constexpr int32 ManifestFormatVersion = 1;

	struct FManifest
	{
		FString PackageDirectory;
		FString MapId;
		TArray<FString> CollisionFiles;
		FString CollisionChecksum;
	};

	/**
	 * Load a strict MapPackage manifest and verify every declared collision file
	 * against its FNV-1a identity. No undeclared collision artifact is consumed.
	 */
	DRIVEINTEGRATION_API bool LoadAndVerifyManifest(
		const FString& PackageDirectory,
		FManifest& OutManifest,
		FString& OutError);

	/** Compute the same cross-platform collision identity used by SimCore. */
	DRIVEINTEGRATION_API bool ComputeCollisionChecksum(
		const FString& PackageDirectory,
		const TArray<FString>& CollisionFiles,
		FString& OutChecksum,
		FString& OutError);

	/**
	 * Compute collision identity from the files currently on disk and replace
	 * manifest.cfg last. A partial export therefore fails closed on next load.
	 */
	DRIVEINTEGRATION_API bool WriteManifestLast(
		const FString& PackageDirectory,
		const FString& MapId,
		const TArray<FString>& CollisionFiles,
		FString& OutChecksum,
		FString& OutError);
}
