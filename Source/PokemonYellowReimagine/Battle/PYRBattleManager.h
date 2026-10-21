// Runs one live battle at a time: spawns the fighters (growing them to battle size), possesses the player's
// Pokemon, resolves hits with Yellow's damage, types, stages and status, and runs swaps, Potions, Poke Balls,
// experience, evolution, the fifth-move prompt, Pikachu's friendship and Yellow's captions. It hands Red back when
// the fight ends. `journey` starts every fight with StartBattle and owns the camera; this never sets the view target.
//
// m4t: the battle has its own clock (Now()). SetBattlePaused holds it with a stack of reasons (the switch menu, the
// learn-move prompt): while any reason is active the clock stops, the fighters, attacks and effects stand still
// (custom time dilation 0 plus a guard in each battle tick) and no command but the menu's own goes through. The camera,
// the HUD and the Game Boy screens keep running. Every battle timer (cooldowns, the swap and Potion timers, status,
// Bide, stun, the foe brain, the ball's wobbles, the trainer's next Pokemon, the end of the fight) reads that clock.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PYRBattleTypes.h"
#include "PYRMonPawn.h"
#include "Arena/PYRPoolRules.h"
#include "../Party/PYRObedience.h"
#include "PYRBattleManager.generated.h"

class ACharacter;
class APlayerController;
class APYRAttack;
class APYRMoveFx;
class SPYRBattleHUD;
struct FPYRMoveInfo;
struct FPYRMon;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPYRBattleOutcomeDynamic, EPYRBattleOutcome, Outcome);
DECLARE_MULTICAST_DELEGATE_OneParam(FPYRBattleOutcomeNative, EPYRBattleOutcome);
// m1 signature, kept so m1 encounter code still binds until it moves to the outcome-only handler.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPYRBattleEnded, EPYRBattleResult, Result, FName, TrainerId);

// The end-of-battle multicast: bind with AddDynamic/AddUniqueDynamic (a UFUNCTION taking EPYRBattleOutcome),
// or AddUObject/AddLambda/AddWeakLambda (native). Broadcast once per battle with Won, Lost, Fled or Caught.
struct FOnPYRBattleEnded
{
    FPYRBattleOutcomeDynamic Dynamic;
    FPYRBattleOutcomeNative Native;
    FPYRBattleEnded Legacy;

    template <class U> void __Internal_AddDynamic(U* Obj, void (U::*Fn)(EPYRBattleOutcome), FName Name) { Dynamic.__Internal_AddDynamic(Obj, Fn, Name); }
    template <class U> void __Internal_AddUniqueDynamic(U* Obj, void (U::*Fn)(EPYRBattleOutcome), FName Name) { Dynamic.__Internal_AddUniqueDynamic(Obj, Fn, Name); }
    template <class U> void __Internal_RemoveDynamic(U* Obj, void (U::*Fn)(EPYRBattleOutcome), FName Name) { Dynamic.__Internal_RemoveDynamic(Obj, Fn, Name); }
    template <class U> void __Internal_AddDynamic(U* Obj, void (U::*Fn)(EPYRBattleResult, FName), FName Name) { Legacy.__Internal_AddDynamic(Obj, Fn, Name); }
    template <class U> void __Internal_AddUniqueDynamic(U* Obj, void (U::*Fn)(EPYRBattleResult, FName), FName Name) { Legacy.__Internal_AddUniqueDynamic(Obj, Fn, Name); }
    template <class U> void __Internal_RemoveDynamic(U* Obj, void (U::*Fn)(EPYRBattleResult, FName), FName Name) { Legacy.__Internal_RemoveDynamic(Obj, Fn, Name); }
    template <typename... A> FDelegateHandle AddUObject(A&&... Args) { return Native.AddUObject(Forward<A>(Args)...); }
    template <typename... A> FDelegateHandle AddLambda(A&&... Args) { return Native.AddLambda(Forward<A>(Args)...); }
    template <typename... A> FDelegateHandle AddWeakLambda(A&&... Args) { return Native.AddWeakLambda(Forward<A>(Args)...); }
    template <typename... A> FDelegateHandle AddRaw(A&&... Args) { return Native.AddRaw(Forward<A>(Args)...); }
    bool Remove(FDelegateHandle Handle) { return Native.Remove(Handle); }
    void RemoveAll(UObject* Obj) { Native.RemoveAll(Obj); Dynamic.RemoveAll(Obj); Legacy.RemoveAll(Obj); }
    bool IsBound() const { return Native.IsBound() || Dynamic.IsBound() || Legacy.IsBound(); }
    void Broadcast(EPYRBattleOutcome Outcome, FName TrainerId)
    {
        Native.Broadcast(Outcome);
        Dynamic.Broadcast(Outcome);
        Legacy.Broadcast(EPYRBattleResult(uint8(Outcome)), TrainerId);
    }
};

// One enemy attack's timing, logged for the tell test.
struct FPYRTellRecord
{
    FName Move;
    FName Species;
    double TellStart = 0, HitStart = 0;
    bool bHadTell = false;
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRBattleManager : public AActor
{
    GENERATED_BODY()
public:
    APYRBattleManager();
    // The world's manager; spawns one if missing.
    static APYRBattleManager* Get(const UObject* Ctx);
    static APYRBattleManager* Find(const UObject* Ctx);

    // ---- The `journey` seam
    void StartBattle(const FPYRBattleSetup& Setup);
    FOnPYRBattleEnded OnBattleEnded;
    // The wild ring's gap: the fight ends Fled.
    void RequestFlee();
    bool IsBattleActive() const { return bInBattle; }
    // The outcome is decided and the ring is clearing (moves no longer go).
    bool IsEnding() const { return bInBattle && EndAt >= 0; }
    APYRMonPawn* GetPlayerMon() const { return Player.Get(); }
    APYRMonPawn* GetFoeMon() const { return Foe.Get(); }
    // The most recent Yellow caption ('It's super effective!', 'Wild Pidgey appeared!'...).
    FText LastCaption() const { return FText::FromString(CaptionText); }
    // Where every active enemy warning is (glows, crouches, ground shadows and stripes), for the camera.
    void GetActiveTells(TArray<FVector>& Out) const;
    // A non-fighting body for wild Pokemon in the field (field size).
    static ACharacter* SpawnMonVisual(UWorld* World, FName Species, const FTransform& Transform);
    // Test hook for `journey`: the current fight ends at once with this outcome (a win pays the prize).
    void DebugFinish(EPYRBattleOutcome Outcome);

    // ---- m1 seam (the m1 encounter code still calls these)
    void StartWild(const FPYRArena& Arena, const FPYRFoe& Foe, FVector FoeStart, bool bFoeStunned2s);
    void StartTrainer(const FPYRArena& Arena, const TArray<FPYRFoe>& Foes, FName TrainerId, int32 Prize);
    void Flee() { RequestFlee(); }
    bool IsInBattle() const { return bInBattle; }
    bool IsTrainerBattle() const { return bInBattle && IsTrainerKind(); }

    // ---- Player handlers (keys and FPYRBattleBot both call these)
    void PressMove(int32 Slot);
    void PressDodge();
    void PressSwap(int32 PartyIndex);
    void PressPotion();
    // E (m3): runners and crawlers hop (clearing gaps under 4 m); swimmers dive or come up.
    void PressHop();
    // F: hold to aim (the catch meter circles the foe), release to throw.
    void BeginAim();
    void ReleaseAim();
    void PressBall();  // Throws at once.
    // m4t: the switch menu. Q (Tab, the pad's left shoulder) opens it and pauses the battle. Released with a direction
    // held, it swaps to that slot at once (the m2 flick); released without one it stays open: arrows or the D-pad move
    // the cursor, E/Enter/Space or the pad's A take the Pokemon under it, 1-6 take one directly, and Q, Esc, Backspace
    // or the pad's B cancel (not after a faint: that switch is forced). A faint opens it forced, paused too.
    void OpenWheel(bool bHeld = false);
    void CloseWheel(FVector2D Direction);  // The key's release: picks by direction when it's long enough.
    int32 GetWheelCursor() const { return WheelCursor; }
    void MoveWheelCursor(int32 Step);
    void ConfirmWheel();
    void CancelWheel();
    bool IsWheelHeld() const { return bWheelHeld; }
    // m4t: Run (X, the pad's Y, the HUD's RUN button, the stream's RUN). A wild battle always ends: "Got away safely!",
    // then Fled. Trainer, gym, lab and scripted fights refuse ("No! There's no running from a trainer battle!") and go on.
    // Ignored while the switch menu or the learn prompt holds the battle.
    void PressRun();
    int32 RunPresses = 0, RunRefusals = 0;
    double RunPressedAt = -100;   // Battle clock: the HUD lights the RUN button for a moment.
    // The learn-move prompt is up over the fight (PYRMoveLearning; the battle is paused under it).
    bool IsMenuOpen() const { return IsPausedFor(PauseLearn()); }
    // The fighter in the ring takes its party slot's moves (after a prompt changed them); a new move starts ready.
    void SyncCarried(int32 PartyIndex);
    float GetCooldownFraction(int32 Slot) const;

    // ---- m4t: the battle pause gate
    static FName PauseSwitch() { return FName(TEXT("switch")); }
    static FName PauseLearn() { return FName(TEXT("learn")); }
    // Adds (bPause) or removes one Reason; reasons nest. The first stops the battle clock and freezes the battle's
    // actors; the last one off starts them again.
    void SetBattlePaused(bool bPause, FName Reason);
    bool IsBattlePaused() const { return PauseReasons.Num() > 0; }
    bool IsPausedFor(FName Reason) const { return PauseReasons.Contains(Reason); }
    const TArray<FName>& GetPauseReasons() const { return PauseReasons; }
    // Seconds the battle clock has been held in all (tests).
    double PausedSeconds() const;
    // The battle clock of Ctx's world (world time when there is no manager): pawns, bodies, attacks and the rig use it.
    static double BattleNow(const UObject* Ctx);

    // The HUD widget while a fight is on (tests click its RUN button through its own handler).
    TSharedPtr<SPYRBattleHUD> GetHud() const { return Hud; }

    // ---- State for the HUD, bot and tests
    APYRMonPawn* PlayerMon() const { return Player.Get(); }
    APYRMonPawn* FoeMon() const { return Foe.Get(); }
    ACharacter* GetRed() const { return Red.Get(); }
    const FPYRBattleSetup& GetSetup() const { return Setup; }
    FVector GetCenter() const { return Setup.Center; }
    float GetRadius() const { return Setup.Radius; }
    // m3: the pool rules for this fight (inactive unless tagged water meets the ring).
    const FPYRPoolRules& GetPool() const { return Pool; }
    // m3: cave rings. The ceiling's world height (from FPYRBattleSetup::MaxHoverHeightCm over the floor at the ring's
    // centre). Worker `encounters` can read it to keep the battle camera under the rock.
    bool HasCeiling() const { return CeilingZ < 1e8f; }
    float GetCeilingZ() const { return CeilingZ; }
    float GetFloorZ() const { return CenterFloorZ; }
    // m3 obedience: the last check's result and counts (tests and the HUD).
    EPYRObey LastObey = EPYRObey::Obeys;
    int32 ObeyChecks = 0, ObeyIgnored = 0;
    // Pay Day coins picked up after the fight (2 x the user's level per hit).
    int32 PayDayMoney = 0;
    EPYRBattleKind GetKind() const { return Setup.Kind; }
    bool IsTrainerKind() const { return Setup.Kind == EPYRBattleKind::Trainer || Setup.Kind == EPYRBattleKind::Gym || Setup.Kind == EPYRBattleKind::Lab; }
    int32 ActiveIndex() const { return ActivePartyIndex; }
    bool IsWheelOpen() const { return bWheelOpen; }
    bool IsWheelForced() const { return bWheelForced; }
    bool IsAiming() const { return bAiming; }
    float SwapSecondsLeft() const;
    float PotionSecondsLeft() const;
    bool IsSwapReady() const { return SwapSecondsLeft() <= 0; }
    bool IsPotionReady() const { return PotionSecondsLeft() <= 0; }
    bool IsBallBusy() const { return BallPhase != EBall::None; }
    bool IsWobbling() const { return BallPhase == EBall::Wobble; }
    int32 WobblesShown() const { return Wobbles; }
    // Yellow's catch chance for a Poke Ball right now.
    float CatchChance() const;
    // The meter: 0 red, 1 yellow, 2 green (the chance against the best it can get at 1 HP).
    int32 CatchMeterBand() const;
    double Now() const;
    FString CaptionText;
    double CaptionAt = -100;
    TArray<FString> CaptionLog;
    FString Shout; double ShoutUntil = 0;
    FString Banner; double BannerUntil = 0;
    TArray<FPYRTellRecord> TellLog;
    TMap<FName, int32> FxLog;       // Effects spawned per move id.
    int32 BallEffectsSpawned = 0;   // Red-light returns and ball releases (never for Pikachu).
    int32 PotionsUsed = 0;
    int32 BallsBlocked = 0, BallsDodged = 0;
    bool bAutoDodgeUsed = false;
    int32 AutoDodges = 0;
    float LastDamage = 0;           // The last hit's damage (0 when it had no effect).
    float LastFactor = 1;           // Its type factor.
    bool LastHitLanded = false;     // False for a miss, a dodge or no effect.
    int32 Participants() const { return Fought.Num(); }
    TSet<int32> Fought;             // Party indices that fought the current foe.
    TSet<int32> FoughtAll;          // ... this battle.
    APYRMonPawn* SittingPikachu() const { return PikachuSide.Get(); }
    EPYRBattleOutcome LastOutcome = EPYRBattleOutcome::Fled;
    EPYRBattleResult LastResult = EPYRBattleResult::Fled;  // m1 name.
    bool bHasResult = false;
    int32 FoeIndex = 0;
    TArray<FPYRMonSpec> FoeTeam;
    FName TrainerName;

    // ---- Called by attacks and pawns
    void RegisterAttack(APYRAttack* Attack);
    // Resolves Move from User landing on Victim. Returns the damage dealt (0 when missed, dodged or immune).
    float ResolveHit(APYRMonPawn* User, APYRMonPawn* Victim, const FPYRMoveInfo& Move, FVector At);
    void ResolveSelf(APYRMonPawn* User, const FPYRMoveInfo& Move);
    // Foes use this (tell first); the player's moves go through PressMove.
    bool FoeUseMove(APYRMonPawn* User, FName Move);
    // Selfdestruct's user faints after its blast.
    void ExplodeFaint(APYRMonPawn* User);
    void Caption(const FString& Line);
    void SayRed(const FString& Line, float Seconds = 2.2f);
    void HitStop(float RealSeconds);
    void Shake(float Strength);
    void NoteFx(FName Move, APYRMoveFx* Fx);
    void TellAdded(FName Move, FName Species, double TellStart, double HitStart, bool bHadTell);
    static bool GroundZ(UWorld* World, FVector At, float& OutZ, const AActor* Ignore = nullptr);
    static FString MonName(FName Species);

    // ---- Test hooks
    float ForcedCatchRoll = -1;     // In [0,1): overrides the catch roll.
    float ForcedBallDodgeRoll = -1; // In [0,1): overrides the wild foe's ball-dodge roll.
    bool bNoRandom = false;         // Tests: no damage roll, no crits, moves never miss by accuracy.
    bool bFoePassive = false;       // Tests: the foe brain does nothing (forced moves still work).
    bool bHoldEnd = false;          // Shots: a decided fight stays on its last caption (m4t-run-wild).
    void DebugForceFoeMove(FName Move);
    void DebugSetFoeHp(float Fraction);
    void DebugResetCooldowns();

    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    void Begin();
    APYRMonPawn* SpawnFighter(const FPYRMon& Mon, bool bPlayerSide, int32 PartyIndex, FVector At, FVector Face);
    void SendOutPlayer(int32 PartyIndex, FVector At, bool bFreeDodge);
    void SendOutFoe(int32 Index, FVector At);
    void RecallPlayer(bool bFainted);
    void PlayerFainted();
    void FoeFainted();
    void AwardXp(const FPYRMonSpec& Beaten);
    // m4t: moves still waiting after a level-up go to the one learn-move prompt at once, with the battle paused.
    void OpenLearnPrompt();
    bool bDeferLearn = false;
    void FreezeActors(bool bFreeze);
    void ShutWheel();
    void Decide(EPYRBattleOutcome Outcome, float Delay);
    void End(EPYRBattleOutcome Outcome);
    void Cleanup();
    void SyncParty();
    void TickBall(float Dt);
    void TickDemo(float Dt);
    void TickPikachuSide(float Dt);
    void TickStatuses(double T);
    void FireMove(APYRMonPawn* User, int32 Slot);
    // Yellow's checks before a move (confusion, full paralysis; obedience for Red's traded Pokemon). False when the
    // turn is lost; Slot may change (a disobedient Pokemon using another move).
    bool PreMove(APYRMonPawn* User, int32& Slot, bool bPlayer);
    // Spawns the attack for Move (Mirror Move takes the foe's last move).
    APYRAttack* LaunchMove(APYRMonPawn* User, const FPYRMoveInfo& Move, bool bTell);
    void HurtInConfusion(APYRMonPawn* User);
    void Faint(APYRMonPawn* Victim);
    // Where a fighter of this species can stand at At (pool: swimmers in open water, the rest on platforms).
    FVector StandingSpot(FName Species, FVector At) const;
    float Probe() const;  // How far above a point to start ground traces (lower under a cave ceiling).
    FPYRPoolRules Pool;
    float CeilingZ = 1e9f, CenterFloorZ = 0;
    void ThrowBall(FVector From);
    APlayerController* PC() const;
    FVector MonSpot(bool bPlayerSide) const;
    FVector RedSpot() const;
    void ShowHud(bool bShow);
    FString TrainerTitle() const;

    bool bInBattle = false;
    FPYRBattleSetup Setup;
    TWeakObjectPtr<APYRMonPawn> Player, Foe;
    TWeakObjectPtr<ACharacter> Red;
    TWeakObjectPtr<AActor> Follower;
    TWeakObjectPtr<APYRMonPawn> PikachuSide;  // Pikachu by Red's side when swapped out.
    FVector RedEdge = FVector::ZeroVector;
    FVector LastPikachuPos = FVector::ZeroVector;
    FVector RunInTo = FVector::ZeroVector; double RunInUntil = 0;
    int32 ActivePartyIndex = INDEX_NONE;
    double SwapReadyAt = 0, PotionReadyAt = 0;
    bool bWheelOpen = false, bWheelForced = false, bWheelHeld = false;
    int32 WheelCursor = 0;
    TArray<FName> PauseReasons;
    double PausedTotal = 0, PausedSince = -1;
    TArray<TWeakObjectPtr<AActor>> Frozen;
    TArray<float> FrozenLife;
    bool bAiming = false; double AimSince = 0;
    double EndAt = -1; EPYRBattleOutcome PendingOutcome = EPYRBattleOutcome::Won;
    double FoeNextAt = -1;
    double DemoAt = 0; int32 DemoStep = 0;
    TArray<TWeakObjectPtr<APYRAttack>> Attacks;
    double HitStopEndReal = 0;
    TSharedPtr<SPYRBattleHUD> Hud;

    // Poke Ball throw: wind-up by Red, flight, wobbles.
    enum class EBall : uint8 { None, Windup, Flight, Wobble };
    EBall BallPhase = EBall::None;
    double BallPhaseAt = 0;
    int32 Wobbles = 0, BreakAtWobble = 0;
    bool bBallHolds = false, bBallDemo = false;
    FVector BallFrom, BallTo;
    UPROPERTY() TObjectPtr<AActor> BallActor;
};
