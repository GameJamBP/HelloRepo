// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HelloRepo : ModuleRules
{
	public HelloRepo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"HelloRepo",
			"HelloRepo/Variant_Platforming",
			"HelloRepo/Variant_Platforming/Animation",
			"HelloRepo/Variant_Combat",
			"HelloRepo/Variant_Combat/AI",
			"HelloRepo/Variant_Combat/Animation",
			"HelloRepo/Variant_Combat/Gameplay",
			"HelloRepo/Variant_Combat/Interfaces",
			"HelloRepo/Variant_Combat/UI",
			"HelloRepo/Variant_SideScrolling",
			"HelloRepo/Variant_SideScrolling/AI",
			"HelloRepo/Variant_SideScrolling/Gameplay",
			"HelloRepo/Variant_SideScrolling/Interfaces",
			"HelloRepo/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
