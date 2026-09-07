#include "SimCoreMapPackage.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace SimCoreMapPackage
{
namespace
{
	constexpr uint64 FnvOffsetBasis = 14695981039346656037ull;
	constexpr uint64 FnvPrime = 1099511628211ull;
	constexpr int64 MaxCollisionPayloadBytes = 512ll * 1024ll * 1024ll;

	void HashBytes(uint64& Hash, const uint8* Bytes, int64 Count)
	{
		for (int64 Index = 0; Index < Count; ++Index)
		{
			Hash ^= Bytes[Index];
			Hash *= FnvPrime;
		}
	}

	bool IsSafeCollisionFilename(const FString& Filename)
	{
		if (Filename.IsEmpty()
			|| Filename == TEXT(".")
			|| Filename == TEXT("..")
			|| Filename != FPaths::GetCleanFilename(Filename)
			|| FPaths::IsRelative(Filename) == false)
		{
			return false;
		}
		for (const TCHAR Character : Filename)
		{
			if (!FChar::IsAlnum(Character)
				&& Character != TEXT('_')
				&& Character != TEXT('-')
				&& Character != TEXT('.'))
			{
				return false;
			}
		}
		return true;
	}

	bool ValidateCollisionFiles(
		const TArray<FString>& CollisionFiles,
		FString& OutError)
	{
		if (CollisionFiles.IsEmpty() || CollisionFiles.Num() > 32)
		{
			OutError = TEXT("collision_files must contain between 1 and 32 files");
			return false;
		}

		TSet<FString> Seen;
		bool bHasGroundSurface = false;
		for (const FString& Filename : CollisionFiles)
		{
			if (!IsSafeCollisionFilename(Filename))
			{
				OutError = FString::Printf(
					TEXT("collision file must be a safe relative filename: %s"),
					*Filename);
				return false;
			}
			if (Seen.Contains(Filename))
			{
				OutError = FString::Printf(
					TEXT("duplicate collision file: %s"),
					*Filename);
				return false;
			}
			Seen.Add(Filename);
			bHasGroundSurface |= Filename == TEXT("ground_surface.csv");
		}
		if (!bHasGroundSurface)
		{
			OutError = TEXT("collision_files must include ground_surface.csv");
			return false;
		}
		return true;
	}

	bool HashFile(uint64& Hash, const FString& Path, FString& OutError)
	{
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path));
		if (!Reader)
		{
			OutError = FString::Printf(
				TEXT("collision payload not found: %s"),
				*Path);
			return false;
		}

		const int64 Size = Reader->TotalSize();
		if (Size < 0 || Size > MaxCollisionPayloadBytes)
		{
			OutError = FString::Printf(
				TEXT("collision payload exceeds 512 MiB: %s"),
				*Path);
			return false;
		}

		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(64 * 1024);
		int64 Remaining = Size;
		while (Remaining > 0)
		{
			const int64 Count = FMath::Min<int64>(Remaining, Buffer.Num());
			Reader->Serialize(Buffer.GetData(), Count);
			if (Reader->IsError())
			{
				OutError = FString::Printf(
					TEXT("failed to read collision payload: %s"),
					*Path);
				return false;
			}
			HashBytes(Hash, Buffer.GetData(), Count);
			Remaining -= Count;
		}
		return true;
	}

	bool IsValidChecksumText(const FString& Checksum)
	{
		if (Checksum.Len() != 24 || !Checksum.StartsWith(TEXT("fnv1a64:")))
		{
			return false;
		}
		for (int32 Index = 8; Index < Checksum.Len(); ++Index)
		{
			const TCHAR Character = Checksum[Index];
			if (!FChar::IsDigit(Character)
				&& !(Character >= TEXT('a') && Character <= TEXT('f')))
			{
				return false;
			}
		}
		return true;
	}

}

FString ResolvePackageDirectory(const FString& PackageDirectory, const FString& ProjectDirectory)
{
	// Editor -game can return a ProjectDir relative to the executable. The
	// two-argument conversion does not make a relative base absolute for us.
	// Without this first conversion, every reconnect prepends ProjectDir again.
	const FString AbsoluteProjectDirectory = FPaths::ConvertRelativePathToFull(
		ProjectDirectory.IsEmpty() ? FPaths::ProjectDir() : ProjectDirectory);
	FString Resolved = FPaths::ConvertRelativePathToFull(AbsoluteProjectDirectory, PackageDirectory);
	FPaths::NormalizeDirectoryName(Resolved);
	return Resolved;
}

bool ComputeCollisionChecksum(
	const FString& PackageDirectory,
	const TArray<FString>& CollisionFiles,
	FString& OutChecksum,
	FString& OutError)
{
	OutChecksum.Reset();
	OutError.Reset();
	if (!ValidateCollisionFiles(CollisionFiles, OutError))
	{
		return false;
	}

	const FString ResolvedDirectory = ResolvePackageDirectory(PackageDirectory);
	uint64 Hash = FnvOffsetBasis;
	constexpr uint8 Separator = 0;
	for (const FString& Filename : CollisionFiles)
	{
		FTCHARToUTF8 Utf8Filename(*Filename);
		HashBytes(
			Hash,
			reinterpret_cast<const uint8*>(Utf8Filename.Get()),
			Utf8Filename.Length());
		HashBytes(Hash, &Separator, 1);
		if (!HashFile(Hash, FPaths::Combine(ResolvedDirectory, Filename), OutError))
		{
			return false;
		}
		HashBytes(Hash, &Separator, 1);
	}

	OutChecksum = FString::Printf(
		TEXT("fnv1a64:%016llx"),
		static_cast<unsigned long long>(Hash));
	return true;
}

bool LoadAndVerifyManifest(
	const FString& PackageDirectory,
	FManifest& OutManifest,
	FString& OutError)
{
	OutManifest = {};
	OutError.Reset();
	const FString ResolvedDirectory = ResolvePackageDirectory(PackageDirectory);
	const FString ManifestPath = FPaths::Combine(ResolvedDirectory, TEXT("manifest.cfg"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ManifestPath))
	{
		OutError = FString::Printf(TEXT("MapPackage manifest not found: %s"), *ManifestPath);
		return false;
	}

	TMap<FString, FString> Values;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		FString Line = Lines[Index];
		int32 CommentIndex = INDEX_NONE;
		if (Line.FindChar(TEXT('#'), CommentIndex))
		{
			Line.LeftInline(CommentIndex);
		}
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty())
		{
			continue;
		}

		FString Key;
		FString Value;
		if (!Line.Split(TEXT("="), &Key, &Value))
		{
			OutError = FString::Printf(
				TEXT("%s:%d must use key=value"),
				*ManifestPath,
				Index + 1);
			return false;
		}
		Key.TrimStartAndEndInline();
		Value.TrimStartAndEndInline();
		if (Key.IsEmpty() || Value.IsEmpty() || Values.Contains(Key))
		{
			OutError = FString::Printf(
				TEXT("%s:%d has an empty or duplicate key"),
				*ManifestPath,
				Index + 1);
			return false;
		}
		Values.Add(Key, Value);
	}

	const TSet<FString> ExpectedKeys{
		TEXT("format_version"),
		TEXT("map_id"),
		TEXT("coordinate_frame"),
		TEXT("collision_files"),
		TEXT("collision_checksum")};
	for (const TPair<FString, FString>& Pair : Values)
	{
		if (!ExpectedKeys.Contains(Pair.Key))
		{
			OutError = FString::Printf(TEXT("unknown manifest key: %s"), *Pair.Key);
			return false;
		}
	}
	for (const FString& Key : ExpectedKeys)
	{
		if (!Values.Contains(Key))
		{
			OutError = FString::Printf(TEXT("missing manifest key: %s"), *Key);
			return false;
		}
	}
	if (Values[TEXT("format_version")] != FString::FromInt(ManifestFormatVersion))
	{
		OutError = FString::Printf(
			TEXT("unsupported manifest format_version: %s"),
			*Values[TEXT("format_version")]);
		return false;
	}
	if (Values[TEXT("map_id")].Len() > 128)
	{
		OutError = TEXT("manifest map_id exceeds 128 characters");
		return false;
	}
	if (Values[TEXT("coordinate_frame")] != TEXT("map_enu"))
	{
		OutError = TEXT("manifest coordinate_frame must be map_enu");
		return false;
	}

	TArray<FString> CollisionFiles;
	Values[TEXT("collision_files")].ParseIntoArray(CollisionFiles, TEXT(","), false);
	for (FString& Filename : CollisionFiles)
	{
		Filename.TrimStartAndEndInline();
	}
	if (!ValidateCollisionFiles(CollisionFiles, OutError))
	{
		return false;
	}
	const FString ExpectedChecksum = Values[TEXT("collision_checksum")];
	if (!IsValidChecksumText(ExpectedChecksum))
	{
		OutError = FString::Printf(
			TEXT("invalid collision_checksum: %s"),
			*ExpectedChecksum);
		return false;
	}
	FString ActualChecksum;
	if (!ComputeCollisionChecksum(
		ResolvedDirectory,
		CollisionFiles,
		ActualChecksum,
		OutError))
	{
		return false;
	}
	if (ExpectedChecksum != ActualChecksum)
	{
		OutError = FString::Printf(
			TEXT("collision checksum mismatch: manifest=%s computed=%s"),
			*ExpectedChecksum,
			*ActualChecksum);
		return false;
	}

	OutManifest.PackageDirectory = ResolvedDirectory;
	OutManifest.MapId = Values[TEXT("map_id")];
	OutManifest.CollisionFiles = MoveTemp(CollisionFiles);
	OutManifest.CollisionChecksum = MoveTemp(ActualChecksum);
	return true;
}

bool WriteManifestLast(
	const FString& PackageDirectory,
	const FString& MapId,
	const TArray<FString>& CollisionFiles,
	FString& OutChecksum,
	FString& OutError)
{
	OutChecksum.Reset();
	OutError.Reset();
	if (MapId.IsEmpty() || MapId.Len() > 128
		|| MapId.Contains(TEXT("\r")) || MapId.Contains(TEXT("\n")))
	{
		OutError = TEXT("MapPackage map_id is empty or invalid");
		return false;
	}
	const FString ResolvedDirectory = ResolvePackageDirectory(PackageDirectory);
	if (!ComputeCollisionChecksum(
		ResolvedDirectory,
		CollisionFiles,
		OutChecksum,
		OutError))
	{
		return false;
	}

	const FString Manifest = FString::Printf(
		TEXT("# Generated by SimCore Ground Collision Exporter.\n")
		TEXT("format_version=%d\n")
		TEXT("map_id=%s\n")
		TEXT("coordinate_frame=map_enu\n")
		TEXT("collision_files=%s\n")
		TEXT("collision_checksum=%s\n"),
		ManifestFormatVersion,
		*MapId,
		*FString::Join(CollisionFiles, TEXT(",")),
		*OutChecksum);
	const FString OutputPath = FPaths::Combine(ResolvedDirectory, TEXT("manifest.cfg"));
	const FString TemporaryPath = OutputPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(
		Manifest,
		*TemporaryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(
			TEXT("failed to write temporary manifest: %s"),
			*TemporaryPath);
		return false;
	}
	if (!IFileManager::Get().Move(*OutputPath, *TemporaryPath, true, true))
	{
		IFileManager::Get().Delete(*TemporaryPath, false, true);
		OutError = FString::Printf(TEXT("failed to replace manifest: %s"), *OutputPath);
		return false;
	}
	return true;
}
}
