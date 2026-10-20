using UnrealBuildTool;

public class SnarehavenEditorTarget : TargetRules
{
	public SnarehavenEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Snarehaven");
	}
}
