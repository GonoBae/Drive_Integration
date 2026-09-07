// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DriveIntegration : ModuleRules
{
	public DriveIntegration(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"AudioMixer",
			"PhysicsCore",
			"ProceduralMeshComponent",
			"WebSockets"
		});
		PrivateDependencyModuleNames.Add("Json");
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });
	}
}
