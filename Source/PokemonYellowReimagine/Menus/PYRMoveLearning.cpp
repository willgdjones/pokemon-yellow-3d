#include "Menus/PYRMoveLearning.h"

#include "Battle/PYRBattleManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Menus/PYRScreensCommon.h"
#include "Music/GbMusicPlayer.h"
#include "Party/PYRSpecies.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

using namespace PYRScreensCommon;
using namespace PYRMoveLearning;

namespace
{
const FSlateBrush* WhiteBrush() { return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")); }

// Yellow's lines for the prompt, one sentence a line (the Game Boy's line breaks inside a box become spaces).
TArray<FString> Sentences(const FString& Text)
{
    TArray<FString> Pages, Out;
    Text.ParseIntoArray(Pages, TEXT("\f"), true);
    for (const FString& P : Pages) Out.Add(P.Replace(TEXT("\n"), TEXT(" ")).TrimStartAndEnd());
    return Out;
}

class FLearnScreen : public FPYRScreen
{
public:
    int32 PartyIndex = INDEX_NONE;
    FName Move;
    FString MonName, MoveStr;
    EStage Stage = EStage::None;
    int32 ListCursor = 0;
    double OpenedAt = 0;
    TArray<FKey> Held;
    TArray<TSharedPtr<SWidget>> ButtonWidgets;
    TFunction<void(int32, FName)> OnResult;   // The slot forgotten and the move that was in it, or -1 when abandoned.

    // The only keys the prompt answers.
    static EPYRKey MenuKey(const FKey& Key)
    {
        if (Key == EKeys::W || Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up) return EPYRKey::Up;
        if (Key == EKeys::S || Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down) return EPYRKey::Down;
        if (Key == EKeys::Enter || Key == EKeys::E || Key == EKeys::Gamepad_FaceButton_Bottom) return EPYRKey::Confirm;
        if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right) return EPYRKey::Back;
        return EPYRKey::None;
    }
    static const TArray<FKey>& AnswerKeys()
    {
        static const TArray<FKey> Keys = {EKeys::W, EKeys::Up, EKeys::Gamepad_DPad_Up, EKeys::S, EKeys::Down, EKeys::Gamepad_DPad_Down, EKeys::Enter, EKeys::E,
            EKeys::Gamepad_FaceButton_Bottom, EKeys::Escape, EKeys::BackSpace, EKeys::Gamepad_FaceButton_Right};
        return Keys;
    }

    double RealNow() const
    {
        const UWorld* W = GetWorld();
        return W ? W->GetRealTimeSeconds() : FPlatformTime::Seconds();
    }
    bool TakesInput() const { return RealNow() - OpenedAt >= InputDelay; }

    const FPYRMon* Mon() const
    {
        const UPYRTrainerState* S = State(GetWorld());
        return S && S->Party.IsValidIndex(PartyIndex) ? &S->Party[PartyIndex] : nullptr;
    }

    // Called once, before the push: the guard's clock and the keys already held.
    void Open()
    {
        OpenedAt = RealNow();
        Held.Reset();
        if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
            for (const FKey& K : AnswerKeys()) if (PC->IsInputKeyDown(K)) Held.Add(K);
        Enter(EStage::Ask);
    }

    TArray<FString> StageText() const
    {
        switch (Stage)
        {
        case EStage::Ask: return Sentences(Sys(TEXT("TryingToLearnText"), F2(TEXT("wLearnMoveMonName"), MonName, TEXT("wStringBuffer"), MoveStr)));
        case EStage::List: return Sentences(Sys(TEXT("WhichMoveToForgetText")));
        case EStage::Confirm:
        {
            const FPYRMon* M = Mon();
            const FString Old = M && M->Carried.IsValidIndex(ListCursor) ? PYRScreens::MoveName(M->Carried[ListCursor]) : FString();
            return {FString::Printf(TEXT("Forget %s?"), *Old)};
        }
        case EStage::Abandon: return Sentences(Sys(TEXT("AbandonLearningText"), F1(TEXT("wStringBuffer"), MoveStr)));
        default: return {};
        }
    }

    void Enter(EStage To)
    {
        Stage = To;
        // Yellow's YES/NO boxes start on YES; "Forget X?" starts on NO, so a mashed confirm never forgets a move.
        Cursor = To == EStage::Confirm ? 1 : 0;
        if (UPYRMenus* M = Owner.Get())
        {
            TArray<FString> Lines = StageText();
            if (To == EStage::Ask)
            {
                // The text log keeps the Game Boy's pages as the text box would.
                TArray<FString> Pages;
                Sys(TEXT("TryingToLearnText"), F2(TEXT("wLearnMoveMonName"), MonName, TEXT("wStringBuffer"), MoveStr)).ParseIntoArray(Pages, TEXT("\f"), true);
                M->TextLog.Append(Pages);
            }
            else M->TextLog.Append(Lines);
        }
        if (Widget.IsValid()) Refresh();
    }

    void Finish(int32 Slot, FName Old = NAME_None)
    {
        TFunction<void(int32, FName)> Done = OnResult;
        Close();
        if (Done) Done(Slot, Old);
    }

    void Choose(int32 YesNo)
    {
        const bool bYes = YesNo == 0;
        switch (Stage)
        {
        case EStage::Ask: Enter(bYes ? EStage::List : EStage::Abandon); return;
        case EStage::Confirm:
            if (!bYes) { Enter(EStage::List); return; }
            Forget(ListCursor);
            return;
        case EStage::Abandon:
            if (bYes) { Finish(-1); return; }
            Enter(EStage::Ask);
            return;
        default: return;
        }
    }

    // The player picked the slot and said YES to "Forget X?": the only place a known move is replaced.
    void Forget(int32 Slot)
    {
        UPYRTrainerState* S = State(GetWorld());
        if (!S || !S->Party.IsValidIndex(PartyIndex) || !S->Party[PartyIndex].Carried.IsValidIndex(Slot)) { Finish(-1); return; }
        FPYRMon& M = S->Party[PartyIndex];
        const FName Old = M.Carried[Slot];
        M.Carried[Slot] = Move;   // Yellow puts the new move in the forgotten one's slot.
        M.Learned.AddUnique(Move);
        UE_LOG(LogTemp, Display, TEXT("PYR_LEARN_FORGOT %s %s for %s"), *MonName, *Old.ToString(), *Move.ToString());
        Finish(Slot, Old);
    }

    void Act(EPYRKey Key)
    {
        const FPYRMon* M = Mon();
        const int32 Rows = M ? FMath::Clamp(M->Carried.Num(), 1, 4) : 4;
        if (Stage == EStage::List)
        {
            switch (Key)
            {
            case EPYRKey::Up: ListCursor = (ListCursor + Rows - 1) % Rows; Cursor = ListCursor; Refresh(); return;
            case EPYRKey::Down: ListCursor = (ListCursor + 1) % Rows; Cursor = ListCursor; Refresh(); return;
            case EPYRKey::Confirm: Enter(EStage::Confirm); return;
            case EPYRKey::Back: Enter(EStage::Abandon); return;
            default: return;
            }
        }
        switch (Key)
        {
        case EPYRKey::Up: case EPYRKey::Down: Cursor = 1 - Cursor; Refresh(); return;
        case EPYRKey::Confirm: Choose(Cursor); return;
        case EPYRKey::Back:
            // Back: from the question to the abandon question, from "Forget X?" to the list, from the abandon question
            // to the first question (NO).
            Enter(Stage == EStage::Ask ? EStage::Abandon : Stage == EStage::Confirm ? EStage::List : EStage::Ask);
            return;
        default: return;
        }
    }

    // A click or a tap on one of the prompt's buttons.
    void Click(int32 Index)
    {
        if (!TakesInput() || bClosed) return;
        if (UPYRMenus* M = Owner.Get()) M->Blip();
        if (Stage == EStage::List)
        {
            if (Index >= 0 && Index < 4) { ListCursor = Index; Enter(EStage::Confirm); }
            else Enter(EStage::Abandon);
            return;
        }
        Cursor = Index == 0 ? 0 : 1;
        Choose(Cursor);
    }

    virtual bool Input(EPYRKey Key) override
    {
        if (TakesInput() && !bClosed) Act(Key);
        return true;
    }

    virtual bool RawKey(const FKey& Key, bool bShift) override
    {
        // Every other key is the prompt's: the move keys, Space, Q, Tab, R, F and X do nothing here.
        if (!TakesInput() || Held.Contains(Key)) return true;
        const EPYRKey K = MenuKey(Key);
        if (K == EPYRKey::None) return true;
        if (UPYRMenus* M = Owner.Get()) { ++M->Presses; if (K == EPYRKey::Confirm || K == EPYRKey::Back) M->Blip(); }
        Act(K);
        return true;
    }

    virtual void Tick(float) override
    {
        // A key held when the prompt opened counts again once it has been let go.
        if (Held.Num() == 0) return;
        const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
        Held.RemoveAll([PC](const FKey& K) { return !PC || !PC->IsInputKeyDown(K); });
    }

    // ---- Look: the Game Boy's boxes over a light veil.
    TSharedRef<SWidget> Button(int32 Index, const TSharedRef<SWidget>& Content)
    {
        TWeakPtr<FPYRScreen> Weak = AsShared();
        TSharedRef<SWidget> B = SNew(SBorder)
            .BorderImage(WhiteBrush())
            .BorderBackgroundColor(FLinearColor(0, 0, 0, 0))
            .Padding(FMargin(4, 2))
            .OnMouseButtonDown_Lambda([Weak, Index](const FGeometry&, const FPointerEvent&)
            {
                if (TSharedPtr<FPYRScreen> S = Weak.Pin()) static_cast<FLearnScreen*>(S.Get())->Click(Index);
                return FReply::Handled();
            })
            [
                Content
            ];
        ButtonWidgets.Add(B);
        return B;
    }

    static TSharedRef<SWidget> CursorMark(bool bOn, int32 Size)
    {
        return SNew(SBox).WidthOverride(Size * .9f)[PYRGb::Text(bOn ? FString(TEXT("▶")) : FString(TEXT(" ")), Size - 4)];
    }

    TSharedRef<SWidget> YesNo()
    {
        TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
        const TCHAR* Labels[] = {TEXT("YES"), TEXT("NO")};
        for (int32 I = 0; I < 2; ++I)
            Col->AddSlot().AutoHeight().Padding(FMargin(0, 2))
            [
                Button(I, SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[CursorMark(Cursor == I, 30)]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[PYRGb::Text(Labels[I], 30)])
            ];
        return PYRGb::Box(Col, 18.f);
    }

    TSharedRef<SWidget> Question(bool bWithYesNo)
    {
        TSharedRef<SVerticalBox> Words = SNew(SVerticalBox);
        for (const FString& L : StageText()) Words->AddSlot().AutoHeight().Padding(FMargin(0, 3))[PYRGb::Text(L, 30)];
        TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
        if (bWithYesNo) Col->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(FMargin(0, 0, 0, 8))[YesNo()];
        Col->AddSlot().AutoHeight()[SNew(SBox).WidthOverride(1240)[PYRGb::Box(Words, 22.f)]];
        return PYRGb::PlaceAligned(Col, FVector2D(.5f, 1.f), FVector2D(.5f, 1.f), FVector2D(0, -28));
    }

    // One move: name, its type on the type's colour, the cooldown class and seconds.
    static TSharedRef<SWidget> MoveRow(FName Id, bool bCursor, bool bShowCursor)
    {
        const FPYRMoveInfo* Info = PYRData::Move(Id);
        const FString Type = Info ? PYRData::TypeName(Info->Type).ToString().ToUpper() : FString();
        const FString Class = Info ? FString::Printf(TEXT("%s  %.0f s"), *PYRData::TierName(Info->Tier).ToString().ToUpper(), Info->Cooldown) : FString();
        const FLinearColor TypeCol = Info ? PYRData::TypeColour(Info->Type) : FLinearColor::Gray;
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[bShowCursor ? CursorMark(bCursor, 28) : SNew(SBox).WidthOverride(25.f)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(330)[PYRGb::Text(PYRScreens::MoveName(Id), 28)]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 16, 0))
            [
                SNew(SBox).WidthOverride(170)
                [
                    SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PYRGb::Ink).Padding(FMargin(2))
                    [
                        SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(TypeCol).Padding(FMargin(8, 2)).HAlign(HAlign_Center)
                        [
                            PYRGb::Text(Type, 20)
                        ]
                    ]
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(260)[PYRGb::Text(Class, 22, false)]];
    }

    TSharedRef<SWidget> MoveList(bool bActive)
    {
        const FPYRMon* M = Mon();
        TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
        Col->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 10))[PYRGb::Text(Sentences(Sys(TEXT("WhichMoveToForgetText")))[0], 30)];
        for (int32 I = 0; M && I < M->Carried.Num() && I < 4; ++I)
        {
            TSharedRef<SWidget> Row = MoveRow(M->Carried[I], I == ListCursor, true);
            Col->AddSlot().AutoHeight().Padding(FMargin(0, 3))[bActive ? Button(I, Row) : Row];
        }
        // The new move beside them for comparison (it can't be picked).
        Col->AddSlot().AutoHeight().Padding(FMargin(0, 14, 0, 2))[PYRGb::Text(TEXT("NEW MOVE"), 20)];
        Col->AddSlot().AutoHeight()
            [
                SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(FColor::FromHex(TEXT("ffd733")))).Padding(FMargin(4, 4))
                [
                    MoveRow(Move, false, false)
                ]
            ];
        if (bActive)
        {
            Col->AddSlot().AutoHeight().Padding(FMargin(0, 14, 0, 0))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[PYRGb::Text(TEXT("W/S choose   ENTER/E pick   ESC back"), 18, false)]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        Button(4, SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PYRGb::Ink).Padding(FMargin(2))
                            [
                                SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PYRGb::Paper).Padding(FMargin(12, 2))[PYRGb::Text(TEXT("BACK"), 22)]
                            ])
                    ]
                ];
        }
        return PYRGb::PlaceAligned(SNew(SBox).WidthOverride(900)[PYRGb::Box(Col, 22.f)], FVector2D(.5f, .08f), FVector2D(.5f, 0.f));
    }

    virtual TSharedRef<SWidget> Build() override
    {
        ButtonWidgets.Reset();
        TSharedRef<SOverlay> Root = SNew(SOverlay);
        // A light veil: the fight (paused) or the field shows through.
        Root->AddSlot()[SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0, 0, 0, .3f))];
        if (Stage == EStage::List) Root->AddSlot()[MoveList(true)];
        if (Stage == EStage::Confirm) Root->AddSlot()[MoveList(false)];
        if (Stage != EStage::List) Root->AddSlot()[Question(true)];
        return Root;
    }

    virtual FString Describe() const override
    {
        FString Out = FString::Join(StageText(), TEXT("\n"));
        if (Stage == EStage::List || Stage == EStage::Confirm)
            if (const FPYRMon* M = Mon())
                for (int32 I = 0; I < M->Carried.Num(); ++I) Out += FString::Printf(TEXT("\n%s%s"), I == ListCursor ? TEXT("> ") : TEXT("  "), *PYRScreens::MoveName(M->Carried[I]));
        if (Stage != EStage::List) Out += Cursor == 0 ? TEXT("\n> YES\n  NO") : TEXT("\n  YES\n> NO");
        return Out;
    }
};

FLearnScreen* FindPrompt(const UObject* Ctx)
{
    const UPYRMenus* M = Menus(Ctx);
    TSharedPtr<FPYRScreen> S = M ? M->Find(TEXT("learn")) : nullptr;
    return S.IsValid() ? static_cast<FLearnScreen*>(S.Get()) : nullptr;
}
}

namespace PYRMoveLearning
{
void RequestLearn(const UObject* Ctx, int32 Index, FName Move, TFunction<void(bool)> OnDone)
{
    UPYRMenus* M = Menus(Ctx);
    UPYRTrainerState* S = State(Ctx);
    if (!M || !S || !S->Party.IsValidIndex(Index) || !PYRData::Move(Move)) { if (OnDone) OnDone(false); return; }
    FPYRMon& Mon = S->Party[Index];
    Mon.Pending.Remove(Move);
    if (Mon.Carried.Contains(Move)) { if (OnDone) OnDone(false); return; }
    const FString MonName = UPYRTrainerState::DisplayName(Mon), MoveStr = PYRScreens::MoveName(Move);
    TWeakObjectPtr<const UObject> W = Ctx;

    // In a fight, the fight waits for the whole exchange (the texts after the choice included).
    APYRBattleManager* BM = APYRBattleManager::Find(Ctx);
    TWeakObjectPtr<APYRBattleManager> Battle = BM && BM->IsBattleActive() ? BM : nullptr;
    auto Done = [Battle, Index, OnDone](bool bLearned)
    {
        if (APYRBattleManager* B = Battle.Get())
        {
            B->SyncCarried(Index);
            B->SetBattlePaused(false, APYRBattleManager::PauseLearn());
        }
        if (OnDone) OnDone(bLearned);
    };
    if (Battle.IsValid()) Battle->SetBattlePaused(true, APYRBattleManager::PauseLearn());
    auto Learned = [W, MonName, MoveStr, Done]()
    {
        if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(W.Get())) Music->Jingle(TEXT("get_item_1"));
        if (UPYRMenus* M2 = Menus(W.Get()))
            M2->Say(Sys(TEXT("LearnedMove1Text"), F2(TEXT("wLearnMoveMonName"), MonName, TEXT("wStringBuffer"), MoveStr)), [Done]() { Done(true); });
        else Done(true);
    };
    if (Mon.Carried.Num() < 4)
    {
        // A free slot: no question.
        Mon.Carried.Add(Move);
        Mon.Learned.AddUnique(Move);
        Learned();
        return;
    }
    TSharedRef<FLearnScreen> P = MakeShared<FLearnScreen>();
    P->Id = TEXT("learn");
    P->PartyIndex = Index;
    P->Move = Move;
    P->MonName = MonName;
    P->MoveStr = MoveStr;
    P->Owner = M;
    P->OnResult = [W, MonName, MoveStr, Learned, Done](int32 Slot, FName Old)
    {
        UPYRMenus* M2 = Menus(W.Get());
        if (!M2) { Done(Slot >= 0); return; }
        if (Slot < 0)
        {
            UE_LOG(LogTemp, Display, TEXT("PYR_LEARN_ABANDONED %s %s"), *MonName, *MoveStr);
            M2->Say(Sys(TEXT("DidNotLearnText"), F2(TEXT("wLearnMoveMonName"), MonName, TEXT("wStringBuffer"), MoveStr)), [Done]() { Done(false); });
            return;
        }
        M2->Say(Sys(TEXT("OneTwoAndText")) + Sys(TEXT("PoofText")) + Sys(TEXT("ForgotAndText"), F2(TEXT("wLearnMoveMonName"), MonName, TEXT("wNameBuffer"), PYRScreens::MoveName(Old))), Learned);
    };
    P->Open();
    UE_LOG(LogTemp, Display, TEXT("PYR_LEARN_PROMPT %s %s"), *MonName, *MoveStr);
    M->Push(P);
}

void LearnPending(const UObject* Ctx, TFunction<void()> OnDone)
{
    UPYRTrainerState* S = State(Ctx);
    const int32 I = S ? S->FirstPending() : INDEX_NONE;
    if (I == INDEX_NONE) { if (OnDone) OnDone(); return; }
    const FName Move = S->Party[I].Pending[0];
    TWeakObjectPtr<const UObject> W = Ctx;
    RequestLearn(Ctx, I, Move, [W, I, Move, OnDone](bool)
    {
        if (UPYRTrainerState* S2 = State(W.Get())) if (S2->Party.IsValidIndex(I)) S2->Party[I].Pending.Remove(Move);
        LearnPending(W.Get(), OnDone);
    });
}

bool View(const UObject* Ctx, FView& Out)
{
    const FLearnScreen* P = FindPrompt(Ctx);
    if (!P) return false;
    Out.Stage = P->Stage;
    Out.Cursor = P->Cursor;
    Out.ListCursor = P->ListCursor;
    Out.PartyIndex = P->PartyIndex;
    Out.Move = P->Move;
    Out.Text = FString::Join(P->StageText(), TEXT("\n"));
    Out.Rows.Reset();
    if (const FPYRMon* M = P->Mon()) for (const FName& C : M->Carried) Out.Rows.Add(PYRScreens::MoveName(C));
    Out.bTakesInput = P->TakesInput();
    return true;
}

TArray<TSharedPtr<SWidget>> Buttons(const UObject* Ctx)
{
    const FLearnScreen* P = FindPrompt(Ctx);
    return P ? P->ButtonWidgets : TArray<TSharedPtr<SWidget>>();
}

bool StepKeep(const UObject* Ctx)
{
    UPYRMenus* M = Menus(Ctx);
    TSharedPtr<FPYRScreen> Top = M ? M->Top() : nullptr;
    if (!Top.IsValid()) return false;
    if (Top->Id == TEXT("learn"))
    {
        const FLearnScreen* P = static_cast<FLearnScreen*>(Top.Get());
        if (!P->TakesInput()) return true;
        switch (P->Stage)
        {
        case EStage::Ask: M->Press(P->Cursor == 1 ? EPYRKey::Confirm : EPYRKey::Down); break;
        case EStage::List: M->Press(EPYRKey::Back); break;
        case EStage::Confirm: M->Press(P->Cursor == 1 ? EPYRKey::Confirm : EPYRKey::Down); break;
        case EStage::Abandon: M->Press(P->Cursor == 0 ? EPYRKey::Confirm : EPYRKey::Up); break;
        default: break;
        }
        return true;
    }
    const APYRBattleManager* BM = APYRBattleManager::Find(Ctx);
    if (Top->Id == TEXT("text") && BM && BM->IsPausedFor(APYRBattleManager::PauseLearn())) { M->Press(EPYRKey::Confirm); return true; }
    return false;
}
}
