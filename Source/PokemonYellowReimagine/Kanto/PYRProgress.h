// M4: the journey's own state outside the party and the bag (Party/PYRTrainerState): the names, the options, every
// story flag, the items and hidden items taken, the trainers beaten, the play time, the Repel steps and the items
// kept in Red's PC. One per game instance; the save (Save/PYRSave) writes it whole.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Party/PYRTrainerState.h"
#include "PYRProgress.generated.h"

UENUM()
enum class EPYRTextSpeed : uint8 { Fast, Medium, Slow };

USTRUCT()
struct FPYROptions
{
    GENERATED_BODY()
    UPROPERTY() EPYRTextSpeed TextSpeed = EPYRTextSpeed::Medium;
    UPROPERTY() bool bBattleAnimation = true;   // ON: the anime battle effects; OFF: plain hits.
    UPROPERTY() bool bBattleShift = true;       // BATTLE STYLE SHIFT or SET (kept for the Game Boy layout).
    UPROPERTY() bool bEarphones = false;        // SOUND: MONO or EARPHONE (stereo panning of cues).
    // Seconds per letter for the text speed (Game Boy frames at 60 Hz: 1, 3 and 5).
    float LetterSeconds() const { return TextSpeed == EPYRTextSpeed::Fast ? 1.f / 60.f : TextSpeed == EPYRTextSpeed::Medium ? 3.f / 60.f : 5.f / 60.f; }
};

// Everything UPYRProgress keeps, as one saveable value.
USTRUCT()
struct FPYRProgressSnapshot
{
    GENERATED_BODY()
    UPROPERTY() FString PlayerName;
    UPROPERTY() FString RivalName;
    UPROPERTY() FPYROptions Options;
    UPROPERTY() TArray<FName> Flags;
    UPROPERTY() TArray<FString> Taken;
    UPROPERTY() TArray<FString> Beaten;
    UPROPERTY() double PlaySeconds = 0;
    UPROPERTY() int32 RepelSteps = 0;
    UPROPERTY() TArray<FPYRItemStack> PcItems;
    UPROPERTY() int32 TrainerId = 0;
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRProgress : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    static UPYRProgress* Get(const UObject* Ctx);
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // The design's defaults are Red and Gary (Game Boy caps).
    FString PlayerName = TEXT("RED");
    FString RivalName = TEXT("GARY");
    FPYROptions Options;
    int32 TrainerId = 0;          // Red's five-digit ID No.

    // ---- Flags (story beats, gifts, roadblocks). Names are free-form ("got_pokedex", "parcel_delivered"...).
    bool Has(FName Flag) const { return Flags.Contains(Flag); }
    void Set(FName Flag, bool bOn = true);
    const TSet<FName>& GetFlags() const { return Flags; }

    // ---- Items on the ground and hidden items: "MAP:x:y" once taken, never back.
    static FString SpotKey(FName Map, int32 X, int32 Y) { return FString::Printf(TEXT("%s:%d:%d"), *Map.ToString(), X, Y); }
    bool IsTaken(const FString& Key) const { return Taken.Contains(Key); }
    void Take(const FString& Key) { Taken.Add(Key); }

    // ---- Trainers: "MAP:npc id" once beaten, never again.
    static FString TrainerKey(FName Map, const FString& NpcId) { return FString::Printf(TEXT("%s:%s"), *Map.ToString(), *NpcId); }
    bool IsBeaten(const FString& Key) const { return Beaten.Contains(Key); }
    void Beat(const FString& Key) { Beaten.Add(Key); }
    int32 NumBeaten() const { return Beaten.Num(); }

    // ---- Play time (counted while a world is playing and not paused).
    double PlaySeconds = 0;

    // ---- Repel: steps left; encounters below the lead's level are blocked while it lasts.
    int32 RepelSteps = 0;

    // ---- Red's PC items (Game Boy: 50 slots).
    TArray<FPYRItemStack> PcItems;
    static constexpr int32 PcSlots = 50;
    bool DepositPc(FName Item, int32 Count);
    bool WithdrawPc(FName Item, int32 Count);
    int32 PcCount(FName Item) const;

    // Replace <PLAYER> and <RIVAL> (and the Game Boy's POKé tokens) in a Yellow string.
    FString Fill(const FString& Text) const;

    FPYRProgressSnapshot MakeSnapshot() const;
    void ApplySnapshot(const FPYRProgressSnapshot& S);
    void ResetToNewGame();

private:
    TSet<FName> Flags;
    TSet<FString> Taken;
    TSet<FString> Beaten;
};
