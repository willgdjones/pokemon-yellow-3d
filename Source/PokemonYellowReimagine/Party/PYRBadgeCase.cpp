#include "PYRBadgeCase.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Input/Reply.h"
#include "PYRTrainerState.h"
#include "Widgets/SLeafWidget.h"
#include "../Battle/PYRBattleHUD.h"

namespace
{
using namespace PYRDraw;

struct FBadgeLook { const TCHAR* Name; FLinearColor Colour; };
FBadgeLook Look(int32 I)
{
    static const FBadgeLook Looks[] = {
        {TEXT("Boulder"), FLinearColor(FColor::FromHex(TEXT("9a9488")))},
        {TEXT("Cascade"), FLinearColor(FColor::FromHex(TEXT("6ec6f0")))},
        {TEXT("Thunder"), FLinearColor(FColor::FromHex(TEXT("f0a030")))},
        {TEXT("Rainbow"), FLinearColor(FColor::FromHex(TEXT("5cb85c")))},
        {TEXT("Soul"), FLinearColor(FColor::FromHex(TEXT("f070a0")))},
        {TEXT("Marsh"), FLinearColor(FColor::FromHex(TEXT("e8c040")))},
        {TEXT("Volcano"), FLinearColor(FColor::FromHex(TEXT("e3342f")))},
        {TEXT("Earth"), FLinearColor(FColor::FromHex(TEXT("4a9a3a")))},
    };
    return Looks[FMath::Clamp(I, 0, 7)];
}

// Each badge's shape, drawn flat with ink lines in the anime style.
void PaintBadge(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f C, float R, int32 I)
{
    const FLinearColor Col = Look(I).Colour, White(1, 1, 1);
    auto Poly = [&](int32 N, float Radius, float Spin, FLinearColor Colour, float Thick)
    {
        TArray<FVector2f> P;
        for (int32 k = 0; k <= N; ++k) { const float A = Spin + 2 * PI * k / N; P.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * Radius); }
        Lines(Out, Layer + 2, G, P, Colour, Thick);
    };
    switch (I)
    {
    case 0:  // Boulder: a grey octagon.
        InkBox(Out, Layer, G, C - FVector2f(R, R), FVector2f(2 * R, 2 * R), Col, 3.f, Round());
        Poly(8, R * .72f, PI / 8, Ink, 4.f);
        Poly(8, R * .4f, PI / 8, White * .9f, 3.f);
        break;
    case 1:  // Cascade: a blue raindrop.
        InkBox(Out, Layer, G, C - FVector2f(R * .75f, R * .5f), FVector2f(1.5f * R, 1.5f * R), Col, 3.f, Round());
        Lines(Out, Layer + 1, G, {C + FVector2f(-R * .72f, R * .1f), C + FVector2f(0, -R * 1.05f), C + FVector2f(R * .72f, R * .1f)}, Ink, 4.f);
        Box(Out, Layer + 2, G, C + FVector2f(-R * .3f, -R * .05f), FVector2f(R * .25f, R * .25f), White, Round());
        break;
    case 2:  // Thunder: an orange sun with spikes.
        InkBox(Out, Layer, G, C - FVector2f(R * .6f, R * .6f), FVector2f(1.2f * R, 1.2f * R), Col, 3.f, Round());
        for (int32 k = 0; k < 8; ++k)
        {
            const float A = 2 * PI * k / 8;
            Lines(Out, Layer + 1, G, {C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R * .65f, C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R * 1.05f}, Col * .8f, 5.f);
        }
        Box(Out, Layer + 2, G, C - FVector2f(R * .25f, R * .25f), FVector2f(R * .5f, R * .5f), Ink, Round());
        break;
    case 3:  // Rainbow: a flower of coloured petals.
        for (int32 k = 0; k < 6; ++k)
        {
            const float A = 2 * PI * k / 6;
            static const TCHAR* Petals[] = {TEXT("e3342f"), TEXT("f0a030"), TEXT("ffd733"), TEXT("5cb85c"), TEXT("6ec6f0"), TEXT("a860c8")};
            InkBox(Out, Layer, G, C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R * .55f - FVector2f(R * .42f, R * .42f), FVector2f(R * .84f, R * .84f),
                FLinearColor(FColor::FromHex(Petals[k])), 2.f, Round());
        }
        InkBox(Out, Layer + 2, G, C - FVector2f(R * .3f, R * .3f), FVector2f(R * .6f, R * .6f), White, 2.f, Round());
        break;
    case 4:  // Soul: a pink heart.
        InkBox(Out, Layer, G, C + FVector2f(-R * .9f, -R * .6f), FVector2f(R, R), Col, 3.f, Round());
        InkBox(Out, Layer, G, C + FVector2f(-R * .1f, -R * .6f), FVector2f(R, R), Col, 3.f, Round());
        Lines(Out, Layer + 1, G, {C + FVector2f(-R * .85f, R * .05f), C + FVector2f(0, R * .95f), C + FVector2f(R * .85f, R * .05f)}, Col, 9.f);
        Lines(Out, Layer + 2, G, {C + FVector2f(-R * .9f, R * .1f), C + FVector2f(0, R), C + FVector2f(R * .9f, R * .1f)}, Ink, 3.f);
        break;
    case 5:  // Marsh: two gold rings.
        Ring(Out, Layer, G, C, R * .85f, 1.f, Col, 7.f);
        Ring(Out, Layer + 1, G, C, R * .45f, 1.f, Col * .8f, 7.f);
        Ring(Out, Layer + 2, G, C, R * 1.f, 1.f, Ink, 2.f);
        break;
    case 6:  // Volcano: a red flame.
        InkBox(Out, Layer, G, C - FVector2f(R * .65f, R * .35f), FVector2f(1.3f * R, 1.3f * R), Col, 3.f, Round());
        Lines(Out, Layer + 1, G, {C + FVector2f(-R * .6f, 0), C + FVector2f(-R * .3f, -R * .9f), C + FVector2f(0, -R * .35f), C + FVector2f(R * .3f, -R * 1.05f), C + FVector2f(R * .6f, 0)}, Ink, 4.f);
        Box(Out, Layer + 2, G, C + FVector2f(-R * .25f, R * .05f), FVector2f(R * .5f, R * .5f), FLinearColor(FColor::FromHex(TEXT("ffd733"))), Round());
        break;
    default:  // Earth: a green leaf.
        InkBox(Out, Layer, G, C - FVector2f(R * .9f, R * .5f), FVector2f(1.8f * R, R), Col, 3.f, Round());
        Lines(Out, Layer + 1, G, {C + FVector2f(-R * .8f, R * .3f), C + FVector2f(R * .8f, -R * .3f)}, Ink, 3.f);
        break;
    }
}

class SPYRBadgeCaseWidget : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SPYRBadgeCaseWidget) {}
        SLATE_ARGUMENT(APlayerController*, Controller)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args) { Controller = Args._Controller; }

    TArray<bool> Lit() const
    {
        TArray<bool> Out;
        const UPYRTrainerState* State = Controller.IsValid() ? UPYRTrainerState::Get(Controller.Get()) : nullptr;
        for (int32 I = 0; I < int32(EPYRBadge::Count); ++I) Out.Add(State && State->HasBadge(EPYRBadge(I)));
        return Out;
    }

    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1280, 720); }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Key) override
    {
        if (Key.GetKey() == EKeys::B || Key.GetKey() == EKeys::Escape || Key.GetKey() == EKeys::BackSpace) { UPYRBadgeCase::Close(); return FReply::Handled(); }
        return FReply::Unhandled();
    }
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { UPYRBadgeCase::Close(); return FReply::Handled(); }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer,
        const FWidgetStyle& Style, bool bParentEnabled) const override
    {
        const FVector2f Size = FVector2f(G.GetLocalSize());
        const TArray<bool> On = Lit();
        int32 Count = 0;
        for (bool b : On) Count += b ? 1 : 0;
        const FVector2f CaseSize(820, 440);
        const FVector2f At = (Size - CaseSize) * .5f;
        const FLinearColor Leather(FColor::FromHex(TEXT("8a4a2a"))), Velvet(FColor::FromHex(TEXT("3a2a5a")));
        Box(Out, Layer, G, FVector2f(0, 0), Size, Ink * FLinearColor(1, 1, 1, .45f));
        InkBox(Out, Layer + 1, G, At, CaseSize, Leather, 5.f, Panel());
        InkBox(Out, Layer + 2, G, At + FVector2f(24, 70), CaseSize - FVector2f(48, 94), Velvet, 3.f, Panel());
        InkText(Out, Layer + 3, G, At + FVector2f(28, 16), FString::Printf(TEXT("BADGES   %d / 8"), Count), 26, Sun, 3.f);
        Text(Out, Layer + 3, G, At + FVector2f(CaseSize.X - 190, 26), TEXT("B or Esc closes"), 13, Paper, false);
        for (int32 I = 0; I < 8; ++I)
        {
            const FVector2f C = At + FVector2f(24 + 97 + (I % 4) * 194, 70 + 88 + (I / 4) * 172);
            const float R = 46;
            // The socket, then the badge if earned.
            InkBox(Out, Layer + 3, G, C - FVector2f(R + 8, R + 8), FVector2f(2 * R + 16, 2 * R + 16), Velvet * .6f, 3.f, Round());
            if (On[I]) PaintBadge(Out, Layer + 5, G, C, R, I);
            else Ring(Out, Layer + 5, G, C, R * .6f, 1.f, Paper * FLinearColor(1, 1, 1, .25f), 3.f);
            const FString Name = Look(I).Name;
            const FVector2f TS = Measure(Name, 14);
            Text(Out, Layer + 9, G, C + FVector2f(-TS.X * .5f, R + 14), Name, 14, On[I] ? Paper : Paper * FLinearColor(1, 1, 1, .4f), false);
        }
        return Layer + 12;
    }

private:
    TWeakObjectPtr<APlayerController> Controller;
};

TSharedPtr<SPYRBadgeCaseWidget> GCase;
TWeakObjectPtr<APlayerController> GCasePC;
}

void PYRShowBadgeCase(APlayerController* PC) { UPYRBadgeCase::Open(PC); }

void UPYRBadgeCase::Open(APlayerController* PC)
{
    if (GCase.IsValid() || !PC) return;
    GCasePC = PC;
    GCase = SNew(SPYRBadgeCaseWidget).Controller(PC);
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->AddViewportWidgetContent(GCase.ToSharedRef(), 45);
    FInputModeGameAndUI Mode;
    Mode.SetWidgetToFocus(GCase);
    Mode.SetHideCursorDuringCapture(false);
    PC->SetInputMode(Mode);
    PC->bShowMouseCursor = true;
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(GCase);
}

void UPYRBadgeCase::Toggle(APlayerController* PC)
{
    if (IsOpen()) Close();
    else Open(PC);
}

bool UPYRBadgeCase::IsOpen() { return GCase.IsValid(); }

void UPYRBadgeCase::Close()
{
    if (GCase.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(GCase.ToSharedRef());
    GCase.Reset();
    if (APlayerController* PC = GCasePC.Get())
    {
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;
    }
    GCasePC = nullptr;
}

TArray<bool> UPYRBadgeCase::ShownSlots() { return GCase.IsValid() ? GCase->Lit() : TArray<bool>(); }

FString UPYRBadgeCase::Describe()
{
    const TArray<bool> On = ShownSlots();
    TArray<FString> Parts;
    for (int32 I = 0; I < On.Num(); ++I) Parts.Add(FString::Printf(TEXT("%s: %s"), Look(I).Name, On[I] ? TEXT("earned") : TEXT("empty")));
    return FString::Join(Parts, TEXT(", "));
}
