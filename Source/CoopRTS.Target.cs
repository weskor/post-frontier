using UnrealBuildTool;

public class CoopRTSTarget : TargetRules
{
	public CoopRTSTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("CoopRTS");
	}
}
