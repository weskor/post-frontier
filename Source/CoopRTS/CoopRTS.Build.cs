using UnrealBuildTool;

public class CoopRTS : ModuleRules
{
	public CoopRTS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"AIModule", "NavigationSystem", "GameplayTasks"
		});
		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "SlateCore", "ApplicationCore" });
	}
}
