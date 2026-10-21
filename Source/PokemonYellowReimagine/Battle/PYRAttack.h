// One use of a move: an optional tell (a crouch, a glow or a ground shadow, 0.4-1.2 s), then a hit window whose
// shape comes from the move (bolt, needle, dash, dive, cone, falling circle, tail sweep, ring pulse, Bide's
// shockwave). Every move spawns its anime effect the moment it goes active. Hits are tested analytically against
// the opponent's capsule, so they behave the same under -NullRHI.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Party/PYRSpecies.h"
#include "PYRAttack.generated.h"

class APYRMonPawn;
class APYRMoveFx;
class UStaticMeshComponent;

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRAttack : public AActor
{
    GENERATED_BODY()
public:
    APYRAttack();
    // bTell: enemy attacks warn first for Move.TellSeconds (0.4-1.2 s).
    void Init(APYRMonPawn* InUser, const FPYRMoveInfo& InMove, bool bTell);
    virtual void Tick(float Dt) override;

    enum class EPhase : uint8 { Tell, Active, Done };
    EPhase Phase = EPhase::Tell;
    FPYRMoveInfo Move;
    TWeakObjectPtr<APYRMonPawn> User;
    double TellStart = 0, HitStart = 0, HitEnd = 0;
    double ChargeStart = 0;        // Bide: the orange glow starts here, the tell at TellStart.
    bool bHasTell = false, bHit = false;
    // m3: a dived swimmer's attack. It surfaces with a 0.6 s splash tell (bubbles and spreading rings where it will
    // come up), then strikes from below.
    bool bFromBelow = false;
    FVector Origin, Dir, Aim;      // Aim: target point (circle, dive landing).
    FVector Head;                  // Projectile position.
    float Travelled = 0;
    bool IsTelling() const { return Phase == EPhase::Tell && bHasTell && Now() >= TellStart; }
    bool IsLive() const { return Phase != EPhase::Done; }
    // Tell points for the camera and the bot (the user, the shadow centre, points along the stripe).
    void GetTellPoints(TArray<FVector>& Out) const;
    // Seconds until the hit window opens (0 once it is open).
    double SecondsToHit() const;
    // Seconds until this attack would reach Target where it stands now (negative once past).
    double TimeToImpact(const APYRMonPawn* Target) const;
    // Would a target at its location right now be inside this attack's footprint?
    bool Threatens(const APYRMonPawn* Target) const;
    void Cancel();
    // The effects this attack spawned (tests look at them).
    TArray<TWeakObjectPtr<APYRMoveFx>> Effects;

private:
    void StartActive();
    void TickActive(float Dt);
    bool HeightOverlaps(const APYRMonPawn* Target) const;
    bool TryHit(APYRMonPawn* Target, FVector At);
    void Finish();
    APYRMoveFx* Fx(uint8 Kind, FVector At, FVector FxDir, FLinearColor Colour, float Size, float Life);
    UStaticMeshComponent* AddMesh(const TCHAR* Shape, FVector Scale, FLinearColor Colour, bool bWorld = true);
    APYRMonPawn* Target() const;
    double Now() const;
    float FloorZ = 0;
    bool bTellShown = false;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Tell;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Stripe;
    TWeakObjectPtr<APYRMoveFx> Carrier;  // The effect riding the projectile head (bolt, needle, wave).
    TWeakObjectPtr<APYRMoveFx> Mound;    // Dig: the mound of earth running to the target.
    double NextTellFx = 0;
    bool bSelfDestructed = false;
};
