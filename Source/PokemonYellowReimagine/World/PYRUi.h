// Road UI in Slate: speech bubbles over speakers, the key prompt at the bottom and cream panels (bond, Mart).
// Everything is added to the game viewport once; text is kept here too so tests can read what is on screen.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PYRUi.generated.h"

class SConstraintCanvas;
class SWidget;

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRUi : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UPYRUi* Get(const UObject* Ctx);

    // ---- m2 seams: the anime speech box (speaker tab over a white box with a thick ink border, at the bottom of the
    // screen) and the bold anime caption across the upper screen. Seconds <= 0 keeps it until replaced or cleared.
    void ShowDialogue(FText Speaker, FText Line, float Seconds);
    void ShowCaption(FText Text, float Seconds);
    void ClearDialogue() { DialogueUntil = -1; }
    void ClearCaption() { CaptionUntil = -1; }
    FText CurrentCaption() const;
    FText CurrentDialogue() const;
    FText CurrentSpeaker() const;

    // A bubble over Speaker (HeightCm above its origin) for Seconds; replaces the speaker's last bubble.
    void Bubble(AActor* Speaker, const FString& Text, float Seconds, float HeightCm = 200.f);
    void ClearBubble(const AActor* Speaker);
    FString BubbleText(const AActor* Speaker) const;
    // The key hint at the bottom of the screen ("F  Talk"); empty hides it.
    void SetPrompt(const FString& Text) { Prompt = Text; }
    const FString& GetPrompt() const { return Prompt; }
    // A cream panel on the right of the screen; Body is read every frame.
    void ShowPanel(FName Id, TFunction<FString()> Body);
    void HidePanel(FName Id);
    bool IsPanelOpen(FName Id) const;
    FString PanelText(FName Id) const;

    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPYRUi, STATGROUP_Tickables); }
    virtual void Deinitialize() override;

private:
    struct FBubble { TWeakObjectPtr<AActor> Speaker; FString Text; double Until = 0; float Height = 200; TSharedPtr<SWidget> Widget; };
    struct FPanel { FName Id; TFunction<FString()> Body; TSharedPtr<SWidget> Widget; };
    TSharedPtr<SConstraintCanvas> Canvas;
    TArray<TSharedPtr<FBubble>> Bubbles;
    TArray<TSharedPtr<FPanel>> Panels;
    FString Prompt;
    bool bPromptAdded = false;
    FText DialogueSpeaker, DialogueLine, Caption;
    double DialogueUntil = -1, CaptionUntil = -1;
    bool bBoxesAdded = false;
    void AddBoxes();
    bool EnsureCanvas();
    void MakeBubbleWidget(const TSharedPtr<FBubble>& B);
    void MakePanelWidget(const TSharedPtr<FPanel>& P);
    double Now() const;
};
