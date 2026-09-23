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
		// SensorRig reads JSON through FFileHelper, not Unreal's INI config system.
		RuntimeDependencies.Add("$(ProjectDir)/Config/sensors.json", StagedFileType.NonUFS);
		// Client and server negotiate the exact bytes of this shared catalog.
		foreach (string File in new string[]
		{
			"catalog.json", "profiles/sedan.json", "profiles/compact.json",
			"profiles/truck.json", "profiles/motorcycle.json"
		})
		{
			RuntimeDependencies.Add("$(ProjectDir)/Config/VehicleCatalog/" + File, StagedFileType.NonUFS);
		}
		// Optional axle modules are loose JSON, read before connecting to the server.
		RuntimeDependencies.Add("$(ProjectDir)/Config/VehicleCatalog/parts/...", StagedFileType.NonUFS);
		RuntimeDependencies.Add("$(ProjectDir)/Config/VehicleCatalog/loadouts/...", StagedFileType.NonUFS);
	}
}
