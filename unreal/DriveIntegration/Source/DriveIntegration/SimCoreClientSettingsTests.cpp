#include "SimCoreClientSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SimCoreClientComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreClientSettingsUrlTest,
	"DriveIntegration.Configuration.LoopbackEndpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreClientSettingsUrlTest::RunTest(const FString& Parameters)
{
	FString Url, Error;
	bool Ok = TestTrue(TEXT("existing no-path default accepted"),
		SimCoreClientSettings::NormalizeServerUrl(TEXT("ws://127.0.0.1:9000"), Url, Error));
	Ok &= TestEqual(TEXT("root URI supplied for WebSocket handshake"), Url, TEXT("ws://127.0.0.1:9000/"));
	Ok &= TestTrue(TEXT("localhost and case normalized"),
		SimCoreClientSettings::NormalizeServerUrl(TEXT("WS://LOCALHOST:65535/"), Url, Error));
	Ok &= TestEqual(TEXT("canonical localhost"), Url, TEXT("ws://localhost:65535/"));
	for (const TCHAR* Invalid : {TEXT(""), TEXT("http://127.0.0.1:9000"),
		TEXT("ws://127.0.0.1:0"), TEXT("ws://127.0.0.1:65536"), TEXT("ws://127.0.0.1:-1"),
		TEXT("ws://127.0.0.1:9e3"), TEXT("ws://127.0.0.1"), TEXT("ws://localhost.evil:9000/"),
		TEXT("ws://192.168.1.2:9000/"), TEXT("ws://user:pass@127.0.0.1:9000/"),
		TEXT("ws://127.0.0.1:9000/path"), TEXT("ws://127.0.0.1:9000/?token=secret"),
		TEXT("ws://127.0.0.1:9000#fragment"), TEXT("ws://127.0.0.1:9000//")})
	{
		Ok &= TestFalse(TEXT("invalid/remote endpoint rejected"),
			SimCoreClientSettings::NormalizeServerUrl(Invalid, Url, Error));
		Ok &= TestFalse(TEXT("rejection has explanation"), Error.IsEmpty());
	}
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreClientSettingsPrecedenceTest,
	"DriveIntegration.Configuration.FilePrecedenceAndRelocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreClientSettingsPrecedenceTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()),
		TEXT("Automation/Client Settings ") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString DefaultFile = Root / TEXT("SimCoreClient.ini");
	const FString OtherFile = Root / TEXT("Explicit Config.ini");
	IFileManager::Get().MakeDirectory(*Root, true);
	SimCoreClientSettings::FSettings Defaults;
	Defaults.ServerUrl = TEXT("ws://127.0.0.1:9000");
	Defaults.MapPackageDirectory = TEXT("../../map_packages/signal_city_v2");
	SimCoreClientSettings::FSettings Actual;
	FString Error;
	bool Ok = TestTrue(TEXT("missing optional file preserves editor defaults"),
		SimCoreClientSettings::Load(Defaults, TEXT("-unattended"), Root, Actual, Error));
	Ok &= TestEqual(TEXT("serialized map remains unchanged"), Actual.MapPackageDirectory, Defaults.MapPackageDirectory);
	Ok &= TestTrue(TEXT("optional file not reported loaded"), Actual.LoadedConfigPath.IsEmpty());
	const FString Content = TEXT("; editable deployment config\n[SimCoreClient]\nServerUrl=ws://localhost:9001/\nMapPackageDirectory=\"map packages/signal_city_v2\"\n");
	Ok &= TestTrue(TEXT("write test config"), FFileHelper::SaveStringToFile(Content, *DefaultFile));
	Ok &= TestTrue(TEXT("file overrides serialized endpoint"),
		SimCoreClientSettings::Load(Defaults, TEXT(""), Root, Actual, Error));
	Ok &= TestEqual(TEXT("file endpoint used"), Actual.ServerUrl, TEXT("ws://localhost:9001/"));
	Ok &= TestEqual(TEXT("map path anchors to config, not working directory"), Actual.MapPackageDirectory,
		FPaths::ConvertRelativePathToFull(Root, TEXT("map packages/signal_city_v2")));
	Ok &= TestTrue(TEXT("CLI endpoint wins"), SimCoreClientSettings::Load(Defaults,
		TEXT("-SimCoreServerUrl=\"ws://127.0.0.1:9010/\""), Root, Actual, Error));
	Ok &= TestEqual(TEXT("CLI port used"), Actual.ServerUrl, TEXT("ws://127.0.0.1:9010/"));
	Ok &= TestTrue(TEXT("write explicit file"), FFileHelper::SaveStringToFile(Content, *OtherFile));
	Ok &= TestTrue(TEXT("quoted config path containing spaces"), SimCoreClientSettings::Load(Defaults,
		TEXT("-SimCoreClientConfig=\"Explicit Config.ini\""), Root, Actual, Error));
	Ok &= TestEqual(TEXT("explicit file is absolute"), Actual.LoadedConfigPath, OtherFile);
	for (const TCHAR* InvalidArgs : {TEXT("-SimCoreClientConfig=missing.ini"),
		TEXT("-SimCoreClientConfig="), TEXT("-SimCoreServerUrl"),
		TEXT("-SimCoreServerUrl=ws://localhost:9 -SimCoreServerUrl=ws://localhost:10"),
		TEXT("-SimCoreClientConfig=\"unclosed path")})
	{
		Ok &= TestFalse(TEXT("bad explicit arguments fail closed"),
			SimCoreClientSettings::Load(Defaults, InvalidArgs, Root, Actual, Error));
	}
	for (const TCHAR* Invalid : {TEXT(""), TEXT("ServerUrl=ws://localhost:9000"),
		TEXT("[SimCoreClient]\nServerUrl="), TEXT("[SimCoreClient]\nUnknown=value"),
		TEXT("[SimCoreClient]\nServerUrl=ws://localhost:9000\nServerUrl=ws://localhost:9001"),
		TEXT("[SimCoreClient]\nServerUrl=ws://localhost:9000\n[SimCoreClient]"),
		TEXT("[SimCoreClient]\nServerUrl=ws://localhost:9000\nMapPackageDirectory=")})
	{
		Actual = Defaults;
		Ok &= TestFalse(TEXT("malformed config rejected"), SimCoreClientSettings::ParseConfig(Invalid, Root, Actual, Error));
		Ok &= TestEqual(TEXT("failed parse leaves defaults untouched"), Actual.ServerUrl, Defaults.ServerUrl);
	}
	Ok &= TestTrue(TEXT("write invalid present file"),
		FFileHelper::SaveStringToFile(TEXT("[SimCoreClient]\nServerUrl=bad"), *DefaultFile));
	Ok &= TestFalse(TEXT("valid CLI does not mask malformed file"), SimCoreClientSettings::Load(Defaults,
		TEXT("-SimCoreServerUrl=ws://localhost:9000/"), Root, Actual, Error));
	IFileManager::Get().Delete(*DefaultFile);
	IFileManager::Get().Delete(*OtherFile);
	IFileManager::Get().DeleteDirectory(*Root, false, false);
	return Ok;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreClientSettingsFailureTest,
	"DriveIntegration.Configuration.InvalidConfigBlocksConnectionAndSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreClientSettingsFailureTest::RunTest(const FString& Parameters)
{
	const FString OriginalCommandLine = FCommandLine::Get();
	const FString MissingPath = FPaths::ConvertRelativePathToFull(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()),
		TEXT("missing-client-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ini"));
	FCommandLine::Set(*FString::Printf(TEXT("-SimCoreClientConfig=\"%s\""), *MissingPath));
	USimCoreClientComponent* Client = NewObject<USimCoreClientComponent>();
	AddExpectedError(TEXT("Client configuration rejected"), EAutomationExpectedErrorFlags::Contains, 2);
	Client->Connect();
	bool Ok = TestEqual(TEXT("configuration error fails before map/socket"),
		Client->ConnectionState, ESimCoreConnectionState::Incompatible);
	Ok &= TestFalse(TEXT("no socket opened"), Client->Socket.IsValid());
	Ok &= TestFalse(TEXT("automatic retry disabled"), Client->bAutoReconnectEnabled);
	Ok &= TestTrue(TEXT("HUD explains configuration failure"),
		Client->GetConnectionStatusText().Contains(TEXT("configuration error")));
	Client->SelectVehicleClass(SimCoreProtocol::ERuntimeVehicleClass::Truck);
	Ok &= TestEqual(TEXT("vehicle selection cannot bypass bad config"),
		Client->ConnectionState, ESimCoreConnectionState::Incompatible);
	Ok &= TestFalse(TEXT("selection did not open a socket"), Client->Socket.IsValid());
	FCommandLine::Set(*OriginalCommandLine);
	return Ok;
}
#endif
