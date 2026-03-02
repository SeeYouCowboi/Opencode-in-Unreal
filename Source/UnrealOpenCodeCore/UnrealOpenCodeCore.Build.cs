// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealOpenCodeCore : ModuleRules
{
	public UnrealOpenCodeCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Sockets",
				"Networking",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Json",
				"JsonUtilities",
			}
		);
	}
}
