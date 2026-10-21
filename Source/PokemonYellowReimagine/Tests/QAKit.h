// QA harness: helpers shared by the QA_* tests (QATests.cpp: the warp and link tour, the text dump, the talk tour
// and the visual tour; QAStory.cpp: the story playthrough).
//
// The tests don't judge Game Boy fidelity themselves. Every observation is logged as one line
//     QA_REC {"kind": "...", ...}
// and an external checker (not included) compares the records with pret/pokeyellow and YellowData and writes the bug list
// (the QA bug list). A QA test passes when its tour ran to the end; with -QAStrict it
// also fails on anything it could judge by itself (a warp that never fired, a soft-lock, a beat that never happened).
#pragma once

#include "CoreMinimal.h"
#include "Tests/M4TestKit.h"

class FJsonObject;
class FJsonValue;

namespace QA
{
// A record of this kind, to fill and then Log.
TSharedRef<FJsonObject> Rec(const TCHAR* Kind);
// Logs "QA_REC <compact json>".
void Log(const TSharedRef<FJsonObject>& Record);
TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& In);
TArray<TSharedPtr<FJsonValue>> Point(FIntPoint P);
bool Strict();
// Yellow's water on an outdoor map shows: each water tile has its surface drawn, and nothing flat lies over it.
bool WaterShows(FGameTestContext& Test, FName Map, FString& Why, int32* Count = nullptr);
// -QAShotDir=<dir>, else <ProjectSaved>/QAShots.
FString ShotDir();

// Nothing open, running, fighting or warping.
bool Idle(FGameTestContext& Test);
// One tick of a player getting through whatever is on screen: text goes on, YES/NO gets bYes, the naming screen takes
// the default name, and a fight on the stage is won through the battle test hooks. True while anything is busy.
bool Drive(FGameTestContext& Test, bool bYes = true);
// Wins the current fight (the foe knocked out each time it is out).
void WinFight(FGameTestContext& Test);

// ---- Walking like a player: a path over Kanto tiles (steps, ledge hops and warps), then the test stick along it.
struct FStep
{
    FIntPoint Tile = FIntPoint::ZeroValue;   // Kanto
    bool bWarp = false;                      // stepping onto this tile fires a warp (the next step is where it lands)
    bool bHop = false;                       // a ledge hop lands here
};

// Cheapest path From -> To (walking 1 per tile, a ledge hop 2, a warp 25). Avoid: tiles to keep off (people).
// AllowedMaps (optional): warps may only lead into these maps. To may be a warp tile (the path ends as it fires).
bool Plan(FGameTestContext& Test, FIntPoint From, FIntPoint To, TArray<FStep>& Out, const TSet<FIntPoint>& Avoid = TSet<FIntPoint>(),
    const TSet<FName>* AllowedMaps = nullptr);
// Who stands on a Kanto tile: a shown person's id or an item ball's key, else "".
FString PersonAt(FGameTestContext& Test, FIntPoint Kanto);
// Tiles people (shown ones) and item balls stand on now.
TSet<FIntPoint> PeopleTiles(FGameTestContext& Test);
// The Kanto tile of a map's local tile.
FIntPoint K(FGameTestContext& Test, FName Map, int32 X, int32 Y);
FString TileText(FGameTestContext& Test, FIntPoint Kanto);   // "MAP x,y"

struct FWalker
{
    enum class EState : uint8 { Walking, Arrived, Stuck, NoPath };
    FIntPoint Goal = FIntPoint::ZeroValue;
    TArray<FStep> Path;
    int32 At = 0;
    double LastProgress = 0;
    FIntPoint LastTile = FIntPoint(MIN_int32, MIN_int32);
    int32 Replans = 0, Warps = 0, StuckCount = 0;
    TSet<FName> Allowed;
    bool bUseAllowed = false;
    FString Why;
    // Plan from Red's tile; false (and Why) when no path exists.
    bool Start(FGameTestContext& Test, FIntPoint InGoal);
    // One tick: steers the test stick; waits (without counting as stuck) while Red is locked or warping.
    EState Tick(FGameTestContext& Test);
    bool Replan(FGameTestContext& Test, bool bAvoidPeople);
};

// ---- Past the maps: what the world draws on tiles no map covers, within Ring tiles of the outdoor maps.
// Bits per Kanto tile: 1 a tree (sphere, cylinder or cone), 2 ground (a plane), 4 anything else.
TMap<FIntPoint, uint8> DrawnOffMap(FGameTestContext& Test, int32 Ring);
// The kind of scenery a look code shows, in the checker's words (its LOOK_KIND).
const TCHAR* LookKind(TCHAR Look);

// A QA_<bugid> regression test on a visual tour shot ("map-OAKS_LAB", "beat-brock-battle"): the scene is staged
// exactly as for the picture, then Check runs where the picture would be taken (true: fixed; Why says what it saw).
EGameTest ShotCheck(FGameTestContext& Test, const FString& Shot, TFunction<bool(FGameTestContext&, FString&)> Check);

// Turns Red to face Dir on his tile (the E key reads what he faces).
void Face(FGameTestContext& Test, EYDir Dir);
// A tile next to Person (or across a counter) Red can stand on and the facing from there; false when none.
bool TalkSpot(FGameTestContext& Test, const class APYRPerson* Person, FIntPoint& OutKanto, EYDir& OutFace);
}
