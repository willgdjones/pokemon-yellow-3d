// The Game Boy screens, part two: the bag and using items, the trainer card, SAVE, OPTION, the Mart, the PCs, naming,
// learning a move, the evolution scene, the Town Map, Pikachu's face, Oak's intro and the title.
#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/PYRField.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRScreensCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Music/GbMusicPlayer.h"
#include "Partner/PYRBond.h"
#include "Party/PYRBadgeCase.h"
#include "Party/PYRSpecies.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Save/PYRSave.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "World/PYRPaint.h"
#include "Yellow/YellowImages.h"

using namespace PYRGb;
using namespace PYRScreensCommon;
using namespace PYRScreens;

namespace
{
bool IsKey(const UObject* Ctx, FName Item)
{
    const FYellowDataSet* D = Data(Ctx);
    const FYItem* I = D ? D->Item(Item) : nullptr;
    return I ? I->bKey : Item == UPYRTrainerState::OaksParcel;
}

int32 PriceOf(const UObject* Ctx, FName Item)
{
    const FYellowDataSet* D = Data(Ctx);
    const FYItem* I = D ? D->Item(Item) : nullptr;
    return I ? I->Price : UPYRTrainerState::Price(Item);
}

// The bag's list: its slots and CANCEL, or a PC's.
class FItemListScreen : public FPYRScreen
{
public:
    TFunction<TArray<FPYRItemStack>()> Source;
    TFunction<void(int32, FName)> OnPick;    // index, item (-1/None: CANCEL or Back)
    FString Title;
    int32 Top = 0;
    int32 SelectFrom = INDEX_NONE;          // the bag's SELECT (move a slot)
    bool bBag = false;
    static constexpr int32 Rows = 6;
    TArray<FPYRItemStack> Items() const { return Source ? Source() : TArray<FPYRItemStack>(); }
    virtual TSharedRef<SWidget> Build() override
    {
        const TArray<FPYRItemStack> List = Items();
        TArray<FString> Lines;
        for (int32 I = Top; I < FMath::Min(List.Num() + 1, Top + Rows); ++I)
        {
            if (I == List.Num()) { Lines.Add(TEXT("CANCEL")); continue; }
            const bool bKeyItem = IsKey(GetWorld(), List[I].Item);
            Lines.Add(FString::Printf(TEXT("%s%s%s"), I == SelectFrom ? TEXT("▷") : TEXT(""), *ItemName(GetWorld(), List[I].Item),
                bKeyItem ? TEXT("") : *FString::Printf(TEXT("\n        ×%2d"), List[I].Count)));
        }
        TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
        if (!Title.IsEmpty()) Col->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 8))[PYRGb::Text(Title, 24)];
        Col->AddSlot().AutoHeight()[PYRGb::List(Lines, Cursor - Top, 26)];
        return Place(Box(Col), FVector2D(.40f, .08f), 0.f);
    }
    virtual bool Input(EPYRKey Key) override
    {
        const TArray<FPYRItemStack> List = Items();
        const int32 N = List.Num() + 1;
        switch (Key)
        {
        case EPYRKey::Up: Cursor = FMath::Max(0, Cursor - 1); break;
        case EPYRKey::Down: Cursor = FMath::Min(N - 1, Cursor + 1); break;
        case EPYRKey::Back:
            if (SelectFrom != INDEX_NONE) { SelectFrom = INDEX_NONE; Refresh(); return true; }
            { TFunction<void(int32, FName)> P = OnPick; Close(); if (P) P(-1, NAME_None); }
            return true;
        case EPYRKey::Confirm:
            if (SelectFrom != INDEX_NONE)
            {
                if (UPYRTrainerState* S = State(GetWorld())) if (Cursor < List.Num()) S->SwapBagSlots(SelectFrom, Cursor);
                SelectFrom = INDEX_NONE;
                Refresh();
                return true;
            }
            if (Cursor >= List.Num()) { TFunction<void(int32, FName)> P = OnPick; Close(); if (P) P(-1, NAME_None); return true; }
            if (OnPick) OnPick(Cursor, List[Cursor].Item);
            return true;
        default: return true;
        }
        if (Cursor < Top) Top = Cursor;
        if (Cursor >= Top + Rows) Top = Cursor - Rows + 1;
        Refresh();
        return true;
    }
    virtual void OnReveal() override { const int32 N = Items().Num() + 1; Cursor = FMath::Min(Cursor, N - 1); Top = FMath::Min(Top, Cursor); Refresh(); }
    virtual FString Describe() const override
    {
        FString Out = Title.IsEmpty() ? FString() : Title + TEXT("\n");
        const TArray<FPYRItemStack> List = Items();
        for (int32 I = 0; I <= List.Num(); ++I)
            Out += (I == Cursor ? TEXT("> ") : TEXT("  ")) + (I == List.Num() ? FString(TEXT("CANCEL")) : FString::Printf(TEXT("%s x%d"), *ItemName(GetWorld(), List[I].Item), List[I].Count)) + TEXT("\n");
        return Out;
    }
};

TSharedRef<FItemListScreen> BagList(const UObject* Ctx)
{
    TSharedRef<FItemListScreen> L = MakeShared<FItemListScreen>();
    L->Id = TEXT("bag");
    L->bBag = true;
    TWeakObjectPtr<const UObject> W = Ctx;
    L->Source = [W]() { const UPYRTrainerState* S = State(W.Get()); return S ? S->Bag : TArray<FPYRItemStack>(); };
    return L;
}
}

// ------------------------------------------------------------------------------------------------ bag
namespace PYRScreens
{
FString CardTime(const UObject* Ctx)
{
    const UPYRProgress* P = Prog(Ctx);
    const int32 Secs = P ? FMath::FloorToInt(P->PlaySeconds) : 0;
    return FString::Printf(TEXT("TIME/%d:%02d"), Secs / 3600, (Secs / 60) % 60);
}

void OpenBag(const UObject* Ctx)
{
    TSharedRef<FItemListScreen> L = BagList(Ctx);
    TWeakPtr<FItemListScreen> Weak = L;
    TWeakObjectPtr<const UObject> W = Ctx;
    L->OnPick = [Weak, W](int32 Index, FName Item)
    {
        if (Index < 0 || Item.IsNone()) return;
        // USE, TOSS or SELECT (move the slot).
        TSharedRef<FPYRListScreen> Sub = MakeList(TEXT("bag-sub"), {TEXT("USE"), TEXT("TOSS"), TEXT("SELECT")}, FVector2D(.72f, .56f), nullptr);
        TWeakPtr<FPYRListScreen> WeakSub = Sub;
        Sub->OnPick = [Weak, WeakSub, W, Index, Item](int32 Choice)
        {
            if (TSharedPtr<FPYRListScreen> S = WeakSub.Pin()) S->Close();
            const UObject* C = W.Get();
            if (!C) return;
            if (Choice == 0) UseItem(C, Item, [Weak]() { if (TSharedPtr<FItemListScreen> L = Weak.Pin()) L->OnReveal(); });
            else if (Choice == 1)
            {
                UPYRMenus* M = Menus(C);
                if (IsKey(C, Item)) { M->Say(Sys(TEXT("TooImportantToTossText"))); return; }
                const UPYRTrainerState* S = State(C);
                const int32 Have = S ? S->ItemCount(Item) : 0;
                AskQuantity(C, Have, 0, [W, Item](int32 Qty)
                {
                    const UObject* C2 = W.Get();
                    if (!C2 || Qty <= 0) return;
                    Menus(C2)->Ask(Sys(TEXT("IsItOKToTossItemText"), F1(TEXT("wStringBuffer"), ItemName(C2, Item))), [W, Item, Qty](bool bYes)
                    {
                        const UObject* C3 = W.Get();
                        if (!C3 || !bYes) return;
                        if (UPYRTrainerState* S3 = State(C3)) S3->RemoveItem(Item, Qty);
                        Menus(C3)->Say(Sys(TEXT("ThrewAwayItemText"), F1(TEXT("wNameBuffer"), ItemName(C3, Item))));
                    });
                });
            }
            else if (TSharedPtr<FItemListScreen> L = Weak.Pin()) { L->SelectFrom = Index; L->Refresh(); }
        };
        if (UPYRMenus* M = Menus(W.Get())) M->Push(Sub);
    };
    if (UPYRMenus* M = Menus(Ctx)) M->Push(L);
}

// ------------------------------------------------------------------------------------------------ using items
namespace
{
int32 HealAmount(const FString& K)
{
    if (K == TEXT("potion")) return 20;
    if (K == TEXT("superpotion")) return 50;
    if (K == TEXT("hyperpotion")) return 200;
    if (K == TEXT("maxpotion") || K == TEXT("fullrestore")) return 999;
    if (K == TEXT("freshwater")) return 50;
    if (K == TEXT("sodapop")) return 60;
    if (K == TEXT("lemonade")) return 80;
    return 0;
}

EPYRStatus CureFor(const FString& K, bool& bAll)
{
    bAll = K == TEXT("fullheal") || K == TEXT("fullrestore");
    if (K == TEXT("antidote")) return EPYRStatus::Poison;
    if (K == TEXT("parlyzheal")) return EPYRStatus::Paralysis;
    if (K == TEXT("burnheal")) return EPYRStatus::Burn;
    if (K == TEXT("awakening")) return EPYRStatus::Sleep;
    return EPYRStatus::None;
}
}

void UseItem(const UObject* Ctx, FName Item, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(Ctx);
    UPYRTrainerState* S = State(Ctx);
    UPYRProgress* P = Prog(Ctx);
    UPYRField* Field = UPYRField::Get(Ctx);
    if (!M || !S || !P) { if (OnDone) OnDone(); return; }
    const FString K = Item.ToString();
    const FString Name = ItemName(Ctx, Item);
    TWeakObjectPtr<const UObject> W = Ctx;
    auto NotTime = [M, OnDone]() { M->Say(Sys(TEXT("ItemUseNotTimeText")), OnDone); };
    auto NoEffect = [M, OnDone]() { M->Say(Sys(TEXT("ItemUseNoEffectText")), OnDone); };
    // Potions and status heals: on a chosen Pokemon.
    bool bAllCure = false;
    const EPYRStatus Cure = CureFor(K, bAllCure);
    const int32 Heal = HealAmount(K);
    if (Heal > 0 || Cure != EPYRStatus::None || bAllCure)
    {
        ChooseMon(Ctx, Sys(TEXT("PartyMenuItemUseText")), [W, Item, Heal, Cure, bAllCure, OnDone](int32 I)
        {
            const UObject* C = W.Get();
            UPYRTrainerState* S2 = State(C);
            UPYRMenus* M2 = Menus(C);
            if (!C || !S2 || !M2 || !S2->Party.IsValidIndex(I)) { if (OnDone) OnDone(); return; }
            FPYRMon& Mon = S2->Party[I];
            const FString MonName = UPYRTrainerState::DisplayName(Mon);
            FString Line;
            if (Mon.Hp > 0 && Heal > 0 && Mon.Hp < Mon.MaxHp)
            {
                const int32 Before = FMath::CeilToInt(Mon.Hp);
                Mon.Hp = FMath::Min(Mon.MaxHp, Mon.Hp + Heal);
                if (bAllCure) Mon.Status = EPYRStatus::None;
                Line = Sys(TEXT("PotionText"), F2(TEXT("wNameBuffer"), MonName, TEXT("wHPBarHPDifference"), FString::FromInt(FMath::CeilToInt(Mon.Hp) - Before)));
            }
            else if (Mon.Hp > 0 && Mon.Status != EPYRStatus::None && (bAllCure || Mon.Status == Cure))
            {
                const EPYRStatus Was = Mon.Status;
                Mon.Status = EPYRStatus::None;
                const TCHAR* Label = bAllCure ? TEXT("FullHealText") : Was == EPYRStatus::Poison ? TEXT("AntidoteText") : Was == EPYRStatus::Paralysis ? TEXT("ParlyzHealText")
                    : Was == EPYRStatus::Burn ? TEXT("BurnHealText") : TEXT("AwakeningText");
                Line = Sys(Label, F1(TEXT("wNameBuffer"), MonName));
            }
            if (Line.IsEmpty()) { M2->Say(Sys(TEXT("ItemUseNoEffectText")), OnDone); return; }
            S2->RemoveItem(Item, 1);
            if (Mon.Species == FName(TEXT("pikachu"))) PYRBond::Apply(C, TEXT("used_item"));
            M2->Say(Line, OnDone);
        });
        return;
    }
    // Rare Candy: a level.
    if (K == TEXT("rarecandy"))
    {
        ChooseMon(Ctx, Sys(TEXT("PartyMenuItemUseText")), [W, Item, OnDone](int32 I)
        {
            const UObject* C = W.Get();
            UPYRTrainerState* S2 = State(C);
            if (!C || !S2 || !S2->Party.IsValidIndex(I) || S2->Party[I].Level >= 100) { if (C) Menus(C)->Say(Sys(TEXT("ItemUseNoEffectText")), OnDone); return; }
            FPYRMon& Mon = S2->Party[I];
            S2->RemoveItem(Item, 1);
            S2->GiveXp(Mon, FMath::Max(0, PYRData::XpForLevel(Mon.Species, Mon.Level + 1) - Mon.Xp));
            if (Mon.Species == FName(TEXT("pikachu"))) PYRBond::Apply(C, TEXT("levelup"));
            Menus(C)->Say(Sys(TEXT("RareCandyText"), F2(TEXT("wNameBuffer"), UPYRTrainerState::DisplayName(Mon), TEXT("wCurEnemyLevel"), FString::FromInt(Mon.Level))), [W, OnDone]()
            {
                LearnPending(W.Get(), [W, OnDone]()
                {
                    APokemonYellowReimagineCharacter* Red = APokemonYellowReimagineCharacter::Find(W.Get());
                    const FVector At = Red ? Red->GetActorLocation() + Red->GetActorForwardVector() * 250.f - FVector(0, 0, 88.f) : FVector::ZeroVector;
                    EvolvePending(W.Get(), At, Red ? Red->GetActorRotation().Yaw + 180.f : 0.f, OnDone);
                });
            });
        });
        return;
    }
    // Repels: steps without weak wild Pokemon.
    if (K == TEXT("repel") || K == TEXT("superrepel") || K == TEXT("maxrepel"))
    {
        S->RemoveItem(Item, 1);
        P->RepelSteps = K == TEXT("repel") ? 100 : K == TEXT("superrepel") ? 200 : 250;
        M->Say(Sys(TEXT("ItemUseText001")) + TEXT("\n") + Sys(TEXT("ItemUseText002"), F1(TEXT("wStringBuffer"), Name)), OnDone);
        return;
    }
    // Escape Rope: out of a cave, back to the last Pokemon Centre's town.
    if (K == TEXT("escaperope"))
    {
        const FYMap* Map = Field ? Field->GetMap() : nullptr;
        if (!Map || !Map->bCave) { NotTime(); return; }
        S->RemoveItem(Item, 1);
        M->Say(Sys(TEXT("ItemUseText001")) + TEXT("\n") + Sys(TEXT("ItemUseText002"), F1(TEXT("wStringBuffer"), Name)), [W, OnDone]()
        {
            UPYRField* F = UPYRField::Get(W.Get());
            const UPYRTrainerState* S2 = State(W.Get());
            const AYellowWorld* World = F ? F->GetYellowWorld() : nullptr;
            const FYBuilding* Centre = World && S2 && !S2->LastCentre.IsNone() ? World->BuildingWithDoorTo(S2->LastCentre) : nullptr;
            if (F && World && Centre && Centre->Doors.Num())
            {
                const FIntPoint Out = Centre->Doors[0] + FIntPoint(0, 1);
                const FYMap* Town = World->MapAtKanto(Out);
                if (Town) F->PlaceRed(Town->Id, Out.X - Town->Origin.X, Out.Y - Town->Origin.Y, EYDir::Down);
            }
            else if (F) F->PlaceRed(TEXT("PALLET_TOWN"), 5, 6, EYDir::Down);
            if (OnDone) OnDone();
        });
        return;
    }
    // Evolution stones: a chosen Pokemon evolves (Pikachu refuses the Thunder Stone, as in Yellow).
    if (K.EndsWith(TEXT("stone")))
    {
        ChooseMon(Ctx, Sys(TEXT("PartyMenuItemUseText")), [W, Item, OnDone](int32 I)
        {
            const UObject* C = W.Get();
            UPYRTrainerState* S2 = State(C);
            UPYRMenus* M2 = Menus(C);
            if (!C || !S2 || !M2 || !S2->Party.IsValidIndex(I)) { if (OnDone) OnDone(); return; }
            const FPYRMon& Mon = S2->Party[I];
            if (Mon.Species == FName(TEXT("pikachu")) && Item == FName(TEXT("thunderstone")))
            {
                M2->Say(Sys(TEXT("RefusingText"), F1(TEXT("wNameBuffer"), UPYRTrainerState::DisplayName(Mon))), OnDone);
                return;
            }
            const FName Into = PYRData::EvolutionByItem(Mon.Species, Item);
            if (Into.IsNone()) { M2->Say(Sys(TEXT("ItemUseNoEffectText")), OnDone); return; }
            S2->RemoveItem(Item, 1);
            APokemonYellowReimagineCharacter* Red = APokemonYellowReimagineCharacter::Find(C);
            const FVector At = Red ? Red->GetActorLocation() + Red->GetActorForwardVector() * 250.f - FVector(0, 0, 88.f) : FVector::ZeroVector;
            Evolve(C, I, Into, At, Red ? Red->GetActorRotation().Yaw + 180.f : 0.f, [W, OnDone](bool) { LearnPending(W.Get(), OnDone); });
        });
        return;
    }
    // TMs and HMs: boot it up, teach a compatible Pokemon (the forget screen if it knows four).
    const FYellowDataSet* D = Data(Ctx);
    const FYItem* Info = D ? D->Item(Item) : nullptr;
    if (Info && (Info->Tm > 0 || Info->Hm > 0) && !Info->Move.IsNone())
    {
        const bool bHm = Info->Hm > 0;
        const int32 Number = bHm ? 50 + Info->Hm : Info->Tm;
        const FName Move = Info->Move;
        const FString Boot = Sys(bHm ? TEXT("BootedUpHMText") : TEXT("BootedUpTMText"));
        M->Say(Boot, [W, Item, Move, Number, bHm, OnDone]()
        {
            UPYRMenus* M2 = Menus(W.Get());
            if (!M2) return;
            M2->Ask(Sys(TEXT("TeachMachineMoveText"), F1(TEXT("wStringBuffer"), MoveName(Move))), [W, Item, Move, Number, bHm, OnDone](bool bYes)
            {
                if (!bYes) { if (OnDone) OnDone(); return; }
                ChooseMon(W.Get(), TEXT("Teach to which\nPOKéMON?"), [W, Item, Move, Number, bHm, OnDone](int32 I)
                {
                    const UObject* C = W.Get();
                    UPYRTrainerState* S2 = State(C);
                    if (!C || !S2 || !S2->Party.IsValidIndex(I)) { if (OnDone) OnDone(); return; }
                    FPYRMon& Mon = S2->Party[I];
                    if (!PYRData::CanLearnTm(Mon.Species, Number))
                    {
                        Menus(C)->Say(Sys(TEXT("MonCannotLearnMachineMoveText"), F2(TEXT("wNameBuffer"), UPYRTrainerState::DisplayName(Mon), TEXT("wStringBuffer"), MoveName(Move))), OnDone);
                        return;
                    }
                    if (Mon.Carried.Contains(Move))
                    {
                        Menus(C)->Say(FString::Printf(TEXT("%s knows\n%s!"), *UPYRTrainerState::DisplayName(Mon), *MoveName(Move)), OnDone);
                        return;
                    }
                    LearnMove(C, I, Move, [W, Item, bHm, I, OnDone](bool bLearned)
                    {
                        const UObject* C2 = W.Get();
                        if (bLearned && C2)
                        {
                            if (!bHm) if (UPYRTrainerState* S3 = State(C2)) S3->RemoveItem(Item, 1);
                            if (UPYRTrainerState* S3 = State(C2)) if (S3->Party.IsValidIndex(I) && S3->Party[I].Species == FName(TEXT("pikachu"))) PYRBond::Apply(C2, TEXT("used_tm_hm"));
                        }
                        if (OnDone) OnDone();
                    });
                });
            });
        });
        return;
    }
    if (K == TEXT("townmap")) { OpenTownMap(Ctx); if (OnDone) OnDone(); return; }
    NotTime();
}

// ------------------------------------------------------------------------------------------------ trainer card
namespace
{
class FCardScreen : public FPYRScreen
{
public:
    FSlateBrush Portrait, Cells[8];
    virtual bool IsFullScreen() const override { return true; }
    // A YellowData picture drawn at Size (nearest filtering keeps its Game Boy pixels), or nothing.
    TSharedRef<SWidget> Picture(FSlateBrush& Brush, const FString& File, FVector2D Size)
    {
        UYellowImages* Images = UYellowImages::Get(GetWorld());
        UTexture2D* Tex = Images ? Images->Load(File) : nullptr;
        if (!Tex) return SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y);
        Brush.SetResourceObject(Tex);
        Brush.ImageSize = Size;
        Brush.DrawAs = ESlateBrushDrawType::Image;
        return SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y)[SNew(SImage).Image(&Brush)];
    }
    virtual TSharedRef<SWidget> Build() override
    {
        const UPYRTrainerState* S = State(GetWorld());
        const UPYRProgress* P = Prog(GetWorld());
        const FString Top = FString::Printf(TEXT("NAME/%s\nMONEY/%s\n%s"), P ? *P->PlayerName : TEXT("RED"), *Money(S ? S->Money : 0), *PYRScreens::CardTime(GetWorld()));
        TSharedRef<SHorizontalBox> Row1 = SNew(SHorizontalBox), Row2 = SNew(SHorizontalBox);
        // The badge case (gfx/trainer_card/badges.png): each gym leader's face until his badge is won, then the badge.
        for (int32 I = 0; I < 8; ++I)
        {
            const bool bHave = S && S->HasBadge(EPYRBadge(I));
            const FString File = FString::Printf(TEXT("trainercard/%s_%d.png"), bHave ? TEXT("badge") : TEXT("face"), I);
            TSharedRef<SWidget> Pic = Picture(Cells[I], File, FVector2D(96, 96));
            TSharedRef<SVerticalBox> Cell = SNew(SVerticalBox);
            Cell->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(10, 6, 10, 0))[PYRGb::Text(FString::FromInt(I + 1), 20)];
            Cell->AddSlot().AutoHeight().Padding(FMargin(10, 0, 10, 6))[Pic];
            (I < 4 ? Row1 : Row2)->AddSlot().AutoWidth()[Cell];
        }
        return Page(SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Box(SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[PYRGb::Text(Top, 32)]
                + SHorizontalBox::Slot().AutoWidth()[Picture(Portrait, TEXT("trainercard/red.png"), FVector2D(168, 168))], 24.f)]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 30, 0, 10))[PYRGb::Text(TEXT("★BADGES★"), 28)]
            + SVerticalBox::Slot().AutoHeight()[Row1]
            + SVerticalBox::Slot().AutoHeight()[Row2]);
    }
    virtual bool Input(EPYRKey Key) override { if (Key == EPYRKey::Confirm || Key == EPYRKey::Back) Close(); return true; }
    virtual FString Describe() const override
    {
        const UPYRTrainerState* S = State(GetWorld());
        const UPYRProgress* P = Prog(GetWorld());
        FString Out = FString::Printf(TEXT("NAME %s MONEY %d BADGES"), P ? *P->PlayerName : TEXT("RED"), S ? S->Money : 0);
        for (int32 I = 0; I < 8; ++I) Out += S && S->HasBadge(EPYRBadge(I)) ? FString::Printf(TEXT(" %s"), *UPYRTrainerState::BadgeName(EPYRBadge(I)).ToString()) : FString();
        return Out;
    }
};
}

void OpenTrainerCard(const UObject* Ctx)
{
    TSharedRef<FCardScreen> S = MakeShared<FCardScreen>();
    S->Id = TEXT("trainer-card");
    if (UPYRMenus* M = Menus(Ctx)) M->Push(S);
}

// ------------------------------------------------------------------------------------------------ save
void OpenSave(const UObject* Ctx)
{
    UPYRMenus* M = Menus(Ctx);
    if (!M) return;
    TWeakObjectPtr<const UObject> W = Ctx;
    M->Ask(Sys(TEXT("WouldYouLikeToSaveText")), [W](bool bYes)
    {
        UPYRMenus* M2 = Menus(W.Get());
        if (!bYes || !M2) return;
        M2->Say(Sys(TEXT("SavingText")), [W]()
        {
            UPYRSave* Save = UPYRSave::Get(W.Get());
            FString Why;
            const bool bOk = Save && Save->SaveNow(TEXT("START menu"), &Why);
            if (UPYRMenus* M3 = Menus(W.Get()))
            {
                if (bOk || (Save && !Save->CanUseDisk())) { M3->Blip(TEXT("save")); M3->Say(Sys(TEXT("GameSavedText"))); }
                else
                {
                    // The Game Boy can't fail to save and has no words for it: the disk error goes to the log.
                    UE_LOG(LogYellow, Error, TEXT("Save failed: %s"), *Why);
                    M3->Blip(TEXT("denied"));
                }
            }
        });
    });
}

// ------------------------------------------------------------------------------------------------ options
namespace
{
class FOptionsScreen : public FPYRScreen
{
public:
    virtual bool IsFullScreen() const override { return true; }
    FString Rows() const
    {
        const UPYRProgress* P = Prog(GetWorld());
        const FPYROptions O = P ? P->Options : FPYROptions();
        // Yellow's one-page OPTION screen (engine/menus/options.asm): a label and its setting per row, then CANCEL.
        auto Row = [this](int32 Index, const TCHAR* Label, const TCHAR* Value)
        {
            return FString(Cursor == Index ? TEXT("▶") : TEXT(" ")) + Label + Value + TEXT("\n\n");
        };
        const TCHAR* Speed = O.TextSpeed == EPYRTextSpeed::Fast ? TEXT("FAST") : O.TextSpeed == EPYRTextSpeed::Medium ? TEXT("MID") : TEXT("SLOW");
        FString Out;
        Out += Row(0, TEXT("TEXT SPEED :"), Speed);
        Out += Row(1, TEXT("ANIMATION  :"), O.bBattleAnimation ? TEXT("ON") : TEXT("OFF"));
        Out += Row(2, TEXT("BATTLESTYLE:"), O.bBattleShift ? TEXT("SHIFT") : TEXT("SET"));
        Out += Row(3, TEXT("SOUND:"), O.bEarphones ? TEXT("EARPHONE1") : TEXT("MONO"));
        Out += Row(4, TEXT("PRINT:"), TEXT("NORMAL"));
        Out += FString(Cursor == 5 ? TEXT("▶") : TEXT(" ")) + TEXT("CANCEL");
        return Out;
    }
    virtual TSharedRef<SWidget> Build() override { return Page(Box(PYRGb::Text(Rows(), 30), 30.f)); }
    virtual bool Input(EPYRKey Key) override
    {
        UPYRProgress* P = Prog(GetWorld());
        if (!P) { Close(); return true; }
        FPYROptions& O = P->Options;
        switch (Key)
        {
        case EPYRKey::Up: Cursor = (Cursor + 5) % 6; break;
        case EPYRKey::Down: Cursor = (Cursor + 1) % 6; break;
        case EPYRKey::Left:
        case EPYRKey::Right:
        {
            const int32 D = Key == EPYRKey::Right ? 1 : -1;
            if (Cursor == 0) O.TextSpeed = EPYRTextSpeed((int32(O.TextSpeed) + D + 3) % 3);
            else if (Cursor == 1) O.bBattleAnimation = !O.bBattleAnimation;
            else if (Cursor == 2) O.bBattleShift = !O.bBattleShift;
            else if (Cursor == 3) O.bEarphones = !O.bEarphones;
            break;
        }
        case EPYRKey::Confirm: if (Cursor == 5) { Close(); return true; } break;
        case EPYRKey::Back: Close(); return true;
        default: return true;
        }
        Refresh();
        return true;
    }
    virtual FString Describe() const override { return Rows(); }
};
}

void OpenOptions(const UObject* Ctx)
{
    TSharedRef<FOptionsScreen> S = MakeShared<FOptionsScreen>();
    S->Id = TEXT("options");
    if (UPYRMenus* M = Menus(Ctx)) M->Push(S);
}

// ------------------------------------------------------------------------------------------------ Mart
namespace
{
void MartMenu(TWeakObjectPtr<const UObject> W, FName MartMap, TSharedPtr<FMoneyScreen> Money, TFunction<void()> OnDone);

void MartBuy(TWeakObjectPtr<const UObject> W, FName MartMap, TSharedPtr<FMoneyScreen> MoneyBox, TFunction<void()> OnDone)
{
    const UObject* C = W.Get();
    const FYellowDataSet* D = Data(C);
    const TArray<FName>* Stock = D ? D->Marts.Find(MartMap) : nullptr;
    TArray<FName> Items = Stock ? *Stock : TArray<FName>();
    if (!Stock) { Items = {UPYRTrainerState::PokeBall, UPYRTrainerState::Potion}; }
    TArray<FString> Rows;
    for (const FName& I : Items) Rows.Add(FString::Printf(TEXT("%s\n          %s"), *ItemName(C, I), *Money(PriceOf(C, I))));
    Rows.Add(TEXT("CANCEL"));
    TSharedRef<FPYRListScreen> L = MakeList(TEXT("mart-buy"), Rows, FVector2D(.36f, .08f), nullptr);
    TWeakPtr<FPYRListScreen> WeakL = L;
    L->OnBack = [W, MartMap, MoneyBox, OnDone, WeakL]()
    {
        if (TSharedPtr<FPYRListScreen> Me = WeakL.Pin()) Me->Close();
        MartMenu(W, MartMap, MoneyBox, OnDone);
    };
    L->OnPick = [W, Items, MartMap, MoneyBox, OnDone, WeakL](int32 I)
    {
        if (!Items.IsValidIndex(I)) { if (TSharedPtr<FPYRListScreen> Me = WeakL.Pin()) Me->OnBack(); return; }
        const FName Item = Items[I];
        const int32 Price = PriceOf(W.Get(), Item);
        const UPYRTrainerState* S = State(W.Get());
        const int32 CanAfford = Price > 0 && S ? FMath::Clamp(S->Money / Price, 1, 99) : 1;
        AskQuantity(W.Get(), CanAfford, Price, [W, Item, Price, MoneyBox](int32 Qty)
        {
            UPYRMenus* M = Menus(W.Get());
            if (Qty <= 0 || !M) return;
            M->Ask(Sys(TEXT("PokemartTellBuyPriceText"), F2(TEXT("wStringBuffer"), ItemName(W.Get(), Item), TEXT("hMoney"), FString::FromInt(Price * Qty))),
                [W, Item, Price, Qty, MoneyBox](bool bYes)
                {
                    UPYRMenus* M2 = Menus(W.Get());
                    UPYRTrainerState* S2 = State(W.Get());
                    if (!bYes || !M2 || !S2) return;
                    if (S2->Money < Price * Qty) { M2->Say(Sys(TEXT("PokemartNotEnoughMoneyText"))); return; }
                    if (!S2->HasRoomFor(Item)) { M2->Say(Sys(TEXT("PokemartItemBagFullText"))); return; }
                    S2->AddMoney(-Price * Qty);
                    S2->AddItem(Item, Qty);
                    M2->Blip(TEXT("purchase"));
                    if (MoneyBox.IsValid()) MoneyBox->Refresh();
                    UE_LOG(LogYellow, Display, TEXT("YELLOW_MART_BUY %s x%d for %d, money now %d"), *Item.ToString(), Qty, Price * Qty, S2->Money);
                    M2->Say(Sys(TEXT("PokemartBoughtItemText")));
                });
        });
    };
    if (UPYRMenus* M = Menus(C)) M->Push(L);
}

void MartSell(TWeakObjectPtr<const UObject> W, FName MartMap, TSharedPtr<FMoneyScreen> MoneyBox, TFunction<void()> OnDone)
{
    const UObject* C = W.Get();
    UPYRMenus* M = Menus(C);
    const UPYRTrainerState* S = State(C);
    if (!M || !S) return;
    if (S->Bag.Num() == 0) { M->Say(Sys(TEXT("PokemartItemBagEmptyText")), [W, MartMap, MoneyBox, OnDone]() { MartMenu(W, MartMap, MoneyBox, OnDone); }); return; }
    TSharedRef<FItemListScreen> L = BagList(C);
    L->Id = TEXT("mart-sell");
    L->Title = TEXT("");
    TWeakPtr<FItemListScreen> WeakL = L;
    L->OnPick = [W, MartMap, MoneyBox, OnDone, WeakL](int32 Index, FName Item)
    {
        if (Index < 0) { MartMenu(W, MartMap, MoneyBox, OnDone); return; }
        const int32 Price = PriceOf(W.Get(), Item);
        // Key items (and priceless ones) can't be sold.
        if (IsKey(W.Get(), Item) || Price <= 0) { Menus(W.Get())->Say(Sys(TEXT("PokemartUnsellableItemText"))); return; }
        const UPYRTrainerState* S2 = State(W.Get());
        AskQuantity(W.Get(), S2 ? S2->ItemCount(Item) : 1, Price / 2, [W, Item, Price, MoneyBox, WeakL](int32 Qty)
        {
            UPYRMenus* M2 = Menus(W.Get());
            if (Qty <= 0 || !M2) return;
            M2->Ask(Sys(TEXT("PokemartTellSellPriceText"), F1(TEXT("hMoney"), FString::FromInt(Price / 2 * Qty))), [W, Item, Price, Qty, MoneyBox, WeakL](bool bYes)
            {
                UPYRTrainerState* S3 = State(W.Get());
                if (!bYes || !S3 || !S3->RemoveItem(Item, Qty)) return;
                S3->AddMoney(Price / 2 * Qty);
                if (UPYRMenus* M3 = Menus(W.Get())) M3->Blip(TEXT("purchase"));
                if (MoneyBox.IsValid()) MoneyBox->Refresh();
                if (TSharedPtr<FItemListScreen> L2 = WeakL.Pin()) L2->OnReveal();
                UE_LOG(LogYellow, Display, TEXT("YELLOW_MART_SELL %s x%d for %d, money now %d"), *Item.ToString(), Qty, Price / 2 * Qty, S3->Money);
            });
        });
    };
    M->Push(L);
}

void MartMenu(TWeakObjectPtr<const UObject> W, FName MartMap, TSharedPtr<FMoneyScreen> MoneyBox, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(W.Get());
    if (!M) return;
    TSharedRef<FPYRListScreen> L = MakeList(TEXT("mart"), {TEXT("BUY"), TEXT("SELL"), TEXT("QUIT")}, FVector2D(.06f, .06f), nullptr);
    TWeakPtr<FPYRListScreen> WeakL = L;
    auto Quit = [W, MoneyBox, OnDone, WeakL]()
    {
        if (TSharedPtr<FPYRListScreen> Me = WeakL.Pin()) Me->Close();
        if (MoneyBox.IsValid()) MoneyBox->Close();
        if (UPYRMenus* M2 = Menus(W.Get())) M2->Say(TEXT("Thank you!"), OnDone);
    };
    L->OnBack = Quit;
    L->OnPick = [W, MartMap, MoneyBox, OnDone, WeakL, Quit](int32 I)
    {
        if (I == 2) { Quit(); return; }
        if (TSharedPtr<FPYRListScreen> Me = WeakL.Pin()) Me->Close();
        if (I == 0) MartBuy(W, MartMap, MoneyBox, OnDone);
        else MartSell(W, MartMap, MoneyBox, OnDone);
    };
    M->Push(L);
}
}

void OpenMart(const UObject* Ctx, FName MartMap, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(Ctx);
    if (!M) return;
    TSharedRef<FMoneyScreen> MoneyBox = MakeShared<FMoneyScreen>();
    MoneyBox->Id = TEXT("money");
    M->Push(MoneyBox);
    MartMenu(Ctx, MartMap, MoneyBox, OnDone);
}
}
