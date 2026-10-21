// Anime-style battle effects built from flat-colour primitives (unlit /Game/BattleFx/M_FxFlat when present):
// jagged bolts with white cores, speed lines, dust puffs, wind swirls, threads, stat arrows, ring pulses and
// shockwaves, needles, claw slashes, psychic waves, impact stars. Plus spark bursts and a camera-shake modifier.
#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "GameFramework/Actor.h"
#include "PYRBattleFx.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;

namespace PYRFx
{
    // /Engine/BasicShapes/<Shape> (Sphere, Cube, Cylinder, Cone, Plane).
    POKEMONYELLOWREIMAGINE_API UStaticMesh* Shape(const TCHAR* Name);
    // A no-collision primitive with its own colour, attached to Parent (or the owner's root). bFlat uses the
    // unlit effect material (flat anime colour, alpha from the colour) when it exists.
    POKEMONYELLOWREIMAGINE_API UStaticMeshComponent* AddPrimitive(AActor* Owner, USceneComponent* Parent, const TCHAR* ShapeName,
        FVector Loc, FVector Scale, FLinearColor Colour, FRotator Rot = FRotator::ZeroRotator, bool bFlat = false);
    POKEMONYELLOWREIMAGINE_API void SetColour(UStaticMeshComponent* Mesh, FLinearColor Colour);
    POKEMONYELLOWREIMAGINE_API FLinearColor Palette(const TCHAR* Hex);
}

enum class EPYRFx : uint8
{
    Bolt,         // Jagged yellow bolts with white cores between two points (Thundershock).
    CheekSpark,   // Little star sparks at the cheeks (Thundershock).
    SpeedLines,   // Parallel white streaks trailing a dash (Tackle, dodges).
    DustPuff,     // Puffs of dust on the ground.
    WindSwirl,    // A white wind swirl travelling along Dir (Gust).
    Threads,      // White threads along Dir (String Shot).
    StatUp,       // Rising arrows and a flash in the stat colour.
    StatDown,     // Falling arrows in the stat colour.
    RingPulse,    // A flat ring expanding to Size (Thunder Wave, Screech).
    Shockwave,    // A thick orange ground ring expanding to Size with dust (Bide).
    Needle,       // A purple needle with a trail (Poison Sting).
    Impact,       // Radial speed lines and flat graphic sparks (big hits).
    Claw,         // Three slash streaks (Scratch, Peck, Bite, Fury Attack...).
    PsychicWave,  // Pink concentric rings travelling along Dir (Confusion).
    SandSpray,    // A spray of sand grains in a cone (Sand-Attack).
    PowderCloud,  // A puffing powder cloud (Poison Powder).
    RockFall,     // A boulder dropping onto the point (Rock Throw).
    TailArc,      // A wide grey arc (Onix's tail sweep).
    Glint,        // A white star flash (Harden, Defense Curl, Double Team).
    // m3
    WaterJet,     // A blue jet of droplets with a white core trailing back from the head (Water Gun).
    Bubbles,      // A cluster of wobbling bubbles (Bubble, Bubblebeam, Acid in its own colour).
    Flame,        // Orange flame tongues with a yellow core (Ember).
    VineLash,     // A green vine lashing out along Dir with a leaf at the tip (Vine Whip).
    Notes,        // Music notes floating out in a ring (Sing).
    GasCloud,     // A billowing cloud rolling along Dir (Smog, Poison Gas, Smokescreen).
    Coins,        // Spinning gold coins scattering (Pay Day).
    Stars,        // Yellow six-point stars (Swift).
    Seeds,        // Seeds flying along Dir (Leech Seed).
    DrainOrbs,    // Green orbs flowing along Dir, from the target back to the user (Absorb, Leech Life, Leech Seed's sap).
    SleepZ,       // Z glyphs rising from a sleeper.
    DizzyStars,   // Little stars circling a confused head.
    HealSparkle,  // Rising green-white crosses (Recover, Potions).
    Coil,         // Rings squeezing around a bound Pokemon (Wrap, Bind, Clamp).
    Splash,       // Droplets thrown up and out (a surfacing swimmer, a fall into the water, Splash).
    Explosion,    // A white-orange blast with radial streaks (Selfdestruct).
    DirtMound,    // A moving mound of earth (Dig's tell).
    Crescent,     // A white crescent shock (Sonicboom).
    Tornado,      // A spinning funnel (Whirlwind).
    SoundWave,    // Arcs of sound travelling along Dir (Supersonic).
    Orb,          // A glowing orb with a white core (Confuse Ray).
    Ripple,       // Rings spreading on the water (a swimmer diving or moving underwater).
};

// One effect: a few flat pieces animated by kind for Life seconds, then gone.
UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRMoveFx : public AActor
{
    GENERATED_BODY()
public:
    APYRMoveFx();
    // At: where; Dir: travel or facing; Size: length or radius in cm.
    static APYRMoveFx* Spawn(UWorld* World, EPYRFx Kind, FVector At, FVector Dir, FLinearColor Colour, float Size, float Life);
    // Bolts: redraw between two points (a travelling Thundershock).
    void SetEnds(FVector From, FVector To);
    virtual void Tick(float Dt) override;
    EPYRFx Kind = EPYRFx::Impact;
    FName MoveId;  // The move that spawned it (tests count these).
    int32 VisiblePieces() const;
private:
    void Build();
    void BuildBolts();
    UStaticMeshComponent* Piece(const TCHAR* Shape, FVector Loc, FVector Scale, FLinearColor Colour, FRotator Rot = FRotator::ZeroRotator);
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
    TArray<FVector> BaseLoc, BaseScale, Vel;
    FVector From = FVector::ZeroVector, To = FVector::ZeroVector, Dir = FVector::ForwardVector;
    FLinearColor Colour = FLinearColor::White;
    float Size = 100, Life = .4f, Age = 0, Rejag = 0;
};

// A burst of flat pieces flying out and shrinking (sparks, feathers, red light, spray).
UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRFxBurst : public AActor
{
    GENERATED_BODY()
public:
    APYRFxBurst();
    static APYRFxBurst* Spawn(UWorld* World, FVector At, FLinearColor Colour, int32 Count, float Size, float Speed, float Life,
        const TCHAR* ShapeName = TEXT("Cube"), float Gravity = -600.f);
    virtual void Tick(float Dt) override;
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
    TArray<FVector> Velocities;
    TArray<FVector> BaseScales;
    float Age = 0, Life = .4f, Gravity = -600;
};

// Adds a decaying random offset to whatever the camera shows.
UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRShakeModifier : public UCameraModifier
{
    GENERATED_BODY()
public:
    void Kick(float Strength, float Seconds);
    virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
    float Strength = 0, Left = 0, Total = 0;
};
