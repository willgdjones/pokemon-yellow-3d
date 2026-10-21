#include "World/PYRUi.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "World/PYRPaint.h"

namespace
{
// Anime cards: white with a thick ink border and ink text.
TSharedRef<SWidget> Card(TAttribute<FText> Text, int32 Size, TAttribute<EVisibility> Visible)
{
    const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    return SNew(SBorder)
        .Visibility(Visible)
        .BorderImage(White)
        .BorderBackgroundColor(PYRColor::Ink)
        .Padding(FMargin(4))
        [
            SNew(SBorder)
            .BorderImage(White)
            .BorderBackgroundColor(PYRColor::White)
            .Padding(FMargin(16, 9))
            [
                SNew(STextBlock)
                .Text(Text)
                .ColorAndOpacity(FSlateColor(PYRColor::Ink))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size))
            ]
        ];
}

FSlateFontInfo Outlined(int32 Size, int32 Outline)
{
    FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
    Font.OutlineSettings.OutlineSize = Outline;
    Font.OutlineSettings.OutlineColor = PYRColor::Ink;
    return Font;
}
}

void UPYRUi::ShowDialogue(FText Speaker, FText Line, float Seconds)
{
    DialogueSpeaker = Speaker;
    DialogueLine = Line;
    DialogueUntil = Seconds > 0.f ? Now() + Seconds : TNumericLimits<double>::Max();
    AddBoxes();
}

void UPYRUi::ShowCaption(FText Text, float Seconds)
{
    Caption = Text;
    CaptionUntil = Seconds > 0.f ? Now() + Seconds : TNumericLimits<double>::Max();
    AddBoxes();
}

FText UPYRUi::CurrentCaption() const { return Now() < CaptionUntil ? Caption : FText::GetEmpty(); }
FText UPYRUi::CurrentDialogue() const { return Now() < DialogueUntil ? DialogueLine : FText::GetEmpty(); }
FText UPYRUi::CurrentSpeaker() const { return Now() < DialogueUntil ? DialogueSpeaker : FText::GetEmpty(); }

void UPYRUi::AddBoxes()
{
    if (bBoxesAdded || !EnsureCanvas()) return;
    bBoxesAdded = true;
    TWeakObjectPtr<UPYRUi> Self = this;
    const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    // The speech box: a yellow name tab on a white box with a thick ink border, across the bottom third.
    Canvas->AddSlot()
        .Anchors(FAnchors(.18f, 1, .82f, 1))
        .Alignment(FVector2D(0, 1))
        .AutoSize(true)
        .Offset(FMargin(0, -40, 0, 0))
        [
            SNew(SVerticalBox)
            .Visibility(TAttribute<EVisibility>::CreateLambda([Self]() { return Self.IsValid() && !Self->CurrentDialogue().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(28, 0, 0, -4))
            [
                SNew(SBorder).BorderImage(White).BorderBackgroundColor(PYRColor::Ink).Padding(FMargin(4, 4, 4, 0))
                .Visibility(TAttribute<EVisibility>::CreateLambda([Self]() { return Self.IsValid() && !Self->CurrentSpeaker().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
                [
                    SNew(SBorder).BorderImage(White).BorderBackgroundColor(PYRColor::Yellow).Padding(FMargin(18, 5))
                    [
                        SNew(STextBlock).Text(TAttribute<FText>::CreateLambda([Self]() { return Self.IsValid() ? Self->CurrentSpeaker() : FText::GetEmpty(); }))
                        .ColorAndOpacity(FSlateColor(PYRColor::Ink)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 20))
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(White).BorderBackgroundColor(PYRColor::Ink).Padding(FMargin(5))
                [
                    SNew(SBorder).BorderImage(White).BorderBackgroundColor(PYRColor::White).Padding(FMargin(28, 18))
                    [
                        SNew(STextBlock).Text(TAttribute<FText>::CreateLambda([Self]() { return Self.IsValid() ? Self->CurrentDialogue() : FText::GetEmpty(); }))
                        .AutoWrapText(true).MinDesiredWidth(640.f)
                        .ColorAndOpacity(FSlateColor(PYRColor::Ink)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 24))
                    ]
                ]
            ]
        ];
    // The caption: big bold yellow letters with a thick ink outline, across the upper screen.
    Canvas->AddSlot()
        .Anchors(FAnchors(.5f, .2f))
        .Alignment(FVector2D(.5f, .5f))
        .AutoSize(true)
        [
            SNew(STextBlock)
            .Visibility(TAttribute<EVisibility>::CreateLambda([Self]() { return Self.IsValid() && !Self->CurrentCaption().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
            .Text(TAttribute<FText>::CreateLambda([Self]() { return Self.IsValid() ? Self->CurrentCaption() : FText::GetEmpty(); }))
            .Justification(ETextJustify::Center)
            .ColorAndOpacity(FSlateColor(PYRColor::Yellow))
            .ShadowOffset(FVector2D(4, 4))
            .ShadowColorAndOpacity(PYRColor::Red)
            .Font(Outlined(46, 4))
        ];
}

UPYRUi* UPYRUi::Get(const UObject* Ctx)
{
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    return World ? World->GetSubsystem<UPYRUi>() : nullptr;
}

double UPYRUi::Now() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0; }

bool UPYRUi::EnsureCanvas()
{
    if (Canvas.IsValid()) return true;
    UWorld* World = GetWorld();
    if (!World || !World->IsGameWorld() || !GEngine || !GEngine->GameViewport || GEngine->GameViewport->GetWorld() != World) return false;
    Canvas = SNew(SConstraintCanvas);
    GEngine->GameViewport->AddViewportWidgetContent(Canvas.ToSharedRef(), 20);
    return true;
}

void UPYRUi::Deinitialize()
{
    if (Canvas.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Canvas.ToSharedRef());
    Canvas.Reset();
    Super::Deinitialize();
}

void UPYRUi::Bubble(AActor* Speaker, const FString& Text, float Seconds, float HeightCm)
{
    if (!Speaker) return;
    ClearBubble(Speaker);
    TSharedPtr<FBubble> B = MakeShared<FBubble>();
    B->Speaker = Speaker;
    B->Text = Text;
    B->Until = Now() + Seconds;
    B->Height = HeightCm;
    Bubbles.Add(B);
    MakeBubbleWidget(B);
}

void UPYRUi::MakeBubbleWidget(const TSharedPtr<FBubble>& B)
{
    if (B->Widget.IsValid() || !EnsureCanvas()) return;
    TWeakPtr<FBubble> Weak = B;
    TWeakObjectPtr<UPYRUi> Self = this;
    B->Widget = Card(FText::FromString(B->Text), 15, TAttribute<EVisibility>::CreateLambda([Weak]()
    {
        TSharedPtr<FBubble> P = Weak.Pin();
        return P && P->Speaker.IsValid() && !P->Speaker->IsHidden() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
    }));
    Canvas->AddSlot()
        .Anchors(FAnchors(0, 0))
        .Alignment(FVector2D(.5, 1))
        .AutoSize(true)
        .Offset(TAttribute<FMargin>::CreateLambda([Weak, Self]()
        {
            TSharedPtr<FBubble> P = Weak.Pin();
            UWorld* World = Self.IsValid() ? Self->GetWorld() : nullptr;
            APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
            FVector2D Screen(-2000, -2000);
            if (P && P->Speaker.IsValid() && PC)
            {
                const FVector At = P->Speaker->GetActorLocation() + FVector(0, 0, P->Height);
                if (PC->ProjectWorldLocationToScreen(At, Screen, true))
                    Screen /= FMath::Max(.1f, UWidgetLayoutLibrary::GetViewportScale(World));
                else Screen = FVector2D(-2000, -2000);
            }
            return FMargin(Screen.X, Screen.Y, 0, 0);
        }))
        [B->Widget.ToSharedRef()];
}

void UPYRUi::ClearBubble(const AActor* Speaker)
{
    for (int32 i = Bubbles.Num() - 1; i >= 0; --i)
    {
        if (Bubbles[i]->Speaker.Get() != Speaker) continue;
        if (Canvas.IsValid() && Bubbles[i]->Widget.IsValid()) Canvas->RemoveSlot(Bubbles[i]->Widget.ToSharedRef());
        Bubbles.RemoveAt(i);
    }
}

FString UPYRUi::BubbleText(const AActor* Speaker) const
{
    for (const TSharedPtr<FBubble>& B : Bubbles)
        if (B->Speaker.Get() == Speaker) return B->Text;
    return FString();
}

void UPYRUi::ShowPanel(FName Id, TFunction<FString()> Body)
{
    HidePanel(Id);
    TSharedPtr<FPanel> P = MakeShared<FPanel>();
    P->Id = Id;
    P->Body = MoveTemp(Body);
    Panels.Add(P);
    MakePanelWidget(P);
}

void UPYRUi::MakePanelWidget(const TSharedPtr<FPanel>& P)
{
    if (P->Widget.IsValid() || !EnsureCanvas()) return;
    TWeakPtr<FPanel> Weak = P;
    P->Widget = Card(TAttribute<FText>::CreateLambda([Weak]()
    {
        TSharedPtr<FPanel> Pin = Weak.Pin();
        return FText::FromString(Pin && Pin->Body ? Pin->Body() : FString());
    }), 16, EVisibility::HitTestInvisible);
    Canvas->AddSlot()
        .Anchors(FAnchors(1, 0))
        .Alignment(FVector2D(1, 0))
        .AutoSize(true)
        .Offset(FMargin(-40, 40, 0, 0))
        [P->Widget.ToSharedRef()];
}

void UPYRUi::HidePanel(FName Id)
{
    for (int32 i = Panels.Num() - 1; i >= 0; --i)
    {
        if (Panels[i]->Id != Id) continue;
        if (Canvas.IsValid() && Panels[i]->Widget.IsValid()) Canvas->RemoveSlot(Panels[i]->Widget.ToSharedRef());
        Panels.RemoveAt(i);
    }
}

bool UPYRUi::IsPanelOpen(FName Id) const
{
    return Panels.ContainsByPredicate([Id](const TSharedPtr<FPanel>& P) { return P->Id == Id; });
}

FString UPYRUi::PanelText(FName Id) const
{
    for (const TSharedPtr<FPanel>& P : Panels)
        if (P->Id == Id && P->Body) return P->Body();
    return FString();
}

void UPYRUi::Tick(float DeltaTime)
{
    const double T = Now();
    for (int32 i = Bubbles.Num() - 1; i >= 0; --i)
    {
        if (Bubbles[i]->Speaker.IsValid() && T < Bubbles[i]->Until) continue;
        if (Canvas.IsValid() && Bubbles[i]->Widget.IsValid()) Canvas->RemoveSlot(Bubbles[i]->Widget.ToSharedRef());
        Bubbles.RemoveAt(i);
    }
    for (const TSharedPtr<FBubble>& B : Bubbles) MakeBubbleWidget(B);
    for (const TSharedPtr<FPanel>& P : Panels) MakePanelWidget(P);
    if ((DialogueUntil > 0 || CaptionUntil > 0) && !bBoxesAdded) AddBoxes();
    if (!bPromptAdded && EnsureCanvas())
    {
        bPromptAdded = true;
        TWeakObjectPtr<UPYRUi> Self = this;
        Canvas->AddSlot()
            .Anchors(FAnchors(.5f, 1))
            .Alignment(FVector2D(.5, 1))
            .AutoSize(true)
            .Offset(FMargin(0, -36, 0, 0))
            [Card(TAttribute<FText>::CreateLambda([Self]() { return FText::FromString(Self.IsValid() ? Self->Prompt : FString()); }), 15,
                TAttribute<EVisibility>::CreateLambda([Self]()
                {
                    return Self.IsValid() && !Self->Prompt.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                }))];
    }
}
