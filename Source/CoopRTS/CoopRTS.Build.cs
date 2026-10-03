using UnrealBuildTool;

public class CoopRTS : ModuleRules
{
	public CoopRTS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;
		// Rules/ and Content/ subfolders include module-root headers and each other by module-relative path.
		PrivateIncludePaths.Add(ModuleDirectory);
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"AIModule", "NavigationSystem", "GameplayTasks",
			"OnlineSubsystem", "OnlineSubsystemUtils"
		});
		PrivateDependencyModuleNames.AddRange(new string[] { "CoreOnline", "Json", "SlateCore", "ApplicationCore" });
	}
}
