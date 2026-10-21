using UnrealBuildTool;
public class PokemonYellowReimagineEditorTarget : TargetRules
{
    public PokemonYellowReimagineEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("PokemonYellowReimagine");
    }
}
