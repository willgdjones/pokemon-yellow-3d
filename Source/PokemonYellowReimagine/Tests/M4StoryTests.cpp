// M4 story and services: the roadblocks from Pallet Town to Route 22's gate, trainers (sight, walking up, teams,
// prizes), the Pokemon Centre, the Mart and using items from the bag.
#include "Tests/M4TestKit.h"

#include "Battle/BattleTestKit.h"
#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Encounters/PYRRing.h"
#include "Engine/World.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Menus/PYRSysText.h"
#include "Party/PYRSpecies.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Stage/PYREncounters.h"
#include "Stage/PYRStage.h"

using namespace M4;

namespace
{
UPYRScripts* Scripts(FGameTestContext& Test) { return UPYRScripts::Get(Test.World); }
UPYREncounters* Enc(FGameTestContext& Test) { return UPYREncounters::Get(Test.World); }

// Everything quiet: no menus, no script, no fight.
bool Idle(FGameTestContext& Test)
{
    return !Menus(Test)->AnyOpen() && !Scripts(Test)->IsRunning() && !Enc(Test)->IsBusy() && !Field(Test)->IsWarping();
}

// A fight on the stage: the foe knocked out each time it is out, until the battle is won.
void WinFight(FGameTestContext& Test)
{
    APYRBattleManager* BM = APYRBattleManager::Get(Test.World);
    if (!BM || !BM->IsBattleActive() || BM->IsEnding()) return;
    BM->bNoRandom = true;
    APYRMonPawn* Foe = BM->GetFoeMon();
    APYRMonPawn* Mine = BM->GetPlayerMon();
    if (Foe && Mine && !Foe->bFainted && Foe->Hp > 0) PYRTest::KnockOut(BM);
}

// The trainers the story scripts battle themselves (events with a battle) are not sight trainers.
TSet<FString> EventTrainers(const FYellowDataSet* D)
{
    TSet<FString> Out;
    for (const TSharedPtr<FJsonObject>& E : D->Events)
    {
        FString Npc;
        if (E.IsValid() && E->HasField(TEXT("battle")) && E->TryGetStringField(TEXT("npc"), Npc)) Out.Add(Npc);
    }
    return Out;
}

// A sight trainer with at least MinSight open tiles in front, outdoors (a grass-free tile for Red at the far end).
APYRPerson* SightTrainer(FGameTestContext& Test, int32 MinSight, int32 MinParty, FIntPoint& OutRedTile)
{
    const FYellowDataSet* D = Data(Test);
    const AYellowWorld* W = World(Test);
    const TSet<FString> Scripted = EventTrainers(D);
    for (const TWeakObjectPtr<APYRPerson>& PW : Field(Test)->People)
    {
        APYRPerson* P = PW.Get();
        if (!P || !P->IsTrainer() || !P->IsShown() || P->bWalker || P->Trainer.Sight < MinSight || Scripted.Contains(P->Id)) continue;
        if (D->TrainerParty(P->Trainer).Num() < MinParty) continue;
        const FYMap* M = D->Map(P->Map);
        if (!M || !M->bOutdoor) continue;
        FIntPoint T = P->Tile;
        int32 Open = 0;
        for (int32 I = 1; I <= P->Trainer.Sight; ++I)
        {
            if (W->IsEdgeBlocked(T, P->Facing)) break;
            T += YDir::Delta(P->Facing);
            const FYMap* TM = W->MapAtKanto(T);
            if (!TM || TM->Id != M->Id || W->IsSolidAt(T) || Field(Test)->IsOccupied(T, P)) break;
            const FIntPoint L = T - M->Origin;
            if (M->WarpAt(L.X, L.Y) != INDEX_NONE) break;
            Open = I;
        }
        if (Open < MinSight) continue;
        OutRedTile = P->Tile + YDir::Delta(P->Facing) * Open;
        return P;
    }
    return nullptr;
}

// The weakest unbeaten trainer (by its strongest Pokemon) with at least MinParty Pokemon.
APYRPerson* AnyTrainer(FGameTestContext& Test, int32 MinParty)
{
    const FYellowDataSet* D = Data(Test);
    const TSet<FString> Scripted = EventTrainers(D);
    APYRPerson* Best = nullptr;
    int32 BestLevel = MAX_int32;
    for (const TWeakObjectPtr<APYRPerson>& PW : Field(Test)->People)
        if (APYRPerson* P = PW.Get())
        {
            const TArray<FYSlot> Team = D->TrainerParty(P->Trainer);
            if (!P->IsTrainer() || !P->IsShown() || Scripted.Contains(P->Id) || Team.Num() < MinParty || P->IsBeaten()) continue;
            int32 Top = 0;
            for (const FYSlot& S : Team) Top = FMath::Max(Top, S.Level);
            if (Top < BestLevel) { Best = P; BestLevel = Top; }
        }
    return Best;
}
}

// ------------------------------------------------------------------------------------------------ roadblocks
// Without a Pokemon, Oak stops Red at Route 1's grass and takes him back to the lab, where he gets Pikachu.
GAME_TEST(Roadblock_OakStopsAtGrass, 180)
{
    static FState St;
    M4_READY();
    const FYMap* Pallet = Data(Test)->Map(TEXT("PALLET_TOWN"));
    UPYRField* F = Field(Test);
    Quiet(true, false);
    if (St.Step == 0)
    {
        // A new game: no Pokemon, no flags. The grass is at the top of Pallet Town (the 'gg' gap).
        for (int32 X = 0; X < Pallet->Width && St.P.X == 0; ++X) if (Pallet->Tile(X, 0) == TEXT('g')) St.P = FIntPoint(X, 0);
        if (St.P.X == 0) M4_FAIL(TEXT("no grass on Pallet Town's top row"));
        Place(Test, TEXT("PALLET_TOWN"), St.P.X, 4, EYDir::Up);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        if (!Scripts(Test)->IsRunning()) WalkToTile(Test, TEXT("PALLET_TOWN"), St.P.X, St.P.Y, 5.f);
        if (F->CurrentMap == TEXT("ROUTE_1")) M4_FAIL(TEXT("Red walked into Route 1 without a Pokemon"));
        if (Scripts(Test)->Ran.Contains(TEXT("oak_stops_red"))) { StopWalking(Test); St.Note = FString::Printf(TEXT("stopped at (%d,%d); "), F->CurrentTile.X, F->CurrentTile.Y); M4_NEXT(); }
        if (Test.Seconds - St.T0 > 8.0) M4_FAIL(TEXT("walked to (%d,%d) with no Oak"), F->CurrentTile.X, F->CurrentTile.Y);
        return EGameTest::Running;
    }
    // His lines, the walk to the lab, choosing, and Pikachu.
    Advance(Test);
    const UPYRTrainerState* S = State(Test);
    if (S->Party.Num() > 0 && Progress(Test)->Has(TEXT("EVENT_GOT_STARTER")) && Idle(Test))
    {
        if (S->Party[0].Species != TEXT("pikachu")) M4_FAIL(TEXT("the starter is %s"), *S->Party[0].Species.ToString());
        if (F->CurrentMap != TEXT("OAKS_LAB")) M4_FAIL(TEXT("after Oak, Red is in %s"), *F->CurrentMap.ToString());
        M4_PASS(TEXT("%sOak led Red to the lab; Pikachu L%d in the party; Red in OAKS_LAB (%d,%d)"), *St.Note, S->Party[0].Level, F->CurrentTile.X, F->CurrentTile.Y);
    }
    if (Test.Seconds - St.T0 > 120.0) M4_FAIL(TEXT("Oak's scene never finished: %s"), *Menus(Test)->ScreenText());
    return EGameTest::Running;
}

// The Viridian Mart hands Red Oak's Parcel and sells nothing until Oak has it.
GAME_TEST(Roadblock_ViridianMartParcel, 150)
{
    static FState St;
    M4_READY();
    UPYRField* F = Field(Test);
    UPYRTrainerState* S = State(Test);
    Quiet(true, false);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 6}});
        Flags(Test, {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_OAK_ASKED_TO_CHOOSE_MON"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB")});
        const FYMap* City = Data(Test)->Map(TEXT("VIRIDIAN_CITY"));
        for (int32 I = 0; I < City->Warps.Num(); ++I)
            if (City->Warps[I].ToMap == TEXT("VIRIDIAN_MART")) St.P = FIntPoint(City->Warps[I].X, City->Warps[I].Y);
        Place(Test, TEXT("VIRIDIAN_CITY"), St.P.X, St.P.Y + 1, EYDir::Up);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // In through the door: the clerk calls Red over and hands him the parcel.
        if (F->CurrentMap != TEXT("VIRIDIAN_MART")) { WalkToTile(Test, TEXT("VIRIDIAN_CITY"), St.P.X, St.P.Y, 5.f); if (Test.Seconds - St.T0 > 6) M4_FAIL(TEXT("never got into the Mart")); return EGameTest::Running; }
        StopWalking(Test);
        Advance(Test);
        if (S->HasItem(UPYRTrainerState::OaksParcel) && Idle(Test))
        {
            if (!Progress(Test)->Has(TEXT("EVENT_GOT_OAKS_PARCEL"))) M4_FAIL(TEXT("parcel given but EVENT_GOT_OAKS_PARCEL unset"));
            St.Note = TEXT("the clerk gave OAK's PARCEL on entering; ");
            M4_NEXT();
        }
        if (Test.Seconds - St.T0 > 30) M4_FAIL(TEXT("no parcel: %s"), *Said(Test));
        return EGameTest::Running;
    }
    APYRPerson* Clerk = Person(Test, TEXT("VIRIDIAN_MART"), TEXT("VIRIDIANMART_CLERK"));
    if (!Clerk) M4_FAIL(TEXT("no VIRIDIANMART_CLERK"));
    if (St.Step == 2)
    {
        if (!PlaceToTalk(Test, Clerk)) M4_FAIL(TEXT("no tile to talk to the clerk from"));
        Menus(Test)->TextLog.Reset();
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        M4_WAIT(.2);
        PressE(Test);
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        M4_WAIT(.3);
        if (Menus(Test)->IsOpen(TEXT("mart"))) M4_FAIL(TEXT("the Mart opened before Oak got his parcel"));
        const FString Wait = FirstLineOf(PYRGb::Fill(Test.World, Data(Test)->TextFor(TEXT("ViridianMartClerkSayHiToOakText"))));
        if (!SaidContains(Test, Wait)) M4_FAIL(TEXT("the clerk said '%s', not '%s'"), *Said(Test), *Wait);
        SkipText(Test);
        St.Note += TEXT("before Oak has it the clerk only says hi to him; ");
        Flags(Test, {TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")});
        PressE(Test);
        M4_NEXT();
    }
    M4_WAIT(.2);
    if (St.Step == 5 && Menus(Test)->IsOpen(TEXT("mart")))
    {
        // BUY: Yellow's Viridian stock at Yellow's prices (POKé BALL 200, POTION 300).
        Press(Test, EPYRKey::Confirm);
        TSharedPtr<FPYRScreen> Buy = Menus(Test)->Find(TEXT("mart-buy"));
        if (!Buy.IsValid()) M4_FAIL(TEXT("BUY did not open the list: %s"), *Menus(Test)->ScreenText());
        const FString List = Buy->Describe();
        for (const TCHAR* Want : {TEXT("POKé BALL"), TEXT("POTION")})
        {
            const int32 At = List.Find(Want, ESearchCase::CaseSensitive);
            const FString Price = At == INDEX_NONE ? FString() : List.Mid(At).Left(40);
            const TCHAR* Cost = FCString::Strcmp(Want, TEXT("POTION")) == 0 ? TEXT("¥300") : TEXT("¥200");
            if (!Price.Contains(Cost)) M4_FAIL(TEXT("%s is not sold at %s: %s"), Want, Cost, *List);
        }
        M4_PASS(TEXT("%safter, BUY/SELL/QUIT opens and BUY sells POKé BALL at ¥200 and POTION at ¥300"), *St.Note);
    }
    Advance(Test);
    if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the Mart never opened after the parcel: %s"), *Menus(Test)->ScreenText());
    return EGameTest::Running;
}

// The old man lies across Viridian's north road until Red has the Pokedex; then he shows how to catch.
GAME_TEST(Roadblock_OldManNeedsPokedex, 240)
{
    static FState St;
    M4_READY();
    UPYRField* F = Field(Test);
    Quiet(true, false);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 8}});
        Flags(Test, {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL")});
        Place(Test, TEXT("VIRIDIAN_CITY"), 19, 11, EYDir::Up);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // North into the trigger tile (19,9): his line, and Red walks back down.
        if (!Scripts(Test)->IsRunning() && !Menus(Test)->AnyOpen() && St.I == 0) WalkToTile(Test, TEXT("VIRIDIAN_CITY"), 19, 7, 5.f);
        if (Scripts(Test)->Ran.Contains(TEXT("viridian_old_man_sleepy_blocks_north"))) { St.I = 1; StopWalking(Test); }
        if (F->CurrentTile.Y < 9) M4_FAIL(TEXT("Red got past the old man to (%d,%d) without the Pokedex"), F->CurrentTile.X, F->CurrentTile.Y);
        if (St.I == 1)
        {
            Advance(Test);
            if (Idle(Test))
            {
                if (F->CurrentTile.Y < 10) M4_FAIL(TEXT("after the old man Red is at (%d,%d), not pushed back"), F->CurrentTile.X, F->CurrentTile.Y);
                St.Note = FString::Printf(TEXT("without the Pokedex: turned back to (%d,%d); "), F->CurrentTile.X, F->CurrentTile.Y);
                AfterPokedex(Test);
                St.I = 0;
                M4_NEXT();
            }
        }
        if (Test.Seconds - St.T0 > 30) M4_FAIL(TEXT("the old man never blocked the road (Red at %d,%d)"), F->CurrentTile.X, F->CurrentTile.Y);
        return EGameTest::Running;
    }
    if (St.Step == 2)
    {
        // With the Pokedex: his catching demo (a battle he plays himself), then the road is open.
        if (!Scripts(Test)->IsRunning() && !Menus(Test)->AnyOpen() && !Enc(Test)->IsBusy() && !Progress(Test)->Has(TEXT("EVENT_COMPLETED_CATCH_TRAINING")))
            WalkToTile(Test, TEXT("VIRIDIAN_CITY"), 19, 8, 5.f);
        Advance(Test);
        if (Progress(Test)->Has(TEXT("EVENT_COMPLETED_CATCH_TRAINING")) && Idle(Test))
        {
            St.Note += TEXT("with it: the catching demo; ");
            M4_NEXT();
        }
        if (Test.Seconds - St.T0 > 150) M4_FAIL(TEXT("no catching demo: %s | %s"), *Menus(Test)->ScreenText(), *Said(Test).Right(300));
        return EGameTest::Running;
    }
    WalkToTile(Test, TEXT("VIRIDIAN_CITY"), 19, 6, 8.f);
    Advance(Test);
    if (F->CurrentMap == TEXT("VIRIDIAN_CITY") && F->CurrentTile.Y <= 7) { StopWalking(Test); M4_PASS(TEXT("%sthen Red walks north to (%d,%d)"), *St.Note, F->CurrentTile.X, F->CurrentTile.Y); }
    if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the road stays shut after the demo (Red at %d,%d)"), F->CurrentTile.X, F->CurrentTile.Y);
    return EGameTest::Running;
}

// The boy at Pewter's east exit walks Red to the gym until Brock is beaten.
GAME_TEST(Roadblock_PewterBoyUntilBadge, 150)
{
    static FState St;
    M4_READY();
    UPYRField* F = Field(Test);
    const AYellowWorld* W = World(Test);
    Quiet(true, false);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 12}});
        AfterPokedex(Test);
        Place(Test, TEXT("PEWTER_CITY"), 33, 17, EYDir::Right);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        if (St.I == 0 && !Scripts(Test)->IsRunning()) WalkToTile(Test, TEXT("PEWTER_CITY"), 38, 17, 5.f);
        if (Scripts(Test)->Ran.Contains(TEXT("pewter_youngster_leads_to_gym"))) { St.I = 1; StopWalking(Test); }
        if (F->CurrentMap != TEXT("PEWTER_CITY") || F->CurrentTile.X > 37) M4_FAIL(TEXT("Red left Pewter east (%s %d,%d) without the badge"), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y);
        if (St.I == 1)
        {
            Advance(Test);
            if (Idle(Test))
            {
                const FYBuilding* Gym = W->BuildingWithDoorTo(TEXT("PEWTER_GYM"));
                const FIntPoint Door = Gym && Gym->Doors.Num() ? Gym->Doors[0] : FIntPoint(-99, -99);
                const FIntPoint K = KantoTile(Test);
                if (FMath::Abs(K.X - Door.X) + FMath::Abs(K.Y - Door.Y) > 2) M4_FAIL(TEXT("the boy left Red at %d,%d, not at the gym door %d,%d"), K.X, K.Y, Door.X, Door.Y);
                St.Note = FString::Printf(TEXT("without the Boulder Badge the boy led Red to the gym door (%d,%d); "), F->CurrentTile.X, F->CurrentTile.Y);
                State(Test)->AwardBadge(EPYRBadge::Boulder);
                Flags(Test, {TEXT("EVENT_BEAT_BROCK")});
                Place(Test, TEXT("PEWTER_CITY"), 33, 17, EYDir::Right);
                St.I = 0;
                M4_NEXT();
            }
        }
        if (Test.Seconds - St.T0 > 40) M4_FAIL(TEXT("the boy never stopped Red (at %d,%d)"), F->CurrentTile.X, F->CurrentTile.Y);
        return EGameTest::Running;
    }
    WalkToTile(Test, TEXT("PEWTER_CITY"), 38, 17, 8.f);
    if (Scripts(Test)->Ran.FilterByPredicate([](const FString& S) { return S == TEXT("pewter_youngster_leads_to_gym"); }).Num() > 1) M4_FAIL(TEXT("the boy stopped Red again after Brock"));
    if (F->CurrentTile.X >= 37 || F->CurrentMap == TEXT("ROUTE_3")) { StopWalking(Test); M4_PASS(TEXT("%safter Brock Red walks east to %s (%d,%d)"), *St.Note, *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y); }
    if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("after Brock Red still cannot pass (at %d,%d)"), F->CurrentTile.X, F->CurrentTile.Y);
    return EGameTest::Running;
}

// The Route 22 gate guard turns Red back without the Boulder Badge and lets him through with it.
GAME_TEST(Roadblock_Route22GateBadge, 120)
{
    static FState St;
    M4_READY();
    UPYRField* F = Field(Test);
    Quiet(true, false);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 12}});
        AfterPokedex(Test);
        Place(Test, TEXT("ROUTE_22_GATE"), 4, 4, EYDir::Up);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        if (St.I == 0 && !Scripts(Test)->IsRunning()) WalkToTile(Test, TEXT("ROUTE_22_GATE"), 4, 1, 5.f);
        if (Scripts(Test)->Ran.Contains(TEXT("route22_gate_blocked"))) { St.I = 1; StopWalking(Test); }
        if (F->CurrentTile.Y < 2) M4_FAIL(TEXT("Red passed the gate without the badge (%d,%d)"), F->CurrentTile.X, F->CurrentTile.Y);
        if (St.I == 1)
        {
            Advance(Test);
            if (Idle(Test))
            {
                if (F->CurrentTile.Y < 3) M4_FAIL(TEXT("the guard did not send Red back (%d,%d)"), F->CurrentTile.X, F->CurrentTile.Y);
                St.Note = FString::Printf(TEXT("no badge: turned back to (%d,%d); "), F->CurrentTile.X, F->CurrentTile.Y);
                State(Test)->AwardBadge(EPYRBadge::Boulder);
                St.I = 0;
                M4_NEXT();
            }
        }
        if (Test.Seconds - St.T0 > 30) M4_FAIL(TEXT("the guard never stopped Red (at %d,%d)"), F->CurrentTile.X, F->CurrentTile.Y);
        return EGameTest::Running;
    }
    if (!Scripts(Test)->IsRunning() && !Menus(Test)->AnyOpen()) WalkToTile(Test, TEXT("ROUTE_22_GATE"), 4, 1, 8.f);
    Advance(Test);
    if (F->CurrentMap != TEXT("ROUTE_22_GATE") || F->CurrentTile.Y <= 1)
    {
        StopWalking(Test);
        M4_PASS(TEXT("%swith the Boulder Badge: passed to %s (%d,%d)%s"), *St.Note, *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y,
            Scripts(Test)->Ran.Contains(TEXT("route22_gate_pass")) ? TEXT(", \"go right ahead\"") : TEXT(""));
    }
    if (Test.Seconds - St.T0 > 12) M4_FAIL(TEXT("with the badge Red still cannot pass (%d,%d)"), F->CurrentTile.X, F->CurrentTile.Y);
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ trainers
// A trainer sees Red along its facing, shows '!', walks up to him tile by tile and starts the battle.
GAME_TEST(Trainer_SpotsAndWalks, 120)
{
    static FState St;
    static TWeakObjectPtr<APYRPerson> Trainer;
    static FIntPoint Home;
    M4_READY();
    UPYRScripts* Sc = Scripts(Test);
    Quiet(false, true);
    if (St.Step == 0)
    {
        // Red's lead is strong and a Repel is on, so wild Pokemon stay away.
        Party(Test, {{TEXT("pikachu"), 60}});
        Progress(Test)->RepelSteps = 999;
        AfterPokedex(Test);
        FIntPoint RedAt;
        APYRPerson* P = SightTrainer(Test, 3, 1, RedAt);
        if (!P) M4_FAIL(TEXT("no sight trainer with three open tiles"));
        Trainer = P;
        Home = P->Tile;
        const FYMap* M = Data(Test)->Map(P->Map);
        Place(Test, P->Map, RedAt.X - M->Origin.X, RedAt.Y - M->Origin.Y, YDir::Opposite(P->Facing));
        St.Note = FString::Printf(TEXT("%s %s (sight %d) at %s (%d,%d) facing %s; Red %d tiles away; "), *P->ClassName(), *P->Id, P->Trainer.Sight, *P->Map.ToString(),
            P->Tile.X - M->Origin.X, P->Tile.Y - M->Origin.Y, YDir::Name(P->Facing), FMath::Abs(RedAt.X - P->Tile.X) + FMath::Abs(RedAt.Y - P->Tile.Y));
        M4_NEXT();
    }
    APYRPerson* P = Trainer.Get();
    if (!P) M4_FAIL(TEXT("the trainer is gone"));
    if (St.Step == 1)
    {
        if (P->IsExclaiming()) St.A = 1;
        if (Sc->Spotted > 0 && Menus(Test)->IsTalking())
        {
            const FIntPoint R = KantoTile(Test);
            const int32 Apart = FMath::Abs(R.X - P->Tile.X) + FMath::Abs(R.Y - P->Tile.Y);
            if (St.A == 0) M4_FAIL(TEXT("no '!' over the trainer"));
            if (Apart != 1) M4_FAIL(TEXT("the trainer stopped %d tiles from Red"), Apart);
            if (P->Tile == Home) M4_FAIL(TEXT("the trainer never walked"));
            St.Note += FString::Printf(TEXT("'!' then walked %d tiles up to Red; "), FMath::Abs(P->Tile.X - Home.X) + FMath::Abs(P->Tile.Y - Home.Y));
            M4_NEXT();
        }
        if (Test.Seconds - St.T0 > 12) M4_FAIL(TEXT("the trainer never noticed Red (spotted %d)"), Sc->Spotted);
        return EGameTest::Running;
    }
    Advance(Test);
    if (Enc(Test)->GetPhase() == EPYREncounterPhase::Fight)
        M4_PASS(TEXT("%sthe battle started on the %s stage"), *St.Note, APYRBattleStage::Name(Enc(Test)->GetFight().Stage));
    if (Test.Seconds - St.T0 > 20) M4_FAIL(TEXT("no battle after the trainer's words"));
    return EGameTest::Running;
}

// Trainer battles close the ring (no worn gap to flee through); wild ones have the gap.
GAME_TEST(Trainer_NoFleeGap, 120)
{
    static FState St;
    M4_READY();
    UPYREncounters* E = Enc(Test);
    APYRBattleManager* BM = APYRBattleManager::Get(Test.World);
    Quiet(true, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 70}});
        Place(Test, TEXT("ROUTE_3"), 10, 8, EYDir::Right);
        APYRPerson* P = AnyTrainer(Test, 1);
        FPYRFight Fight;
        if (!P || !Scripts(Test)->TrainerFight(P, Fight)) M4_FAIL(TEXT("no trainer fight"));
        if (!E->Begin(Fight)) M4_FAIL(TEXT("the trainer fight did not start"));
        St.Note = FString::Printf(TEXT("%s: "), *Fight.TrainerName);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        if (E->GetPhase() != EPYREncounterPhase::Fight) { if (Test.Seconds - St.T0 > 5) M4_FAIL(TEXT("never reached the fight")); return EGameTest::Running; }
        M4_WAIT(1.5);
        APYRRing* Ring = E->GetRing();
        if (!Ring || Ring->HasGap()) M4_FAIL(TEXT("the trainer ring has a gap"));
        // Stand Pikachu where a wild ring's gap would be, for 4 s.
        if (APYRMonPawn* Mine = BM->GetPlayerMon())
        {
            const APYRBattleStage* Stage = E->GetCurrentStage();
            Mine->SetActorLocation(Stage->Centre + FVector(-Stage->Radius + 60.f, 0, Mine->GetActorLocation().Z - Stage->Centre.Z), false, nullptr, ETeleportType::TeleportPhysics);
        }
        if (Test.Seconds - St.T0 < 5.5) return EGameTest::Running;
        if (!BM->IsBattleActive() || E->LastOutcome == EPYRBattleOutcome::Fled && !BM->IsBattleActive()) M4_FAIL(TEXT("the trainer battle ended by standing at the edge"));
        St.Note += TEXT("closed ring, 4 s at its edge and still fighting; ");
        BM->RequestFlee();
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        Advance(Test);
        if (!E->IsBusy()) { E->BeginWild(TEXT("rattata"), 3); M4_NEXT(); }
        if (Test.Seconds - St.T0 > 20) M4_FAIL(TEXT("the trainer fight never ended"));
        return EGameTest::Running;
    }
    if (E->GetPhase() != EPYREncounterPhase::Fight) { if (Test.Seconds - St.T0 > 5) M4_FAIL(TEXT("the wild fight never started")); return EGameTest::Running; }
    M4_WAIT(1.0);
    if (!E->GetRing() || !E->GetRing()->HasGap()) M4_FAIL(TEXT("the wild ring has no gap"));
    M4_PASS(TEXT("%swild ring has its gap"), *St.Note);
}

// Every trainer's team is its trainers.json party (species, levels, special moves), and the next comes out on a faint.
GAME_TEST(Trainer_YellowTeams, 180)
{
    static FState St;
    static FPYRFight Fight;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYREncounters* E = Enc(Test);
    APYRBattleManager* BM = APYRBattleManager::Get(Test.World);
    Quiet(true, true);
    if (St.Step == 0)
    {
        int32 Trainers = 0, Mons = 0;
        for (const TWeakObjectPtr<APYRPerson>& PW : Field(Test)->People)
        {
            APYRPerson* P = PW.Get();
            if (!P || !P->IsTrainer()) continue;
            const TArray<FYSlot> Team = D->TrainerParty(P->Trainer);
            if (Team.Num() == 0) M4_FAIL(TEXT("%s:%s (%s party %d) has no team in trainers.json"), *P->Map.ToString(), *P->Id, *P->Trainer.Class.ToString(), P->Trainer.Party);
            FPYRFight F;
            if (!Scripts(Test)->TrainerFight(P, F)) M4_FAIL(TEXT("%s:%s spawns no fight"), *P->Map.ToString(), *P->Id);
            if (F.Foes.Num() != Team.Num()) M4_FAIL(TEXT("%s:%s fields %d Pokemon, trainers.json has %d"), *P->Map.ToString(), *P->Id, F.Foes.Num(), Team.Num());
            for (int32 I = 0; I < Team.Num(); ++I)
            {
                if (F.Foes[I].Species != Team[I].Species || F.Foes[I].Level != Team[I].Level)
                    M4_FAIL(TEXT("%s:%s slot %d is %s L%d, trainers.json says %s L%d"), *P->Map.ToString(), *P->Id, I, *F.Foes[I].Species.ToString(), F.Foes[I].Level,
                        *Team[I].Species.ToString(), Team[I].Level);
                for (const auto& Pair : Team[I].Moves)
                    if (!F.Foes[I].Moves.IsValidIndex(Pair.Key - 1) || F.Foes[I].Moves[Pair.Key - 1] != Pair.Value)
                        M4_FAIL(TEXT("%s:%s slot %d lacks its special move %s in slot %d"), *P->Map.ToString(), *P->Id, I, *Pair.Value.ToString(), Pair.Key);
                if (!PYRData::Species(F.Foes[I].Species)) M4_FAIL(TEXT("%s:%s fields %s, which the battle tables do not know"), *P->Map.ToString(), *P->Id, *F.Foes[I].Species.ToString());
            }
            ++Trainers;
            Mons += Team.Num();
        }
        St.Note = FString::Printf(TEXT("%d trainers, %d Pokemon, every team equals trainers.json; "), Trainers, Mons);
        // A live fight with a team of two or more.
        APYRPerson* P = AnyTrainer(Test, 2);
        if (!P || !Scripts(Test)->TrainerFight(P, Fight)) M4_FAIL(TEXT("no trainer with two Pokemon"));
        Party(Test, {{TEXT("pikachu"), 40}});
        Place(Test, TEXT("ROUTE_3"), 10, 8, EYDir::Right);
        if (!E->Begin(Fight)) M4_FAIL(TEXT("the fight did not start"));
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        if (E->GetPhase() != EPYREncounterPhase::Fight) return EGameTest::Running;
        M4_WAIT(1.5);
        APYRMonPawn* Foe = BM->GetFoeMon();
        if (!Foe || Foe->Species != Fight.Foes[0].Species) M4_FAIL(TEXT("the first foe is %s, not %s"), Foe ? *Foe->Species.ToString() : TEXT("none"), *Fight.Foes[0].Species.ToString());
        BM->bNoRandom = true;
        PYRTest::KnockOut(BM);
        M4_NEXT();
    }
    M4_WAIT(4.0);
    APYRMonPawn* Foe = BM->GetFoeMon();
    if (!Foe || Foe->Species != Fight.Foes[1].Species || Foe->bFainted) M4_FAIL(TEXT("after a faint the foe is %s, not %s"), Foe ? *Foe->Species.ToString() : TEXT("none"), *Fight.Foes[1].Species.ToString());
    M4_PASS(TEXT("%s%s sent out %s after %s fainted"), *St.Note, *Fight.TrainerName, *Fight.Foes[1].Species.ToString(), *Fight.Foes[0].Species.ToString());
}

// The prize is base_money times the last Pokemon's level, paid once; a beaten trainer never fights again.
GAME_TEST(Trainer_PrizeOnce, 180)
{
    static FState St;
    static TWeakObjectPtr<APYRPerson> Trainer;
    static int32 Prize = 0;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRTrainerState* S = State(Test);
    Quiet(true, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 60}});
        AfterPokedex(Test);
        APYRPerson* P = AnyTrainer(Test, 1);
        if (!P) M4_FAIL(TEXT("no trainer"));
        Trainer = P;
        const TArray<FYSlot> Team = D->TrainerParty(P->Trainer);
        const FYTrainerClass* C = D->TrainerClass(P->Trainer.Class);
        Prize = C->BaseMoney * Team.Last().Level;
        FPYRFight F;
        Scripts(Test)->TrainerFight(P, F);
        if (F.Prize != Prize) M4_FAIL(TEXT("%s's prize is %d, want base %d x level %d = %d"), *P->Id, F.Prize, C->BaseMoney, Team.Last().Level, Prize);
        if (!PlaceToTalk(Test, P)) M4_FAIL(TEXT("no tile to talk to %s from"), *P->Id);
        St.I = S->Money;
        St.Note = FString::Printf(TEXT("%s %s: prize %d (base %d x L%d); "), *C->Name, *P->Id, Prize, C->BaseMoney, Team.Last().Level);
        M4_NEXT();
    }
    APYRPerson* P = Trainer.Get();
    if (St.Step == 1)
    {
        M4_WAIT(.2);
        PressE(Test);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        Advance(Test);
        WinFight(Test);
        if (P->IsBeaten() && Idle(Test))
        {
            if (S->Money - St.I != Prize) M4_FAIL(TEXT("money went %d -> %d, prize %d"), St.I, S->Money, Prize);
            St.Note += FString::Printf(TEXT("won: money %d -> %d; "), St.I, S->Money);
            St.I = S->Money;
            St.N = Enc(Test)->Encounters;
            PlaceToTalk(Test, P);
            M4_NEXT();
        }
        if (Test.Seconds - St.T0 > 100) M4_FAIL(TEXT("the fight never ended: %s"), *Menus(Test)->ScreenText());
        return EGameTest::Running;
    }
    if (St.Step == 3)
    {
        M4_WAIT(.3);
        Menus(Test)->TextLog.Reset();
        PressE(Test);
        M4_NEXT();
    }
    Advance(Test);
    M4_WAIT(2.0);
    if (Enc(Test)->Encounters != St.N) M4_FAIL(TEXT("the beaten trainer fought again"));
    if (S->Money != St.I) M4_FAIL(TEXT("money changed again: %d -> %d"), St.I, S->Money);
    M4_PASS(TEXT("%stalked to again: '%s', no fight, no second prize"), *St.Note, *Said(Test).Left(80));
}

// ------------------------------------------------------------------------------------------------ Centre, Mart, bag
// The nurse heals every Pokemon to full, clears status, and the Centre becomes where Red wakes after a blackout.
GAME_TEST(Heal_CentreFull, 90)
{
    static FState St;
    M4_READY();
    UPYRTrainerState* S = State(Test);
    Quiet(true, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 10}, {TEXT("pidgey"), 8}});
        AfterPokedex(Test);
        S->DebugHurtParty(.3f);
        S->Party[1].Status = EPYRStatus::Poison;
        S->Party[0].Hp = 0;
        APYRPerson* Nurse = Field(Test)->FindPersonBySprite(TEXT("VIRIDIAN_POKECENTER"), TEXT("NURSE"));
        if (!Nurse || !PlaceToTalk(Test, Nurse)) M4_FAIL(TEXT("no Viridian nurse to talk to"));
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.2);
        PressE(Test);
        M4_NEXT();
    }
    Advance(Test);
    if (!Idle(Test)) { if (Test.Seconds - St.T0 > 40) M4_FAIL(TEXT("the nurse never finished: %s"), *Menus(Test)->ScreenText()); return EGameTest::Running; }
    for (const FPYRMon& M : S->Party)
        if (M.Hp < M.MaxHp || M.Status != EPYRStatus::None) M4_FAIL(TEXT("%s at %.0f/%.0f, status %d after the nurse"), *M.Species.ToString(), M.Hp, M.MaxHp, int32(M.Status));
    if (S->LastCentre != TEXT("VIRIDIAN_POKECENTER")) M4_FAIL(TEXT("last Centre is %s"), *S->LastCentre.ToString());
    M4_PASS(TEXT("fainted Pikachu and poisoned Pidgey healed to full; last Centre %s; said: %s"), *S->LastCentre.ToString(), *Said(Test).Left(160));
}

// The Viridian Mart sells Poke Balls at 200 and Potions at 300; every Mart's stock is marts.json's.
GAME_TEST(Mart_BuyYellowStock, 120)
{
    static FState St;
    static TArray<FName> Marts;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRTrainerState* S = State(Test);
    Quiet(true, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 10}});
        AfterPokedex(Test);
        S->Money = 3000;
        S->SetItemCount(UPYRTrainerState::PokeBall, 0);
        APYRPerson* Clerk = Person(Test, TEXT("VIRIDIAN_MART"), TEXT("VIRIDIANMART_CLERK"));
        if (!Clerk || !PlaceToTalk(Test, Clerk)) M4_FAIL(TEXT("no Viridian clerk to talk to"));
        M4_NEXT();
    }
    if (St.Step == 1) { M4_WAIT(.2); PressE(Test); M4_NEXT(); }
    if (St.Step == 2)
    {
        // Through the greeting to BUY.
        if (!Menus(Test)->IsOpen(TEXT("mart"))) { Advance(Test); if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("no Mart menu")); return EGameTest::Running; }
        Press(Test, EPYRKey::Confirm);
        TSharedPtr<FPYRScreen> Buy = Menus(Test)->Find(TEXT("mart-buy"));
        if (!Buy) M4_FAIL(TEXT("BUY did not open the list"));
        const TArray<FString>& Rows = StaticCastSharedPtr<FPYRListScreen>(Buy)->Items;
        auto Row = [&Rows](int32 I) { return Rows.IsValidIndex(I) ? Rows[I].Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("  "), TEXT(" ")) : FString(); };
        if (!Row(0).Contains(TEXT("BALL")) || !Row(0).Contains(TEXT("¥200"))) M4_FAIL(TEXT("first row '%s', want POKé BALL ¥200"), *Row(0));
        if (!Row(1).Contains(TEXT("POTION")) || !Row(1).Contains(TEXT("¥300"))) M4_FAIL(TEXT("second row '%s', want POTION ¥300"), *Row(1));
        St.Note = FString::Printf(TEXT("Viridian: '%s', '%s'; "), *Row(0), *Row(1));
        // Buy three Poke Balls: the quantity up twice, YES.
        Press(Test, EPYRKey::Confirm);
        Press(Test, EPYRKey::Up);
        Press(Test, EPYRKey::Up);
        Press(Test, EPYRKey::Confirm);
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        if (S->ItemCount(UPYRTrainerState::PokeBall) == 0) { Advance(Test, true); if (Test.Seconds - St.T0 > 5) M4_FAIL(TEXT("the purchase never happened: %s"), *Menus(Test)->ScreenText()); return EGameTest::Running; }
        if (S->ItemCount(UPYRTrainerState::PokeBall) != 3 || S->Money != 2400) M4_FAIL(TEXT("bought %d balls, money %d (want 3 and 2400)"), S->ItemCount(UPYRTrainerState::PokeBall), S->Money);
        St.Note += TEXT("bought 3 balls for 600; ");
        Menus(Test)->CloseAll();
        D->Marts.GetKeys(Marts);
        St.I = 0;
        M4_NEXT();
    }
    // Every Mart's list against marts.json.
    if (St.I >= Marts.Num()) M4_PASS(TEXT("%s%d Marts match marts.json"), *St.Note, Marts.Num());
    const FName Mart = Marts[St.I];
    Menus(Test)->CloseAll();
    PYRScreens::OpenMart(Test.World, Mart);
    Press(Test, EPYRKey::Confirm);
    TSharedPtr<FPYRScreen> Buy = Menus(Test)->Find(TEXT("mart-buy"));
    if (!Buy) M4_FAIL(TEXT("no BUY list for %s"), *Mart.ToString());
    const TArray<FString>& Rows = StaticCastSharedPtr<FPYRListScreen>(Buy)->Items;
    const TArray<FName>& Stock = D->Marts[Mart];
    if (Rows.Num() != Stock.Num() + 1) M4_FAIL(TEXT("%s lists %d items, marts.json has %d"), *Mart.ToString(), Rows.Num() - 1, Stock.Num());
    for (int32 I = 0; I < Stock.Num(); ++I)
    {
        const FYItem* It = D->Item(Stock[I]);
        if (!It) M4_FAIL(TEXT("%s sells %s, not in items.json"), *Mart.ToString(), *Stock[I].ToString());
        if (!Rows[I].Contains(It->Name) || !Rows[I].Contains(PYRGb::Money(It->Price))) M4_FAIL(TEXT("%s row %d '%s', want %s %s"), *Mart.ToString(), I, *Rows[I], *It->Name, *PYRGb::Money(It->Price));
    }
    St.Note += FString::Printf(TEXT("%s %d items; "), *Mart.ToString(), Stock.Num());
    Menus(Test)->CloseAll();
    ++St.I;
    return EGameTest::Running;
}

// Selling pays half the price; key items can't be sold.
GAME_TEST(Mart_SellHalf, 90)
{
    static FState St;
    M4_READY();
    UPYRTrainerState* S = State(Test);
    const FYellowDataSet* D = Data(Test);
    Quiet(true, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 10}});
        AfterPokedex(Test);
        S->Bag.Reset();
        S->AddItem(UPYRTrainerState::Potion, 2);
        S->AddItem(UPYRTrainerState::OaksParcel, 1);
        S->Money = 1000;
        Place(Test, TEXT("VIRIDIAN_MART"), 3, 5, EYDir::Up);
        PYRScreens::OpenMart(Test.World, TEXT("VIRIDIAN_MART"));
        // SELL, the Potion, one, YES.
        Press(Test, EPYRKey::Down);
        Press(Test, EPYRKey::Confirm);
        if (!Menus(Test)->IsOpen(TEXT("mart-sell"))) M4_FAIL(TEXT("SELL did not open the bag: %s"), *Menus(Test)->ScreenText());
        Press(Test, EPYRKey::Confirm);
        Press(Test, EPYRKey::Confirm);
        M4_NEXT();
    }
    const int32 Price = D->Item(UPYRTrainerState::Potion) ? D->Item(UPYRTrainerState::Potion)->Price : 300;
    if (St.Step == 1)
    {
        if (S->ItemCount(UPYRTrainerState::Potion) == 2) { Advance(Test, true); if (Test.Seconds - St.T0 > 5) M4_FAIL(TEXT("the sale never happened: %s"), *Menus(Test)->ScreenText()); return EGameTest::Running; }
        if (S->Money != 1000 + Price / 2) M4_FAIL(TEXT("sold a Potion (price %d) for %d"), Price, S->Money - 1000);
        St.Note = FString::Printf(TEXT("a Potion (price %d) sold for %d; "), Price, S->Money - 1000);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        // Back on the sell list: down to OAK's PARCEL.
        TSharedPtr<FPYRScreen> Sell = Menus(Test)->Find(TEXT("mart-sell"));
        if (!Sell) M4_FAIL(TEXT("the sell list closed: %s"), *Menus(Test)->ScreenText());
        while (Menus(Test)->Top() != Sell) Menus(Test)->Pop();
        Menus(Test)->TextLog.Reset();
        const int32 ParcelAt = S->Bag.IndexOfByPredicate([](const FPYRItemStack& I) { return I.Item == UPYRTrainerState::OaksParcel; });
        Sell->Cursor = ParcelAt;
        Press(Test, EPYRKey::Confirm);
        M4_NEXT();
    }
    M4_WAIT(.3);
    if (!S->HasItem(UPYRTrainerState::OaksParcel)) M4_FAIL(TEXT("OAK's PARCEL was sold"));
    const FString No = FirstLineOf(PYRGb::Fill(Test.World, PYRSysText::Get(TEXT("PokemartUnsellableItemText"))));
    if (!SaidContains(Test, No)) M4_FAIL(TEXT("selling the parcel said '%s', want '%s'"), *Said(Test), *No);
    M4_PASS(TEXT("%sOAK's PARCEL: '%s'"), *St.Note, *No);
}

// Items used from the bag in the field: a Potion heals, a Poke Ball is not for now, Pikachu refuses the Thunder Stone.
GAME_TEST(Bag_UseItemOutside, 90)
{
    static FState St;
    M4_READY();
    UPYRTrainerState* S = State(Test);
    Quiet(true, true);
    auto UseFirst = [&Test]()
    {
        PYRScreens::OpenBag(Test.World);
        Press(Test, EPYRKey::Confirm);   // the first slot
        Press(Test, EPYRKey::Confirm);   // USE
    };
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 10}});
        AfterPokedex(Test);
        Place(Test, TEXT("ROUTE_1"), 9, 26, EYDir::Up);
        S->Bag.Reset();
        S->AddItem(UPYRTrainerState::Potion, 2);
        S->Party[0].Hp = 5;
        Menus(Test)->TextLog.Reset();
        UseFirst();
        Press(Test, EPYRKey::Confirm);   // Pikachu
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.2);
        if (S->Party[0].Hp != FMath::Min(S->Party[0].MaxHp, 25.f)) M4_FAIL(TEXT("a Potion left Pikachu at %.0f HP (from 5)"), S->Party[0].Hp);
        if (S->ItemCount(UPYRTrainerState::Potion) != 1) M4_FAIL(TEXT("the Potion was not used up"));
        St.Note = FString::Printf(TEXT("Potion: 5 -> %.0f HP ('%s'); "), S->Party[0].Hp, *Said(Test).Left(60));
        Menus(Test)->CloseAll();
        S->Bag.Reset();
        S->AddItem(UPYRTrainerState::PokeBall, 1);
        Menus(Test)->TextLog.Reset();
        UseFirst();
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.2);
        const FString NotTime = FirstLineOf(PYRGb::Fill(Test.World, PYRSysText::Get(TEXT("ItemUseNotTimeText"))));
        if (!SaidContains(Test, NotTime) || S->ItemCount(UPYRTrainerState::PokeBall) != 1) M4_FAIL(TEXT("a Poke Ball in the field said '%s'"), *Said(Test));
        St.Note += FString::Printf(TEXT("Poke Ball: '%s'; "), *NotTime);
        Menus(Test)->CloseAll();
        S->Bag.Reset();
        S->AddItem(TEXT("thunderstone"), 1);
        Menus(Test)->TextLog.Reset();
        UseFirst();
        Press(Test, EPYRKey::Confirm);   // Pikachu
        M4_NEXT();
    }
    M4_WAIT(.2);
    if (S->Party[0].Species != TEXT("pikachu") || S->ItemCount(TEXT("thunderstone")) != 1) M4_FAIL(TEXT("Pikachu took the Thunder Stone"));
    if (Menus(Test)->IsOpen(TEXT("evolution"))) M4_FAIL(TEXT("the evolution scene started for Pikachu"));
    M4_PASS(TEXT("%sThunder Stone on Pikachu: '%s'"), *St.Note, *Said(Test).Left(60));
}

// ------------------------------------------------------------------------------------------------ events.json, played
// Every gift and story fight from Pallet Town to Cerulean, played the way a player meets it (talking to the person,
// walking onto the trigger tile, facing the machine), each checked against its own events.json gives, sets and takes.
namespace
{
enum class EEvWay : uint8 { Talk, Walk, Tile, Check };
struct FEvPlay { const TCHAR* Id; EEvWay Way; bool bFresh; int32 X; int32 Y; };

struct FEvRun
{
    int32 Index = 0, Phase = 0, MoneyBefore = 0, RanBefore = 0, Retries = 0;
    double T0 = 0, IdleSince = -1, PressedAt = 0, LastLog = 0;
    FIntPoint Target = FIntPoint(-1, -1);
    FString Map;
    TArray<FString> Done, Bad;
};

TArray<FString> Strings(const TSharedPtr<FJsonObject>& E, const TCHAR* FieldName)
{
    TArray<FString> Out;
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (E.IsValid() && E->TryGetArrayField(FieldName, A))
        for (const TSharedPtr<FJsonValue>& V : *A) if (V->Type == EJson::String) Out.Add(V->AsString());
    return Out;
}

TArray<TSharedPtr<FJsonObject>> Objects(const TSharedPtr<FJsonObject>& E, const TCHAR* FieldName)
{
    TArray<TSharedPtr<FJsonObject>> Out;
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (E.IsValid() && E->TryGetArrayField(FieldName, A))
        for (const TSharedPtr<FJsonValue>& V : *A) if (V->Type == EJson::Object) Out.Add(V->AsObject());
    return Out;
}

// Sets the scene for one event; false (and Why) when it can't be set.
bool SetUpEvent(FGameTestContext& Test, const FEvPlay& P, FEvRun& R, FString& Why)
{
    TSharedPtr<FJsonObject> E = Scripts(Test)->Event(P.Id);
    if (!E.IsValid()) { Why = TEXT("not in events.json"); return false; }
    UPYRTrainerState* S = State(Test);
    UPYRProgress* G = Progress(Test);
    Menus(Test)->CloseAll();
    if (P.bFresh)
    {
        Party(Test, {{TEXT("pikachu"), 60}});
        AfterPokedex(Test);
        S->Bag.Reset();
        S->Money = 5000;
        S->BondPoints = 200;
    }
    for (const FString& F : Strings(E, TEXT("requires"))) G->Set(FName(*F));
    for (const FString& F : Strings(E, TEXT("requires_not"))) G->Set(FName(*F), false);
    PYRScripts::Refresh(Test.World);
    R.MoneyBefore = S->Money;
    R.RanBefore = Scripts(Test)->Ran.Num();
    R.Map = E->GetStringField(TEXT("map"));
    const FYMap* M = Data(Test)->Map(FName(*R.Map));
    if (!M) { Why = TEXT("no map ") + R.Map; return false; }
    if (P.Way == EEvWay::Talk)
    {
        FString Npc;
        E->TryGetStringField(TEXT("npc"), Npc);
        APYRPerson* Who = Person(Test, *R.Map, *Npc);
        if (!Who) { Why = TEXT("no person ") + Npc; return false; }
        if (!Who->IsShown()) { Why = Npc + TEXT(" is hidden"); return false; }
        if (!PlaceToTalk(Test, Who)) { Why = TEXT("no tile to talk to ") + Npc; return false; }
        return true;
    }
    if (P.Way == EEvWay::Tile)
    {
        if (!Place(Test, FName(*R.Map), P.X, P.Y, EYDir::Up)) { Why = TEXT("could not place Red"); return false; }
        return true;
    }
    if (P.Way == EEvWay::Check) return true;
    // Walk: from the open tile next to the first trigger, onto it.
    TArray<FIntPoint> Triggers;
    for (const TSharedPtr<FJsonObject>& T : Objects(E, TEXT("trigger_coords"))) Triggers.Add(FIntPoint(T->GetIntegerField(TEXT("x")), T->GetIntegerField(TEXT("y"))));
    if (!Triggers.Num()) { Why = TEXT("no trigger tile"); return false; }
    R.Target = Triggers[0];
    for (EYDir D : {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right})
    {
        const FIntPoint From = R.Target - YDir::Delta(D);
        if (!M->Contains(From.X, From.Y) || Triggers.Contains(From) || World(Test)->IsSolidAt(M->Origin + From)) continue;
        if (Place(Test, FName(*R.Map), From.X, From.Y, D)) return true;
    }
    Why = TEXT("no open tile next to the trigger");
    return false;
}

// What went wrong with an event's gives, sets and takes ("" when all hold).
FString CheckEvent(FGameTestContext& Test, const FString& Id, const FEvRun& R)
{
    TSharedPtr<FJsonObject> E = Scripts(Test)->Event(Id);
    UPYRTrainerState* S = State(Test);
    TArray<FString> Bad;
    for (const TSharedPtr<FJsonObject>& Give : Objects(E, TEXT("gives")))
    {
        FString What;
        if (Give->TryGetStringField(TEXT("item"), What))
        {
            const int32 Want = Give->HasField(TEXT("count")) ? int32(Give->GetIntegerField(TEXT("count"))) : 1;
            if (S->ItemCount(YKey::Item(What)) < Want) Bad.Add(FString::Printf(TEXT("no %s x%d"), *What, Want));
        }
        else if (Give->TryGetStringField(TEXT("pokemon"), What))
        {
            const FName Species(*What.ToLower());
            bool bHas = false;
            for (const FPYRMon& Mon : S->Party) bHas |= Mon.Species == Species;
            if (!bHas && !S->Caught.Contains(Species)) Bad.Add(TEXT("no ") + What);
        }
    }
    for (const FString& F : Strings(E, TEXT("sets"))) if (!Progress(Test)->Has(FName(*F))) Bad.Add(TEXT("flag ") + F + TEXT(" unset"));
    for (const TSharedPtr<FJsonObject>& Set : Objects(E, TEXT("sets")))
    {
        FString Badge;
        if (!Set->TryGetStringField(TEXT("badge"), Badge)) continue;
        const bool bHas = Badge == TEXT("BOULDERBADGE") ? S->HasBadge(EPYRBadge::Boulder) : Badge == TEXT("CASCADEBADGE") ? S->HasBadge(EPYRBadge::Cascade) : false;
        if (!bHas) Bad.Add(TEXT("no ") + Badge);
    }
    for (const TSharedPtr<FJsonObject>& Take : Objects(E, TEXT("takes")))
        if (Take->HasField(TEXT("money")) && S->Money != R.MoneyBefore - int32(Take->GetIntegerField(TEXT("money"))))
            Bad.Add(FString::Printf(TEXT("money %d -> %d"), R.MoneyBefore, S->Money));
    // Red walks again afterwards (a nested gift sequence once left him frozen).
    if (Field(Test)->IsLocked()) Bad.Add(TEXT("Red is still locked"));
    return FString::Join(Bad, TEXT(", "));
}

// Plays the list one event at a time; Passed when all ran as events.json says, Failed with every one that didn't.
EGameTest PlayEvents(FGameTestContext& Test, const TArray<FEvPlay>& List, FEvRun& R, const TCHAR* What)
{
    if (R.Index >= List.Num())
    {
        if (R.Bad.Num()) M4_FAIL(TEXT("%d of %d %s wrong: %s | right: %s"), R.Bad.Num(), List.Num(), What, *FString::Join(R.Bad, TEXT("; ")), *FString::Join(R.Done, TEXT(", ")));
        M4_PASS(TEXT("%d %s, each as events.json says: %s"), List.Num(), What, *FString::Join(R.Done, TEXT(", ")));
    }
    const FEvPlay& P = List[R.Index];
    auto Next = [&R, &Test]() { ++R.Index; R.Phase = 0; R.T0 = Test.Seconds; R.IdleSince = -1; R.Retries = 0; };
    if (R.Phase == 0)
    {
        FString Why;
        if (!SetUpEvent(Test, P, R, Why)) { R.Bad.Add(FString::Printf(TEXT("%s: %s"), P.Id, *Why)); Next(); return EGameTest::Running; }
        R.Phase = 1;
        R.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    // A script started since the scene was set (some events run under their own sequence name, e.g. daisy-map).
    const TArray<FString>& Ran = Scripts(Test)->Ran;
    const bool bRan = P.Way == EEvWay::Check || Ran.Num() > R.RanBefore;
    if (R.Phase == 1)
    {
        if (Test.Seconds - R.T0 < .3) return EGameTest::Running;
        if (P.Way == EEvWay::Talk || P.Way == EEvWay::Tile) { PressE(Test); R.PressedAt = Test.Seconds; R.Phase = 2; }
        else if (P.Way == EEvWay::Walk)
        {
            if (bRan) { StopWalking(Test); R.Phase = 2; }
            else if (!Scripts(Test)->IsRunning() && !Menus(Test)->AnyOpen()) WalkToTile(Test, FName(*R.Map), R.Target.X, R.Target.Y, 8.f);
        }
        else R.Phase = 2;
        if (R.Phase == 1 && Test.Seconds - R.T0 > 20)
        {
            StopWalking(Test);
            R.Bad.Add(FString::Printf(TEXT("%s: never started (at %s %d,%d)"), P.Id, *Field(Test)->CurrentMap.ToString(), Field(Test)->CurrentTile.X, Field(Test)->CurrentTile.Y));
            Next();
        }
        return EGameTest::Running;
    }
    Advance(Test, true);
    WinFight(Test);
    if (bRan && Idle(Test))
    {
        if (R.IdleSince < 0) R.IdleSince = Test.Seconds;
        if (Test.Seconds - R.IdleSince > .8)
        {
            const FString Wrong = CheckEvent(Test, P.Id, R);
            const FString Names = FString::Join(TArray<FString>(Ran.GetData() + R.RanBefore, Ran.Num() - R.RanBefore), TEXT("+"));
            if (Wrong.IsEmpty()) R.Done.Add(FString::Printf(TEXT("%s (%s)"), P.Id, *Names));
            else R.Bad.Add(FString::Printf(TEXT("%s: %s (ran %s)"), P.Id, *Wrong, *Names));
            Next();
        }
        return EGameTest::Running;
    }
    R.IdleSince = -1;
    // Nothing answered E (a walker stepped off): face the person again and press once more.
    if (P.Way == EEvWay::Talk && !bRan && Idle(Test) && Test.Seconds - R.PressedAt > 2.0 && R.Retries < 4)
    {
        FString Npc;
        Scripts(Test)->Event(P.Id)->TryGetStringField(TEXT("npc"), Npc);
        APYRPerson* Who = Person(Test, *R.Map, *Npc);
        UE_LOG(LogYellow, Display, TEXT("EVENT_PLAY %s: nothing answered E at %s (%d,%d), facing %s; talking again"), P.Id, *Field(Test)->CurrentMap.ToString(),
            Field(Test)->CurrentTile.X, Field(Test)->CurrentTile.Y, Who ? *FString::Printf(TEXT("%s at %d,%d"), *Npc, Who->Tile.X, Who->Tile.Y) : TEXT("nobody"));
        if (Who) PlaceToTalk(Test, Who);
        ++R.Retries;
        R.Phase = 1;
        return EGameTest::Running;
    }
    if (Test.Seconds - R.LastLog > 5)
    {
        R.LastLog = Test.Seconds;
        UE_LOG(LogYellow, Display, TEXT("EVENT_PLAY %s: waiting (ran %d, running '%s', locked %d, menus '%s')"), P.Id, bRan ? 1 : 0, *Scripts(Test)->RunningName(),
            Field(Test)->IsLocked() ? 1 : 0, *Menus(Test)->ScreenText().Left(80));
    }
    if (Test.Seconds - R.T0 > 90)
    {
        R.Bad.Add(FString::Printf(TEXT("%s: never finished (%s) %s"), P.Id, bRan ? TEXT("ran") : TEXT("never ran"), *Menus(Test)->ScreenText().Left(80)));
        Menus(Test)->CloseAll();
        Next();
    }
    return EGameTest::Running;
}
}

// The gifts: the Potion sample, Oak's Poke Balls, Daisy's Town Map, TM42, the Old Amber, a fossil, the Magikarp
// salesman, Melanie's Bulbasaur and the Route 24 Charmander.
GAME_TEST(Story_GiftsFromYellow, 360)
{
    static FEvRun R;
    M4_READY();
    Quiet(true, false);
    static const TArray<FEvPlay> List = {
        {TEXT("route1_potion_sample"), EEvWay::Talk, true, 0, 0},
        {TEXT("oaks_lab_poke_balls"), EEvWay::Talk, true, 0, 0},
        {TEXT("daisy_town_map"), EEvWay::Talk, true, 0, 0},
        {TEXT("viridian_fisher_tm42"), EEvWay::Talk, true, 0, 0},
        {TEXT("pewter_old_amber"), EEvWay::Talk, true, 0, 0},
        {TEXT("mt_moon_dome_fossil"), EEvWay::Talk, true, 0, 0},
        {TEXT("mt_moon_magikarp_salesman"), EEvWay::Talk, true, 0, 0},
        {TEXT("cerulean_bulbasaur_gift"), EEvWay::Talk, true, 0, 0},
        {TEXT("route24_charmander_gift"), EEvWay::Talk, true, 0, 0},
    };
    return PlayEvents(Test, List, R, TEXT("gifts"));
}

// The story fights and what they give: Brock, Misty, the rival on Route 22 and in Cerulean, Mt. Moon's Super Nerd and
// Jessie and James, the Cerulean thief and his TM28, Nugget Bridge, and Bill's Cell Separator to the S.S. Ticket.
GAME_TEST(Story_FightsFromYellow, 540)
{
    static FEvRun R;
    M4_READY();
    Quiet(true, false);
    static const TArray<FEvPlay> List = {
        {TEXT("pewter_gym_brock_tm34"), EEvWay::Talk, true, 0, 0},
        {TEXT("cerulean_gym_misty_tm11"), EEvWay::Talk, true, 0, 0},
        {TEXT("route22_rival_battle_1"), EEvWay::Walk, true, 0, 0},
        {TEXT("cerulean_rival_battle"), EEvWay::Walk, true, 0, 0},
        {TEXT("mt_moon_b2f_super_nerd_battle"), EEvWay::Walk, true, 0, 0},
        {TEXT("mt_moon_jessie_james"), EEvWay::Walk, true, 0, 0},
        {TEXT("cerulean_rocket_thief_battle"), EEvWay::Walk, true, 0, 0},
        {TEXT("cerulean_rocket_tm28"), EEvWay::Check, false, 0, 0},
        {TEXT("route24_nugget_bridge_prize"), EEvWay::Walk, true, 0, 0},
        {TEXT("bills_house_bill_asks_help"), EEvWay::Talk, true, 0, 0},
        {TEXT("bills_house_cell_separator_pc"), EEvWay::Tile, false, 1, 5},
        {TEXT("bills_house_ss_ticket"), EEvWay::Talk, false, 0, 0},
    };
    return PlayEvents(Test, List, R, TEXT("story fights"));
}

// Losing every fight: "blacked out", Red wakes in front of the last Centre's nurse with half his money and a healed party.
GAME_TEST(Blackout_WakeAtCentre, 120)
{
    static FState St;
    M4_READY();
    UPYRTrainerState* S = State(Test);
    Quiet(true, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 8}, {TEXT("pidgey"), 5}});
        AfterPokedex(Test);
        S->Money = 1000;
        S->SetLastCentre(TEXT("VIRIDIAN_POKECENTER"));
        Place(Test, TEXT("ROUTE_1"), 9, 26, EYDir::Up);
        St.I = Enc(Test)->Blackouts;
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.3);
        if (!Enc(Test)->BeginWild(TEXT("rattata"), 4)) M4_FAIL(TEXT("no wild battle"));
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        APYRBattleManager* BM = APYRBattleManager::Get(Test.World);
        if (BM && BM->IsBattleActive() && !BM->IsEnding())
        {
            for (FPYRMon& M : S->Party) M.Hp = 0;
            BM->DebugFinish(EPYRBattleOutcome::Lost);
            M4_NEXT();
        }
        if (Test.Seconds - St.T0 > 20) M4_FAIL(TEXT("the battle never started"));
        return EGameTest::Running;
    }
    Advance(Test);
    if (Enc(Test)->Blackouts == St.I || !Idle(Test))
    {
        if (Test.Seconds - St.T0 > 40) M4_FAIL(TEXT("no blackout (%d): %s"), Enc(Test)->Blackouts, *Menus(Test)->ScreenText());
        return EGameTest::Running;
    }
    TArray<FString> Bad;
    if (Field(Test)->CurrentMap != TEXT("VIRIDIAN_POKECENTER")) Bad.Add(TEXT("woke on ") + Field(Test)->CurrentMap.ToString());
    if (S->Money != 500) Bad.Add(FString::Printf(TEXT("money %d, not 500"), S->Money));
    for (const FPYRMon& M : S->Party) if (M.Hp < M.MaxHp) Bad.Add(FString::Printf(TEXT("%s at %.0f/%.0f"), *M.Species.ToString(), M.Hp, M.MaxHp));
    if (!SaidContains(Test, TEXT("blacked"))) Bad.Add(TEXT("never said 'blacked out'"));
    if (Bad.Num()) M4_FAIL(TEXT("%s"), *FString::Join(Bad, TEXT(", ")));
    M4_PASS(TEXT("'blacked out'; woke in VIRIDIAN_POKECENTER (%d,%d), money 1000 -> 500, party healed"), Field(Test)->CurrentTile.X, Field(Test)->CurrentTile.Y);
}

// ------------------------------------------------------------------------------------------------ m4q names
// The m4q task's names for the roadblock checks (each a person with a line: Oak, the Mart clerk, the old man, the boy).
static struct FM4QStoryNames
{
    FM4QStoryNames()
    {
        GameTest::Register(TEXT("Roadblock_OakStopsRedAtGrass"), &GameTest_Roadblock_OakStopsAtGrass, 240);
        GameTest::Register(TEXT("Roadblock_MartParcelThenSells"), &GameTest_Roadblock_ViridianMartParcel, 210);
        GameTest::Register(TEXT("Roadblock_OldManBlocksUntilPokedex"), &GameTest_Roadblock_OldManNeedsPokedex, 300);
        GameTest::Register(TEXT("Roadblock_PewterBoyLeadsToGym"), &GameTest_Roadblock_PewterBoyUntilBadge, 210);
    }
} GM4QStoryNames;
