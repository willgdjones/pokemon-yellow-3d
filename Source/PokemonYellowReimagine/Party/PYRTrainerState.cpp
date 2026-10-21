#include "PYRTrainerState.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "PYRSpecies.h"
#include "Yellow/YellowData.h"

// The loaded tables that price items and mark key items (set by the game mode; null: the built-in prices).
const FYellowDataSet* GYellowPrices = nullptr;

const FName UPYRTrainerState::PokeBall(TEXT("pokeball"));
const FName UPYRTrainerState::Potion(TEXT("potion"));
const FName UPYRTrainerState::OaksParcel(TEXT("oaksparcel"));
const FName UPYRTrainerState::Tm34(TEXT("tm34"));
const FName UPYRTrainerState::BoulderBadge(TEXT("boulderbadge"));

UPYRTrainerState* UPYRTrainerState::Get(const UObject* Ctx)
{
    const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    return GI ? GI->GetSubsystem<UPYRTrainerState>() : nullptr;
}

// ------------------------------------------------------------------------------------------------ items
FName UPYRTrainerState::NormalItem(FName Item)
{
    // Every spelling ("poke-ball", "POKE_BALL", "Potion") to its YKey::Item key.
    return YKey::Item(Item.ToString());
}

namespace
{
// Key items take one slot and never stack (items.json's `key` when loaded).
bool IsKeyItem(FName Item)
{
    if (GYellowPrices) if (const FYItem* I = GYellowPrices->Item(Item)) return I->bKey;
    static const TSet<FName> Keys = {TEXT("oaksparcel"), TEXT("townmap"), TEXT("bicycle"), TEXT("bikevoucher"), TEXT("ssticket"), TEXT("oldamber"),
        TEXT("domefossil"), TEXT("helixfossil"), TEXT("pokeflute"), TEXT("oldrod"), TEXT("goodrod"), TEXT("superrod")};
    return Keys.Contains(Item) || Item.ToString().StartsWith(TEXT("hm"));
}
}

bool UPYRTrainerState::HasRoomFor(FName Item) const
{
    Item = NormalItem(Item);
    for (const FPYRItemStack& S : Bag) if (S.Item == Item) return true;
    return Bag.Num() < BagSlots;
}

bool UPYRTrainerState::AddItem(FName Item, int32 Count)
{
    Item = NormalItem(Item);
    if (Item.IsNone() || Count <= 0) return false;
    // A badge given as an item (older story code) is a badge.
    if (Item == BoulderBadge) { AddBadge(TEXT("Boulder")); return true; }
    for (FPYRItemStack& S : Bag)
        if (S.Item == Item)
        {
            S.Count = IsKeyItem(Item) ? 1 : FMath::Min(99, S.Count + Count);
            return true;
        }
    if (Bag.Num() >= BagSlots) return false;
    Bag.Add(FPYRItemStack{Item, IsKeyItem(Item) ? 1 : FMath::Min(99, Count)});
    return true;
}

bool UPYRTrainerState::RemoveItem(FName Item, int32 Count)
{
    Item = NormalItem(Item);
    if (Count <= 0) return false;
    for (int32 I = 0; I < Bag.Num(); ++I)
    {
        if (Bag[I].Item != Item) continue;
        if (Bag[I].Count < Count) return false;
        Bag[I].Count -= Count;
        if (Bag[I].Count <= 0) Bag.RemoveAt(I);
        return true;
    }
    return false;
}

int32 UPYRTrainerState::ItemCount(FName Item) const
{
    Item = NormalItem(Item);
    if (Item == BoulderBadge) return HasBadge(EPYRBadge::Boulder) ? 1 : 0;
    for (const FPYRItemStack& S : Bag) if (S.Item == Item) return S.Count;
    return 0;
}

void UPYRTrainerState::SetItemCount(FName Item, int32 Count)
{
    Item = NormalItem(Item);
    if (Item == BoulderBadge) { if (Count <= 0) Badges.Remove(TEXT("Boulder")); else AddBadge(TEXT("Boulder")); return; }
    for (int32 I = 0; I < Bag.Num(); ++I)
        if (Bag[I].Item == Item)
        {
            if (Count <= 0) Bag.RemoveAt(I);
            else Bag[I].Count = FMath::Min(99, Count);
            return;
        }
    if (Count > 0) AddItem(Item, Count);
}

void UPYRTrainerState::SwapBagSlots(int32 A, int32 B)
{
    if (Bag.IsValidIndex(A) && Bag.IsValidIndex(B) && A != B) Bag.Swap(A, B);
}

bool UPYRTrainerState::HasBadge(FName Badge) const
{
    return Badges.Contains(Badge);
}

void UPYRTrainerState::AddBadge(FName Badge)
{
    Badges.Add(Badge);
}

FName UPYRTrainerState::BadgeName(EPYRBadge Badge)
{
    static const FName Names[] = {TEXT("Boulder"), TEXT("Cascade"), TEXT("Thunder"), TEXT("Rainbow"), TEXT("Soul"), TEXT("Marsh"), TEXT("Volcano"), TEXT("Earth")};
    const int32 I = int32(Badge);
    return I >= 0 && I < UE_ARRAY_COUNT(Names) ? Names[I] : NAME_None;
}

void UPYRTrainerState::AwardBadge(EPYRBadge Badge)
{
    const FName Name = BadgeName(Badge);
    if (!Name.IsNone()) AddBadge(Name);
}

bool UPYRTrainerState::HasBadge(EPYRBadge Badge) const
{
    const FName Name = BadgeName(Badge);
    return !Name.IsNone() && HasBadge(Name);
}

int32 UPYRTrainerState::BadgeCount() const
{
    int32 N = 0;
    for (int32 I = 0; I < int32(EPYRBadge::Count); ++I) N += HasBadge(EPYRBadge(I)) ? 1 : 0;
    return N;
}

int32 UPYRTrainerState::ObedienceLevel() const
{
    // source: Yellow (pret/pokeyellow engine/battle/core.asm, CheckForDisobedience).
    if (HasBadge(EPYRBadge::Earth)) return 101;
    if (HasBadge(EPYRBadge::Marsh)) return 70;
    if (HasBadge(EPYRBadge::Rainbow)) return 50;
    if (HasBadge(EPYRBadge::Cascade)) return 30;
    return 10;
}

float UPYRTrainerState::BadgeBoost(EPYRStat Stat) const
{
    // source: Yellow (engine/battle/core.asm, ApplyBadgeStatBoosts): stat + stat / 8 for each badge's stat.
    switch (Stat)
    {
    case EPYRStat::Attack: return HasBadge(EPYRBadge::Boulder) ? 1.125f : 1.f;
    case EPYRStat::Defence: return HasBadge(EPYRBadge::Thunder) ? 1.125f : 1.f;
    case EPYRStat::Speed: return HasBadge(EPYRBadge::Soul) ? 1.125f : 1.f;
    case EPYRStat::Special: return HasBadge(EPYRBadge::Volcano) ? 1.125f : 1.f;
    default: return 1.f;
    }
}

FPYRTrainerSnapshot UPYRTrainerState::MakeSnapshot() const
{
    FPYRTrainerSnapshot S;
    S.Party = Party;
    S.Storage = Storage;
    S.CurrentBox = CurrentBox;
    S.Money = Money;
    S.Bag = Bag;
    S.Badges = Badges.Array();
    S.Seen = Seen.Array();
    S.Caught = Caught.Array();
    S.LastCentre = LastCentre;
    S.BondPoints = BondPoints;
    S.PikachuMood = PikachuMood;
    S.bStarterGiven = bStarterGiven;
    return S;
}

void UPYRTrainerState::ApplySnapshot(const FPYRTrainerSnapshot& S)
{
    Party = S.Party;
    if (Party.Num() > 6) Party.SetNum(6);
    Storage = S.Storage;
    CurrentBox = FMath::Clamp(S.CurrentBox, 0, NumBoxes - 1);
    Money = FMath::Clamp(S.Money, 0, 999999);
    Bag = S.Bag;
    if (Bag.Num() > BagSlots) Bag.SetNum(BagSlots);
    Badges = TSet<FName>(S.Badges);
    Seen = TSet<FName>(S.Seen);
    Caught = TSet<FName>(S.Caught);
    LastCentre = S.LastCentre;
    BondPoints = FMath::Clamp(S.BondPoints, 0, 255);
    PikachuMood = FMath::Clamp(S.PikachuMood, 0, 255);
    bStarterGiven = S.bStarterGiven;
}

int32 UPYRTrainerState::Price(FName Item)
{
    Item = NormalItem(Item);
    // Yellow's prices from items.json; the two the battle needs stand in when YellowData is missing.
    if (GYellowPrices) if (const FYItem* I = GYellowPrices->Item(Item)) return I->Price;
    if (Item == Potion) return 300;
    if (Item == PokeBall) return 200;
    return -1;
}

bool UPYRTrainerState::Buy(FName Item)
{
    const int32 Cost = Price(Item);
    if (Cost < 0 || Money < Cost) return false;
    AddMoney(-Cost);
    AddItem(Item);
    return true;
}

// ------------------------------------------------------------------------------------------------ party
FPYRMon UPYRTrainerState::MakeMon(const FPYRMonSpec& Spec)
{
    FPYRMon Mon;
    Mon.Species = Spec.Species;
    Mon.Level = FMath::Clamp(Spec.Level, 1, 100);
    Mon.Xp = PYRData::XpForLevel(Spec.Species, Mon.Level);
    if (const FPYRSpeciesInfo* S = PYRData::Species(Spec.Species)) Mon.MaxHp = PYRData::MaxHp(*S, Mon.Level);
    Mon.Hp = Mon.MaxHp;
    Mon.Carried = PYRSpecies::MovesFor(Spec);
    Mon.Learned = Mon.Carried;
    return Mon;
}

FPYRMon UPYRTrainerState::MakeMon(FName Species, int32 Level)
{
    FPYRMonSpec Spec;
    Spec.Species = Species;
    Spec.Level = Level;
    return MakeMon(Spec);
}

bool UPYRTrainerState::AddMonValue(const FPYRMon& Mon)
{
    MarkSeen(Mon.Species);
    if (Party.Num() < 6) { Party.Add(Mon); return true; }
    Deposit(Mon);
    return false;
}

int32 UPYRTrainerState::BoxCount(int32 InBox) const
{
    int32 N = 0;
    for (const FPYRMon& M : Storage) N += M.Box == InBox ? 1 : 0;
    return N;
}

TArray<int32> UPYRTrainerState::BoxIndices(int32 InBox) const
{
    TArray<int32> Out;
    for (int32 I = 0; I < Storage.Num(); ++I) if (Storage[I].Box == InBox) Out.Add(I);
    return Out;
}

bool UPYRTrainerState::Deposit(const FPYRMon& Mon)
{
    for (int32 Step = 0; Step < NumBoxes; ++Step)
    {
        const int32 Box = (CurrentBox + Step) % NumBoxes;
        if (BoxCount(Box) >= BoxSize) continue;
        FPYRMon Copy = Mon;
        Copy.Box = Box;
        Storage.Add(Copy);
        return true;
    }
    return false;
}

FString UPYRTrainerState::DisplayName(const FPYRMon& Mon)
{
    if (!Mon.Nickname.IsEmpty()) return Mon.Nickname;
    const FPYRSpeciesInfo* S = PYRData::Species(Mon.Species);
    if (S && !S->GbName.IsEmpty()) return S->GbName;
    return (S ? S->Name.ToString() : Mon.Species.ToString()).ToUpper();
}

int32 UPYRTrainerState::FirstEvolving() const
{
    for (int32 I = 0; I < Party.Num(); ++I) if (!Party[I].EvolvePending.IsNone() && Party[I].Hp > 0) return I;
    return INDEX_NONE;
}

bool UPYRTrainerState::ApplyEvolution(int32 PartyIndex, FName Into)
{
    if (!Party.IsValidIndex(PartyIndex) || !PYRData::Species(Into)) return false;
    FPYRMon& Mon = Party[PartyIndex];
    const FName From = Mon.Species;
    Mon.Species = Into;
    Mon.EvolvePending = NAME_None;
    MarkCaught(Into);
    if (const FPYRSpeciesInfo* S = PYRData::Species(Into))
    {
        const float NewMax = PYRData::MaxHp(*S, Mon.Level);
        Mon.Hp = FMath::Max(Mon.Hp > 0.f ? 1.f : 0.f, Mon.Hp + NewMax - Mon.MaxHp);
        Mon.MaxHp = NewMax;
    }
    // Gen 1: after evolving, the new form learns its own moves for the current level.
    for (const FName& Move : PYRData::MovesLearned(Into, Mon.Level - 1, Mon.Level))
    {
        if (Mon.Carried.Contains(Move) || Mon.Pending.Contains(Move)) continue;
        Mon.Learned.AddUnique(Move);
        if (Mon.Carried.Num() < 4) Mon.Carried.Add(Move);
        else Mon.Pending.Add(Move);
    }
    UE_LOG(LogTemp, Display, TEXT("PYR_EVOLVED %s -> %s at level %d"), *From.ToString(), *Into.ToString(), Mon.Level);
    return true;
}

void UPYRTrainerState::ResetToNewGame()
{
    Party.Reset();
    Storage.Reset();
    CurrentBox = 0;
    Money = 3000;
    Seen.Reset();
    Caught.Reset();
    LastCentre = NAME_None;
    Bag.Reset();
    BondPoints = 90;
    PikachuMood = 128;
    Badges.Reset();
    bStarterGiven = false;
}

bool UPYRTrainerState::AddMon(const FPYRMonSpec& Spec)
{
    return AddMonValue(MakeMon(Spec));
}

void UPYRTrainerState::HealParty()
{
    for (FPYRMon& M : Party) { M.Hp = M.MaxHp; M.Status = EPYRStatus::None; }
}

bool UPYRTrainerState::IsPartyAtFull() const
{
    for (const FPYRMon& M : Party) if (M.Hp < M.MaxHp) return false;
    return true;
}

int32 UPYRTrainerState::FirstAble() const
{
    for (int32 i = 0; i < Party.Num(); ++i) if (Party[i].IsAble()) return i;
    return INDEX_NONE;
}

int32 UPYRTrainerState::FirstPending() const
{
    for (int32 i = 0; i < Party.Num(); ++i) if (Party[i].Pending.Num() > 0) return i;
    return INDEX_NONE;
}

int32 UPYRTrainerState::GiveXp(FPYRMon& Mon, int32 Amount, TArray<FPYRGrowthEvent>* Events)
{
    Mon.Xp += FMath::Max(0, Amount);
    int32 Gained = 0;
    while (Mon.Level < 100 && Mon.Xp >= PYRData::XpForLevel(Mon.Species, Mon.Level + 1))
    {
        ++Mon.Level;
        ++Gained;
        FPYRGrowthEvent Event;
        Event.NewLevel = Mon.Level;
        // m4: the evolution waits for its scene after the battle (Back cancels it; it asks again at the next level).
        const FName Into = PYRData::EvolutionAt(Mon.Species, Mon.Level);
        if (!Into.IsNone())
        {
            Event.EvolvedFrom = Mon.Species;
            Event.EvolvedInto = Into;
            Mon.EvolvePending = Into;
        }
        if (const FPYRSpeciesInfo* S = PYRData::Species(Mon.Species))
        {
            const float NewMax = PYRData::MaxHp(*S, Mon.Level);
            Mon.Hp = FMath::Max(0.f, Mon.Hp + NewMax - Mon.MaxHp);  // Levelling adds the HP difference, as in Gen 1.
            Mon.MaxHp = NewMax;
        }
        for (const FName& Move : PYRData::MovesLearned(Mon.Species, Mon.Level - 1, Mon.Level))
        {
            if (Mon.Carried.Contains(Move) || Mon.Pending.Contains(Move)) continue;
            Mon.Learned.AddUnique(Move);
            if (Mon.Carried.Num() < 4) { Mon.Carried.Add(Move); Event.Learned.Add(Move); }
            else { Mon.Pending.Add(Move); Event.Pending.Add(Move); }
        }
        if (Events) Events->Add(Event);
    }
    return Gained;
}

// ------------------------------------------------------------------------------------------------ friendship
EPYRBond UPYRTrainerState::BondFor(int32 Friendship)
{
    return Friendship >= BondThreshold(EPYRBond::BestFriends) ? EPYRBond::BestFriends
        : Friendship >= BondThreshold(EPYRBond::Close) ? EPYRBond::Close
        : Friendship >= BondThreshold(EPYRBond::Friendly) ? EPYRBond::Friendly : EPYRBond::None;
}

int32 UPYRTrainerState::BondThreshold(EPYRBond Tier)
{
    // Yellow's mood bands: grumpy below 100, content from 100, happy from 150, delighted from 200.
    switch (Tier)
    {
    case EPYRBond::Friendly: return 100;
    case EPYRBond::Close: return 150;
    case EPYRBond::BestFriends: return 200;
    default: return 0;
    }
}

// ------------------------------------------------------------------------------------------------ m1 helpers
int32 UPYRTrainerState::CaughtCount() const
{
    int32 Count = 0;
    for (const FName& S : Caught) if (S != PYRData::Pikachu) ++Count;
    return Count;
}

void UPYRTrainerState::GiveStarter()
{
    if (HasStarter()) return;
    bStarterGiven = true;
    Party.Insert(MakeMon(PYRData::Pikachu, 5), 0);
    MarkCaught(PYRData::Pikachu);
    BondPoints = 90;  // Yellow's starting friendship.
    // m4: no gift here; Oak gives Yellow's Poke Balls with the Pokedex (Kanto/PYRScripts).
}

bool UPYRTrainerState::HasStarter() const
{
    if (bStarterGiven) return true;
    for (const FPYRMon& M : Party) if (M.Species == PYRData::Pikachu) return true;
    return false;
}

void UPYRTrainerState::DebugHurtParty(float Fraction)
{
    for (FPYRMon& M : Party) M.Hp = FMath::Max(1.f, FMath::RoundToFloat(M.MaxHp * (1.f - FMath::Clamp(Fraction, 0.f, 1.f))));
}

void UPYRTrainerState::DebugAddCaught(FName Species, int32 Level)
{
    AddCaughtMon(MakeMon(Species, Level));
}

void UPYRTrainerState::DebugSetParty(const TArray<FPYRMonSpec>& Mons)
{
    // A test party stands for a Red who already left Oak's lab, even one without Pikachu in it.
    bStarterGiven = true;
    Party.Reset();
    for (const FPYRMonSpec& Spec : Mons)
    {
        if (Party.Num() >= 6) break;
        Party.Add(MakeMon(Spec));
        MarkCaught(Spec.Species);
    }
}

void UPYRTrainerState::DebugSetParty(const TArray<FPYRFoe>& Mons)
{
    TArray<FPYRMonSpec> Specs;
    for (const FPYRFoe& F : Mons) { FPYRMonSpec S; S.Species = F.Species; S.Level = F.Level; Specs.Add(S); }
    DebugSetParty(Specs);
}
