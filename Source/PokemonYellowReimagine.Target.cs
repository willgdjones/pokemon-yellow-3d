using UnrealBuildTool;
public class PokemonYellowReimagineTarget : TargetRules
{
    public PokemonYellowReimagineTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("PokemonYellowReimagine");
    }
}
