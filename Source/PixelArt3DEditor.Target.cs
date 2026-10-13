using UnrealBuildTool;

public class PixelArt3DEditorTarget : TargetRules
{
	public PixelArt3DEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "PixelArtRender", "PixelArt3D" });
	}
}
