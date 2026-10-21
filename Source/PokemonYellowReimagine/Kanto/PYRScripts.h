// M4: Yellow's scripts from Pallet Town to Cerulean, on the Game Boy's tiles and flags.
// - People: talking runs their Yellow script (Mom, Oak, the Nurses, the Mart clerks, Daisy, the old man, Brock, Misty,
//   Bill, the gifts and the trade) or reads their text; trainers battle until beaten, then say their after-battle line.
// - Events (YellowData events.json): each one's conditions (EVENT_* flags, badges, items, money, Pikachu's happiness)
//   and trigger tiles decide when it runs; its text labels are said in Yellow's order.
// - Roadblocks: Oak stops Red at Route 1's grass without a Pokemon, the Viridian Mart gives Oak's Parcel and sells
//   nothing until it is delivered, the old man lies across Viridian's north road until the Pokedex, the Pewter boy
//   walks Red to the gym until Brock is beaten, and the Route 22 gate guard wants the Boulder Badge.
// - Trainers see Red along their facing within `sight` tiles (no solid tile, edge or person between): '!', the
//   trainer-spot sound and meet theme, a walk up tile by tile, their line, the battle on a closed stage, the prize
//   (base money x the last Pokemon's level), and never again.
// - Who is shown follows the flags (Refresh): Oak in the lab once he led Red there, the lying old man until the
//   Pokedex, the fossils until one is taken, Bill as a Pokemon until he asks for help...
#pragma once

#include "CoreMinimal.h"
#include "Battle/PYRBattleTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Yellow/YellowData.h"
#include "PYRScripts.generated.h"

class APYRPerson;
class APYRSignpost;
class UPYRScripts;
struct FPYRFight;

// A script: steps run one after another; each step calls Next when it is done. Red is locked while it runs.
class POKEMONYELLOWREIMAGINE_API FPYRSeq : public TSharedFromThis<FPYRSeq>
{
public:
    using FStep = TFunction<void(TFunction<void()>)>;
    TWeakObjectPtr<UPYRScripts> Owner;
    FString Name;
    TArray<FStep> Steps;
    int32 At = 0;
    bool bLock = true, bRunning = false;
    TFunction<void()> OnEnd;

    FPYRSeq& Then(FStep Step) { Steps.Add(MoveTemp(Step)); return *this; }
    FPYRSeq& Do(TFunction<void()> F);
    // A text.json label, a system line, or a literal string.
    FPYRSeq& Say(const FString& LabelOrText);
    FPYRSeq& Ask(const FString& LabelOrText, TFunction<void(bool)> OnAnswer);
    FPYRSeq& Wait(float Seconds);
    FPYRSeq& WaitFor(TFunction<bool()> Cond);
    FPYRSeq& WalkPerson(APYRPerson* Person, const TArray<EYDir>& Steps);
    FPYRSeq& WalkRed(const TArray<EYDir>& Steps);
    FPYRSeq& FaceRed(EYDir Dir);
    FPYRSeq& Fade(float Alpha, float Seconds);
    FPYRSeq& Fight(TFunction<void(FPYRFight&)> Setup, TFunction<void(EPYRBattleOutcome)> After = nullptr);
    FPYRSeq& Jingle(FName Id);
    FPYRSeq& Music(FName Id);
    // Give an item with Yellow's "<PLAYER> got X!" (Verb) and the jingle.
    // With TextId, Yellow's own line for the gift (its item name filled in).
    FPYRSeq& Give(FName Item, int32 Count, const FString& Verb = TEXT("got"), const FString& TextId = FString());
    // Into the bag with the jingle and no words (the line that goes with it is said by the script).
    FPYRSeq& GiveQuiet(FName Item, int32 Count);
    FPYRSeq& GiveMon(FName Species, int32 Level);
    void Run();
    void Next();
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRScripts : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UPYRScripts* Get(const UObject* Ctx);
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPYRScripts, STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

    void Start();
    // Show and hide people by the flags.
    void Refresh();
    bool Talk(APYRPerson* Person);
    bool ReadSign(APYRSignpost* Sign);
    bool UseTile(FIntPoint Kanto);
    TSharedRef<FPYRSeq> Seq(const FString& Name);
    bool IsRunning() const { return Running.IsValid(); }
    const FString& RunningName() const { static FString None; return Running.IsValid() ? Running->Name : None; }

    // ---- Events (events.json)
    TSharedPtr<FJsonObject> Event(const FString& Id) const;
    bool Check(const TSharedPtr<FJsonObject>& Event) const;
    void ApplySets(const TSharedPtr<FJsonObject>& Event);
    // A text.json label, else a system line (PYRSysText), else the string itself.
    FString Line(const FString& LabelOrText) const;

    // ---- Trainers
    // The fight for a trainer person (party, prize, stage, texts).
    bool TrainerFight(APYRPerson* Trainer, FPYRFight& Out) const;
    // Start a trainer's challenge (walk up when bWalk, then the line and the battle).
    void Challenge(APYRPerson* Trainer, bool bWalk);
    // Tests switch trainers' sight off to cross a route quietly.
    bool bSight = true;

    // Evidence for tests.
    TArray<FString> Ran;          // script names in the order they started
    int32 Spotted = 0;
    FString LastSpotted;

    // Used by FPYRSeq.
    void Began(TSharedRef<FPYRSeq> S);
    void Ended(TSharedRef<FPYRSeq> S);
    struct FWaiter { TFunction<bool()> Cond; TFunction<void()> Then; };
    TArray<FWaiter> Waiters;

private:
    void OnTile(FName Map, FIntPoint Local);
    void OnMap(FName From, FName To);
    bool RunEventAt(FName Map, FIntPoint Local);
    bool RunEvent(const FString& Id, APYRPerson* Talker);
    void CheckSight();
    TSharedPtr<FPYRSeq> Running;
    TArray<TSharedPtr<FPYRSeq>> Outer;   // sequences waiting on a nested one (held so they can finish)
    bool bStarted = false;
    // People whose battles are story events (gym leaders, the rival, Jessie and James): no generic sight line.
    TSet<FString> EventNpcs;
};

namespace PYRScripts
{
    POKEMONYELLOWREIMAGINE_API void Start(UWorld* World);
    POKEMONYELLOWREIMAGINE_API void Refresh(const UObject* Ctx);
    POKEMONYELLOWREIMAGINE_API bool Talk(UWorld* World, APYRPerson* Person);
    POKEMONYELLOWREIMAGINE_API bool ReadSign(UWorld* World, APYRSignpost* Sign);
    POKEMONYELLOWREIMAGINE_API bool UseTile(UWorld* World, FIntPoint Kanto);
}
