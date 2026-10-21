using System.IO;
using UnrealBuildTool;
public class PokemonYellowReimagine : ModuleRules
{
    public PokemonYellowReimagine(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // System folders include each other from the module root: "Kanto/YellowWorld.h", "Party/PYRTrainerState.h".
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
        // Slate UI (the Game Boy screens, the minimap, the battle HUD), AI-driven bodies, effects.
        PublicDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "UMG", "AIModule", "Niagara" });
        // Roof prisms on the buildings.
        PublicDependencyModuleNames.Add("ProceduralMeshComponent");
        // The save slot writes versioned JSON; YellowData and YellowMusic are JSON too.
        PublicDependencyModuleNames.AddRange(new string[] { "Json", "JsonUtilities" });
        // m4: YellowData's PNGs (minimap, Town Map, Pikachu's faces) become textures at runtime.
        PrivateDependencyModuleNames.Add("ImageWrapper");
        PrivateDependencyModuleNames.Add("AudioExtensions");
        // m4s: the frame-rate test reads stat unit's thread and GPU times.
        PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });

        // m4: YellowData (Tools/yellowdata, from pret/pokeyellow) and YellowMusic (Tools/gbmusic) are read from disk at
        // runtime (FPaths::ProjectDir()), so the package carries them as loose files.
        RuntimeDependencies.Add("$(ProjectDir)/YellowData/...", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(ProjectDir)/YellowMusic/...", StagedFileType.NonUFS);
        // m4t: the phone controls' layout (Hud/PYRPhoneLayout), so the touch Run test reads the very button the page sends.
        RuntimeDependencies.Add("$(ProjectDir)/touch-layout.json", StagedFileType.NonUFS);
        // m4v: the anime look's tables (Content/Look/Kit/README.md): the area grades, the look frames, the frame-rate baseline,
        // the cast's hashes and m4u's HUD layout, read at runtime as loose files.
        RuntimeDependencies.Add("$(ProjectDir)/Content/Look/Kit/*.json", StagedFileType.NonUFS);
    }
}
