// m4v: the light and colour grade per area. Content/Look/Kit/areas.json (staged with the package as a loose file) gives
// each area (the ids of design/look-anime/frames.json, plus "route", "indoor", "gate", "cave", "gym"...) M_ToonPost's
// grade and toon values, the sun's rotation, colour and intensity, the sky light, the painted sky's two colours, the
// haze (the toon pass's fog) and the volume's vignette and bloom; "maps" says which area each Game Boy map is (first
// match wins, a trailing * matches any end) and "stages" which area each battle stage is.
// AYellowWorld::SetFocus (the place that already switches what is drawn per map, as UPYRField switches the music) tells
// this subsystem when Red's map changes; a fight on a stage switches to the stage's area. Outdoor neighbours blend over
// blend_seconds; through a door or into a battle the look snaps (the fade or the flash covers it).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PYRAreaLook.generated.h"

class AYellowWorld;

struct POKEMONYELLOWREIMAGINE_API FPYRLookValues
{
    TMap<FName, float> Scalars;          // M_ToonPost scalar parameters
    TMap<FName, FLinearColor> Vectors;   // M_ToonPost vector parameters
    FRotator SunRotation = FRotator(-52.f, -125.f, 0.f);
    FLinearColor SunColor = FLinearColor(1.f, .95f, .86f);
    float SunIntensity = 3.14159f;
    float SkyLight = 1.f;
    FLinearColor SkyLightColor = FLinearColor::White;
    FLinearColor SkyTop = FLinearColor::Black, SkyHorizon = FLinearColor::Black;   // linear; black: M_Sky's own
    float SkyPainted = 0.f;                       // M_Sky's SkyTexAmount: the painted sky over the gradient
    FLinearColor SkyPaintedTint = FLinearColor::White;
    float SkyPaintedOffset = 0.f, SkyPaintedScale = 1.f;
    FString SkyTexture;                           // M_Sky's SkyTex for this area (empty: the painted sky)
    float Vignette = .12f, Bloom = .15f;
    void LerpTo(const FPYRLookValues& To, float Alpha);
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRAreaLook : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UPYRAreaLook* Get(const UObject* Ctx);
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPYRAreaLook, STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }
    virtual bool IsTickableWhenPaused() const override { return true; }

    // AYellowWorld::SetFocus: Red's map changed.
    void OnFocus(FName Map, bool bWasOutdoor, bool bIsOutdoor);
    // The area a map is, and a battle stage's ("grass", "forest", "cave", "indoor", "pewter-gym", "cerulean-gym").
    FName AreaForMap(FName Map) const;
    FName AreaForStage(const FString& Stage) const;
    // The table's values for an area (with its base and the defaults under it); false for an unknown area.
    bool Values(FName Area, FPYRLookValues& Out) const;
    bool HasArea(FName Area) const { return Areas.Contains(Area); }
    TArray<FName> AreaIds() const;

    // What is showing: the area wanted now, and true once the blend has reached it.
    FName GetArea() const { return Target; }
    bool IsSettled() const { return Blend >= 1.f && !Target.IsNone(); }
    const FPYRLookValues& Current() const { return Now; }
    // Tests and shots: hold this area whatever Red's map is (None lets go); bSnap jumps straight to it.
    void Force(FName Area, bool bSnap = true);
    // The table loaded (areas.json found and parsed).
    bool IsLoaded() const { return bLoaded; }
    FString LoadError() const { return Error; }
    static FString TablePath();

private:
    void Load();
    void Want(FName Area, bool bSnap);
    void Apply(AYellowWorld* World, bool bAll);
    bool bLoaded = false, bTriedLoad = false;
    FString Error;
    TMap<FName, TSharedPtr<class FJsonObject>> Areas;
    TSharedPtr<class FJsonObject> Defaults;
    TArray<TPair<FString, FName>> MapRules;
    TMap<FString, FName> StageAreas;
    float BlendSeconds = 1.2f;

    FName Target, Forced, FocusArea;
    FPYRLookValues From, To, Now;
    float Blend = 1.f;
    bool bInBattle = false;
    bool bDirty = false;
};
