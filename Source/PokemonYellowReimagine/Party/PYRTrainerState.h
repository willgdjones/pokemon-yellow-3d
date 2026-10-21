// Everything Red carries between fights: the party, Bill's storage (12 boxes of 20), money, Yellow's 20-slot bag,
// the Pokedex and Pikachu's friendship. One per game instance, so it survives every battle; Save/PYRSave writes it.
// m4: item ids are YKey::Item keys ("pokeball", "potion", "oaksparcel", "tm34"); the bag keeps Yellow's order.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "../Battle/PYRBattleTypes.h"
#include "PYRSpecies.h"
#include "PYRTrainerState.generated.h"

class FYellowDataSet;
// The loaded Yellow tables that price items and mark key items (null until the game mode loads them).
extern POKEMONYELLOWREIMAGINE_API const FYellowDataSet* GYellowPrices;

UENUM(BlueprintType)
enum class EPYRBond : uint8 { None, Friendly, Close, BestFriends };

// Kanto's eight badges in Yellow's order. Worker `core` reads HasBadge(Boulder) for the Pewter exit; worker
// `encounters` calls AwardBadge(Cascade) when Misty is beaten.
UENUM(BlueprintType)
enum class EPYRBadge : uint8 { Boulder, Cascade, Thunder, Rainbow, Soul, Marsh, Volcano, Earth, Count UMETA(Hidden) };

// One bag or PC slot.
USTRUCT(BlueprintType)
struct FPYRItemStack
{
    GENERATED_BODY()
    UPROPERTY() FName Item;   // YKey::Item
    UPROPERTY() int32 Count = 0;
};

USTRUCT(BlueprintType)
struct FPYRMon
{
    GENERATED_BODY()
    // The original trainer. Red's own Pokemon (starter, catches, gifts such as the Route 24 Charmander and the
    // Cerulean Bulbasaur) say "Red"; a traded Pokemon names someone else and may disobey (see PYRObedience.h).
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName OriginalTrainer = FName(TEXT("Red"));
    bool IsTraded() const { return !OriginalTrainer.IsNone() && OriginalTrainer != FName(TEXT("Red")); }
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Species;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Level = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Xp = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Hp = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxHp = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EPYRStatus Status = EPYRStatus::None;
    // Every move it has known (the loadout panel can swap back to them).
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> Learned;
    // Up to 4 moves bound to keys 1-4, in order.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> Carried;
    // Moves it tried to learn with four already known: the move-loadout prompt asks which one to forget.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> Pending;
    // m4: the nickname (empty: the species name), the original trainer's ID No., the PC box it sits in (storage only)
    // and the species it evolves into once the evolution scene has played (Gen 1 evolves after the battle).
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Nickname;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 OtId = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString OtName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Box = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName EvolvePending;

    bool IsAble() const { return Hp > 0.f; }
};

// The m3 task's name for a party Pokemon.
using FPYRMonInstance = FPYRMon;

// Everything UPYRTrainerState keeps, as one saveable value (the save seam for worker `core`: every field is a
// UPROPERTY, so the save-slot library serialises it). Version 1 is m3's layout.
USTRUCT(BlueprintType)
struct FPYRTrainerSnapshot
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Version = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FPYRMon> Party;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FPYRMon> Storage;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CurrentBox = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Money = 3000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FPYRItemStack> Bag;
    // Badge names ("Boulder", "Cascade"...; see UPYRTrainerState::BadgeName).
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> Badges;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> Seen;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> Caught;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName LastCentre = NAME_None;
    // Pikachu's friendship (Yellow's 0-255).
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 BondPoints = 90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PikachuMood = 128;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bStarterGiven = false;
};

// What a level-up did (for the HUD banner and captions).
struct FPYRGrowthEvent
{
    int32 NewLevel = 0;
    TArray<FName> Learned;   // Moves added to the four.
    TArray<FName> Pending;   // Moves waiting for the forget prompt.
    FName EvolvedFrom, EvolvedInto;
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRTrainerState : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    static UPYRTrainerState* Get(const UObject* Ctx);

    UPROPERTY(BlueprintReadOnly) TArray<FPYRMon> Party;    // At most 6.
    // Bill's PC: every stored Pokemon, each in its FPYRMon::Box (12 boxes of 20).
    UPROPERTY(BlueprintReadOnly) TArray<FPYRMon> Storage;
    UPROPERTY(BlueprintReadWrite) int32 CurrentBox = 0;
    static constexpr int32 NumBoxes = 12;
    static constexpr int32 BoxSize = 20;
    static constexpr int32 BagSlots = 20;
    int32 BoxCount(int32 Box) const;
    // Indices into Storage of one box's Pokemon, in order.
    TArray<int32> BoxIndices(int32 Box) const;
    // Put a Pokemon in the current box (or the next one with room); false when every box is full.
    bool Deposit(const FPYRMon& Mon);
    UPROPERTY(BlueprintReadWrite) int32 Money = 3000;
    UPROPERTY(BlueprintReadOnly) TSet<FName> Seen;
    UPROPERTY(BlueprintReadOnly) TSet<FName> Caught;
    // The last Pokemon Centre used; NAME_None means Red's house.
    UPROPERTY(BlueprintReadWrite) FName LastCentre = NAME_None;
    // Yellow's bag: up to 20 slots in order, 99 of each (key items one).
    UPROPERTY(BlueprintReadOnly) TArray<FPYRItemStack> Bag;
    // Set a count directly (tests, the bag's TOSS); 0 removes the slot.
    void SetItemCount(FName Item, int32 Count);
    // True when the item can join the bag (a slot already holds it, or a slot is free).
    bool HasRoomFor(FName Item) const;
    // Move a slot up or down the list (the bag's SELECT).
    void SwapBagSlots(int32 A, int32 B);
    // Pikachu's friendship, Yellow's 0-255 scale (UPYRBond::GetFriendship reads it).
    UPROPERTY(BlueprintReadWrite) int32 BondPoints = 90;
    // Pikachu's mood (Yellow 0-255, 128 neutral): events pull it toward their target (Partner/PYRBond).
    UPROPERTY(BlueprintReadWrite) int32 PikachuMood = 128;

    // ---- The `journey` seam
    void AddMoney(int32 Amount) { Money = FMath::Max(0, Money + Amount); }
    // Full HP, status cleared (a Pokemon Centre, a blackout wake-up, the lab battle).
    void HealParty();
    bool AddItem(FName Item, int32 Count = 1);
    bool RemoveItem(FName Item, int32 Count = 1);
    int32 ItemCount(FName Item) const;
    bool HasItem(FName Item) const { return ItemCount(Item) > 0; }
    // Adds a Pokemon to the party, or to Bill's storage when the party is full. Returns true for the party.
    bool AddMon(const FPYRMonSpec& Spec);
    void MarkSeen(FName Species) { if (!Species.IsNone()) Seen.Add(Species); }
    void MarkCaught(FName Species) { if (!Species.IsNone()) { Seen.Add(Species); Caught.Add(Species); } }

    static const FName PokeBall;
    static const FName Potion;
    static const FName OaksParcel;
    static const FName Tm34;
    static const FName BoulderBadge;  // m1 name: badges are not bag items; kept so HasItem(BoulderBadge) reads the badge.

    // ---- Party helpers
    // A fresh Pokemon from a spec: Yellow stats at its level, the spec's moves or Yellow's last four learned.
    static FPYRMon MakeMon(const FPYRMonSpec& Spec);
    static FPYRMon MakeMon(FName Species, int32 Level);
    bool AddMonValue(const FPYRMon& Mon);
    int32 FirstAble() const;
    bool IsPartyAtFull() const;
    // Gen 1 experience; levels learn Yellow moves (a fifth waits in Pending). An evolution waits in EvolvePending
    // until the evolution scene runs it (ApplyEvolution) or is cancelled (Back).
    int32 GiveXp(FPYRMon& Mon, int32 Amount, TArray<FPYRGrowthEvent>* Events = nullptr);
    // m4: turn a party Pokemon into Into (the scene's end, or a Moon Stone): species, stats and the new form's moves
    // at its level (a fifth waits in Pending). Returns false when it can't.
    bool ApplyEvolution(int32 PartyIndex, FName Into);
    // Party members waiting for their evolution scene.
    int32 FirstEvolving() const;
    // The name shown for a Pokemon: its nickname, else the species name in Game Boy caps.
    static FString DisplayName(const FPYRMon& Mon);
    // (m4t: the m3 ResolvePending is gone: only PYRMoveLearning's confirmed prompt replaces a known move.)
    // Party members with a move waiting for the forget prompt.
    int32 FirstPending() const;

    // ---- Badges (m3). The FName versions below (HasBadge("Boulder"), AddBadge) still work and share the same set.
    void AwardBadge(EPYRBadge Badge);
    bool HasBadge(EPYRBadge Badge) const;
    int32 BadgeCount() const;
    static FName BadgeName(EPYRBadge Badge);
    // Yellow's obedience level: traded Pokemon obey up to 10 with no badge, 30 with Cascade, 50 with Rainbow, 70 with
    // Marsh, and at every level with Earth (101).
    int32 ObedienceLevel() const;
    // Yellow's badge boosts for Red's Pokemon in battle: Boulder raises Attack, Thunder Defense, Soul Speed and
    // Volcano Special, each by 1/8 (x1.125). 1 when the badge isn't held.
    float BadgeBoost(EPYRStat Stat) const;

    // ---- Save seam for worker `core`: the whole state as one value, and back.
    FPYRTrainerSnapshot MakeSnapshot() const;
    void ApplySnapshot(const FPYRTrainerSnapshot& Snapshot);

    // ---- Friendship (Yellow scale; tiers for the battle abilities)
    void AddBond(int32 Points) { BondPoints = FMath::Clamp(BondPoints + Points, 0, 255); }
    EPYRBond GetBond() const { return BondFor(BondPoints); }
    static EPYRBond BondFor(int32 Friendship);
    static int32 BondThreshold(EPYRBond Tier);

    // ---- m1 helpers other folders still call
    int32 CaughtCount() const;
    bool HasBadge(FName Badge) const;
    void AddBadge(FName Badge);
    void GiveStarter();
    bool HasStarter() const;
    void HealAll() { HealParty(); }
    void DebugHurtParty(float Fraction);
    void SetLastCentre(FName Centre) { LastCentre = Centre; }
    void HalveMoney() { Money /= 2; }
    bool Buy(FName Item);
    static int32 Price(FName Item);
    void DebugAddCaught(FName Species, int32 Level);
    bool AddCaughtMon(const FPYRMon& Mon) { MarkCaught(Mon.Species); return AddMonValue(Mon); }
    void DebugSetParty(const TArray<FPYRFoe>& Mons);
    void DebugSetParty(const TArray<FPYRMonSpec>& Mons);
    UPROPERTY(BlueprintReadOnly) TSet<FName> Badges;
    // Clear everything (a new game).
    void ResetToNewGame();

    // `journey` broadcasts it after a blackout wake-up; Pikachu listens.
    FSimpleMulticastDelegate OnBlackoutWake;

private:
    UPROPERTY() bool bStarterGiven = false;
    static FName NormalItem(FName Item);
};
