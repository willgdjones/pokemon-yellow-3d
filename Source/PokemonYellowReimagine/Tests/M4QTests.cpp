// m4q: the milestone's own checks, each self-judging (they fail on what they find wrong):
// - Roaming_*: every warp and map edge against Yellow's data, the scenery past every edge, a real walk from Pallet
//   Town to Cerulean in the one level, doors straight into rooms;
// - Trainers_TeamsMatchYellow: every trainer's team, levels and prize against trainers.json (data/trainers/parties.asm)
//   and Yellow's own named teams;
// - Text_EveryLineMatchesYellow: everyone and every sign talked to; every line shown is one of Yellow's lines.
#include "Tests/QAKit.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Menus/PYRScreens.h"
#include "Misc/PackageName.h"
#include "Party/PYRSpecies.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "UnrealClient.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRSysText.h"
#include "Partner/PYRPikachuFollower.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Stage/PYREncounters.h"

using namespace M4;

namespace
{
bool Standable(const FYMap* M, int32 X, int32 Y)
{
    return M->Contains(X, Y) && !YTile::IsSolid(M->Tile(X, Y)) && M->WarpAt(X, Y) == INDEX_NONE;
}

// Everyone off the road (a guard on a door or a walker on an edge would block the walk; this is about the map).
void HidePeople(FGameTestContext& Test)
{
    for (const TWeakObjectPtr<APYRPerson>& P : Field(Test)->People) if (P.IsValid()) P->SetShown(false);
}

struct FWarpCheck { FName Map; int32 Warp = 0; FIntPoint From = FIntPoint::ZeroValue; EYDir Dir = EYDir::None; };
}

// Every warp from Pallet Town to Cerulean, walked into from a tile beside it: Red lands on the warp its data names
// (pokeyellow's warp tables, verify.py-checked), one tile down off a door tile (door_tile_ids.asm) facing down;
// a warp out of the range stops him with a line and leaves him on his map.
GAME_TEST(Roaming_EveryWarpLandsWhereYellowSays, 1200)
{
    static FState St;
    static TArray<FWarpCheck> Cases;
    static int32 Landed = 0, Blocked = 0, Unreachable = 0, BlockedBefore = 0, WarpsBefore = 0;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRField* F = Field(Test);
    AYellowWorld* W = World(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    Quiet(true, true);
    UPYRScripts::Get(Test.World)->bSight = false;
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 30}});
        AfterPokedex(Test);
        State(Test)->AwardBadge(EPYRBadge::Boulder);
        HidePeople(Test);
        for (const FName& Id : D->MapOrder)
        {
            const FYMap* M = D->Map(Id);
            for (int32 I = 0; M && I < M->Warps.Num(); ++I)
            {
                FWarpCheck C{Id, I};
                const TArray<EYDir> Order = M->bOutdoor ? TArray<EYDir>{EYDir::Up, EYDir::Left, EYDir::Right, EYDir::Down} : TArray<EYDir>{EYDir::Down, EYDir::Up, EYDir::Left, EYDir::Right};
                for (const EYDir Dir : Order)
                {
                    const FIntPoint From = FIntPoint(M->Warps[I].X, M->Warps[I].Y) - YDir::Delta(Dir);
                    if (Standable(M, From.X, From.Y) && !W->IsEdgeBlocked(M->Origin + From, Dir)) { C.From = From; C.Dir = Dir; break; }
                }
                Cases.Add(C);
            }
        }
        M4_NEXT();
    }
    if (St.I >= Cases.Num())
        M4_PASS(TEXT("%d warps: %d landed where Yellow says, %d out of the range stopped with a line, %d with no open tile beside them (Cerulean Cave's needs Surf)"),
            Cases.Num(), Landed, Blocked, Unreachable);
    const FWarpCheck& C = Cases[St.I];
    const FYMap* M = D->Map(C.Map);
    const FYWarp& Wp = M->Warps[C.Warp];
    if (St.Step == 1)
    {
        Menus(Test)->CloseAll();
        if (C.Dir == EYDir::None) { ++Unreachable; ++St.I; return EGameTest::Running; }
        // Maps refresh their people on arrival: nobody stands on the tile Red walks in from (Cerulean's GUARD2).
        HidePeople(Test);
        // Pikachu, left on this tile by the last case, would push Red aside as he is placed: out of the way first.
        if (APYRPikachuFollower* Pika = APYRPikachuFollower::Find(Test.World)) Pika->SetActorLocation(Pika->GetActorLocation() + FVector(0, 0, -5000));
        // Arriving on a map shows its people again (Cerulean's GUARD2 on 27,12): hide them and place Red once more.
        Place(Test, C.Map, C.From.X, C.From.Y, C.Dir);
        HidePeople(Test);
        Place(Test, C.Map, C.From.X, C.From.Y, C.Dir);
        BlockedBefore = F->WarpsBlocked;
        WarpsBefore = F->WarpsTaken;
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.2);
        const bool bFired = F->WarpsTaken != WarpsBefore || F->IsWarping();
        const bool bStopped = F->WarpsBlocked != BlockedBefore;
        if (!bFired && !bStopped)
        {
            if (Test.Seconds - St.T0 > 3.5)
            {
                // What stands on the warp tile (diagnosis).
                TArray<FOverlapResult> Over;
                Test.World->OverlapMultiByChannel(Over, W->TileCenter(C.Map, Wp.X, Wp.Y) + FVector(0, 0, 90), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeBox(FVector(30, 30, 60)));
                FString Blockers;
                for (const FOverlapResult& O : Over) if (O.bBlockingHit) Blockers += GetNameSafe(O.GetActor()) + TEXT("/") + GetNameSafe(O.GetComponent()) + TEXT(" ");
                FString Path;
                for (const FIntPoint& T : F->Trail) Path += QA::TileText(Test, T) + TEXT(" > ");
                UE_LOG(LogTemp, Display, TEXT("WARP_BLOCKERS %s; trail %s; red %s"), *Blockers, *Path, *R->GetActorLocation().ToString());
            }
            if (Test.Seconds - St.T0 > 3.5) M4_FAIL(TEXT("%s warp %d at %d,%d did nothing when walked into from %d,%d (Red at %s, script '%s', locks %s)"), *C.Map.ToString(), C.Warp, Wp.X, Wp.Y, C.From.X, C.From.Y,
                *QA::TileText(Test, F->KantoTile), *UPYRScripts::Get(Test.World)->RunningName(), F->IsLockedBy(TEXT("script")) ? TEXT("script") : F->IsLockedBy(TEXT("menu")) ? TEXT("menu") : TEXT("none"));
            HidePeople(Test);
            R->TestInput = FVector(YDir::Delta(C.Dir).X, YDir::Delta(C.Dir).Y, 0.f);
            return EGameTest::Running;
        }
        StopWalking(Test);
        M4_NEXT();
    }
    // Landed (after the fade and the step off a door), or stopped.
    if ((F->IsWarping() || R->IsScriptWalking()) && Test.Seconds - St.T0 < 4.0) return EGameTest::Running;
    M4_WAIT(.3);
    FName To;
    int32 ToWarp = INDEX_NONE;
    if (!W->WarpDestination(C.Map, C.Warp, F->LastOutdoor, To, ToWarp))
    {
        // Stopped with Yellow's words where Yellow has some (Route 23's guards, the man at Cerulean Cave); Diglett's Cave,
        // behind Route 2's Cut trees, has none, and Red only steps back.
        const FString Line = F->BlockedWarpText(C.Map, C.Warp);
        if (Line.IsEmpty() && Wp.ToMap != TEXT("DIGLETTS_CAVE")) M4_FAIL(TEXT("%s warp %d leads out of the range with no line to stop Red"), *C.Map.ToString(), C.Warp);
        if (F->CurrentMap != C.Map) M4_FAIL(TEXT("%s warp %d (to %s) took Red to %s"), *C.Map.ToString(), C.Warp, *Wp.ToMap.ToString(), *F->CurrentMap.ToString());
        if (!Line.IsEmpty() && !Menus(Test)->AnyOpen() && !SaidContains(Test, FirstLineOf(PYRGb::Fill(Test.World, Line)))) M4_FAIL(TEXT("%s warp %d: Red was not told why he can't go"), *C.Map.ToString(), C.Warp);
        ++Blocked;
    }
    else
    {
        const FYMap* Dest = D->Map(To);
        const FYWarp& DW = Dest->Warps[ToWarp];
        // Someone standing on the step (Cerulean's GUARD2 outside the trashed house) holds Red on the door, as the
        // Game Boy's forced step is blocked by a sprite.
        bool bSomeoneBelow = false;
        for (const TWeakObjectPtr<APYRPerson>& P : F->People) bSomeoneBelow |= P.IsValid() && P->IsShown() && P->Tile == Dest->Origin + FIntPoint(DW.X, DW.Y + 1);
        const bool bStep = DW.bDoor && !bSomeoneBelow && Dest->Contains(DW.X, DW.Y + 1) && !YTile::IsSolid(Dest->Tile(DW.X, DW.Y + 1)) && Dest->WarpAt(DW.X, DW.Y + 1) == INDEX_NONE;
        const FIntPoint Want(DW.X, DW.Y + (bStep ? 1 : 0));
        if (F->CurrentMap != To || F->CurrentTile != Want)
            M4_FAIL(TEXT("%s warp %d at %d,%d: Red at %s %d,%d, Yellow puts him at %s %d,%d%s"), *C.Map.ToString(), C.Warp, Wp.X, Wp.Y, *F->CurrentMap.ToString(),
                F->CurrentTile.X, F->CurrentTile.Y, *To.ToString(), Want.X, Want.Y, bStep ? TEXT(" (a step down off the door)") : TEXT(""));
        if (bStep && F->Facing != EYDir::Down) M4_FAIL(TEXT("%s warp %d: after the step off the door Red faces %s, not down"), *C.Map.ToString(), C.Warp, YDir::Name(F->Facing));
        ++Landed;
    }
    ++St.I;
    St.Step = 1;
    return EGameTest::Running;
}

// Every map connection crossed at its first, middle and last open tile: Red lands on the neighbour's tile the
// connection's offset gives (pokeyellow data/maps/headers: other x = x - offset going north/south, y - offset east/west).
GAME_TEST(Roaming_EveryEdgeConnectsToYellowTile, 900)
{
    struct FEdge { FName Map; EYDir Dir; int32 X, Y; FName To; FIntPoint Want; };
    static FState St;
    static TArray<FEdge> Edges;
    static int32 MapsBefore = 0;
    static FName FirstMap;
    static FIntPoint FirstTile;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRField* F = Field(Test);
    const AYellowWorld* W = World(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    Quiet(true, true);
    UPYRScripts::Get(Test.World)->bSight = false;
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 30}});
        AfterPokedex(Test);
        State(Test)->AwardBadge(EPYRBadge::Boulder);
        HidePeople(Test);
        for (const FName& Id : D->MapOrder)
        {
            const FYMap* M = D->Map(Id);
            if (!M || !M->bOutdoor) continue;
            for (const EYDir Dir : {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right})
            {
                const FYConnection& C = M->Conn[int32(Dir)];
                const FYMap* O = C.bValid && !C.bOutOfScope ? D->Map(C.Map) : nullptr;
                if (!O) continue;
                TArray<FEdge> Side;
                const int32 Len = Dir == EYDir::Up || Dir == EYDir::Down ? M->Width : M->Height;
                for (int32 I = 0; I < Len; ++I)
                {
                    const int32 X = Dir == EYDir::Left ? 0 : Dir == EYDir::Right ? M->Width - 1 : I;
                    const int32 Y = Dir == EYDir::Up ? 0 : Dir == EYDir::Down ? M->Height - 1 : I;
                    const FIntPoint Back = FIntPoint(X, Y) - YDir::Delta(Dir);
                    if (!Standable(M, X, Y) || !Standable(M, Back.X, Back.Y)) continue;
                    const FIntPoint Want = Dir == EYDir::Up ? FIntPoint(X - C.Offset, O->Height - 1) : Dir == EYDir::Down ? FIntPoint(X - C.Offset, 0)
                        : Dir == EYDir::Left ? FIntPoint(O->Width - 1, Y - C.Offset) : FIntPoint(0, Y - C.Offset);
                    if (!O->Contains(Want.X, Want.Y) || YTile::IsSolid(O->Tile(Want.X, Want.Y))) continue;
                    Side.Add({Id, Dir, X, Y, C.Map, Want});
                }
                if (Side.Num() == 0) M4_FAIL(TEXT("%s's %s edge into %s has no open crossing"), *Id.ToString(), YDir::Name(Dir), *C.Map.ToString());
                Edges.Add(Side[0]);
                if (Side.Num() > 2) Edges.Add(Side[Side.Num() / 2]);
                if (Side.Num() > 1) Edges.Add(Side.Last());
            }
        }
        M4_NEXT();
    }
    if (St.I >= Edges.Num()) M4_PASS(TEXT("%d crossings, each landing on the neighbour's tile at pokeyellow's offset"), Edges.Num());
    const FEdge& E = Edges[St.I];
    if (St.Step == 1)
    {
        const FIntPoint Back = FIntPoint(E.X, E.Y) - YDir::Delta(E.Dir);
        Place(Test, E.Map, Back.X, Back.Y, E.Dir);
        MapsBefore = F->MapChanges;
        FirstMap = NAME_None;
        M4_NEXT();
    }
    M4_WAIT(.15);
    if (FirstMap.IsNone() && F->MapChanges != MapsBefore) { FirstMap = F->CurrentMap; FirstTile = F->CurrentTile; }
    if (FirstMap.IsNone())
    {
        if (Test.Seconds - St.T0 > 3.0) M4_FAIL(TEXT("walking %s from %s %d,%d never reached %s"), YDir::Name(E.Dir), *E.Map.ToString(), E.X, E.Y, *E.To.ToString());
        R->TestInput = FVector(YDir::Delta(E.Dir).X, YDir::Delta(E.Dir).Y, 0.f);
        return EGameTest::Running;
    }
    StopWalking(Test);
    if (FirstMap != E.To || FirstTile != E.Want)
        M4_FAIL(TEXT("%s %d,%d going %s: landed on %s %d,%d, Yellow's connection gives %s %d,%d"), *E.Map.ToString(), E.X, E.Y, YDir::Name(E.Dir), *FirstMap.ToString(),
            FirstTile.X, FirstTile.Y, *E.To.ToString(), E.Want.X, E.Want.Y);
    ++St.I;
    St.Step = 1;
    return EGameTest::Running;
}

// From every outdoor edge tile, 1 to 8 tiles out: either the connected map, or drawn scenery that is the neighbour's
// own look where a connection leads out of the range (Route 21's sea, Route 23, Routes 9 and 5), else the map's
// border block. Never nothing.
GAME_TEST(Roaming_SceneryPastEveryEdge, 300)
{
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    const AYellowWorld* W = World(Test);
    const TMap<FIntPoint, uint8> Drawn = QA::DrawnOffMap(Test, 8);
    TMap<FName, int32> Sources;
    int32 Checked = 0;
    for (const FName& Id : D->MapOrder)
    {
        const FYMap* M = D->Map(Id);
        if (!M || !M->bOutdoor) continue;
        for (const EYDir Dir : {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right})
        {
            const FYConnection& C = M->Conn[int32(Dir)];
            const int32 Len = Dir == EYDir::Up || Dir == EYDir::Down ? M->Width : M->Height;
            for (int32 I = 0; I < Len; ++I)
            {
                const int32 X = Dir == EYDir::Left ? 0 : Dir == EYDir::Right ? M->Width - 1 : I;
                const int32 Y = Dir == EYDir::Up ? 0 : Dir == EYDir::Down ? M->Height - 1 : I;
                for (int32 Out = 1; Out <= 8; ++Out)
                {
                    const FIntPoint K = M->Origin + FIntPoint(X, Y) + YDir::Delta(Dir) * Out;
                    if (W->MapAtKanto(K)) continue;   // a map continues here
                    ++Checked;
                    const TCHAR Look = W->SceneryLookAt(K);
                    const FName From = W->ScenerySourceAt(K);
                    if (!Look || !Drawn.FindRef(K))
                        M4_FAIL(TEXT("nothing drawn %d tiles past %s's %s edge at %d,%d (Kanto %d,%d)"), Out, *Id.ToString(), YDir::Name(Dir), X, Y, K.X, K.Y);
                    // Where an out-of-range connection covers the tile, its own map must be what is drawn there.
                    if (C.bValid && C.bOutOfScope)
                        for (const FYMap& S : D->Scenery)
                            if (S.Id == C.Map && S.WorldRect().Contains(K) && From != S.Id)
                                M4_FAIL(TEXT("%d tiles past %s's %s edge: %s drawn where %s is"), Out, *Id.ToString(), YDir::Name(Dir), *From.ToString(), *C.Map.ToString());
                    Sources.FindOrAdd(From)++;
                }
            }
        }
    }
    FString Note;
    for (const auto& Pair : Sources) Note += FString::Printf(TEXT("%s %d, "), *Pair.Key.ToString(), Pair.Value);
    if (!Sources.Contains(TEXT("ROUTE_21"))) M4_FAIL(TEXT("no Route 21 past Pallet Town's south edge: %s"), *Note);
    M4_PASS(TEXT("%d tiles 1-8 past the edges, all drawn: %s"), Checked, *Note);
}

// Pallet Town to Cerulean City on foot along a planned path (Route 1, Viridian, Route 2, the forest, Pewter, Route 3,
// Mt. Moon's floors, Route 4), in the one level: no level load, no world rebuilt.
GAME_TEST(Roaming_NoLoadingPalletToCerulean, 1200)
{
    static FState St;
    static QA::FWalker Walker;
    static TWeakObjectPtr<AYellowWorld> StartWorld;
    static TWeakObjectPtr<ULevel> StartLevel;
    static int32 Levels = 0;
    static TArray<FName> Visited;
    static const TCHAR* Legs[] = {TEXT("ROUTE_1"), TEXT("VIRIDIAN_CITY"), TEXT("ROUTE_2"), TEXT("VIRIDIAN_FOREST"), TEXT("PEWTER_CITY"), TEXT("ROUTE_3"),
        TEXT("MT_MOON_1F"), TEXT("MT_MOON_B2F"), TEXT("ROUTE_4"), TEXT("CERULEAN_CITY")};
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRField* F = Field(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    Quiet(true, true);
    UPYRScripts::Get(Test.World)->bSight = false;
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 30}});
        AfterPokedex(Test);
        State(Test)->AwardBadge(EPYRBadge::Boulder);
        HidePeople(Test);
        StartWorld = World(Test);
        StartLevel = Test.World->PersistentLevel;
        Levels = Test.World->GetLevels().Num();
        Place(Test, TEXT("PALLET_TOWN"), 10, 12, EYDir::Up);
        R->SetRun(true);
        St.I = 0;
        M4_NEXT();
    }
    if (World(Test) != StartWorld.Get() || Test.World->PersistentLevel != StartLevel.Get() || Test.World->GetLevels().Num() != Levels)
        M4_FAIL(TEXT("the level changed on the way (at %s)"), *F->CurrentMap.ToString());
    if (Visited.Num() == 0 || Visited.Last() != F->CurrentMap) Visited.Add(F->CurrentMap);
    if (St.Step == 1)
    {
        if (St.I >= UE_ARRAY_COUNT(Legs))
        {
            FString Path;
            for (const FName& N : Visited) Path += (Path.IsEmpty() ? TEXT("") : TEXT(" > ")) + N.ToString();
            M4_PASS(TEXT("walked Pallet Town to Cerulean City in the one level (%d levels throughout): %s"), Levels, *Path);
        }
        const FYMap* M = D->Map(Legs[St.I]);
        if (!M) M4_FAIL(TEXT("no %s"), Legs[St.I]);
        // The walkable tile nearest the map's middle that Red can reach (parts of Route 2 need Cut; for the cave's
        // floors, one on the way through).
        TArray<FIntPoint> Goals;
        for (int32 Y = 0; Y < M->Height; ++Y)
            for (int32 X = 0; X < M->Width; ++X)
                if (Standable(M, X, Y)) Goals.Add(FIntPoint(X, Y));
        const FIntPoint Mid(M->Width / 2, M->Height / 2);
        Goals.Sort([Mid](const FIntPoint& A, const FIntPoint& B) { return (A - Mid).SizeSquared() < (B - Mid).SizeSquared(); });
        bool bStarted = false;
        for (int32 G = 0; G < Goals.Num() && G < 400 && !bStarted; G += 4) bStarted = Walker.Start(Test, M->Origin + Goals[G]);
        if (!bStarted) M4_FAIL(TEXT("no way to %s: %s"), Legs[St.I], *Walker.Why);
        M4_NEXT();
    }
    R->SetRun(true);
    // Each map shows its people again on arrival; this walk is about the world, so they stay out of the way.
    HidePeople(Test);
    // Story scenes on the way (the old man's catching demo) are played through as a player would.
    if (!QA::Idle(Test) && !F->IsWarping()) { StopWalking(Test); QA::Drive(Test); return EGameTest::Running; }
    const QA::FWalker::EState S = Walker.Tick(Test);
    if (S == QA::FWalker::EState::Stuck || S == QA::FWalker::EState::NoPath) M4_FAIL(TEXT("on the way to %s: %s"), Legs[St.I], *Walker.Why);
    if (S == QA::FWalker::EState::Arrived || F->CurrentMap == Legs[St.I])
    {
        StopWalking(Test);
        ++St.I;
        St.Step = 1;
    }
    if (Test.Seconds - St.T0 > 400) M4_FAIL(TEXT("still walking to %s after 400 s (at %s)"), Legs[St.I], *QA::TileText(Test, F->KantoTile));
    return EGameTest::Running;
}

// Through the doors of Oak's lab, the Viridian Pokemon Centre and the Viridian Mart: straight into the room, on the
// Yellow tile, with a fade no longer than the Game Boy's door transition (under 0.6 s dark, under 1.2 s in all).
GAME_TEST(Roaming_DoorsOpenStraightIntoRooms, 150)
{
    static FState St;
    static double WarpStart = -1, DarkFrom = -1, Dark = 0;
    static const TCHAR* Doors[][2] = {{TEXT("PALLET_TOWN"), TEXT("OAKS_LAB")}, {TEXT("VIRIDIAN_CITY"), TEXT("VIRIDIAN_POKECENTER")}, {TEXT("VIRIDIAN_CITY"), TEXT("VIRIDIAN_MART")}};
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRField* F = Field(Test);
    const AYellowWorld* W = World(Test);
    Quiet(true, true);
    if (St.Step == 0) { AfterPokedex(Test); HidePeople(Test); St.Step = 1; return EGameTest::Running; }
    if (St.I >= 3) M4_PASS(TEXT("%s"), *St.Note);
    const FName From = Doors[St.I][0], To = Doors[St.I][1];
    const FYMap* A = D->Map(From);
    if (St.Step == 1)
    {
        int32 Warp = INDEX_NONE;
        for (int32 I = 0; I < A->Warps.Num(); ++I) if (A->Warps[I].ToMap == To) { Warp = I; break; }
        if (Warp == INDEX_NONE) M4_FAIL(TEXT("no door from %s into %s"), *From.ToString(), *To.ToString());
        St.P = FIntPoint(A->Warps[Warp].X, A->Warps[Warp].Y);
        St.N = Warp;
        Place(Test, From, St.P.X, St.P.Y + 1, EYDir::Up);
        WarpStart = DarkFrom = -1;
        Dark = 0;
        M4_NEXT();
    }
    M4_WAIT(.2);
    if (F->IsWarping() && WarpStart < 0) WarpStart = Test.Seconds;
    if (F->GetFade() > .98f) { if (DarkFrom < 0) DarkFrom = Test.Seconds; }
    else if (DarkFrom >= 0) { Dark += Test.Seconds - DarkFrom; DarkFrom = -1; }
    if (F->CurrentMap == From && !F->IsWarping()) { WalkToTile(Test, From, St.P.X, St.P.Y, 5.f); if (Test.Seconds - St.T0 > 5) M4_FAIL(TEXT("never went through %s's door"), *To.ToString()); return EGameTest::Running; }
    if (F->IsWarping() || F->GetFade() > .01f) return EGameTest::Running;
    StopWalking(Test);
    const double Took = Test.Seconds - WarpStart;
    FName DestMap;
    int32 DestWarp = 0;
    W->WarpDestination(From, St.N, From, DestMap, DestWarp);
    const FYWarp& Wp = D->Map(To)->Warps[DestWarp];
    if (F->CurrentMap != To || F->CurrentTile != FIntPoint(Wp.X, Wp.Y)) M4_FAIL(TEXT("%s's door put Red at %s %d,%d, not %d,%d"), *To.ToString(), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y, Wp.X, Wp.Y);
    if (Dark > .6 || Took > 1.2) M4_FAIL(TEXT("%s's door: %.2f s dark, %.2f s in all (the Game Boy's door is a quick fade)"), *To.ToString(), Dark, Took);
    St.Note += FString::Printf(TEXT("%s -> %s %d,%d in %.2f s (%.2f s dark); "), *From.ToString(), *To.ToString(), Wp.X, Wp.Y, Took, Dark);
    ++St.I;
    St.Step = 1;
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ trainers
// Every trainer on the maps fights with trainers.json's party (data/trainers/parties.asm: species and levels) for
// base money x the last level; Yellow's story teams are the ones the task names.
GAME_TEST(Trainers_TeamsMatchYellow, 180)
{
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRScripts* Sc = UPYRScripts::Get(Test.World);
    int32 Trainers = 0;
    for (const TWeakObjectPtr<APYRPerson>& PW : Field(Test)->People)
    {
        APYRPerson* P = PW.Get();
        if (!P || !P->IsTrainer()) continue;
        const TArray<FYSlot> Party = D->TrainerParty(P->Trainer);
        const FYTrainerClass* C = D->TrainerClass(P->Trainer.Class);
        if (Party.Num() == 0 || !C) M4_FAIL(TEXT("%s %s: no party %s #%d in trainers.json"), *P->Map.ToString(), *P->Id, *P->Trainer.Class.ToString(), P->Trainer.Party);
        FPYRFight Fight;
        if (!Sc->TrainerFight(P, Fight)) M4_FAIL(TEXT("%s %s: no fight"), *P->Map.ToString(), *P->Id);
        if (Fight.Foes.Num() != Party.Num()) M4_FAIL(TEXT("%s %s: %d Pokemon, Yellow has %d"), *P->Map.ToString(), *P->Id, Fight.Foes.Num(), Party.Num());
        for (int32 I = 0; I < Party.Num(); ++I)
            if (Fight.Foes[I].Species != Party[I].Species || Fight.Foes[I].Level != Party[I].Level)
                M4_FAIL(TEXT("%s %s slot %d: %s L%d, Yellow has %s L%d"), *P->Map.ToString(), *P->Id, I, *Fight.Foes[I].Species.ToString(), Fight.Foes[I].Level,
                    *Party[I].Species.ToString(), Party[I].Level);
        if (Fight.Prize != C->BaseMoney * Party.Last().Level)
            M4_FAIL(TEXT("%s %s: prize %d, Yellow pays %d x %d"), *P->Map.ToString(), *P->Id, Fight.Prize, C->BaseMoney, Party.Last().Level);
        ++Trainers;
    }
    // The story's teams (events.json names each fight's class and party number).
    struct FWant { const TCHAR* Event; std::initializer_list<TPair<const TCHAR*, int32>> Team; };
    const FWant Wants[] = {
        {TEXT("oaks_lab_rival_battle"), {{TEXT("eevee"), 5}}},
        {TEXT("route22_rival_battle_1"), {{TEXT("spearow"), 9}, {TEXT("eevee"), 8}}},
        {TEXT("pewter_gym_brock_tm34"), {{TEXT("geodude"), 10}, {TEXT("onix"), 12}}},
        {TEXT("cerulean_gym_misty_tm11"), {{TEXT("staryu"), 18}, {TEXT("starmie"), 21}}},
        {TEXT("mt_moon_jessie_james"), {{TEXT("ekans"), 14}, {TEXT("meowth"), 14}, {TEXT("koffing"), 14}}},
    };
    FString Note;
    for (const FWant& Want : Wants)
    {
        const TSharedPtr<FJsonObject> E = Sc->Event(Want.Event);
        const TSharedPtr<FJsonObject>* B = nullptr;
        if (!E.IsValid() || !E->TryGetObjectField(TEXT("battle"), B) || !B) M4_FAIL(TEXT("events.json has no fight %s"), Want.Event);
        FYTrainerRef Ref;
        Ref.bValid = true;
        Ref.Class = FName(*(*B)->GetStringField(TEXT("class")));
        Ref.Party = int32((*B)->GetNumberField(TEXT("trainer_no")));
        const TArray<FYSlot> Party = D->TrainerParty(Ref);
        FString Got;
        for (const FYSlot& S : Party) Got += FString::Printf(TEXT("%s L%d "), *S.Species.ToString(), S.Level);
        if (Party.Num() != int32(Want.Team.size())) M4_FAIL(TEXT("%s: %s, Yellow's team has %d"), Want.Event, *Got, int32(Want.Team.size()));
        int32 I = 0;
        for (const TPair<const TCHAR*, int32>& T : Want.Team)
        {
            if (Party[I].Species != FName(T.Key) || Party[I].Level != T.Value) M4_FAIL(TEXT("%s: %s, Yellow's is %s L%d at slot %d"), Want.Event, *Got, T.Key, T.Value, I);
            ++I;
        }
        Note += FString::Printf(TEXT("%s: %s; "), Want.Event, *Got.TrimEnd());
    }
    M4_PASS(TEXT("%d map trainers with trainers.json teams and prizes; %s"), Trainers, *Note);
}

// ------------------------------------------------------------------------------------------------ text
namespace
{
FString NormLine(const FString& In)
{
    FString S = In.Replace(TEXT("▶"), TEXT(" ")).Replace(TEXT("▼"), TEXT(" ")).Replace(TEXT("\r"), TEXT(""));
    S.TrimStartAndEndInline();
    while (S.Contains(TEXT("  "))) S.ReplaceInline(TEXT("  "), TEXT(" "));
    return S;
}

// Every line Yellow can print here: text.json and the system lines, with the names filled; lines with a RAM fill
// ({str:..}, {num:..}) become wildcard patterns.
void YellowLines(FGameTestContext& Test, TSet<FString>& Exact, TArray<FString>& Patterns)
{
    TArray<FString> Texts;
    Data(Test)->Text.GenerateValueArray(Texts);
    for (const auto& Pair : PYRSysText::Table()) Texts.Add(Pair.Value);
    for (const FString& T : Texts)
    {
        TArray<FString> Lines;
        PYRGb::Fill(Test.World, T).Replace(TEXT("\f"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), true);
        for (const FString& L : Lines)
        {
            if (!L.Contains(TEXT("{"))) { Exact.Add(NormLine(L)); continue; }
            FString P;
            for (int32 I = 0; I < L.Len(); ++I)
            {
                if (L[I] == TEXT('{')) { const int32 E = L.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I); if (E != INDEX_NONE) { P += TEXT("*"); I = E; continue; } }
                P.AppendChar(L[I]);
            }
            Patterns.Add(NormLine(P));
        }
    }
}
}

// Everyone and every sign from Pallet Town to Cerulean, talked to or read the way a player does (E from the next tile
// or across a counter): each answers, and every line shown is one of Yellow's (text.json, extracted from pokeyellow and
// checked by verify.py, or Yellow's system lines), the names filled in.
GAME_TEST(Text_EveryLineMatchesYellow, 1800)
{
    static FState St;
    static TArray<TWeakObjectPtr<AActor>> Who;
    static TSet<FString> Exact;
    static TArray<FString> Patterns;
    static int32 Lines = 0, Talked = 0;
    M4_READY();
    UPYRField* F = Field(Test);
    UPYRMenus* Me = Menus(Test);
    UPYRProgress* P = Progress(Test);
    Quiet(true, true);
    UPYRScripts::Get(Test.World)->bSight = false;
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 40}});
        AfterPokedex(Test);
        YellowLines(Test, Exact, Patterns);
        for (const TWeakObjectPtr<APYRPerson>& Pe : F->People) if (Pe.IsValid() && Pe->IsShown() && !Pe->Model.bProp) Who.Add(Pe.Get());
        for (const TWeakObjectPtr<APYRSignpost>& S : F->Signs) if (S.IsValid()) Who.Add(S.Get());
        M4_NEXT();
    }
    if (St.I >= Who.Num()) M4_PASS(TEXT("%d people and signs answered; %d lines shown, every one Yellow's"), Talked, Lines);
    AActor* A = Who[St.I].Get();
    APYRPerson* Pe = Cast<APYRPerson>(A);
    APYRSignpost* Sign = Cast<APYRSignpost>(A);
    const FString Name = Pe ? Pe->Map.ToString() + TEXT(" ") + Pe->Id : Sign ? Sign->Map.ToString() + TEXT(" sign ") + Sign->TextId : FString(TEXT("?"));
    auto Next = [&]() { if (Pe) Pe->bFrozen = false; Me->CloseAll(); StopWalking(Test); ++St.I; St.N = 0; St.Step = 1; St.T0 = Test.Seconds; };
    if (!A || (Pe && !Pe->IsShown())) { Next(); return EGameTest::Running; }
    if (St.Step == 1)
    {
        Me->CloseAll();
        // A walker stands still while Red talks to him (he would step off between placing Red and E).
        if (Pe) { Pe->bFrozen = true; if (Pe->IsMoving()) return EGameTest::Running; }
        if (Pe && Pe->IsTrainer() && !Pe->IsBeaten()) P->Beat(Pe->TrainerKey());
        FIntPoint Spot;
        EYDir Face = EYDir::Up;
        bool bOk = false;
        if (Pe) bOk = QA::TalkSpot(Test, Pe, Spot, Face);
        else
            for (const EYDir Dir : {EYDir::Up, EYDir::Left, EYDir::Right, EYDir::Down})
            {
                const FIntPoint From = Sign->Tile - YDir::Delta(Dir);
                const FYMap* M = World(Test)->MapAtKanto(From);
                if (M && M->Id == Sign->Map && !World(Test)->IsSolidAt(From) && M->WarpAt(From.X - M->Origin.X, From.Y - M->Origin.Y) == INDEX_NONE) { Spot = From; Face = Dir; bOk = true; break; }
            }
        if (!bOk) { Next(); return EGameTest::Running; }   // nobody can stand beside them (behind a counter corner)
        const FYMap* M = World(Test)->MapAtKanto(Spot);
        Place(Test, M->Id, Spot.X - M->Origin.X, Spot.Y - M->Origin.Y, Face);
        Me->TextLog.Reset();
        M4_NEXT();
    }
    if (St.Step == 2) { M4_WAIT(.25 + .35 * St.N); PressE(Test); St.A = 0; M4_NEXT(); }   // each try waits a little longer (a busy machine)
    // Through what is said: text on, NO to questions, menus left.
    const FName Top = TopId(Test);
    if (!Top.IsNone() && Top != TEXT("text") && Top != TEXT("choice") && Top != TEXT("naming")) { Me->Press(EPYRKey::Back); if (TopId(Test) == Top) Me->Pop(); }
    QA::Drive(Test, false);
    if (!QA::Idle(Test))
    {
        St.A = 0;
        if (Test.Seconds - St.T0 > 25) M4_FAIL(TEXT("%s: still talking after 25 s (%s)"), *Name, *Me->ScreenText().Left(160));
        return EGameTest::Running;
    }
    if (St.A == 0) { St.A = Test.Seconds; return EGameTest::Running; }
    if (Test.Seconds - St.A < .4) return EGameTest::Running;
    // A walker can take a step as Red arrives, and E can land while the last talk is still letting go: up to five
    // tries from wherever they stand.
    if (Me->TextLog.Num() == 0 && St.N < 4) { ++St.N; St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    if (Me->TextLog.Num() == 0)
        M4_FAIL(TEXT("%s said nothing to E (Red at %s facing %s; they at %s%s)"), *Name, *QA::TileText(Test, F->KantoTile), YDir::Name(F->Facing),
            Pe ? *QA::TileText(Test, Pe->Tile) : TEXT("-"), Pe && Pe->IsMoving() ? TEXT(", moving") : TEXT(""));
    for (const FString& Page : Me->TextLog)
    {
        TArray<FString> PageLines;
        Page.ParseIntoArray(PageLines, TEXT("\n"), true);
        for (const FString& Raw : PageLines)
        {
            const FString L = NormLine(Raw);
            if (L.IsEmpty() || L.StartsWith(TEXT("["))) continue;
            ++Lines;
            bool bOk = Exact.Contains(L);
            for (int32 I = 0; !bOk && I < Patterns.Num(); ++I) bOk = L.MatchesWildcard(Patterns[I], ESearchCase::CaseSensitive);
            if (!bOk) M4_FAIL(TEXT("%s said '%s', which is not Yellow's"), *Name, *L);
        }
    }
    ++Talked;
    Next();
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ models
namespace
{
bool MeshExists(const FString& Folder, const FString& Id)
{
    return !Id.IsEmpty() && (FPackageName::DoesPackageExist(FString::Printf(TEXT("/Game/%s/%s/SK_%s"), *Folder, *Id, *Id))
        || FPackageName::DoesPackageExist(FString::Printf(TEXT("/Game/%s/%s/SM_%s"), *Folder, *Id, *Id)));
}
}

// Every Game Boy person sprite on the maps wears an imported model (no stand-in capsule), and every species Red can
// meet, be given, trade for or evolve into by Cerulean (wild tables, trainers' teams, gifts, the Route 2 trade, their
// level and Moon Stone evolutions) has its model for the battle stage.
GAME_TEST(Models_EverySpriteAndSpeciesResolves, 120)
{
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    TArray<FString> Missing;
    int32 Sprites = 0;
    TSet<FName> SeenSprites;
    for (const TWeakObjectPtr<APYRPerson>& PW : Field(Test)->People)
    {
        const APYRPerson* P = PW.Get();
        if (!P || P->Model.bProp) continue;
        ++Sprites;
        SeenSprites.Add(P->Sprite);
        if (!MeshExists(P->Model.bPokemon ? TEXT("Pokemon") : TEXT("Characters"), P->Model.Model))
            Missing.AddUnique(FString::Printf(TEXT("%s (%s %s -> %s)"), *P->Sprite.ToString(), *P->Map.ToString(), *P->Id, *P->Model.Model));
        // What is drawn: a mesh, never the primitive stand-in.
        TArray<UMeshComponent*> Meshes;
        P->GetComponents<UMeshComponent>(Meshes);
        bool bModel = false;
        for (const UMeshComponent* C : Meshes)
        {
            const FString Path = C->IsA<USkeletalMeshComponent>() ? GetPathNameSafe(Cast<USkeletalMeshComponent>(C)->GetSkeletalMeshAsset())
                : C->IsA<UStaticMeshComponent>() ? GetPathNameSafe(Cast<UStaticMeshComponent>(C)->GetStaticMesh()) : FString();
            bModel |= Path.StartsWith(TEXT("/Game/Characters/")) || Path.StartsWith(TEXT("/Game/Pokemon/"));
        }
        if (!bModel) Missing.AddUnique(FString::Printf(TEXT("%s %s draws no model"), *P->Map.ToString(), *P->Id));
    }
    // The species within reach: wild ones, the trainers Red can fight (Viridian Gym stays locked until seven badges),
    // gifts and trades; and what the ones Red can own (wild, gifts, trades) grow into by the highest level anyone
    // within reach has.
    TSet<FName> Species = {TEXT("pikachu"), TEXT("eevee")};
    TSet<FName> Owned = {TEXT("pikachu"), TEXT("eevee")};
    int32 TopLevel = 0;
    auto Meet = [&Species, &TopLevel](const FYSlot& S) { Species.Add(S.Species); TopLevel = FMath::Max(TopLevel, S.Level); };
    for (const FName& Id : D->MapOrder) if (const FYWild* W = D->Wild.Find(Id)) for (const FYSlot& S : W->Grass) { Meet(S); Owned.Add(S.Species); }
    for (const TWeakObjectPtr<APYRPerson>& PW : Field(Test)->People)
        if (PW.IsValid() && PW->IsTrainer() && PW->Map != FName(TEXT("VIRIDIAN_GYM"))) for (const FYSlot& S : D->TrainerParty(PW->Trainer)) Meet(S);
    for (const TSharedPtr<FJsonObject>& E : D->Events)
    {
        const TSharedPtr<FJsonObject>* B = nullptr;
        if (E->TryGetObjectField(TEXT("battle"), B) && B)
        {
            FYTrainerRef Ref;
            Ref.bValid = true;
            Ref.Class = FName(*(*B)->GetStringField(TEXT("class")));
            Ref.Party = int32((*B)->GetNumberField(TEXT("trainer_no")));
            for (const FYSlot& S : D->TrainerParty(Ref)) Meet(S);
        }
        const TArray<TSharedPtr<FJsonValue>>* Gives = nullptr;
        if (E->TryGetArrayField(TEXT("gives"), Gives))
            for (const TSharedPtr<FJsonValue>& G : *Gives)
            {
                FString Mon;
                if (G->Type == EJson::Object && G->AsObject()->TryGetStringField(TEXT("pokemon"), Mon)) { Species.Add(YKey::Species(Mon)); Owned.Add(YKey::Species(Mon)); }
            }
    }
    for (const TSharedPtr<FJsonValue>& T : D->Trades)
        if (T->Type == EJson::Object) { Species.Add(YKey::Species(T->AsObject()->GetStringField(TEXT("give")))); Species.Add(YKey::Species(T->AsObject()->GetStringField(TEXT("get")))); Owned.Add(YKey::Species(T->AsObject()->GetStringField(TEXT("get")))); }
    // Their evolutions by level (up to the top level within reach) or Moon Stone (Mt. Moon's), again and again.
    for (bool bGrew = true; bGrew && D->SpeciesJson.IsValid();)
    {
        bGrew = false;
        for (const auto& Pair : D->SpeciesJson->Values)
        {
            const FName Key = YKey::Species(FString(*Pair.Key));
            const TSharedPtr<FJsonObject> O = Pair.Value.IsValid() && Pair.Value->Type == EJson::Object ? Pair.Value->AsObject() : nullptr;
            const TArray<TSharedPtr<FJsonValue>>* Evos = nullptr;
            if (!O.IsValid() || !Owned.Contains(Key) || !O->TryGetArrayField(TEXT("evolutions"), Evos)) continue;
            for (const TSharedPtr<FJsonValue>& V : *Evos)
            {
                const TSharedPtr<FJsonObject> Ev = V->AsObject();
                const FString Method = Ev->GetStringField(TEXT("method"));
                FString Param;
                Ev->TryGetStringField(TEXT("param"), Param);
                const bool bLevel = Method == TEXT("level") && int32(Ev->GetNumberField(TEXT("param"))) <= TopLevel;
                if (!bLevel && !(Method == TEXT("item") && Param.Contains(TEXT("MOON_STONE")))) continue;
                const FName To = YKey::Species(Ev->GetStringField(TEXT("to")));
                if (!Owned.Contains(To)) { Owned.Add(To); Species.Add(To); bGrew = true; }
            }
        }
    }
    for (const FName& S : Species) if (!MeshExists(TEXT("Pokemon"), PYRSpecies::AssetId(S))) Missing.AddUnique(FString::Printf(TEXT("species %s (%s)"), *S.ToString(), *PYRSpecies::AssetId(S)));
    if (Missing.Num()) M4_FAIL(TEXT("%d without a model: %s"), Missing.Num(), *FString::Join(Missing, TEXT(", ")));
    M4_PASS(TEXT("%d people (%d sprite types) and %d species within reach (evolutions up to level %d), every one a model"), Sprites, SeenSprites.Num(), Species.Num(), TopLevel);
}
