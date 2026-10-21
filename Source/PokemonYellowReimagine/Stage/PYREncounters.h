// M4: random encounters and the trip to and from the battle stage.
// - Each time Red's tile changes onto tall grass (g), or cave floor (c) in a cave map, the Game Boy's check runs:
//   random(0..255) < the map's encounter rate. The slot is picked with Yellow's odds 51,51,39,25,25,25,13,13,11,3 of
//   256 from wild.json. A Repel blocks wild Pokemon below the lead's level.
// - Every fight (wild, trainer, gym, the lab, the old man's lesson) goes the same way: three white flashes and the
//   wipe, Red and Pikachu on the classic stage for the place (Stage/PYRStage), the battle music, m3's real-time fight
//   in a ring over the stage (a worn gap at the edge for wild battles: 3 s in it flees; trainers' rings are closed),
//   the high camera. Afterwards: the trainer's last words and prize, a caught Pokemon's nickname, the forget-move
//   screen for a fifth move, the evolution scene, then the flash back to the exact tile Red left with Pikachu at his
//   heels. A loss blacks out to the last Pokemon Centre (half the money, the party healed) unless the fight says not.
#pragma once

#include "CoreMinimal.h"
#include "Battle/PYRBattleTypes.h"
#include "Stage/PYRStage.h"
#include "Subsystems/WorldSubsystem.h"
#include "Yellow/YellowData.h"
#include "PYREncounters.generated.h"

class APYRBattleCamera;
class APYRRing;
class APYRPerson;

struct FPYRFight
{
    EPYRBattleKind Kind = EPYRBattleKind::Wild;
    TArray<FPYRMonSpec> Foes;
    FName TrainerId;                // for the battle manager and logs
    FString TrainerName;            // "BUG CATCHER", "BROCK"
    int32 Prize = 0;
    EPYRStageKind Stage = EPYRStageKind::Grass;
    bool bStageSet = false;         // false: the stage for Red's current map
    bool bNoBlackout = false;
    FString WinText;                // said after a won trainer battle, before the prize
    FString LossText;
    FName Music;                    // the battle theme (default by kind)
    TFunction<void(EPYRBattleOutcome)> OnDone;
    // The trainer's model(s) standing across the stage from Red ("jessie,james" for two), and their height.
    FString TrainerModel;
    float TrainerHeight = 165.f;
};

UENUM()
enum class EPYREncounterPhase : uint8 { Idle, Flash, Fight, After, Return, Blackout };

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYREncounters : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UPYREncounters* Get(const UObject* Ctx);
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPYREncounters, STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

    // Builds the stages and listens to Red's steps (the game mode calls it once the world is up).
    void Start();
    APYRBattleStage* GetStage(EPYRStageKind Kind) const;
    static EPYRStageKind StageForMap(const FYMap* Map);

    // ---- Yellow's rolls
    // Slot 0..9 for a 0..255 roll (51,51,39,25,25,25,13,13,11,3).
    static int32 SlotFor(int32 Roll);
    // The encounter rate of a map (its own, else wild.json's grass rate).
    static int32 RateFor(const FYellowDataSet& Data, const FYMap& Map);
    // One step on Tile in Map: true (and the Pokemon) when a wild one appears.
    static bool RollStep(const FYellowDataSet& Data, const FYMap& Map, TCHAR Tile, FRandomStream& Rng, FYSlot& Out);
    // Which Pokemon a Repel keeps away (level below the lead's).
    bool RepelBlocks(const FYSlot& Slot) const;

    // ---- Fights
    bool Begin(const FPYRFight& Fight);
    bool BeginWild(FName Species, int32 Level);
    bool IsBusy() const { return Phase != EPYREncounterPhase::Idle; }
    EPYREncounterPhase GetPhase() const { return Phase; }
    const FPYRFight& GetFight() const { return Fight; }
    APYRBattleStage* GetCurrentStage() const { return CurrentStage.Get(); }
    // The trainer standing across the stage (null in wild battles).
    AActor* GetTrainerVisual() const { return TrainerVisual.Get(); }
    APYRRing* GetRing() const { return Ring.Get(); }
    APYRBattleCamera* GetCamera() const { return Camera.Get(); }

    // Evidence for tests.
    int32 Encounters = 0, Flashes = 0, Blackouts = 0, Returns = 0, StepsChecked = 0;
    FName ReturnMap;
    FIntPoint ReturnTile = FIntPoint::ZeroValue;
    EYDir ReturnFacing = EYDir::Down;
    EPYRBattleOutcome LastOutcome = EPYRBattleOutcome::Fled;
    FYSlot LastWild;
    float GapTime = 0.f;
    // Tests: the next roll always (or never) meets this slot.
    int32 ForceSlot = -1;
    bool bForceEncounter = false;
    FRandomStream Rng;

private:
    void OnTile(FName Map, FIntPoint Local);
    void StartFight();
    void OnBattleEnded(EPYRBattleOutcome Outcome);
    void AfterSteps();
    void NextAfter();
    void ReturnToField();
    void Blackout();
    void Finish();

    UPROPERTY() TArray<TObjectPtr<APYRBattleStage>> Stages;
    TWeakObjectPtr<APYRBattleStage> CurrentStage;
    TWeakObjectPtr<AActor> TrainerVisual;
    TWeakObjectPtr<APYRRing> Ring;
    TWeakObjectPtr<APYRBattleCamera> Camera;
    FPYRFight Fight;
    EPYREncounterPhase Phase = EPYREncounterPhase::Idle;
    double Clock = 0;
    int32 FlashStep = 0;
    FName SongBefore;
    TArray<TFunction<void(TFunction<void()>)>> AfterQueue;
    bool bStarted = false;
    int32 PartyBefore = 0, StorageBefore = 0;
};
