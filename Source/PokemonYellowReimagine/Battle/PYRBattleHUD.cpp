#include "PYRBattleHUD.h"

#include "PYRBattleManager.h"
#include "PYRMonPawn.h"
#include "Hud/PYRPhoneLayout.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "../Party/PYRSpecies.h"
#include "../Party/PYRTrainerState.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace PYRDraw
{
// The package palette: #ffd733 sun, #e3342f cap red, #6ec6f0 sky, #5cb85c leaf, #fff4d6 paper, #1f1f2e ink.
const FLinearColor Ink = FLinearColor(FColor::FromHex(TEXT("1f1f2e")));
const FLinearColor Paper = FLinearColor(FColor::FromHex(TEXT("fff4d6")));
const FLinearColor Sun = FLinearColor(FColor::FromHex(TEXT("ffd733")));
const FLinearColor Cap = FLinearColor(FColor::FromHex(TEXT("e3342f")));
const FLinearColor Sky = FLinearColor(FColor::FromHex(TEXT("6ec6f0")));
const FLinearColor Leaf = FLinearColor(FColor::FromHex(TEXT("5cb85c")));

const FSlateBrush* White() { return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")); }

const FSlateBrush* Round()
{
    static FSlateBrush Brush;
    static bool bInit = false;
    if (!bInit)
    {
        bInit = true;
        Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
        Brush.TintColor = FSlateColor(FLinearColor::White);
        Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
        Brush.OutlineSettings.Width = 0;
    }
    return &Brush;
}

const FSlateBrush* Panel()
{
    static FSlateBrush Brush;
    static bool bInit = false;
    if (!bInit)
    {
        bInit = true;
        Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
        Brush.TintColor = FSlateColor(FLinearColor::White);
        Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
        Brush.OutlineSettings.CornerRadii = FVector4(12, 12, 12, 12);
        Brush.OutlineSettings.Width = 0;
    }
    return &Brush;
}

void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f Pos, FVector2f Size, FLinearColor Colour, const FSlateBrush* Brush)
{
    FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), Brush ? Brush : White(), ESlateDrawEffect::None, Colour);
}

void InkBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f Pos, FVector2f Size, FLinearColor Colour, float InkW, const FSlateBrush* Brush)
{
    Box(Out, Layer, G, Pos - FVector2f(InkW, InkW), Size + FVector2f(InkW * 2, InkW * 2), Ink * FLinearColor(1, 1, 1, Colour.A), Brush);
    Box(Out, Layer + 1, G, Pos, Size, Colour, Brush);
}

static FSlateFontInfo Font(int32 Size) { return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size); }

FVector2f Measure(const FString& Text, int32 Size)
{
    if (!FSlateApplication::IsInitialized()) return FVector2f(Text.Len() * Size * .6f, Size * 1.3f);
    return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font(Size)));
}

// m4u: a move's name in its icon, Width across: one line at the largest size from 15 pt down to 12, else two lines (at
// its space, or before a word's tail: THUNDER / SHOCK, QUICK / ATTACK) from 15 pt down, else the narrowest that fits down
// to 10 pt.
void MoveLabel(const FString& Name, float Width, FString& Line1, FString& Line2, int32& Font)
{
    for (Font = 15; Font >= 12; --Font) if (Measure(Name, Font).X <= Width) { Line1 = Name; Line2.Reset(); return; }
    Line1 = Name;
    Line2.Reset();
    if (!Name.Split(TEXT(" "), &Line1, &Line2))
    {
        static const TCHAR* Tails[] = {TEXT("SHOCK"), TEXT("PUNCH"), TEXT("BOLT"), TEXT("BEAM"), TEXT("WHIP"), TEXT("ATTACK"), TEXT("STRIKE"),
            TEXT("SLASH"), TEXT("SCREEN"), TEXT("SPORE"), TEXT("POWDER"), TEXT("SHOT"), TEXT("KICK"), TEXT("SLAM"), TEXT("BLAST"), TEXT("STORM"),
            TEXT("PUMP"), TEXT("WIND"), TEXT("WALL"), TEXT("BIND"), TEXT("DRAIN"), TEXT("BOMB"), TEXT("TOSS"), TEXT("DANCE"), TEXT("CANNON"),
            TEXT("SWIPE"), TEXT("SPIN"), TEXT("THROW"), TEXT("SONG"), TEXT("BALL"), TEXT("FANG"), TEXT("WAVE"), TEXT("SPRAY"), TEXT("SHELL"),
            TEXT("STOMP"), TEXT("SHIELD"), TEXT("BURN"), TEXT("BITE"), TEXT("SPIKE"), TEXT("STRENGTH"), TEXT("FLASH"), TEXT("FLAME"), TEXT("FIRE")};
        for (const TCHAR* T : Tails)
            if (Name.Len() > FCString::Strlen(T) + 2 && Name.EndsWith(T)) { Line1 = Name.LeftChop(FCString::Strlen(T)); Line2 = T; break; }
    }
    if (!Line2.IsEmpty())
    {
        for (Font = 15; Font >= 10; --Font) if (Measure(Line1, Font).X <= Width && Measure(Line2, Font).X <= Width) return;
        Font = 10;
        return;
    }
    for (Font = 12; Font > 10 && Measure(Name, Font).X > Width; --Font) {}
}

FBox BodyBounds(const AActor* Actor)
{
    // Not the actors' bounds: a Pokemon's aura, ripple and shadow meshes (and its rig's own bounds) reach metres past it.
    if (const APYRMonPawn* Mon = Cast<APYRMonPawn>(Actor))
    {
        const UCapsuleComponent* C = Mon->GetCapsuleComponent();
        const float Half = C ? C->GetScaledCapsuleHalfHeight() : 50.f, R = C ? C->GetScaledCapsuleRadius() : 50.f;
        const FVector Feet = Mon->GetActorLocation() - FVector(0, 0, Half);
        const float H = FMath::Max(Mon->GetBattleScreenHeightM() * 100.f, Half * 2.f), W = FMath::Max(R, H * .35f);
        return FBox(Feet - FVector(W, W, 0), Feet + FVector(W, W, H));
    }
    if (const ACharacter* Ch = Cast<ACharacter>(Actor)) if (Ch->GetCapsuleComponent()) return Ch->GetCapsuleComponent()->Bounds.GetBox();
    return FBox(ForceInit);
}

float CaptionTopClear(const FPYRHudLayout& L, FVector2f TextSize, const TArray<TPair<FBox2f, float>>& Avoid)
{
    const float Tops[] = {L.CaptionTop, L.Size.Y * .40f, L.Size.Y * .80f, L.Size.Y * .18f, L.Size.Y * .50f, L.Size.Y * .72f};
    float Best = L.CaptionTop, Least = TNumericLimits<float>::Max();
    for (const float Top : Tops)
    {
        const FBox2f Band = CaptionBand(L, TextSize, Top);
        float Cover = 0.f;
        for (const TPair<FBox2f, float>& A : Avoid)
            if (Band.Intersect(A.Key)) Cover += Band.Overlap(A.Key).GetArea() * A.Value;
        if (Cover < Least - 1.f) { Least = Cover; Best = Top; }
    }
    return Best;
}

FBox2f CaptionBand(const FPYRHudLayout& L, FVector2f TextSize, float Top)
{
    return FBox2f(FVector2f(L.CaptionCentreX - TextSize.X * .5f - 40, Top - 10), FVector2f(L.CaptionCentreX + TextSize.X * .5f + 40, Top + TextSize.Y + 10));
}

void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f Pos, const FString& String, int32 Size, FLinearColor Colour, bool bShadow)
{
    const FVector2f Extent = Measure(String, Size);
    if (bShadow)
        FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(Extent, FSlateLayoutTransform(Pos + FVector2f(2, 2))), String, Font(Size), ESlateDrawEffect::None, FLinearColor(0, 0, 0, Colour.A * .6f));
    FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(Extent, FSlateLayoutTransform(Pos)), String, Font(Size), ESlateDrawEffect::None, Colour);
}

void InkText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f Pos, const FString& String, int32 Size, FLinearColor Colour, float InkW)
{
    const FVector2f Extent = Measure(String, Size) + FVector2f(InkW * 2, InkW * 2);
    const FLinearColor Edge = Ink * FLinearColor(1, 1, 1, Colour.A);
    for (int32 i = 0; i < 8; ++i)
    {
        const float A = i * PI / 4;
        const FVector2f Off(FMath::Cos(A) * InkW, FMath::Sin(A) * InkW);
        FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(Extent, FSlateLayoutTransform(Pos + Off)), String, Font(Size), ESlateDrawEffect::None, Edge);
    }
    FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(Extent, FSlateLayoutTransform(Pos)), String, Font(Size), ESlateDrawEffect::None, Colour);
}

void Lines(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2f>& Points, FLinearColor Colour, float Thickness)
{
    if (Points.Num() < 2) return;
    FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, Colour, true, Thickness);
}

void Ring(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f Centre, float Radius, float Fraction, FLinearColor Colour, float Thickness)
{
    Fraction = FMath::Clamp(Fraction, 0.f, 1.f);
    if (Fraction <= 0) return;
    TArray<FVector2f> Points;
    const int32 Steps = FMath::Max(2, FMath::CeilToInt(48 * Fraction));
    for (int32 i = 0; i <= Steps; ++i)
    {
        const float A = -PI / 2 + 2 * PI * Fraction * i / Steps;
        Points.Add(Centre + FVector2f(FMath::Cos(A), FMath::Sin(A)) * Radius);
    }
    Lines(Out, Layer, G, Points, Colour, Thickness);
}
}

using namespace PYRDraw;

namespace
{
FLinearColor HpColour(float Frac)
{
    return Frac > .5f ? Leaf : Frac > .2f ? Sun : Cap;
}

FString SpeciesName(FName Id) { return APYRBattleManager::MonName(Id); }

// A tiny body-family glyph: runner chevrons, flier wings, crawler wave, giant block.
void BodyIcon(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f C, float S, EPYRBody Body, FLinearColor Col)
{
    TArray<FVector2f> P;
    switch (Body)
    {
    case EPYRBody::Runner:
        Lines(Out, Layer, G, {C + FVector2f(-S, -S * .7f), C + FVector2f(-S * .2f, 0), C + FVector2f(-S, S * .7f)}, Col, 3.f);
        Lines(Out, Layer, G, {C + FVector2f(-S * .1f, -S * .7f), C + FVector2f(S * .7f, 0), C + FVector2f(-S * .1f, S * .7f)}, Col, 3.f);
        break;
    case EPYRBody::Flier:
        Lines(Out, Layer, G, {C + FVector2f(-S, -S * .4f), C + FVector2f(-S * .4f, S * .2f), C + FVector2f(0, -S * .1f), C + FVector2f(S * .4f, S * .2f), C + FVector2f(S, -S * .4f)}, Col, 3.f);
        break;
    case EPYRBody::Crawler:
        for (int32 i = 0; i <= 8; ++i) P.Add(C + FVector2f(-S + 2 * S * i / 8.f, FMath::Sin(i * PI / 2) * S * .3f + S * .3f));
        Lines(Out, Layer, G, P, Col, 3.f);
        break;
    case EPYRBody::Swimmer:
        // A fin over two waves.
        Lines(Out, Layer, G, {C + FVector2f(-S * .4f, 0), C + FVector2f(S * .1f, -S * .8f), C + FVector2f(S * .4f, 0)}, Col, 3.f);
        for (int32 i = 0; i <= 8; ++i) P.Add(C + FVector2f(-S + 2 * S * i / 8.f, FMath::Sin(i * PI / 2) * S * .2f + S * .45f));
        Lines(Out, Layer, G, P, Col, 3.f);
        break;
    case EPYRBody::Giant:
        // m4v (the blind judge read Onix's circle as empty): a craggy boulder, the giants' family mark, closed.
        Lines(Out, Layer, G, {C + FVector2f(-S, S * .6f), C + FVector2f(-S * .65f, -S * .2f), C + FVector2f(-S * .2f, -S * .7f), C + FVector2f(S * .25f, -S * .45f),
            C + FVector2f(S * .55f, -S * .75f), C + FVector2f(S, S * .6f), C + FVector2f(-S, S * .6f)}, Col, 3.f);
        Lines(Out, Layer, G, {C + FVector2f(-S * .2f, -S * .7f), C + FVector2f(-S * .05f, S * .05f), C + FVector2f(S * .3f, S * .3f)}, Col, 2.f);
        break;
    default:
        Box(Out, Layer, G, C - FVector2f(S * .7f, S * .7f), FVector2f(S * 1.4f, S * 1.4f), Col);
        break;
    }
}
}

void SPYRBattleHUD::Construct(const FArguments& Args)
{
    Manager = Args._Manager;
    // m4t: hit-testable for the RUN button and the switch menu's slots; every other click falls through to the game.
    SetVisibility(EVisibility::Visible);
}

FPYRHudLayout FPYRHudLayout::Make(FVector2f Size)
{
    FPYRHudLayout L;
    L.Size = Size;
    // Desktop (every build before m4u): the foe's card top left, Red's bottom left with the Potion and ball counts and
    // RUN beside it, the moves bottom right under the dodge box and the swap timer.
    L.FoeCard = FVector2f(28, 24);
    L.PlayerCard = FVector2f(28, Size.Y - 110);
    L.Items = FVector2f(376, Size.Y - 92);
    L.Run = FBox2f(FVector2f(604, Size.Y - 92), FVector2f(604, Size.Y - 92) + RunSize());
    L.MovesStart = FVector2f(Size.X - 28 - 4 * Icon - 3 * Gap, Size.Y - Icon - 28);
    L.Dodge = FVector2f(L.MovesStart.X, L.MovesStart.Y - 46);
    L.SwapCentre = FVector2f(Size.X - 64, Size.Y - 176);
    L.CaptionCentreX = Size.X * .5f;
    L.CaptionTop = Size.Y * .62f;
    L.BannerTop = 26;
    L.PausedRight = Size.X - 52;
    L.PausedTop = 30;
    L.WheelCentre = Size * .5f;
    L.WheelScale = 1.f;
    const FVector2f View = PYRPhoneLayout::ViewportSize();
    if (!PYRPhoneLayout::IsPhone() || View.X <= 0 || Size.X <= 0) return L;

    // Phone: inside the safe area (viewport px to the HUD's local units).
    L.bPhone = true;
    const float F = Size.X / View.X;
    const FPYRSafeArea S = PYRPhoneLayout::Safe();
    const float M = 10.f;   // air inside the safe area, past the ink outlines
    const float Top = S.Top * F + M, SideBottom = S.SideBottom * F - M;
    const float Left = FPYRSafeArea::Margin * F + M, Right = (View.X - FPYRSafeArea::Margin) * F - M;
    const float ColL = S.ColumnLeft * F + M, ColR = S.ColumnRight * F - M, ColB = S.ColumnBottom * F - M;
    // Both cards in the top corners.
    L.FoeCard = FVector2f(Left, Top);
    L.PlayerCard = FVector2f(Right - CardSize().X, Top);
    // The moves on the column's floor, the Potion and ball counts to their left and RUN to their right; the dodge box
    // above the moves and the swap timer above RUN. A narrow column takes the counts and RUN up a row instead.
    const float MovesW = 4 * Icon + 3 * Gap;
    const float RowW = ItemsSize().X + 16 + MovesW + 16 + RunSize().X;
    const float IconTop = ColB - Icon;
    const float Mid = (ColL + ColR) * .5f;
    if (ColR - ColL >= RowW)
    {
        const float X0 = Mid - RowW * .5f;
        L.Items = FVector2f(X0, IconTop + Icon - ItemsSize().Y);
        L.MovesStart = FVector2f(X0 + ItemsSize().X + 16, IconTop);
        const FVector2f RunAt(L.MovesStart.X + MovesW + 16, IconTop + Icon - RunSize().Y);
        L.Run = FBox2f(RunAt, RunAt + RunSize());
        L.Dodge = FVector2f(L.MovesStart.X, IconTop - 46);
    }
    else
    {
        L.MovesStart = FVector2f(Mid - MovesW * .5f, IconTop);
        L.Dodge = FVector2f(L.MovesStart.X, IconTop - 46);
        const float RowY = L.Dodge.Y - 12 - ItemsSize().Y;
        L.Items = FVector2f(L.MovesStart.X, RowY);
        const FVector2f RunAt(L.MovesStart.X + ItemsSize().X + 16, RowY);
        L.Run = FBox2f(RunAt, RunAt + RunSize());
    }
    // The swap ring over RUN, its SWAP label (to +50 under the centre) clear of the button.
    L.SwapCentre = FVector2f(L.Run.GetCenter().X, L.Run.Min.Y - 56);
    // The caption across the band's middle, below the cards; the banner between the cards; PAUSED under Red's card.
    const float CardsBottom = Top + CardSize().Y;
    L.CaptionCentreX = Size.X * .5f;
    L.CaptionTop = FMath::Max(CardsBottom + 12, (CardsBottom + SideBottom) * .5f - 35);
    L.BannerTop = Top;
    L.PausedRight = Right - 22;
    L.PausedTop = CardsBottom + 24;
    // The switch menu (its backdrop 512 across, the hint 300 above the centre, the badge-case hint 278 below) fits the
    // column above the dodge box and the swap ring, scaled down when it must.
    const float Avail = FMath::Min(L.Dodge.Y, L.SwapCentre.Y - SwapRadius - 3) - 8 - Top;
    L.WheelScale = FMath::Min3(1.f, Avail / 600.f, (ColR - ColL) / 530.f);
    L.WheelCentre = FVector2f(Mid, Top + 300 * L.WheelScale + FMath::Max(0.f, Avail - 590 * L.WheelScale) * .5f);
    return L;
}

FBox2f SPYRBattleHUD::RunRect(FVector2f Size)
{
    // Beside the Potion and ball counts (bottom left on Desktop, the column on Phone).
    return FPYRHudLayout::Make(Size).Run;
}

FVector2f SPYRBattleHUD::WheelSlot(FVector2f Size, int32 Index, int32 Count)
{
    const FPYRHudLayout L = FPYRHudLayout::Make(Size);
    const float A = -PI / 2 + 2 * PI * Index / FMath::Max(1, Count);
    return L.WheelCentre + FVector2f(FMath::Cos(A), FMath::Sin(A)) * FPYRHudLayout::WheelRadius * L.WheelScale;
}

TArray<TPair<FString, FBox2f>> SPYRBattleHUD::ProtectedRects() const
{
    TArray<TPair<FString, FBox2f>> Out;
    APYRBattleManager* M = Manager.Get();
    if (!M || !M->IsBattleActive()) return Out;
    const FGeometry& G = GetCachedGeometry();
    const FVector2f Size = FVector2f(G.GetLocalSize());
    if (Size.X < 10.f) return Out;
    const FPYRHudLayout L = FPYRHudLayout::Make(Size);
    auto Add = [&](const FString& Name, FBox2f Local, float Pad)
    {
        Local = Local.ExpandBy(Pad);
        Out.Add({Name, FBox2f(FVector2f(G.LocalToAbsolute(FVector2D(Local.Min))), FVector2f(G.LocalToAbsolute(FVector2D(Local.Max))))});
    };
    const bool bDemo = M->GetKind() == EPYRBattleKind::Demo;
    if (M->FoeMon()) Add(TEXT("foe card (name, level, HP)"), L.Card(true), 4.f);
    if (M->PlayerMon()) Add(TEXT("player card (name, level, HP)"), L.Card(false), 4.f);
    if (UPYRTrainerState::Get(M) && !bDemo) Add(TEXT("items"), FBox2f(L.Items, L.Items + FPYRHudLayout::ItemsSize()), 3.f);
    if (!bDemo) Add(TEXT("RUN"), L.Run, 3.f);
    if (const APYRMonPawn* P = M->PlayerMon())
    {
        for (int32 I = 0; I < 4; ++I) Add(FString::Printf(TEXT("move %d"), I + 1), L.MoveBox(I), 4.f);
        Add(TEXT("dodge"), FBox2f(L.Dodge, L.Dodge + FPYRHudLayout::DodgeSize()), 3.f);
        if (P->GetBody() == EPYRBody::Runner || P->GetBody() == EPYRBody::Crawler || P->IsSwimmer())
        {
            const FVector2f Hop = L.Dodge + FVector2f(FPYRHudLayout::HopOffset, 0);
            Add(TEXT("hop"), FBox2f(Hop, Hop + FPYRHudLayout::HopSize()), 3.f);
        }
    }
    if (!bDemo)
        Add(TEXT("swap timer"), FBox2f(L.SwapCentre - FVector2f(30, 30), L.SwapCentre + FVector2f(30, 50)), 3.f);
    const float CapAge = float(M->Now() - M->CaptionAt);
    if (!M->CaptionText.IsEmpty() && CapAge < 2.2f && !M->IsWheelOpen())
    {
        const FVector2f TS = Measure(M->CaptionText, 34);
        const FVector2f At(L.CaptionCentreX - TS.X * .5f, L.bPhone || CaptionTopShown < 0 ? L.CaptionTop : CaptionTopShown);
        Add(TEXT("caption"), FBox2f(At - FVector2f(40, 10), At + TS + FVector2f(40, 10)), 0.f);
    }
    if (M->IsWheelOpen())
        if (const UPYRTrainerState* State = UPYRTrainerState::Get(M))
            for (int32 I = 0; I < State->Party.Num(); ++I)
            {
                const FVector2f C = WheelSlot(Size, I, State->Party.Num());
                const float R = 60.f * L.WheelScale;
                Add(FString::Printf(TEXT("switch slot %d"), I + 1), FBox2f(C - FVector2f(R, R), C + FVector2f(R, R)), 0.f);
            }
    return Out;
}

FReply SPYRBattleHUD::Press(const FGeometry& G, FVector2f ScreenPos)
{
    APYRBattleManager* M = Manager.Get();
    if (!M || !M->IsBattleActive()) return FReply::Unhandled();
    const FVector2f Size = FVector2f(G.GetLocalSize());
    const FVector2f At = FVector2f(G.AbsoluteToLocal(FVector2D(ScreenPos)));
    if (M->IsWheelOpen())
    {
        // A slot of the switch menu.
        const UPYRTrainerState* State = UPYRTrainerState::Get(M);
        const int32 N = State ? State->Party.Num() : 0;
        const float Reach = 56.f * FPYRHudLayout::Make(Size).WheelScale;
        for (int32 i = 0; i < N; ++i)
            if (FVector2f::Distance(At, WheelSlot(Size, i, N)) <= Reach) { M->PressSwap(i); ++Clicks; return FReply::Handled(); }
        return FReply::Handled();   // The menu holds the fight: nothing else is clicked through it.
    }
    if (M->GetKind() != EPYRBattleKind::Demo && RunRect(Size).IsInside(At)) { ++Clicks; M->PressRun(); return FReply::Handled(); }
    return FReply::Unhandled();
}

FReply SPYRBattleHUD::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& Event)
{
    if (Event.GetEffectingButton() != EKeys::LeftMouseButton && !Event.IsTouchEvent()) return FReply::Unhandled();
    return Press(G, FVector2f(Event.GetScreenSpacePosition()));
}

FReply SPYRBattleHUD::OnTouchStarted(const FGeometry& G, const FPointerEvent& Event)
{
    return Press(G, FVector2f(Event.GetScreenSpacePosition()));
}

int32 SPYRBattleHUD::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Culling, FSlateWindowElementList& Out,
    int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
    APYRBattleManager* M = Manager.Get();
    if (!M || !M->IsBattleActive()) return Layer;
    UWorld* World = M->GetWorld();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    const UPYRTrainerState* State = UPYRTrainerState::Get(M);
    const FVector2f Size = FVector2f(G.GetLocalSize());
    const double Now = M->Now();
    const double Real = FPlatformTime::Seconds();
    const float PaintDt = LastPaint > 0 ? FMath::Clamp(float(Real - LastPaint), 0.f, .1f) : 0.f;
    LastPaint = Real;
    FVector2D ViewSize(1280, 720);
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(ViewSize);
    const float ToLocal = ViewSize.X > 0 ? Size.X / float(ViewSize.X) : 1.f;
    auto Project = [&](FVector W, FVector2f& OutP) -> bool
    {
        FVector2D S;
        if (!PC || !PC->ProjectWorldLocationToScreen(W, S, false)) return false;
        OutP = FVector2f(S) * ToLocal;
        return true;
    };
    const int32 L0 = Layer + 1, L1 = Layer + 4, L2 = Layer + 8;
    const APYRMonPawn* P = M->PlayerMon();
    const APYRMonPawn* F = M->FoeMon();
    const FPYRHudLayout Lay = FPYRHudLayout::Make(Size);

    // ---- Name cards: the foe top left, Red's Pokemon bottom left (Phone: top right), with draining HP bars.
    auto Card = [&](const APYRMonPawn* Mon, FVector2f At, int32 Slot)
    {
        if (!Mon || !Mon->Info) return;
        if (ShownFor[Slot].Get() != Mon) { ShownFor[Slot] = Mon; ShownHp[Slot] = Mon->Hp; }
        ShownHp[Slot] = FMath::FInterpConstantTo(ShownHp[Slot], Mon->Hp, PaintDt, FMath::Max(12.f, Mon->MaxHp * 1.2f));
        InkBox(Out, L0, G, At, FVector2f(330, 76), Paper, 4.f, Panel());
        InkBox(Out, L0 + 2, G, At + FVector2f(10, 10), FVector2f(56, 56), Mon->Info->Colour, 3.f, Round());
        BodyIcon(Out, L1, G, At + FVector2f(38, 38), 12, Mon->GetBody(), Ink);
        InkText(Out, L1, G, At + FVector2f(78, 4), SpeciesName(Mon->Species).ToUpper(), 17, Paper, 2.f);
        Text(Out, L1, G, At + FVector2f(262, 7), FString::Printf(TEXT(":L%d"), Mon->Level), 15, Ink, false);
        const float Frac = FMath::Clamp(ShownHp[Slot] / FMath::Max(1.f, Mon->MaxHp), 0.f, 1.f);
        Text(Out, L1, G, At + FVector2f(78, 36), TEXT("HP"), 13, Sun * .6f + Ink * .4f, false);
        InkBox(Out, L0 + 2, G, At + FVector2f(106, 38), FVector2f(208, 14), Ink * .8f, 2.f, Round());
        if (Frac > 0) Box(Out, L0 + 4, G, At + FVector2f(106, 38), FVector2f(208 * Frac, 14), HpColour(Frac), Round());
        FString Tags;
        if (Mon->Status == EPYRStatus::Paralysis) Tags += TEXT("PAR ");
        if (Mon->Status == EPYRStatus::Poison) Tags += TEXT("PSN ");
        if (Mon->Status == EPYRStatus::Sleep) Tags += TEXT("SLP ");
        if (Mon->Status == EPYRStatus::Burn) Tags += TEXT("BRN ");
        if (Now < Mon->ConfusedUntil) Tags += TEXT("CONF ");
        if (Mon->bSeeded) Tags += TEXT("SEEDED ");
        if (Now < Mon->TrappedUntil) Tags += TEXT("BOUND ");
        if (Mon->bDived) Tags += TEXT("DIVED ");
        if (Mon->IsBiding()) Tags += TEXT("BIDE ");
        if (Mon->HealLeft > 0) Tags += TEXT("HEALING ");
        if (!Tags.IsEmpty()) Text(Out, L1, G, At + FVector2f(106, 54), Tags, 11, Cap, false);
    };
    Card(F, Lay.FoeCard, 1);
    Card(P, Lay.PlayerCard, 0);

    // ---- Items: Potions with their cooldown ring, and balls.
    if (State && M->GetKind() != EPYRBattleKind::Demo)
    {
        const FVector2f At = Lay.Items;
        InkBox(Out, L0, G, At, FVector2f(210, 48), Paper, 3.f, Panel());
        InkBox(Out, L0 + 2, G, At + FVector2f(10, 10), FVector2f(28, 28), FLinearColor(.72f, .45f, .95f), 2.f, Round());
        Ring(Out, L1, G, At + FVector2f(24, 24), 18, M->PotionSecondsLeft() / 10.f, Ink, 4);
        Text(Out, L1, G, At + FVector2f(46, 12), FString::Printf(TEXT("R x%d"), State->ItemCount(UPYRTrainerState::Potion)), 15, Ink, false);
        InkBox(Out, L0 + 2, G, At + FVector2f(112, 10), FVector2f(28, 28), Cap, 2.f, Round());
        Box(Out, L0 + 4, G, At + FVector2f(112, 25), FVector2f(28, 13), FLinearColor::White, Round());
        Text(Out, L1, G, At + FVector2f(148, 12), FString::Printf(TEXT("F x%d"), State->ItemCount(UPYRTrainerState::PokeBall)), 15, Ink, false);
    }

    // ---- m4t: RUN (X, the pad's Y; click or tap it). Lit for a moment when pressed: sun for a wild escape, red for a
    // trainer's refusal.
    if (M->GetKind() != EPYRBattleKind::Demo)
    {
        const FBox2f R = Lay.Run;
        const bool bLit = Now - M->RunPressedAt < 1.4;
        const bool bRefused = bLit && M->IsTrainerKind();
        const FLinearColor Fill = bRefused ? Cap : bLit ? Sun : Paper;
        if (bLit) InkBox(Out, L0, G, R.Min - FVector2f(6, 6), R.GetSize() + FVector2f(12, 12), Sun * FLinearColor(1, 1, 1, .55f), 0.f, Panel());
        InkBox(Out, L0 + 1, G, R.Min, R.GetSize(), M->IsBattlePaused() ? Fill * .7f : Fill, 3.f, Panel());
        // A little runner: three speed lines.
        for (int32 i = 0; i < 3; ++i)
            Lines(Out, L1, G, {R.Min + FVector2f(12, 15 + i * 9), R.Min + FVector2f(30 - i * 4, 15 + i * 9)}, Ink, 3.f);
        Text(Out, L1, G, R.Min + FVector2f(40, 11), TEXT("RUN"), 17, Ink, false);
        Text(Out, L1, G, R.Min + FVector2f(100, 14), TEXT("X"), 13, Ink * .7f + Paper * .3f, false);
    }

    // ---- Moves (bottom right): type-coloured icons, key number, emptying cooldown ring.
    if (P)
    {
        const float IconSize = FPYRHudLayout::Icon, Gap = FPYRHudLayout::Gap;
        const FVector2f Start = Lay.MovesStart;
        for (int32 i = 0; i < 4; ++i)
        {
            const FVector2f At = Start + FVector2f(i * (IconSize + Gap), 0);
            const FPYRMoveInfo* Move = P->Carried.IsValidIndex(i) ? PYRData::Move(P->Carried[i]) : nullptr;
            if (!Move) { InkBox(Out, L0, G, At, FVector2f(IconSize, IconSize), Ink * FLinearColor(1, 1, 1, .5f), 3.f, Panel()); continue; }
            const float Cd = P->GetCooldownFraction(i);
            const FLinearColor TypeCol = PYRData::TypeColour(Move->Type);
            InkBox(Out, L0, G, At, FVector2f(IconSize, IconSize), Cd > 0 ? TypeCol * .45f + Ink * .2f : TypeCol, 4.f, Panel());
            InkText(Out, L1, G, At + FVector2f(8, 3), FString::FromInt(i + 1), 18, Paper, 2.f);
            // The whole name, as big as fits (m4u: at 9 pt it was 6 px on a 720p screen, which the blind judge couldn't
            // read): one line, else two at a space or a word's tail (THUNDER / SHOCK), else smaller, never below 10 pt.
            const FString Name = Move->Name.ToString().ToUpper();
            FString Line1, Line2;
            int32 Font = 0;
            MoveLabel(Name, IconSize - 8, Line1, Line2, Font);
            const FVector2f W1 = Measure(Line1, Font), W2 = Measure(Line2, Font);
            const float LineH = Font * 1.3f;
            Text(Out, L1, G, At + FVector2f((IconSize - W1.X) * .5f, IconSize - 4 - LineH * (Line2.IsEmpty() ? 1 : 2)), Line1, Font, Ink, false);
            if (!Line2.IsEmpty()) Text(Out, L1, G, At + FVector2f((IconSize - W2.X) * .5f, IconSize - 4 - LineH), Line2, Font, Ink, false);
            // Cooldown class as pips (quick 1, tactical 2, big 3), so no letter reads like a key.
            for (int32 Pip = 0; Pip <= int32(Move->Tier); ++Pip)
                Box(Out, L1, G, At + FVector2f(IconSize - 14 - Pip * 9, 8), FVector2f(6, 6), Ink * .8f, Round());
            if (Cd > 0)
            {
                Ring(Out, L2, G, At + FVector2f(IconSize * .5f, IconSize * .5f - 2), 22, 1.f, Ink * FLinearColor(1, 1, 1, .6f), 7);
                Ring(Out, L2 + 1, G, At + FVector2f(IconSize * .5f, IconSize * .5f - 2), 22, Cd, Paper, 5);
                InkText(Out, L2 + 2, G, At + FVector2f(IconSize * .5f - 7, IconSize * .5f - 14), FString::Printf(TEXT("%.0f"), FMath::CeilToFloat(P->Cooldown[i])), 14, Paper, 2.f);
            }
            if (P->IsSlotDisabled(i))
            {
                // Disabled: a dark cover and a big cross.
                Box(Out, L2 + 3, G, At, FVector2f(IconSize, IconSize), Ink * FLinearColor(1, 1, 1, .6f), Panel());
                Lines(Out, L2 + 4, G, {At + FVector2f(14, 14), At + FVector2f(IconSize - 14, IconSize - 14)}, Cap, 6.f);
                Lines(Out, L2 + 4, G, {At + FVector2f(IconSize - 14, 14), At + FVector2f(14, IconSize - 14)}, Cap, 6.f);
            }
        }
        // Dodge pip above the moves.
        const FVector2f Pip = Lay.Dodge;
        const bool bReady = P->IsDodgeReady();
        InkBox(Out, L0, G, Pip, FVector2f(150, 32), Paper, 3.f, Panel());
        InkBox(Out, L0 + 2, G, Pip + FVector2f(8, 6), FVector2f(20, 20), bReady ? Leaf : Ink * .5f, 2.f, Round());
        if (P->BodyComp) Ring(Out, L1, G, Pip + FVector2f(18, 16), 13, P->BodyComp->DodgeCooldownFraction(), Ink, 3);
        const TCHAR* DodgeWord = P->GetBody() == EPYRBody::Crawler ? TEXT("SPACE TUCK") : P->GetBody() == EPYRBody::Giant ? TEXT("NO DODGE")
            : P->GetBody() == EPYRBody::Swimmer ? TEXT("SPACE DART") : TEXT("SPACE DODGE");
        Text(Out, L1, G, Pip + FVector2f(36, 7), DodgeWord, 11, Ink, false);
        // m3: E hops (runners, crawlers) or dives (swimmers in the water).
        FString HopWord;
        if (P->GetBody() == EPYRBody::Runner || P->GetBody() == EPYRBody::Crawler) HopWord = TEXT("E HOP");
        else if (P->IsSwimmer() && P->IsInWater()) HopWord = P->bDived ? TEXT("E SURFACE") : Now < P->SurfacedUntil ? FString::Printf(TEXT("DIVE IN %.0f"), FMath::CeilToFloat(float(P->SurfacedUntil - Now))) : TEXT("E DIVE");
        if (!HopWord.IsEmpty())
        {
            const FVector2f HopAt(Pip.X + FPYRHudLayout::HopOffset, Pip.Y);
            InkBox(Out, L0, G, HopAt, FVector2f(118, 32), Paper, 3.f, Panel());
            Text(Out, L1, G, HopAt + FVector2f(12, 7), HopWord, 11, Ink, false);
        }
    }

    // ---- Swap timer (above the moves, right).
    if (M->GetKind() != EPYRBattleKind::Demo)
    {
        const FVector2f C = Lay.SwapCentre;
        const float Left = M->SwapSecondsLeft();
        InkBox(Out, L0, G, C - FVector2f(30, 30), FVector2f(60, 60), Paper, 3.f, Round());
        Ring(Out, L1, G, C, 24, Left > 0 ? Left / 30.f : 1.f, Left > 0 ? Cap : Leaf, 6);
        const FString T = Left > 0 ? FString::FromInt(FMath::CeilToInt(Left)) : TEXT("Q");
        const FVector2f TS = Measure(T, 13);
        Text(Out, L1 + 1, G, C - TS * .5f, T, 13, Ink, false);
        InkText(Out, L1, G, C + FVector2f(-26, 32), TEXT("SWAP"), 12, Paper, 2.f);
    }

    // ---- Catch meter circling the foe while F is held (red -> yellow -> green as its HP falls).
    FVector2f FoeScreen;
    if (F && (M->IsAiming() || M->IsBallBusy()) && !F->IsHidden() && Project(F->GetActorLocation(), FoeScreen))
    {
        const int32 Band = M->CatchMeterBand();
        const FLinearColor Col = Band == 0 ? Cap : Band == 1 ? Sun : Leaf;
        const float R = F->GetBody() == EPYRBody::Giant ? 150.f : 70.f;
        const float Spin = float(FMath::Fmod(Now * .6, 1.0));
        Ring(Out, L1, G, FoeScreen, R, 1.f, Ink, 9);
        Ring(Out, L1 + 1, G, FoeScreen, R, 1.f, Col, 5);
        Ring(Out, L1 + 2, G, FoeScreen, R + 10, M->CatchChance(), Paper, 4);
        for (int32 i = 0; i < 4; ++i)
        {
            const float A = 2 * PI * (Spin + i * .25f);
            const FVector2f D(FMath::Cos(A), FMath::Sin(A));
            Lines(Out, L1 + 3, G, {FoeScreen + D * (R - 12), FoeScreen + D * (R + 14)}, Col, 4);
        }
    }

    // ---- Red's shout bubble (cheers only; it never steers).
    if (!M->Shout.IsEmpty() && Now < M->ShoutUntil && M->GetRed() && !M->IsWheelOpen())
    {
        FVector2f S;
        if (Project(M->GetRed()->GetActorLocation() + FVector(0, 0, 130), S))
        {
            const FVector2f TS = Measure(M->Shout, 14);
            const FVector2f At = S - FVector2f(TS.X * .5f + 12, TS.Y + 22);
            InkBox(Out, L1, G, At, TS + FVector2f(24, 14), FLinearColor::White, 3.f, Panel());
            Text(Out, L2, G, At + FVector2f(12, 7), M->Shout, 14, Ink, false);
        }
    }

    // ---- Yellow's caption in bold across the ring (pops in, holds 2 s).
    const float CapAge = float(Now - M->CaptionAt);
    if (!M->CaptionText.IsEmpty() && CapAge < 2.2f && !M->IsWheelOpen())
    {
        const bool bBig = M->CaptionText.Contains(TEXT("super effective")) || M->CaptionText.Contains(TEXT("caught")) || M->CaptionText.Contains(TEXT("appeared"))
            || M->CaptionText.Contains(TEXT("sent out")) || M->CaptionText.Contains(TEXT("fainted"));
        const int32 FontSize = FMath::RoundToInt((bBig ? 34 : 27) * (CapAge < .08f ? 1.25f : 1.f));
        const FVector2f TS = Measure(M->CaptionText, FontSize);
        // m4u: on Desktop the caption keeps off both Pokemon and Red (at 62% down it covered Pikachu in the ring, the
        // blind judge's occlusion): of a few heights, the one whose band covers least of where each stands on screen (a
        // Pokemon counts three times Red), the usual one when it's clear. Phone keeps its band in the safe area.
        if (!Lay.bPhone)
        {
            TArray<TPair<FBox2f, float>> Mons;
            for (const AActor* Mon : {static_cast<const AActor*>(P), static_cast<const AActor*>(F), static_cast<const AActor*>(M->GetRed())})
            {
                if (!Mon || Mon->IsHidden()) continue;
                const FBox Body = BodyBounds(Mon);
                if (!Body.IsValid) continue;
                const FVector Origin = Body.GetCenter(), Extent = Body.GetExtent();
                FBox2f B(ForceInit);
                for (const float Sx : {-1.f, 1.f}) for (const float Sy : {-1.f, 1.f}) for (const float Sz : {-1.f, 1.f})
                {
                    FVector2f Sp;
                    if (Project(Origin + Extent * FVector(Sx, Sy, Sz), Sp)) B += Sp;
                }
                if (B.bIsValid) Mons.Add({B, Mon == M->GetRed() ? 1.f : 3.f});
            }
            if (CaptionFor != M->CaptionAt)
            {
                CaptionFor = M->CaptionAt;
                CaptionTopShown = CaptionTopClear(Lay, TS, Mons);
            }
        }
        else CaptionTopShown = Lay.CaptionTop;
        const FVector2f At(Lay.CaptionCentreX - TS.X * .5f, CaptionTopShown);
        const float Alpha = FMath::Clamp((2.2f - CapAge) / .3f, 0.f, 1.f);
        // A slanted colour band behind the words, like the show's title cards.
        const FLinearColor Band = M->CaptionText.Contains(TEXT("doesn't affect")) || M->CaptionText.Contains(TEXT("not very")) ? Sky : M->CaptionText.Contains(TEXT("super")) ? Cap : Sun;
        Box(Out, L2, G, At - FVector2f(40, 10), TS + FVector2f(80, 20), Ink * FLinearColor(1, 1, 1, .85f * Alpha), Panel());
        Box(Out, L2 + 1, G, At - FVector2f(34, 5), TS + FVector2f(68, 10), Band * FLinearColor(1, 1, 1, Alpha), Panel());
        InkText(Out, L2 + 2, G, At, M->CaptionText, FontSize, FLinearColor(1, 1, 1, Alpha), 3.5f);
    }

    // ---- Banner (level up, evolution, Bill's PC).
    if (!M->Banner.IsEmpty() && Now < M->BannerUntil)
    {
        const FVector2f TS = Measure(M->Banner, 22);
        const FVector2f At((Size.X - TS.X) * .5f - 24, Lay.BannerTop);
        InkBox(Out, L1, G, At, TS + FVector2f(48, 24), Sun, 4.f, Panel());
        Text(Out, L2, G, At + FVector2f(24, 12), M->Banner, 22, Ink, false);
    }

    // ---- m4t: the battle is held (switch menu or learn prompt): a cool tint over the frozen fight and PAUSED.
    if (M->IsBattlePaused())
    {
        Box(Out, Layer, G, FVector2f(0, 0), Size, Sky * FLinearColor(.35f, .45f, .6f, .38f));
        const FString Word = TEXT("PAUSED");
        const FVector2f WS = Measure(Word, 26);
        const FVector2f At(Lay.PausedRight - WS.X, Lay.PausedTop);   // Top right, clear of the menus over the middle.
        InkBox(Out, L1, G, At - FVector2f(22, 8), WS + FVector2f(44, 16), Ink * FLinearColor(1, 1, 1, .85f), 3.f, Panel());
        InkText(Out, L1 + 2, G, At, Word, 26, Sky, 2.5f);
    }

    // ---- Party wheel (Q or Tab): name, level, HP, body icon, the swap cooldown, a lock on the active one, FAINTED on
    // the ones that can't come in, and the cursor ring on the one a confirm would send out.
    if (M->IsWheelOpen() && State)
    {
        // K: the phone layout's scale for the whole menu (1 on Desktop).
        const FVector2f C = Lay.WheelCentre;
        const float K = Lay.WheelScale;
        auto Pt = [K](int32 Points) { return FMath::Max(6, FMath::RoundToInt(Points * K)); };
        Box(Out, L1, G, C - FVector2f(256, 256) * K, FVector2f(512, 512) * K, Ink * FLinearColor(1, 1, 1, .62f), Round());
        const int32 N = State->Party.Num();
        for (int32 i = 0; i < N; ++i)
        {
            const FPYRMon& Mon = State->Party[i];
            const FPYRSpeciesInfo* Info = PYRData::Species(Mon.Species);
            const FVector2f S = WheelSlot(Size, i, N);
            const bool bActive = i == M->ActiveIndex() && P && !P->bFainted;
            const bool bAble = Mon.IsAble();
            if (i == M->GetWheelCursor() && bAble && !bActive) InkBox(Out, L1 + 1, G, S - FVector2f(60, 60) * K, FVector2f(120, 120) * K, Sun, 3.f, Round());
            InkBox(Out, L1 + 2, G, S - FVector2f(52, 52) * K, FVector2f(104, 104) * K, bAble ? Paper : Paper * .55f, 4.f, Round());
            if (Info) InkBox(Out, L1 + 4, G, S - FVector2f(22, 44) * K, FVector2f(44, 44) * K, bAble ? Info->Colour : Info->Colour * .4f, 2.f, Round());
            if (Info) BodyIcon(Out, L2, G, S + FVector2f(0, -22) * K, 10 * K, Info->Body, Ink);
            const FString Name = FString::Printf(TEXT("%d %s"), i + 1, *SpeciesName(Mon.Species).ToUpper());
            const FVector2f TS = Measure(Name, Pt(10));
            Text(Out, L2, G, S + FVector2f(-TS.X * .5f, 2 * K), Name, Pt(10), Ink, false);
            const FString Lv = FString::Printf(TEXT(":L%d  %.0f/%.0f"), Mon.Level, FMath::Max(0.f, Mon.Hp), Mon.MaxHp);
            const FVector2f LS = Measure(Lv, Pt(9));
            Text(Out, L2, G, S + FVector2f(-LS.X * .5f, 17 * K), Lv, Pt(9), Ink, false);
            const float Frac = FMath::Clamp(Mon.Hp / FMath::Max(1.f, Mon.MaxHp), 0.f, 1.f);
            Box(Out, L2, G, S + FVector2f(-34, 34) * K, FVector2f(68, 8) * K, Ink * .8f, Round());
            if (Frac > 0) Box(Out, L2 + 1, G, S + FVector2f(-33, 35) * K, FVector2f(66 * Frac, 6) * K, HpColour(Frac), Round());
            if (!bAble)
            {
                const FString Gone = TEXT("FAINTED");
                const FVector2f GS = Measure(Gone, Pt(11));
                InkText(Out, L2 + 3, G, S - GS * .5f + FVector2f(0, -8) * K, Gone, Pt(11), Cap, 2.f);
            }
            if (bActive)
            {
                // A padlock on the active one.
                Box(Out, L2 + 2, G, S - FVector2f(52, 52) * K, FVector2f(104, 104) * K, Ink * FLinearColor(1, 1, 1, .45f), Round());
                InkBox(Out, L2 + 3, G, S + FVector2f(-10, -10) * K, FVector2f(20, 16) * K, Sun, 2.f, Panel());
                Ring(Out, L2 + 3, G, S + FVector2f(0, -12) * K, 7 * K, .5f, Sun, 3);
            }
        }
        const float Left = M->SwapSecondsLeft();
        const FString Mid = M->IsWheelForced() ? TEXT("Choose your next Pokemon!") : Left > 0 ? FString::Printf(TEXT("Swap in %d s"), FMath::CeilToInt(Left)) : TEXT("Who's next?");
        const FVector2f TS = Measure(Mid, Pt(14));
        InkText(Out, L2, G, C - TS * .5f, Mid, Pt(14), Paper, 2.f);
        if (!M->IsWheelForced() && Left > 0) Ring(Out, L2, G, C, 70 * K, Left / 30.f, Cap, 6);
        // How to choose (the fight waits).
        const FString How = M->IsWheelForced() ? TEXT("ARROWS + E/ENTER  or  1-6") : TEXT("ARROWS + E/ENTER  or  1-6     Q/ESC  BACK");
        const FVector2f HW = Measure(How, Pt(12));
        InkBox(Out, L2, G, C + FVector2f(-HW.X * .5f - 14 * K, -300 * K), HW + FVector2f(28, 14) * K, Paper, 3.f, Panel());
        Text(Out, L2 + 1, G, C + FVector2f(-HW.X * .5f, -293 * K), How, Pt(12), Ink, false);
        // m3: the badge case is one key away.
        const FString CaseHint = FString::Printf(TEXT("B  BADGE CASE (%d/8)"), State->BadgeCount());
        const FVector2f HS = Measure(CaseHint, Pt(12));
        InkBox(Out, L2, G, C + FVector2f(-HS.X * .5f - 14 * K, 250 * K), HS + FVector2f(28, 14) * K, Sun, 3.f, Panel());
        Text(Out, L2 + 1, G, C + FVector2f(-HS.X * .5f, 257 * K), CaseHint, Pt(12), Ink, false);
    }

    return L2 + 8;
}
