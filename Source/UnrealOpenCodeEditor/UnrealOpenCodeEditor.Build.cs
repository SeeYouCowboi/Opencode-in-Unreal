// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealOpenCodeEditor : ModuleRules
{
	public UnrealOpenCodeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"EditorSubsystem",
				"UnrealEd",
				"UnrealOpenCodeCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"Json",
				"JsonUtilities",
				"AssetRegistry",
				"InputCore",
				"LevelEditor",
				"ToolMenus",
				"ApplicationCore",
				"DesktopPlatform",
				"HTTP",
			}
		);
	}
}
