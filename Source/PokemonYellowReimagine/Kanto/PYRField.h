// M4: the overworld as the Game Boy runs it, over the free 3D walking.
// - Tracks Red's tile, the map he is in (it changes the moment his tile falls in another map: connections are seamless)
//   and the last outdoor map (interiors' LAST_MAP warps lead back to it).
// - Warps: stepping onto a warp tile (d) fires it: a 0.3 s Game Boy fade, Red and Pikachu move to the paired warp in
//   the same level (no load), and outside a door Red steps one tile down as on the Game Boy. Standing on a warp at a
//   room's edge and pushing out (a mat, a cave mouth) fires it too.
// - Spawns every NPC, sign and item ball on its tile from the map JSON, and answers E (talk, read, take, hidden items).
// - Everyone else hooks in through OnTileChanged, OnMapChanged and OnWarped: encounters, trainers' sight, the scripts,
//   the minimap and the music.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Yellow/YellowData.h"
#include "PYRField.generated.h"

class AYellowWorld;
class APYRPerson;
class APYRSignpost;
class APYRItemBall;
class APokemonYellowReimagineCharacter;

DECLARE_MULTICAST_DELEGATE_TwoParams(FPYRTileChanged, FName /*Map*/, FIntPoint /*Local*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FPYRMapChanged, FName /*From*/, FName /*To*/);

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRField : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UPYRField* Get(const UObject* Ctx);
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPYRField, STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

    // The game mode calls this once the world is built: spawns every map's people, signs and item balls.
    void Start(AYellowWorld* InWorld);
    AYellowWorld* GetYellowWorld() const { return World.Get(); }
    APokemonYellowReimagineCharacter* Red() const;

    // ---- Where Red is
    FName CurrentMap;
    FIntPoint CurrentTile = FIntPoint::ZeroValue;   // local to CurrentMap
    FIntPoint KantoTile = FIntPoint::ZeroValue;
    FName LastOutdoor;
    EYDir Facing = EYDir::Down;
    // The tiles Red walked, newest last (Pikachu follows this trail).
    TArray<FIntPoint> Trail;
    const FYMap* GetMap() const;
    FPYRTileChanged OnTileChanged;
    FPYRMapChanged OnMapChanged;
    FSimpleMulticastDelegate OnWarped;
    int32 TileChanges = 0, MapChanges = 0, WarpsTaken = 0;

    // ---- Moving Red (no warp fires on the arrival tile until he leaves it)
    bool PlaceRed(FName Map, int32 X, int32 Y, EYDir Face);
    // Go through warp WarpIndex of Map to its pair (the fade, then Red and Pikachu move). False when it leads nowhere.
    bool TakeWarp(FName Map, int32 WarpIndex, bool bFade = true);
    bool IsWarping() const { return WarpPhase != 0; }
    // The Game Boy's step off a door tile after arriving by warp WarpIndex of M (the tile below is free and walkable).
    bool ShouldStepOffDoor(const FYMap& M, int32 WarpIndex) const;
    // A warp that leads out of Pallet Town to Cerulean (Route 23, Diglett's Cave, Cerulean Cave): Red walks back off
    // it, as Route 23's guards walk the player back, told why in Yellow's words where Yellow has some (Route 23's
    // guards, the man at Cerulean Cave). The text shown, for tests ("" when there is none, or the warp isn't blocked).
    FString BlockedWarpText(FName Map, int32 WarpIndex) const;
    // The warp leads past the range (it is blocked).
    bool IsOutOfRange(FName Map, int32 WarpIndex) const;
    int32 WarpsBlocked = 0;
    // Fade the screen to black (Alpha 1) and back; used by warps, the battle flash and scripts.
    void Fade(float Alpha, float Seconds, const FLinearColor& Colour = FLinearColor::Black);
    float GetFade() const { return FadeAlpha; }

    // ---- Locks: while any is held Red doesn't move (menus, text, scripts, battles).
    void Lock(FName Why);
    void Unlock(FName Why);
    bool IsLocked() const { return Locks.Num() > 0; }
    bool IsLockedBy(FName Why) const { return Locks.Contains(Why); }
    FString LockText() const { return Locks.Num() ? FString::JoinBy(Locks, TEXT(","), [](const FName& N) { return N.ToString(); }) : FString(TEXT("none")); }

    // ---- E: talk to the person Red faces (within 1.2 m, or across a counter), read a sign, take an item ball, find a
    // hidden item on his tile or the one he faces. True when something answered.
    bool Interact();
    // The facing tile (Kanto).
    FIntPoint FacingTile() const;

    // ---- Objects
    TArray<TWeakObjectPtr<APYRPerson>> People;
    TArray<TWeakObjectPtr<APYRSignpost>> Signs;
    TArray<TWeakObjectPtr<APYRItemBall>> Balls;
    APYRPerson* FindPerson(FName Map, const FString& Id) const;
    APYRPerson* FindPersonBySprite(FName Map, const FString& SpritePart) const;
    // A person, Red or Pikachu standing on the tile (Ignore excluded).
    bool IsOccupied(FIntPoint Kanto, const AActor* Ignore = nullptr) const;
    // Walkable for a person's step: not solid, on a map, nobody there, not a warp.
    bool CanStep(FIntPoint Kanto, const AActor* Who) const;
    // Give an item with Yellow's words and the get-item jingle ("<PLAYER> found POTION!").
    // TextId: the Yellow line to say instead ("GotMapText"...), its {str:wStringBuffer} filled with the item's name.
    void GiveItem(FName Item, int32 Count, const FString& Verb, TFunction<void()> OnDone = nullptr, const FString& TextId = FString());
    // A hidden item at a Kanto tile (the map's hidden list), if it is still there.
    bool HiddenAt(FIntPoint Kanto, FName& OutMap, int32& OutIndex) const;

private:
    TWeakObjectPtr<AYellowWorld> World;
    TSet<FName> Locks;
    bool bStarted = false, bArmed = true;
    FIntPoint ArrivalTile = FIntPoint(MIN_int32, MIN_int32);
    float EdgePush = 0.f;
    // Warp sequence: 1 fading out, 2 fading in.
    int32 WarpPhase = 0;
    double WarpClock = 0;
    FName WarpMap;
    int32 WarpIndex = 0;
    bool bAutoStep = false;
    float FadeAlpha = 0.f, FadeTarget = 0.f, FadeRate = 0.f;
    FLinearColor FadeColour = FLinearColor::Black;
    void SpawnObjects();
    void UpdateTile();
    void TickWarp(float Dt);
    void Arrive(FName Map, int32 Warp);
    void ApplyFade();
    bool TryHidden(FIntPoint Kanto);
    void BlockWarp(FName Map, int32 WarpIndex);
};
