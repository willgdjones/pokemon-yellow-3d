// m4u: the phone layout. On a phone, a remote-play page (the game was played over a browser stream during development) draws its own controls over the game: the
// top bar (Full screen, Touch, Sound, End and the stats box), the move stick bottom left, the button cluster bottom right
// (START, B / Back, A / Talk, Dodge, Sprint, Party, Throw) and the number row along the bottom (1-4, RUN). The game can't
// move those, so it keeps its own UI out from under them.
//
// Two layouts:
// - Desktop: the layout every build had before m4u.
// - Phone: the viewport is under 480 px high or wider than 1.9:1, or a page draws controls over the game (the game is
//   pixel-streamed, or touch is switched on). The battle HUD's two Pokemon cards take the top corners below the top bar;
//   the move icons, items, RUN, dodge and swap sit in the centre column between the stick and the button cluster, above
//   the number row; the menus (the party list, its STATS/SWITCH/CANCEL box, the learn prompt, the text box) are laid out
//   in that column, scaled down to fit rather than cut; the minimap moves to the top right.
//
// Where the controls are: the page's own CSS, mirrored below for the page size (844 x 390 by default, a phone held
// sideways) with every touch control at least 44 x 44 page px, then mapped onto the game's picture the way the page
// shows it (object-fit: contain, so a 16:9 stream sits between two bars on a phone). The safe area is the viewport less
// those rectangles and an 8 px margin.
//
// Touch is on when:
// - a remote-play page says so: a message {"type":"PYRTouch","touch":true,"width":844,"height":390} (the page's
//   touch class and size);
// - or the game runs with -TouchLayout, or the console variable pyr.TouchLayout is 1 (2 forces it off);
// - or the game is streamed (never, in this build) and Config/DefaultGame.ini [PYRPhoneLayout] TouchWhenStreamed is true (the default:
//   the stream is always 16:9, so without the page's message the game can't tell a phone from a desktop browser).
// Tests and shots switch it on with SetTouch, as the page's message would.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

enum class EPYRLayout : uint8 { Desktop, Phone };

// Which part of the page a control belongs to.
enum class EPYRTouchZone : uint8 { TopBar, Stick, Actions, Moves };

struct FPYRTouchControl
{
    FString Label;
    FKey Key;                 // the key the page sends (touch-layout.json), Invalid for the top bar
    EPYRTouchZone Zone = EPYRTouchZone::TopBar;
    FBox2f Page;              // page CSS px
    FBox2f View;              // viewport px
};

// The parts of the viewport no page control covers, in viewport px: a band across the screen from Top to SideBottom,
// and below it a column from ColumnLeft to ColumnRight down to ColumnBottom.
struct FPYRSafeArea
{
    FVector2f View = FVector2f(1280, 720);
    float Top = 0, SideBottom = 720, ColumnLeft = 0, ColumnRight = 1280, ColumnBottom = 720;
    FBox2f Band() const { return FBox2f(FVector2f(Margin, Top), FVector2f(View.X - Margin, SideBottom)); }
    FBox2f Column() const { return FBox2f(FVector2f(ColumnLeft, Top), FVector2f(ColumnRight, ColumnBottom)); }
    static constexpr float Margin = 8.f;
};

namespace PYRPhoneLayout
{
    // The phone thresholds.
    constexpr float PhoneMaxHeight = 480.f;
    constexpr float PhoneMinAspect = 1.9f;
    constexpr float MinControl = 44.f;

    POKEMONYELLOWREIMAGINE_API EPYRLayout Current();
    inline bool IsPhone() { return Current() == EPYRLayout::Phone; }
    POKEMONYELLOWREIMAGINE_API const TCHAR* Name(EPYRLayout Layout);
    // The game viewport in px (zero when there is none).
    POKEMONYELLOWREIMAGINE_API FVector2f ViewportSize();
    // True when the viewport alone is phone-shaped.
    POKEMONYELLOWREIMAGINE_API bool PhoneViewport(FVector2f View);
    POKEMONYELLOWREIMAGINE_API bool Streamed();
    // The page's touch controls are on screen.
    POKEMONYELLOWREIMAGINE_API bool TouchOn();
    // A page draws over the game (its top bar at least): streamed, or touch on.
    POKEMONYELLOWREIMAGINE_API bool PageOverlay();
    // The page the controls are laid out on, in CSS px.
    POKEMONYELLOWREIMAGINE_API FVector2f PageSize();
    // Every page control over the game now, in page and viewport px (empty with no page overlay).
    POKEMONYELLOWREIMAGINE_API TArray<FPYRTouchControl> Controls();
    // The same for a given page, viewport and touch state (tests).
    POKEMONYELLOWREIMAGINE_API TArray<FPYRTouchControl> ControlsFor(FVector2f Page, FVector2f View, bool bTouch);
    // The key the page's stick sends for a direction (up 0, down 1, left 2, right 3; touch-layout.json "stick").
    POKEMONYELLOWREIMAGINE_API FKey StickKey(int32 Direction);
    POKEMONYELLOWREIMAGINE_API FPYRSafeArea Safe();
    POKEMONYELLOWREIMAGINE_API FPYRSafeArea SafeFor(const TArray<FPYRTouchControl>& Controls, FVector2f View);

    // Touch as the stream page reports it. Page zero keeps the page size already known.
    POKEMONYELLOWREIMAGINE_API void SetTouch(bool bOn, FVector2f Page = FVector2f::ZeroVector);
    // Forget the page's report (tests).
    POKEMONYELLOWREIMAGINE_API void ResetTouch();
    // A UIInteraction from the stream page; true when it was a PYRTouch message.
    POKEMONYELLOWREIMAGINE_API bool HandleStreamMessage(const FString& Json);
    // Listen for the page's messages on every streamer (no-op without Pixel Streaming); called once per world.
    POKEMONYELLOWREIMAGINE_API void Start(UWorld* World);

    // The size the Desktop menus were laid out for (the 720p and 1080p screens' local size): on Phone the menus get a
    // frame this size, scaled down into the column (UPYRMenus).
    constexpr float RefWidth = 1920.f, RefHeight = 1080.f;

    // The in-game picture of the page's controls (tests and shots only, never on a real stream): drawn when touch is on
    // and the run is automated or has -TouchGuide.
    POKEMONYELLOWREIMAGINE_API bool GuideWanted();
    // Its widgets this frame, one per control (label -> widget), for the overlap tests.
    POKEMONYELLOWREIMAGINE_API TArray<TPair<FPYRTouchControl, TSharedPtr<class SWidget>>> GuideWidgets();
    POKEMONYELLOWREIMAGINE_API void TickGuide(UWorld* World);
}
