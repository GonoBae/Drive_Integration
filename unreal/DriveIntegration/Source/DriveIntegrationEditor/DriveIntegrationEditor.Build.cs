using UnrealBuildTool;
using System.IO;

public class DriveIntegrationEditor : ModuleRules
{
	public DriveIntegrationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add(ModuleDirectory);
		// The existing runtime module keeps its exported headers in its root.
		PrivateIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "../DriveIntegration")));
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "UnrealEd", "AssetRegistry", "InputCore",
			"MaterialEditor", "MeshDescription", "StaticMeshDescription", "DriveIntegration"
		});
	}
}
