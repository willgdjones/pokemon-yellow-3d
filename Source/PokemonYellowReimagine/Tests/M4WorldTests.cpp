// M4 world and roaming: the Game Boy maps built tile for tile, walking, ledges, doors, the no-load road to Cerulean,
// the captured mouse, the minimap, the Town Map, the map objects and the Game Boy music.
#include "Tests/M4TestKit.h"

#include "Components/CapsuleComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Hud/PYRHud.h"
#include "InputKeyEventArgs.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/YellowWorld.h"
#include "Menu/PYRPlayerController.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Music/GbMusicPlayer.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

using namespace M4;

namespace
{
bool Walkable(const FYMap& M, int32 X, int32 Y)
{
    return M.Contains(X, Y) && !YTile::IsSolid(M.Tile(X, Y)) && M.WarpAt(X, Y) == INDEX_NONE;
}

bool Plain(const FYMap& M, int32 X, int32 Y)
{
    return Walkable(M, X, Y) && M.Tile(X, Y) != TEXT('g');
}

// A crossing from map A into its neighbour B: the last tile in A and the first in B, both open.
bool FindCrossing(const AYellowWorld* W, const FYMap& A, FName B, FIntPoint& OutFrom, EYDir& OutDir)
{
    for (int32 D = 0; D < 4; ++D)
    {
        if (!A.Conn[D].bValid || A.Conn[D].Map != B) continue;
        const EYDir Dir = EYDir(D);
        const FIntPoint Step = YDir::Delta(Dir);
        const int32 Len = Step.X != 0 ? A.Height : A.Width;
        // From the middle out, so the crossing is a real road rather than a corner.
        for (int32 K = 0; K < Len; ++K)
        {
            const int32 I = Len / 2 + ((K % 2) ? (K + 1) / 2 : -K / 2);
            if (I < 0 || I >= Len) continue;
            const int32 X = Step.X > 0 ? A.Width - 1 : Step.X < 0 ? 0 : I;
            const int32 Y = Step.Y > 0 ? A.Height - 1 : Step.Y < 0 ? 0 : I;
            if (!Walkable(A, X, Y) || !Walkable(A, X - Step.X, Y - Step.Y)) continue;
            const FIntPoint K1 = A.Origin + FIntPoint(X, Y) + Step;
            const FYMap* Other = W->MapAtKanto(K1);
            if (!Other || Other->Id != B) continue;
            const FIntPoint L1 = K1 - Other->Origin;
            if (!Walkable(*Other, L1.X, L1.Y) || !Walkable(*Other, L1.X + Step.X, L1.Y + Step.Y)) continue;
            OutFrom = FIntPoint(X, Y) - Step;
            OutDir = Dir;
            return true;
        }
    }
    return false;
}

// A warp in A into B with an open tile next to it to walk from.
bool FindWarpApproach(const AYellowWorld* W, const FYMap& A, FName B, FName LastOutdoor, FIntPoint& OutFrom, EYDir& OutDir, int32& OutWarp)
{
    for (int32 I = 0; I < A.Warps.Num(); ++I)
    {
        FName To;
        int32 ToWarp = 0;
        if (!W->WarpDestination(A.Id, I, LastOutdoor, To, ToWarp) || To != B) continue;
        const FYWarp& Wp = A.Warps[I];
        // Doors are walked into from below; mats and ladders from any open side.
        for (const EYDir Dir : {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right})
        {
            const FIntPoint From = FIntPoint(Wp.X, Wp.Y) - YDir::Delta(Dir);
            if (!Walkable(A, From.X, From.Y)) continue;
            OutFrom = From;
            OutDir = Dir;
            OutWarp = I;
            return true;
        }
    }
    return false;
}

FString FirstLine(const FString& Text)
{
    FString T = Text.Replace(TEXT("\\n"), TEXT("\n")).Replace(TEXT("\\f"), TEXT("\n")).Replace(TEXT("\f"), TEXT("\n"));
    TArray<FString> Lines;
    T.ParseIntoArray(Lines, TEXT("\n"), true);
    for (const FString& L : Lines) if (!L.TrimStartAndEnd().IsEmpty()) return L.TrimStartAndEnd();
    return FString();
}
}

// ------------------------------------------------------------------------------------------------ maps
// Every manifest map is built, its size matches its file, and a collision probe of every tile matches `tiles`.
GAME_TEST(Map_TileExact, 240)
{
    static FState St;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    AYellowWorld* W = World(Test);
    if (St.Step == 0)
    {
        const FYMap* Pallet = D->Map(TEXT("PALLET_TOWN"));
        const FYMap* Route1 = D->Map(TEXT("ROUTE_1"));
        if (!Pallet || Pallet->Width != 20 || Pallet->Height != 18) M4_FAIL(TEXT("PALLET_TOWN is %dx%d, not 20x18"), Pallet ? Pallet->Width : 0, Pallet ? Pallet->Height : 0);
        if (!Route1 || Route1->Width != 20 || Route1->Height != 36) M4_FAIL(TEXT("ROUTE_1 is %dx%d, not 20x36"), Route1 ? Route1->Width : 0, Route1 ? Route1->Height : 0);
        if (W->NumMapsBuilt() != D->MapOrder.Num()) M4_FAIL(TEXT("%d maps built of %d in the manifest"), W->NumMapsBuilt(), D->MapOrder.Num());
        // The collision bodies are in the physics scene once one solid tile answers.
        FIntPoint Solid(-1, -1);
        for (int32 Y = 0; Y < Pallet->Height && Solid.X < 0; ++Y) for (int32 X = 0; X < Pallet->Width && Solid.X < 0; ++X) if (Pallet->Tile(X, Y) == TEXT('#')) Solid = FIntPoint(X, Y);
        if (!W->ProbeBlocked(Pallet->Origin + Solid))
        {
            if (Test.Seconds > 8.0) M4_FAIL(TEXT("no collision answers at PALLET_TOWN (%d,%d) after 8 s"), Solid.X, Solid.Y);
            return EGameTest::Running;
        }
        St.Note = FString::Printf(TEXT("collision answered at %.1f s; "), Test.Seconds);
        St.Step = 1;
        return EGameTest::Running;
    }
    // One map a frame: its file's size, its actor, and every tile probed.
    if (St.I < D->MapOrder.Num())
    {
        const FName Id = D->MapOrder[St.I++];
        const FYMap* M = D->Map(Id);
        if (!M || !W->MapActor(Id)) M4_FAIL(TEXT("%s is not built"), *Id.ToString());
        if (M->Tiles.Num() != M->Height) M4_FAIL(TEXT("%s: %d tile rows for height %d"), *Id.ToString(), M->Tiles.Num(), M->Height);
        for (int32 Y = 0; Y < M->Height; ++Y)
        {
            if (M->Tiles[Y].Len() != M->Width) M4_FAIL(TEXT("%s row %d is %d wide, not %d"), *Id.ToString(), Y, M->Tiles[Y].Len(), M->Width);
            for (int32 X = 0; X < M->Width; ++X)
            {
                const FIntPoint K = M->Origin + FIntPoint(X, Y);
                const bool bSolid = YTile::IsSolid(M->Tile(X, Y));
                const bool bProbe = W->ProbeBlocked(K);
                ++St.N;
                if (bSolid != bProbe)
                    M4_FAIL(TEXT("%s (%d,%d) is '%c' but the probe says %s"), *Id.ToString(), X, Y, M->Tile(X, Y), bProbe ? TEXT("blocked") : TEXT("open"));
            }
        }
        return EGameTest::Running;
    }
    M4_PASS(TEXT("%s%d maps, %d tiles probed, every one matches its tile; PALLET_TOWN 20x18, ROUTE_1 20x36; %d collision boxes"), *St.Note, D->MapOrder.Num(), St.N, W->TotalCollisionRects());
}

// Red walks diagonally over open ground without snapping to tiles, stops against a '#' within 0.1 m, slides along
// it, and never enters water.
GAME_TEST(Map_FreeWalkCollision, 120)
{
    static FState St;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    AYellowWorld* W = World(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    Quiet(true, true);
    static FIntPoint Open, Wall, Water;
    if (St.Step == 0)
    {
        // Find the three spots in the first outdoor maps that have them.
        bool bOpen = false, bWall = false, bWater = false;
        for (const FName& Id : {FName(TEXT("PALLET_TOWN")), FName(TEXT("ROUTE_1")), FName(TEXT("VIRIDIAN_CITY")), FName(TEXT("ROUTE_22"))})
        {
            const FYMap* M = D->Map(Id);
            if (!M) continue;
            for (int32 Y = 1; Y < M->Height - 5 && !bOpen; ++Y)
                for (int32 X = 1; X < M->Width - 5 && !bOpen; ++X)
                {
                    bool bAll = true;
                    for (int32 J = 0; J < 5 && bAll; ++J) for (int32 I = 0; I < 5 && bAll; ++I) bAll = Plain(*M, X + I, Y + J);
                    if (bAll) { Open = M->Origin + FIntPoint(X, Y); bOpen = true; }
                }
            // A '#' east of two open tiles, with the same shape one row down (to slide along).
            for (int32 Y = 0; Y < M->Height - 1 && !bWall; ++Y)
                for (int32 X = 2; X < M->Width - 1 && !bWall; ++X)
                    if (M->Tile(X + 1, Y) == TEXT('#') && M->Tile(X + 1, Y + 1) == TEXT('#') && Plain(*M, X, Y) && Plain(*M, X - 1, Y) && Plain(*M, X - 2, Y)
                        && Plain(*M, X, Y + 1) && !W->IsEdgeBlocked(M->Origin + FIntPoint(X, Y), EYDir::Down))
                    { Wall = M->Origin + FIntPoint(X, Y); bWall = true; }
            // Open ground north of water.
            for (int32 Y = 1; Y < M->Height && !bWater; ++Y)
                for (int32 X = 0; X < M->Width && !bWater; ++X)
                    if (M->Tile(X, Y) == TEXT('w') && Plain(*M, X, Y - 1) && Plain(*M, X, Y - 2)) { Water = M->Origin + FIntPoint(X, Y); bWater = true; }
        }
        if (!bOpen || !bWall || !bWater) M4_FAIL(TEXT("no test ground found (open %d, wall %d, water %d)"), bOpen, bWall, bWater);
        const FYMap* M = W->MapAtKanto(Open);
        Place(Test, M->Id, Open.X - M->Origin.X, Open.Y - M->Origin.Y, EYDir::Down);
        St.V = R->GetActorLocation();
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // Diagonal: south-east for 0.5 s.
        M4_WAIT(.2);
        R->TestInput = FVector(1, 1, 0);
        if (Test.Seconds - St.T0 < .7) return EGameTest::Running;
        R->TestInput = FVector::ZeroVector;
        const FVector Moved = R->GetActorLocation() - St.V;
        if (Moved.X < 60.f || Moved.Y < 60.f) M4_FAIL(TEXT("diagonal walk moved only (%.0f, %.0f)"), Moved.X, Moved.Y);
        if (FMath::Abs(Moved.X - Moved.Y) > 25.f) M4_FAIL(TEXT("diagonal walk bent: (%.0f, %.0f)"), Moved.X, Moved.Y);
        const float FracX = FMath::Fmod(R->GetActorLocation().X - W->GetOffset().X, 100.f), FracY = FMath::Fmod(R->GetActorLocation().Y - W->GetOffset().Y, 100.f);
        St.Note = FString::Printf(TEXT("diagonal (%.0f, %.0f) cm, stopped at tile fraction (%.0f, %.0f); "), Moved.X, Moved.Y, FracX, FracY);
        const FYMap* M = W->MapAtKanto(Wall);
        Place(Test, M->Id, Wall.X - 2 - M->Origin.X, Wall.Y - M->Origin.Y, EYDir::Right);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        // East into the wall for 1.2 s: he stops with his capsule at the tile edge.
        M4_WAIT(.2);
        R->TestInput = FVector(1, 0, 0);
        if (Test.Seconds - St.T0 < 1.4) return EGameTest::Running;
        R->TestInput = FVector::ZeroVector;
        const float Face = W->KantoCenter(Wall).X + 50.f;
        const float Gap = Face - (R->GetActorLocation().X + R->GetCapsuleComponent()->GetScaledCapsuleRadius());
        if (Gap < -2.f || Gap > 10.f) M4_FAIL(TEXT("stopped %.1f cm from the '#' edge (want 0..10)"), Gap);
        St.Note += FString::Printf(TEXT("stopped %.1f cm from the wall; "), Gap);
        St.V = R->GetActorLocation();
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        // Into the wall and south: he slides along it.
        R->TestInput = FVector(1, .8f, 0);
        if (Test.Seconds - St.T0 < .5) return EGameTest::Running;
        R->TestInput = FVector::ZeroVector;
        const FVector Moved = R->GetActorLocation() - St.V;
        if (Moved.Y < 40.f || FMath::Abs(Moved.X) > 5.f) M4_FAIL(TEXT("no slide along the wall: moved (%.1f, %.1f)"), Moved.X, Moved.Y);
        St.Note += FString::Printf(TEXT("slid %.0f cm along it; "), Moved.Y);
        const FYMap* M = W->MapAtKanto(Water);
        Place(Test, M->Id, Water.X - M->Origin.X, Water.Y - 2 - M->Origin.Y, EYDir::Down);
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        M4_WAIT(.2);
        R->TestInput = FVector(0, 1, 0);
        if (KantoTile(Test) == Water) M4_FAIL(TEXT("Red walked onto water at (%d,%d)"), Water.X, Water.Y);
        if (Test.Seconds - St.T0 < 1.6) return EGameTest::Running;
        R->TestInput = FVector::ZeroVector;
        M4_PASS(TEXT("%sheld at the water's edge (tile %d,%d)"), *St.Note, KantoTile(Test).X, KantoTile(Test).Y);
    }
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ roaming
// Pallet Town to Cerulean City through every connection and warp on the way, in the one world: no level loads.
GAME_TEST(Roam_PalletToCeruleanNoLoad, 300)
{
    static FState St;
    static TWeakObjectPtr<AYellowWorld> StartWorld;
    static TWeakObjectPtr<ULevel> StartLevel;
    static int32 Levels = 0;
    static FIntPoint Target;
    M4_READY();
    static const TCHAR* Route[] = {TEXT("PALLET_TOWN"), TEXT("ROUTE_1"), TEXT("VIRIDIAN_CITY"), TEXT("ROUTE_2"), TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), TEXT("VIRIDIAN_FOREST"),
        TEXT("VIRIDIAN_FOREST_NORTH_GATE"), TEXT("ROUTE_2"), TEXT("PEWTER_CITY"), TEXT("ROUTE_3"), TEXT("ROUTE_4"), TEXT("MT_MOON_1F"), TEXT("MT_MOON_B1F"), TEXT("MT_MOON_B2F"),
        TEXT("MT_MOON_B1F"), TEXT("ROUTE_4"), TEXT("CERULEAN_CITY")};
    constexpr int32 Legs = UE_ARRAY_COUNT(Route) - 1;
    const FYellowDataSet* D = Data(Test);
    AYellowWorld* W = World(Test);
    UPYRField* F = Field(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    Quiet(true, true);
    if (St.Step == 0)
    {
        AfterPokedex(Test);
        StartWorld = W;
        StartLevel = Test.World->PersistentLevel;
        Levels = Test.World->GetLevels().Num();
        Place(Test, TEXT("PALLET_TOWN"), 10, 10, EYDir::Up);
        St.Step = 1;
        St.I = 0;
        return EGameTest::Running;
    }
    if (W != StartWorld.Get() || Test.World->PersistentLevel != StartLevel.Get() || Test.World->GetLevels().Num() != Levels)
        M4_FAIL(TEXT("the world changed on leg %d (%s)"), St.I, Route[St.I]);
    if (St.I >= Legs)
        M4_PASS(TEXT("%d legs Pallet Town to Cerulean City: %s; the same world and level throughout, %d levels"), Legs, *St.Note, Levels);
    const FName From = Route[St.I], To = Route[St.I + 1];
    if (St.Step == 1)
    {
        // Stand next to the crossing into the next map.
        const FYMap* A = D->Map(From);
        if (!A || !D->Map(To)) M4_FAIL(TEXT("%s or %s is missing"), *From.ToString(), *To.ToString());
        FIntPoint At;
        EYDir Dir = EYDir::Up;
        int32 Warp = INDEX_NONE;
        bool bFound = FindCrossing(W, *A, To, At, Dir);
        // Mt. Moon's floors link through ladders; the rooms on B1F are separate, so the harness tries each warp in turn.
        if (!bFound) bFound = FindWarpApproach(W, *A, To, F->LastOutdoor, At, Dir, Warp);
        if (!bFound) M4_FAIL(TEXT("no way from %s into %s"), *From.ToString(), *To.ToString());
        Place(Test, From, At.X, At.Y, Dir);
        Target = A->Origin + At + YDir::Delta(Dir) * (Warp == INDEX_NONE ? 2 : 1);
        St.A = 0;
        St.Step = 2;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 2)
    {
        if (Test.Seconds - St.T0 < .3) return EGameTest::Running;
        if (F->CurrentMap == To && !F->IsWarping())
        {
            StopWalking(Test);
            St.Note += FString::Printf(TEXT("%s%s"), St.I ? TEXT(" > ") : TEXT(""), *To.ToString());
            ++St.I;
            St.Step = 1;
            return EGameTest::Running;
        }
        if (!F->IsWarping() && !F->IsLocked()) WalkTo(Test, W->KantoCenter(Target) + FVector(0, 0, R->GetActorLocation().Z), 5.f);
        if (Test.Seconds - St.T0 > 6.0) M4_FAIL(TEXT("stuck going from %s to %s at %s (%d,%d)"), *From.ToString(), *To.ToString(), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y);
    }
    return EGameTest::Running;
}

// A Route 1 ledge: hopping down works, walking back up does not.
GAME_TEST(Roam_LedgeOneWay, 90)
{
    static FState St;
    static FIntPoint Ledge;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    AYellowWorld* W = World(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    Quiet(true, true);
    const FYMap* M = D->Map(TEXT("ROUTE_1"));
    if (!M) M4_FAIL(TEXT("no ROUTE_1"));
    if (St.Step == 0)
    {
        bool bFound = false;
        for (int32 Y = 2; Y < M->Height - 2 && !bFound; ++Y)
            for (int32 X = 0; X < M->Width && !bFound; ++X)
                if (M->Tile(X, Y) == TEXT('v') && Plain(*M, X, Y - 1) && Plain(*M, X, Y - 2) && Walkable(*M, X, Y + 1)) { Ledge = FIntPoint(X, Y); bFound = true; }
        if (!bFound) M4_FAIL(TEXT("no 'v' ledge with open ground above and below on ROUTE_1"));
        Party(Test, {{TEXT("pikachu"), 6}});
        Place(Test, TEXT("ROUTE_1"), Ledge.X, Ledge.Y - 2, EYDir::Down);
        St.I = R->GetHops();
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.2);
        R->TestInput = FVector(0, 1, 0);
        const FIntPoint L = KantoTile(Test) - M->Origin;
        if (R->GetHops() > St.I && !R->IsHopping() && L.Y > Ledge.Y)
        {
            R->TestInput = FVector::ZeroVector;
            St.Note = FString::Printf(TEXT("hopped down over (%d,%d) to row %d; "), Ledge.X, Ledge.Y, L.Y);
            M4_NEXT();
        }
        if (Test.Seconds - St.T0 > 4.0) M4_FAIL(TEXT("no hop down the ledge at (%d,%d): Red at (%d,%d), hops %d"), Ledge.X, Ledge.Y, L.X, L.Y, R->GetHops());
        return EGameTest::Running;
    }
    if (St.Step == 2)
    {
        // Back up for 2 s: never onto the ledge row.
        R->TestInput = FVector(0, -1, 0);
        const FIntPoint L = KantoTile(Test) - M->Origin;
        if (L.Y <= Ledge.Y) M4_FAIL(TEXT("walked back up onto the ledge row (%d,%d)"), L.X, L.Y);
        if (Test.Seconds - St.T0 < 2.0) return EGameTest::Running;
        R->TestInput = FVector::ZeroVector;
        M4_PASS(TEXT("%sblocked walking back up (still at row %d)"), *St.Note, L.Y);
    }
    return EGameTest::Running;
}

// Ctrl crouches: Red is lower and slower in Route 1's tall grass and still walks through it.
GAME_TEST(Roam_CrouchInGrass, 90)
{
    static FState St;
    static FIntPoint Grass;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    Quiet(true, true);
    const FYMap* M = D->Map(TEXT("ROUTE_1"));
    if (!M) M4_FAIL(TEXT("no ROUTE_1"));
    if (St.Step == 0)
    {
        bool bFound = false;
        for (int32 Y = 0; Y < M->Height && !bFound; ++Y)
            for (int32 X = 0; X < M->Width - 3 && !bFound; ++X)
                if (M->Tile(X, Y) == TEXT('g') && M->Tile(X + 1, Y) == TEXT('g') && M->Tile(X + 2, Y) == TEXT('g')) { Grass = FIntPoint(X, Y); bFound = true; }
        if (!bFound) M4_FAIL(TEXT("no three grass tiles in a row on ROUTE_1"));
        Party(Test, {{TEXT("pikachu"), 6}});
        Place(Test, TEXT("ROUTE_1"), Grass.X, Grass.Y, EYDir::Right);
        St.A = R->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.2);
        // Ctrl, as the player presses it.
        if (APlayerController* PC = Test.World->GetFirstPlayerController()) PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftControl, IE_Pressed, 1.f));
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.2);
        if (!R->IsCrouching()) M4_FAIL(TEXT("Ctrl did not crouch"));
        const float Half = R->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (Half > St.A * .8f) M4_FAIL(TEXT("crouched capsule %.0f is not lower than standing %.0f"), Half, St.A);
        St.Note = FString::Printf(TEXT("crouched: half height %.0f -> %.0f; "), St.A, Half);
        St.V = R->GetActorLocation();
        St.B = 0;
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        R->TestInput = FVector(1, 0, 0);
        St.B = FMath::Max(St.B, R->GetGroundSpeed());
        if (Test.Seconds - St.T0 < 1.2) return EGameTest::Running;
        R->TestInput = FVector::ZeroVector;
        const float Moved = R->GetActorLocation().X - St.V.X;
        const float Walk = APokemonYellowReimagineCharacter::WalkSpeed;
        if (Moved < 60.f) M4_FAIL(TEXT("crouched Red moved only %.0f cm through the grass"), Moved);
        if (St.B > Walk * .6f) M4_FAIL(TEXT("crouched speed %.0f is not slower than walking %.0f"), St.B, Walk);
        const FIntPoint L = KantoTile(Test) - M->Origin;
        if (M->Tile(L.X, L.Y) != TEXT('g')) St.Note += TEXT("(left the grass patch) ");
        if (APlayerController* PC = Test.World->GetFirstPlayerController()) PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftControl, IE_Pressed, 1.f));
        St.Note += FString::Printf(TEXT("moved %.0f cm at up to %.0f cm/s (walking %.0f); "), Moved, St.B, Walk);
        M4_NEXT();
    }
    M4_WAIT(.3);
    if (R->IsCrouching()) M4_FAIL(TEXT("Ctrl again did not stand him up"));
    M4_PASS(TEXT("%sstood up again"), *St.Note);
}

// The door tiles of Oak's lab, the Viridian Centre and the Viridian Mart put Red on the matching interior warp.
GAME_TEST(Roam_DoorsIntoRooms, 120)
{
    static FState St;
    static int32 WarpIndex = 0;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    AYellowWorld* W = World(Test);
    UPYRField* F = Field(Test);
    Quiet(true, true);
    static const TCHAR* Doors[][2] = {{TEXT("PALLET_TOWN"), TEXT("OAKS_LAB")}, {TEXT("VIRIDIAN_CITY"), TEXT("VIRIDIAN_POKECENTER")}, {TEXT("VIRIDIAN_CITY"), TEXT("VIRIDIAN_MART")}};
    if (St.Step == 0) { AfterPokedex(Test); St.Step = 1; return EGameTest::Running; }
    if (St.I >= 3) M4_PASS(TEXT("%s"), *St.Note);
    const FName From = Doors[St.I][0], To = Doors[St.I][1];
    const FYMap* A = D->Map(From);
    if (St.Step == 1)
    {
        FIntPoint At;
        EYDir Dir;
        if (!A || !FindWarpApproach(W, *A, To, From, At, Dir, WarpIndex)) M4_FAIL(TEXT("no door from %s into %s"), *From.ToString(), *To.ToString());
        Place(Test, From, At.X, At.Y, Dir);
        St.P = FIntPoint(A->Warps[WarpIndex].X, A->Warps[WarpIndex].Y);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.2);
        if (!F->IsWarping() && F->CurrentMap == From) WalkToTile(Test, From, St.P.X, St.P.Y, 5.f);
        if (F->CurrentMap == To && !F->IsWarping())
        {
            StopWalking(Test);
            FName DestMap;
            int32 DestWarp = 0;
            W->WarpDestination(From, WarpIndex, From, DestMap, DestWarp);
            const FYMap* B = D->Map(To);
            const FYWarp& Wp = B->Warps[DestWarp];
            if (F->CurrentTile != FIntPoint(Wp.X, Wp.Y)) M4_FAIL(TEXT("%s's door put Red at (%d,%d), not on warp %d (%d,%d)"), *To.ToString(), F->CurrentTile.X, F->CurrentTile.Y, DestWarp, Wp.X, Wp.Y);
            St.Note += FString::Printf(TEXT("%s door (%d,%d) -> %s warp %d (%d,%d); "), *From.ToString(), St.P.X, St.P.Y, *To.ToString(), DestWarp, Wp.X, Wp.Y);
            ++St.I;
            St.Step = 1;
            return EGameTest::Running;
        }
        if (Test.Seconds - St.T0 > 5.0) M4_FAIL(TEXT("walking into %s's door: Red at %s (%d,%d)"), *To.ToString(), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y);
    }
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ camera and HUD
// The mouse is captured for good; a big mouse move still turns the camera; a menu frees it and closing recaptures it.
GAME_TEST(Camera_MouseCaptured, 60)
{
    static FState St;
    if (!ReadyAnyData(Test)) return EGameTest::Running;
    APYRPlayerController* PC = Cast<APYRPlayerController>(Test.World->GetFirstPlayerController());
    if (!PC) M4_FAIL(TEXT("no APYRPlayerController"));
    UGameViewportClient* VC = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->ViewportClient : nullptr;
    if (St.Step == 0)
    {
        if (!PC->IsMouseCaptured()) M4_FAIL(TEXT("the mouse is not captured at the start"));
        if (PC->bShowMouseCursor) M4_FAIL(TEXT("the cursor shows in the field"));
        if (VC && VC->GetMouseCaptureMode() != EMouseCaptureMode::CapturePermanently && VC->GetMouseCaptureMode() != EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown)
            M4_FAIL(TEXT("viewport capture mode %d, not permanent"), int32(VC->GetMouseCaptureMode()));
        St.A = PC->GetControlRotation().Yaw;
        // One large mouse move (as a fast flick sends).
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 60.f, 1));
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.1);
        const float Turned = FMath::Abs(FMath::FindDeltaAngleDegrees(St.A, PC->GetControlRotation().Yaw));
        if (Turned < 5.f) M4_FAIL(TEXT("a 60-count mouse move turned the camera only %.1f degrees"), Turned);
        St.Note = FString::Printf(TEXT("captured (viewport mode %d), a big mouse move turned %.0f degrees; "), VC ? int32(VC->GetMouseCaptureMode()) : -1, Turned);
        St.I = PC->Captures;
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Enter, IE_Pressed, 1.f));
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.1);
        if (!Menus(Test)->IsOpen(TEXT("start"))) M4_FAIL(TEXT("Enter did not open START"));
        if (PC->IsMouseCaptured() || !PC->bShowMouseCursor) M4_FAIL(TEXT("the mouse stays captured while the menu is open"));
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Escape, IE_Pressed, 1.f));
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        M4_WAIT(.1);
        if (Menus(Test)->AnyOpen()) M4_FAIL(TEXT("Esc did not close START"));
        if (!PC->IsMouseCaptured() || PC->bShowMouseCursor || PC->Captures <= St.I) M4_FAIL(TEXT("the mouse was not recaptured after the menu closed"));
        St.Note += FString::Printf(TEXT("released while START was open, recaptured after (captures %d, releases %d); "), PC->Captures, PC->Releases);
        // Alt-tab away and back: the application loses and regains activation.
        if (!FSlateApplication::IsInitialized()) M4_FAIL(TEXT("%sno Slate application to alt-tab"), *St.Note);
        St.I = PC->Captures;
        FSlateApplication::Get().OnApplicationActivationStateChanged().Broadcast(false);
        FSlateApplication::Get().OnApplicationActivationStateChanged().Broadcast(true);
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        M4_WAIT(.1);
        if (!PC->IsMouseCaptured() || PC->bShowMouseCursor || PC->Captures <= St.I) M4_FAIL(TEXT("%sthe mouse was not recaptured after alt-tab"), *St.Note);
        St.A = PC->GetControlRotation().Yaw;
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 60.f, 1));
        M4_NEXT();
    }
    M4_WAIT(.1);
    const float Turned = FMath::Abs(FMath::FindDeltaAngleDegrees(St.A, PC->GetControlRotation().Yaw));
    if (Turned < 5.f) M4_FAIL(TEXT("%safter alt-tab a mouse move turned only %.1f degrees"), *St.Note, Turned);
    M4_PASS(TEXT("%safter alt-tab recaptured and still turning (%.0f degrees)"), *St.Note, Turned);
}

// The minimap sits bottom-right, shows the map's own picture, and three tiles east moves its window three tiles.
GAME_TEST(Minimap_TilesScroll, 90)
{
    static FState St;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRHud* Hud = UPYRHud::Get(Test.World);
    APokemonYellowReimagineCharacter* R = Red(Test);
    Quiet(true, true);
    if (!Hud) M4_FAIL(TEXT("no HUD"));
    if (St.Step == 0)
    {
        const FYMap* M = D->Map(TEXT("PALLET_TOWN"));
        // An open row four tiles long.
        for (int32 Y = 2; Y < M->Height - 2 && St.P == FIntPoint::ZeroValue; ++Y)
            for (int32 X = 1; X < M->Width - 5 && St.P == FIntPoint::ZeroValue; ++X)
                if (Plain(*M, X, Y) && Plain(*M, X + 1, Y) && Plain(*M, X + 2, Y) && Plain(*M, X + 3, Y)) St.P = FIntPoint(X, Y);
        Party(Test, {{TEXT("pikachu"), 6}});
        Place(Test, TEXT("PALLET_TOWN"), St.P.X, St.P.Y, EYDir::Right);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.4);
        if (!Hud->IsShown()) M4_FAIL(TEXT("the minimap is hidden in Pallet Town"));
        if (Hud->GetAnchor() != FVector2D(1, 1)) M4_FAIL(TEXT("anchored at %s, not bottom-right"), *Hud->GetAnchor().ToString());
        if (!Hud->GetSize().Equals(FVector2D(240, 216), .5)) M4_FAIL(TEXT("size %s, not 160x144 at 1.5x"), *Hud->GetSize().ToString());
        if (Hud->GetImage() != TEXT("minimap/kanto_outdoor.png")) M4_FAIL(TEXT("outdoors it shows '%s'"), *Hud->GetImage());
        St.V = FVector(Hud->GetWindowTiles(), 0);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        if (!WalkToTile(Test, TEXT("PALLET_TOWN"), St.P.X + 3, St.P.Y, 3.f) && Test.Seconds - St.T0 < 4.0) return EGameTest::Running;
        M4_WAIT(.3);
        const FVector2D Moved = Hud->GetWindowTiles() - FVector2D(St.V.X, St.V.Y);
        if (FMath::Abs(Moved.X - 3.f) > .25f || FMath::Abs(Moved.Y) > .25f) M4_FAIL(TEXT("three tiles east moved the window (%.2f, %.2f) tiles"), Moved.X, Moved.Y);
        St.Note = FString::Printf(TEXT("bottom-right %gx%g px, %s, window moved (%.2f, %.2f) for three tiles east; "), Hud->GetSize().X, Hud->GetSize().Y, *Hud->GetImage(), Moved.X, Moved.Y);
        PYRScreens::OpenStart(Test.World);
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        M4_WAIT(.2);
        if (Hud->IsShown()) M4_FAIL(TEXT("the minimap shows over the START menu"));
        Place(Test, TEXT("REDS_HOUSE_1F"), 3, 4, EYDir::Down);
        M4_NEXT();
    }
    M4_WAIT(.4);
    const FString Want = TEXT("minimap/ringed/REDS_HOUSE_1F.png");   // the room ringed by its border block (m4r)
    const bool bHasPicture = FPaths::FileExists(FPaths::Combine(UYellowData::DefaultRoot(), Want));
    if (bHasPicture && Hud->GetImage() != Want) M4_FAIL(TEXT("indoors it shows '%s', not %s"), *Hud->GetImage(), *Want);
    M4_PASS(TEXT("%shidden under START; indoors %s"), *St.Note, *Hud->GetImage());
}

// The Town Map's cursor is on Pallet Town when Red is there.
GAME_TEST(TownMap_ShowsKanto, 60)
{
    static FState St;
    M4_READY();
    if (St.Step == 0)
    {
        Place(Test, TEXT("PALLET_TOWN"), 5, 6, EYDir::Down);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.3);
        PYRScreens::OpenTownMap(Test.World);
        M4_NEXT();
    }
    M4_WAIT(.1);
    TSharedPtr<FPYRScreen> S = Menus(Test)->Find(TEXT("town-map"));
    if (!S) M4_FAIL(TEXT("the Town Map did not open"));
    const FString Desc = S->Describe();
    if (Desc.StartsWith(TEXT("none"))) M4_FAIL(TEXT("no townmap/townmap.png: %s"), *Desc);
    // townmap.json's Pallet Town cell.
    FString Text;
    TSharedPtr<FJsonObject> Json;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(UYellowData::DefaultRoot(), TEXT("townmap/townmap.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json))
        M4_FAIL(TEXT("YellowData missing: townmap/townmap.json"));
    const TSharedPtr<FJsonObject>* Pallet = nullptr;
    if (!Json->TryGetObjectField(TEXT("PALLET_TOWN"), Pallet)) M4_FAIL(TEXT("townmap.json has no PALLET_TOWN"));
    const int32 X = (*Pallet)->GetIntegerField(TEXT("x")), Y = (*Pallet)->GetIntegerField(TEXT("y"));
    const FString Want = FString::Printf(TEXT("cursor %d,%d"), X * 8 + 4, Y * 8 + 4);
    if (!Desc.Contains(Want)) M4_FAIL(TEXT("Town Map shows '%s', want %s (cell %d,%d)"), *Desc, *Want, X, Y);
    M4_PASS(TEXT("%s (townmap.json PALLET_TOWN cell %d,%d)"), *Desc, X, Y);
}

// ------------------------------------------------------------------------------------------------ objects
// Every person, sign, item ball and hidden item stands within 0.1 m of its tile; one sign per map reads its text.
GAME_TEST(World_ObjectsInPlace, 300)
{
    static FState St;
    static TArray<TWeakObjectPtr<APYRSignpost>> ToRead;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    AYellowWorld* W = World(Test);
    UPYRField* F = Field(Test);
    UPYRMenus* Mn = Menus(Test);
    Quiet(true, true);
    if (St.Step == 0)
    {
        AfterPokedex(Test);
        int32 People = 0, Trainers = 0, Signs = 0, Balls = 0, Hidden = 0;
        auto Near = [W](const AActor* A, FIntPoint K) { return FVector::Dist2D(A->GetActorLocation(), W->KantoCenter(K)) <= 10.f; };
        for (const TWeakObjectPtr<APYRPerson>& P : F->People)
        {
            if (!P.IsValid()) continue;
            const FYMap* M = D->Map(P->Map);
            const FYNpc* N = M ? M->FindNpc(P->Id) : nullptr;
            if (!N) M4_FAIL(TEXT("%s:%s is not in its map file"), *P->Map.ToString(), *P->Id);
            if (P->Home != M->Origin + FIntPoint(N->X, N->Y)) M4_FAIL(TEXT("%s:%s's home is not its tile"), *P->Map.ToString(), *P->Id);
            if (!P->IsMoving() && P->Tile == P->Home && !Near(P.Get(), P->Home))
                M4_FAIL(TEXT("%s:%s stands %.0f cm off its tile (%d,%d)"), *P->Map.ToString(), *P->Id, FVector::Dist2D(P->GetActorLocation(), W->KantoCenter(P->Home)), N->X, N->Y);
            ++People;
            Trainers += P->IsTrainer() ? 1 : 0;
        }
        int32 WantPeople = 0, WantSigns = 0, WantBalls = 0;
        for (const FName& Id : D->MapOrder) if (const FYMap* M = D->Map(Id)) { WantPeople += M->Npcs.Num(); WantSigns += M->Signs.Num(); WantBalls += M->Items.Num(); }
        if (People != WantPeople) M4_FAIL(TEXT("%d people spawned for %d in the maps"), People, WantPeople);
        for (const TWeakObjectPtr<APYRSignpost>& S : F->Signs)
        {
            if (!S.IsValid()) continue;
            if (!Near(S.Get(), S->Tile)) M4_FAIL(TEXT("a sign in %s is %.0f cm off its tile"), *S->Map.ToString(), FVector::Dist2D(S->GetActorLocation(), W->KantoCenter(S->Tile)));
            ++Signs;
        }
        for (const TWeakObjectPtr<APYRItemBall>& B : F->Balls)
        {
            if (!B.IsValid()) continue;
            if (!Near(B.Get(), B->Tile)) M4_FAIL(TEXT("the %s ball in %s is off its tile"), *B->Item.ToString(), *B->Map.ToString());
            ++Balls;
        }
        for (const FName& Id : D->MapOrder)
            if (const FYMap* M = D->Map(Id))
                for (const FYItemBall& H : M->Hidden)
                {
                    FName HM;
                    int32 HI = 0;
                    if (!F->HiddenAt(M->Origin + FIntPoint(H.X, H.Y), HM, HI) || HM != Id) M4_FAIL(TEXT("hidden %s at %s (%d,%d) is not findable"), *H.Item.ToString(), *Id.ToString(), H.X, H.Y);
                    ++Hidden;
                }
        if (Signs != WantSigns || Balls != WantBalls) M4_FAIL(TEXT("%d signs and %d balls spawned for %d and %d"), Signs, Balls, WantSigns, WantBalls);
        St.Note = FString::Printf(TEXT("%d people (%d trainers), %d signs, %d item balls, %d hidden items on their tiles; "), People, Trainers, Signs, Balls, Hidden);
        // One sign per map to read.
        TSet<FName> Maps;
        for (const TWeakObjectPtr<APYRSignpost>& S : F->Signs) if (S.IsValid() && !Maps.Contains(S->Map)) { Maps.Add(S->Map); ToRead.Add(S); }
        St.Step = 1;
        St.I = 0;
        return EGameTest::Running;
    }
    if (St.I >= ToRead.Num()) M4_PASS(TEXT("%sread %d signs (one per map), %d from the front"), *St.Note, St.N, ToRead.Num());
    APYRSignpost* Sign = ToRead[St.I].Get();
    const FYMap* M = Sign ? D->Map(Sign->Map) : nullptr;
    if (!Sign || !M) { ++St.I; return EGameTest::Running; }
    if (St.Step == 1)
    {
        // Stand in front of it (below first: wall plates are read facing up).
        const FIntPoint L = Sign->Tile - M->Origin;
        bool bPlaced = false;
        for (const EYDir Dir : {EYDir::Up, EYDir::Left, EYDir::Right, EYDir::Down})
        {
            const FIntPoint From = L - YDir::Delta(Dir);
            if (!Walkable(*M, From.X, From.Y)) continue;
            Place(Test, M->Id, From.X, From.Y, Dir);
            bPlaced = true;
            break;
        }
        if (!bPlaced) { ++St.I; return EGameTest::Running; }
        Mn->TextLog.Reset();
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.15);
        F->Interact();
        M4_NEXT();
    }
    M4_WAIT(.1);
    const FString Want = FirstLine(PYRGb::Fill(Test.World, D->TextFor(Sign->TextId)));
    if (!Want.IsEmpty() && !SaidContains(Test, Want))
        M4_FAIL(TEXT("the sign at %s (%d,%d) said '%s', want '%s' (%s)"), *M->Id.ToString(), Sign->Tile.X - M->Origin.X, Sign->Tile.Y - M->Origin.Y, *Said(Test), *Want, *Sign->TextId);
    ++St.N;
    Mn->CloseAll();
    ++St.I;
    St.Step = 1;
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ music
// Pallet Town plays pallet_town, Route 1 switches to its own tune, and an item jingle plays over it and hands back.
GAME_TEST(Music_GbTunes, 90)
{
    static FState St;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UGbMusicPlayer* Music = UGbMusicPlayer::Get(Test.World);
    UPYRField* F = Field(Test);
    Quiet(true, true);
    if (!Music) M4_FAIL(TEXT("no music player"));
    if (!Music->HasYellowMusic()) M4_FAIL(TEXT("YellowMusic missing: %s (needs the gbmusic worker's output); %s"), *UYellowData::MusicRoot(), *Music->LastError);
    const FYMap* Pallet = D->Map(TEXT("PALLET_TOWN"));
    const FYMap* Route1 = D->Map(TEXT("ROUTE_1"));
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 6}});
        Flags(Test, {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER")});
        FIntPoint At;
        EYDir Dir;
        if (!FindCrossing(World(Test), *Pallet, TEXT("ROUTE_1"), At, Dir)) M4_FAIL(TEXT("no way north out of Pallet Town"));
        St.P = At;
        Place(Test, TEXT("PALLET_TOWN"), At.X, At.Y + 2, EYDir::Up);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(1.0);
        const FName Want = FName(*Pallet->Music.ToLower());
        if (Music->GetSong() != Want) M4_FAIL(TEXT("Pallet Town plays '%s', not '%s'"), *Music->GetSong().ToString(), *Want.ToString());
        St.Note = FString::Printf(TEXT("Pallet Town: %s; "), *Want.ToString());
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        WalkToTile(Test, TEXT("PALLET_TOWN"), St.P.X, St.P.Y - 3, 5.f);
        if (F->CurrentMap != TEXT("ROUTE_1")) { if (Test.Seconds - St.T0 > 6) M4_FAIL(TEXT("did not reach Route 1")); return EGameTest::Running; }
        StopWalking(Test);
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        M4_WAIT(.7);
        const FName Want = FName(*Route1->Music.ToLower());
        if (Music->GetSong() != Want) M4_FAIL(TEXT("Route 1 plays '%s', not '%s'"), *Music->GetSong().ToString(), *Want.ToString());
        St.Note += FString::Printf(TEXT("Route 1: %s; "), *Want.ToString());
        St.I = Music->Resumes;
        F->GiveItem(UPYRTrainerState::Potion, 1, TEXT("found"));
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        M4_WAIT(.1);
        if (Music->GetJingle() != TEXT("get_item_1")) M4_FAIL(TEXT("finding an item played jingle '%s'"), *Music->GetJingle().ToString());
        St.Note += TEXT("get_item_1 over it; ");
        M4_NEXT();
    }
    Advance(Test);
    if (Music->IsJinglePlaying()) { if (Test.Seconds - St.T0 > 12) M4_FAIL(TEXT("the jingle never ended")); return EGameTest::Running; }
    if (Music->Resumes <= St.I || Music->GetSong() != FName(*Route1->Music.ToLower())) M4_FAIL(TEXT("after the jingle the song is '%s' (resumes %d)"), *Music->GetSong().ToString(), Music->Resumes);
    M4_PASS(TEXT("%sthen %s resumed"), *St.Note, *Music->GetSong().ToString());
}

// ------------------------------------------------------------------------------------------------ m4q names
// The m4q task's names for these checks (the same tests).
static struct FM4QWorldNames
{
    FM4QWorldNames() { GameTest::Register(TEXT("Roaming_LedgeHopDownNotUp"), &GameTest_Roam_LedgeOneWay, 90); }
} GM4QWorldNames;
