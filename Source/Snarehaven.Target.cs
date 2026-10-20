using UnrealBuildTool;

public class SnarehavenTarget : TargetRules
{
	public SnarehavenTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Snarehaven");
	}
}
