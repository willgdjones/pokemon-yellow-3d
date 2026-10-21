// The Game Boy screens, part three: the PCs, naming, learning a move, the evolution scene, the Town Map, Pikachu's
// face, Oak's intro and the title.
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
#include "Menus/PYRIntroStage.h"
#include "Menus/PYRMoveLearning.h"
#include "Widgets/Layout/SSpacer.h"
#include "Menus/PYRScreensCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Music/GbMusicPlayer.h"
#include "Partner/PYRBond.h"
#include "Party/PYRSpecies.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Save/PYRSave.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
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
// ---- A list of Pokemon (a PC box or the party) to pick from.
class FMonListScreen : public FPYRListScreen
{
public:
    virtual bool IsFullScreen() const override { return false; }
};

TArray<FString> MonRows(const TArray<FPYRMon>& Mons)
{
    TArray<FString> Rows;
    for (const FPYRMon& M : Mons) Rows.Add(FString::Printf(TEXT("%s  :L%d"), *UPYRTrainerState::DisplayName(M), M.Level));
    Rows.Add(TEXT("CANCEL"));
    return Rows;
}

void PcMain(TWeakObjectPtr<const UObject> W, TFunction<void()> OnDone);

void BillsPc(TWeakObjectPtr<const UObject> W, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(W.Get());
    if (!M) return;
    const UPYRTrainerState* S = State(W.Get());
    const FString Title = FString::Printf(TEXT("BOX No.%d"), (S ? S->CurrentBox : 0) + 1);
    TSharedRef<FPYRListScreen> L = MakeList(TEXT("pc-bill"), {TEXT("WITHDRAW PKMN"), TEXT("DEPOSIT PKMN"), TEXT("RELEASE PKMN"), TEXT("CHANGE BOX"), TEXT("SEE YA!")}, FVector2D(.05f, .06f), nullptr);
    L->Title = Title;
    TWeakPtr<FPYRListScreen> WeakL = L;
    auto Back = [W, OnDone, WeakL]() { if (TSharedPtr<FPYRListScreen> Me = WeakL.Pin()) Me->Close(); PcMain(W, OnDone); };
    L->OnBack = Back;
    L->OnPick = [W, OnDone, WeakL, Back](int32 I)
    {
        UPYRMenus* M2 = Menus(W.Get());
        UPYRTrainerState* S2 = State(W.Get());
        if (!M2 || !S2) return;
        if (I == 4) { Back(); return; }
        if (I == 0 || I == 2)
        {
            // WITHDRAW or RELEASE: a Pokemon from the current box.
            const TArray<int32> Idx = S2->BoxIndices(S2->CurrentBox);
            if (Idx.Num() == 0) { M2->Say(Sys(TEXT("NothingStoredText"))); return; }
            TArray<FPYRMon> Mons;
            for (int32 X : Idx) Mons.Add(S2->Storage[X]);
            TSharedRef<FPYRListScreen> Pick = MakeList(TEXT("pc-box"), MonRows(Mons), FVector2D(.40f, .06f), nullptr);
            Pick->Title = I == 0 ? TEXT("Withdraw which\nPOKéMON?") : Sys(TEXT("ReleaseWhichMonText"));
            TWeakPtr<FPYRListScreen> WeakPick = Pick;
            Pick->OnPick = [W, I, Idx, WeakPick](int32 K)
            {
                if (TSharedPtr<FPYRListScreen> P = WeakPick.Pin()) P->Close();
                UPYRMenus* M3 = Menus(W.Get());
                UPYRTrainerState* S3 = State(W.Get());
                if (!M3 || !S3 || !Idx.IsValidIndex(K)) return;
                const FPYRMon Mon = S3->Storage[Idx[K]];
                const FString Name = UPYRTrainerState::DisplayName(Mon);
                if (I == 0)
                {
                    if (S3->Party.Num() >= 6) { M3->Say(TEXT("You can't take\nany more POKéMON.\fDeposit POKéMON\nfirst.")); return; }
                    S3->Storage.RemoveAt(Idx[K]);
                    S3->Party.Add(Mon);
                    M3->Say(FString::Printf(TEXT("%s is\ntaken out.\nGot %s."), *Name, *Name));
                }
                else
                    M3->Ask(Sys(TEXT("OnceReleasedText"), F1(TEXT("wStringBuffer"), Name)), [W, Idx, K, Name](bool bYes)
                    {
                        UPYRTrainerState* S4 = State(W.Get());
                        if (!bYes || !S4 || !S4->Storage.IsValidIndex(Idx[K])) return;
                        S4->Storage.RemoveAt(Idx[K]);
                        Menus(W.Get())->Say(Sys(TEXT("MonWasReleasedText"), F1(TEXT("wStringBuffer"), Name)));
                    });
            };
            M2->Push(Pick);
            return;
        }
        if (I == 1)
        {
            // DEPOSIT: never the last Pokemon, and Pikachu won't go (it never goes into a ball).
            ChooseMon(W.Get(), Sys(TEXT("DepositWhichMonText")), [W](int32 K)
            {
                UPYRMenus* M3 = Menus(W.Get());
                UPYRTrainerState* S3 = State(W.Get());
                if (!M3 || !S3 || !S3->Party.IsValidIndex(K)) return;
                if (S3->Party.Num() <= 1) { M3->Say(Sys(TEXT("CantDepositLastMonText"))); return; }
                const FPYRMon Mon = S3->Party[K];
                const FString Name = UPYRTrainerState::DisplayName(Mon);
                if (Mon.Species == FName(TEXT("pikachu"))) { M3->Say(Sys(TEXT("RefusingText"), F1(TEXT("wNameBuffer"), Name))); return; }
                if (S3->BoxCount(S3->CurrentBox) >= UPYRTrainerState::BoxSize) { M3->Say(Sys(TEXT("BoxFullText"))); return; }
                FPYRMon Stored = Mon;
                Stored.Box = S3->CurrentBox;
                S3->Party.RemoveAt(K);
                S3->Storage.Add(Stored);
                M3->Say(Sys(TEXT("MonWasStoredText"), F2(TEXT("wStringBuffer"), Name, TEXT("wBoxNumString"), FString::FromInt(S3->CurrentBox + 1))));
            });
            return;
        }
        // CHANGE BOX: the Game Boy saves when the box changes.
        TArray<FString> Boxes;
        for (int32 B = 0; B < UPYRTrainerState::NumBoxes; ++B) Boxes.Add(FString::Printf(TEXT("BOX %2d  (%2d)%s"), B + 1, S2->BoxCount(B), B == S2->CurrentBox ? TEXT(" ◀") : TEXT("")));
        M2->Say(Sys(TEXT("ChooseABoxText")), [W, Boxes, WeakL]()
        {
            TSharedRef<FPYRListScreen> Pick = MakeList(TEXT("pc-boxes"), Boxes, FVector2D(.40f, .06f), nullptr);
            TWeakPtr<FPYRListScreen> WeakPick = Pick;
            Pick->OnPick = [W, WeakPick, WeakL](int32 B)
            {
                if (TSharedPtr<FPYRListScreen> P = WeakPick.Pin()) P->Close();
                if (UPYRTrainerState* S3 = State(W.Get())) S3->CurrentBox = FMath::Clamp(B, 0, UPYRTrainerState::NumBoxes - 1);
                if (UPYRSave* Save = UPYRSave::Get(W.Get())) Save->SaveNow(TEXT("change box"));
                if (TSharedPtr<FPYRListScreen> L2 = WeakL.Pin()) { L2->Title = FString::Printf(TEXT("BOX No.%d"), B + 1); L2->Refresh(); }
            };
            if (UPYRMenus* M3 = Menus(W.Get())) M3->Push(Pick);
        });
    };
    M->Push(L);
}

void MyPc(TWeakObjectPtr<const UObject> W, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(W.Get());
    if (!M) return;
    TSharedRef<FPYRListScreen> L = MakeList(TEXT("pc-mine"), {TEXT("WITHDRAW ITEM"), TEXT("DEPOSIT ITEM"), TEXT("TOSS ITEM"), TEXT("LOG OFF")}, FVector2D(.05f, .06f), nullptr);
    TWeakPtr<FPYRListScreen> WeakL = L;
    auto Back = [W, OnDone, WeakL]() { if (TSharedPtr<FPYRListScreen> Me = WeakL.Pin()) Me->Close(); PcMain(W, OnDone); };
    L->OnBack = Back;
    L->OnPick = [W, Back](int32 I)
    {
        UPYRMenus* M2 = Menus(W.Get());
        if (!M2) return;
        if (I == 3) { Back(); return; }
        const bool bDeposit = I == 1;
        const UPYRProgress* P = Prog(W.Get());
        const UPYRTrainerState* S = State(W.Get());
        const TArray<FPYRItemStack> From = bDeposit ? (S ? S->Bag : TArray<FPYRItemStack>()) : (P ? P->PcItems : TArray<FPYRItemStack>());
        if (From.Num() == 0) { M2->Say(Sys(bDeposit ? TEXT("NothingToDepositText") : TEXT("NothingStoredText"))); return; }
        TArray<FString> Rows;
        for (const FPYRItemStack& It : From) Rows.Add(FString::Printf(TEXT("%s ×%d"), *ItemName(W.Get(), It.Item), It.Count));
        Rows.Add(TEXT("CANCEL"));
        TSharedRef<FPYRListScreen> Pick = MakeList(TEXT("pc-items"), Rows, FVector2D(.40f, .06f), nullptr);
        Pick->Title = Sys(bDeposit ? TEXT("WhatToDepositText") : I == 0 ? TEXT("WhatToWithdrawText") : TEXT("WhatToTossText"));
        TWeakPtr<FPYRListScreen> WeakPick = Pick;
        Pick->OnPick = [W, I, From, WeakPick](int32 K)
        {
            if (TSharedPtr<FPYRListScreen> Pk = WeakPick.Pin()) Pk->Close();
            if (!From.IsValidIndex(K)) return;
            const FName Item = From[K].Item;
            AskQuantity(W.Get(), From[K].Count, 0, [W, I, Item](int32 Qty)
            {
                UPYRMenus* M3 = Menus(W.Get());
                UPYRProgress* P3 = Prog(W.Get());
                UPYRTrainerState* S3 = State(W.Get());
                if (Qty <= 0 || !M3 || !P3 || !S3) return;
                const FString Name = ItemName(W.Get(), Item);
                if (I == 1)
                {
                    if (S3->RemoveItem(Item, Qty) && P3->DepositPc(Item, Qty)) M3->Say(Sys(TEXT("ItemWasStoredText"), F1(TEXT("wNameBuffer"), Name)));
                }
                else if (I == 0)
                {
                    if (!S3->HasRoomFor(Item)) { M3->Say(Sys(TEXT("NoMoreRoomForItemText"))); return; }
                    if (P3->WithdrawPc(Item, Qty)) { S3->AddItem(Item, Qty); M3->Say(Sys(TEXT("WithdrewItemText"), F1(TEXT("wNameBuffer"), Name))); }
                }
                else if (P3->WithdrawPc(Item, Qty)) M3->Say(Sys(TEXT("ThrewAwayItemText"), F1(TEXT("wNameBuffer"), Name)));
            });
        };
        M2->Push(Pick);
    };
    M->Push(L);
}

void OaksPc(TWeakObjectPtr<const UObject> W, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(W.Get());
    if (!M) return;
    M->Say(Sys(TEXT("AccessedOaksPCText")), [W, OnDone]()
    {
        Menus(W.Get())->Ask(Sys(TEXT("GetDexRatedText")), [W, OnDone](bool bYes)
        {
            UPYRMenus* M2 = Menus(W.Get());
            const UPYRTrainerState* S = State(W.Get());
            auto Close = [W, OnDone]() { Menus(W.Get())->Say(Sys(TEXT("ClosedOaksPCText")), [W, OnDone]() { PcMain(W, OnDone); }); };
            if (!bYes || !M2 || !S) { Close(); return; }
            const int32 Seen = S->Seen.Num(), Own = S->Caught.Num();
            // Oak's line for the owned count (text/pokedex_ratings.asm).
            static const int32 Bands[] = {9, 19, 29, 39, 49, 59, 69, 79, 89, 99, 109, 119, 129, 139, 149, 151};
            static const TCHAR* Labels[] = {TEXT("DexRatingText_Own0To9"), TEXT("DexRatingText_Own10To19"), TEXT("DexRatingText_Own20To29"), TEXT("DexRatingText_Own30To39"),
                TEXT("DexRatingText_Own40To49"), TEXT("DexRatingText_Own50To59"), TEXT("DexRatingText_Own60To69"), TEXT("DexRatingText_Own70To79"), TEXT("DexRatingText_Own80To89"),
                TEXT("DexRatingText_Own90To99"), TEXT("DexRatingText_Own100To109"), TEXT("DexRatingText_Own110To119"), TEXT("DexRatingText_Own120To129"),
                TEXT("DexRatingText_Own130To139"), TEXT("DexRatingText_Own140To149"), TEXT("DexRatingText_Own150To151")};
            int32 B = 0;
            while (B < 15 && Own > Bands[B]) ++B;
            if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(W.Get())) Music->Jingle(TEXT("dex_evaluation"));
            M2->Say(Sys(TEXT("DexCompletionText"), F2(TEXT("hDexRatingNumMonsSeen"), FString::FromInt(Seen), TEXT("hDexRatingNumMonsOwned"), FString::FromInt(Own))) + TEXT("\f") + Sys(Labels[B]), Close);
        });
    });
}

void PcMain(TWeakObjectPtr<const UObject> W, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(W.Get());
    const UPYRProgress* P = Prog(W.Get());
    if (!M) return;
    const bool bBill = P && (P->Has(TEXT("EVENT_MET_BILL")) || P->Has(TEXT("EVENT_MET_BILL_2")));
    const FString Player = P ? P->PlayerName : FString(TEXT("RED"));
    TSharedRef<FPYRListScreen> L = MakeList(TEXT("pc"), {bBill ? TEXT("BILL's PC") : TEXT("SOMEONE's PC"), Player + TEXT("'s PC"), TEXT("PROF.OAK's PC"), TEXT("LOG OFF")},
        FVector2D(.05f, .06f), nullptr);
    TWeakPtr<FPYRListScreen> WeakL = L;
    auto LogOff = [W, OnDone, WeakL]() { if (TSharedPtr<FPYRListScreen> Me = WeakL.Pin()) Me->Close(); if (UPYRMenus* M2 = Menus(W.Get())) M2->Blip(TEXT("press_ab")); if (OnDone) OnDone(); };
    L->OnBack = LogOff;
    L->OnPick = [W, OnDone, WeakL, LogOff, bBill](int32 I)
    {
        if (I == 3) { LogOff(); return; }
        if (TSharedPtr<FPYRListScreen> Me = WeakL.Pin()) Me->Close();
        UPYRMenus* M2 = Menus(W.Get());
        if (I == 0) M2->Say(Sys(bBill ? TEXT("AccessedBillsPCText") : TEXT("AccessedSomeonesPCText")), [W, OnDone]() { BillsPc(W, OnDone); });
        else if (I == 1) M2->Say(Sys(TEXT("AccessedMyPCText")), [W, OnDone]() { MyPc(W, OnDone); });
        else OaksPc(W, OnDone);
    };
    M->Push(L);
}
}

namespace PYRScreens
{
void OpenPc(const UObject* Ctx, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(Ctx);
    if (!M) return;
    TWeakObjectPtr<const UObject> W = Ctx;
    M->Say(Sys(TEXT("TurnedOnPC1Text")), [W, OnDone]() { PcMain(W, OnDone); });
}
}

// ------------------------------------------------------------------------------------------------ naming
namespace
{
class FNamingScreen : public FPYRScreen
{
public:
    FString Prompt, Default;
    TArray<FString> Cells;               // the name, one grid cell each (<PK> and <MN> are one cell)
    TFunction<void(FString)> OnDone;
    int32 Col = 0, Row = 0;
    bool bLower = false;
    bool bBeside = false;                // Oak's opening: a panel beside the 3D stage instead of a full page
    static constexpr int32 Cols = 9, Rows = 5, MaxLen = 7;
    // data/text/alphabets.asm: five rows of nine, the last cell ED, then the case switch.
    static const TArray<TArray<FString>>& Grid(bool bLowerCase)
    {
        static const TArray<TArray<FString>> Upper = {
            {TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E"), TEXT("F"), TEXT("G"), TEXT("H"), TEXT("I")},
            {TEXT("J"), TEXT("K"), TEXT("L"), TEXT("M"), TEXT("N"), TEXT("O"), TEXT("P"), TEXT("Q"), TEXT("R")},
            {TEXT("S"), TEXT("T"), TEXT("U"), TEXT("V"), TEXT("W"), TEXT("X"), TEXT("Y"), TEXT("Z"), TEXT(" ")},
            {TEXT("×"), TEXT("("), TEXT(")"), TEXT(":"), TEXT(";"), TEXT("["), TEXT("]"), TEXT("PK"), TEXT("MN")},
            {TEXT("-"), TEXT("?"), TEXT("!"), TEXT("♂"), TEXT("♀"), TEXT("/"), TEXT("."), TEXT(","), TEXT("ED")}};
        static const TArray<TArray<FString>> Lower = [] {
            TArray<TArray<FString>> L = Upper;
            for (int32 R = 0; R < 3; ++R) for (FString& C : L[R]) C = C.ToLower();
            return L;
        }();
        return bLowerCase ? Lower : Upper;
    }
    virtual bool IsFullScreen() const override { return !bBeside; }
    bool OnEd() const { return Row == Rows - 1 && Col == Cols - 1; }
    bool OnCase() const { return Row == Rows; }
    FString Name() const { return FString::Join(Cells, TEXT("")); }
    virtual TSharedRef<SWidget> Build() override
    {
        FString Shown = Name();
        for (int32 I = Cells.Num(); I < MaxLen; ++I) Shown += TEXT("_");
        FString GridText;
        const TArray<TArray<FString>>& G = Grid(bLower);
        for (int32 R = 0; R < Rows; ++R)
        {
            for (int32 C = 0; C < Cols; ++C)
            {
                const FString& Cell = G[R][C];
                GridText += (R == Row && C == Col ? TEXT("▶") : TEXT(" ")) + Cell + (Cell.Len() < 2 ? TEXT(" ") : TEXT(""));
            }
            GridText += TEXT("\n");
        }
        GridText += FString(OnCase() ? TEXT("▶") : TEXT(" ")) + (bLower ? TEXT("UPPER CASE") : TEXT("lower case"));
        TSharedRef<SWidget> Content = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[PYRGb::Text(Prompt, 30)]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 16))[PYRGb::Text(Shown, 38)]
            + SVerticalBox::Slot().AutoHeight()[PYRGb::Box(PYRGb::Text(GridText, 30), 20.f)];
        if (bBeside) return PYRGb::Place(PYRGb::Box(Content, 18.f), FVector2D(.53f, .05f));
        return Page(Content);
    }
    void Finish()
    {
        const FString Typed = Name().TrimStartAndEnd();
        const FString Out = Typed.IsEmpty() ? Default : Typed;
        TFunction<void(FString)> Done = OnDone;
        Close();
        if (Done) Done(Out);
    }
    void Add(const FString& Cell)
    {
        if (Cells.Num() >= MaxLen) return;
        Cells.Add(Cell);
        if (Cells.Num() >= MaxLen) { Row = Rows - 1; Col = Cols - 1; }   // full: the cursor jumps to ED, as on the Game Boy
    }
    virtual bool Input(EPYRKey Key) override
    {
        switch (Key)
        {
        case EPYRKey::Up: Row = (Row + Rows) % (Rows + 1); break;
        case EPYRKey::Down: Row = (Row + 1) % (Rows + 1); break;
        case EPYRKey::Left: if (!OnCase()) Col = (Col + Cols - 1) % Cols; break;
        case EPYRKey::Right: if (!OnCase()) Col = (Col + 1) % Cols; break;
        case EPYRKey::Back: if (Cells.Num() > 0) Cells.Pop(); break;
        case EPYRKey::Start: Finish(); return true;
        case EPYRKey::Confirm:
            if (OnEd()) { Finish(); return true; }
            if (OnCase()) bLower = !bLower;
            else Add(Grid(bLower)[Row][Col]);
            break;
        default: return true;
        }
        Refresh();
        return true;
    }
    // The keyboard: letters type themselves (Shift for the other case), Backspace deletes, Enter is ED; the arrows
    // and Space still work the grid.
    virtual bool RawKey(const FKey& Key, bool bShift) override
    {
        const FString Name = Key.GetFName().ToString();
        if (Name.Len() == 1 && FChar::IsAlpha(Name[0]))
        {
            const bool bLowerLetter = bLower != bShift;
            Add(bLowerLetter ? Name.ToLower() : Name.ToUpper());
            Refresh();
            return true;
        }
        if (Key == EKeys::BackSpace) { if (Cells.Num() > 0) Cells.Pop(); Refresh(); return true; }
        if (Key == EKeys::Enter) { Finish(); return true; }
        const TCHAR* Punct = Key == EKeys::Hyphen ? TEXT("-") : Key == EKeys::Period ? TEXT(".") : Key == EKeys::Comma ? TEXT(",")
            : Key == EKeys::Slash ? (bShift ? TEXT("?") : TEXT("/")) : Key == EKeys::One && bShift ? TEXT("!") : nullptr;
        if (Punct) { Add(Punct); Refresh(); return true; }
        return false;
    }
    virtual FString Describe() const override { return FString::Printf(TEXT("%s [%s] cursor %d,%d%s"), *Prompt, *Name(), Row, Col, OnEd() ? TEXT(" ED") : OnCase() ? TEXT(" CASE") : TEXT("")); }
};
}

namespace PYRScreens
{
void OpenNaming(const UObject* Ctx, const FString& Prompt, const FString& Default, TFunction<void(FString)> OnDone, bool bBeside)
{
    TSharedRef<FNamingScreen> S = MakeShared<FNamingScreen>();
    S->Id = TEXT("naming");
    S->Prompt = Prompt;
    S->Default = Default;
    S->bBeside = bBeside;
    S->OnDone = MoveTemp(OnDone);
    if (UPYRMenus* M = Menus(Ctx)) M->Push(S);
}

void OfferNickname(const UObject* Ctx, int32 Index, bool bInStorage, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(Ctx);
    UPYRTrainerState* S = State(Ctx);
    if (!M || !S) { if (OnDone) OnDone(); return; }
    FPYRMon* Mon = bInStorage ? (S->Storage.IsValidIndex(Index) ? &S->Storage[Index] : nullptr) : (S->Party.IsValidIndex(Index) ? &S->Party[Index] : nullptr);
    if (!Mon) { if (OnDone) OnDone(); return; }
    const FString Species = SpeciesName(Mon->Species);
    TWeakObjectPtr<const UObject> W = Ctx;
    M->Ask(Sys(TEXT("DoYouWantToNicknameText"), F1(TEXT("wNameBuffer"), Species)), [W, Index, bInStorage, Species, OnDone](bool bYes)
    {
        if (!bYes) { if (OnDone) OnDone(); return; }
        OpenNaming(W.Get(), FString::Printf(TEXT("%s\nNICKNAME?"), *Species), Species, [W, Index, bInStorage, Species, OnDone](FString Name)
        {
            UPYRTrainerState* S2 = State(W.Get());
            FPYRMon* Mon2 = !S2 ? nullptr : bInStorage ? (S2->Storage.IsValidIndex(Index) ? &S2->Storage[Index] : nullptr) : (S2->Party.IsValidIndex(Index) ? &S2->Party[Index] : nullptr);
            if (Mon2 && Name != Species) Mon2->Nickname = Name;
            if (OnDone) OnDone();
        });
    });
}

// ------------------------------------------------------------------------------------------------ learning moves
// m4t: one function for every move learned (Menus/PYRMoveLearning): level-ups, evolution and TMs/HMs all get the same
// isolated prompt, and only its confirmed YES replaces a known move.
void LearnMove(const UObject* Ctx, int32 Index, FName Move, TFunction<void(bool)> OnDone)
{
    PYRMoveLearning::RequestLearn(Ctx, Index, Move, MoveTemp(OnDone));
}

void LearnPending(const UObject* Ctx, TFunction<void()> OnDone)
{
    PYRMoveLearning::LearnPending(Ctx, MoveTemp(OnDone));
}
}


// ------------------------------------------------------------------------------------------------ evolution
namespace
{
// The evolution scene: the Pokemon in front of a close camera, flickering between its two forms faster and faster;
// Back stops it (Huh? X stopped evolving!).
class FEvolutionScreen : public FPYRScreen
{
public:
    int32 Index = 0;
    FName From, Into;
    FVector At = FVector::ZeroVector;
    float Yaw = 0.f;
    TFunction<void(bool)> OnDone;
    double Clock = 0;
    bool bFlicker = false, bDone = false;
    TWeakObjectPtr<ACharacter> A, B;
    TWeakObjectPtr<ACameraActor> Cam;
    TWeakObjectPtr<AActor> OldView;
    int32 Swaps = 0;
    static constexpr float FlickerSeconds = 4.f;
    virtual bool ReleasesMouse() const override { return false; }
    virtual TSharedRef<SWidget> Build() override { return SNew(SSpacer); }
    void Begin()
    {
        UWorld* W = GetWorld();
        const FTransform T(FRotator(0, Yaw, 0), At);
        A = APYRBattleManager::SpawnMonVisual(W, From, T);
        B = APYRBattleManager::SpawnMonVisual(W, Into, T);
        if (B.IsValid()) B->SetActorHiddenInGame(true);
        FActorSpawnParameters P;
        P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector Eye = At + FRotator(0, Yaw, 0).Vector() * 420.f + FVector(0, 0, 170.f);
        Cam = W->SpawnActor<ACameraActor>(Eye, (At + FVector(0, 0, 70.f) - Eye).Rotation(), P);
        if (APlayerController* PC = W->GetFirstPlayerController()) { OldView = PC->GetViewTarget(); PC->SetViewTarget(Cam.Get()); }
        if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(W)) Music->Jingle(TEXT("evolution"));
        TWeakPtr<FPYRScreen> Weak = AsShared();
        Menus(W)->Say(Sys(TEXT("IsEvolvingText"), F1(TEXT("wStringBuffer"), SpeciesName(From))), [Weak]()
        {
            if (TSharedPtr<FPYRScreen> S = Weak.Pin()) { StaticCastSharedPtr<FEvolutionScreen>(S)->bFlicker = true; StaticCastSharedPtr<FEvolutionScreen>(S)->Clock = 0; }
        });
    }
    void End(bool bEvolved)
    {
        if (bDone) return;
        bDone = true;
        bFlicker = false;
        UWorld* W = GetWorld();
        if (A.IsValid()) A->SetActorHiddenInGame(bEvolved);
        if (B.IsValid()) B->SetActorHiddenInGame(!bEvolved);
        TWeakPtr<FPYRScreen> Weak = AsShared();
        auto Finish = [Weak, bEvolved]()
        {
            TSharedPtr<FPYRScreen> S = Weak.Pin();
            if (!S) return;
            FEvolutionScreen* E = static_cast<FEvolutionScreen*>(S.Get());
            if (E->A.IsValid()) E->A->Destroy();
            if (E->B.IsValid()) E->B->Destroy();
            if (APlayerController* PC = E->GetWorld()->GetFirstPlayerController()) if (E->OldView.IsValid()) PC->SetViewTarget(E->OldView.Get());
            if (E->Cam.IsValid()) E->Cam->Destroy();
            TFunction<void(bool)> Done = E->OnDone;
            E->Close();
            if (Done) Done(bEvolved);
        };
        if (!bEvolved)
        {
            if (UPYRTrainerState* S = State(W)) if (S->Party.IsValidIndex(Index)) S->Party[Index].EvolvePending = NAME_None;
            Menus(W)->Say(Sys(TEXT("StoppedEvolvingText"), F1(TEXT("wStringBuffer"), SpeciesName(From))), Finish);
            return;
        }
        if (UPYRTrainerState* S = State(W)) S->ApplyEvolution(Index, Into);
        Menus(W)->Say(Sys(TEXT("EvolvedText"), F1(TEXT("wStringBuffer"), SpeciesName(From))) + Sys(TEXT("IntoText"), F1(TEXT("wNameBuffer"), SpeciesName(Into))), Finish);
    }
    virtual void Tick(float Dt) override
    {
        if (!bFlicker || bDone) return;
        Clock += Dt;
        // Faster and faster: the form shown flips on a sine whose rate grows.
        const float Rate = 2.f + 10.f * FMath::Clamp(float(Clock) / FlickerSeconds, 0.f, 1.f);
        const bool bShowNew = FMath::Sin(float(Clock) * Rate * PI) > 0.f;
        if (A.IsValid() && B.IsValid() && A->IsHidden() != bShowNew) { A->SetActorHiddenInGame(bShowNew); B->SetActorHiddenInGame(!bShowNew); ++Swaps; }
        if (Clock >= FlickerSeconds) End(true);
    }
    virtual bool Input(EPYRKey Key) override
    {
        if (bFlicker && Key == EPYRKey::Back) End(false);
        return true;
    }
    virtual FString Describe() const override { return FString::Printf(TEXT("evolving %s -> %s (%s, %d swaps)"), *From.ToString(), *Into.ToString(), bFlicker ? TEXT("flicker") : TEXT("text"), Swaps); }
};
}

namespace PYRScreens
{
void Evolve(const UObject* Ctx, int32 Index, FName Into, const FVector& At, float Yaw, TFunction<void(bool)> OnDone)
{
    UPYRMenus* M = Menus(Ctx);
    UPYRTrainerState* S = State(Ctx);
    if (!M || !S || !S->Party.IsValidIndex(Index) || !PYRData::Species(Into)) { if (OnDone) OnDone(false); return; }
    TSharedRef<FEvolutionScreen> E = MakeShared<FEvolutionScreen>();
    E->Id = TEXT("evolution");
    E->Index = Index;
    E->From = S->Party[Index].Species;
    E->Into = Into;
    E->At = At;
    E->Yaw = Yaw;
    E->OnDone = MoveTemp(OnDone);
    M->Push(E);
    E->Begin();
}

void EvolvePending(const UObject* Ctx, const FVector& At, float Yaw, TFunction<void()> OnDone)
{
    UPYRTrainerState* S = State(Ctx);
    const int32 I = S ? S->FirstEvolving() : INDEX_NONE;
    if (I == INDEX_NONE) { if (OnDone) OnDone(); return; }
    TWeakObjectPtr<const UObject> W = Ctx;
    const FVector Where = At;
    Evolve(Ctx, I, S->Party[I].EvolvePending, At, Yaw, [W, Where, Yaw, OnDone](bool)
    {
        // The new form's moves for its level, then the next Pokemon waiting.
        LearnPending(W.Get(), [W, Where, Yaw, OnDone]() { EvolvePending(W.Get(), Where, Yaw, OnDone); });
    });
}
}

// ------------------------------------------------------------------------------------------------ Town Map, Pikachu
namespace
{
class FPictureScreen : public FPYRScreen
{
public:
    TWeakObjectPtr<UTexture2D> Tex;
    FSlateBrush Brush;
    float Scale = 4.f;
    FVector2D Cursor2D = FVector2D(-1, -1);   // picture px of the blinking cursor
    FString Caption;
    double Clock = 0;
    bool bFull = true;
    TFunction<void()> OnDone;
    virtual bool IsFullScreen() const override { return bFull; }
    virtual TSharedRef<SWidget> Build() override
    {
        UTexture2D* T = Tex.Get();
        const FVector2D Size = T ? FVector2D(T->GetSizeX(), T->GetSizeY()) * Scale : FVector2D(160, 144) * Scale;
        if (T) { Brush.SetResourceObject(T); Brush.ImageSize = Size; Brush.DrawAs = ESlateBrushDrawType::Image; }
        TSharedRef<SOverlay> Pic = SNew(SOverlay)
            + SOverlay::Slot()[SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y)[T ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(&Brush)) : StaticCastSharedRef<SWidget>(PYRGb::Text(TEXT("(no picture)"), 24))]];
        if (Cursor2D.X >= 0 && FMath::Frac(Clock * 2.0) < .6)
            Pic->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(Cursor2D.X * Scale - 12, Cursor2D.Y * Scale - 12, 0, 0))
                [SNew(SBox).WidthOverride(24).HeightOverride(24)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(PYRColor::Hex(0xe3342f))]];
        TSharedRef<SVerticalBox> Col = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Box(Pic, 6.f)];
        if (!Caption.IsEmpty()) Col->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0, 14))[Box(PYRGb::Text(Caption, 30))];
        if (!bFull) return PlaceAligned(Col, FVector2D(.5f, .42f), FVector2D(.5f, .5f));
        return Page(Col);
    }
    virtual void Tick(float Dt) override
    {
        const bool bWas = FMath::Frac(Clock * 2.0) < .6;
        Clock += Dt;
        if (Cursor2D.X >= 0 && (FMath::Frac(Clock * 2.0) < .6) != bWas) Refresh();
    }
    virtual bool Input(EPYRKey Key) override
    {
        if (Key == EPYRKey::Confirm || Key == EPYRKey::Back) { TFunction<void()> D = OnDone; Close(); if (D) D(); }
        return true;
    }
    virtual FString Describe() const override { return FString::Printf(TEXT("%s cursor %.0f,%.0f %s"), Tex.IsValid() ? *Tex->GetName() : TEXT("none"), Cursor2D.X, Cursor2D.Y, *Caption); }
};
}

namespace
{
// Oak's opening over the 3D stage: nothing of its own is drawn (the stage is the picture; the text box and the naming
// panel come on top). It follows the beats, ends when Red has shrunk away (a white fade, then his room), and keeps a
// log of the beats and who was on the stage for tests.
class FIntroScreen : public FPYRScreen
{
public:
    TWeakObjectPtr<AYellowIntroStage> Stage;
    TFunction<void()> OnDone;
    TArray<FString> BeatLog;
    double EndClock = -1;
    virtual bool ReleasesMouse() const override { return false; }
    virtual TSharedRef<SWidget> Build() override { return SNew(SSpacer); }
    void Beat(const TCHAR* Name)
    {
        if (Stage.IsValid()) Stage->Beat(Name);
        FString Who;
        if (Stage.IsValid())
        {
            // Who the beat puts on the stage (the slide-in is still running).
            if (FCString::Strcmp(Name, TEXT("oak")) == 0) Who = TEXT("oak");
            else if (FCString::Strcmp(Name, TEXT("pikachu")) == 0) Who = TEXT("oak+pikachu");
            else if (FCString::Strcmp(Name, TEXT("rival")) == 0) Who = TEXT("rival");
            else Who = TEXT("red");
        }
        BeatLog.Add(FString::Printf(TEXT("%s:%s"), Name, *Who));
        UE_LOG(LogYellow, Display, TEXT("YELLOW_INTRO %s (%s)"), Name, *Who);
        if (FCString::Strcmp(Name, TEXT("shrink")) == 0) EndClock = 0;
    }
    virtual void Tick(float Dt) override
    {
        if (EndClock < 0) return;
        EndClock += Dt;
        UPYRField* Field = UPYRField::Get(GetWorld());
        // Red shrinks, the screen whites out, and he is in his room (the Game Boy's GBFadeOutToWhite).
        if (EndClock >= AYellowIntroStage::ShrinkSeconds && Field && Field->GetFade() < .01f && EndClock < AYellowIntroStage::ShrinkSeconds + .5)
            Field->Fade(1.f, .35f, FLinearColor::White);
        if (EndClock >= AYellowIntroStage::ShrinkSeconds + .5 || (!Field && EndClock >= AYellowIntroStage::ShrinkSeconds))
        {
            EndClock = -1;
            if (Stage.IsValid()) Stage->Close();
            if (Field) Field->Fade(0.f, .45f, FLinearColor::White);
            if (Field)
                if (const FYMap* M = Field->GetMap())
                    if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(GetWorld())) Music->Play(FName(*M->Music), .3f);
            TFunction<void()> Done = OnDone;
            Close();
            if (Done) Done();
        }
    }
    virtual bool Input(EPYRKey Key) override { return true; }
    FString ShownList() const
    {
        FString Shown;
        if (Stage.IsValid()) for (const FName& N : Stage->Shown()) Shown += (Shown.IsEmpty() ? TEXT("") : TEXT(",")) + N.ToString();
        return Shown;
    }
    virtual FString Describe() const override { return FString::Printf(TEXT("stage=%s beat=%s shown=%s models=%d"), Stage.IsValid() ? TEXT("up") : TEXT("none"), Stage.IsValid() ? *Stage->GetBeat().ToString() : TEXT("-"), *ShownList(), Stage.IsValid() ? Stage->ModelsLoaded() : 0); }
};
}

namespace PYRScreens
{
void OpenTownMap(const UObject* Ctx)
{
    UPYRMenus* M = Menus(Ctx);
    if (!M) return;
    TSharedRef<FPictureScreen> S = MakeShared<FPictureScreen>();
    S->Id = TEXT("town-map");
    if (UYellowImages* Images = UYellowImages::Get(Ctx)) S->Tex = Images->Load(TEXT("townmap/townmap.png"));
    // Where Red is: his outdoor map, or for a room the outdoor map its exit leads to.
    const UPYRField* Field = UPYRField::Get(Ctx);
    const FYMap* Map = Field ? Field->GetMap() : nullptr;
    FName Place = Map ? (Map->bOutdoor ? Map->Id : Field->LastOutdoor) : NAME_None;
    FString Text;
    const FString Path = FPaths::Combine(UYellowData::DefaultRoot(), TEXT("townmap/townmap.json"));
    if (FFileHelper::LoadFileToString(Text, *Path))
    {
        TSharedPtr<FJsonObject> Json;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid())
        {
            // The room's own entry when it has one (every interior names its parent's spot).
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (Map && Json->TryGetObjectField(Map->Id.ToString(), Entry) && Entry) {}
            else if (Json->TryGetObjectField(Place.ToString(), Entry) && Entry) {}
            if (Entry && Entry->IsValid())
            {
                double X = 0, Y = 0;
                (*Entry)->TryGetNumberField(TEXT("x"), X);
                (*Entry)->TryGetNumberField(TEXT("y"), Y);
                // townmap.json counts the Game Boy's 8 px cells (x < 20, y < 18); larger numbers are pixels.
                const bool bCells = X < 21 && Y < 19;
                S->Cursor2D = bCells ? FVector2D(X * 8 + 4, Y * 8 + 4) : FVector2D(X, Y);
            }
        }
    }
    else UE_LOG(LogYellow, Error, TEXT("YellowData missing: %s"), *Path);
    const FYMap* PlaceMap = Field && Field->GetYellowWorld() && Field->GetYellowWorld()->GetData() ? Field->GetYellowWorld()->GetData()->Map(Place) : nullptr;
    S->Caption = PlaceMap ? PlaceMap->Name : Place.ToString();
    M->Push(S);
}

void ShowPikachu(const UObject* Ctx, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(Ctx);
    if (!M) return;
    TSharedRef<FPictureScreen> S = MakeShared<FPictureScreen>();
    S->Id = TEXT("pikachu-face");
    S->bFull = false;
    S->Scale = 6.f;
    S->OnDone = MoveTemp(OnDone);
    const FString Face = PYRBond::Face(Ctx);
    FString File = FString::Printf(TEXT("pikachu/%s.png"), *Face);
    if (const FYellowDataSet* D = Data(Ctx)) if (const FString* F = D->Pikachu.FaceFiles.Find(Face)) File = *F;
    if (UYellowImages* Images = UYellowImages::Get(Ctx)) S->Tex = Images->Load(File);
    S->Caption = Face.IsEmpty() ? FString(TEXT("PIKACHU")) : FString::Printf(TEXT("PIKACHU (%s)"), *Face.Replace(TEXT("_"), TEXT(" ")).ToUpper());
    UE_LOG(LogYellow, Display, TEXT("YELLOW_PIKACHU_FACE %s"), *Face);
    M->Push(S);
}

// ------------------------------------------------------------------------------------------------ intro and title
void Intro(const UObject* Ctx, TFunction<void()> OnDone)
{
    UPYRMenus* M = Menus(Ctx);
    if (!M) { if (OnDone) OnDone(); return; }
    UWorld* World = Ctx->GetWorld();
    // Oak's 3D stage (engine/movie/oak_speech/oak_speech.asm), with MUSIC_ROUTES2 as on the Game Boy.
    AYellowIntroStage* Stage = AYellowIntroStage::Open(World);
    TSharedRef<FIntroScreen> Screen = MakeShared<FIntroScreen>();
    Screen->Id = TEXT("intro");
    Screen->Stage = Stage;
    Screen->OnDone = MoveTemp(OnDone);
    M->Push(Screen);
    if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(Ctx)) Music->Play(TEXT("routes2"), .3f);
    TWeakPtr<FIntroScreen> WeakScreen = Screen;
    TWeakObjectPtr<const UObject> W = Ctx;
    auto Beat = [WeakScreen](const TCHAR* Name)
    {
        if (TSharedPtr<FIntroScreen> S = WeakScreen.Pin()) S->Beat(Name);
    };
    // NEW NAME or one of Yellow's names; NEW NAME opens the naming panel beside the one being named.
    auto NameMenu = [W](const FString& Ask, TArray<FString> Options, int32 DefaultIndex, const FString& Prompt, TFunction<void(FString)> Then)
    {
        Menus(W.Get())->Choose(Ask, Options, [W, Options, DefaultIndex, Prompt, Then](int32 I)
        {
            if (I <= 0) OpenNaming(W.Get(), Prompt, Options[FMath::Max(1, DefaultIndex)], Then, true);   // NEW NAME (or Back)
            else Then(Options[I]);
        });
    };
    Beat(TEXT("oak"));
    M->Say(Sys(TEXT("OakSpeechText1")), [W, Beat, NameMenu, WeakScreen]()
    {
        Beat(TEXT("pikachu"));
        Menus(W.Get())->Say(Sys(TEXT("OakSpeechText2A")), [W, Beat, NameMenu, WeakScreen]()
        {
            PYR::Sfx(W.Get(), TEXT("S_pikachu_cry"));
            Menus(W.Get())->Say(Sys(TEXT("OakSpeechText2B")), [W, Beat, NameMenu, WeakScreen]()
            {
                Beat(TEXT("red"));
                NameMenu(Sys(TEXT("IntroducePlayerText")), {TEXT("NEW NAME"), TEXT("RED"), TEXT("ASH"), TEXT("JACK")}, 1, TEXT("YOUR NAME?"),
                    [W, Beat, NameMenu, WeakScreen](FString Player)
                {
                    if (UPYRProgress* P = Prog(W.Get())) P->PlayerName = Player.Left(7);
                    Menus(W.Get())->Say(Sys(TEXT("YourNameIsText")), [W, Beat, NameMenu, WeakScreen]()
                    {
                        Beat(TEXT("rival"));
                        NameMenu(Sys(TEXT("IntroduceRivalText")), {TEXT("NEW NAME"), TEXT("BLUE"), TEXT("GARY"), TEXT("JOHN")}, 2, TEXT("RIVAL's NAME?"),
                            [W, Beat, WeakScreen](FString Rival)
                        {
                            if (UPYRProgress* P = Prog(W.Get())) P->RivalName = Rival.Left(7);
                            Menus(W.Get())->Say(Sys(TEXT("HisNameIsText")), [W, Beat, WeakScreen]()
                            {
                                Beat(TEXT("red-again"));
                                Menus(W.Get())->Say(Sys(TEXT("OakSpeechText3")), [Beat]() { Beat(TEXT("shrink")); });
                            });
                        });
                    });
                });
            });
        });
    });
}

void Title(const UObject* Ctx, bool bHasSave, TFunction<void(bool)> OnPick)
{
    UPYRMenus* M = Menus(Ctx);
    if (!M) return;
    TSharedRef<FPageScreen> Bg = MakeShared<FPageScreen>();
    Bg->Id = TEXT("title");
    // The title logo's words, as Yellow's title screen shows them.
    Bg->Pages = {FString(TEXT("\n\n     ")) + TEXT("POKéMON") + TEXT("\n        ") + TEXT("YELLOW VERSION") + TEXT("\n\n     ") + TEXT("Special PIKACHU Edition")};
    M->Push(Bg);
    TArray<FString> Items;
    if (bHasSave) Items.Add(TEXT("CONTINUE"));
    Items.Add(TEXT("NEW GAME"));
    Items.Add(TEXT("OPTION"));
    TSharedRef<FPYRListScreen> L = MakeList(TEXT("title-menu"), Items, FVector2D(.08f, .62f), nullptr);
    TWeakPtr<FPYRListScreen> WeakL = L;
    TWeakPtr<FPageScreen> WeakBg = Bg;
    TWeakObjectPtr<const UObject> W = Ctx;
    L->OnBack = []() {};
    L->OnPick = [W, WeakL, WeakBg, Items, OnPick](int32 I)
    {
        if (Items[I] == TEXT("OPTION")) { OpenOptions(W.Get()); return; }
        if (TSharedPtr<FPYRListScreen> Me = WeakL.Pin()) Me->Close();
        if (TSharedPtr<FPageScreen> B = WeakBg.Pin()) B->Close();
        if (OnPick) OnPick(Items[I] == TEXT("CONTINUE"));
    };
    M->Push(L);
}
}
