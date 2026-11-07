using UnrealBuildTool;

public class DTHAnimationWizard : ModuleRules
{
	public DTHAnimationWizard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// The Pose Wizard's measured core: solve, bone map, retarget inverse, writer, Daz state format, body harness.
			"DTHPoseCore",
			"DeveloperSettings",
			"Json",
			"UnrealEd",
			"AssetRegistry",
			"Projects",
			"Slate",
			"SlateCore",
			"InputCore",
			"ToolMenus",
			"WorkspaceMenuStructure",
			"PropertyEditor",
			"ApplicationCore",
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
		});
	}
}
