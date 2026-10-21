#include "Hud/PYRPhoneLayout.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameTest.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
TAutoConsoleVariable<int32> CVarTouchLayout(TEXT("pyr.TouchLayout"), 0,
    TEXT("m4u: the stream page's touch controls. 0 as the page or the config says, 1 on (the phone layout), 2 off."));

// What the page last said (PYRTouch), and the page size it gave.
bool bPageSaid = false, bPageTouch = false;
FVector2f PageSaid = FVector2f::ZeroVector;

bool ConfigBool(const TCHAR* Key, bool bDefault)
{
    bool b = bDefault;
    if (GConfig) GConfig->GetBool(TEXT("PYRPhoneLayout"), Key, b, GGameIni);
    return b;
}

float ConfigFloat(const TCHAR* Key, float Default)
{
    float F = Default;
    if (GConfig) GConfig->GetFloat(TEXT("PYRPhoneLayout"), Key, F, GGameIni);
    return F;
}

// A browser keyCode (what the stream page sends) as the engine's key.
FKey BrowserKey(int32 Code)
{
    if (Code >= 65 && Code <= 90) return FKey(*FString::Chr(TCHAR(Code)));
    static const FKey Digits[] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
    if (Code >= 48 && Code <= 57) return Digits[Code - 48];
    switch (Code)
    {
    case 13: return EKeys::Enter;
    case 9: return EKeys::Tab;
    case 16: return EKeys::LeftShift;
    case 27: return EKeys::Escape;
    case 32: return EKeys::SpaceBar;
    default: return EKeys::Invalid;
    }
}

struct FButtonSpec { FString Label; FKey Key; };
struct FLayoutSpec { TArray<FButtonSpec> Buttons, Extra; FKey Up = EKeys::W, Down = EKeys::S, Left = EKeys::A, Right = EKeys::D; };

// touch-layout.json in the project folder, the page's layout for this game (staged with the package); the built-in
// buttons when it can't be read.
const FLayoutSpec& Layout()
{
    static FLayoutSpec Spec;
    static bool bRead = false;
    if (bRead) return Spec;
    bRead = true;
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("touch-layout.json")))
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())
    {
        for (const TPair<const TCHAR*, TArray<FButtonSpec>*>& Group : {TPair<const TCHAR*, TArray<FButtonSpec>*>(TEXT("buttons"), &Spec.Buttons),
                 TPair<const TCHAR*, TArray<FButtonSpec>*>(TEXT("extra"), &Spec.Extra)})
        {
            const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
            if (!Root->TryGetArrayField(Group.Key, List)) continue;
            for (const TSharedPtr<FJsonValue>& V : *List)
            {
                const TSharedPtr<FJsonObject>* B = nullptr;
                if (V->TryGetObject(B)) Group.Value->Add({(*B)->GetStringField(TEXT("label")), BrowserKey(int32((*B)->GetNumberField(TEXT("key"))))});
            }
        }
        const TSharedPtr<FJsonObject>* Stick = nullptr;
        if (Root->TryGetObjectField(TEXT("stick"), Stick))
        {
            Spec.Up = BrowserKey(int32((*Stick)->GetNumberField(TEXT("up"))));
            Spec.Down = BrowserKey(int32((*Stick)->GetNumberField(TEXT("down"))));
            Spec.Left = BrowserKey(int32((*Stick)->GetNumberField(TEXT("left"))));
            Spec.Right = BrowserKey(int32((*Stick)->GetNumberField(TEXT("right"))));
        }
    }
    if (Spec.Buttons.Num() == 0)
        Spec.Buttons = {{TEXT("START"), EKeys::Enter}, {TEXT("B / Back"), EKeys::Q}, {TEXT("A / Talk"), EKeys::E}, {TEXT("Dodge"), EKeys::SpaceBar},
            {TEXT("Sprint"), EKeys::LeftShift}, {TEXT("Party"), EKeys::Tab}, {TEXT("Throw"), EKeys::F}};
    if (Spec.Extra.Num() == 0)
        Spec.Extra = {{TEXT("1"), EKeys::One}, {TEXT("2"), EKeys::Two}, {TEXT("3"), EKeys::Three}, {TEXT("4"), EKeys::Four}, {TEXT("RUN"), EKeys::X}};
    return Spec;
}

FBox2f PageRect(float X, float Y, float W, float H) { return FBox2f(FVector2f(X, Y), FVector2f(X + W, Y + H)); }

// The guide's widgets, kept between frames.
TSharedPtr<SConstraintCanvas> GuideCanvas;
TArray<TPair<FPYRTouchControl, TSharedPtr<SWidget>>> GuideItems;
FString GuideKey;
TWeakObjectPtr<UGameViewportClient> GuideViewport;
FString LastLogged;
}

namespace PYRPhoneLayout
{
const TCHAR* Name(EPYRLayout Layout) { return Layout == EPYRLayout::Phone ? TEXT("Phone") : TEXT("Desktop"); }

FVector2f ViewportSize()
{
    if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport) return FVector2f::ZeroVector;
    const FIntPoint S = GEngine->GameViewport->Viewport->GetSizeXY();
    return FVector2f(S.X, S.Y);
}

bool PhoneViewport(FVector2f View)
{
    // Under 100 px the size isn't known yet (a null renderer, the first frame): not a phone.
    if (View.X < 100.f || View.Y < 100.f) return false;
    return View.Y < PhoneMaxHeight || View.X / View.Y > PhoneMinAspect;
}

bool Streamed()
{
    // This build is never streamed (see Start).
    return false;
}

bool TouchOn()
{
    const int32 Forced = CVarTouchLayout.GetValueOnGameThread();
    if (Forced == 1) return true;
    if (Forced == 2) return false;
    static const bool bArgOn = FParse::Param(FCommandLine::Get(), TEXT("TouchLayout"));
    static const bool bArgOff = FParse::Param(FCommandLine::Get(), TEXT("NoTouchLayout"));
    if (bArgOff) return false;
    if (bArgOn) return true;
    if (bPageSaid) return bPageTouch;
    return Streamed() && ConfigBool(TEXT("TouchWhenStreamed"), true);
}

bool PageOverlay() { return Streamed() || TouchOn(); }

EPYRLayout Current()
{
    return PhoneViewport(ViewportSize()) || PageOverlay() ? EPYRLayout::Phone : EPYRLayout::Desktop;
}

FVector2f PageSize()
{
    if (bPageSaid && PageSaid.X > 0 && PageSaid.Y > 0) return PageSaid;
    const FVector2f View = ViewportSize();
    // A phone-sized window is its own page; a stream (always 16:9) is shown on the phone the config names.
    if (!Streamed() && PhoneViewport(View)) return View;
    return FVector2f(ConfigFloat(TEXT("AssumedPageWidth"), 844.f), ConfigFloat(TEXT("AssumedPageHeight"), 390.f));
}

TArray<FPYRTouchControl> ControlsFor(FVector2f Page, FVector2f View, bool bTouch)
{
    TArray<FPYRTouchControl> Out;
    if (Page.X < 1.f || Page.Y < 1.f || View.X < 1.f || View.Y < 1.f) return Out;
    // player.css: "@media (max-height:500px)" makes everything a little smaller on a phone held sideways.
    const bool bSmall = Page.Y <= 500.f;
    auto Add = [&Out](const FString& Label, FKey Key, EPYRTouchZone Zone, FBox2f Box) { FPYRTouchControl C; C.Label = Label; C.Key = Key; C.Zone = Zone; C.Page = Box; Out.Add(C); };

    // The top bar (aside: top 6, left/right 8; the stats box two lines high, the buttons centred beside it).
    Add(TEXT("Full screen / Touch / Sound / End"), EKeys::Invalid, EPYRTouchZone::TopBar, PageRect(8, 6, 232, 46));
    Add(TEXT("Stats"), EKeys::Invalid, EPYRTouchZone::TopBar, PageRect(Page.X - 8 - 150, 6, 150, 46));
    if (bTouch)
    {
        const FLayoutSpec& L = Layout();
        // #stick: left 24, bottom 30, 145 round (small: bottom 22, 125).
        const float Stick = bSmall ? 125.f : 145.f;
        Add(TEXT("MOVE"), EKeys::Invalid, EPYRTouchZone::Stick, PageRect(24, Page.Y - (bSmall ? 22.f : 30.f) - Stick, Stick, Stick));
        // #actions: a 3-column grid, gap 7, right 14, bottom 25 (small: bottom 12). Buttons at least 68 wide; the model
        // makes them at least 44 high (the page's own are 40 on a small screen).
        const float H = FMath::Max(MinControl, bSmall ? 40.f : 46.f);
        const float Pad = bSmall ? 18.f : 36.f;
        float Cols[3] = {0, 0, 0};
        for (int32 I = 0; I < L.Buttons.Num(); ++I) Cols[I % 3] = FMath::Max(Cols[I % 3], FMath::Max(68.f, 6.5f * L.Buttons[I].Label.Len() + Pad));
        const int32 Rows = FMath::DivideAndRoundUp(L.Buttons.Num(), 3);
        const float GridW = Cols[0] + Cols[1] + Cols[2] + 14.f;
        const float GridH = Rows * H + (Rows - 1) * 7.f;
        const float Left = Page.X - 14.f - GridW, Top = Page.Y - (bSmall ? 12.f : 25.f) - GridH;
        for (int32 I = 0; I < L.Buttons.Num(); ++I)
        {
            const int32 R = I / 3, C = I % 3;
            float X = Left;
            for (int32 J = 0; J < C; ++J) X += Cols[J] + 7.f;
            Add(L.Buttons[I].Label, L.Buttons[I].Key, EPYRTouchZone::Actions, PageRect(X, Top + R * (H + 7.f), Cols[C], H));
        }
        // #moves: a row from 40% of the width (small: 30%), bottom 12, gap 5; padding 10 x 14 on a 16 px font.
        float X = Page.X * (bSmall ? .3f : .4f);
        for (const FButtonSpec& B : L.Extra)
        {
            const float W = FMath::Max(MinControl, 10.5f * B.Label.Len() + 30.f);
            Add(B.Label, B.Key, EPYRTouchZone::Moves, PageRect(X, Page.Y - 12.f - MinControl, W, MinControl));
            X += W + 5.f;
        }
    }
    // The page shows the game's picture with object-fit: contain (a 16:9 stream between two bars on a phone).
    const float S = FMath::Min(Page.X / View.X, Page.Y / View.Y);
    const FVector2f Offset = (Page - View * S) * .5f;
    for (FPYRTouchControl& C : Out) C.View = FBox2f((C.Page.Min - Offset) / S, (C.Page.Max - Offset) / S);
    return Out;
}

TArray<FPYRTouchControl> Controls()
{
    if (!PageOverlay()) return {};
    return ControlsFor(PageSize(), ViewportSize(), TouchOn());
}

FKey StickKey(int32 Direction)
{
    const FLayoutSpec& L = Layout();
    return Direction == 0 ? L.Up : Direction == 1 ? L.Down : Direction == 2 ? L.Left : L.Right;
}

FPYRSafeArea SafeFor(const TArray<FPYRTouchControl>& Controls, FVector2f View)
{
    const float M = FPYRSafeArea::Margin;
    FPYRSafeArea S;
    S.View = View;
    S.Top = M; S.SideBottom = View.Y - M; S.ColumnLeft = M; S.ColumnRight = View.X - M; S.ColumnBottom = View.Y - M;
    const FBox2f Screen(FVector2f::ZeroVector, View);
    for (const FPYRTouchControl& C : Controls)
    {
        if (!C.View.Intersect(Screen)) continue;
        switch (C.Zone)
        {
        case EPYRTouchZone::TopBar: S.Top = FMath::Max(S.Top, C.View.Max.Y + M); break;
        case EPYRTouchZone::Stick: S.SideBottom = FMath::Min(S.SideBottom, C.View.Min.Y - M); S.ColumnLeft = FMath::Max(S.ColumnLeft, C.View.Max.X + M); break;
        case EPYRTouchZone::Actions: S.SideBottom = FMath::Min(S.SideBottom, C.View.Min.Y - M); S.ColumnRight = FMath::Min(S.ColumnRight, C.View.Min.X - M); break;
        case EPYRTouchZone::Moves: S.ColumnBottom = FMath::Min(S.ColumnBottom, C.View.Min.Y - M); break;
        }
    }
    S.SideBottom = FMath::Max(S.SideBottom, S.Top);
    S.ColumnBottom = FMath::Max(S.ColumnBottom, S.Top);
    if (S.ColumnRight < S.ColumnLeft) S.ColumnRight = S.ColumnLeft;
    return S;
}

FPYRSafeArea Safe()
{
    const FVector2f View = ViewportSize();
    return SafeFor(Controls(), View.X > 0 ? View : FVector2f(1280, 720));
}

void SetTouch(bool bOn, FVector2f Page)
{
    bPageSaid = true;
    bPageTouch = bOn;
    if (Page.X > 0 && Page.Y > 0) PageSaid = Page;
}

void ResetTouch()
{
    bPageSaid = bPageTouch = false;
    PageSaid = FVector2f::ZeroVector;
}

bool HandleStreamMessage(const FString& Json)
{
    TSharedPtr<FJsonObject> Obj;
    FString Type;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Obj) || !Obj.IsValid() || !Obj->TryGetStringField(TEXT("type"), Type) || Type != TEXT("PYRTouch"))
        return false;
    bool bOn = false;
    Obj->TryGetBoolField(TEXT("touch"), bOn);
    double W = 0, H = 0;
    Obj->TryGetNumberField(TEXT("width"), W);
    Obj->TryGetNumberField(TEXT("height"), H);
    SetTouch(bOn, FVector2f(W, H));
    UE_LOG(LogTemp, Display, TEXT("PYR_LAYOUT the stream page says touch %s, page %.0fx%.0f"), bOn ? TEXT("on") : TEXT("off"), W, H);
    return true;
}

void Start(UWorld* World)
{
    // A remote-play front end would hook its own messages up here and hand them to HandleStreamMessage; this build
    // has none, so touch comes from -TouchLayout, pyr.TouchLayout or SetTouch.
}

bool GuideWanted()
{
    static const bool bAsked = FParse::Param(FCommandLine::Get(), TEXT("TouchGuide"));
    return TouchOn() && !Streamed() && (bAsked || GameTest::Automated());
}

TArray<TPair<FPYRTouchControl, TSharedPtr<SWidget>>> GuideWidgets() { return GuideItems; }

void TickGuide(UWorld* World)
{
    Start(World);
    // The guide's widgets live in statics: let them go before Slate shuts down, not at static destruction.
    static bool bHooked = false;
    if (!bHooked)
    {
        bHooked = true;
        FCoreDelegates::OnEnginePreExit.AddLambda([]
        {
            if (UGameViewportClient* Old = GuideViewport.Get())
                if (GuideCanvas.IsValid()) Old->RemoveViewportWidgetContent(GuideCanvas.ToSharedRef());
            GuideItems.Empty();
            GuideCanvas.Reset();
        });
    }
    const FVector2f View = ViewportSize();
    // The layout as it changes, for the logs and the playtester.
    const FPYRSafeArea S = Safe();
    const FVector2f Page = PageSize();
    const FString State = FString::Printf(TEXT("%s view %.0fx%.0f page %.0fx%.0f touch %s streamed %s safe top %.0f side-bottom %.0f column %.0f-%.0f bottom %.0f"),
        Name(Current()), View.X, View.Y, Page.X, Page.Y, TouchOn() ? TEXT("on") : TEXT("off"), Streamed() ? TEXT("yes") : TEXT("no"),
        S.Top, S.SideBottom, S.ColumnLeft, S.ColumnRight, S.ColumnBottom);
    if (State != LastLogged && View.X > 0)
    {
        LastLogged = State;
        UE_LOG(LogTemp, Display, TEXT("PYR_LAYOUT %s"), *State);
    }

    UGameViewportClient* VC = World ? World->GetGameViewport() : nullptr;
    if (GuideCanvas.IsValid() && GuideViewport.Get() != VC)
    {
        if (UGameViewportClient* Old = GuideViewport.Get()) Old->RemoveViewportWidgetContent(GuideCanvas.ToSharedRef());
        GuideCanvas.Reset();
        GuideItems.Reset();
        GuideKey.Reset();
    }
    const bool bWant = VC && GuideWanted() && View.X > 0;
    if (!bWant)
    {
        if (GuideCanvas.IsValid()) GuideCanvas->SetVisibility(EVisibility::Collapsed);
        GuideItems.Reset();
        GuideKey.Reset();
        return;
    }
    if (!GuideCanvas.IsValid())
    {
        GuideCanvas = SNew(SConstraintCanvas);
        VC->AddViewportWidgetContent(GuideCanvas.ToSharedRef(), 95);
        GuideViewport = VC;
    }
    GuideCanvas->SetVisibility(EVisibility::HitTestInvisible);
    const TArray<FPYRTouchControl> List = Controls();
    FString Key = FString::Printf(TEXT("%.0fx%.0f"), View.X, View.Y);
    for (const FPYRTouchControl& C : List) Key += FString::Printf(TEXT("|%s %.1f %.1f %.1f %.1f"), *C.Label, C.View.Min.X, C.View.Min.Y, C.View.Max.X, C.View.Max.Y);
    if (Key == GuideKey) return;
    GuideKey = Key;
    GuideCanvas->ClearChildren();
    GuideItems.Reset();
    // Drawn as the page draws them: dark translucent rounded buttons with a light edge and white words; the stick a ring.
    static FSlateBrush Rounded = []
    {
        FSlateBrush B;
        B.DrawAs = ESlateBrushDrawType::RoundedBox;
        B.TintColor = FSlateColor(FLinearColor::White);
        B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
        B.OutlineSettings.CornerRadii = FVector4(10, 10, 10, 10);
        B.OutlineSettings.Width = 2.f;
        B.OutlineSettings.Color = FSlateColor(FLinearColor(1, 1, 1, .45f));
        return B;
    }();
    static FSlateBrush Round = []
    {
        FSlateBrush B = Rounded;
        B.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
        return B;
    }();
    for (const FPYRTouchControl& C : List)
    {
        const bool bStick = C.Zone == EPYRTouchZone::Stick;
        const bool bTop = C.Zone == EPYRTouchZone::TopBar;
        TSharedRef<SWidget> Item = SNew(SBorder)
            .BorderImage(bStick ? &Round : &Rounded)
            .BorderBackgroundColor(bTop ? FLinearColor(.08f, .13f, .21f, .67f) : FLinearColor(.09f, .17f, .27f, .7f))
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(C.Label)).ColorAndOpacity(FLinearColor::White)
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), bTop ? 9 : 12))
            ];
        GuideCanvas->AddSlot()
            .Anchors(FAnchors(C.View.Min.X / View.X, C.View.Min.Y / View.Y, C.View.Max.X / View.X, C.View.Max.Y / View.Y))
            .Offset(FMargin(0))
            [
                Item
            ];
        GuideItems.Add({C, Item});
    }
}
}
