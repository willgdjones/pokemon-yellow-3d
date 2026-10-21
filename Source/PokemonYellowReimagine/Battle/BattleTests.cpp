// m2 battle tests: moves and types, bodies, swapping and Potions, catching, Pikachu and growth. Each builds the flat
// 40 m test pad with a 100 cm ledge at (0, 60000, 0) (away from Kanto), starts fights through StartBattle (the
// `journey` seam) with DebugSetParty, and plays them through the same Press* handlers as the keys. Test names use
// dots (Battle.Moves.FireAtOnce), so they register through PYR_TEST instead of GAME_TEST.
#include "BattleTestKit.h"
#include "PYRAttack.h"
#include "PYRBattleBot.h"
#include "PYRBattleFx.h"
#include "PYRBattleManager.h"
#include "PYRBattleWorld.h"
#include "PYRMonPawn.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "../GameTest.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "../Partner/PYRBond.h"
#include "../Partner/PYRPikachuFollower.h"
#include "../Party/PYRSpecies.h"
#include "../Party/PYRTrainerState.h"
#include "../World/PYRUi.h"
#include "../Menus/PYRMenus.h"
#include "../Menus/PYRMoveLearning.h"

using namespace PYRData;

namespace PYRTest
{
bool Ready(FGameTestContext& Test)
{
    APlayerController* PC = Test.World->GetFirstPlayerController();
    return Test.Seconds > .5 && PC && PC->GetPawn();
}

APYRBattleManager* M(FGameTestContext& Test) { return APYRBattleManager::Get(Test.World); }
UPYRTrainerState* S(FGameTestContext& Test) { return UPYRTrainerState::Get(Test.World); }

FPYRMonSpec Mon(FName Species, int32 Level, TArray<FName> Moves)
{
    FPYRMonSpec Spec;
    Spec.Species = Species;
    Spec.Level = Level;
    Spec.Moves = MoveTemp(Moves);
    return Spec;
}

// A fight on the pad through StartBattle, with a passive foe (it only does what the test forces) and no dice.
APYRBattleManager* Fight(FGameTestContext& Test, const TArray<FPYRMonSpec>& Party, const TArray<FPYRMonSpec>& Foes,
    EPYRBattleKind Kind, FVector FoeOffset)
{
    APYRBattleManager* Mgr = M(Test);
    if (Mgr->IsBattleActive()) Mgr->RequestFlee();
    UPYRTrainerState* State = S(Test);
    State->DebugSetParty(Party);
    State->SetItemCount(UPYRTrainerState::Potion, 5);
    State->SetItemCount(UPYRTrainerState::PokeBall, 10);
    const FVector Pad = UPYRBattleWorld::EnsureTestPad(Test.World);
    FPYRBattleSetup Setup;
    Setup.Kind = Kind;
    Setup.Foes = Foes;
    Setup.TrainerId = Kind == EPYRBattleKind::Gym ? FName(TEXT("brock")) : Kind == EPYRBattleKind::Trainer ? FName(TEXT("bug-catcher-1")) : NAME_None;
    Setup.Prize = Kind == EPYRBattleKind::Wild ? 0 : 100;
    Setup.Center = Pad + FVector(-500, 0, 0);
    Setup.Radius = 1450.f;
    Mgr->bFoePassive = true;
    Mgr->bNoRandom = true;
    Mgr->ForcedBallDodgeRoll = Mgr->ForcedCatchRoll = -1;
    Mgr->StartBattle(Setup);
    if (APYRMonPawn* F = Mgr->GetFoeMon())
        if (!FoeOffset.IsZero())
        {
            float Floor = 0;
            const FVector At = Setup.Center + FoeOffset;
            APYRBattleManager::GroundZ(Test.World, At + FVector(0, 0, 400), Floor, F);
            F->SetActorLocation(FVector(At.X, At.Y, Floor + F->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
        }
    return Mgr;
}

// Puts a fighter on the ground (fliers at their hover height) at X/Y.
void Place(APYRMonPawn* P, FVector XY)
{
    if (!P) return;
    float Floor = 0;
    // Under a cave ceiling the trace starts just above the floor (from 10 m up it would land on the rock).
    const APYRBattleManager* Mgr = APYRBattleManager::Find(P);
    const float Up = Mgr && Mgr->IsBattleActive() && Mgr->HasCeiling() ? 150.f : 1000.f;
    APYRBattleManager::GroundZ(P->GetWorld(), FVector(XY.X, XY.Y, XY.Z + Up), Floor, P);
    const bool bHover = P->GetBody() == EPYRBody::Flier && P->Info;
    const float Z = Floor + (bHover ? P->Info->HoverHeight() : P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f);
    P->SetActorLocation(FVector(XY.X, XY.Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
    P->GetCharacterMovement()->StopMovementImmediately();
}

void Face(APYRMonPawn* P, const AActor* At)
{
    if (P && At) P->SetActorRotation(FRotator(0, (At->GetActorLocation() - P->GetActorLocation()).Rotation().Yaw, 0));
}

// The live attack a foe is making (the most recent), if any.
APYRAttack* FoeAttack(UWorld* World)
{
    APYRAttack* Found = nullptr;
    for (TActorIterator<APYRAttack> It(World); It; ++It)
        if (It->IsLive() && It->User.IsValid() && !It->User->bPlayerSide) Found = *It;
    return Found;
}

bool Near(float A, float B, float Tol) { return FMath::Abs(A - B) <= Tol; }

bool CaptionSeen(APYRBattleManager* Mgr, const FString& Part)
{
    for (const FString& Line : Mgr->CaptionLog) if (Line.Contains(Part)) return true;
    return false;
}

// Knocks the foe out with one of the player's moves landing (the real hit path: experience, level-ups, evolution).
// m4u: the hit's dice are pinned (no accuracy roll, crit or damage roll) whatever the battle's own bNoRandom, so a staged
// knock-out in a real Kanto fight can't miss; true when the foe went down.
bool KnockOut(APYRBattleManager* Mgr)
{
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) return false;
    if (F->bFainted) return true;
    F->Hp = 1;
    F->InvulnerableUntil = 0;
    const bool bWasNoRandom = Mgr->bNoRandom;
    Mgr->bNoRandom = true;
    Mgr->ResolveHit(P, F, *Move(Tackle), F->GetActorLocation());
    Mgr->bNoRandom = bWasNoRandom;
    return F->bFainted || F->Hp <= 0.f;
}
}

using namespace PYRTest;


// ------------------------------------------------------------------------------------------------ moves and types
PYR_TEST(MovesFireAtOnce, "Battle.Moves.FireAtOnce", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, {Mon(Pikachu, 5)}, {Mon(Pidgey, 30)}, EPYRBattleKind::Wild, FVector(0, 900, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P || !Mgr->IsBattleActive()) PYR_FAIL(TEXT("no player mon after StartBattle"));
    if (St.Step == 1)
    {
        PYR_WAIT(1.2);
        const int32 FxBefore = Mgr->FxLog.FindRef(ThunderShock);
        const double Before = Test.World->GetTimeSeconds();
        Mgr->PressMove(0);
        APYRAttack* Live = nullptr;
        for (TActorIterator<APYRAttack> It(Test.World); It; ++It) if (It->User.Get() == P && It->Phase == APYRAttack::EPhase::Active) Live = *It;
        if (P->LastMove != ThunderShock || P->LastMoveTime != Before) PYR_FAIL(TEXT("move not started on press (last %s)"), *P->LastMove.ToString());
        if (!Live) PYR_FAIL(TEXT("no active attack in the same frame"));
        if (Mgr->FxLog.FindRef(ThunderShock) <= FxBefore) PYR_FAIL(TEXT("no Thundershock effect in the same frame"));
        St.A = Mgr->GetCooldownFraction(0);
        if (St.A < .95f) PYR_FAIL(TEXT("cooldown ring not full after use (%.2f)"), St.A);
        St.Step = 2; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 2)
    {
        PYR_WAIT(.5);
        St.B = Mgr->GetCooldownFraction(0);
        if (St.B <= 0.f || St.B >= St.A) PYR_FAIL(TEXT("ring not emptying (%.2f after 0.5 s)"), St.B);
        if (P->IsMoveReady(0)) PYR_FAIL(TEXT("ready again after 0.5 s"));
        St.Step = 3;
        return EGameTest::Running;
    }
    if (!P->IsMoveReady(0)) { if (Test.Seconds - St.T0 > 3) PYR_FAIL(TEXT("never ready again")); return EGameTest::Running; }
    if (Mgr->GetCooldownFraction(0) > 0.f) PYR_FAIL(TEXT("ready but the ring still shows %.2f"), Mgr->GetCooldownFraction(0));
    PYR_PASS(TEXT("Thundershock active with its bolt effect in the press frame; ring %.2f -> %.2f at 0.5 s -> empty when ready (%.2f s)"),
        St.A, St.B, Test.Seconds - St.T0 + .5);
}

PYR_TEST(DodgeHitWindow, "Battle.Dodge.HitWindow", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, {Mon(Pikachu, 8)}, {Mon(Pidgey, 5)}); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    if (St.Step == 1 || St.Step == 4)
    {
        PYR_WAIT(1.2);
        Place(P, F->GetActorLocation() - FVector(0, 400, 0));
        Face(F, P);
        St.A = P->Hp;
        Mgr->DebugResetCooldowns();
        Mgr->DebugForceFoeMove(Gust);
        St.Step = St.Step == 1 ? 2 : 5;
        return EGameTest::Running;
    }
    APYRAttack* A = FoeAttack(Test.World);
    if (St.Step == 2)
    {
        if (!A) PYR_FAIL(TEXT("no Gust"));
        // Gust's cone lands on the window's first frame: dodge on the frame before, so the dodge covers the window.
        if (A->SecondsToHit() > .05) return EGameTest::Running;
        if (!A->Threatens(P)) PYR_FAIL(TEXT("Gust would not reach Pikachu"));
        Mgr->PressDodge();
        St.C = float(A->HitEnd);
        St.Step = 3; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 3)
    {
        const double Now = Test.World->GetTimeSeconds();
        if (A && Now >= A->HitStart && Now <= A->HitStart + .05 && !P->IsInvulnerable()) PYR_FAIL(TEXT("dodge did not cover the landing"));
        PYR_WAIT(1.0);
        if (P->Hp != St.A) PYR_FAIL(TEXT("dodged Gust still did %.0f damage"), St.A - P->Hp);
        St.Step = 4; St.T0 = Test.Seconds - 5;
        return EGameTest::Running;
    }
    if (A && A->IsLive()) return EGameTest::Running;
    if (P->Hp >= St.A) PYR_FAIL(TEXT("control Gust without a dodge did no damage"));
    PYR_PASS(TEXT("a dodge inside Gust's hit window took 0; the same Gust undodged took %.0f"), St.A - P->Hp);
}

PYR_TEST(TellsEveryAttack, "Battle.Tells.EveryAttack", 400)
{
    // Every move in the table, used by a foe of each body that changes its shape (runner, flier dive, giant sweep).
    static FState St;
    static TArray<TPair<FName, FName>> Cases;
    static FString Summary;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (Cases.Num() == 0)
    {
        for (const FName& Move : AllMoves()) Cases.Add({Rattata, Move});
        for (const FName& Move : {Gust, Peck, Tackle, SandAttack}) Cases.Add({Pidgey, Move});
        for (const FName& Move : {Tackle, Screech, Bide, RockThrow}) Cases.Add({Onix, Move});
    }
    if (St.I >= Cases.Num()) PYR_PASS(TEXT("%d foe moves, each told 0.4-1.2 s before its hit window: %s"), Cases.Num(), *Summary);
    const FName Who = Cases[St.I].Key, MoveId = Cases[St.I].Value;
    APYRMonPawn* F = Mgr->GetFoeMon();
    // A fresh fight for each foe, and again after Selfdestruct or Whirlwind ends one.
    if (St.Step == 0 || !F || F->Species != Who || F->bFainted || !Mgr->IsBattleActive() || Mgr->IsMenuOpen())
    {
        if (Mgr->IsMenuOpen()) if (UPYRMenus* Me = UPYRMenus::Get(Test.World)) Me->CloseAll();  // m4t: a learn prompt, dropped (the fight restarts)
        if (St.Step != 0 && St.Step != 99) { St.Step = 99; }
        Fight(Test, {Mon(Pikachu, 60)}, {Mon(Who, 30)}, Who == Onix ? EPYRBattleKind::Gym : EPYRBattleKind::Wild, FVector(0, 800, 0));
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(1.2);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P) PYR_FAIL(TEXT("no player"));
    P->Hp = P->MaxHp;
    F->MaxHp = F->Hp = 100000.f;
    // One move at a time: the previous one must be done.
    for (TActorIterator<APYRAttack> It(Test.World); It; ++It)
        if (It->IsLive() && It->User.Get() == F) { if (Test.Seconds - St.T0 > 8) PYR_FAIL(TEXT("%s never finished"), *It->Move.Id.ToString()); return EGameTest::Running; }
    F->Carried = {MoveId};
    const int32 Logged = Mgr->TellLog.Num();
    TArray<FVector> Before;
    Mgr->GetActiveTells(Before);
    Mgr->DebugForceFoeMove(MoveId);
    if (Mgr->TellLog.Num() != Logged + 1) PYR_FAIL(TEXT("%s did not start %s"), *Who.ToString(), *MoveId.ToString());
    const FPYRTellRecord& R = Mgr->TellLog.Last();
    const double Lead = R.HitStart - R.TellStart;
    if (!R.bHadTell || Lead < .4 - 1e-3 || Lead > 1.2 + 1e-3) PYR_FAIL(TEXT("%s %s tell lead %.2f s (told %d)"), *Who.ToString(), *MoveId.ToString(), Lead, R.bHadTell ? 1 : 0);
    Summary += FString::Printf(TEXT("%s/%s %.2f; "), *Who.ToString(), *MoveId.ToString(), Lead);
    ++St.I;
    St.T0 = Test.Seconds - 1.2;
    return EGameTest::Running;
}

PYR_TEST(TypesNoEffectOnRock, "Battle.Types.NoEffectOnRock", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    const FName Foes[2] = {Geodude, Onix};
    if (St.Step == 0 || St.Step == 2)
    {
        const bool bOnix = St.Step == 2;
        Fight(Test, {Mon(Pikachu, 10)}, {bOnix ? Mon(Onix, 12) : Mon(Geodude, 10)}, bOnix ? EPYRBattleKind::Gym : EPYRBattleKind::Wild, FVector(0, 700, 0));
        Mgr->CaptionLog.Reset();
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(1.2);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    const FString Want = FString::Printf(TEXT("It doesn't affect %s"), *APYRBattleManager::MonName(Foes[St.Step / 2]));
    if (St.I == St.Step)
    {
        if (!CaptionSeen(Mgr, Want)) { if (Test.Seconds - St.T0 > 5) PYR_FAIL(TEXT("no '%s' caption (last '%s')"), *Want, *Mgr->LastCaption().ToString()); return EGameTest::Running; }
        if (F->Hp != St.A) PYR_FAIL(TEXT("%s took %.0f from Thundershock"), *Foes[St.Step / 2].ToString(), St.A - F->Hp);
        St.Note += FString::Printf(TEXT("%s: 0 damage, '%s...'; "), *Foes[St.Step / 2].ToString(), *Want);
        if (St.Step == 3) PYR_PASS(TEXT("%s"), *St.Note);
        ++St.Step;
        return EGameTest::Running;
    }
    Place(P, F->GetActorLocation() + FVector(0, -500, 0));
    Face(P, F);
    St.A = F->Hp;
    Mgr->PressMove(0);
    if (P->LastMove != ThunderShock) PYR_FAIL(TEXT("slot 1 is %s"), *P->LastMove.ToString());
    St.I = St.Step;
    return EGameTest::Running;
}

PYR_TEST(TypesSuperEffective, "Battle.Types.SuperEffective", 60)
{
    // Gust is Normal in Yellow, so the Flying example is Spearow's Peck on Caterpie; plus Thundershock on Pidgey.
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0 || St.Step == 2)
    {
        if (St.Step == 0) Fight(Test, {Mon(Spearow, 10)}, {Mon(Caterpie, 30)}, EPYRBattleKind::Wild, FVector(0, 600, 0));
        else Fight(Test, {Mon(Pikachu, 10)}, {Mon(Pidgey, 30)}, EPYRBattleKind::Wild, FVector(0, 700, 0));
        Mgr->CaptionLog.Reset();
        ++St.Step; St.T0 = Test.Seconds; St.I = 0;
        return EGameTest::Running;
    }
    PYR_WAIT(1.2);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    if (St.I == 0)
    {
        Place(P, F->GetActorLocation() + FVector(0, St.Step == 1 ? -250 : -500, 0));
        Face(P, F);
        St.A = F->Hp;
        Mgr->PressMove(0);
        St.I = 1;
        return EGameTest::Running;
    }
    if (!CaptionSeen(Mgr, TEXT("It's super effective!"))) { if (Test.Seconds - St.T0 > 5) PYR_FAIL(TEXT("no super-effective caption (last '%s', move %s)"), *Mgr->LastCaption().ToString(), *P->LastMove.ToString()); return EGameTest::Running; }
    if (Mgr->LastFactor < 2.f || F->Hp >= St.A) PYR_FAIL(TEXT("factor %.1f, damage %.0f"), Mgr->LastFactor, St.A - F->Hp);
    St.Note += FString::Printf(TEXT("%s on %s: x%.0f, %.0f damage, 'It's super effective!'; "), *P->LastMove.ToString(), *F->Species.ToString(), Mgr->LastFactor, St.A - F->Hp);
    if (St.Step == 3) PYR_PASS(TEXT("%s"), *St.Note);
    St.Step = 2;
    return EGameTest::Running;
}

PYR_TEST(FxEveryMoveVisible, "Battle.Fx.EveryMoveVisible", 300)
{
    static FState St;
    static FString Summary;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    const TArray<FName>& Moves = AllMoves();
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    // A fresh fight at the start, and after Selfdestruct (the user faints) or Whirlwind (the wild foe is blown away).
    const bool bOver = St.Step != 0 && (!Mgr->IsBattleActive() || Mgr->IsEnding() || !P || !F || P->bFainted || F->bFainted);
    if (St.Step == 0 || (bOver && St.Step == 1))
    {
        if (Mgr->IsMenuOpen()) if (UPYRMenus* Me = UPYRMenus::Get(Test.World)) Me->CloseAll();  // m4t: a learn prompt, dropped (the fight restarts)
        Fight(Test, {Mon(Pikachu, 60)}, {Mon(Rattata, 60)}, EPYRBattleKind::Wild, FVector(0, 500, 0));
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (bOver && St.Step == 2 && St.I < Moves.Num() && Mgr->FxLog.FindRef(Moves[St.I]) > St.A)
    {
        // The move that ended the fight showed its effect first: count it and start again.
        Summary += FString::Printf(TEXT("%s %.2fs (fight over); "), *Moves[St.I].ToString(), Test.Seconds - St.C);
        ++St.I;
        St.Step = 1;
        return EGameTest::Running;
    }
    if (!P || !F) PYR_FAIL(TEXT("fighters missing at %d"), St.I);
    PYR_WAIT(1.2);
    if (St.I >= Moves.Num()) PYR_PASS(TEXT("%d moves, each spawned a visible effect: %s"), Moves.Num(), *Summary);
    const FName MoveId = Moves[St.I];
    P->Hp = P->MaxHp; F->MaxHp = F->Hp = 100000.f;  // Nothing faints, so the fight never ends mid-list.
    if (St.Step == 1)
    {
        if (P->IsBiding() || Test.World->GetTimeSeconds() < P->BusyUntil) return EGameTest::Running;
        Place(P, F->GetActorLocation() + FVector(0, -300, 0));
        Face(P, F);
        P->Carried = {MoveId};
        Mgr->DebugResetCooldowns();
        St.A = Mgr->FxLog.FindRef(MoveId);
        Mgr->PressMove(0);
        if (P->LastMove != MoveId) PYR_FAIL(TEXT("%s did not fire"), *MoveId.ToString());
        St.Step = 2; St.C = Test.Seconds;
        return EGameTest::Running;
    }
    if (Mgr->FxLog.FindRef(MoveId) <= St.A)
    {
        if (Test.Seconds - St.C > 5) PYR_FAIL(TEXT("%s spawned no visible effect in 5 s"), *MoveId.ToString());
        return EGameTest::Running;
    }
    Summary += FString::Printf(TEXT("%s %.2fs; "), *MoveId.ToString(), Test.Seconds - St.C);
    ++St.I;
    St.Step = 1;
    return EGameTest::Running;
}

PYR_TEST(DataYellowRoster, "Battle.Data.YellowRoster", 30)
{
    PYR_READY();
    struct FWant { FName Id; EPYRBody Body; };
    const FWant Want[] = {
        {Pikachu, EPYRBody::Runner}, {Eevee, EPYRBody::Runner}, {Rattata, EPYRBody::Runner}, {NidoranF, EPYRBody::Runner}, {NidoranM, EPYRBody::Runner},
        {Mankey, EPYRBody::Runner}, {Sandshrew, EPYRBody::Runner}, {Pidgey, EPYRBody::Flier}, {Pidgeotto, EPYRBody::Flier}, {Spearow, EPYRBody::Flier},
        {Butterfree, EPYRBody::Flier}, {Caterpie, EPYRBody::Crawler}, {Weedle, EPYRBody::Crawler}, {Metapod, EPYRBody::Crawler},
        {Kakuna, EPYRBody::Crawler}, {Geodude, EPYRBody::Crawler}, {Diglett, EPYRBody::Crawler}, {Onix, EPYRBody::Giant}};
    for (const FWant& W : Want)
    {
        const FPYRSpeciesInfo* Info = PYRSpecies::Find(W.Id);
        if (!Info) PYR_FAIL(TEXT("%s missing"), *W.Id.ToString());
        if (Info->Body != W.Body) PYR_FAIL(TEXT("%s body %d, want %d"), *W.Id.ToString(), int32(Info->Body), int32(W.Body));
        if (Info->HeightM <= 0 || Info->Learnset.Num() == 0) PYR_FAIL(TEXT("%s has no height or learnset"), *W.Id.ToString());
        for (const FPYRLearn& L : Info->Learnset) if (!PYRSpecies::FindMove(L.Move)) PYR_FAIL(TEXT("%s learns unknown %s"), *W.Id.ToString(), *L.Move.ToString());
    }
    if (!PYRSpecies::FindMove(Bide)) PYR_FAIL(TEXT("Bide (TM34) missing"));
    const FPYRSpeciesInfo& Pk = *PYRSpecies::Find(Pikachu);
    if (Pk.BaseHp != 35 || Pk.BaseAttack != 55 || Pk.BaseDefence != 30 || Pk.BaseSpeed != 90 || Pk.BaseSpecial != 50) PYR_FAIL(TEXT("Pikachu base stats"));
    if (DefaultMoves(Pikachu, 5) != TArray<FName>{ThunderShock, Growl}) PYR_FAIL(TEXT("Pikachu 5 moves"));
    if (MovesLearned(Pikachu, 5, 6) != TArray<FName>{TailWhip} || MovesLearned(Pikachu, 7, 8) != TArray<FName>{ThunderWave}) PYR_FAIL(TEXT("Pikachu Tail Whip 6 / Thunder Wave 8"));
    // Yellow (special_moves.asm): Brock's Onix gets Bind and Bide; his Geodude 10 knows only Tackle.
    if (PYRSpecies::MovesFor(Mon(Geodude, 10)) != TArray<FName>{Tackle}) PYR_FAIL(TEXT("Geodude 10 moves"));
    if (PYRSpecies::MovesFor(Mon(Onix, 12)) != TArray<FName>{Tackle, Screech, Bind, Bide}) PYR_FAIL(TEXT("Onix 12 moves"));
    if (EvolutionAt(Caterpie, 7) != Metapod || EvolutionAt(Caterpie, 6) != NAME_None || EvolutionAt(Metapod, 10) != Butterfree || EvolutionAt(Weedle, 7) != Kakuna)
        PYR_FAIL(TEXT("evolutions"));
    if (Effectiveness(EPYRType::Electric, *PYRSpecies::Find(Geodude)) != 0.f || Effectiveness(EPYRType::Electric, *PYRSpecies::Find(Diglett)) != 0.f)
        PYR_FAIL(TEXT("Electric must do nothing to Ground"));
    const FPYRMoveInfo& Ts = *PYRSpecies::FindMove(ThunderShock);
    if (Ts.Power != 40 || Ts.Type != EPYRType::Electric || Ts.Tier != EPYRMoveTier::Quick) PYR_FAIL(TEXT("Thundershock data"));
    if (PYRSpecies::FindMove(ThunderWave)->Tier != EPYRMoveTier::Big || PYRSpecies::FindMove(Growl)->Tier != EPYRMoveTier::Tactical) PYR_FAIL(TEXT("cooldown classes"));
    if (PYRSpecies::AssetId(NidoranF) != TEXT("nidoran_f")) PYR_FAIL(TEXT("asset id %s"), *PYRSpecies::AssetId(NidoranF));
    PYR_PASS(TEXT("18 m2 species with Yellow bodies, heights and learnsets; Pikachu 5 = Thundershock+Growl, Tail Whip 6, Thunder Wave 8; Geodude 10 Tackle; Onix 12 Tackle+Screech+Bind+Bide; Caterpie 7/10 and Weedle 7 evolve; Electric x0 on Ground"));
}

// ------------------------------------------------------------------------------------------------ bodies
PYR_TEST(BodyLedgeRules, "Battle.Body.LedgeRules", 90)
{
    // The pad's 100 cm ledge sits inside a Route 1-sized ring (the ring interface only bounds movement).
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    const FVector Pad = UPYRBattleWorld::PadCentre;
    if (St.Step == 0 || St.Step == 4)
    {
        Fight(Test, {Mon(St.Step == 0 ? Pidgey : Pikachu, 8)}, {Mon(Caterpie, 3)}, EPYRBattleKind::Wild, FVector(-500, 1200, 0));
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P) PYR_FAIL(TEXT("no fighter"));
    switch (St.Step)
    {
    case 1: case 5:
        PYR_WAIT(1.2);
        Place(P, Pad + FVector(-300, 0, 0));
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    case 2:  // Pidgey up.
        P->Steer(FVector(1, 0, 0));
        if (P->GetActorLocation().X > Pad.X + 800 && P->FloorZ() > 90.f) { St.A = P->GetActorLocation().Z; St.Step = 3; St.T0 = Test.Seconds; }
        else if (Test.Seconds - St.T0 > 5) PYR_FAIL(TEXT("Pidgey could not fly up onto the ledge (x %.0f)"), P->GetActorLocation().X - Pad.X);
        return EGameTest::Running;
    case 3:  // Pidgey down.
        P->Steer(FVector(-1, 0, 0));
        if (P->GetActorLocation().X < Pad.X - 200 && P->FloorZ() < 10.f) { St.Note = FString::Printf(TEXT("Pidgey up and back down; "));  St.Step = 4; return EGameTest::Running; }
        if (Test.Seconds - St.T0 > 5) PYR_FAIL(TEXT("Pidgey could not come back down (x %.0f)"), P->GetActorLocation().X - Pad.X);
        return EGameTest::Running;
    case 6:  // Pikachu blocked going up.
        P->Steer(FVector(1, 0, 0));
        if (P->FloorZ() > 50.f) PYR_FAIL(TEXT("Pikachu climbed the 100 cm ledge"));
        PYR_WAIT(2.0);
        St.B = P->GetActorLocation().X - Pad.X;
        if (St.B > 400) PYR_FAIL(TEXT("Pikachu passed the ledge face (x %.0f)"), St.B);
        Place(P, Pad + FVector(900, 0, 0));
        St.Step = 7; St.T0 = Test.Seconds;
        return EGameTest::Running;
    case 7:  // Pikachu hops down.
        P->Steer(FVector(-1, 0, 0));
        if (P->GetActorLocation().X < Pad.X && P->FloorZ() < 10.f && P->BottomZ() < 20.f)
            PYR_PASS(TEXT("%sPikachu blocked below the ledge at x %.0f and hopped down from the top"), *St.Note, St.B);
        if (Test.Seconds - St.T0 > 5) PYR_FAIL(TEXT("Pikachu could not hop down (x %.0f z %.0f)"), P->GetActorLocation().X - Pad.X, P->BottomZ());
    }
    return EGameTest::Running;
}

PYR_TEST(BodyFlierDodgeDistance, "Battle.Body.FlierDodgeDistance", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    const FVector Pad = UPYRBattleWorld::PadCentre;
    const FName Who = St.Step < 3 ? Pikachu : Pidgey;
    if (St.Step == 0 || St.Step == 3) { Fight(Test, {Mon(Who, 8)}, {Mon(Caterpie, 3)}, EPYRBattleKind::Wild, FVector(-500, 1200, 0)); ++St.Step; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P) PYR_FAIL(TEXT("no fighter"));
    if (St.Step == 1 || St.Step == 4)
    {
        PYR_WAIT(1.2);
        Place(P, Pad + FVector(-500, 400, 0));
        St.V = P->GetActorLocation();
        P->LastSteer = FVector(-1, 0, 0);
        Mgr->PressDodge();
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(.8);
    const float D = FVector::Dist2D(St.V, P->GetActorLocation());
    if (St.Step == 2) { St.A = D; St.Step = 3; return EGameTest::Running; }
    const float Ratio = D / FMath::Max(1.f, St.A);
    if (Ratio < 1.6f || Ratio > 2.4f) PYR_FAIL(TEXT("Pikachu dash %.0f cm, Pidgey dodge %.0f cm, ratio %.2f"), St.A, D, Ratio);
    PYR_PASS(TEXT("Pikachu dash %.0f cm, Pidgey sideways dodge %.0f cm, ratio %.2f"), St.A, D, Ratio);
}

PYR_TEST(BodySweepOverCrawler, "Battle.Body.SweepOverCrawler", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    const FName Who = St.Step < 3 ? Caterpie : Pikachu;
    if (St.Step == 0 || St.Step == 3) { Fight(Test, {Mon(Who, 12)}, {Mon(Onix, 12)}, EPYRBattleKind::Gym); ++St.Step; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    if (St.Step == 1 || St.Step == 4)
    {
        PYR_WAIT(1.2);
        Place(P, F->GetActorLocation() + F->GetActorForwardVector() * 450.f);
        St.A = P->Hp;
        Mgr->DebugForceFoeMove(Tackle);  // Onix's Tackle is a wide tail sweep.
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(2.5);
    if (St.Step == 2)
    {
        if (P->Hp != St.A) PYR_FAIL(TEXT("the tail sweep hit Caterpie for %.0f (top %.0f cm)"), St.A - P->Hp, P->TopZ() - P->FloorZ());
        St.C = P->TopZ() - P->FloorZ();
        St.Step = 3;
        return EGameTest::Running;
    }
    if (P->Hp >= St.A) PYR_FAIL(TEXT("control: the sweep missed Pikachu too"));
    PYR_PASS(TEXT("Onix's tail sweep passed over Caterpie (top %.0f cm) and hit Pikachu for %.0f"), St.C, St.A - P->Hp);
}

PYR_TEST(BodyRockGroundsFlier, "Battle.Body.RockGroundsFlier", 60)
{
    static FState St;
    static double Down = -1, Up = -1;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, {Mon(Pidgey, 20)}, {Mon(Geodude, 16)}, EPYRBattleKind::Trainer); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    if (St.Step == 1)
    {
        PYR_WAIT(1.2);
        Place(P, F->GetActorLocation() + FVector(0, -700, 0));
        St.A = P->Hp;
        Mgr->DebugForceFoeMove(RockThrow);
        if (Mgr->TellLog.Num() == 0 || Mgr->TellLog.Last().Move != RockThrow) PYR_FAIL(TEXT("Geodude 16 has no Rock Throw"));
        St.Step = 2; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    const bool bOnFloor = P->BottomZ() - P->FloorZ() < 15.f;
    const double T = Test.World->GetTimeSeconds();
    if (bOnFloor && Down < 0) Down = T;
    if (!bOnFloor && Down >= 0 && Up < 0) Up = T;
    if (Up >= 0)
    {
        const double On = Up - Down;
        if (P->Hp >= St.A) PYR_FAIL(TEXT("Rock Throw did no damage"));
        if (On < .7 || On > 1.4) PYR_FAIL(TEXT("on the floor for %.2f s"), On);
        PYR_PASS(TEXT("Rock Throw hit (%.0f) and grounded Pidgey for %.2f s, then it hovered again"), St.A - P->Hp, On);
    }
    if (Test.Seconds - St.T0 > 6) PYR_FAIL(TEXT("Pidgey never grounded (hp %.0f/%.0f)"), P->Hp, St.A);
    return EGameTest::Running;
}

PYR_TEST(BodySpeedsDiffer, "Battle.Body.SpeedsDiffer", 60)
{
    static FState St;
    static float Speeds[3];
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    const FName Who[3] = {Pikachu, Pidgey, Caterpie};
    const FVector Pad = UPYRBattleWorld::PadCentre;
    const int32 K = St.Step / 3;
    if (K >= 3)
    {
        if (!Near(Speeds[0], 700, 70) || !Near(Speeds[1], 550, 55) || !Near(Speeds[2], 350, 35)) PYR_FAIL(TEXT("speeds %.0f / %.0f / %.0f"), Speeds[0], Speeds[1], Speeds[2]);
        PYR_PASS(TEXT("runner Pikachu %.0f, flier Pidgey %.0f, crawler Caterpie %.0f cm/s"), Speeds[0], Speeds[1], Speeds[2]);
    }
    if (St.Step % 3 == 0) { Fight(Test, {Mon(Who[K], 8)}, {Mon(Caterpie, 3)}, EPYRBattleKind::Wild, FVector(0, 1300, 0)); ++St.Step; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P) PYR_FAIL(TEXT("no fighter"));
    if (St.Step % 3 == 1)
    {
        PYR_WAIT(1.2);
        Place(P, Pad + FVector(-200, -700, 0));
        ++St.Step; St.T0 = Test.Seconds; St.V = FVector::ZeroVector;
        return EGameTest::Running;
    }
    P->Steer(FVector(-1, 0, 0));
    const double Run = Test.Seconds - St.T0;
    if (Run > .5 && St.V.IsZero()) { St.V = P->GetActorLocation(); St.A = Test.Seconds; }
    if (Run < 1.5) return EGameTest::Running;
    Speeds[K] = FVector::Dist2D(St.V, P->GetActorLocation()) / float(Test.Seconds - St.A);
    ++St.Step;
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ swapping and Potions
static TArray<FPYRMonSpec> Trio() { return {Mon(Pikachu, 8), Mon(Pidgey, 8), Mon(Caterpie, 8)}; }

PYR_TEST(SwapWheelBringsMon, "Battle.Swap.WheelBringsMon", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, Trio(), {Mon(Pidgey, 3)}, EPYRBattleKind::Wild, FVector(0, 900, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    Mgr->OpenWheel();
    if (!Mgr->IsWheelOpen()) PYR_FAIL(TEXT("Tab did not open the wheel"));
    Mgr->PressSwap(2);  // Key 3 on the wheel.
    if (!Mgr->GetPlayerMon() || Mgr->GetPlayerMon()->Species != Caterpie || Mgr->ActiveIndex() != 2 || Mgr->IsWheelOpen())
        PYR_FAIL(TEXT("key 3 did not bring Caterpie"));
    Mgr->DebugResetCooldowns();
    Mgr->OpenWheel();
    Mgr->CloseWheel(FVector2D(.87f, -.5f));  // Toward the lower-right slot (Pidgey, 120 degrees).
    if (!Mgr->GetPlayerMon() || Mgr->GetPlayerMon()->Species != Pidgey) PYR_FAIL(TEXT("direction pick did not bring Pidgey"));
    PYR_PASS(TEXT("wheel: key 3 brought Caterpie, a lower-right flick brought Pidgey"));
}

PYR_TEST(SwapCooldown30, "Battle.Swap.Cooldown30", 90)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, Trio(), {Mon(Pidgey, 3)}, EPYRBattleKind::Wild, FVector(0, 900, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    const double T = Test.Seconds - St.T0;
    switch (St.Step)
    {
    case 1:
        if (T < 1.2) return EGameTest::Running;
        Mgr->PressSwap(1);
        if (Mgr->ActiveIndex() != 1) PYR_FAIL(TEXT("first swap failed"));
        St.T0 = Test.Seconds; St.Step = 2;
        return EGameTest::Running;
    case 2:
        if (T < 1) return EGameTest::Running;
        St.A = Mgr->SwapSecondsLeft();
        Mgr->PressSwap(2);
        if (Mgr->ActiveIndex() != 1) PYR_FAIL(TEXT("second swap allowed after 1 s"));
        if (St.A < 28.f || St.A > 29.5f) PYR_FAIL(TEXT("timer shows %.1f s after 1 s"), St.A);
        if (!Mgr->Shout.Contains(TEXT("Not yet"))) PYR_FAIL(TEXT("no timer shown on the blocked swap ('%s')"), *Mgr->Shout);
        St.Note = Mgr->Shout;
        St.Step = 3;
        return EGameTest::Running;
    case 3:
        if (T < 29.5) return EGameTest::Running;
        Mgr->PressSwap(2);
        if (Mgr->ActiveIndex() != 1) PYR_FAIL(TEXT("swap allowed at 29.5 s"));
        St.Step = 4;
        return EGameTest::Running;
    case 4:
        if (T < 30.1) return EGameTest::Running;
        Mgr->PressSwap(2);
        if (Mgr->ActiveIndex() != 2) PYR_FAIL(TEXT("swap still blocked at 30.1 s"));
        PYR_PASS(TEXT("blocked at 1 s (timer %.1f s, Red: '%s') and at 29.5 s, allowed at 30.1 s"), St.A, *St.Note);
    }
    return EGameTest::Running;
}

PYR_TEST(SwapIncomingDodge, "Battle.Swap.IncomingDodge", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, Trio(), {Mon(Pidgey, 8)}); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    if (St.Step == 1)
    {
        PYR_WAIT(1.2);
        Place(P, F->GetActorLocation() - FVector(0, 350, 0));
        Face(F, P);
        Mgr->DebugForceFoeMove(Gust);
        St.Step = 2;
        return EGameTest::Running;
    }
    APYRAttack* A = FoeAttack(Test.World);
    if (St.Step == 2)
    {
        if (!A || A->SecondsToHit() > .05) return EGameTest::Running;
        Mgr->PressSwap(2);  // Caterpie lands right as the Gust does.
        if (!Mgr->GetPlayerMon() || Mgr->GetPlayerMon()->Species != Caterpie) PYR_FAIL(TEXT("swap failed"));
        St.A = Mgr->GetPlayerMon()->Hp;
        St.B = float(Mgr->GetPlayerMon()->InvulnerableUntil - Test.World->GetTimeSeconds());
        St.Step = 3; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(1.0);
    if (Mgr->GetPlayerMon()->Hp != St.A) PYR_FAIL(TEXT("incoming Caterpie took %.0f"), St.A - Mgr->GetPlayerMon()->Hp);
    if (!Near(St.B, .5f, .05f)) PYR_FAIL(TEXT("untouchable for %.2f s"), St.B);
    PYR_PASS(TEXT("the Gust landed on the incoming Caterpie during its %.2f s untouchable window: 0 damage"), St.B);
}

PYR_TEST(SwapFaintForces, "Battle.Swap.FaintForces", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, Trio(), {Mon(Pidgey, 8)}); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    Mgr->PressSwap(1);  // Starts the 30 s cooldown.
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P || Mgr->SwapSecondsLeft() < 25) PYR_FAIL(TEXT("setup swap failed"));
    P->Hp = 1;
    P->InvulnerableUntil = 0;
    Mgr->ResolveHit(Mgr->GetFoeMon(), P, *Move(Tackle), P->GetActorLocation());
    if (!P->bFainted) PYR_FAIL(TEXT("Pidgey did not faint"));
    if (!CaptionSeen(Mgr, TEXT("Pidgey fainted!"))) PYR_FAIL(TEXT("no faint caption"));
    if (!Mgr->IsWheelOpen() || !Mgr->IsWheelForced()) PYR_FAIL(TEXT("faint did not force the wheel open"));
    St.A = Mgr->SwapSecondsLeft();
    Mgr->PressSwap(2);
    if (!Mgr->GetPlayerMon() || Mgr->GetPlayerMon()->Species != Caterpie || !Mgr->IsBattleActive()) PYR_FAIL(TEXT("forced swap blocked"));
    PYR_PASS(TEXT("'Pidgey fainted!' opened the wheel; Caterpie came in with %.0f s of swap cooldown left"), St.A);
}

PYR_TEST(PotionHealOverTime, "Battle.Potion.HealOverTime", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, {Mon(Pikachu, 12)}, {Mon(Pidgey, 5)}); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    const double T = Test.Seconds - St.T0;
    switch (St.Step)
    {
    case 1:
        if (T < 1.2) return EGameTest::Running;
        P->Hp = 5;
        St.I = S(Test)->ItemCount(UPYRTrainerState::Potion);
        Mgr->PressPotion();
        if (S(Test)->ItemCount(UPYRTrainerState::Potion) != St.I - 1) PYR_FAIL(TEXT("Potion not used"));
        St.T0 = Test.Seconds; St.Step = 2;
        return EGameTest::Running;
    case 2:
        if (T < 1.0) return EGameTest::Running;
        St.A = P->Hp;
        St.Step = 3;
        return EGameTest::Running;
    case 3:
        if (T < 2.5) return EGameTest::Running;
        if (!Near(St.A, 15, 1.5f)) PYR_FAIL(TEXT("after 1 s HP %.1f (want ~15)"), St.A);
        if (!Near(P->Hp, 25, .5f)) PYR_FAIL(TEXT("after 2.5 s HP %.1f (want 25)"), P->Hp);
        St.Note = FString::Printf(TEXT("5 -> %.1f after 1 s -> %.1f after 2.5 s; "), St.A, P->Hp);
        // Second Potion, cut short by a Gust.
        Mgr->DebugResetCooldowns();
        Place(P, F->GetActorLocation() - FVector(0, 400, 0));
        Face(F, P);
        P->Hp = 5;
        Mgr->PressPotion();
        Mgr->DebugForceFoeMove(Gust);
        St.Step = 4; St.T0 = Test.Seconds;
        return EGameTest::Running;
    case 4:
    {
        APYRAttack* A = FoeAttack(Test.World);
        if (A && A->Phase != APYRAttack::EPhase::Done && !A->bHit) return EGameTest::Running;
        St.B = P->Hp;
        if (P->HealLeft > 0) PYR_FAIL(TEXT("heal kept going after the hit (%.1f left)"), P->HealLeft);
        St.Step = 5; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    case 5:
        if (T < 2.0) return EGameTest::Running;
        if (P->Hp > St.B + .01f) PYR_FAIL(TEXT("HP rose from %.1f to %.1f after the hit"), St.B, P->Hp);
        PYR_PASS(TEXT("%sa Gust at %.1f HP stopped the second Potion (no more HP over 2 s)"), *St.Note, St.B);
    }
    return EGameTest::Running;
}

PYR_TEST(PotionCooldown10, "Battle.Potion.Cooldown10", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, {Mon(Pikachu, 9)}, {Mon(Pidgey, 3)}, EPYRBattleKind::Wild, FVector(0, 900, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P) PYR_FAIL(TEXT("no Pikachu"));
    const double T = Test.Seconds - St.T0;
    switch (St.Step)
    {
    case 1:
        if (T < 1.2) return EGameTest::Running;
        P->Hp = 5;
        St.I = S(Test)->ItemCount(UPYRTrainerState::Potion);
        Mgr->PressPotion();
        Mgr->PressPotion();
        if (S(Test)->ItemCount(UPYRTrainerState::Potion) != St.I - 1) PYR_FAIL(TEXT("second Potion allowed at once"));
        St.T0 = Test.Seconds; St.Step = 2;
        return EGameTest::Running;
    case 2:
        if (T < 9.5) return EGameTest::Running;
        P->Hp = 5;
        St.A = Mgr->PotionSecondsLeft();
        Mgr->PressPotion();
        if (S(Test)->ItemCount(UPYRTrainerState::Potion) != St.I - 1) PYR_FAIL(TEXT("Potion allowed at 9.5 s"));
        St.Step = 3;
        return EGameTest::Running;
    case 3:
        if (T < 10.2) return EGameTest::Running;
        Mgr->PressPotion();
        if (S(Test)->ItemCount(UPYRTrainerState::Potion) != St.I - 2) PYR_FAIL(TEXT("Potion still blocked at 10.2 s"));
        PYR_PASS(TEXT("blocked at once and at 9.5 s (%.1f s left), allowed at 10.2 s"), St.A);
    }
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ catching
PYR_TEST(CatchMeterColours, "Battle.Catch.MeterColours", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, {Mon(Pikachu, 9)}, {Mon(Pidgey, 5)}, EPYRBattleKind::Wild, FVector(0, 900, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!F) PYR_FAIL(TEXT("no foe"));
    // Sweep HP down: the band must go red -> yellow -> green and never back, and the chance must rise.
    int32 Last = -1, Seen = 0;
    float LastChance = -1;
    FString Trace;
    for (int32 Pct = 100; Pct >= 1; Pct -= 3)
    {
        F->Hp = FMath::Max(1.f, FMath::RoundToFloat(F->MaxHp * Pct / 100.f));
        const int32 Band = Mgr->CatchMeterBand();
        const float Chance = Mgr->CatchChance();
        if (Band < Last) PYR_FAIL(TEXT("band fell from %d to %d at %d%%"), Last, Band, Pct);
        if (Chance + 1e-4f < LastChance) PYR_FAIL(TEXT("chance fell at %d%%"), Pct);
        if (Band != Last) { Trace += FString::Printf(TEXT("%s from %d%% (%.2f); "), Band == 0 ? TEXT("red") : Band == 1 ? TEXT("yellow") : TEXT("green"), Pct, Chance); Seen |= 1 << Band; }
        Last = Band; LastChance = Chance;
    }
    if (Seen != 7) PYR_FAIL(TEXT("not every colour shown: %s"), *Trace);
    F->Hp = F->MaxHp;
    if (Mgr->CatchMeterBand() != 0) PYR_FAIL(TEXT("full HP is not red"));
    PYR_PASS(TEXT("Yellow's Poke Ball odds on Pidgey: %s"), *Trace);
}

PYR_TEST(CatchUnstunnedDodges, "Battle.Catch.UnstunnedDodges", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, {Mon(Pikachu, 9)}, {Mon(Pidgey, 5)}, EPYRBattleKind::Wild, FVector(0, 900, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    if (St.Step == 1)
    {
        Mgr->ForcedBallDodgeRoll = .1f;
        St.I = S(Test)->ItemCount(UPYRTrainerState::PokeBall);
        Mgr->BeginAim();
        if (!Mgr->IsAiming()) PYR_FAIL(TEXT("F did not start aiming"));
        Mgr->ReleaseAim();
        if (S(Test)->ItemCount(UPYRTrainerState::PokeBall) != St.I - 1) PYR_FAIL(TEXT("ball not thrown"));
        St.Step = 2; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (Mgr->IsBallBusy()) { if (Test.Seconds - St.T0 > 5) PYR_FAIL(TEXT("ball never resolved")); return EGameTest::Running; }
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!Mgr->IsBattleActive() || !F || F->IsHidden()) PYR_FAIL(TEXT("the ball caught an unstunned foe that should dodge"));
    if (Mgr->BallsDodged != 1) PYR_FAIL(TEXT("no dodge counted"));
    if (!F->BodyComp || F->BodyComp->IsDodgeReady()) PYR_FAIL(TEXT("foe did not use its dodge"));
    PYR_PASS(TEXT("the unstunned Pidgey dodged the ball ('%s'): ball lost, fight on"), *Mgr->LastCaption().ToString());
}

PYR_TEST(CatchPartyOrStorage, "Battle.Catch.PartyOrStorage", 90)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    UPYRTrainerState* State = S(Test);
    if (St.Step == 0 || St.Step == 3)
    {
        if (St.Step == 0) Fight(Test, {Mon(Pikachu, 9)}, {Mon(Pidgey, 4)}, EPYRBattleKind::Wild, FVector(0, 900, 0));
        else Fight(Test, {Mon(Pikachu, 9), Mon(Pidgey, 5), Mon(Rattata, 5), Mon(Caterpie, 5), Mon(Spearow, 5), Mon(Mankey, 5)}, {Mon(Rattata, 4)},
            EPYRBattleKind::Wild, FVector(0, 900, 0));
        State->Storage.Reset();
        State->Caught.Reset();
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 1 || St.Step == 4)
    {
        PYR_WAIT(1.2);
        Mgr->DebugSetFoeHp(.15f);
        Mgr->GetFoeMon()->StunnedUntil = Test.World->GetTimeSeconds() + 10;
        Mgr->ForcedCatchRoll = 0.f;
        Mgr->PressBall();
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (Mgr->IsBattleActive()) { if (Test.Seconds - St.T0 > 8) PYR_FAIL(TEXT("no catch after 8 s (wobbles %d)"), Mgr->WobblesShown()); return EGameTest::Running; }
    if (Mgr->LastOutcome != EPYRBattleOutcome::Caught) PYR_FAIL(TEXT("battle ended %d, not Caught"), int32(Mgr->LastOutcome));
    if (St.Step == 2)
    {
        if (State->Party.Num() != 2 || State->Party[1].Species != Pidgey) PYR_FAIL(TEXT("Pidgey not in the party"));
        if (!State->Caught.Contains(Pidgey)) PYR_FAIL(TEXT("MarkCaught not called for Pidgey"));
        if (!CaptionSeen(Mgr, TEXT("All right! Pidgey was caught!"))) PYR_FAIL(TEXT("no catch caption"));
        St.Note = FString::Printf(TEXT("Pidgey Lv %d joined the party; "), State->Party[1].Level);
        St.Step = 3;
        return EGameTest::Running;
    }
    if (State->Party.Num() != 6 || State->Storage.Num() != 1 || State->Storage[0].Species != Rattata) PYR_FAIL(TEXT("party %d, storage %d"), State->Party.Num(), State->Storage.Num());
    if (!State->Caught.Contains(Rattata)) PYR_FAIL(TEXT("MarkCaught not called for Rattata"));
    PYR_PASS(TEXT("%swith a full party Rattata went to Bill's PC (storage %d); both marked caught"), *St.Note, State->Storage.Num());
}

PYR_TEST(CatchTrainerBlocks, "Battle.Catch.TrainerBlocks", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Fight(Test, {Mon(Pikachu, 9)}, {Mon(Caterpie, 6), Mon(Weedle, 6)}, EPYRBattleKind::Trainer); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    if (St.Step == 1)
    {
        Mgr->GetFoeMon()->StunnedUntil = Test.World->GetTimeSeconds() + 10;
        Mgr->ForcedCatchRoll = 0.f;
        St.I = S(Test)->ItemCount(UPYRTrainerState::PokeBall);
        Mgr->PressBall();
        St.Step = 2; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (Mgr->IsBallBusy()) { if (Test.Seconds - St.T0 > 4) PYR_FAIL(TEXT("ball never resolved")); return EGameTest::Running; }
    if (!Mgr->IsBattleActive() || S(Test)->Party.Num() != 1) PYR_FAIL(TEXT("the trainer's Caterpie was caught"));
    if (Mgr->BallsBlocked != 1 || !CaptionSeen(Mgr, TEXT("The trainer blocked the BALL! Don't be a thief!"))) PYR_FAIL(TEXT("no block (caption '%s')"), *Mgr->LastCaption().ToString());
    if (!Mgr->GetFoeMon() || Mgr->GetFoeMon()->IsHidden()) PYR_FAIL(TEXT("the foe vanished"));
    PYR_PASS(TEXT("batted away: 'The trainer blocked the BALL! Don't be a thief!' (balls %d -> %d)"), St.I, S(Test)->ItemCount(UPYRTrainerState::PokeBall));
}

// ------------------------------------------------------------------------------------------------ Pikachu
PYR_TEST(PikachuNeverInBall, "Battle.Pikachu.NeverInBall", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0)
    {
        St.I = Mgr->BallEffectsSpawned;
        Fight(Test, Trio(), {Mon(Pidgey, 3)}, EPYRBattleKind::Wild, FVector(0, 900, 0));
        if (Mgr->BallEffectsSpawned != St.I) PYR_FAIL(TEXT("Pikachu came out of a ball"));
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(1.2);
    if (St.Step == 1)
    {
        APYRMonPawn* P = Mgr->GetPlayerMon();
        if (!P || P->Species != Pikachu) PYR_FAIL(TEXT("no Pikachu"));
        if (!Mgr->GetRed()) PYR_FAIL(TEXT("no Red"));
        St.A = FVector::Dist2D(P->GetActorLocation(), Mgr->GetRed()->GetActorLocation());
        St.I = Mgr->BallEffectsSpawned;
        Mgr->PressSwap(2);
        if (Mgr->BallEffectsSpawned != St.I + 1) PYR_FAIL(TEXT("recalling Pikachu used a ball (%d effects)"), Mgr->BallEffectsSpawned - St.I);
        if (!Mgr->SittingPikachu()) PYR_FAIL(TEXT("Pikachu vanished"));
        St.Step = 2; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    APYRMonPawn* Side = Mgr->SittingPikachu();
    if (!Side || Side->IsHidden()) PYR_FAIL(TEXT("Pikachu not visible after the swap"));
    PYR_WAIT(3.5);
    const float D = FVector::Dist2D(Side->GetActorLocation(), Mgr->GetRed()->GetActorLocation());
    if (D > 250) PYR_FAIL(TEXT("Pikachu is %.0f cm from Red"), D);
    PYR_PASS(TEXT("Pikachu ran in from Red's side (no ball), and when swapped out ran back and sits %.0f cm from Red (only Caterpie's ball opened)"), D);
}

PYR_TEST(PikachuFaintLowersBond, "Battle.Pikachu.FaintLowersBond", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { S(Test)->BondPoints = 90; Fight(Test, {Mon(Pikachu, 9), Mon(Pidgey, 9)}, {Mon(Rattata, 6)}); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P || P->Species != Pikachu) PYR_FAIL(TEXT("no Pikachu"));
    St.A = S(Test)->BondPoints;
    P->Hp = 1;
    P->InvulnerableUntil = 0;
    Mgr->ResolveHit(Mgr->GetFoeMon(), P, *Move(Tackle), P->GetActorLocation());
    if (!P->bFainted) PYR_FAIL(TEXT("Pikachu did not faint"));
    if (S(Test)->BondPoints >= St.A) PYR_FAIL(TEXT("friendship %0.f -> %d"), St.A, S(Test)->BondPoints);
    PYR_PASS(TEXT("Pikachu fainted: friendship %.0f -> %d"), St.A, S(Test)->BondPoints);
}

PYR_TEST(PikachuCloseAutoDodge, "Battle.Pikachu.CloseAutoDodge", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0)
    {
        S(Test)->BondPoints = UPYRTrainerState::BondThreshold(EPYRBond::Close) + 10;
        Fight(Test, {Mon(Pikachu, 9)}, {Mon(Pidgey, 6)});
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    if (St.Step == 1 || St.Step == 3)
    {
        PYR_WAIT(1.2);
        Place(P, F->GetActorLocation() - FVector(0, 400, 0));
        Face(F, P);
        Mgr->DebugResetCooldowns();
        St.A = P->Hp;
        Mgr->DebugForceFoeMove(Gust);
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(1.5);
    if (St.Step == 2)
    {
        if (P->Hp != St.A || Mgr->AutoDodges != 1) PYR_FAIL(TEXT("no auto-dodge at Close (took %.0f)"), St.A - P->Hp);
        St.Step = 3; St.T0 = Test.Seconds - 1.2;
        return EGameTest::Running;
    }
    if (P->Hp >= St.A || Mgr->AutoDodges != 1) PYR_FAIL(TEXT("auto-dodged twice in one battle"));
    PYR_PASS(TEXT("at Close friendship (%d) Pikachu dodged the first Gust by itself, and only once (the second hit for %.0f)"), S(Test)->BondPoints, St.A - P->Hp);
}

// ------------------------------------------------------------------------------------------------ growth
PYR_TEST(GrowthExpToFighter, "Battle.Growth.ExpToFighter", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    UPYRTrainerState* State = S(Test);
    if (St.Step == 0 || St.Step == 2)
    {
        Fight(Test, {Mon(Pikachu, 6), Mon(Caterpie, 6), Mon(Pidgey, 6)}, {Mon(Pidgey, 5)});
        St.A = State->Party[0].Xp; St.B = State->Party[1].Xp; St.C = State->Party[2].Xp;
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(1.2);
    if (St.Step == 3 && St.I == 0) { Mgr->PressSwap(1); St.I = 1; if (Mgr->ActiveIndex() != 1) PYR_FAIL(TEXT("swap failed")); }
    if (Mgr->IsBattleActive()) { KnockOut(Mgr); if (Test.Seconds - St.T0 > 8) PYR_FAIL(TEXT("battle never ended")); return EGameTest::Running; }
    if (Mgr->LastOutcome != EPYRBattleOutcome::Won) PYR_FAIL(TEXT("battle ended %d"), int32(Mgr->LastOutcome));
    const int32 GotPk = State->Party[0].Xp - int32(St.A), GotCt = State->Party[1].Xp - int32(St.B), GotPg = State->Party[2].Xp - int32(St.C);
    if (St.Step == 1)
    {
        const int32 Want = XpYield(Pidgey, 5, false, 1);
        if (GotPk != Want || GotCt != 0 || GotPg != 0) PYR_FAIL(TEXT("solo: Pikachu +%d (want %d), Caterpie +%d, Pidgey +%d"), GotPk, Want, GotCt, GotPg);
        St.Note = FString::Printf(TEXT("Pikachu alone: +%d, benched +0; "), GotPk);
        St.Step = 2;
        return EGameTest::Running;
    }
    const int32 Half = XpYield(Pidgey, 5, false, 2);
    if (GotPk != Half || GotCt != Half || GotPg != 0) PYR_FAIL(TEXT("split: Pikachu +%d, Caterpie +%d (want %d each), Pidgey +%d"), GotPk, GotCt, Half, GotPg);
    PYR_PASS(TEXT("%sPikachu then Caterpie: +%d each, benched Pidgey +0"), *St.Note, Half);
}

PYR_TEST(GrowthPikachuLearnset, "Battle.Growth.PikachuLearnset", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    UPYRTrainerState* State = S(Test);
    const int32 Target = St.Step < 2 ? 6 : 8;
    const FName Want = St.Step < 2 ? TailWhip : ThunderWave;
    if (St.Step == 0 || St.Step == 2)
    {
        if (St.Step == 0) Fight(Test, {Mon(Pikachu, 5)}, {Mon(Pidgey, 5)});
        else
        {
            const FPYRMon Keep = State->Party[0];
            Fight(Test, {Mon(Pikachu, 7)}, {Mon(Pidgey, 5)});
            State->Party[0].Carried = Keep.Carried; State->Party[0].Learned = Keep.Learned;
            if (APYRMonPawn* P = Mgr->GetPlayerMon()) P->Carried = Keep.Carried;
        }
        FPYRMon& Pk = State->Party[0];
        if (Pk.Carried.Contains(Want)) PYR_FAIL(TEXT("%s known before level %d"), *Want.ToString(), Target);
        Pk.Xp = XpForLevel(Pikachu, Target) - 1;
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(1.2);
    if (Mgr->IsBattleActive())
    {
        KnockOut(Mgr);
        APYRMonPawn* P = Mgr->GetPlayerMon();
        if (P && P->Carried.Contains(Want)) St.C = 1;  // The fighter can use it at once.
        if (Test.Seconds - St.T0 > 8) PYR_FAIL(TEXT("battle never ended"));
        return EGameTest::Running;
    }
    const FPYRMon& Pk = State->Party[0];
    if (Pk.Level != Target || !Pk.Carried.Contains(Want)) PYR_FAIL(TEXT("Lv %d, carries %s: %d"), Pk.Level, *Want.ToString(), Pk.Carried.Contains(Want) ? 1 : 0);
    if (St.C < 1) PYR_FAIL(TEXT("the fighter in the ring did not get %s"), *Want.ToString());
    if (!CaptionSeen(Mgr, FString::Printf(TEXT("learned %s"), *Move(Want)->Name.ToString()))) PYR_FAIL(TEXT("no 'learned' caption"));
    St.Note += FString::Printf(TEXT("Lv %d learned %s (%d moves); "), Pk.Level, *Move(Want)->Name.ToString(), Pk.Carried.Num());
    if (St.Step == 3) PYR_PASS(TEXT("%s"), *St.Note);
    St.Step = 2; St.C = 0;
    return EGameTest::Running;
}

// m4t: the in-fight 1-4 prompt is gone (attack keys answered it). A fifth move opens the one learn prompt at once with
// the battle paused; it is answered here with the menu keys a player uses: Enter (YES), S to GROWL, Enter, W to YES on
// "Forget GROWL?", Enter, then the text. The fight then ends as usual.
namespace
{
void TapKey(UWorld* World, FKey Key)
{
    if (APlayerController* PC = World->GetFirstPlayerController())
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
    }
}

bool M4LearnSaid(const UPYRMenus* Me)
{
    const FString All = Me ? FString::Join(Me->TextLog, TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")) : FString();
    return All.Contains(TEXT("Poof!")) && All.Contains(TEXT("forgot GROWL!")) && All.Contains(TEXT("learned QUICK ATTACK!"));
}
}

PYR_TEST(GrowthFifthMoveForget, "Battle.Growth.FifthMoveForget", 60)
{
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    UPYRTrainerState* State = S(Test);
    UPYRMenus* Me = UPYRMenus::Get(Test.World);
    if (St.Step == 0)
    {
        Fight(Test, {Mon(Pikachu, 10)}, {Mon(Pidgey, 5)});
        FPYRMon& Pk = State->Party[0];
        if (Pk.Carried.Num() != 4 || Pk.Carried.Contains(QuickAttack)) PYR_FAIL(TEXT("Lv 10 Pikachu carries %d"), Pk.Carried.Num());
        Pk.Xp = XpForLevel(Pikachu, 11) - 1;
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(1.2);
    static int32 GrowlSlot = INDEX_NONE;
    if (St.Step == 1) GrowlSlot = State->Party[0].Carried.IndexOfByKey(Growl);
    PYRMoveLearning::FView V;
    const bool bPrompt = PYRMoveLearning::View(Test.World, V);
    if (St.Step == 1)
    {
        KnockOut(Mgr);
        if (!PYRMoveLearning::View(Test.World, V) || V.Stage != PYRMoveLearning::EStage::Ask) PYR_FAIL(TEXT("no learn prompt at level 11"));
        if (!Mgr->IsBattlePaused() || !Mgr->IsMenuOpen()) PYR_FAIL(TEXT("the battle is not paused under the prompt"));
        St.Note = V.Text.Replace(TEXT("\n"), TEXT(" / "));
        St.Step = 2; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 2)
    {
        PYR_WAIT(2.5);  // The fight waits for an answer.
        if (!Mgr->IsBattleActive() || !bPrompt) PYR_FAIL(TEXT("the battle ended without an answer"));
        TapKey(Test.World, EKeys::Enter);   // YES: the list
        PYRMoveLearning::View(Test.World, V);
        if (V.Stage != PYRMoveLearning::EStage::List) PYR_FAIL(TEXT("Enter did not open the list (stage %d)"), int32(V.Stage));
        for (int32 I = 0; I < GrowlSlot; ++I) TapKey(Test.World, EKeys::S);
        TapKey(Test.World, EKeys::Enter);   // GROWL
        PYRMoveLearning::View(Test.World, V);
        if (V.Stage != PYRMoveLearning::EStage::Confirm || !State->Party[0].Carried.Contains(Growl)) PYR_FAIL(TEXT("no 'Forget GROWL?' or GROWL went before the confirm"));
        TapKey(Test.World, EKeys::W);       // NO -> YES
        TapKey(Test.World, EKeys::Enter);
        St.Step = 3; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 3)
    {
        if (Me && Me->AnyOpen()) { TapKey(Test.World, EKeys::Enter); if (Test.Seconds - St.T0 > 10) PYR_FAIL(TEXT("the texts never closed")); return EGameTest::Running; }
        const FPYRMon& Pk = State->Party[0];
        if (Pk.Carried.Num() != 4 || Pk.Carried.Contains(Growl) || Pk.Carried.IndexOfByKey(QuickAttack) != GrowlSlot || Pk.Pending.Num() != 0)
            PYR_FAIL(TEXT("after forgetting Growl: %d moves, Growl %d, Quick Attack in slot %d"), Pk.Carried.Num(), Pk.Carried.Contains(Growl) ? 1 : 0, Pk.Carried.IndexOfByKey(QuickAttack));
        if (!M4LearnSaid(Me)) PYR_FAIL(TEXT("no Poof/learned text"));
        St.Step = 4; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (Mgr->IsBattleActive()) { if (Test.Seconds - St.T0 > 5) PYR_FAIL(TEXT("battle did not end after the answer")); return EGameTest::Running; }
    PYR_PASS(TEXT("level 11 with four moves opened the learn prompt over a paused fight ('%s'); Enter, S, Enter, W, Enter forgot GROWL for QUICK ATTACK in its slot"), *St.Note);
}

// Battle.Growth.CaterpieLine retired in M4: evolution waits for the evolution scene (Growth_CaterpieEvolves).

// ------------------------------------------------------------------------------------------------ whole fights (extra)
// The bot plays full fights against foes that fight back: balance checks for the slice, not in the milestone list.
namespace PYRTest
{
EGameTest BotFight(FGameTestContext& Test, FState& St, TFunctionRef<void()> Start, EPYRBotPlan Plan, const TCHAR* What)
{
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { Start(); Mgr->bFoePassive = false; Mgr->bNoRandom = false; St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    if (Mgr->IsBattleActive())
    {
        St.Bot.Step(Mgr, Plan, Test.World->GetDeltaSeconds());
        if (Mgr->GetFoeMon() && Mgr->FoeIndex > St.I) St.I = Mgr->FoeIndex;
        return EGameTest::Running;
    }
    const UPYRTrainerState* State = S(Test);
    FString Party;
    for (const FPYRMon& Mon : State->Party) Party += FString::Printf(TEXT("%s %.0f/%.0f; "), *Mon.Species.ToString(), Mon.Hp, Mon.MaxHp);
    Test.Detail = FString::Printf(TEXT("%s: outcome %d after %.0f s, foes sent %d, bot moves %d dodges %d swaps %d potions %d, party %s"),
        What, int32(Mgr->LastOutcome), Test.Seconds - St.T0, St.I + 1, St.Bot.Moves, St.Bot.Dodges, St.Bot.Swaps, St.Bot.Potions, *Party);
    return Mgr->LastOutcome == EPYRBattleOutcome::Won ? EGameTest::Passed : EGameTest::Failed;
}
}

PYR_TEST(BotBeatsWildPidgey, "Battle.Bot.WildPidgey", 150)
{
    static FState St;
    return BotFight(Test, St, [&]() { Fight(Test, {Mon(Pikachu, 6)}, {Mon(Pidgey, 3)}, EPYRBattleKind::Wild, FVector(300, 600, 0)); }, EPYRBotPlan::Aggressive,
        TEXT("Pikachu 6 vs wild Pidgey 3"));
}

PYR_TEST(BotBeatsBrock, "Battle.Bot.Brock", 400)
{
    // Yellow's Brock: Geodude 10 and Onix 12. A typical Yellow party at Pewter: Pikachu, Butterfree, Pidgey.
    static FState St;
    return BotFight(Test, St, [&]() { Fight(Test, {Mon(Pikachu, 14), Mon(Butterfree, 13), Mon(Pidgey, 12)}, {Mon(Geodude, 10), Mon(Onix, 12)}, EPYRBattleKind::Gym); },
        EPYRBotPlan::NoElectric, TEXT("Brock (Geodude 10, Onix 12)"));
}

// ------------------------------------------------------------------------------------------------ M4 names
// The M4 task names these checks; they are the M2 tests above, run under the new names too.
static struct FPYRM4Names
{
    FPYRM4Names()
    {
        GameTest::Register(TEXT("Battle_MoveFiresCooldownRing"), &PYRTest_MovesFireAtOnce, 60);
        GameTest::Register(TEXT("Battle_TellBeforeHit"), &PYRTest_TellsEveryAttack, 400);
        GameTest::Register(TEXT("Battle_ThundershockNoEffectRock"), &PYRTest_TypesNoEffectOnRock, 60);
        // m4q's names.
        GameTest::Register(TEXT("Battle_MoveFiresAtOnceWithCooldownRing"), &PYRTest_MovesFireAtOnce, 60);
        GameTest::Register(TEXT("Battle_ThundershockNoEffectOnGeodudeOnix"), &PYRTest_TypesNoEffectOnRock, 60);
        GameTest::Register(TEXT("Heal_PotionOverTime"), &PYRTest_PotionHealOverTime, 60);
        GameTest::Register(TEXT("Heal_PotionCooldown"), &PYRTest_PotionCooldown10, 60);
        GameTest::Register(TEXT("Growth_ExpToFighter"), &PYRTest_GrowthExpToFighter, 60);
        GameTest::Register(TEXT("Growth_PikachuLearns"), &PYRTest_GrowthPikachuLearnset, 60);
        GameTest::Register(TEXT("Growth_FifthMoveForget"), &PYRTest_GrowthFifthMoveForget, 60);
        GameTest::Register(TEXT("Pikachu_FaintLowers"), &PYRTest_PikachuFaintLowersBond, 60);
        GameTest::Register(TEXT("Pikachu_CloseDodge"), &PYRTest_PikachuCloseAutoDodge, 60);
    }
} GPYRM4Names;
