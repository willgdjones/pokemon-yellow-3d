// m4r: the follow-up fix round's regression tests .
// - The minimap's people (designer-001): MinimapShowsPeopleSprites, MinimapShowsPikachuAndRed, MinimapHidesHiddenPeople.
//   Like the Game Boy screen, the minimap draws every shown person, trainer, item ball, Pikachu and Red as their own
//   Yellow overworld sprite, at their live tile, facing their way, and leaves out whoever the story hides.
// - Q065TextLine (text-024): no hand-written line on the warps past the range.
// - CeruleanEastScenery (warp-016): Route 9 past Cerulean's east edge, in the world and on the minimap.
#include "Tests/QAKit.h"

#include "Hud/PYRHud.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Partner/PYRPikachuFollower.h"
#include "PokemonYellowReimagineCharacter.h"

using namespace M4;

namespace
{
int32 FacingIndex(EYDir D) { return D == EYDir::Up ? 1 : D == EYDir::Left ? 2 : D == EYDir::Right ? 3 : 0; }

const FPYRMinimapMarker* MarkerFor(const UPYRHud* Hud, const AActor* A)
{
    for (const FPYRMinimapMarker& Mk : Hud->GetMarkers()) if (Mk.Actor.Get() == A) return &Mk;
    return nullptr;
}

const FPYRMinimapMarker* RedMarker(const UPYRHud* Hud)
{
    for (const FPYRMinimapMarker& Mk : Hud->GetMarkers()) if (Mk.bRed) return &Mk;
    return nullptr;
}

// Where an actor should sit on the minimap, in picture tiles: Red's marker plus the world offset from Red.
FVector2D Expected(const UPYRHud* Hud, const AActor* Red, const AActor* A)
{
    const FPYRMinimapMarker* R = RedMarker(Hud);
    const FVector D = (A->GetActorLocation() - Red->GetActorLocation()) / YTile::Size;
    return (R ? R->Tile : FVector2D::ZeroVector) + FVector2D(D.X, D.Y);
}

// Inside the minimap's 10 x 9 tile window (the actor's centre).
bool InWindow(const UPYRHud* Hud, const FVector2D& Pos)
{
    const FVector2D W = Hud->GetWindowTiles();
    return Pos.X > W.X && Pos.Y > W.Y && Pos.X < W.X + UPYRHud::ScreenW / 16.f && Pos.Y < W.Y + UPYRHud::ScreenH / 16.f;
}

// A tile Red can be placed on within Reach tiles of a Kanto tile (walkable, on a map, no warp, nobody there).
bool SpotNear(FGameTestContext& Test, FIntPoint Target, int32 Reach, FName& OutMap, FIntPoint& OutLocal)
{
    const AYellowWorld* W = World(Test);
    const UPYRField* F = Field(Test);
    for (int32 R = 1; R <= Reach; ++R)
        for (int32 Dy = -R; Dy <= R; ++Dy)
            for (int32 Dx = -R; Dx <= R; ++Dx)
            {
                if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != R) continue;
                const FIntPoint K = Target + FIntPoint(Dx, Dy);
                const FYMap* M = W->MapAtKanto(K);
                if (!M) continue;
                const FIntPoint L = K - M->Origin;
                if (YTile::IsSolid(M->Tile(L.X, L.Y)) || M->WarpAt(L.X, L.Y) != INDEX_NONE || F->IsOccupied(K)) continue;
                OutMap = M->Id;
                OutLocal = L;
                return true;
            }
    return false;
}

// Every shown person and item ball in the window has its own marker with its own sprite at its live tile, facing its
// way; every marker is someone shown. Fills Why on a fault; Count gets the people checked.
bool CheckWindow(FGameTestContext& Test, FString& Why, int32& Count, TSet<FName>& Sprites)
{
    const UPYRHud* Hud = UPYRHud::Get(Test.World);
    const AYellowWorld* W = World(Test);
    const UPYRField* F = Field(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    if (!Hud->IsShown()) { Why = TEXT("the minimap is hidden"); return false; }
    if (Hud->GetPeopleImage().IsEmpty() || Hud->AtlasRowCount() == 0) { Why = TEXT("no people atlas"); return false; }
    const FYMap* Here = F->GetMap();
    int32 Visible = 0, Drawn = 0;
    for (const TWeakObjectPtr<APYRPerson>& PW : F->People)
    {
        const APYRPerson* P = PW.Get();
        if (!P || !P->IsShown()) continue;
        const FYMap* PM = Data(Test)->Map(P->Map);
        if (!(P->Map == Here->Id || (Here->bOutdoor && PM && PM->bOutdoor))) continue;
        const FVector2D Want = Expected(Hud, R, P);
        if (!InWindow(Hud, Want)) continue;
        ++Visible;
        const FPYRMinimapMarker* Mk = MarkerFor(Hud, P);
        if (!Mk) { Why = FString::Printf(TEXT("%s %s (%s) at %s is in the window but not on the minimap"), *P->Map.ToString(), *P->Id, *P->Sprite.ToString(), *QA::TileText(Test, P->Tile)); return false; }
        if (Mk->Sprite != P->Sprite || Mk->Row == INDEX_NONE || Mk->Row != Hud->AtlasRow(P->Sprite))
        { Why = FString::Printf(TEXT("%s is drawn as %s row %d, not its own %s"), *P->Id, *Mk->Sprite.ToString(), Mk->Row, *P->Sprite.ToString()); return false; }
        const FIntPoint Live = W->ToKanto(P->GetActorLocation());
        if (Mk->Kanto != Live || FVector2D::Distance(Mk->Tile, Want) > .3f)
        { Why = FString::Printf(TEXT("%s is drawn at %s (%.2f off), but stands on %s"), *P->Id, *QA::TileText(Test, Mk->Kanto), FVector2D::Distance(Mk->Tile, Want), *QA::TileText(Test, Live)); return false; }
        if (Mk->Frame / 2 != FacingIndex(P->Facing))
        { Why = FString::Printf(TEXT("%s faces %s but its sprite faces frame %d"), *P->Id, YDir::Name(P->Facing), Mk->Frame); return false; }
        Sprites.Add(P->Sprite);
        ++Drawn;
    }
    for (const TWeakObjectPtr<APYRItemBall>& B : F->Balls)
    {
        if (!B.IsValid()) continue;
        const FYMap* BM = Data(Test)->Map(B->Map);
        if (!(B->Map == Here->Id || (Here->bOutdoor && BM && BM->bOutdoor)) || !InWindow(Hud, Expected(Hud, R, B.Get()))) continue;
        ++Visible;
        const FPYRMinimapMarker* Mk = MarkerFor(Hud, B.Get());
        if (!Mk || Mk->Sprite != TEXT("SPRITE_POKE_BALL") || Mk->Kanto != B->Tile) { Why = FString::Printf(TEXT("the item ball at %s is not drawn as a Poke Ball there"), *QA::TileText(Test, B->Tile)); return false; }
        ++Drawn;
    }
    // Every marker is someone shown (nobody hidden by the story, nothing twice).
    TSet<const AActor*> Seen;
    for (const FPYRMinimapMarker& Mk : Hud->GetMarkers())
    {
        const AActor* A = Mk.Actor.Get();
        if (!A) { Why = TEXT("a marker for an actor that is gone"); return false; }
        if (Seen.Contains(A)) { Why = FString::Printf(TEXT("%s is drawn twice"), *GetNameSafe(A)); return false; }
        Seen.Add(A);
        if (const APYRPerson* P = Cast<APYRPerson>(A))
            if (!P->IsShown()) { Why = FString::Printf(TEXT("%s is hidden but drawn"), *P->Id); return false; }
    }
    Count += Drawn;
    return Drawn == Visible;
}
}

// In Pallet Town, Viridian, Pewter and Cerulean: standing beside each shown person in turn, everyone in the window is
// on the minimap as their own Yellow sprite at their tile, facing their way; then one walker is moved and its sprite
// follows it to the new tile (the live tile, not the spawn tile).
GAME_TEST(MinimapShowsPeopleSprites, 240)
{
    static FState St;
    static TArray<TWeakObjectPtr<APYRPerson>> Cases;
    static TSet<FName> Sprites;
    static int32 Checked = 0;
    M4_READY();
    UPYRField* F = Field(Test);
    UPYRHud* Hud = UPYRHud::Get(Test.World);
    if (!Hud) M4_FAIL(TEXT("no HUD"));
    static const FName Towns[] = {TEXT("PALLET_TOWN"), TEXT("VIRIDIAN_CITY"), TEXT("PEWTER_CITY"), TEXT("CERULEAN_CITY")};
    if (St.Step == 0)
    {
        Quiet(true, true);
        AfterPokedex(Test);
        PYRScripts::Refresh(Test.World);
        for (const TWeakObjectPtr<APYRPerson>& P : F->People)
            if (P.IsValid())
            {
                P->bFrozen = true;   // no wandering while each is checked (the live tile is checked at the end)
                for (const FName& T : Towns) if (P->Map == T && P->IsShown()) Cases.Add(P);
            }
        if (Cases.Num() < 20) M4_FAIL(TEXT("only %d people shown in the four towns"), Cases.Num());
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // Place Red beside the next person.
        if (St.I >= Cases.Num()) { St.Step = 3; St.T0 = Test.Seconds; return EGameTest::Running; }
        APYRPerson* P = Cases[St.I].Get();
        FName Map;
        FIntPoint L;
        if (!P || !P->IsShown() || !SpotNear(Test, P->Tile, 3, Map, L)) { ++St.I; return EGameTest::Running; }
        Place(Test, Map, L.X, L.Y, EYDir::Down);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.35);
        APYRPerson* P = Cases[St.I].Get();
        if (P && !MarkerFor(Hud, P)) M4_FAIL(TEXT("%s %s (%s) beside Red at %s is not on the minimap"), *P->Map.ToString(), *P->Id, *P->Sprite.ToString(), *QA::TileText(Test, P->Tile));
        FString Why;
        if (!CheckWindow(Test, Why, Checked, Sprites)) M4_FAIL(TEXT("beside %s: %s"), P ? *P->Id : TEXT("?"), *Why);
        ++St.I;
        St.Step = 1;
        return EGameTest::Running;
    }
    if (St.Step == 3)
    {
        // The live tile: a Viridian walker moved two tiles is drawn where it now stands, not where it spawned.
        APYRPerson* P = Person(Test, TEXT("VIRIDIAN_CITY"), TEXT("VIRIDIANCITY_YOUNGSTER1"));
        if (!P) for (const TWeakObjectPtr<APYRPerson>& C : Cases) if (C.IsValid() && C->bWalker && C->Map == TEXT("VIRIDIAN_CITY")) { P = C.Get(); break; }
        if (!P) M4_FAIL(TEXT("no Viridian walker"));
        St.P = P->Tile;
        FIntPoint To = P->Tile;
        for (const FIntPoint D : {FIntPoint(2, 0), FIntPoint(-2, 0), FIntPoint(0, 2), FIntPoint(0, -2)})
            if (F->CanStep(P->Tile + D, P) && F->CanStep(P->Tile + D / 2, P)) { To = P->Tile + D; break; }
        if (To == P->Tile) M4_FAIL(TEXT("%s has no free tile two away"), *P->Id);
        FName Map;
        FIntPoint L;
        if (!SpotNear(Test, P->Tile, 3, Map, L)) M4_FAIL(TEXT("nowhere to stand near %s"), *P->Id);
        Place(Test, Map, L.X, L.Y, EYDir::Down);
        P->Teleport(To, EYDir::Left);
        St.Note = P->Id;
        M4_NEXT();
    }
    M4_WAIT(.35);
    APYRPerson* P = Person(Test, TEXT("VIRIDIAN_CITY"), *St.Note);
    const FPYRMinimapMarker* Mk = P ? MarkerFor(Hud, P) : nullptr;
    if (!Mk) M4_FAIL(TEXT("%s, moved, is not on the minimap"), *St.Note);
    if (Mk->Kanto != P->Tile || Mk->Kanto == St.P) M4_FAIL(TEXT("%s moved from %s to %s but is drawn at %s"), *St.Note, *QA::TileText(Test, St.P), *QA::TileText(Test, P->Tile), *QA::TileText(Test, Mk->Kanto));
    if (Mk->Frame / 2 != FacingIndex(EYDir::Left)) M4_FAIL(TEXT("%s turned left but its sprite shows frame %d"), *St.Note, Mk->Frame);
    FString Why;
    if (!CheckWindow(Test, Why, Checked, Sprites)) M4_FAIL(TEXT("after the move: %s"), *Why);
    if (Sprites.Num() < 10) M4_FAIL(TEXT("only %d different sprites drawn"), Sprites.Num());
    M4_PASS(TEXT("%d people in the four towns, %d sprite checks, %d different Yellow sprites (%s...); %s moved %s -> %s and its sprite followed"), Cases.Num(), Checked,
        Sprites.Num(), *FString::JoinBy(Sprites.Array(), TEXT(","), [](const FName& N) { return N.ToString(); }).Left(120), *St.Note, *QA::TileText(Test, St.P),
        *QA::TileText(Test, P->Tile));
}

// Pikachu and Red each have their own sprite (SPRITE_PIKACHU and SPRITE_RED, different rows of the atlas); walking
// three tiles east, both move on the minimap, face east and show their step frames.
GAME_TEST(MinimapShowsPikachuAndRed, 120)
{
    static FState St;
    static TSet<int32> RedFrames, PikaFrames;
    static FVector2D RedFrom, PikaFrom;
    M4_READY();
    UPYRHud* Hud = UPYRHud::Get(Test.World);
    APYRPikachuFollower* Pika = APYRPikachuFollower::Find(Test.World);
    if (!Hud || !Pika) M4_FAIL(TEXT("no HUD or no Pikachu"));
    const FYMap* M = Data(Test)->Map(TEXT("PALLET_TOWN"));
    if (St.Step == 0)
    {
        Quiet(true, true);
        AfterPokedex(Test);
        // An open row five tiles long in Pallet Town.
        for (int32 Y = 6; Y < M->Height - 2 && St.P == FIntPoint::ZeroValue; ++Y)
            for (int32 X = 2; X < M->Width - 6 && St.P == FIntPoint::ZeroValue; ++X)
            {
                bool bOpen = true;
                for (int32 I = -1; I <= 4; ++I) bOpen &= !YTile::IsSolid(M->Tile(X + I, Y)) && M->WarpAt(X + I, Y) == INDEX_NONE && !Field(Test)->IsOccupied(M->Origin + FIntPoint(X + I, Y));
                if (bOpen) St.P = FIntPoint(X, Y);
            }
        Place(Test, TEXT("PALLET_TOWN"), St.P.X, St.P.Y, EYDir::Right);
        Pika->SnapBehindRed();
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.5);
        const FPYRMinimapMarker* R = RedMarker(Hud);
        const FPYRMinimapMarker* K = MarkerFor(Hud, Pika);
        if (!R || R->Sprite != TEXT("SPRITE_RED") || R->Row == INDEX_NONE) M4_FAIL(TEXT("Red is not drawn as SPRITE_RED"));
        if (!K) M4_FAIL(TEXT("Pikachu (on %s) is not on the minimap"), *QA::TileText(Test, Pika->Tile()));
        if (K->Sprite != TEXT("SPRITE_PIKACHU") || K->Row == INDEX_NONE || K->Row == R->Row) M4_FAIL(TEXT("Pikachu is drawn as %s row %d (Red row %d)"), *K->Sprite.ToString(), K->Row, R->Row);
        if (K->Kanto != Pika->Tile()) M4_FAIL(TEXT("Pikachu is drawn at %s but stands on %s"), *QA::TileText(Test, K->Kanto), *QA::TileText(Test, Pika->Tile()));
        RedFrom = Hud->GetWindowTiles();
        PikaFrom = K->Tile;
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        const bool bThere = WalkToTile(Test, TEXT("PALLET_TOWN"), St.P.X + 3, St.P.Y, 6.f);
        if (const FPYRMinimapMarker* R = RedMarker(Hud)) RedFrames.Add(R->Frame);
        if (const FPYRMinimapMarker* K = MarkerFor(Hud, Pika)) PikaFrames.Add(K->Frame);
        if (!bThere && Test.Seconds - St.T0 < 5.0) return EGameTest::Running;
        StopWalking(Test);
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        if (const FPYRMinimapMarker* K = MarkerFor(Hud, Pika)) PikaFrames.Add(K->Frame);
        M4_WAIT(.8);
        const FPYRMinimapMarker* K = MarkerFor(Hud, Pika);
        if (!K) M4_FAIL(TEXT("Pikachu left the minimap after the walk"));
        const float RedMoved = (Hud->GetWindowTiles() - RedFrom).X;
        const float PikaMoved = (K->Tile - PikaFrom).X;
        if (FMath::Abs(RedMoved - 3.f) > .3f) M4_FAIL(TEXT("Red walked three tiles east but moved %.2f on the minimap"), RedMoved);
        if (PikaMoved < 1.7f) M4_FAIL(TEXT("Pikachu followed but moved only %.2f tiles on the minimap"), PikaMoved);
        if (K->Kanto != Pika->Tile()) M4_FAIL(TEXT("Pikachu is drawn at %s but stands on %s"), *QA::TileText(Test, K->Kanto), *QA::TileText(Test, Pika->Tile()));
        // East = facing 3: frames 6 (standing) and 7 (the step).
        if (!RedFrames.Contains(7) || !RedFrames.Contains(6)) M4_FAIL(TEXT("Red's sprite frames while walking east: %s (want 6 and 7)"), *FString::JoinBy(RedFrames, TEXT(","), [](int32 V) { return FString::FromInt(V); }));
        if (!PikaFrames.Contains(7)) M4_FAIL(TEXT("Pikachu's sprite never stepped facing east: %s"), *FString::JoinBy(PikaFrames, TEXT(","), [](int32 V) { return FString::FromInt(V); }));
        M4_PASS(TEXT("Red (SPRITE_RED) and Pikachu (SPRITE_PIKACHU) drawn; three tiles east moved Red's window %.2f and Pikachu %.2f; frames Red %s, Pikachu %s"), RedMoved, PikaMoved,
            *FString::JoinBy(RedFrames, TEXT(","), [](int32 V) { return FString::FromInt(V); }), *FString::JoinBy(PikaFrames, TEXT(","), [](int32 V) { return FString::FromInt(V); }));
    }
    return EGameTest::Running;
}

// People hidden by story flags are not drawn, and appear when the story shows them: the Pewter boy at the east exit
// goes once Brock is beaten (EVENT_BEAT_BROCK); in Viridian the sleepy old man lying across the road gives way to the
// standing one when Red has the Pokedex (EVENT_GOT_POKEDEX).
GAME_TEST(MinimapHidesHiddenPeople, 120)
{
    static FState St;
    M4_READY();
    UPYRHud* Hud = UPYRHud::Get(Test.World);
    if (!Hud) M4_FAIL(TEXT("no HUD"));
    APYRPerson* Boy = Person(Test, TEXT("PEWTER_CITY"), TEXT("PEWTERCITY_YOUNGSTER"));
    APYRPerson* Sleepy = Person(Test, TEXT("VIRIDIAN_CITY"), TEXT("VIRIDIANCITY_OLD_MAN_SLEEPY"));
    APYRPerson* Standing = Person(Test, TEXT("VIRIDIAN_CITY"), TEXT("VIRIDIANCITY_OLD_MAN2"));
    if (!Boy || !Sleepy || !Standing) M4_FAIL(TEXT("missing the Pewter boy or the Viridian old men"));
    auto PlaceNear = [&](APYRPerson* P)
    {
        FName Map;
        FIntPoint L;
        return SpotNear(Test, P->Tile, 3, Map, L) && Place(Test, Map, L.X, L.Y, EYDir::Down);
    };
    if (St.Step == 0)
    {
        Quiet(true, true);
        Party(Test, {{TEXT("pikachu"), 8}});
        Flags(Test, {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL")});
        Boy->bFrozen = Sleepy->bFrozen = Standing->bFrozen = true;
        if (!PlaceNear(Sleepy)) M4_FAIL(TEXT("nowhere to stand near the old man"));
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.4);
        if (!Sleepy->IsShown() || Standing->IsShown()) M4_FAIL(TEXT("before the Pokedex: sleepy shown %d, standing shown %d"), Sleepy->IsShown(), Standing->IsShown());
        if (!MarkerFor(Hud, Sleepy)) M4_FAIL(TEXT("the old man lying across the road is not on the minimap"));
        if (MarkerFor(Hud, Standing)) M4_FAIL(TEXT("the standing old man is hidden but drawn"));
        Flags(Test, {TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")});
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.4);
        if (Sleepy->IsShown() || !Standing->IsShown()) M4_FAIL(TEXT("after the Pokedex: sleepy shown %d, standing shown %d"), Sleepy->IsShown(), Standing->IsShown());
        if (MarkerFor(Hud, Sleepy)) M4_FAIL(TEXT("the sleepy old man is hidden after the Pokedex but still drawn"));
        if (!InWindow(Hud, Expected(Hud, Red(Test), Standing))) M4_FAIL(TEXT("the standing old man is out of the window"));
        if (!MarkerFor(Hud, Standing)) M4_FAIL(TEXT("the standing old man, shown after the Pokedex, is not drawn"));
        if (!PlaceNear(Boy)) M4_FAIL(TEXT("nowhere to stand near the Pewter boy"));
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        M4_WAIT(.4);
        if (!Boy->IsShown() || !MarkerFor(Hud, Boy)) M4_FAIL(TEXT("the Pewter boy before Brock: shown %d, drawn %d"), Boy->IsShown(), MarkerFor(Hud, Boy) != nullptr);
        Flags(Test, {TEXT("EVENT_BEAT_BROCK")});
        M4_NEXT();
    }
    M4_WAIT(.4);
    if (Boy->IsShown()) M4_FAIL(TEXT("the Pewter boy is still shown after Brock"));
    if (MarkerFor(Hud, Boy)) M4_FAIL(TEXT("the Pewter boy is hidden after Brock but still on the minimap"));
    int32 Hidden = 0;
    for (const TWeakObjectPtr<APYRPerson>& P : Field(Test)->People) Hidden += P.IsValid() && !P->IsShown() && MarkerFor(Hud, P.Get()) ? 1 : 0;
    if (Hidden) M4_FAIL(TEXT("%d hidden people drawn"), Hidden);
    M4_PASS(TEXT("the sleepy old man drawn until the Pokedex, then the standing one; the Pewter boy drawn until Brock is beaten, then gone"));
}

// text-024 (the planner's Q-065): Kanto/PYRField.cpp said "It's too dark and deep to go on." on Diglett's Cave's ladder,
// a line pokeyellow doesn't have. Every warp past the range now says only Yellow's words, or nothing where Yellow has
// none: Route 23's guards their badge line, Cerulean Cave the man at its door; Diglett's Cave (its Route 2 house is
// behind Cut trees this early) stops Red silently. Walking onto the ladder: Red is stopped, nothing is said, and he
// steps back off it.
GAME_TEST(Q065TextLine, 120)
{
    static FState St;
    static int32 Blocked = 0;
    static bool bSaid = false;
    M4_READY();
    UPYRField* F = Field(Test);
    const FYellowDataSet* D = Data(Test);
    const FYMap* M = D->Map(TEXT("DIGLETTS_CAVE_ROUTE_2"));
    if (!M) M4_FAIL(TEXT("no DIGLETTS_CAVE_ROUTE_2"));
    const int32 Ladder = M->WarpAt(4, 4);
    if (St.Step == 0)
    {
        if (Ladder == INDEX_NONE) M4_FAIL(TEXT("no ladder warp at 4,4"));
        AfterPokedex(Test);
        Quiet(true, true);
        // Each warp past the range: its words are Yellow's (a text.json line) or none.
        const TPair<const TCHAR*, const TCHAR*> Want[] = {{TEXT("CERULEAN_CAVE_1F"), TEXT("CeruleanCitySuperNerd3Text")}, {TEXT("ROUTE_23"), TEXT("_Route23YouDontHaveTheBadgeYetText")},
            {TEXT("DIGLETTS_CAVE"), nullptr}};
        for (const FName& Id : D->MapOrder)
        {
            const FYMap* Mp = D->Map(Id);
            for (int32 I = 0; Mp && I < Mp->Warps.Num(); ++I)
            {
                if (!F->IsOutOfRange(Id, I)) continue;
                const FString Line = F->BlockedWarpText(Id, I);
                if (Line.Contains(TEXT("too dark"))) M4_FAIL(TEXT("%s warp %d still says '%s'"), *Id.ToString(), I, *Line);
                bool bKnown = false;
                for (const auto& P : Want)
                {
                    if (Mp->Warps[I].ToMap != FName(P.Key)) continue;
                    bKnown = true;
                    if (!P.Value && !Line.IsEmpty()) M4_FAIL(TEXT("%s warp %d to %s says '%s'; Yellow has no line"), *Id.ToString(), I, P.Key, *Line);
                    if (P.Value && !Line.StartsWith(D->TextFor(P.Value).Left(16))) M4_FAIL(TEXT("%s warp %d to %s says '%s', not Yellow's %s"), *Id.ToString(), I, P.Key, *Line.Left(60), P.Value);
                }
                if (!bKnown) M4_FAIL(TEXT("%s warp %d leads past the range to %s, unchecked"), *Id.ToString(), I, *Mp->Warps[I].ToMap.ToString());
                ++St.N;
            }
        }
        const FString Cave = F->BlockedWarpText(TEXT("CERULEAN_CITY"), D->Map(TEXT("CERULEAN_CITY"))->WarpAt(4, 11));
        if (Cave != D->TextFor(TEXT("CeruleanCitySuperNerd3Text"))) M4_FAIL(TEXT("Cerulean Cave says '%s', not Yellow's CeruleanCitySuperNerd3Text"), *Cave.Left(80));
        Place(Test, TEXT("DIGLETTS_CAVE_ROUTE_2"), 4, 5, EYDir::Up);
        Blocked = F->WarpsBlocked;
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.4);
        bSaid |= Menus(Test)->AnyOpen();
        if (F->WarpsBlocked == Blocked)
        {
            if (F->CurrentMap != TEXT("DIGLETTS_CAVE_ROUTE_2")) M4_FAIL(TEXT("the ladder took Red to %s"), *F->CurrentMap.ToString());
            WalkToTile(Test, TEXT("DIGLETTS_CAVE_ROUTE_2"), 4, 4, 8.f);
            if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("walking onto the ladder neither warped nor stopped Red"));
            return EGameTest::Running;
        }
        StopWalking(Test);
        M4_NEXT();
    }
    bSaid |= Menus(Test)->AnyOpen();
    if (F->CurrentTile == FIntPoint(4, 4) && Test.Seconds - St.T0 < 6) return EGameTest::Running;
    M4_WAIT(1.0);
    if (bSaid) M4_FAIL(TEXT("a text box opened on Diglett's Cave's ladder: '%s'"), *Menus(Test)->ScreenText().Left(80));
    if (F->CurrentMap != TEXT("DIGLETTS_CAVE_ROUTE_2") || F->CurrentTile == FIntPoint(4, 4)) M4_FAIL(TEXT("Red ended at %s %d,%d"), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y);
    if (F->IsLocked()) M4_FAIL(TEXT("Red is still locked (%s)"), *F->LockText());
    M4_PASS(TEXT("%d warps past the range say only Yellow's words (Cerulean Cave: CeruleanCitySuperNerd3Text) or none; Diglett's Cave's ladder stops Red without a word and he steps back to %d,%d"),
        St.N, F->CurrentTile.X, F->CurrentTile.Y);
}

// warp-016 (the planner's Q-063): east of Cerulean the world shows Route 9 from Yellow's map data (scenery.json, at
// pokeyellow's connection offset): each tile past the seam carries Route 9's own look and is drawn (trees as trees, rock
// as rock, ground as ground), never a void or a filler. Walking east to the edge, Red stops on Cerulean's last column and
// stays in Cerulean (the collision stays closed at the seam, the designer's 30 Sep decision; Yellow's own block on this
// road is a Cut tree a few tiles in, which Red can't pass this early either). The minimap there shows Route 9's tiles
// past the edge too (minimap/kanto_outdoor.png), as the Game Boy screen does, not a blank.
GAME_TEST(CeruleanEastScenery, 120)
{
    static FState St;
    static int32 MaxX = MIN_int32;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    AYellowWorld* W = World(Test);
    UPYRField* F = Field(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    const FYMap* C = D->Map(TEXT("CERULEAN_CITY"));
    const FYMap* R9 = nullptr;
    for (const FYMap& S : D->Scenery) if (S.Id == TEXT("ROUTE_9")) R9 = &S;
    if (!C || !R9) M4_FAIL(TEXT("no Cerulean or no Route 9 scenery"));
    if (St.Step == 0)
    {
        AfterPokedex(Test);
        Quiet(true, true);
        // Route 9's tiles in the band past the seam: Yellow's look, from Route 9, drawn.
        if (R9->Origin.X != C->Origin.X + C->Width || R9->Origin.Y != C->Origin.Y + C->Conn[int32(EYDir::Right)].Offset)
            M4_FAIL(TEXT("Route 9 at %s, not at Cerulean's east connection (offset %d)"), *R9->Origin.ToString(), C->Conn[int32(EYDir::Right)].Offset);
        const TMap<FIntPoint, uint8> Drawn = QA::DrawnOffMap(Test, 8);
        int32 Trees = 0, Rock = 0, Ground = 0;
        for (int32 Y = 0; Y < R9->Height; ++Y)
            for (int32 X = 0; X < 6; ++X)
            {
                const FIntPoint K = R9->Origin + FIntPoint(X, Y);
                const TCHAR L = R9->Look(X, Y);
                if (W->SceneryLookAt(K) != L || W->ScenerySourceAt(K) != TEXT("ROUTE_9"))
                    M4_FAIL(TEXT("Route 9 %d,%d: drawn as '%c' from %s, Yellow has '%c'"), X, Y, W->SceneryLookAt(K) ? W->SceneryLookAt(K) : TEXT('-'), *W->ScenerySourceAt(K).ToString(), L);
                const uint8 Bits = Drawn.FindRef(K);
                if (!Bits) M4_FAIL(TEXT("Route 9 %d,%d ('%c') has nothing drawn"), X, Y, L);
                if (L == TEXT('T') && !(Bits & 1)) M4_FAIL(TEXT("Route 9 %d,%d is a tree in Yellow but no tree is drawn"), X, Y);
                if (L == TEXT('r') && !(Bits & 4)) M4_FAIL(TEXT("Route 9 %d,%d is rock in Yellow but no rock is drawn"), X, Y);
                Trees += L == TEXT('T');
                Rock += L == TEXT('r');
                Ground += (Bits & 2) ? 1 : 0;
            }
        St.Note = FString::Printf(TEXT("Route 9's first 6 columns (%d tiles: %d trees, %d rock, %d on ground) drawn from its own looks"), R9->Height * 6, Trees, Rock, Ground);
        // A Cerulean row open to the east edge, with Route 9 walkable-looking ground beyond (the road east).
        for (int32 Y = 0; Y < C->Height && St.P == FIntPoint::ZeroValue; ++Y)
        {
            bool bOpen = true;
            for (int32 X = C->Width - 4; X < C->Width; ++X) bOpen &= !YTile::IsSolid(C->Tile(X, Y)) && C->WarpAt(X, Y) == INDEX_NONE;
            const int32 Ry = C->Origin.Y + Y - R9->Origin.Y;
            if (bOpen && Ry >= 0 && Ry < R9->Height && FString(TEXT("spfyG")).Contains(FString::Chr(R9->Look(0, Ry)))) St.P = FIntPoint(C->Width - 4, Y);
        }
        if (St.P == FIntPoint::ZeroValue) M4_FAIL(TEXT("no Cerulean row open to the east edge"));
        Place(Test, TEXT("CERULEAN_CITY"), St.P.X, St.P.Y, EYDir::Right);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.3);
        // Push east for three seconds.
        R->TestInput = FVector(1, 0, 0);
        MaxX = FMath::Max(MaxX, KantoTile(Test).X);
        if (F->CurrentMap != TEXT("CERULEAN_CITY")) M4_FAIL(TEXT("walking east took Red into %s"), *F->CurrentMap.ToString());
        if (Test.Seconds - St.T0 < 3.3) return EGameTest::Running;
        StopWalking(Test);
        M4_NEXT();
    }
    M4_WAIT(.4);
    const int32 Edge = C->Origin.X + C->Width - 1;
    if (MaxX > Edge) M4_FAIL(TEXT("Red walked onto Route 9 (tile x %d, Cerulean ends at %d)"), MaxX, Edge);
    if (KantoTile(Test).X != Edge) M4_FAIL(TEXT("Red stopped at x %d, not on Cerulean's last column %d"), KantoTile(Test).X, Edge);
    // The minimap past the edge: Route 9's tiles in the stitched picture, two tiles east of Red.
    UPYRHud* Hud = UPYRHud::Get(Test.World);
    if (!Hud || Hud->GetImage() != TEXT("minimap/kanto_outdoor.png")) M4_FAIL(TEXT("the minimap shows '%s'"), Hud ? *Hud->GetImage() : TEXT("nothing"));
    TArray<uint8> Bytes;
    const FString Png = FPaths::Combine(UYellowData::DefaultRoot(), TEXT("minimap/kanto_outdoor.png"));
    if (!FFileHelper::LoadFileToArray(Bytes, *Png)) M4_FAIL(TEXT("YellowData missing: %s"), *Png);
    IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> Img = Module.CreateImageWrapper(EImageFormat::PNG);
    TArray<uint8> Raw;
    if (!Img.IsValid() || !Img->SetCompressed(Bytes.GetData(), Bytes.Num()) || !Img->GetRaw(ERGBFormat::BGRA, 8, Raw)) M4_FAIL(TEXT("can't read %s"), *Png);
    const FVector2D RedPic = Hud->GetWindowTiles() + FVector2D(5.f, 4.5f);
    int32 Filled = 0, Probes = 0;
    for (int32 Dx = 1; Dx <= 4; ++Dx)
        for (int32 Dy = -3; Dy <= 3; ++Dy)
        {
            const int32 Px = FMath::FloorToInt((RedPic.X + Dx) * 16.f), Py = FMath::FloorToInt((RedPic.Y + Dy) * 16.f);
            if (Px < 0 || Py < 0 || Px >= int32(Img->GetWidth()) || Py >= int32(Img->GetHeight())) continue;
            ++Probes;
            Filled += Raw[(int64(Py) * Img->GetWidth() + Px) * 4 + 3] > 0 ? 1 : 0;
        }
    if (Probes == 0 || Filled != Probes) M4_FAIL(TEXT("the minimap past Cerulean's east edge is blank at %d of %d tiles"), Probes - Filled, Probes);
    M4_PASS(TEXT("%s; walking east Red stopped on Cerulean's last column (x %d) and stayed in Cerulean; the minimap shows Route 9's tiles past the edge (%d of %d probes)"),
        *St.Note, Edge, Filled, Probes);
}

// The conversation camera (visual-074, -080): talking to someone face to face, the camera looks across the line between
// them, so they stand well apart in the picture; when the talk ends and something else turns the camera (a script, a
// staged view, the player), it keeps that view and doesn't swing back to where it was before the talk.
GAME_TEST(TalkCameraLooksAcrossThenYields, 90)
{
    static FState St;
    M4_READY();
    APlayerController* PC = Test.World->GetFirstPlayerController();
    APYRPerson* Girl = Person(Test, TEXT("PALLET_TOWN"), TEXT("PALLETTOWN_GIRL"));
    if (!PC || !Girl) M4_FAIL(TEXT("no controller or no Pallet girl"));
    if (St.Step == 0)
    {
        AfterPokedex(Test);
        Quiet(true, true);
        Girl->bFrozen = true;
        if (!PlaceToTalk(Test, Girl)) M4_FAIL(TEXT("nowhere to stand to talk to the girl"));
        // The camera straight behind Red, looking at her over his shoulder.
        const FVector To = Girl->GetActorLocation() - Red(Test)->GetActorLocation();
        PC->SetControlRotation(FRotator(-30.f, To.Rotation().Yaw, 0.f));
        St.A = To.Rotation().Yaw;
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.3);
        PressE(Test);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(1.6);
        if (!Menus(Test)->IsOpen(TEXT("text"))) M4_FAIL(TEXT("talking to the girl opened no text box"));
        const FRotator R = PC->PlayerCameraManager->GetCameraRotation();
        const float Across = FMath::Abs(FVector::DotProduct(FRotationMatrix(R).GetUnitAxis(EAxis::Y), Girl->GetActorLocation() - Red(Test)->GetActorLocation()));
        const float Turned = FMath::Abs(FRotator::NormalizeAxis(PC->GetControlRotation().Yaw - St.A));
        if (Across < 75.f) M4_FAIL(TEXT("while they talk, Red and the girl are only %.0f cm apart across the picture (turned %.0f degrees)"), Across, Turned);
        St.B = Across;
        St.Note = FString::Printf(TEXT("talking: turned %.0f degrees, %.0f cm apart across the view; "), Turned, Across);
        Menus(Test)->CloseAll();
        PC->SetControlRotation(FRotator(-25.f, 37.f, 0.f));
        M4_NEXT();
    }
    M4_WAIT(1.5);
    const float Yaw = PC->GetControlRotation().Yaw;
    if (FMath::Abs(FRotator::NormalizeAxis(Yaw - 37.f)) > 3.f) M4_FAIL(TEXT("%safter the talk the camera was turned to 37 degrees but swung to %.0f"), *St.Note, Yaw);
    M4_PASS(TEXT("%safter the talk a new view (37 degrees) stays (%.0f)"), *St.Note, Yaw);
}

// Viridian Gym's spinner floor (visual-055): every arrow tile of pokeyellow's ViridianGymArrowTilePlayerMovement is drawn
// as an arrow pointing the way it sends Red, and every square where a slide stops as a stop panel; they are all tiles
// Yellow's looks call 'm', and no 'm' tile of the gym is left as a red door mat except the two at the door.
GAME_TEST(ViridianGymSpinnerFloor, 120)
{
    static FState St;
    M4_READY();
    const FYMap* M = Data(Test)->Map(TEXT("VIRIDIAN_GYM"));
    AYellowWorld* W = World(Test);
    if (!M) M4_FAIL(TEXT("no Viridian Gym"));
    if (St.Step == 0)
    {
        Quiet(true, true);
        AfterPokedex(Test);
        Place(Test, TEXT("VIRIDIAN_GYM"), 16, 16, EYDir::Up);
        M4_NEXT();
    }
    M4_WAIT(.4);
    int32 Arrows = 0, Stops = 0, Mats = 0;
    for (int32 Y = 0; Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X)
        {
            FIntPoint D;
            bool bStop = false;
            const FName F = W->FurnitureAt(TEXT("VIRIDIAN_GYM"), X, Y);
            if (AYellowWorld::GymSpinnerAt(*M, X, Y, D, bStop))
            {
                if (M->Look(X, Y) != TEXT('m') || YTile::IsSolid(M->Tile(X, Y))) M4_FAIL(TEXT("spinner %d,%d is look '%c' tile '%c' in Yellow's data"), X, Y, M->Look(X, Y), M->Tile(X, Y));
                const TCHAR* Want = bStop ? TEXT("spinner_stop") : D.Y < 0 ? TEXT("spinner_up") : D.Y > 0 ? TEXT("spinner_down") : D.X < 0 ? TEXT("spinner_left") : TEXT("spinner_right");
                if (F != FName(Want)) M4_FAIL(TEXT("%d,%d should be %s but is %s"), X, Y, Want, *F.ToString());
                (bStop ? Stops : Arrows) += 1;
            }
            else if (M->Look(X, Y) == TEXT('m'))
            {
                if (M->WarpAt(X, Y) == INDEX_NONE) M4_FAIL(TEXT("%d,%d is a spinner-floor 'm' tile not drawn as one"), X, Y);
                ++Mats;
            }
        }
    if (Arrows != 12 || Stops != 12) M4_FAIL(TEXT("%d arrows and %d stops drawn (Yellow: 12 and 12)"), Arrows, Stops);
    M4_PASS(TEXT("12 arrow tiles each pointing its way, 12 stop squares, %d door mats"), Mats);
}

// The trainer card's TIME (visual-093): play time runs while Red is in the field, survives a save, and the card prints
// it as hours:minutes the way Yellow does.
GAME_TEST(TrainerCardTimeCounts, 90)
{
    static FState St;
    M4_READY();
    UPYRProgress* P = UPYRProgress::Get(Test.World);
    if (!P) M4_FAIL(TEXT("no progress"));
    if (St.Step == 0)
    {
        Quiet(true, true);
        AfterPokedex(Test);
        Place(Test, TEXT("PALLET_TOWN"), 9, 12, EYDir::Up);
        P->PlaySeconds = 0;
        M4_NEXT();
    }
    M4_WAIT(3.0);
    const double Ran = P->PlaySeconds;
    if (Ran < 1.5 || Ran > 30.0) M4_FAIL(TEXT("after 3 s in the field the play time is %.2f s"), Ran);
    P->PlaySeconds = 2 * 3600 + 14 * 60 + 30;
    if (PYRScreens::CardTime(Test.World) != TEXT("TIME/2:14")) M4_FAIL(TEXT("2:14:30 of play prints as %s"), *PYRScreens::CardTime(Test.World));
    const FPYRProgressSnapshot Saved = P->MakeSnapshot();
    P->PlaySeconds = 0;
    P->ApplySnapshot(Saved);
    if (PYRScreens::CardTime(Test.World) != TEXT("TIME/2:14")) M4_FAIL(TEXT("after a save and load the card reads %s"), *PYRScreens::CardTime(Test.World));
    PYRScreens::OpenTrainerCard(Test.World);
    M4_PASS(TEXT("%.1f s of play counted in 3 s; 2:14:30 reads TIME/2:14 and survives a save"), Ran);
}

// South of Pallet Town (warp-002): Route 21's sea, its border block, runs on to the horizon on both sides of the route
// and past its end, as the Game Boy shows open water there, not a meadow 20 tiles out.
GAME_TEST(PalletSouthSeaToHorizon, 90)
{
    static FState St;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    AYellowWorld* W = World(Test);
    const FYMap* R21 = nullptr;
    for (const FYMap& S : D->Scenery) if (S.Id == TEXT("ROUTE_21")) R21 = &S;
    if (!R21) M4_FAIL(TEXT("no Route 21 scenery"));
    int32 Checked = 0;
    for (const FIntPoint Off : {FIntPoint(-30, 40), FIntPoint(-40, 70), FIntPoint(R21->Width + 30, 40), FIntPoint(R21->Width + 40, 70), FIntPoint(R21->Width / 2, R21->Height + 30),
             FIntPoint(R21->Width / 2, R21->Height + 40)})
    {
        const FIntPoint K = R21->Origin + Off;
        if (W->MapAtKanto(K)) continue;
        const TCHAR L = W->SceneryLookAt(K);
        if (L != TEXT('W')) M4_FAIL(TEXT("Kanto %s, %d,%d from Route 21, is '%c', not the sea"), *K.ToString(), Off.X, Off.Y, L ? L : TEXT('-'));
        ++Checked;
    }
    if (Checked < 4) M4_FAIL(TEXT("only %d sea tiles checked"), Checked);
    M4_PASS(TEXT("%d tiles 30-40 out from Route 21 are its sea"), Checked);
}
