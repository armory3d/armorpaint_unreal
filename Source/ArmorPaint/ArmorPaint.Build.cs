// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ArmorPaint : ModuleRules
{
	public ArmorPaint(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"InputCore",
			"EditorFramework",
			"UnrealEd",
			"ToolMenus",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"MeshDescription",
			"StaticMeshDescription",
			"DesktopPlatform",
			"AssetTools",
			"Settings",
			"MaterialEditor",
			"MaterialBaking",
			"ImageCore",
			"Json",
		});
	}
}
