#include "Menus/PYRScreens.h"

#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreensCommon.h"
#include "Menus/PYRSysText.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Music/GbMusicPlayer.h"
#include "Partner/PYRBond.h"
#include "Party/PYRSpecies.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Save/PYRSave.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Stage/PYREncounters.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Yellow/YellowData.h"
#include "Yellow/YellowImages.h"

using namespace PYRGb;
using namespace PYRScreensCommon;

namespace
{
FString Pad(const FString& S, int32 N)
{
    FString Out = S;
    while (Out.Len() < N) Out += TEXT(" ");
    return Out;
}

FString Status(const FPYRMon& M)
{
    if (M.Hp <= 0) return TEXT("FNT");
    switch (M.Status)
    {
    case EPYRStatus::Paralysis: return TEXT("PAR");
    case EPYRStatus::Poison: return TEXT("PSN");
    case EPYRStatus::Sleep: return TEXT("SLP");
    case EPYRStatus::Burn: return TEXT("BRN");
    default: return FString();
    }
}

FString TypeCaps(EPYRType T) { return PYRData::TypeName(T).ToString().ToUpper(); }
}

// ------------------------------------------------------------------------------------------------ names
namespace PYRScreens
{
FString SpeciesName(FName Species)
{
    const FPYRSpeciesInfo* S = PYRData::Species(Species);
    if (S && !S->GbName.IsEmpty()) return S->GbName;
    return (S ? S->Name.ToString() : Species.ToString()).ToUpper();
}

FString MoveName(FName Move)
{
    const FPYRMoveInfo* M = PYRData::Move(Move);
    if (M && !M->GbName.IsEmpty()) return M->GbName;
    return (M ? M->Name.ToString() : Move.ToString()).ToUpper();
}

FString ItemName(const UObject* Ctx, FName Item)
{
    if (const FYellowDataSet* D = Data(Ctx)) if (const FYItem* I = D->Item(Item)) return I->Name;
    if (Item == UPYRTrainerState::PokeBall) return TEXT("POKé BALL");
    return Item.ToString().ToUpper();
}
}

using namespace PYRScreens;

// ------------------------------------------------------------------------------------------------ START
namespace
{
class FStartScreen : public FPYRListScreen
{
public:
    TArray<FString> Keys;
    bool bPaused = false;
    virtual void OnClosed() override
    {
        if (bPaused)
            if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr) PC->SetPause(false);
        if (UPYRField* F = UPYRField::Get(GetWorld())) F->Unlock(TEXT("menu"));
    }
};
}

namespace PYRScreens
{
TArray<FString> StartEntries(const UObject* Ctx)
{
    TArray<FString> Out;
    const UPYRProgress* P = Prog(Ctx);
    const UPYRTrainerState* S = State(Ctx);
    if (P && P->Has(TEXT("EVENT_GOT_POKEDEX"))) Out.Add(TEXT("POKéDEX"));
    if (S && S->Party.Num() > 0) Out.Add(TEXT("POKéMON"));
    Out.Add(TEXT("ITEM"));
    Out.Add(P ? P->PlayerName : FString(TEXT("RED")));
    Out.Add(TEXT("SAVE"));
    Out.Add(TEXT("OPTION"));
    Out.Add(TEXT("EXIT"));
    return Out;
}

void OpenStart(const UObject* Ctx)
{
    UPYRMenus* M = Menus(Ctx);
    if (!M || M->IsOpen(TEXT("start"))) return;
    TSharedRef<FStartScreen> S = MakeShared<FStartScreen>();
    S->Id = TEXT("start");
    S->Items = StartEntries(Ctx);
    S->Anchor = FVector2D(.72f, .04f);
    S->Width = 0.f;
    TWeakPtr<FStartScreen> Weak = S;
    TWeakObjectPtr<const UObject> WCtx = Ctx;
    S->OnPick = [Weak, WCtx](int32 I)
    {
        TSharedPtr<FStartScreen> Self = Weak.Pin();
        const UObject* C = WCtx.Get();
        if (!Self || !C || !Self->Items.IsValidIndex(I)) return;
        const FString& E = Self->Items[I];
        if (E.StartsWith(TEXT("POKéDEX"))) OpenPokedex(C);
        else if (E.StartsWith(TEXT("POKéMON"))) OpenParty(C);
        else if (E == TEXT("ITEM")) OpenBag(C);
        else if (E == TEXT("SAVE")) OpenSave(C);
        else if (E == TEXT("OPTION")) OpenOptions(C);
        else if (E == TEXT("EXIT")) Self->Close();
        else OpenTrainerCard(C);
    };
    // Only Esc (or Q/Backspace) closes it; Enter picks.
    S->OnBack = [Weak]() { if (TSharedPtr<FStartScreen> Self = Weak.Pin()) Self->Close(); };
    if (UPYRField* F = UPYRField::Get(Ctx)) F->Lock(TEXT("menu"));
    // In a fight the START menu pauses it.
    if (APYRBattleManager* BM = APYRBattleManager::Find(Ctx))
        if (BM->IsBattleActive())
            if (APlayerController* PC = Ctx->GetWorld()->GetFirstPlayerController()) { PC->SetPause(true); S->bPaused = true; }
    M->Push(S);
    M->Blip(TEXT("start_menu"));
}
}

// ------------------------------------------------------------------------------------------------ party
namespace
{
// The party screen's Game Boy icon for a species (species.json menu_icon -> YellowData/icons/<ICON>.png).
UTexture2D* MenuIcon(const UObject* Ctx, FName Species)
{
    const FYellowDataSet* D = UYellowData::Data(Ctx);
    if (!D || !D->SpeciesJson.IsValid()) return nullptr;
    static TMap<FName, FString> Icons;
    if (Icons.Num() == 0)
        for (const auto& Pair : D->SpeciesJson->Values)
            if (const TSharedPtr<FJsonObject>* O = nullptr; Pair.Value.IsValid() && Pair.Value->TryGetObject(O) && O)
            {
                FString Icon;
                if ((*O)->TryGetStringField(TEXT("menu_icon"), Icon)) Icons.Add(YKey::Species(FString(*Pair.Key)), Icon);
            }
    const FString* Icon = Icons.Find(Species);
    UYellowImages* Images = UYellowImages::Get(Ctx);
    return Icon && Images ? Images->Load(FString::Printf(TEXT("icons/%s.png"), **Icon)) : nullptr;
}

class FPartyScreen : public FPYRScreen
{
public:
    FSlateBrush IconBrushes[6];
    FString Title = TEXT("Choose a POKéMON.");
    TFunction<void(int32)> OnChoose;     // choose mode; null: browse
    int32 SwitchFrom = INDEX_NONE;
    // m4u: each row's name, level and HP (bar and numbers), as last built.
    TArray<TPair<FString, TSharedPtr<SWidget>>> RowParts;
    virtual void Protected(TArray<TPair<FString, TSharedPtr<SWidget>>>& Out) const override { Out.Append(RowParts); }
    virtual bool IsFullScreen() const override { return true; }
    virtual TSharedRef<SWidget> Build() override
    {
        const UPYRTrainerState* S = State(GetWorld());
        TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
        const int32 N = S ? S->Party.Num() : 0;
        RowParts.Reset();
        for (int32 I = 0; I < N; ++I)
        {
            const FPYRMon& M = S->Party[I];
            const FString Mark = I == Cursor ? TEXT("▶") : I == SwitchFrom ? TEXT("▷") : TEXT(" ");
            // The Game Boy's little icon of the Pokemon's kind (a Pikachu, a bird, a bug...).
            TSharedRef<SWidget> IconWidget = PYRGb::Text(TEXT(" "), 24);
            if (UTexture2D* Tex = I < 6 ? MenuIcon(GetWorld(), M.Species) : nullptr)
            {
                IconBrushes[I].SetResourceObject(Tex);
                IconBrushes[I].ImageSize = FVector2D(40, 40);
                IconBrushes[I].DrawAs = ESlateBrushDrawType::Image;
                IconWidget = SNew(SImage).Image(&IconBrushes[I]);
            }
            const FString Who = UPYRTrainerState::DisplayName(M);
            const TSharedRef<SWidget> NameW = PYRGb::Text(Who, 28);
            const TSharedRef<SWidget> LevelW = PYRGb::Text(FString::Printf(TEXT(":L%d"), M.Level), 26);
            const TSharedRef<SWidget> BarW = HpBar(M.Hp, M.MaxHp, 200.f);
            const TSharedRef<SWidget> HpW = PYRGb::Text(FString::Printf(TEXT("%3d/%3d"), FMath::CeilToInt(M.Hp), FMath::RoundToInt(M.MaxHp)), 24);
            RowParts.Add({FString::Printf(TEXT("party %d name %s"), I + 1, *Who), NameW});
            RowParts.Add({FString::Printf(TEXT("party %d level"), I + 1), LevelW});
            RowParts.Add({FString::Printf(TEXT("party %d HP bar"), I + 1), BarW});
            RowParts.Add({FString::Printf(TEXT("party %d HP"), I + 1), HpW});
            Col->AddSlot().AutoHeight().Padding(FMargin(0, 6))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 14, 0))[SNew(SBox).WidthOverride(26)[PYRGb::Text(Mark, 24)]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 18, 0))[SNew(SBox).WidthOverride(40).HeightOverride(40)[IconWidget]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(260)[NameW]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(120)[LevelW]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(80)[PYRGb::Text(Status(M), 24)]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(10, 0))[BarW]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[HpW]
            ];
        }
        Col->AddSlot().FillHeight(1.f)[SNew(SSpacer)];
        Col->AddSlot().AutoHeight()[Box(PYRGb::Text(SwitchFrom != INDEX_NONE ? FString(TEXT("Move POKéMON\nwhere?")) : Title, 28))];
        return Page(Col);
    }
    virtual bool Input(EPYRKey Key) override
    {
        const UPYRTrainerState* S = State(GetWorld());
        const int32 N = S ? S->Party.Num() : 0;
        if (N == 0) { Close(); return true; }
        switch (Key)
        {
        case EPYRKey::Up: Cursor = (Cursor + N - 1) % N; Refresh(); return true;
        case EPYRKey::Down: Cursor = (Cursor + 1) % N; Refresh(); return true;
        case EPYRKey::Back:
            if (SwitchFrom != INDEX_NONE) { SwitchFrom = INDEX_NONE; Refresh(); return true; }
            if (OnChoose) { TFunction<void(int32)> C = OnChoose; Close(); C(-1); return true; }
            Close();
            return true;
        case EPYRKey::Confirm:
        {
            if (OnChoose) { TFunction<void(int32)> C = OnChoose; const int32 At = Cursor; Close(); C(At); return true; }
            if (SwitchFrom != INDEX_NONE)
            {
                if (UPYRTrainerState* St = State(GetWorld())) if (SwitchFrom != Cursor) St->Party.Swap(SwitchFrom, Cursor);
                SwitchFrom = INDEX_NONE;
                Refresh();
                return true;
            }
            TWeakPtr<FPYRScreen> Weak = AsShared();
            const int32 At = Cursor;
            TSharedRef<FPYRListScreen> Sub = MakeList(TEXT("party-sub"), {TEXT("STATS"), TEXT("SWITCH"), TEXT("CANCEL")}, FVector2D(.72f, .58f), nullptr);
            TWeakPtr<FPYRListScreen> WeakSub = Sub;
            Sub->OnPick = [Weak, WeakSub, At](int32 I)
            {
                if (TSharedPtr<FPYRListScreen> L = WeakSub.Pin()) L->Close();
                TSharedPtr<FPYRScreen> Self = Weak.Pin();
                if (!Self) return;
                if (I == 0) OpenStats(Self->GetWorld(), At);
                else if (I == 1) { StaticCastSharedPtr<FPartyScreen>(Self)->SwitchFrom = At; Self->Refresh(); }
            };
            if (UPYRMenus* M = Owner.Get()) M->Push(Sub);
            return true;
        }
        default: return true;
        }
    }
    virtual FString Describe() const override
    {
        FString Out;
        const UPYRTrainerState* S = State(GetWorld());
        if (S) for (int32 I = 0; I < S->Party.Num(); ++I)
            Out += FString::Printf(TEXT("%s%s :L%d HP %d/%d %s\n"), I == Cursor ? TEXT("> ") : TEXT("  "), *UPYRTrainerState::DisplayName(S->Party[I]),
                S->Party[I].Level, FMath::CeilToInt(S->Party[I].Hp), FMath::RoundToInt(S->Party[I].MaxHp), *Status(S->Party[I]));
        return Out + Title;
    }
};
}

namespace PYRScreens
{
void OpenParty(const UObject* Ctx)
{
    TSharedRef<FPartyScreen> S = MakeShared<FPartyScreen>();
    S->Id = TEXT("party");
    if (UPYRMenus* M = Menus(Ctx)) M->Push(S);
}

void ChooseMon(const UObject* Ctx, const FString& Title, TFunction<void(int32)> OnPick)
{
    TSharedRef<FPartyScreen> S = MakeShared<FPartyScreen>();
    S->Id = TEXT("party");
    S->Title = Title;
    S->OnChoose = OnPick ? MoveTemp(OnPick) : TFunction<void(int32)>([](int32) {});
    if (UPYRMenus* M = Menus(Ctx)) M->Push(S);
}
}

// ------------------------------------------------------------------------------------------------ stats
namespace
{
class FStatsScreen : public FPYRScreen
{
public:
    int32 Index = 0, PageNo = 0;
    virtual bool IsFullScreen() const override { return true; }
    virtual TSharedRef<SWidget> Build() override
    {
        const UPYRTrainerState* S = State(GetWorld());
        const UPYRProgress* P = Prog(GetWorld());
        if (!S || !S->Party.IsValidIndex(Index)) return Page(PYRGb::Text(TEXT("")));
        const FPYRMon& M = S->Party[Index];
        const FPYRSpeciesInfo* Info = PYRData::Species(M.Species);
        FString Text;
        if (PageNo == 0)
        {
            const FString Ot = M.IsTraded() ? (M.OtName.IsEmpty() ? M.OriginalTrainer.ToString().ToUpper() : M.OtName) : (P ? P->PlayerName : FString(TEXT("RED")));
            const int32 IdNo = M.IsTraded() ? M.OtId : (P ? P->TrainerId : 0);
            Text += FString::Printf(TEXT("No.%03d  %s\n:L%d   STATUS/%s\nHP: %d/%d\n\n"), Info ? Info->Dex : 0, *UPYRTrainerState::DisplayName(M), M.Level,
                Status(M).IsEmpty() ? TEXT("OK") : *Status(M), FMath::CeilToInt(M.Hp), FMath::RoundToInt(M.MaxHp));
            if (Info)
            {
                Text += FString::Printf(TEXT("TYPE1/ %s\n"), *TypeCaps(Info->Type1));
                if (Info->Type2 != EPYRType::None) Text += FString::Printf(TEXT("TYPE2/ %s\n"), *TypeCaps(Info->Type2));
                // engine/pokemon/status_screen.asm TypesIDNoOTText: "<ID>№/" then "OT/".
                Text += FString(TEXT("ID№/")) + FString::Printf(TEXT(" %05d\n"), IdNo) + TEXT("OT/") + TEXT(" ") + Ot + TEXT("\n\n");
                Text += FString::Printf(TEXT("ATTACK   %d\nDEFENSE  %d\nSPEED    %d\nSPECIAL  %d"),
                    FMath::RoundToInt(PYRData::StatAt(*Info, EPYRStat::Attack, M.Level)), FMath::RoundToInt(PYRData::StatAt(*Info, EPYRStat::Defence, M.Level)),
                    FMath::RoundToInt(PYRData::StatAt(*Info, EPYRStat::Speed, M.Level)), FMath::RoundToInt(PYRData::StatAt(*Info, EPYRStat::Special, M.Level)));
            }
        }
        else
        {
            const int32 Next = PYRData::XpForLevel(M.Species, FMath::Min(100, M.Level + 1));
            Text += FString::Printf(TEXT("%s\n\nEXP POINTS\n   %d\nLEVEL UP\n   %d to :L%d\n\n"), *UPYRTrainerState::DisplayName(M), M.Xp, FMath::Max(0, Next - M.Xp), FMath::Min(100, M.Level + 1));
            for (int32 I = 0; I < 4; ++I)
            {
                if (M.Carried.IsValidIndex(I))
                {
                    const FPYRMoveInfo* Mv = PYRData::Move(M.Carried[I]);
                    Text += FString::Printf(TEXT("%s\n     PP %d/%d  %s\n"), *MoveName(M.Carried[I]), Mv ? Mv->Pp : 0, Mv ? Mv->Pp : 0, Mv ? *TypeCaps(Mv->Type) : TEXT(""));
                }
                else Text += TEXT("-\n     --\n");
            }
        }
        return Page(PYRGb::Text(Text, 28));
    }
    virtual bool Input(EPYRKey Key) override
    {
        if (Key == EPYRKey::Confirm && PageNo == 0) { PageNo = 1; Refresh(); return true; }
        if (Key == EPYRKey::Confirm || Key == EPYRKey::Back) { Close(); return true; }
        return true;
    }
    virtual FString Describe() const override { return FString::Printf(TEXT("stats of party %d, page %d"), Index, PageNo + 1); }
};
}

namespace PYRScreens
{
void OpenStats(const UObject* Ctx, int32 PartyIndex)
{
    TSharedRef<FStatsScreen> S = MakeShared<FStatsScreen>();
    S->Id = TEXT("stats");
    S->Index = PartyIndex;
    if (UPYRMenus* M = Menus(Ctx)) M->Push(S);
}
}

// ------------------------------------------------------------------------------------------------ Pokedex
namespace
{
class FDexScreen : public FPYRScreen
{
public:
    TArray<FName> Order;   // by number, every species the tables know
    int32 Top = 0;
    static constexpr int32 Rows = 7;
    virtual bool IsFullScreen() const override { return true; }
    void Fill()
    {
        Order = PYRData::DexOrder();
        Order.RemoveAll([](const FName& Sp) { const FPYRSpeciesInfo* S = PYRData::Species(Sp); return !S || S->Dex <= 0 || S->Dex > 151; });
    }
    FString Row(int32 I) const
    {
        const UPYRTrainerState* S = State(GetWorld());
        const FName Sp = Order[I];
        const FPYRSpeciesInfo* Info = PYRData::Species(Sp);
        const bool bSeen = S && S->Seen.Contains(Sp), bOwn = S && S->Caught.Contains(Sp);
        return FString::Printf(TEXT("%03d %s %s"), Info ? Info->Dex : I + 1, bOwn ? TEXT("●") : TEXT(" "), bSeen ? *SpeciesName(Sp) : TEXT("----------"));
    }
    virtual TSharedRef<SWidget> Build() override
    {
        if (Order.Num() == 0) Fill();
        const UPYRTrainerState* S = State(GetWorld());
        TArray<FString> Lines;
        for (int32 I = Top; I < FMath::Min(Order.Num(), Top + Rows); ++I) Lines.Add(Row(I));
        const int32 Seen = S ? S->Seen.Num() : 0, Own = S ? S->Caught.Num() : 0;
        return Page(
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)[Box(PYRGb::List(Lines, Cursor - Top, 28))]
            + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(30, 0, 0, 0))[PYRGb::Text(FString::Printf(TEXT("CONTENTS\n\nSEEN\n   %d\nOWN\n   %d\n\n▶ DATA\n   QUIT"), Seen, Own), 28)]);
    }
    virtual bool Input(EPYRKey Key) override
    {
        if (Order.Num() == 0) Fill();
        const int32 N = Order.Num();
        if (N == 0) { Close(); return true; }
        switch (Key)
        {
        case EPYRKey::Up: Cursor = FMath::Max(0, Cursor - 1); break;
        case EPYRKey::Down: Cursor = FMath::Min(N - 1, Cursor + 1); break;
        case EPYRKey::Left: Cursor = FMath::Max(0, Cursor - Rows); break;
        case EPYRKey::Right: Cursor = FMath::Min(N - 1, Cursor + Rows); break;
        case EPYRKey::Back: Close(); return true;
        case EPYRKey::Confirm:
        {
            const UPYRTrainerState* S = State(GetWorld());
            if (S && S->Seen.Contains(Order[Cursor])) OpenDexEntry(GetWorld(), Order[Cursor]);
            return true;
        }
        default: return true;
        }
        if (Cursor < Top) Top = Cursor;
        if (Cursor >= Top + Rows) Top = Cursor - Rows + 1;
        Refresh();
        return true;
    }
    virtual FString Describe() const override
    {
        const UPYRTrainerState* S = State(GetWorld());
        FString Out = FString::Printf(TEXT("SEEN %d OWN %d\n"), S ? S->Seen.Num() : 0, S ? S->Caught.Num() : 0);
        for (int32 I = Top; I < FMath::Min(Order.Num(), Top + Rows); ++I) Out += (I == Cursor ? TEXT("> ") : TEXT("  ")) + Row(I) + TEXT("\n");
        return Out;
    }
};
}

namespace PYRScreens
{
void OpenPokedex(const UObject* Ctx)
{
    TSharedRef<FDexScreen> S = MakeShared<FDexScreen>();
    S->Id = TEXT("pokedex");
    S->Fill();
    if (UPYRMenus* M = Menus(Ctx)) M->Push(S);
}

void OpenDexEntry(const UObject* Ctx, FName Species, TFunction<void()> OnDone)
{
    const FPYRSpeciesInfo* Info = PYRData::Species(Species);
    const UPYRTrainerState* S = State(Ctx);
    const bool bOwn = S && S->Caught.Contains(Species);
    TSharedRef<FPageScreen> P = MakeShared<FPageScreen>();
    P->Id = TEXT("dex-entry");
    P->OnDone = MoveTemp(OnDone);
    FString Head = FString::Printf(TEXT("%s\n%s POKéMON\n\nNo.%03d\n"), *SpeciesName(Species), Info ? *Info->Category.ToString() : TEXT(""), Info ? Info->Dex : 0);
    if (Info && bOwn) Head += FString::Printf(TEXT("HT  %.1fm\nWT  %.1fkg"), Info->HeightM, Info->WeightKg);
    P->Pages.Add(Head);
    if (Info && bOwn && !Info->DexText.IsEmpty())
    {
        TArray<FString> Parts;
        Info->DexText.ToString().Replace(TEXT("\\f"), TEXT("\f")).ParseIntoArray(Parts, TEXT("\f"), true);
        for (const FString& Part : Parts) P->Pages.Add(Head + TEXT("\n\n") + Part.Replace(TEXT("\\n"), TEXT("\n")));
        if (P->Pages.Num() > 1) P->Pages.RemoveAt(0);
    }
    if (UPYRMenus* M = Menus(Ctx)) M->Push(P);
}
}
