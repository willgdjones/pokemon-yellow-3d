// M4 wild Pokemon and the live battles on the stages: Yellow's encounter tables and rates, the flash into the stage,
// fleeing back to the tile, Repel; the camera over the stage, Gen 1 damage, the Boulder Badge; Pikachu outside its
// ball and its face; growing (evolution scene, TMs).
#include "Tests/M4TestKit.h"

#include "Battle/BattleTestKit.h"
#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Encounters/PYRRing.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Partner/PYRBond.h"
#include "Partner/PYRPikachuFollower.h"
#include "Party/PYRSpecies.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Stage/PYREncounters.h"
#include "Stage/PYRStage.h"

using namespace M4;

namespace
{
UPYREncounters* Enc(FGameTestContext& Test) { return UPYREncounters::Get(Test.World); }
APYRBattleManager* BM(FGameTestContext& Test) { return APYRBattleManager::Get(Test.World); }

// The first grass (or cave floor) tile of a map with open ground on its west to step in from.
bool GrassStep(const FYMap& M, FIntPoint& OutFrom, FIntPoint& OutGrass)
{
    const TCHAR Want = M.bCave ? TEXT('c') : TEXT('g');
    for (int32 Y = 0; Y < M.Height; ++Y)
        for (int32 X = 1; X < M.Width; ++X)
            if (M.Tile(X, Y) == Want && !YTile::IsSolid(M.Tile(X - 1, Y)) && M.WarpAt(X - 1, Y) == INDEX_NONE && M.WarpAt(X, Y) == INDEX_NONE)
            {
                OutFrom = FIntPoint(X - 1, Y);
                OutGrass = FIntPoint(X, Y);
                return true;
            }
    return false;
}

// A forced wild encounter by stepping onto grass in Map (the next step always meets one; the table picks who).
bool StartGrassEncounter(FGameTestContext& Test, FName MapId, FIntPoint& OutGrass)
{
    const FYMap* M = Data(Test)->Map(MapId);
    FIntPoint From;
    if (!M || !GrassStep(*M, From, OutGrass)) return false;
    Place(Test, MapId, From.X, From.Y, EYDir::Right);
    Enc(Test)->bForceEncounter = true;
    return true;
}

double SlotShare(int32 Slot)
{
    static const int32 Chances[] = {51, 51, 39, 25, 25, 25, 13, 13, 11, 3};
    return Chances[Slot] / 256.0;
}
}

// ------------------------------------------------------------------------------------------------ tables and rates
// 5000 encounters per map: every one is a wild.json species at its level, in Yellow's slot shares within 2%.
GAME_TEST(Wild_OnlyYellowSpecies, 120)
{
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    // wild.json straight from the file, to check the loader too.
    FString Text;
    TSharedPtr<FJsonObject> Json;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(UYellowData::DefaultRoot(), TEXT("wild.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json))
        M4_FAIL(TEXT("YellowData missing: wild.json"));
    FString Note;
    int32 Maps = 0;
    for (const auto& Pair : Json->Values)
    {
        const FName MapId(*Pair.Key);
        const TSharedPtr<FJsonObject> Obj = Pair.Value->AsObject();
        const TArray<TSharedPtr<FJsonValue>>* Grass = nullptr;
        if (!Obj.IsValid() || !Obj->TryGetArrayField(TEXT("grass"), Grass) || Grass->Num() == 0) continue;
        const FYWild* W = D->Wild.Find(MapId);
        if (!W || W->Grass.Num() != Grass->Num()) M4_FAIL(TEXT("%s: %d grass slots loaded for %d in wild.json"), *MapId.ToString(), W ? W->Grass.Num() : 0, Grass->Num());
        for (int32 I = 0; I < Grass->Num(); ++I)
        {
            const TArray<TSharedPtr<FJsonValue>>& S = (*Grass)[I]->AsArray();
            if (W->Grass[I].Level != int32(S[0]->AsNumber()) || W->Grass[I].Species != YKey::Species(S[1]->AsString()))
                M4_FAIL(TEXT("%s slot %d is %s L%d, wild.json says %s L%d"), *MapId.ToString(), I, *W->Grass[I].Species.ToString(), W->Grass[I].Level, *S[1]->AsString(), int32(S[0]->AsNumber()));
            if (!PYRData::Species(W->Grass[I].Species)) M4_FAIL(TEXT("%s slot %d: %s is unknown to the battle tables"), *MapId.ToString(), I, *W->Grass[I].Species.ToString());
        }
        // 5000 encounters rolled the way a step does, rate aside.
        FRandomStream Rng(0x6a11 + Maps);
        TArray<int32> Hits;
        Hits.SetNumZeroed(W->Grass.Num());
        for (int32 N = 0; N < 5000; ++N) ++Hits[FMath::Min(UPYREncounters::SlotFor(Rng.RandRange(0, 255)), W->Grass.Num() - 1)];
        for (int32 I = 0; I < W->Grass.Num(); ++I)
        {
            const double Share = Hits[I] / 5000.0;
            if (FMath::Abs(Share - SlotShare(I)) > .02) M4_FAIL(TEXT("%s slot %d came up %.3f, Yellow gives %.3f"), *MapId.ToString(), I, Share, SlotShare(I));
        }
        ++Maps;
        Note += FString::Printf(TEXT("%s(%d) "), *MapId.ToString(), W->Grass.Num());
    }
    if (Maps == 0) M4_FAIL(TEXT("wild.json has no grass tables"));
    M4_PASS(TEXT("%d maps x 5000 encounters, species and levels from wild.json, slot shares within 2%%: %s"), Maps, *Note);
}

// 2000 grass steps on Route 1 meet a Pokemon about rate/256 of the time; 2000 path steps never do.
GAME_TEST(Wild_RandomAtRate, 60)
{
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    const FYMap* M = D->Map(TEXT("ROUTE_1"));
    if (!M) M4_FAIL(TEXT("no ROUTE_1"));
    const int32 Rate = UPYREncounters::RateFor(*D, *M);
    if (Rate <= 0) M4_FAIL(TEXT("Route 1 has no encounter rate"));
    FRandomStream Rng(0x51e9);
    int32 Grass = 0, Path = 0;
    FYSlot Slot;
    for (int32 I = 0; I < 2000; ++I) Grass += UPYREncounters::RollStep(*D, *M, TEXT('g'), Rng, Slot) ? 1 : 0;
    for (int32 I = 0; I < 2000; ++I) Path += UPYREncounters::RollStep(*D, *M, TEXT('.'), Rng, Slot) ? 1 : 0;
    const double Want = Rate / 256.0, Got = Grass / 2000.0;
    // Four standard deviations of 2000 draws.
    const double Tol = 4.0 * FMath::Sqrt(Want * (1 - Want) / 2000.0);
    if (FMath::Abs(Got - Want) > Tol) M4_FAIL(TEXT("grass met %.3f per step, rate %d/256 = %.3f (tolerance %.3f)"), Got, Rate, Want, Tol);
    if (Path != 0) M4_FAIL(TEXT("%d encounters on the path"), Path);
    M4_PASS(TEXT("Route 1 rate %d/256 = %.3f; 2000 grass steps met %d (%.3f); 2000 path steps met 0"), Rate, Want, Grass, Got);
}

// Mt. Moon's cave floor meets Pokemon at its own rate, and a meeting there goes to the cave stage.
GAME_TEST(Wild_CaveEncounters, 90)
{
    static FState St;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    const FYMap* M = D->Map(TEXT("MT_MOON_1F"));
    Quiet(false, true);
    if (St.Step == 0)
    {
        if (!M || !M->bCave) M4_FAIL(TEXT("MT_MOON_1F is not a cave"));
        const int32 Rate = UPYREncounters::RateFor(*D, *M);
        FRandomStream Rng(0xcafe);
        int32 Met = 0;
        FYSlot Slot;
        for (int32 I = 0; I < 2000; ++I) Met += UPYREncounters::RollStep(*D, *M, TEXT('c'), Rng, Slot) ? 1 : 0;
        if (Rate <= 0 || Met == 0) M4_FAIL(TEXT("cave floor: rate %d, %d meetings in 2000 steps"), Rate, Met);
        St.Note = FString::Printf(TEXT("cave floor rate %d/256: %d in 2000 steps; "), Rate, Met);
        Party(Test, {{TEXT("pikachu"), 20}});
        AfterPokedex(Test);
        FIntPoint Grass;
        if (!StartGrassEncounter(Test, M->Id, Grass)) M4_FAIL(TEXT("no cave floor to step on"));
        St.P = Grass;
        M4_NEXT();
    }
    if (Enc(Test)->GetPhase() != EPYREncounterPhase::Fight)
    {
        WalkToTile(Test, M->Id, St.P.X, St.P.Y, 5.f);
        if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("no meeting on the cave floor (phase %d)"), int32(Enc(Test)->GetPhase()));
        return EGameTest::Running;
    }
    StopWalking(Test);
    const FYWild* W = D->Wild.Find(M->Id);
    const FName Foe = Enc(Test)->GetFight().Foes[0].Species;
    if (!W || !W->Grass.ContainsByPredicate([Foe](const FYSlot& S) { return S.Species == Foe; })) M4_FAIL(TEXT("met %s, not in Mt. Moon's table"), *Foe.ToString());
    if (!Enc(Test)->GetCurrentStage() || Enc(Test)->GetCurrentStage()->Kind != EPYRStageKind::Cave) M4_FAIL(TEXT("the cave meeting is not on the cave stage"));
    M4_PASS(TEXT("%smet %s L%d on the cave stage"), *St.Note, *Foe.ToString(), Enc(Test)->GetFight().Foes[0].Level);
}

// The flash plays, then Red, Pikachu and the foe stand on the grass stage in this same world.
GAME_TEST(Wild_FlashThenStage, 90)
{
    static FState St;
    M4_READY();
    UPYREncounters* E = Enc(Test);
    Quiet(false, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 8}});
        AfterPokedex(Test);
        if (!StartGrassEncounter(Test, TEXT("ROUTE_1"), St.P)) M4_FAIL(TEXT("no Route 1 grass"));
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        if (E->GetPhase() == EPYREncounterPhase::Idle) { WalkToTile(Test, TEXT("ROUTE_1"), St.P.X, St.P.Y, 5.f); if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("no encounter in the grass")); return EGameTest::Running; }
        StopWalking(Test);
        if (E->GetPhase() == EPYREncounterPhase::Flash) St.A = FMath::Max(St.A, Field(Test)->GetFade());
        if (E->GetPhase() != EPYREncounterPhase::Fight) return EGameTest::Running;
        if (E->Flashes < 3) M4_FAIL(TEXT("%d flashes before the stage"), E->Flashes);
        St.Note = FString::Printf(TEXT("%d white flashes (fade peak %.2f); "), E->Flashes, St.A);
        M4_NEXT();
    }
    M4_WAIT(1.5);
    const APYRBattleStage* Stage = E->GetCurrentStage();
    APYRMonPawn* Mine = BM(Test)->GetPlayerMon();
    APYRMonPawn* Foe = BM(Test)->GetFoeMon();
    APokemonYellowReimagineCharacter* R = Red(Test);
    if (!Stage || Stage->Kind != EPYRStageKind::Grass) M4_FAIL(TEXT("not on the grass stage"));
    if (!Mine || !Foe) M4_FAIL(TEXT("no fighters"));
    for (const AActor* A : {static_cast<const AActor*>(R), static_cast<const AActor*>(Mine), static_cast<const AActor*>(Foe)})
    {
        if (A->GetWorld() != Test.World) M4_FAIL(TEXT("%s is in another world"), *A->GetName());
        if (FVector::Dist2D(A->GetActorLocation(), Stage->Centre) > Stage->Radius + 50.f || FMath::Abs(A->GetActorLocation().Z - Stage->Centre.Z) > 400.f)
            M4_FAIL(TEXT("%s at %s is off the stage at %s"), *A->GetName(), *A->GetActorLocation().ToCompactString(), *Stage->Centre.ToCompactString());
    }
    if (Mine->Species != TEXT("pikachu")) M4_FAIL(TEXT("Red's fighter is %s"), *Mine->Species.ToString());
    M4_PASS(TEXT("%sRed, Pikachu and a wild %s on the grass stage %.0f m below the road, in the one world"), *St.Note, *Foe->Species.ToString(), -Stage->Centre.Z / 100.f);
}

// Forest, cave and grass each have their stage.
GAME_TEST(Wild_StageMatchesPlace, 120)
{
    static FState St;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYREncounters* E = Enc(Test);
    Quiet(false, true);
    static const TCHAR* Places[] = {TEXT("VIRIDIAN_FOREST"), TEXT("MT_MOON_1F"), TEXT("ROUTE_1")};
    static const EPYRStageKind Kinds[] = {EPYRStageKind::Forest, EPYRStageKind::Cave, EPYRStageKind::Grass};
    if (St.Step == 0)
    {
        for (int32 I = 0; I < 3; ++I)
            if (UPYREncounters::StageForMap(D->Map(Places[I])) != Kinds[I]) M4_FAIL(TEXT("%s maps to the %s stage"), Places[I], APYRBattleStage::Name(UPYREncounters::StageForMap(D->Map(Places[I]))));
        Party(Test, {{TEXT("pikachu"), 30}});
        AfterPokedex(Test);
        St.Step = 1;
        St.I = 0;
    }
    if (St.I >= 3) M4_PASS(TEXT("%s"), *St.Note);
    if (St.Step == 1)
    {
        if (!StartGrassEncounter(Test, Places[St.I], St.P)) M4_FAIL(TEXT("no grass in %s"), Places[St.I]);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        if (E->GetPhase() != EPYREncounterPhase::Fight)
        {
            WalkToTile(Test, Places[St.I], St.P.X, St.P.Y, 5.f);
            if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("no encounter in %s"), Places[St.I]);
            return EGameTest::Running;
        }
        StopWalking(Test);
        if (!E->GetCurrentStage() || E->GetCurrentStage()->Kind != Kinds[St.I]) M4_FAIL(TEXT("%s's wild fight is on the %s stage"), Places[St.I], E->GetCurrentStage() ? APYRBattleStage::Name(E->GetCurrentStage()->Kind) : TEXT("no"));
        St.Note += FString::Printf(TEXT("%s: %s stage; "), Places[St.I], APYRBattleStage::Name(Kinds[St.I]));
        BM(Test)->RequestFlee();
        M4_NEXT();
    }
    Advance(Test);
    if (E->IsBusy()) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("the flight from %s never ended"), Places[St.I]); return EGameTest::Running; }
    ++St.I;
    St.Step = 1;
    return EGameTest::Running;
}

// Three seconds in the ring's worn gap flees a wild fight and puts Red back on the tile he met it on.
GAME_TEST(Wild_FleeReturnsToTile, 90)
{
    static FState St;
    M4_READY();
    UPYREncounters* E = Enc(Test);
    UPYRField* F = Field(Test);
    Quiet(false, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 30}});
        AfterPokedex(Test);
        if (!StartGrassEncounter(Test, TEXT("ROUTE_1"), St.P)) M4_FAIL(TEXT("no Route 1 grass"));
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        if (E->GetPhase() != EPYREncounterPhase::Fight) { WalkToTile(Test, TEXT("ROUTE_1"), St.P.X, St.P.Y, 5.f); if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("no encounter")); return EGameTest::Running; }
        StopWalking(Test);
        St.V = FVector(E->ReturnTile.X, E->ReturnTile.Y, 0);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(1.2);
        // Pikachu into the gap and held there until the fight ends.
        if (E->GetPhase() == EPYREncounterPhase::Fight && BM(Test)->IsBattleActive())
        {
            APYRRing* Ring = E->GetRing();
            APYRMonPawn* Mine = BM(Test)->GetPlayerMon();
            const APYRBattleStage* Stage = E->GetCurrentStage();
            if (!Ring || !Ring->HasGap() || !Mine) M4_FAIL(TEXT("no gap or no fighter"));
            const FVector Gap = Stage->Centre + FVector(-Stage->Radius + 40.f, 0, 0);
            Mine->SetActorLocation(FVector(Gap.X, Gap.Y, Mine->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
            if (Test.Seconds - St.T0 > 12) M4_FAIL(TEXT("standing in the gap never fled (gap time %.1f)"), E->GapTime);
            return EGameTest::Running;
        }
        St.A = Test.Seconds - St.T0 - 1.2f;
        M4_NEXT();
    }
    Advance(Test);
    if (E->IsBusy()) { if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("never back from the flight")); return EGameTest::Running; }
    if (E->LastOutcome != EPYRBattleOutcome::Fled) M4_FAIL(TEXT("the fight ended with outcome %d, not fled"), int32(E->LastOutcome));
    if (F->CurrentMap != TEXT("ROUTE_1") || F->CurrentTile != FIntPoint(int32(St.V.X), int32(St.V.Y))) M4_FAIL(TEXT("back at %s (%d,%d), met at ROUTE_1 (%.0f,%.0f)"), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y, St.V.X, St.V.Y);
    if (!SaidContains(Test, TEXT("Got away safely"))) M4_FAIL(TEXT("no 'Got away safely!'"));
    M4_PASS(TEXT("fled after %.1f s in the gap; back on ROUTE_1 (%d,%d) where the grass met him"), St.A, F->CurrentTile.X, F->CurrentTile.Y);
}

// A Repel keeps wild Pokemon weaker than the lead away and lets stronger ones through.
GAME_TEST(Wild_RepelBlocksWeak, 90)
{
    static FState St;
    M4_READY();
    UPYREncounters* E = Enc(Test);
    UPYRProgress* P = Progress(Test);
    Quiet(false, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 20}});
        AfterPokedex(Test);
        State(Test)->Bag.Reset();
        State(Test)->AddItem(TEXT("repel"), 1);
        PYRScreens::UseItem(Test.World, TEXT("repel"));
        if (P->RepelSteps != 100 || State(Test)->HasItem(TEXT("repel"))) M4_FAIL(TEXT("using a Repel set %d steps"), P->RepelSteps);
        Menus(Test)->CloseAll();
        FYSlot Weak, Strong;
        Weak.Level = 5;
        Strong.Level = 25;
        if (!E->RepelBlocks(Weak) || E->RepelBlocks(Strong)) M4_FAIL(TEXT("Repel blocks L5 %d, L25 %d with a L20 lead"), E->RepelBlocks(Weak), E->RepelBlocks(Strong));
        St.Note = TEXT("Repel: 100 steps; blocks L5, lets L25 through (lead L20); ");
        // Route 1's Pokemon are all weaker: forced meetings in the grass come to nothing.
        if (!StartGrassEncounter(Test, TEXT("ROUTE_1"), St.P)) M4_FAIL(TEXT("no Route 1 grass"));
        St.I = E->Encounters;
        St.N = P->RepelSteps;
        M4_NEXT();
    }
    WalkToTile(Test, TEXT("ROUTE_1"), St.P.X, St.P.Y, 5.f);
    if (E->Encounters != St.I) M4_FAIL(TEXT("a Route 1 Pokemon came out through the Repel"));
    M4_WAIT(2.0);
    StopWalking(Test);
    if (P->RepelSteps >= St.N) M4_FAIL(TEXT("the Repel did not count the steps (%d)"), P->RepelSteps);
    M4_PASS(TEXT("%sa grass step with a meeting forced met nothing; %d steps left"), *St.Note, P->RepelSteps);
}

// ------------------------------------------------------------------------------------------------ live battles
// Through a wild fight the camera stays over the stage and keeps both fighters in view.
GAME_TEST(Battle_CameraKeepsStage, 120)
{
    static FState St;
    M4_READY();
    UPYREncounters* E = Enc(Test);
    Quiet(false, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 12}});
        AfterPokedex(Test);
        if (!StartGrassEncounter(Test, TEXT("ROUTE_1"), St.P)) M4_FAIL(TEXT("no Route 1 grass"));
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        if (E->GetPhase() != EPYREncounterPhase::Fight) { WalkToTile(Test, TEXT("ROUTE_1"), St.P.X, St.P.Y, 5.f); if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("no encounter")); return EGameTest::Running; }
        StopWalking(Test);
        M4_NEXT();
    }
    // Pikachu runs about the stage for 4 s; every frame both fighters must be inside the view.
    APYRMonPawn* Mine = BM(Test)->GetPlayerMon();
    APYRMonPawn* Foe = BM(Test)->GetFoeMon();
    APlayerCameraManager* Cam = Test.World->GetFirstPlayerController()->PlayerCameraManager;
    const APYRBattleStage* Stage = E->GetCurrentStage();
    if (Test.Seconds - St.T0 < 1.0 || !Mine || !Foe) return EGameTest::Running;
    const double T = Test.Seconds - St.T0;
    Mine->AddMovementInput(FVector(FMath::Sin(T * 1.3), FMath::Cos(T * .9), 0), 1.f);
    const FVector Eye = Cam->GetCameraLocation();
    const FVector Fwd = Cam->GetCameraRotation().Vector();
    const float Half = Cam->GetFOVAngle() * .5f;
    if (FVector::Dist2D(Eye, Stage->Centre) > Stage->Radius * 3.f || Eye.Z < Stage->Centre.Z) M4_FAIL(TEXT("the camera at %s left the stage at %s"), *Eye.ToCompactString(), *Stage->Centre.ToCompactString());
    for (const APYRMonPawn* P : {Mine, Foe})
    {
        const float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Fwd, (P->GetActorLocation() - Eye).GetSafeNormal())));
        St.A = FMath::Max(St.A, Angle);
        if (Angle > Half) M4_FAIL(TEXT("%s is %.0f degrees off the camera's centre (half view %.0f)"), *P->Species.ToString(), Angle, Half);
    }
    if (T < 5.0) return EGameTest::Running;
    M4_PASS(TEXT("4 s of running about: both fighters within %.0f of the %.0f-degree half view; camera %.0f m from the stage centre"), St.A, Half, FVector::Dist(Eye, Stage->Centre) / 100.f);
}

// Gen 1 damage, hand-computed: Pikachu L10 Thundershock on Pidgey L10 is 20.
// Level 10: floor(2*10/5+2) = 6; Special 16 vs 13 (base 50/35, DV 8); floor(floor(6*40*16/13)/50)+2 = 7; STAB x1.5 -> 10;
// Electric on Flying x2 -> 20 (no crit, no random roll).
GAME_TEST(Battle_YellowDamage, 60)
{
    static PYRTest::FState St;
    if (!PYRTest::Ready(Test)) return EGameTest::Running;
    APYRBattleManager* Mgr = PYRTest::M(Test);
    const FName Pikachu(TEXT("pikachu")), Pidgey(TEXT("pidgey")), Thundershock(TEXT("thundershock"));
    if (St.Step == 0)
    {
        State(Test)->Badges.Reset();
        PYRTest::Fight(Test, {PYRTest::Mon(Pikachu, 10, {Thundershock})}, {PYRTest::Mon(Pidgey, 10)});
        St.Step = 1;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (Test.Seconds - St.T0 < 1.2) return EGameTest::Running;
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    const FPYRMoveInfo* Move = PYRData::Move(Thundershock);
    if (!P || !F || !Move) M4_FAIL(TEXT("no fight or no Thundershock"));
    const float Formula = PYRData::Damage(10, 16, 13, Move->Power, 1.5f, 2.f);
    Mgr->bNoRandom = true;
    F->InvulnerableUntil = 0;
    const float Before = F->Hp;
    Mgr->ResolveHit(P, F, *Move, F->GetActorLocation());
    const float Dealt = Before - F->Hp;
    if (Formula != 20.f) M4_FAIL(TEXT("PYRData::Damage gives %.0f, the hand value is 20"), Formula);
    if (Mgr->LastDamage != 20.f || Dealt != 20.f) M4_FAIL(TEXT("the live hit dealt %.0f (LastDamage %.0f), want 20"), Dealt, Mgr->LastDamage);
    M4_PASS(TEXT("Pikachu L10 Thundershock (power %.0f) on Pidgey L10: formula 20, live hit 20 (HP %.0f -> %.0f)"), Move->Power, Before, F->Hp);
}

// The Boulder Badge raises Red's Pokemon's Attack by an eighth in battle (Yellow's badge boost); foes get none.
GAME_TEST(Badges_BoulderBoost, 60)
{
    static PYRTest::FState St;
    if (!PYRTest::Ready(Test)) return EGameTest::Running;
    APYRBattleManager* Mgr = PYRTest::M(Test);
    UPYRTrainerState* S = State(Test);
    const FName Pikachu(TEXT("pikachu")), Rattata(TEXT("rattata"));
    if (St.Step == 0)
    {
        S->Badges.Reset();
        PYRTest::Fight(Test, {PYRTest::Mon(Pikachu, 20)}, {PYRTest::Mon(Pikachu, 20)});
        St.Step = 1;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (Test.Seconds - St.T0 < 1.2) return EGameTest::Running;
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) M4_FAIL(TEXT("no fighters"));
    const float Without = P->EffectiveStat(EPYRStat::Attack);
    S->AwardBadge(EPYRBadge::Boulder);
    const float With = P->EffectiveStat(EPYRStat::Attack), Foe = F->EffectiveStat(EPYRStat::Attack);
    const float Want = FMath::FloorToFloat(Without * 1.125f);
    if (S->BadgeBoost(EPYRStat::Attack) != 1.125f) M4_FAIL(TEXT("Boulder Badge boost %.3f"), S->BadgeBoost(EPYRStat::Attack));
    if (With != Want) M4_FAIL(TEXT("Attack with the badge %.0f, want %.0f (from %.0f)"), With, Want, Without);
    if (Foe != Without) M4_FAIL(TEXT("the foe's Attack changed with Red's badge (%.0f)"), Foe);
    if (P->EffectiveStat(EPYRStat::Defence) != F->EffectiveStat(EPYRStat::Defence)) M4_FAIL(TEXT("the Boulder Badge changed Defence"));
    M4_PASS(TEXT("Pikachu L20 Attack %.0f -> %.0f with the Boulder Badge (x9/8); the foe's stays %.0f; Defence untouched"), Without, With, Foe);
}

// ------------------------------------------------------------------------------------------------ Pikachu
// Pikachu walks behind Red through a connection and a door and back, never in a ball, always a tile or two behind.
GAME_TEST(Pikachu_FollowsNeverBall, 120)
{
    static FState St;
    M4_READY();
    UPYRField* F = Field(Test);
    APYRPikachuFollower* Pika = APYRPikachuFollower::Find(Test.World);
    const AYellowWorld* W = World(Test);
    Quiet(true, true);
    if (!Pika) M4_FAIL(TEXT("no Pikachu follower"));
    // Legs: north into Route 1, back into Pallet, in at Red's front door, out again. A leg may start from a placed tile;
    // targets past a map's edge walk on into the next map.
    struct FLeg { const TCHAR* StartMap; int32 SX, SY; const TCHAR* Map; int32 X, Y; const TCHAR* Arrive; };
    static const FLeg Legs[] = {
        {TEXT("PALLET_TOWN"), 10, 3, TEXT("PALLET_TOWN"), 10, -3, TEXT("ROUTE_1")},
        {nullptr, 0, 0, TEXT("ROUTE_1"), 10, 38, TEXT("PALLET_TOWN")},
        {TEXT("PALLET_TOWN"), 5, 7, TEXT("PALLET_TOWN"), 5, 5, TEXT("REDS_HOUSE_1F")},
        {nullptr, 0, 0, TEXT("REDS_HOUSE_1F"), -1, 10, TEXT("PALLET_TOWN")}};
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 8}});
        AfterPokedex(Test);
        St.I = 0;
        St.N = -1;
        M4_NEXT();
    }
    if (St.I >= 4) M4_PASS(TEXT("%sPikachu out of its ball throughout, at most %.1f tiles behind after moving"), *St.Note, St.A);
    const FLeg& L = Legs[St.I];
    if (St.N != St.I)
    {
        St.N = St.I;
        if (L.StartMap) Place(Test, L.StartMap, L.SX, L.SY, EYDir::Up);
        St.P = FIntPoint(L.X < 0 ? F->CurrentTile.X : L.X, L.Y);
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (Pika->IsHidden()) M4_FAIL(TEXT("Pikachu is hidden (in a ball?) on leg %d at %s"), St.I, *F->CurrentMap.ToString());
    if (Pika->GetMode() != EPYRPikachuMode::Follow) M4_FAIL(TEXT("Pikachu is not following (mode %d)"), int32(Pika->GetMode()));
    if (F->CurrentMap != L.Arrive && !F->IsWarping()) WalkToTile(Test, L.Map, St.P.X, St.P.Y, 5.f);
    if (F->CurrentMap == L.Arrive && !F->IsWarping() && !F->IsLocked())
    {
        StopWalking(Test);
        if (St.B == 0.f) St.B = Test.Seconds;
        if (Test.Seconds - St.B < 1.2) return EGameTest::Running;
        St.B = 0.f;
        const float Apart = FVector::Dist2D(Pika->GetActorLocation(), Red(Test)->GetActorLocation()) / 100.f;
        const FYMap* PM = W->MapAt(Pika->GetActorLocation());
        if (Apart > 2.2f) M4_FAIL(TEXT("in %s Pikachu is %.1f tiles from Red"), L.Arrive, Apart);
        // Right after a connection Pikachu may still be on the last tile of the map behind; after a warp it is always
        // in Red's map.
        const bool bConnection = St.I <= 1;
        if (!PM || (PM->Id != F->CurrentMap && !(bConnection && Apart <= 1.6f)))
            M4_FAIL(TEXT("Pikachu is in %s, Red in %s (%.1f tiles apart)"), PM ? *PM->Id.ToString() : TEXT("nowhere"), *F->CurrentMap.ToString(), Apart);
        St.A = FMath::Max(St.A, Apart);
        St.Note += FString::Printf(TEXT("%s (%.1f tiles); "), L.Arrive, Apart);
        ++St.I;
        return EGameTest::Running;
    }
    if (Test.Seconds - St.T0 > 12) M4_FAIL(TEXT("leg %d to %s stuck at %s (%d,%d)"), St.I, L.Arrive, *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y);
    return EGameTest::Running;
}

// Talking to Pikachu shows its face from pikachu.json for its happiness and mood, and the face changes with them.
GAME_TEST(Pikachu_FaceChanges, 90)
{
    static FState St;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRTrainerState* S = State(Test);
    APYRPikachuFollower* Pika = APYRPikachuFollower::Find(Test.World);
    Quiet(true, true);
    struct FCase { int32 Happy, Mood; };
    static const FCase Cases[] = {{40, 60}, {140, 128}, {250, 200}};
    if (!D->Pikachu.IsLoaded()) M4_FAIL(TEXT("YellowData missing: pikachu.json face table"));
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 8}});
        AfterPokedex(Test);
        Place(Test, TEXT("PALLET_TOWN"), 10, 8, EYDir::Down);
        M4_NEXT();
    }
    if (St.I >= 3)
    {
        TSet<FString> Faces(St.Seen);
        if (Faces.Num() < 2) M4_FAIL(TEXT("the face never changed: %s"), *FString::Join(St.Seen, TEXT(", ")));
        M4_PASS(TEXT("%s"), *St.Note);
    }
    if (St.Step == 1)
    {
        M4_WAIT(1.0);
        Menus(Test)->CloseAll();
        S->BondPoints = Cases[St.I].Happy;
        S->PikachuMood = Cases[St.I].Mood;
        // Face Pikachu and talk (T).
        APokemonYellowReimagineCharacter* R = Red(Test);
        R->SetActorRotation(FRotator(0, (Pika->GetActorLocation() - R->GetActorLocation()).Rotation().Yaw, 0));
        R->TalkToPikachu();
        M4_NEXT();
    }
    M4_WAIT(.2);
    TSharedPtr<FPYRScreen> Screen = Menus(Test)->Find(TEXT("pikachu-face"));
    if (!Screen) M4_FAIL(TEXT("T by Pikachu showed no face"));
    const FString Want = D->Pikachu.FaceFor(Cases[St.I].Happy, Cases[St.I].Mood);
    const FString Desc = Screen->Describe();
    if (Want.IsEmpty() || !Desc.Contains(Want.Replace(TEXT("_"), TEXT(" ")).ToUpper())) M4_FAIL(TEXT("happiness %d mood %d shows '%s', pikachu.json says %s"), Cases[St.I].Happy, Cases[St.I].Mood, *Desc, *Want);
    if (Desc.StartsWith(TEXT("none"))) M4_FAIL(TEXT("no picture for face %s"), *Want);
    St.Seen.Add(Want);
    St.Note += FString::Printf(TEXT("happiness %d mood %d: %s; "), Cases[St.I].Happy, Cases[St.I].Mood, *Want);
    Menus(Test)->CloseAll();
    ++St.I;
    St.Step = 1;
    St.T0 = Test.Seconds;
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ growing
// Caterpie evolves at 7 through the evolution scene; at 10 Back stops Metapod once, then it becomes a flying Butterfree.
GAME_TEST(Growth_CaterpieEvolves, 150)
{
    static FState St;
    M4_READY();
    UPYRTrainerState* S = State(Test);
    Quiet(true, true);
    const FName Caterpie(TEXT("caterpie")), Metapod(TEXT("metapod")), Butterfree(TEXT("butterfree"));
    auto Grow = [S](int32 To)
    {
        FPYRMon& M = S->Party[0];
        S->GiveXp(M, FMath::Max(0, PYRData::XpForLevel(M.Species, To) - M.Xp));
    };
    auto Scene = [&Test]()
    {
        APokemonYellowReimagineCharacter* R = Red(Test);
        PYRScreens::EvolvePending(Test.World, R->GetActorLocation() + R->GetActorForwardVector() * 250.f - FVector(0, 0, 88.f), R->GetActorRotation().Yaw + 180.f, nullptr);
    };
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("caterpie"), 6}});
        Place(Test, TEXT("ROUTE_1"), 9, 26, EYDir::Up);
        Grow(7);
        if (S->Party[0].Level != 7 || S->Party[0].EvolvePending != Metapod) M4_FAIL(TEXT("Caterpie L%d, evolving into '%s'"), S->Party[0].Level, *S->Party[0].EvolvePending.ToString());
        Scene();
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // Let the scene play out (text on, the flicker runs by itself).
        if (TSharedPtr<FPYRScreen> Evo = Menus(Test)->Find(TEXT("evolution"))) St.Note = Evo->Describe();
        if (Menus(Test)->IsTalking()) Advance(Test);
        if (Menus(Test)->AnyOpen()) { if (Test.Seconds - St.T0 > 30) M4_FAIL(TEXT("the scene never ended")); return EGameTest::Running; }
        if (S->Party[0].Species != Metapod) M4_FAIL(TEXT("after the scene it is %s"), *S->Party[0].Species.ToString());
        St.Note = FString::Printf(TEXT("L7: Caterpie -> Metapod through the scene; "));
        Grow(10);
        if (S->Party[0].EvolvePending != Butterfree) M4_FAIL(TEXT("Metapod L10 is not waiting to evolve"));
        Scene();
        St.I = 0;
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        // Back during the flicker stops it.
        TSharedPtr<FPYRScreen> Evo = Menus(Test)->Find(TEXT("evolution"));
        if (Menus(Test)->Top() == Evo && Evo && Evo->Describe().Contains(TEXT("flicker")) && St.I == 0) { M4_WAIT(1.0); Press(Test, EPYRKey::Back); St.I = 1; }
        else if (Menus(Test)->IsTalking()) Advance(Test);
        if (Menus(Test)->AnyOpen()) { if (Test.Seconds - St.T0 > 30) M4_FAIL(TEXT("the scene never ended")); return EGameTest::Running; }
        if (S->Party[0].Species != Metapod) M4_FAIL(TEXT("Back did not stop the evolution (%s)"), *S->Party[0].Species.ToString());
        if (!SaidContains(Test, TEXT("stopped evolving"))) St.Note += TEXT("(no 'stopped evolving' line) ");
        St.Note += TEXT("L10: Back stopped Metapod; ");
        // The next level up asks again.
        Grow(11);
        Scene();
        M4_NEXT();
    }
    if (Menus(Test)->IsTalking()) Advance(Test);
    if (Menus(Test)->AnyOpen()) { if (Test.Seconds - St.T0 > 30) M4_FAIL(TEXT("the last scene never ended")); return EGameTest::Running; }
    if (S->Party[0].Species != Butterfree) M4_FAIL(TEXT("L11 is %s, not Butterfree"), *S->Party[0].Species.ToString());
    const FPYRSpeciesInfo* Info = PYRData::Species(Butterfree);
    if (!Info || Info->Body != EPYRBody::Flier) M4_FAIL(TEXT("Butterfree does not fly"));
    M4_PASS(TEXT("%sL11: Metapod -> Butterfree, a flier"), *St.Note);
}

// A TM teaches its move to a Pokemon that can learn it and is used up; one that can't is told so.
GAME_TEST(Growth_TeachTm, 90)
{
    static FState St;
    M4_READY();
    UPYRTrainerState* S = State(Test);
    const FYellowDataSet* D = Data(Test);
    Quiet(true, true);
    const FName Tm = YKey::Item(TEXT("TM_BIDE"));
    const FYItem* Info = D->Item(Tm);
    if (!Info || Info->Tm != 34) M4_FAIL(TEXT("items.json has no TM34 (BIDE)"));
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 10}, {TEXT("caterpie"), 5}});
        S->Bag.Reset();
        S->AddItem(Tm, 1);
        Place(Test, TEXT("ROUTE_1"), 9, 26, EYDir::Up);
        if (!PYRData::CanLearnTm(TEXT("pikachu"), 34) || PYRData::CanLearnTm(TEXT("caterpie"), 34)) M4_FAIL(TEXT("species.json TM lists: Pikachu %d, Caterpie %d for TM34"), PYRData::CanLearnTm(TEXT("pikachu"), 34), PYRData::CanLearnTm(TEXT("caterpie"), 34));
        Menus(Test)->TextLog.Reset();
        PYRScreens::UseItem(Test.World, Tm);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // Caterpie first: it can't.
        if (!Menus(Test)->IsOpen(TEXT("party"))) { Advance(Test, true); if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("never asked whom to teach: %s"), *Menus(Test)->ScreenText()); return EGameTest::Running; }
        Press(Test, EPYRKey::Down);
        Press(Test, EPYRKey::Confirm);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.2);
        SkipText(Test);
        if (S->Party[1].Carried.Contains(Info->Move) || S->ItemCount(Tm) != 1) M4_FAIL(TEXT("Caterpie learned %s"), *Info->Move.ToString());
        St.Note = FString::Printf(TEXT("Caterpie can't learn %s; "), *Info->Move.ToString());
        PYRScreens::UseItem(Test.World, Tm);
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        if (!Menus(Test)->IsOpen(TEXT("party"))) { Advance(Test, true); if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("never asked again")); return EGameTest::Running; }
        Press(Test, EPYRKey::Confirm);
        M4_NEXT();
    }
    Advance(Test, true);
    if (Menus(Test)->AnyOpen()) { if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("teaching never finished: %s"), *Menus(Test)->ScreenText()); return EGameTest::Running; }
    if (!S->Party[0].Carried.Contains(Info->Move)) M4_FAIL(TEXT("Pikachu did not learn %s (moves %d)"), *Info->Move.ToString(), S->Party[0].Carried.Num());
    if (S->ItemCount(Tm) != 0) M4_FAIL(TEXT("the TM was not used up"));
    M4_PASS(TEXT("%sPikachu learned %s from TM34; the TM is gone"), *St.Note, *Info->Move.ToString());
}

// ------------------------------------------------------------------------------------------------ m4q
// Every map with wild Pokemon (grass and cave floors), 20000 steps each with a fixed seed, rolled the way Red's steps
// roll them: meetings at Yellow's rate (rate/256, within four standard deviations), only the species of Yellow's
// table for the map, at the table's levels.
GAME_TEST(Encounters_YellowTablesLevelsRates, 180)
{
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    FString Note;
    int32 Maps = 0;
    for (const FName& Id : D->MapOrder)
    {
        const FYMap* M = D->Map(Id);
        const FYWild* W = D->Wild.Find(Id);
        if (!M || !W || W->Grass.Num() == 0) continue;
        const int32 Rate = UPYREncounters::RateFor(*D, *M);
        if (Rate <= 0) M4_FAIL(TEXT("%s has a wild table but no encounter rate"), *Id.ToString());
        const TCHAR Tile = M->bCave ? TEXT('c') : TEXT('g');
        TSet<TPair<FName, int32>> Allowed;
        for (const FYSlot& S : W->Grass) Allowed.Add(TPair<FName, int32>(S.Species, S.Level));
        FRandomStream Rng(0x3a77 + Maps);
        int32 Met = 0;
        constexpr int32 Steps = 20000;
        for (int32 I = 0; I < Steps; ++I)
        {
            FYSlot Slot;
            if (!UPYREncounters::RollStep(*D, *M, Tile, Rng, Slot)) continue;
            ++Met;
            if (!Allowed.Contains(TPair<FName, int32>(Slot.Species, Slot.Level)))
                M4_FAIL(TEXT("%s met %s L%d, which Yellow's table for it doesn't have"), *Id.ToString(), *Slot.Species.ToString(), Slot.Level);
        }
        const double Want = Rate / 256.0, Got = double(Met) / Steps;
        const double Tol = 4.0 * FMath::Sqrt(Want * (1 - Want) / Steps);
        if (FMath::Abs(Got - Want) > Tol) M4_FAIL(TEXT("%s met Pokemon on %.4f of steps, Yellow's rate %d/256 = %.4f"), *Id.ToString(), Got, Rate, Want);
        Note += FString::Printf(TEXT("%s %d/256 (%.3f); "), *Id.ToString(), Rate, Got);
        ++Maps;
    }
    if (Maps == 0) M4_FAIL(TEXT("no map has wild Pokemon"));
    M4_PASS(TEXT("%d maps x 20000 steps: only Yellow's species at Yellow's levels, at Yellow's rates: %s"), Maps, *Note);
}

// A 20 s wild fight with Pikachu running about: the camera keeps Pikachu, the foe and Red inside the view the whole
// time, and its yaw never swings more than 5 degrees.
GAME_TEST(Battle_CameraKeepsStageInViewNoSpin, 180)
{
    static FState St;
    static float Yaw0 = 0.f, MaxSwing = 0.f;
    M4_READY();
    UPYREncounters* E = Enc(Test);
    Quiet(false, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 40}});
        AfterPokedex(Test);
        if (!StartGrassEncounter(Test, TEXT("ROUTE_1"), St.P)) M4_FAIL(TEXT("no Route 1 grass"));
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        if (E->GetPhase() != EPYREncounterPhase::Fight) { WalkToTile(Test, TEXT("ROUTE_1"), St.P.X, St.P.Y, 5.f); if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("no encounter")); return EGameTest::Running; }
        StopWalking(Test);
        BM(Test)->bFoePassive = true;   // the foe stays in the fight for the whole 20 s
        M4_NEXT();
    }
    APYRMonPawn* Mine = BM(Test)->GetPlayerMon();
    APYRMonPawn* Foe = BM(Test)->GetFoeMon();
    APlayerCameraManager* Cam = Test.World->GetFirstPlayerController()->PlayerCameraManager;
    APokemonYellowReimagineCharacter* R = Red(Test);
    if (Test.Seconds - St.T0 < 1.0 || !Mine || !Foe) return EGameTest::Running;
    const double T = Test.Seconds - St.T0;
    Mine->AddMovementInput(FVector(FMath::Sin(T * 1.3), FMath::Cos(T * .9), 0), 1.f);
    const FVector Eye = Cam->GetCameraLocation();
    const FRotator Rot = Cam->GetCameraRotation();
    if (St.N == 0) { Yaw0 = Rot.Yaw; St.N = 1; }
    MaxSwing = FMath::Max(MaxSwing, FMath::Abs(FRotator::NormalizeAxis(Rot.Yaw - Yaw0)));
    if (MaxSwing > 5.f) M4_FAIL(TEXT("the camera's yaw swung %.1f degrees at %.1f s"), MaxSwing, T);
    // Each of them inside the 16:9 frame: across within the half field of view, up and down within the vertical half.
    const float HalfH = Cam->GetFOVAngle() * .5f;
    const float HalfV = FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(HalfH)) * 9.f / 16.f));
    for (const AActor* A : {static_cast<const AActor*>(Mine), static_cast<const AActor*>(Foe), static_cast<const AActor*>(R)})
    {
        const FVector L = Rot.UnrotateVector(A->GetActorLocation() - Eye);   // X forward, Y right, Z up
        if (L.X <= 0) M4_FAIL(TEXT("%s is behind the camera at %.1f s"), *A->GetName(), T);
        const float Across = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(L.Y), L.X));
        const float Up = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(L.Z), L.X));
        St.A = FMath::Max(St.A, FMath::Max(Across / HalfH, Up / HalfV) * 100.f);
        if (Across > HalfH || Up > HalfV)
            M4_FAIL(TEXT("%s is out of frame at %.1f s: %.0f degrees across (half %.0f), %.0f up/down (half %.0f)"), *A->GetName(), T, Across, HalfH, Up, HalfV);
    }
    if (T < 21.0) return EGameTest::Running;
    M4_PASS(TEXT("20 s of running about: Pikachu, the foe and Red always in frame (at most %.0f%% of the way to an edge); yaw swung %.1f degrees at most"), St.A, MaxSwing);
}

static struct FM4QEncounterNames
{
    FM4QEncounterNames() { GameTest::Register(TEXT("Encounters_FleeReturnsToSameTile"), &GameTest_Wild_FleeReturnsToTile, 150); }
} GM4QEncounterNames;
