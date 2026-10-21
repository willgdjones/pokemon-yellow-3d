#include "Menus/PYRMenus.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/PYRPhoneLayout.h"
#include "InputCoreTypes.h"
#include "Kanto/PYRProgress.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Menu/PYRPlayerController.h"
#include "Music/GbMusicPlayer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "World/PYRPaint.h"

// ------------------------------------------------------------------------------------------------ styling
namespace PYRGb
{
const FLinearColor Ink = PYRColor::Hex(0x181820);
const FLinearColor Paper = PYRColor::Hex(0xf8f8f0);

namespace
{
const FSlateBrush* White() { return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")); }
}

TSharedRef<SWidget> Box(const TSharedRef<SWidget>& Content, float Padding)
{
    // The Game Boy's double line: black, a white gap, black again, then the white page.
    return SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Ink).Padding(FMargin(3))
        [
            SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Paper).Padding(FMargin(3))
            [
                SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Ink).Padding(FMargin(2))
                [
                    SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Paper).Padding(FMargin(Padding, Padding * .75f))
                    [
                        Content
                    ]
                ]
            ]
        ];
}

TSharedRef<SWidget> Text(const FString& InText, int32 Size, bool bBold)
{
    return SNew(STextBlock)
        .Text(FText::FromString(InText))
        .ColorAndOpacity(FSlateColor(Ink))
        .Font(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size));
}

TSharedRef<SWidget> List(const TArray<FString>& Rows, int32 Cursor, int32 Size, TArray<TSharedPtr<SWidget>>* OutRows)
{
    TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
    if (OutRows) OutRows->Reset();
    for (int32 I = 0; I < Rows.Num(); ++I)
    {
        TSharedPtr<SHorizontalBox> Row;
        Col->AddSlot().AutoHeight().Padding(FMargin(0, 3))
            [
                SAssignNew(Row, SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 8, 0))
                [
                    SNew(SBox).WidthOverride(Size * .8f)
                    [
                        Text(I == Cursor ? FString(TEXT("▶")) : FString(TEXT(" ")), Size - 4)
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    Text(Rows[I], Size)
                ]
            ];
        if (OutRows) OutRows->Add(Row);
    }
    return Col;
}

TSharedRef<SWidget> Place(const TSharedRef<SWidget>& Content, FVector2D TopLeft, float Width, float Height)
{
    TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
    Canvas->AddSlot()
        .Anchors(FAnchors(TopLeft.X, TopLeft.Y))
        .Alignment(FVector2D(0, 0))
        .AutoSize(Width <= 0.f && Height <= 0.f)
        .Offset(FMargin(0, 0, Width, Height))
        [
            Content
        ];
    return Canvas;
}

TSharedRef<SWidget> PlaceAligned(const TSharedRef<SWidget>& Content, FVector2D Anchor, FVector2D Alignment, FVector2D Offset)
{
    TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
    Canvas->AddSlot()
        .Anchors(FAnchors(Anchor.X, Anchor.Y))
        .Alignment(Alignment)
        .AutoSize(true)
        .Offset(FMargin(Offset.X, Offset.Y, 0, 0))
        [
            Content
        ];
    return Canvas;
}

TSharedRef<SWidget> Page(const TSharedRef<SWidget>& Content)
{
    return SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Paper).Padding(FMargin(40, 30))[Content];
}

TSharedRef<SWidget> HpBar(float Hp, float MaxHp, float Width)
{
    const float F = MaxHp > 0 ? FMath::Clamp(Hp / MaxHp, 0.f, 1.f) : 0.f;
    const FLinearColor Col = F > .5f ? PYRColor::Hex(0x38b848) : F > .2f ? PYRColor::Hex(0xf0c020) : PYRColor::Hex(0xe03028);
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 6, 0))[Text(TEXT("HP:"), 16)]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Ink).Padding(FMargin(2))
            [
                SNew(SBox).WidthOverride(Width).HeightOverride(8)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(FMath::Max(F, .0001f))[SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Col)]
                    + SHorizontalBox::Slot().FillWidth(FMath::Max(1.f - F, .0001f))[SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Paper)]
                ]
            ]
        ];
}

FString Money(int32 Amount)
{
    return FString::Printf(TEXT("¥%d"), Amount);
}

FString Fill(const UObject* Ctx, const FString& InText)
{
    if (const UPYRProgress* P = UPYRProgress::Get(Ctx)) return P->Fill(InText);
    FString Out = InText;
    Out.ReplaceInline(TEXT("<PLAYER>"), TEXT("RED"));
    Out.ReplaceInline(TEXT("<RIVAL>"), TEXT("GARY"));
    return Out;
}
}

// ------------------------------------------------------------------------------------------------ screens
UWorld* FPYRScreen::GetWorld() const { return Owner.IsValid() ? Owner->GetWorld() : nullptr; }

void FPYRScreen::Refresh()
{
    if (UPYRMenus* M = Owner.Get()) M->Push(AsShared());  // Push of a screen already on the stack rebuilds it in place.
}

void FPYRScreen::Close()
{
    if (UPYRMenus* M = Owner.Get()) M->Remove(AsShared());
}

TSharedRef<SWidget> FPYRListScreen::Build()
{
    TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
    if (!Title.IsEmpty()) Col->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 6))[PYRGb::Text(Title, 22)];
    Col->AddSlot().AutoHeight()[PYRGb::List(Items, Cursor, 26, &RowWidgets)];
    return PYRGb::Place(PYRGb::Box(Col), Anchor, Width);
}

void FPYRListScreen::Protected(TArray<TPair<FString, TSharedPtr<SWidget>>>& Out) const
{
    for (int32 I = 0; I < RowWidgets.Num() && I < Items.Num(); ++I) Out.Add({FString::Printf(TEXT("%s: %s"), *Id.ToString(), *Items[I]), RowWidgets[I]});
}

bool FPYRListScreen::Input(EPYRKey Key)
{
    if (Items.Num() == 0) { if (Key == EPYRKey::Back || Key == EPYRKey::Confirm) { if (OnBack) OnBack(); else Close(); } return true; }
    switch (Key)
    {
    case EPYRKey::Up: Cursor = (Cursor + Items.Num() - 1) % Items.Num(); Refresh(); return true;
    case EPYRKey::Down: Cursor = (Cursor + 1) % Items.Num(); Refresh(); return true;
    case EPYRKey::Confirm:
    {
        TFunction<void(int32)> Pick = OnPick;
        const int32 At = Cursor;
        if (bCloseOnPick) Close();
        if (Pick) Pick(At);
        return true;
    }
    case EPYRKey::Back:
    {
        TFunction<void()> Back = OnBack;
        if (Back) Back(); else Close();
        return true;
    }
    default: return true;
    }
}

FString FPYRListScreen::Describe() const
{
    FString Out = Title.IsEmpty() ? FString() : Title + TEXT("\n");
    for (int32 I = 0; I < Items.Num(); ++I) Out += (I == Cursor ? TEXT("> ") : TEXT("  ")) + Items[I] + TEXT("\n");
    return Out;
}

// The Game Boy text box: two lines at a time at the bottom, typed out at the options' text speed, a blinking arrow
// when a box is full; confirm (or back) goes on.
class FPYRTextScreen : public FPYRScreen
{
public:
    // Speaker: who says this part (m4u: the dialogue box names the speaker, look.json dialogue-box), empty for signs,
    // narration and lines whose speaker isn't known.
    struct FPart { TArray<TArray<FString>> Pages; TFunction<void()> OnDone; TFunction<void()> OnLast; FString Speaker; };
    TArray<FPart> Parts;
    int32 Part = 0, PageIndex = 0, Line = 0;
    float Shown = 0.f;       // letters shown in the current view
    float Blink = 0.f;
    bool bHold = false;      // a question box is up over the last page

    void Add(const FString& Text, TFunction<void()> OnDone, TFunction<void()> OnLast = nullptr, const FString& Speaker = FString())
    {
        FPart P;
        P.Speaker = Speaker;
        FString Norm = Text;
        Norm.ReplaceInline(TEXT("\\f"), TEXT("\f"));
        Norm.ReplaceInline(TEXT("\\n"), TEXT("\n"));
        Norm.ReplaceInline(TEXT("\r"), TEXT(""));
        TArray<FString> PageTexts;
        Norm.ParseIntoArray(PageTexts, TEXT("\f"), false);
        for (const FString& Pt : PageTexts)
        {
            TArray<FString> Lines;
            Pt.ParseIntoArray(Lines, TEXT("\n"), false);
            while (Lines.Num() && Lines.Last().TrimStartAndEnd().IsEmpty()) Lines.Pop();
            if (Lines.Num() == 0) continue;
            P.Pages.Add(Lines);
        }
        if (P.Pages.Num() == 0) P.Pages.Add({FString()});
        P.OnDone = MoveTemp(OnDone);
        P.OnLast = MoveTemp(OnLast);
        Parts.Add(MoveTemp(P));
    }
    const TArray<FString>& Lines() const { return Parts[Part].Pages[PageIndex]; }
    FString ViewText() const
    {
        const TArray<FString>& L = Lines();
        FString Out = L.IsValidIndex(Line) ? L[Line] : FString();
        if (L.IsValidIndex(Line + 1)) Out += TEXT("\n") + L[Line + 1];
        return Out;
    }
    int32 ViewLetters() const { return ViewText().Len(); }
    bool IsViewComplete() const { return Shown >= ViewLetters(); }
    bool IsLastView() const { return Line + 2 >= Lines().Num() && PageIndex + 1 >= Parts[Part].Pages.Num(); }

    virtual bool ReleasesMouse() const override { return false; }
    virtual TSharedRef<SWidget> Build() override
    {
        const FString Full = ViewText();
        const FString Visible = Full.Left(FMath::FloorToInt(Shown));
        const bool bArrow = IsViewComplete() && !bHold && FMath::Frac(Blink) < .6f;
        const FString& Speaker = Parts[Part].Speaker;
        TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
        // The speaker's name on a tab over the box's top-left corner, overlapping its frame.
        if (!Speaker.IsEmpty())
            Col->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(28, 0, 0, -10))
            [
                SAssignNew(NameTab, SBox)[PYRGb::Box(PYRGb::Text(Speaker, 26), 8.f)]
            ];
        else NameTab.Reset();
        Col->AddSlot().AutoHeight()
        [
            SAssignNew(TextBox, SBox).WidthOverride(1400).HeightOverride(170)
            [
                PYRGb::Box(
                    SNew(SOverlay)
                    + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)[PYRGb::Text(Visible, 34)]
                    + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)[PYRGb::Text(bArrow ? TEXT("▼") : TEXT(" "), 22)],
                    22.f)
            ]
        ];
        return PYRGb::PlaceAligned(Col,
            // A little left of centre, so its right edge stays clear of the minimap in the bottom-right corner (m4r:
            // the two touched, visual-080).
            FVector2D(.5f, 1.f), FVector2D(.5f, 1.f), FVector2D(-TextBoxShift, -24));
    }
    static constexpr float TextBoxShift = 44.f;
    TSharedPtr<SBox> TextBox, NameTab;
    virtual void Protected(TArray<TPair<FString, TSharedPtr<SWidget>>>& Out) const override
    {
        if (TextBox.IsValid()) Out.Add({TEXT("text box"), TextBox});
        if (NameTab.IsValid()) Out.Add({TEXT("speaker"), NameTab});
    }
    virtual void Tick(float Dt) override
    {
        if (!IsViewComplete())
        {
            float Letter = 3.f / 60.f;
            if (const UPYRProgress* P = UPYRProgress::Get(GetWorld())) Letter = P->Options.LetterSeconds();
            const int32 Before = FMath::FloorToInt(Shown);
            Shown = FMath::Min(float(ViewLetters()), Shown + Dt / FMath::Max(.001f, Letter));
            if (FMath::FloorToInt(Shown) != Before) Refresh();
            if (IsViewComplete() && IsLastView() && Parts[Part].OnLast && !bHold) { bHold = true; TFunction<void()> Last = Parts[Part].OnLast; Last(); }
        }
        else
        {
            const bool bOn = FMath::Frac(Blink) < .6f;
            Blink += Dt;
            if ((FMath::Frac(Blink) < .6f) != bOn) Refresh();
        }
    }
    virtual bool Input(EPYRKey Key) override
    {
        if (bHold) return true;
        if (Key != EPYRKey::Confirm && Key != EPYRKey::Back) return true;
        if (!IsViewComplete())
        {
            Shown = ViewLetters();
            Refresh();
            if (IsLastView() && Parts[Part].OnLast) { bHold = true; TFunction<void()> Last = Parts[Part].OnLast; Last(); }
            return true;
        }
        Advance();
        return true;
    }
    void Advance()
    {
        if (UPYRMenus* M = Owner.Get()) M->Blip();
        if (Line + 2 < Lines().Num()) { ++Line; Shown = FMath::Max(0, Lines()[Line].Len()); Log(); Refresh(); return; }
        if (PageIndex + 1 < Parts[Part].Pages.Num()) { ++PageIndex; Line = 0; Shown = 0; Log(); Refresh(); return; }
        FinishPart();
    }
    void FinishPart()
    {
        TFunction<void()> Done = Parts[Part].OnDone;
        bHold = false;
        if (Part + 1 < Parts.Num())
        {
            ++Part; PageIndex = 0; Line = 0; Shown = 0;
            Log();
            Refresh();
            if (Done) Done();
            return;
        }
        Close();
        if (Done) Done();
    }
    void Log()
    {
        if (UPYRMenus* M = Owner.Get()) M->TextLog.Add(FString::Join(Lines(), TEXT("\n")));
    }
    virtual FString Describe() const override { return FString::Printf(TEXT("[text] %s"), *ViewText()); }
};

// ------------------------------------------------------------------------------------------------ subsystem
UPYRMenus* UPYRMenus::Get(const UObject* Ctx)
{
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    return World ? World->GetSubsystem<UPYRMenus>() : nullptr;
}

void UPYRMenus::EnsureRoot()
{
    if (bAdded) return;
    UWorld* World = GetWorld();
    UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
    if (!Viewport) return;
    Root = SNew(SOverlay);
    Frame = SNew(SConstraintCanvas);
    LayoutFrame();
    Viewport->AddViewportWidgetContent(Frame.ToSharedRef(), 60);
    bAdded = true;
}

void UPYRMenus::LayoutFrame()
{
    if (!Frame.IsValid() || !Root.IsValid()) return;
    const bool bPhone = PYRPhoneLayout::IsPhone();
    const FVector2f View = PYRPhoneLayout::ViewportSize();
    const FBox2f Col = PYRPhoneLayout::Safe().Column();
    const FString Key = !bPhone || View.X <= 0 ? FString(TEXT("Desktop"))
        : FString::Printf(TEXT("Phone %.0fx%.0f %.1f %.1f %.1f %.1f"), View.X, View.Y, Col.Min.X, Col.Min.Y, Col.Max.X, Col.Max.Y);
    if (Key == FrameKey) return;
    FrameKey = Key;
    Frame->ClearChildren();
    if (Key == TEXT("Desktop"))
    {
        Frame->AddSlot().Anchors(FAnchors(0, 0, 1, 1)).Offset(FMargin(0))[Root.ToSharedRef()];
        return;
    }
    // Full-screen pages (the party, stats, Pokedex...) keep the whole screen white behind the column.
    TWeakObjectPtr<UPYRMenus> Self = this;
    Frame->AddSlot().Anchors(FAnchors(0, 0, 1, 1)).Offset(FMargin(0))
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(PYRGb::Paper)
            .Visibility(TAttribute<EVisibility>::CreateLambda([Self]() { return Self.IsValid() && Self->bFullOpen ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
        ];
    // Every screen laid out on a Desktop-sized frame, scaled down into the column.
    Frame->AddSlot().Anchors(FAnchors(Col.Min.X / View.X, Col.Min.Y / View.Y, Col.Max.X / View.X, Col.Max.Y / View.Y)).Offset(FMargin(0))
        [
            SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
            [
                SNew(SBox).WidthOverride(PYRPhoneLayout::RefWidth).HeightOverride(PYRPhoneLayout::RefHeight)
                [
                    Root.ToSharedRef()
                ]
            ]
        ];
}

TArray<TPair<FString, TSharedPtr<SWidget>>> UPYRMenus::ProtectedWidgets() const
{
    TArray<TPair<FString, TSharedPtr<SWidget>>> Out;
    for (const TSharedPtr<FPYRScreen>& S : Stack) if (S.IsValid() && !S->bClosed) S->Protected(Out);
    return Out;
}

void UPYRMenus::Deinitialize()
{
    if (bAdded && Frame.IsValid())
        if (UWorld* World = GetWorld())
            if (UGameViewportClient* Viewport = World->GetGameViewport()) Viewport->RemoveViewportWidgetContent(Frame.ToSharedRef());
    Stack.Reset();
    Super::Deinitialize();
}

// QA hook (off unless asked): with -QAMenuLog or the environment variable PYR_QA_MENU_LOG=1 every newly opened screen
// and every change of the naming screen is logged as "QA_MENU <id>: <what it shows>", so an automated player can
// follow the menus it drives.
static bool QAMenuLog()
{
    static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("QAMenuLog")) || FPlatformMisc::GetEnvironmentVariable(TEXT("PYR_QA_MENU_LOG")) == TEXT("1");
    return bOn;
}

void UPYRMenus::Push(const TSharedRef<FPYRScreen>& Screen)
{
    Screen->Owner = this;
    Screen->bClosed = false;
    const bool bNew = !Stack.Contains(Screen);
    if (bNew) Stack.Add(Screen);
    if (QAMenuLog() && (bNew || Screen->Id == TEXT("naming")))
        UE_LOG(LogTemp, Display, TEXT("QA_MENU %s: %s"), *Screen->Id.ToString(), *Screen->Describe().Replace(TEXT("\n"), TEXT(" | ")));
    Screen->Widget = Screen->Build();
    Rebuild();
    UpdateMouse();
}

void UPYRMenus::Pop()
{
    if (Stack.Num()) Remove(Stack.Last());
}

void UPYRMenus::Remove(const TSharedPtr<FPYRScreen>& Screen)
{
    const int32 At = Stack.IndexOfByKey(Screen);
    if (At == INDEX_NONE) return;
    // Everything above it goes too.
    TArray<TSharedPtr<FPYRScreen>> Gone;
    for (int32 I = Stack.Num() - 1; I >= At; --I) { Gone.Add(Stack[I]); Stack.RemoveAt(I); }
    for (const TSharedPtr<FPYRScreen>& G : Gone) { G->bClosed = true; G->OnClosed(); }
    if (Stack.Num()) Stack.Last()->OnReveal();
    else if (QAMenuLog()) UE_LOG(LogTemp, Display, TEXT("QA_MENU none: every screen closed"));
    Rebuild();
    UpdateMouse();
}

void UPYRMenus::CloseAll()
{
    TArray<TSharedPtr<FPYRScreen>> Gone = Stack;
    Stack.Reset();
    for (const TSharedPtr<FPYRScreen>& G : Gone) { G->bClosed = true; G->OnClosed(); }
    Rebuild();
    UpdateMouse();
}

TSharedPtr<FPYRScreen> UPYRMenus::Find(FName Id) const
{
    for (const TSharedPtr<FPYRScreen>& S : Stack) if (S->Id == Id) return S;
    return nullptr;
}

bool UPYRMenus::AnyMenu() const
{
    for (const TSharedPtr<FPYRScreen>& S : Stack) if (S->ReleasesMouse()) return true;
    return false;
}

void UPYRMenus::Rebuild()
{
    EnsureRoot();
    if (!Root.IsValid()) return;
    Root->ClearChildren();
    // Draw from the topmost full-screen page up.
    int32 From = 0;
    bFullOpen = false;
    for (int32 I = Stack.Num() - 1; I >= 0; --I) if (Stack[I]->IsFullScreen()) { From = I; bFullOpen = true; break; }
    for (int32 I = From; I < Stack.Num(); ++I)
        if (Stack[I]->Widget.IsValid()) Root->AddSlot()[Stack[I]->Widget.ToSharedRef()];
}

void UPYRMenus::UpdateMouse()
{
    const bool bWant = AnyMenu();
    if (bWant == bMouseReleased) return;
    bMouseReleased = bWant;
    if (APYRPlayerController* PC = Cast<APYRPlayerController>(GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr))
        PC->SetMenuMouse(bWant);
}

EPYRKey UPYRMenus::Map(const FKey& Key)
{
    if (Key == EKeys::W || Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up) return EPYRKey::Up;
    if (Key == EKeys::S || Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down) return EPYRKey::Down;
    if (Key == EKeys::A || Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left) return EPYRKey::Left;
    if (Key == EKeys::D || Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right) return EPYRKey::Right;
    if (Key == EKeys::E || Key == EKeys::SpaceBar || Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom) return EPYRKey::Confirm;
    if (Key == EKeys::Q || Key == EKeys::BackSpace || Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right) return EPYRKey::Back;
    return EPYRKey::None;
}

bool UPYRMenus::HandleKey(const FKey& Key)
{
    if (Stack.Num() == 0) return false;
    // Start (Esc, or the pad's Start) skips Oak's speech on to the next question.
    if ((Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right) && IsOpen(TEXT("intro")) && Top()->Id == TEXT("text") && SkipTalk()) return true;
    bool bShift = bShiftDown;
    if (FSlateApplication::IsInitialized()) bShift |= FSlateApplication::Get().GetModifierKeys().IsShiftDown();
    if (Stack.Last()->RawKey(Key, bShift)) { ++Presses; return true; }
    const EPYRKey K = Map(Key);
    if (K != EPYRKey::None) Press(K);
    // While anything is open, nothing else hears the key.
    return true;
}

void UPYRMenus::Type(const FString& Keys, bool bShift)
{
    bShiftDown = bShift;
    for (const TCHAR C : Keys)
    {
        FKey Key;
        if (C == TEXT('\b')) Key = EKeys::BackSpace;
        else if (C == TEXT('\r') || C == TEXT('\n')) Key = EKeys::Enter;
        else if (C == TEXT(' ')) Key = EKeys::SpaceBar;
        else if (C == TEXT('-')) Key = EKeys::Hyphen;
        else if (C == TEXT('.')) Key = EKeys::Period;
        else if (C == TEXT(',')) Key = EKeys::Comma;
        else if (C == TEXT('/')) Key = EKeys::Slash;
        else Key = FKey(*FString::Chr(FChar::ToUpper(C)));
        HandleKey(Key);
    }
    bShiftDown = false;
}

bool UPYRMenus::SkipTalk()
{
    // Run the text box's parts to their end one after another (each part's follow-up may queue more talk), until a
    // question or another screen is up.
    int32 Guard = 0;
    bool bSkipped = false;
    while (Guard++ < 64)
    {
        TSharedPtr<FPYRScreen> T = Top();
        if (!T.IsValid() || T->Id != TEXT("text")) break;
        FPYRTextScreen* Text = static_cast<FPYRTextScreen*>(T.Get());
        if (Text->bHold || Text->Parts[Text->Part].OnLast) break;   // a question waits over its last page
        Text->FinishPart();
        bSkipped = true;
    }
    return bSkipped;
}

bool UPYRMenus::Press(EPYRKey Key)
{
    if (Stack.Num() == 0) return false;
    ++Presses;
    TSharedPtr<FPYRScreen> TopScreen = Stack.Last();
    const bool bUsed = TopScreen->Input(Key);
    if (bUsed && (Key == EPYRKey::Up || Key == EPYRKey::Down || Key == EPYRKey::Left || Key == EPYRKey::Right) && TopScreen->ReleasesMouse()) {}
    else if (bUsed && TopScreen->ReleasesMouse() && (Key == EPYRKey::Confirm || Key == EPYRKey::Back)) Blip();
    return bUsed;
}

void UPYRMenus::SetSpeaker(const FString& Name, const TArray<FString>& Texts)
{
    SpeakerName = Name;
    SpeakerAt = FPlatformTime::Seconds();
    bSpeakerOpening = true;
    SpeakerTexts.Reset();
    for (const FString& T : Texts) if (T.TrimStartAndEnd().Len() >= 4) SpeakerTexts.Add(T.TrimStartAndEnd());
}

FString UPYRMenus::SpeakerFor(const FString& Text)
{
    // The talker's own lines carry his name: the first line said in answer to talking to him (within a second and a
    // half; a story script may turn him first and pick a line of its own, the old man's coffee), then any of his Texts.
    // A script's other voices (Oak calling out, the rival cutting in) and narration (PIKACHU is happy!, an item
    // received) stay unnamed.
    const FString T = Text.TrimStartAndEnd();
    if (SpeakerName.IsEmpty() || T.IsEmpty()) return FString();
    const bool bOpening = bSpeakerOpening && FPlatformTime::Seconds() - SpeakerAt < 1.5;
    bSpeakerOpening = false;
    if (bOpening) return PYRGb::Fill(this, SpeakerName);
    for (const FString& S : SpeakerTexts) if (T == S || T.StartsWith(S) || S.StartsWith(T)) return PYRGb::Fill(this, SpeakerName);
    return FString();
}

void UPYRMenus::Say(const FString& Text, TFunction<void()> OnDone)
{
    const FString Filled = PYRGb::Fill(this, Text);
    const FString Who = SpeakerFor(Text);
    if (TSharedPtr<FPYRScreen> TopScreen = Top())
        if (TopScreen->Id == TEXT("text") && !TopScreen->bClosed)
        {
            StaticCastSharedPtr<FPYRTextScreen>(TopScreen)->Add(Filled, MoveTemp(OnDone), nullptr, Who);
            return;
        }
    TSharedRef<FPYRTextScreen> Screen = MakeShared<FPYRTextScreen>();
    Screen->Id = TEXT("text");
    Screen->Add(Filled, MoveTemp(OnDone), nullptr, Who);
    Screen->Owner = this;
    TextLog.Add(FString::Join(Screen->Lines(), TEXT("\n")));
    Push(Screen);
}

void UPYRMenus::Choose(const FString& Text, const TArray<FString>& Options, TFunction<void(int32)> OnPick)
{
    TWeakObjectPtr<UPYRMenus> Self = this;
    TSharedRef<FPYRTextScreen> Screen = MakeShared<FPYRTextScreen>();
    Screen->Id = TEXT("text");
    TWeakPtr<FPYRTextScreen> WeakText = Screen;
    Screen->Add(PYRGb::Fill(this, Text), nullptr, [Self, WeakText, Options, OnPick]()
    {
        // The last page is out: the choice box opens over it.
        TSharedRef<FPYRListScreen> List = MakeShared<FPYRListScreen>();
        List->Id = TEXT("choice");
        List->Items = Options;
        List->Anchor = FVector2D(.64f, .42f);
        List->OnPick = [Self, WeakText, OnPick](int32 I)
        {
            if (TSharedPtr<FPYRTextScreen> T = WeakText.Pin()) T->Close();
            if (OnPick) OnPick(I);
        };
        List->OnBack = [Self, WeakText, OnPick]()
        {
            if (TSharedPtr<FPYRTextScreen> T = WeakText.Pin()) T->Close();
            if (OnPick) OnPick(-1);
        };
        if (Self.IsValid()) Self->Push(List);
    }, SpeakerFor(Text));
    Screen->Owner = this;
    TextLog.Add(FString::Join(Screen->Lines(), TEXT("\n")));
    Push(Screen);
}

void UPYRMenus::Ask(const FString& Text, TFunction<void(bool)> OnAnswer)
{
    Choose(Text, {TEXT("YES"), TEXT("NO")}, [OnAnswer](int32 I) { if (OnAnswer) OnAnswer(I == 0); });
}

bool UPYRMenus::IsTalking() const
{
    return IsOpen(TEXT("text"));
}

FString UPYRMenus::ShownSpeaker() const
{
    for (int32 I = Stack.Num() - 1; I >= 0; --I)
        if (Stack[I].IsValid() && Stack[I]->Id == TEXT("text") && !Stack[I]->bClosed)
        {
            const FPYRTextScreen* T = static_cast<const FPYRTextScreen*>(Stack[I].Get());
            return T->Parts.IsValidIndex(T->Part) ? T->Parts[T->Part].Speaker : FString();
        }
    return FString();
}

FString UPYRMenus::ScreenText() const
{
    FString Out;
    for (const TSharedPtr<FPYRScreen>& S : Stack) Out += FString::Printf(TEXT("[%s]\n%s\n"), *S->Id.ToString(), *S->Describe());
    return Out;
}

void UPYRMenus::Blip(const TCHAR* Sfx)
{
    UGbMusicPlayer::PlaySfx(this, Sfx);
}

void UPYRMenus::Tick(float DeltaSeconds)
{
    // m4u: the column moves when the viewport, the page or touch changes.
    LayoutFrame();
    // Copy: a tick can close screens.
    TArray<TSharedPtr<FPYRScreen>> Now = Stack;
    for (const TSharedPtr<FPYRScreen>& S : Now) if (!S->bClosed) S->Tick(DeltaSeconds);
}
