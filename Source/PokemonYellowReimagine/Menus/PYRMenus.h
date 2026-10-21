// M4: every Game Boy screen and box (START, party, stats, Pokedex, bag, trainer card, save, options, Mart, PC, naming,
// forget move, evolution, Town Map, Pikachu's face, the text box and YES/NO) is an FPYRScreen on one stack, drawn in
// Slate over the game in Game Boy style (white boxes with a black double border, a black cursor, Game Boy caps).
//
// Input: the player controller hands every key press to UPYRMenus::HandleKey first (APYRPlayerController::InputKey).
// In menus W/S/A/D or the arrows move, E, Space or Enter confirm, and Q, Backspace or Esc go back. The text box takes
// the same keys to advance. Tests press keys through UPYRMenus::Press or the controller.
//
// Menus release the mouse while one is open and give it back when the last closes; the text box keeps it captured.
// Red can't move while anything is open.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PYRMenus.generated.h"

class SWidget;
class SOverlay;
class UPYRMenus;
struct FKey;

enum class EPYRKey : uint8 { Up, Down, Left, Right, Confirm, Back, Start, None };

class POKEMONYELLOWREIMAGINE_API FPYRScreen : public TSharedFromThis<FPYRScreen>
{
public:
    virtual ~FPYRScreen() {}
    FName Id;
    int32 Cursor = 0;
    TWeakObjectPtr<UPYRMenus> Owner;
    // The widget for the current state (called on push and on Refresh).
    virtual TSharedRef<SWidget> Build() = 0;
    // True when the key was used.
    virtual bool Input(EPYRKey Key) { return false; }
    // A keyboard key before it is mapped (the naming screen types letters); true when the screen took it.
    virtual bool RawKey(const FKey& Key, bool bShift) { return false; }
    virtual void Tick(float DeltaSeconds) {}
    // Menus release the mouse; boxes over the world (text) don't.
    virtual bool ReleasesMouse() const { return true; }
    // Screens under a full-screen one aren't drawn.
    virtual bool IsFullScreen() const { return false; }
    // What the screen shows, as text (tests and logs).
    virtual FString Describe() const { return FString(); }
    // Called when it becomes the top again (a screen above closed).
    virtual void OnReveal() {}
    virtual void OnClosed() {}
    // m4u: the parts a player must be able to read and pick (names, levels, HP bars, choices), for the phone layout's
    // overlap tests.
    virtual void Protected(TArray<TPair<FString, TSharedPtr<SWidget>>>& Out) const {}
    // Rebuild the widget.
    void Refresh();
    UWorld* GetWorld() const;
    void Close();
    TSharedPtr<SWidget> Widget;
    bool bClosed = false;
};

// A list of choices with a cursor (the Game Boy's menu box): used for START, YES/NO, the Mart and PC menus.
class POKEMONYELLOWREIMAGINE_API FPYRListScreen : public FPYRScreen
{
public:
    TArray<FString> Items;
    TFunction<void(int32)> OnPick;      // Confirm on an item.
    TFunction<void()> OnBack;           // Back (default: close).
    // Where the box sits: fractions of the screen for its top-left, and a width in px (0: fit).
    FVector2D Anchor = FVector2D(.62f, .06f);
    float Width = 0.f;
    FString Title;
    bool bCloseOnPick = false;
    TArray<TSharedPtr<SWidget>> RowWidgets;   // each choice's row, as last built
    virtual TSharedRef<SWidget> Build() override;
    virtual bool Input(EPYRKey Key) override;
    virtual FString Describe() const override;
    virtual void Protected(TArray<TPair<FString, TSharedPtr<SWidget>>>& Out) const override;
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRMenus : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UPYRMenus* Get(const UObject* Ctx);

    void Push(const TSharedRef<FPYRScreen>& Screen);
    // Close the top screen (or a given one and everything above it).
    void Pop();
    void Remove(const TSharedPtr<FPYRScreen>& Screen);
    void CloseAll();
    TSharedPtr<FPYRScreen> Top() const { return Stack.Num() ? Stack.Last() : nullptr; }
    TSharedPtr<FPYRScreen> Find(FName Id) const;
    bool IsOpen(FName Id) const { return Find(Id).IsValid(); }
    bool AnyOpen() const { return Stack.Num() > 0; }
    // True while a menu that releases the mouse is open.
    bool AnyMenu() const;
    const TArray<TSharedPtr<FPYRScreen>>& ProbeStack() const { return Stack; }
    int32 Depth() const { return Stack.Num(); }

    // A key from the controller: true when a screen used it (or one is open, so nothing else should).
    bool HandleKey(const FKey& Key);
    // The same for tests (and the controller after mapping).
    bool Press(EPYRKey Key);
    static EPYRKey Map(const FKey& Key);
    // Typed keys as the keyboard sends them (tests): each character of Keys pressed on its own; Shift held for bShift.
    void Type(const FString& Keys, bool bShift = false);
    // Start during Oak's opening: the talking runs on to the next question (a name, YES/NO) at once.
    bool SkipTalk();

    // ---- The text box
    // Show a Yellow string: \n breaks a line, \f starts a new box; <PLAYER>/<RIVAL> are filled. OnDone runs after the
    // last page is confirmed. Several Say calls queue one after another.
    void Say(const FString& Text, TFunction<void()> OnDone = nullptr);
    // m4u: who is talking (the field sets it as Red talks to someone): the text box names him over the lines that are
    // his own Texts (look.json dialogue-box: "A speech box with the speaker's name"). <RIVAL> in Name is filled.
    void SetSpeaker(const FString& Name, const TArray<FString>& Texts);
    FString SpeakerFor(const FString& Text);
    // The name on the open text box's speaker tab, empty when it has none (tests).
    FString ShownSpeaker() const;
    // The text, then a YES/NO box (YES first); OnAnswer(true) for YES. Back answers NO.
    void Ask(const FString& Text, TFunction<void(bool)> OnAnswer);
    // The text, then a list of choices; OnPick(-1) on Back.
    void Choose(const FString& Text, const TArray<FString>& Options, TFunction<void(int32)> OnPick);
    bool IsTalking() const;
    // Every page's text shown by the text box so far (tests).
    TArray<FString> TextLog;
    // The talker SetSpeaker named and his lines.
    FString SpeakerName;
    TArray<FString> SpeakerTexts;
    double SpeakerAt = 0;
    bool bSpeakerOpening = false;
    // Everything visible, top to bottom (tests).
    FString ScreenText() const;

    // Game Boy sound effects (YellowMusic sfx_*; UI blips otherwise).
    void Blip(const TCHAR* Sfx = TEXT("press_ab"));

    virtual void Tick(float DeltaSeconds) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPYRMenus, STATGROUP_Tickables); }
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual void Deinitialize() override;

    // Presses taken since start (tests).
    int32 Presses = 0;

    // m4u: every protected part of the open screens (top last), for the phone layout's overlap tests.
    TArray<TPair<FString, TSharedPtr<SWidget>>> ProtectedWidgets() const;

private:
    TArray<TSharedPtr<FPYRScreen>> Stack;
    TSharedPtr<SOverlay> Root;
    // m4u: what holds Root on the viewport: the whole screen on Desktop; on Phone the centre column, with a Desktop-sized
    // (RefWidth x RefHeight) frame scaled down into it, and a paper backdrop behind full-screen pages.
    TSharedPtr<class SConstraintCanvas> Frame;
    FString FrameKey;
    bool bFullOpen = false;
    void LayoutFrame();
    bool bAdded = false;
    bool bMouseReleased = false;
    bool bShiftDown = false;
    void Rebuild();
    void EnsureRoot();
    void UpdateMouse();
};

// Game Boy styling shared by every screen.
namespace PYRGb
{
    // A white box with the Game Boy's black double border.
    POKEMONYELLOWREIMAGINE_API TSharedRef<SWidget> Box(const TSharedRef<SWidget>& Content, float Padding = 14.f);
    POKEMONYELLOWREIMAGINE_API TSharedRef<SWidget> Text(const FString& Text, int32 Size = 26, bool bBold = true);
    // A column of rows with the cursor (a black triangle) on row Cursor (-1: none).
    POKEMONYELLOWREIMAGINE_API TSharedRef<SWidget> List(const TArray<FString>& Rows, int32 Cursor, int32 Size = 26, TArray<TSharedPtr<SWidget>>* OutRows = nullptr);
    // Placement: TopLeft as fractions of the viewport, fixed width (0: fit).
    POKEMONYELLOWREIMAGINE_API TSharedRef<SWidget> Place(const TSharedRef<SWidget>& Content, FVector2D TopLeft, float Width = 0.f, float Height = 0.f);
    // Placement by an anchor on the viewport and the widget's own alignment point, plus an offset in px.
    POKEMONYELLOWREIMAGINE_API TSharedRef<SWidget> PlaceAligned(const TSharedRef<SWidget>& Content, FVector2D Anchor, FVector2D Alignment, FVector2D Offset = FVector2D::ZeroVector);
    // A full-screen white page.
    POKEMONYELLOWREIMAGINE_API TSharedRef<SWidget> Page(const TSharedRef<SWidget>& Content);
    // An HP bar (Game Boy: green, yellow under half, red under a fifth).
    POKEMONYELLOWREIMAGINE_API TSharedRef<SWidget> HpBar(float Hp, float MaxHp, float Width = 160.f);
    // Money as the Game Boy writes it (¥ became $ in the English games; Yellow shows ¥).
    POKEMONYELLOWREIMAGINE_API FString Money(int32 Amount);
    // A Yellow string made ready for the screen (<PLAYER>, <RIVAL>, POKé).
    POKEMONYELLOWREIMAGINE_API FString Fill(const UObject* Ctx, const FString& Text);
    extern const FLinearColor Ink;
    extern const FLinearColor Paper;
}
