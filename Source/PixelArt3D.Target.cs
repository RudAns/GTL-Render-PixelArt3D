using UnrealBuildTool;

public class PixelArt3DTarget : TargetRules
{
	public PixelArt3DTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "PixelArtRender", "PixelArt3D" });
	}
}
