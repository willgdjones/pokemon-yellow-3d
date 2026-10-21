// A Pokemon in the ring (or, with bVisualOnly, a non-fighting body for the field). The player steers the
// active one directly; foes run a small readable brain. How it moves and dodges comes from its body component.
// Outside battle it shows at its field size; as the ring forms it grows over 1 s to its battle display height
// (at least 1.2 m, giants true size), hitbox included, and shrinks back after.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Character.h"
#include "../Party/PYRSpecies.h"
#include "PYRMonPawn.generated.h"

class APYRMonPawn;
class APYRBattleManager;
class UStaticMeshComponent;
class USceneComponent;
class UPYRMonRigAnimatorComponent;

enum class EPYRPose : uint8 { None, Crouch, Rear, Tuck, Sit, Faint, Grounded, Harden, Sleep, Sink, Flop };

// ---- Body families
UCLASS(Abstract)
class POKEMONYELLOWREIMAGINE_API UPYRBodyComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UPYRBodyComponent();
    virtual void Configure(APYRMonPawn* Pawn);
    // Starts a dodge (Dir is a world direction; zero means "sideways"). False when not ready or not possible.
    virtual bool TryDodge(FVector Dir) { return false; }
    virtual void TickBody(float Dt) {}
    virtual bool CanDodge() const { return true; }
    bool IsDodgeReady() const;
    bool IsDodging() const;
    // 0 ready .. 1 just used.
    float DodgeCooldownFraction() const;
    float DodgeCooldown = 1.2f;
    float DodgeInvulnerable = .25f;
    double DodgeReadyAt = 0, DodgeEndsAt = 0;
    FVector DodgeVelocity = FVector::ZeroVector;
    FVector DodgeStart = FVector::ZeroVector;
    float DashLeft = 0;
protected:
    APYRMonPawn* Mon() const;
    double Now() const;
    // Moves the pawn along DodgeVelocity with sweeps while the dodge lasts.
    void TickDash(float Dt);
};

// Runners (7 m/s): a 4 m dash in 0.25 s, invulnerable while dashing, 1.2 s cooldown. Hop down ledges, can't climb.
UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRRunnerBody : public UPYRBodyComponent
{
    GENERATED_BODY()
public:
    virtual void Configure(APYRMonPawn* Pawn) override;
    virtual bool TryDodge(FVector Dir) override;
    virtual void TickBody(float Dt) override { TickDash(Dt); }
    float DashDistance = 400.f, DashSeconds = .25f;
};

// Fliers (5.5 m/s) hover 1.5 m up and cross ledges both ways. Dodge: 8 m sideways (twice the runner's dash),
// 1.8 s cooldown. A Rock hit grounds them for about a second.
UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRFlierBody : public UPYRBodyComponent
{
    GENERATED_BODY()
public:
    virtual void Configure(APYRMonPawn* Pawn) override;
    virtual bool TryDodge(FVector Dir) override;
    virtual void TickBody(float Dt) override;
    void Ground(float Seconds);
    bool IsGrounded() const { return Now() < GroundedUntil; }
    float DashDistance = 800.f, DashSeconds = .35f;
    double GroundedUntil = 0;
    // Set while a dive carries the flier down; hover resumes after.
    double DiveUntil = 0;
private:
    bool bWasGrounded = false;
};

// Crawlers (3.5 m/s) are low (hitbox top under 70 cm): wide sweeps pass over them. Dodge: a 0.5 s tuck in place.
UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRCrawlerBody : public UPYRBodyComponent
{
    GENERATED_BODY()
public:
    virtual void Configure(APYRMonPawn* Pawn) override;
    virtual bool TryDodge(FVector Dir) override;
    virtual void TickBody(float Dt) override;
    float TuckSeconds = .5f;
};

// Giants turn slowly and never dodge.
UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRGiantBody : public UPYRBodyComponent
{
    GENERATED_BODY()
public:
    virtual void Configure(APYRMonPawn* Pawn) override;
    virtual bool CanDodge() const override { return false; }
};

// Swimmers (m3): in the pool's open water they swim at the surface at 6 m/s (the water surface doesn't block them)
// and can dive out of reach; the dodge is a 4.5 m dart. On dry ground they flop at 1.5 m/s and the dodge is a short
// flop. See FPYRPoolRules.
UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRSwimmerBody : public UPYRBodyComponent
{
    GENERATED_BODY()
public:
    virtual void Configure(APYRMonPawn* Pawn) override;
    virtual bool TryDodge(FVector Dir) override;
    virtual void TickBody(float Dt) override;
    bool IsInWater() const { return bInWater; }
    float DashDistance = 450.f, DashSeconds = .3f;
    // Surfaced, the capsule centre rides this many half-heights above the water: about a quarter of the body is under.
    static constexpr float SurfaceRide = .45f;
private:
    bool bInWater = false, bIgnoring = false;
    float FlopClock = 0;
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRMonPawn : public ACharacter
{
    GENERATED_BODY()
public:
    APYRMonPawn();

    // Call right after spawning. Starts at field size; GrowToBattle() makes it battle size over Seconds.
    void InitMon(FName InSpecies, int32 InLevel, float InHp, float InMaxHp, const TArray<FName>& InCarried, bool bInPlayerSide, int32 InPartyIndex,
        EPYRStatus InStatus = EPYRStatus::None);
    void InitVisualOnly(FName InSpecies);
    // Evolution: new species, body, mesh and hitbox in place.
    void ChangeSpecies(FName NewSpecies);

    virtual void Tick(float Dt) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

    // ---- Identity and numbers
    FName Species;
    int32 Level = 1;
    float Hp = 1, MaxHp = 1;
    TArray<FName> Carried;
    bool bPlayerSide = false;
    int32 PartyIndex = INDEX_NONE;
    bool bVisualOnly = false;
    const FPYRSpeciesInfo* Info = nullptr;
    EPYRBody GetBody() const { return Info ? Info->Body : EPYRBody::Runner; }
    UPROPERTY() TObjectPtr<UPYRBodyComponent> BodyComp;

    // ---- Size
    // Battle display height in metres right now (grows to at least 1.2 m in battle; giants true size).
    float GetBattleScreenHeightM() const;
    float GetDisplayScale() const { return DisplayScale; }
    // 1 = battle size; FieldScale() = field size.
    float FieldScale() const;
    void GrowTo(float Scale, float Seconds);
    bool IsGrowing() const { return GrowSeconds > 0 && GrowAge < GrowSeconds; }

    // ---- Moves
    float Cooldown[4] = {0, 0, 0, 0};       // Seconds left.
    float CooldownTotal[4] = {1, 1, 1, 1};
    float GetCooldownFraction(int32 Slot) const;
    bool IsMoveReady(int32 Slot) const;
    int32 SlotOf(FName Move) const;
    float MoveCooldown(const FPYRMoveInfo& Move) const;  // Best friends: Thundershock recharges a third faster.
    double LastMoveTime = -1; FName LastMove;
    double BusyUntil = 0;  // A dash or dive in progress.

    // ---- Battle state (Gen 1 stages -6..+6, status, volatile effects)
    int32 Stages[(int32)EPYRStat::Count] = {0, 0, 0, 0, 0, 0};
    int32 GetStage(EPYRStat Stat) const { return Stages[(int32)Stat]; }
    // Returns the change actually made (0 when already at the limit).
    int32 AddStage(EPYRStat Stat, int32 Delta);
    float EffectiveStat(EPYRStat Stat) const;  // Gen 1 stat at this level x stage (paralysis quarters Speed).
    EPYRStatus Status = EPYRStatus::None;
    double InvulnerableUntil = 0, StunnedUntil = 0, ConfusedUntil = 0;
    double NextPoisonTick = 0;
    bool bFainted = false;
    // Bide: storing damage until BideUntil.
    double BideUntil = 0; float BideStored = 0;
    bool IsBiding() const;
    // Potion heal over time.
    float HealLeft = 0, HealRate = 0;
    bool IsInvulnerable() const;
    bool IsStunned() const;
    bool IsDodgeReady() const { return BodyComp && BodyComp->CanDodge() && BodyComp->IsDodgeReady() && !IsStunned() && !bFainted; }

    // ---- m3 status (Gen 1). Sleep, a squeeze and a fall into the water also set StunnedUntil, so every "can't act"
    // check above covers them.
    double SleepUntil = 0;
    bool IsAsleep() const { return Status == EPYRStatus::Sleep && Now() < SleepUntil; }
    void FallAsleep(float Seconds);
    double TrappedUntil = 0, SqueezeAt = 0;   // Wrap, Bind, Clamp: bound until TrappedUntil, squeezed again at SqueezeAt.
    float SqueezeDamage = 0;
    TWeakObjectPtr<APYRMonPawn> TrappedBy;
    bool bSeeded = false;                     // Leech Seed: 1/16 of max HP to Seeder every 2 s.
    TWeakObjectPtr<APYRMonPawn> Seeder;
    double NextSeedTick = 0;
    int32 DisabledSlot = INDEX_NONE;          // Disable: that move can't be used until DisabledUntil.
    double DisabledUntil = 0;
    bool IsSlotDisabled(int32 Slot) const { return Slot == DisabledSlot && Now() < DisabledUntil; }
    bool bFocused = false;                    // Focus Energy (Gen 1's bug: a quarter of the critical-hit rate).
    double RageUntil = 0;                     // Rage: hits taken raise Attack.
    double NextBurnTick = 0;
    bool bConfused = false;                   // Set with ConfusedUntil; cleared with "... confused no more!".
    double NextStatusFx = 0;
    TWeakObjectPtr<class APYRAttack> CurrentAttack;  // The move it is using now (tell or hit window).
    bool IsAttacking() const;

    // ---- m3 water, hops and burrows
    bool IsSwimmer() const { return GetBody() == EPYRBody::Swimmer; }
    bool IsInWater() const;                   // A swimmer in the pool's open water.
    // Dive (swimmers in open water, once their 2 s surfaced window is over): only a ripple and a shadow show, and
    // nothing can hit it until it surfaces. Attacking from under water surfaces with a 0.6 s splash tell first.
    bool TryDive();
    void SurfaceNow(bool bBigSplash = true);
    bool bDived = false;
    double DivedAt = 0, SurfacedUntil = 0;
    double BurrowUntil = 0;                   // Underground mid-Dig.
    // Dived, underground or down in the water after a fall: attacks pass it by.
    bool IsUntargetable() const;
    // The body is hidden (dived or underground); only the ripple, shadow or mound shows.
    bool IsBodyHidden() const { return bShownDived; }
    // Runners and crawlers (E): a hop that clears gaps under 4 m.
    bool Hop();
    bool IsAirborne() const;
    int32 Hops = 0;
    // Where the current hop started (a hop that lands more than a step higher goes back: ledges stay one-way).
    FVector HopFrom = FVector::ZeroVector;
    float HopFloorZ = 0;  // The capsule bottom at take-off.
    bool bHopping = false, bHopAirborne = false;
    // A runner or crawler that stepped onto open water: no damage, 1 s lost, then back up on the nearest platform.
    double FellInUntil = 0;
    int32 WaterFalls = 0;
    FVector LastFallAt = FVector::ZeroVector, LastPopUpAt = FVector::ZeroVector;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Ripple;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> RippleInner;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> WaterShadow;
    float SpeedFactor() const;
    float FloorZ() const;                    // Ground height under the pawn (trace).
    float BottomZ() const;                   // Capsule bottom.
    float TopZ() const;
    float CapsuleRadius() const;

    // ---- Control (player input and the foe brain both come here)
    void Steer(FVector WorldDir);            // Adds movement input for this frame (normalised).
    bool Dodge(FVector Dir = FVector::ZeroVector);
    FVector LastSteer = FVector::ZeroVector; // Last non-zero steering direction (for dodges).
    FVector2D InputAxes = FVector2D::ZeroVector;  // Raw WASD this frame (x right, y up) for the party wheel.

    // ---- Look
    void SetPose(EPYRPose Pose, float Seconds);
    void SetGlow(bool bOn, FLinearColor Colour = FLinearColor(1, .95f, .5f));
    bool IsGlowing() const;
    void FlashHit();
    void SetFaintLook();
    UPROPERTY() TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Glow;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Aura;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Shadow;
    UPROPERTY() TObjectPtr<UPYRMonRigAnimatorComponent> Rig;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Wings;
    bool bUsesImportedMesh = false;

    // ---- Foe brain
    void TickBrain(float Dt);
    float BrainClock = 1.2f;
    float StrafeSign = 1;

    APYRBattleManager* Manager() const;
    APYRMonPawn* Opponent() const;
    double Now() const;

private:
    void ApplySpecies();
    void BuildVisual();
    void ClearVisual();
    void BuildPrimitives();
    void TickWater(float Dt);
    void TickStatusLook();
    void TickSwimBrain(float Dt, APYRMonPawn* Opp);
    void UpdateDivedLook();
    bool bShownDived = false;
    double NextRippleFx = 0;
    void ApplyScale(float Scale);
    UStaticMeshComponent* AddPart(const TCHAR* Shape, FVector Loc, FVector Scale, FLinearColor Colour, FRotator Rot = FRotator::ZeroRotator, bool bWing = false);
    EPYRPose Pose = EPYRPose::None;
    double PoseUntil = 0;
    double FlashUntil = 0;
    float BobClock = 0;
    float DisplayScale = 1.f, GrowFrom = 1.f, GrowTarget = 1.f, GrowAge = 0.f, GrowSeconds = 0.f;

    // Input (keys bound with BindKey; handlers forward to the battle manager).
    void AxisW(float V); void AxisS(float V); void AxisA(float V); void AxisD(float V);
    void Key1(); void Key2(); void Key3(); void Key4(); void Key5(); void Key6();
    void KeyDodge(); void KeyWheelDown(); void KeyWheelUp(); void KeyPotion(); void KeyAimDown(); void KeyAimUp(); void KeyHop(); void KeyRun();
    void NumberKey(int32 Index);
    FVector2D PendingAxes = FVector2D::ZeroVector;
};
