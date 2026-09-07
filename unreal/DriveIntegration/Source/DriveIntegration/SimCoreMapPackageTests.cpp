#include "SimCoreMapPackage.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCoreMapPackageReconnectPathTest,
	"DriveIntegration.MapPackage.ReconnectPathIsAbsoluteAndIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCoreMapPackageReconnectPathTest::RunTest(const FString& Parameters)
{
	const FString AbsoluteProject = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString RelativeProject = AbsoluteProject;
	bool bOk = TestTrue(TEXT("project made relative to engine executable for regression"),
		FPaths::MakePathRelativeTo(RelativeProject, FPlatformProcess::BaseDir()));
	bOk &= TestTrue(TEXT("exercise the -game relative ProjectDir case"), FPaths::IsRelative(RelativeProject));
	const FString Package = TEXT("../../map_packages/signal_city_v2");
	const FString Expected = SimCoreMapPackage::ResolvePackageDirectory(Package, AbsoluteProject);
	FString Resolved = SimCoreMapPackage::ResolvePackageDirectory(Package, RelativeProject);
	bOk &= TestFalse(TEXT("resolved package is absolute"), FPaths::IsRelative(Resolved));
	bOk &= TestEqual(TEXT("relative and absolute project roots identify the same package"), Resolved, Expected);
	for (int32 Attempt = 0; Attempt < 10; ++Attempt)
	{
		Resolved = SimCoreMapPackage::ResolvePackageDirectory(Resolved, RelativeProject);
		bOk &= TestEqual(TEXT("reconnect never appends another project prefix"), Resolved, Expected);
	}
	FString FirstChecksum;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		SimCoreMapPackage::FManifest Manifest;
		FString Error;
		const bool bVerified = SimCoreMapPackage::LoadAndVerifyManifest(Resolved, Manifest, Error);
		if (!TestTrue(FString::Printf(TEXT("repeated manifest verification: %s"), *Error),
			bVerified)) return false;
		if (Attempt == 0) FirstChecksum = Manifest.CollisionChecksum;
		bOk &= TestEqual(TEXT("reconnect preserves collision identity"), Manifest.CollisionChecksum, FirstChecksum);
		bOk &= TestEqual(TEXT("manifest returns the canonical absolute path"), Manifest.PackageDirectory, Expected);
		Resolved = Manifest.PackageDirectory;
	}
	return bOk;
}
#endif
