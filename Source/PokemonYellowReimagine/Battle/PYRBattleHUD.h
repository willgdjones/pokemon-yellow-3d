// The battle HUD, painted in Slate in the anime style (flat bright colours, thick ink outlines): Yellow's
// captions in bold across the ring, both Pokemon's names, levels and draining HP bars, the four move icons with
// emptying cooldown rings, the dodge pip, the swap timer, Potion and ball counts, the catch meter circling the
// foe while aiming, Red's shout bubble, the level-up banner, the radial party wheel and the fifth-move prompt.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class APYRBattleManager;

// m4u: where each part of the HUD sits, in the HUD's local space of Size. Desktop is the layout every build had before;
// Phone (Hud/PYRPhoneLayout) keeps everything clear of the stream page's controls: the two cards in the top corners
// under the top bar, the moves with items, RUN, dodge and swap in the centre column above the number row, the caption and
// the switch menu in the safe area.
struct FPYRHudLayout
{
    bool bPhone = false;
    FVector2f Size = FVector2f(1920, 1080);
    FVector2f FoeCard, PlayerCard;    // top-left of each CardSize box
    FVector2f Items;                  // ItemsSize
    FBox2f Run;
    FVector2f MovesStart;             // the first of the four Icon boxes, Gap apart
    FVector2f Dodge;                  // DodgeSize, the hop box HopOffset to its right
    FVector2f SwapCentre;
    float CaptionCentreX = 960, CaptionTop = 670;
    float BannerTop = 26;
    float PausedRight = 1868, PausedTop = 30;   // the PAUSED word's right edge and top
    FVector2f WheelCentre = FVector2f(960, 540);
    float WheelScale = 1.f;

    static constexpr float Icon = 78.f, Gap = 12.f, HopOffset = 160.f, SwapRadius = 30.f, WheelRadius = 172.f, WheelSlotRadius = 52.f;
    static FVector2f CardSize() { return FVector2f(330, 76); }
    static FVector2f ItemsSize() { return FVector2f(210, 48); }
    static FVector2f RunSize() { return FVector2f(126, 48); }
    static FVector2f DodgeSize() { return FVector2f(150, 32); }
    static FVector2f HopSize() { return FVector2f(118, 32); }
    FBox2f MoveBox(int32 I) const { const FVector2f At = MovesStart + FVector2f(I * (Icon + Gap), 0); return FBox2f(At, At + FVector2f(Icon, Icon)); }
    FBox2f Card(bool bFoe) const { const FVector2f At = bFoe ? FoeCard : PlayerCard; return FBox2f(At, At + CardSize()); }
    // The layout for a HUD of Size now (the phone layout reads the viewport and the page's controls).
    static POKEMONYELLOWREIMAGINE_API FPYRHudLayout Make(FVector2f Size);
};

class POKEMONYELLOWREIMAGINE_API SPYRBattleHUD : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SPYRBattleHUD) {}
        SLATE_ARGUMENT(APYRBattleManager*, Manager)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
        int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1280, 720); }
    // m4t: a click or a tap on RUN, or on a slot of the open switch menu (the rest falls through to the game).
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnTouchStarted(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual bool SupportsKeyboardFocus() const override { return false; }
    // Where RUN sits and where slot Index of Count sits on the switch menu, in the HUD's local space of Size.
    static FBox2f RunRect(FVector2f Size);
    static FVector2f WheelSlot(FVector2f Size, int32 Index, int32 Count);
    // m4u: what must stay readable, as named rectangles in absolute (screen) space from the widget's cached geometry:
    // both cards (name, level, HP bar), the move icons with their labels, items, RUN, the dodge box, the swap timer, the
    // caption when shown, and the switch menu's slots while it is open.
    TArray<TPair<FString, FBox2f>> ProtectedRects() const;
    int32 Clicks = 0;

private:
    FReply Press(const FGeometry& Geometry, FVector2f ScreenPos);
    TWeakObjectPtr<APYRBattleManager> Manager;
    // HP bars drain toward the real value.
    mutable float ShownHp[2] = {-1, -1};
    mutable TWeakObjectPtr<const AActor> ShownFor[2];
    mutable double LastPaint = 0;
    // m4u: the caption's top as painted (Desktop picks it clear of both Pokemon, once per caption).
    mutable float CaptionTopShown = -1.f;
    mutable double CaptionFor = -1.0;
};

// Shared painting helpers for the battle HUD and the Pikachu face panel.
namespace PYRDraw
{
    POKEMONYELLOWREIMAGINE_API const FSlateBrush* White();
    POKEMONYELLOWREIMAGINE_API const FSlateBrush* Round();   // Fully rounded (circles and pills).
    POKEMONYELLOWREIMAGINE_API const FSlateBrush* Panel();   // Softly rounded panel.
    POKEMONYELLOWREIMAGINE_API void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f Pos, FVector2f Size, FLinearColor Colour, const FSlateBrush* Brush = nullptr);
    // A box with a dark ink outline of Ink pixels.
    POKEMONYELLOWREIMAGINE_API void InkBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f Pos, FVector2f Size, FLinearColor Colour, float Ink = 3.f, const FSlateBrush* Brush = nullptr);
    POKEMONYELLOWREIMAGINE_API void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f Pos, const FString& Text, int32 Size, FLinearColor Colour, bool bShadow = true);
    // Bold text with a thick ink outline (the anime caption look).
    POKEMONYELLOWREIMAGINE_API void InkText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f Pos, const FString& Text, int32 Size, FLinearColor Colour, float Ink = 3.f);
    POKEMONYELLOWREIMAGINE_API FVector2f Measure(const FString& Text, int32 Size);
    // m4u: a move's name laid out in its icon Width across: one or two lines (Line2 empty for one) at Font pt.
    POKEMONYELLOWREIMAGINE_API void MoveLabel(const FString& Name, float Width, FString& Line1, FString& Line2, int32& Font);
    // m4u: where a fighter's body is: a Pokemon's capsule and its battle height, Red's capsule.
    POKEMONYELLOWREIMAGINE_API FBox BodyBounds(const AActor* Actor);
    // m4u: the Desktop caption's top: of a few heights, the one whose band (CaptionBand) covers least of Avoid (screen
    // boxes in the HUD's local space, each with its weight), the layout's own when it is clear.
    POKEMONYELLOWREIMAGINE_API float CaptionTopClear(const FPYRHudLayout& L, FVector2f TextSize, const TArray<TPair<FBox2f, float>>& Avoid);
    POKEMONYELLOWREIMAGINE_API FBox2f CaptionBand(const FPYRHudLayout& L, FVector2f TextSize, float Top);
    // Arc from the top, clockwise, covering Fraction of a circle.
    POKEMONYELLOWREIMAGINE_API void Ring(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2f Centre, float Radius, float Fraction, FLinearColor Colour, float Thickness);
    POKEMONYELLOWREIMAGINE_API void Lines(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2f>& Points, FLinearColor Colour, float Thickness);
    extern POKEMONYELLOWREIMAGINE_API const FLinearColor Ink, Paper, Sun, Cap, Sky, Leaf;
}
