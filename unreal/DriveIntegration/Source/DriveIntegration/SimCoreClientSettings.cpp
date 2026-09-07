#include "SimCoreClientSettings.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace SimCoreClientSettings
{
namespace
{
bool ReadScalar(FString& Value, FString& Error)
{
	Value.TrimStartAndEndInline();
	if (Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")))
	{
		Value = Value.Mid(1, Value.Len() - 2);
	}
	if (Value.IsEmpty() || Value.Len() > 2048 || Value.Contains(TEXT("\"")))
	{
		Error = TEXT("Configuration values must be nonempty, with balanced optional quotes (max 2048 characters)");
		return false;
	}
	for (const TCHAR Character : Value)
	{
		if (Character < TEXT(' '))
		{
			Error = TEXT("Configuration values cannot contain control characters");
			return false;
		}
	}
	return true;
}
}

bool NormalizeServerUrl(const FString& Value, FString& OutUrl, FString& OutError)
{
	OutError.Reset();
	FString Url = Value;
	if (!ReadScalar(Url, OutError)) return false;
	if (!Url.StartsWith(TEXT("ws://"), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("ServerUrl must use ws:// with a loopback host and explicit port");
		return false;
	}
	FString Authority = Url.Mid(5);
	if (Authority.EndsWith(TEXT("/"))) Authority.LeftChopInline(1);
	FString Host, PortText;
	if (!Authority.Split(TEXT(":"), &Host, &PortText)
		|| (!Host.Equals(TEXT("127.0.0.1")) && !Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase))
		|| PortText.IsEmpty() || PortText.Len() > 5)
	{
		OutError = TEXT("ServerUrl must be ws://127.0.0.1:<port>/ or ws://localhost:<port>/; remote hosts, credentials and URL paths are unsupported");
		return false;
	}
	int32 Port = 0;
	for (const TCHAR Digit : PortText)
	{
		if (Digit < TEXT('0') || Digit > TEXT('9'))
		{
			OutError = TEXT("ServerUrl port must be an integer from 1 to 65535, without query or fragment");
			return false;
		}
		Port = Port * 10 + Digit - TEXT('0');
	}
	if (Port < 1 || Port > 65535)
	{
		OutError = TEXT("ServerUrl port must be between 1 and 65535");
		return false;
	}
	// Explicit URI avoids the Windows WebSocket backend's empty-path handshake.
	OutUrl = FString::Printf(TEXT("ws://%s:%d/"), *Host.ToLower(), Port);
	return true;
}

bool ParseConfig(const FString& Text, const FString& ConfigDirectory,
	FSettings& InOutSettings, FString& OutError)
{
	OutError.Reset();
	FSettings Candidate = InOutSettings;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	TSet<FString> Keys;
	bool bSectionSeen = false;
	for (FString Line : Lines)
	{
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty() || Line.StartsWith(TEXT(";")) || Line.StartsWith(TEXT("#"))) continue;
		if (Line == TEXT("[SimCoreClient]") && !bSectionSeen)
		{
			bSectionSeen = true;
			continue;
		}
		FString Key, Value;
		if (!bSectionSeen || !Line.Split(TEXT("="), &Key, &Value))
		{
			OutError = TEXT("Expected one [SimCoreClient] section containing Key=Value entries");
			return false;
		}
		Key.TrimStartAndEndInline();
		if ((Key != TEXT("ServerUrl") && Key != TEXT("MapPackageDirectory")) || Keys.Contains(Key))
		{
			OutError = TEXT("Unknown or duplicate client configuration key");
			return false;
		}
		Keys.Add(Key);
		if (!ReadScalar(Value, OutError)) return false;
		if (Key == TEXT("ServerUrl"))
		{
			if (!NormalizeServerUrl(Value, Candidate.ServerUrl, OutError)) return false;
		}
		else
		{
			Candidate.MapPackageDirectory = FPaths::ConvertRelativePathToFull(ConfigDirectory, Value);
		}
	}
	if (!bSectionSeen || !Keys.Contains(TEXT("ServerUrl")))
	{
		OutError = TEXT("Client config requires [SimCoreClient] and ServerUrl");
		return false;
	}
	InOutSettings = MoveTemp(Candidate);
	return true;
}

bool Load(const FSettings& Defaults, const FString& CommandLine,
	const FString& ProjectDirectory, FSettings& OutSettings, FString& OutError)
{
	OutError.Reset();
	FString ConfigPath = TEXT("SimCoreClient.ini"), UrlOverride;
	TSet<FString> Options;
	const TCHAR* Cursor = *CommandLine;
	FString Token;
	while (FParse::Token(Cursor, Token, false))
	{
		FString Key, Value;
		if (!Token.Split(TEXT("="), &Key, &Value)) Key = Token;
		Key.ToLowerInline();
		if (Key != TEXT("-simcoreclientconfig") && Key != TEXT("-simcoreserverurl")) continue;
		if (Options.Contains(Key))
		{
			OutError = TEXT("Duplicate SimCore client command-line option");
			return false;
		}
		Options.Add(Key);
		if (!ReadScalar(Value, OutError)) return false;
		if (Key == TEXT("-simcoreclientconfig")) ConfigPath = Value;
		else UrlOverride = Value;
	}
	ConfigPath = FPaths::ConvertRelativePathToFull(
		FPaths::ConvertRelativePathToFull(ProjectDirectory), ConfigPath);
	FSettings Candidate = Defaults;
	Candidate.LoadedConfigPath.Reset();
	const int64 FileSize = IFileManager::Get().FileSize(*ConfigPath);
	if (FileSize < 0 && (Options.Contains(TEXT("-simcoreclientconfig"))
		|| IFileManager::Get().FileExists(*ConfigPath)
		|| IFileManager::Get().DirectoryExists(*ConfigPath)))
	{
		OutError = TEXT("The selected SimCore client config file does not exist or cannot be read");
		return false;
	}
	if (FileSize >= 0)
	{
		FString Text;
		if (FileSize > 16384 || !FFileHelper::LoadFileToString(Text, *ConfigPath))
		{
			OutError = TEXT("Cannot read SimCore client config (maximum 16 KiB)");
			return false;
		}
		if (!ParseConfig(Text, FPaths::GetPath(ConfigPath), Candidate, OutError)) return false;
		Candidate.LoadedConfigPath = ConfigPath;
	}
	if (!UrlOverride.IsEmpty()) Candidate.ServerUrl = UrlOverride;
	if (!NormalizeServerUrl(Candidate.ServerUrl, Candidate.ServerUrl, OutError)) return false;
	OutSettings = MoveTemp(Candidate);
	return true;
}
}
