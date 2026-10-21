// Pikachu on the road (tag PikachuFollower), as in Yellow: once Red has it, it walks one tile behind him, tile-true,
// on the tile he just left (UPYRField::Trail), across map connections, through doors (it warps with him) and down
// ledges, and it never goes into a Poke Ball. Talking to it (E facing it, or T) shows its face for its happiness and
// mood (pikachu.json). The battle manager hides it for a fight and puts it back afterwards; while hidden it does
// nothing.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PYRPikachuFollower.generated.h"

class USkeletalMeshComponent;

UENUM()
enum class EPYRPikachuMode : uint8 { Waiting, Follow, Script };

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRPikachuFollower : public ACharacter
{
    GENERATED_BODY()
public:
    APYRPikachuFollower();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    static APYRPikachuFollower* Find(const UObject* Ctx);

    EPYRPikachuMode GetMode() const { return Mode; }
    // Scripted beats: the follower stops until Release().
    void BeginScript();
    void Release();
    // Hop along an arc to a world point over Seconds (scripted).
    void HopTo(const FVector& Ground, float Seconds);
    // Onto the tile behind Red (after a warp, a battle, a placement).
    void SnapBehindRed();
    void TeleportBehindRed() { SnapBehindRed(); }
    void Worried(float Seconds);
    bool IsWorried() const;
    // 'refuse-ball' (the lab: it turns away and zaps the ball), 'happy', 'worried'.
    void PlayMood(FName Mood);
    FName GetLastMood() const { return LastMood; }
    // Talk to it: the face for its mood (Menus/PYRScreens ShowPikachu). False when Red isn't facing it.
    bool TryTalk();
    // The Kanto tile it stands on, and how many tiles it has walked (tests).
    FIntPoint Tile() const;
    int32 TilesWalked = 0;

    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Head;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> EarL;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> EarR;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Tail;

private:
    void BuildBody();
    ACharacter* Red() const;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> Skin;
    EPYRPikachuMode Mode = EPYRPikachuMode::Waiting;
    FVector HopFrom = FVector::ZeroVector, HopTo_ = FVector::ZeroVector;
    double HopStart = -1, HopEnd = -1, WorriedUntil = 0, SwatUntil = 0;
    float Phase = 0.f;
    FName LastMood;
    FIntPoint LastTile = FIntPoint(MIN_int32, MIN_int32);
    FVector Target = FVector::ZeroVector;
};
