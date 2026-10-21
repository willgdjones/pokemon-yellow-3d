// m3 battle tests: the core battle checks again in a Mt. Moon-style cave ring (4 m ceiling, stalagmites) and the
// Cerulean-style pool (water, platforms, walkway), swimmers that dive, platforms and falls, the cave ceiling, the Yellow
// data for every Pokemon from Pewter to Cerulean, catching with Zubat and Clefairy, obedience and badges. The arenas are
// built by PYRArenas (Battle/Arena/PYRTestArenas.h), 150 m under the world; the open pad is m2's.
#include "BattleTestKit.h"
#include "PYRAttack.h"
#include "PYRBattleFx.h"
#include "PYRBattleManager.h"
#include "PYRBattleWorld.h"
#include "PYRMonPawn.h"
#include "Arena/PYRPoolRules.h"
#include "../Menus/PYRMenus.h"
#include "Arena/PYRTestArenas.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "../Encounters/PYRBattleCamera.h"
#include "../Party/PYRBadgeCase.h"
#include "../Party/PYRObedience.h"
#include "../Party/PYRSpecies.h"
#include "../Party/PYRTrainerState.h"

using namespace PYRData;
using namespace PYRTest;

namespace PYRTest
{
const TCHAR* ArenaName(EArena Arena)
{
    return Arena == EArena::Cave ? TEXT("cave") : Arena == EArena::Pool ? TEXT("pool") : TEXT("open");
}

FVector ArenaCentre(EArena Arena)
{
    switch (Arena)
    {
    case EArena::Cave: return PYRArenas::CaveCentre;
    case EArena::Pool: return PYRArenas::PoolCentre;
    default: return UPYRBattleWorld::PadCentre + FVector(-500, 0, 0);
    }
}

APYRBattleManager* FightIn(FGameTestContext& Test, EArena Arena, const TArray<FPYRMonSpec>& Party, const TArray<FPYRMonSpec>& Foes,
    EPYRBattleKind Kind, FVector FoeAt, float Radius, float Ceiling)
{
    APYRBattleManager* Mgr = M(Test);
    if (Mgr->IsBattleActive()) Mgr->RequestFlee();
    UPYRTrainerState* State = S(Test);
    State->DebugSetParty(Party);
    State->SetItemCount(UPYRTrainerState::Potion, 5);
    State->SetItemCount(UPYRTrainerState::PokeBall, 10);
    float R = 1000.f, Ceil = 0.f;
    switch (Arena)
    {
    case EArena::Cave: PYRArenas::EnsureCave(Test.World); R = 1100.f; Ceil = PYRArenas::CaveCeiling; break;
    case EArena::Pool: PYRArenas::EnsurePool(Test.World); R = 1350.f; break;
    default: UPYRBattleWorld::EnsureTestPad(Test.World); break;
    }
    if (Radius > 0.f) R = Radius;
    if (Ceiling >= 0.f) Ceil = Ceiling;
    FPYRBattleSetup Setup;
    Setup.Kind = Kind;
    Setup.Foes = Foes;
    Setup.TrainerId = Kind == EPYRBattleKind::Gym ? FName(Arena == EArena::Pool ? TEXT("misty") : TEXT("brock"))
        : Kind == EPYRBattleKind::Trainer ? FName(TEXT("rocket-1")) : NAME_None;
    Setup.Prize = Kind == EPYRBattleKind::Wild ? 0 : 100;
    Setup.Center = ArenaCentre(Arena);
    Setup.Radius = R;
    Setup.MaxHoverHeightCm = Ceil;
    Mgr->bFoePassive = true;
    Mgr->bNoRandom = true;
    Mgr->ForcedBallDodgeRoll = Mgr->ForcedCatchRoll = -1;
    Mgr->StartBattle(Setup);
    if (APYRMonPawn* F = Mgr->GetFoeMon())
        if (!FoeAt.IsZero())
        {
            const FVector At = Setup.Center + FoeAt;
            if (F->IsSwimmer() && Mgr->GetPool().IsActive())
                F->SetActorLocation(FVector(At.X, At.Y, Mgr->GetPool().SurfaceZ() + F->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * UPYRSwimmerBody::SurfaceRide), false, nullptr, ETeleportType::TeleportPhysics);
            else Place(F, At);
        }
    return Mgr;
}
}

namespace
{
// The pool's spots (relative to its centre): Pikachu's platform (P3), open water beside it, the walkway.
const FVector PoolPlatform(-50, -150, 0), PoolWater(300, 600, 0), PoolWalk(-1550, 0, 0), PoolWaterByWalk(-1100, 0, 0);

FVector AtPool(FVector Offset) { return PYRArenas::PoolCentre + Offset; }

// Puts a runner on a pool platform top (or the walkway), standing.
void Stand(APYRMonPawn* P, FVector World)
{
    if (!P) return;
    float Floor = World.Z;
    APYRBattleManager::GroundZ(P->GetWorld(), World + FVector(0, 0, 200), Floor, P);
    P->SetActorLocation(FVector(World.X, World.Y, Floor + P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
    P->GetCharacterMovement()->StopMovementImmediately();
    P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}

bool OnPlatform(const APYRMonPawn* P)
{
    const UCharacterMovementComponent* Move = P ? P->GetCharacterMovement() : nullptr;
    const AActor* Floor = Move && Move->CurrentFloor.bBlockingHit ? Move->CurrentFloor.HitResult.GetActor() : nullptr;
    return Floor && Floor->ActorHasTag(FPYRPoolRules::PlatformTag);
}

// The newest live attack by User.
APYRAttack* AttackBy(UWorld* World, const APYRMonPawn* User)
{
    APYRAttack* Found = nullptr;
    for (TActorIterator<APYRAttack> It(World); It; ++It) if (It->IsLive() && It->User.Get() == User) Found = *It;
    return Found;
}

bool AnyLiveAttack(UWorld* World)
{
    for (TActorIterator<APYRAttack> It(World); It; ++It) if (It->IsLive()) return true;
    return false;
}

// Yellow's own move list for a fresh Pokemon (WriteMonMoves), from the expected learnset.
struct FLearnRow { int32 Level; const TCHAR* Move; };
TArray<FName> YellowMoves(const TArray<FLearnRow>& Learn, int32 Level)
{
    TArray<FName> Known;
    for (const FLearnRow& L : Learn)
    {
        const FName Id(L.Move);
        if (L.Level > Level || Known.Contains(Id)) continue;
        if (Known.Num() >= 4) Known.RemoveAt(0);
        Known.Add(Id);
    }
    return Known;
}

FString Join(const TArray<FName>& Names)
{
    TArray<FString> S;
    for (const FName& N : Names) S.Add(N.ToString());
    return FString::Join(S, TEXT("+"));
}
}

// ------------------------------------------------------------------------------------------------ core checks x3 arenas
GAME_TEST(Battle_MovesFireInstantly, 180)
{
    // A move fires on the press frame, and its ring empties and refills: on the open pad, in the cave and in the pool.
    static FState St;
    PYR_READY();
    static const EArena Arenas[3] = {EArena::Open, EArena::Cave, EArena::Pool};
    if (St.I >= 3) PYR_PASS(TEXT("%s"), *St.Note);
    const EArena Arena = Arenas[St.I];
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0)
    {
        if (Arena == EArena::Open) FightIn(Test, Arena, {Mon(Pikachu, 5)}, {Mon(Pidgey, 30)}, EPYRBattleKind::Wild, FVector(0, 900, 0));
        else if (Arena == EArena::Cave) FightIn(Test, Arena, {Mon(Pikachu, 5)}, {Mon(Zubat, 30)}, EPYRBattleKind::Wild, FVector(0, 800, 0));
        else FightIn(Test, Arena, {Mon(Pikachu, 5)}, {Mon(Staryu, 30)}, EPYRBattleKind::Gym, PoolWater);
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P || !Mgr->IsBattleActive()) PYR_FAIL(TEXT("%s: no player mon after StartBattle"), ArenaName(Arena));
    if (St.Step == 1)
    {
        PYR_WAIT(1.2);
        if (Arena == EArena::Pool && (!OnPlatform(P) || P->WaterFalls > 0)) PYR_FAIL(TEXT("pool: Pikachu was not sent out onto a platform (falls %d)"), P->WaterFalls);
        const int32 FxBefore = Mgr->FxLog.FindRef(ThunderShock);
        const double Before = Test.World->GetTimeSeconds();
        Mgr->PressMove(0);
        APYRAttack* Live = nullptr;
        for (TActorIterator<APYRAttack> It(Test.World); It; ++It) if (It->User.Get() == P && It->Phase == APYRAttack::EPhase::Active) Live = *It;
        if (P->LastMove != ThunderShock || P->LastMoveTime != Before) PYR_FAIL(TEXT("%s: move not started on press (last %s)"), ArenaName(Arena), *P->LastMove.ToString());
        if (!Live) PYR_FAIL(TEXT("%s: no active attack in the same frame"), ArenaName(Arena));
        if (Mgr->FxLog.FindRef(ThunderShock) <= FxBefore) PYR_FAIL(TEXT("%s: no Thundershock effect in the same frame"), ArenaName(Arena));
        St.A = Mgr->GetCooldownFraction(0);
        if (St.A < .95f) PYR_FAIL(TEXT("%s: cooldown ring not full after use (%.2f)"), ArenaName(Arena), St.A);
        St.Step = 2; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 2)
    {
        PYR_WAIT(.5);
        St.B = Mgr->GetCooldownFraction(0);
        if (St.B <= 0.f || St.B >= St.A) PYR_FAIL(TEXT("%s: ring not emptying (%.2f after 0.5 s)"), ArenaName(Arena), St.B);
        if (P->IsMoveReady(0)) PYR_FAIL(TEXT("%s: ready again after 0.5 s"), ArenaName(Arena));
        St.Step = 3;
        return EGameTest::Running;
    }
    if (!P->IsMoveReady(0)) { if (Test.Seconds - St.T0 > 3) PYR_FAIL(TEXT("%s: never ready again"), ArenaName(Arena)); return EGameTest::Running; }
    if (Mgr->GetCooldownFraction(0) > 0.f) PYR_FAIL(TEXT("%s: ready but the ring still shows %.2f"), ArenaName(Arena), Mgr->GetCooldownFraction(0));
    St.Note += FString::Printf(TEXT("%s: Thundershock active with its bolt in the press frame, ring %.2f -> %.2f at 0.5 s -> empty when ready; "), ArenaName(Arena), St.A, St.B);
    ++St.I; St.Step = 0;
    return EGameTest::Running;
}

GAME_TEST(Battle_DodgeWindowNoDamage, 240)
{
    // A dodge inside the hit window takes no damage; the same attack undodged does. Open: Pidgey's Gust. Cave: Zubat's
    // Leech Life (a swooping dive). Pool: Starmie's Water Gun from the water at Pikachu on the walkway.
    static FState St;
    PYR_READY();
    static const EArena Arenas[3] = {EArena::Open, EArena::Cave, EArena::Pool};
    if (St.I >= 3) PYR_PASS(TEXT("%s"), *St.Note);
    const EArena Arena = Arenas[St.I];
    const FName Move = Arena == EArena::Open ? Gust : Arena == EArena::Cave ? LeechLife : WaterGun;
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0)
    {
        if (Arena == EArena::Open) FightIn(Test, Arena, {Mon(Pikachu, 8)}, {Mon(Pidgey, 5)}, EPYRBattleKind::Wild, FVector(0, 500, 0));
        else if (Arena == EArena::Cave) FightIn(Test, Arena, {Mon(Pikachu, 8)}, {Mon(Zubat, 12)}, EPYRBattleKind::Wild, FVector(0, 500, 0));
        else FightIn(Test, Arena, {Mon(Pikachu, 8)}, {Mon(Starmie, 21)}, EPYRBattleKind::Gym, PoolWaterByWalk);
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("%s: fighters missing"), ArenaName(Arena));
    if (St.Step == 1 || St.Step == 4)
    {
        PYR_WAIT(1.2);
        if (Arena == EArena::Pool) Stand(P, AtPool(PoolWalk));
        else Place(P, F->GetActorLocation() - FVector(0, 400, 0));
        Face(F, P);
        Face(P, F);
        P->LastSteer = Arena == EArena::Pool ? FVector(0, 1, 0) : FVector(1, 0, 0);
        P->Hp = P->MaxHp;
        St.A = P->Hp;
        Mgr->DebugResetCooldowns();
        F->Carried = {Move};
        Mgr->DebugForceFoeMove(Move);
        St.Step = St.Step == 1 ? 2 : 5;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    APYRAttack* A = AttackBy(Test.World, F);
    if (St.Step == 2)
    {
        if (!A) PYR_FAIL(TEXT("%s: no %s"), ArenaName(Arena), *Move.ToString());
        if (A->SecondsToHit() > .05) return EGameTest::Running;
        if (!A->Threatens(P)) PYR_FAIL(TEXT("%s: %s would not reach Pikachu"), ArenaName(Arena), *Move.ToString());
        Mgr->PressDodge();
        if (!P->BodyComp || !P->BodyComp->IsDodging()) PYR_FAIL(TEXT("%s: the dodge did not start"), ArenaName(Arena));
        St.Step = 3; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 3)
    {
        PYR_WAIT(1.2);
        if (P->Hp != St.A) PYR_FAIL(TEXT("%s: dodged %s still did %.0f damage"), ArenaName(Arena), *Move.ToString(), St.A - P->Hp);
        if (Arena == EArena::Pool && P->WaterFalls > 0) PYR_FAIL(TEXT("pool: the dodge went into the water"));
        St.Step = 4; St.T0 = Test.Seconds - 5;
        return EGameTest::Running;
    }
    if (A && A->IsLive()) { if (Test.Seconds - St.T0 > 6) PYR_FAIL(TEXT("%s: control attack never finished"), ArenaName(Arena)); return EGameTest::Running; }
    if (Test.Seconds - St.T0 < .3) return EGameTest::Running;
    if (P->Hp >= St.A) PYR_FAIL(TEXT("%s: control %s without a dodge did no damage"), ArenaName(Arena), *Move.ToString());
    St.Note += FString::Printf(TEXT("%s: dodged %s took 0, undodged took %.0f; "), ArenaName(Arena), *Move.ToString(), St.A - P->Hp);
    ++St.I; St.Step = 0;
    return EGameTest::Running;
}

GAME_TEST(Battle_EveryAttackHasTell, 480)
{
    // Every move every m3 species can use (its Yellow learnset up to the table's cap, plus Misty's Bubblebeam), used by
    // that species as a foe in its own arena (swimmers in the pool, Mt. Moon's Zubat, Clefairy and Paras in the cave),
    // shows a tell 0.4-1.2 s before its hit window. Each swimmer's first move is made from under the water: its tell is
    // the 0.6 s splash.
    static FState St;
    struct FCase { FName Species; FName Move; EArena Arena; bool bFromBelow; };
    static TArray<FCase> Cases;
    static FString Summary;
    static int32 FromBelow = 0;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (Cases.Num() == 0)
    {
        for (const FName& Id : M3Species())
        {
            const FPYRSpeciesInfo* Info = PYRSpecies::Find(Id);
            if (!Info) PYR_FAIL(TEXT("%s missing"), *Id.ToString());
            const EArena Arena = Info->Body == EPYRBody::Swimmer ? EArena::Pool : (Id == Zubat || Id == Clefairy || Id == Paras) ? EArena::Cave : EArena::Open;
            TArray<FName> Moves;
            for (const FPYRLearn& L : Info->Learnset) Moves.AddUnique(L.Move);
            if (Id == Starmie) Moves.AddUnique(BubbleBeam);
            bool bFirst = true;
            for (const FName& Mv : Moves) { Cases.Add({Id, Mv, Arena, bFirst && Arena == EArena::Pool}); bFirst = false; }
        }
    }
    if (St.I >= Cases.Num()) PYR_PASS(TEXT("%d foe moves (%d from under the water), each told 0.4-1.2 s before its hit window: %s"), Cases.Num(), FromBelow, *Summary);
    const FCase& C = Cases[St.I];
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (St.Step == 0 || !F || F->Species != C.Species || F->bFainted || !Mgr->IsBattleActive() || Mgr->IsMenuOpen())
    {
        if (Mgr->IsMenuOpen()) if (UPYRMenus* Me = UPYRMenus::Get(Test.World)) Me->CloseAll();  // m4t: a learn prompt, dropped (the fight restarts)
        FightIn(Test, C.Arena, {Mon(Pikachu, 60)}, {Mon(C.Species, 30)}, C.Arena == EArena::Pool ? EPYRBattleKind::Gym : EPYRBattleKind::Wild,
            C.Arena == EArena::Pool ? PoolWater : FVector(0, 700, 0));
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(1.2);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P) PYR_FAIL(TEXT("no player"));
    P->Hp = P->MaxHp;
    F->MaxHp = F->Hp = 100000.f;
    for (TActorIterator<APYRAttack> It(Test.World); It; ++It)
        if (It->IsLive() && It->User.Get() == F) { if (Test.Seconds - St.T0 > 10) PYR_FAIL(TEXT("%s never finished"), *It->Move.Id.ToString()); return EGameTest::Running; }
    if (C.bFromBelow && !F->bDived)
    {
        F->SurfacedUntil = 0;
        if (!F->TryDive()) { if (Test.Seconds - St.T0 > 4) PYR_FAIL(TEXT("%s could not dive (in water %d)"), *C.Species.ToString(), F->IsInWater() ? 1 : 0); return EGameTest::Running; }
    }
    F->Carried = {C.Move};
    const int32 Logged = Mgr->TellLog.Num();
    Mgr->DebugForceFoeMove(C.Move);
    if (Mgr->TellLog.Num() != Logged + 1) PYR_FAIL(TEXT("%s did not start %s"), *C.Species.ToString(), *C.Move.ToString());
    const FPYRTellRecord& R = Mgr->TellLog.Last();
    const double Lead = R.HitStart - R.TellStart;
    if (!R.bHadTell || Lead < .4 - 1e-3 || Lead > 1.2 + 1e-3) PYR_FAIL(TEXT("%s %s tell lead %.2f s (told %d)"), *C.Species.ToString(), *C.Move.ToString(), Lead, R.bHadTell ? 1 : 0);
    if (C.bFromBelow)
    {
        APYRAttack* A = AttackBy(Test.World, F);
        if (!A || !A->bFromBelow || !FMath::IsNearlyEqual(float(Lead), .6f, .01f)) PYR_FAIL(TEXT("%s %s from under the water: no 0.6 s splash tell (lead %.2f)"), *C.Species.ToString(), *C.Move.ToString(), Lead);
        ++FromBelow;
    }
    Summary += FString::Printf(TEXT("%s/%s%s %.2f; "), *C.Species.ToString(), *C.Move.ToString(), C.bFromBelow ? TEXT(" (from below)") : TEXT(""), Lead);
    ++St.I;
    St.T0 = Test.Seconds - 1.2;
    return EGameTest::Running;
}

GAME_TEST(Battle_CameraKeepsRing, 200)
{
    // Encounters' battle camera framing each arena: over 5 s of fighting (the foe's brain on, Red's Pokemon circling),
    // the whole ring (16 rim points), both Pokemon and Red stay on screen, and the yaw never changes.
    static FState St;
    static TWeakObjectPtr<APYRBattleCamera> Cam;
    static int32 Frames = 0, Off = 0;
    static float Drift = 0, Yaw0 = 0;
    PYR_READY();
    static const EArena Arenas[3] = {EArena::Open, EArena::Cave, EArena::Pool};
    if (St.I >= 3) { if (Cam.IsValid()) Cam->Destroy(); PYR_PASS(TEXT("%s"), *St.Note); }
    const EArena Arena = Arenas[St.I];
    APYRBattleManager* Mgr = M(Test);
    APlayerController* PC = Test.World->GetFirstPlayerController();
    if (St.Step == 0)
    {
        if (Arena == EArena::Open) FightIn(Test, Arena, {Mon(Pikachu, 20)}, {Mon(Pidgey, 15)}, EPYRBattleKind::Wild, FVector(0, 400, 0));
        else if (Arena == EArena::Cave) FightIn(Test, Arena, {Mon(Pikachu, 20)}, {Mon(Zubat, 15)}, EPYRBattleKind::Wild, FVector(0, 400, 0));
        else FightIn(Test, Arena, {Mon(Pidgey, 20)}, {Mon(Starmie, 21)}, EPYRBattleKind::Gym, PoolWater);
        Mgr->bFoePassive = false;
        if (!Cam.IsValid()) Cam = Test.World->SpawnActor<APYRBattleCamera>();
        if (!Cam.IsValid()) PYR_FAIL(TEXT("no battle camera"));
        if (GEngine && GEngine->GameViewport)
        {
            FVector2D Size;
            GEngine->GameViewport->GetViewportSize(Size);
            if (Size.X > 10 && Size.Y > 10) Cam->Aspect = Size.X / Size.Y;
        }
        const ACharacter* Red = Mgr->GetRed();
        const FVector Toward = Red ? (Mgr->GetCenter() - Red->GetActorLocation()).GetSafeNormal2D() : FVector(1, 0, 0);
        Cam->Frame(FVector(Mgr->GetCenter().X, Mgr->GetCenter().Y, Mgr->GetFloorZ()), Mgr->GetRadius(), Toward.Rotation().Yaw, false);
        Cam->SnapToFinal();
        if (PC) PC->SetViewTarget(Cam.Get());
        Yaw0 = Cam->GetActorRotation().Yaw;
        Frames = Off = 0; Drift = 0;
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    ACharacter* Red = Mgr->GetRed();
    if (!P || !F || !Red || !Cam.IsValid()) PYR_FAIL(TEXT("%s: fighters, Red or camera missing"), ArenaName(Arena));
    if (P->Hp < P->MaxHp * .5f) P->Hp = P->MaxHp;
    if (F->Hp < F->MaxHp * .5f) F->Hp = F->MaxHp;
    const FVector Centre(Mgr->GetCenter().X, Mgr->GetCenter().Y, Mgr->GetFloorZ());
    const float R = Mgr->GetRadius();
    // Red's Pokemon circles at 55% of the radius. These test rings have no wall, so a foe that strays past 80% of the
    // radius is set back (a real ring's wall stops it there).
    const float Ang = float(Test.Seconds - St.T0) * .7f;
    const FVector Goal = Centre + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0) * R * .55f;
    P->Steer((Goal - P->GetActorLocation()).GetSafeNormal2D());
    const FVector FromCentre = (F->GetActorLocation() - Centre) * FVector(1, 1, 0);
    if (FromCentre.Size() > R * .8f)
    {
        const FVector Back = Centre + FromCentre.GetSafeNormal() * R * .75f;
        F->SetActorLocation(FVector(Back.X, Back.Y, F->GetActorLocation().Z), true, nullptr, ETeleportType::TeleportPhysics);
    }
    TArray<FVector> Subjects = {P->GetActorLocation(), F->GetActorLocation(), Red->GetActorLocation()};
    Mgr->GetActiveTells(Subjects);
    Cam->SetSubjects(Subjects);
    if (Test.Seconds - St.T0 > 1.2)
    {
        TArray<TPair<FString, FVector>> Points;
        for (int32 k = 0; k < 16; ++k) Points.Add({TEXT("rim"), Centre + FVector(FMath::Cos(k * 2 * PI / 16), FMath::Sin(k * 2 * PI / 16), 0) * R});
        Points.Add({TEXT("player"), P->GetActorLocation()});
        Points.Add({TEXT("foe"), F->GetActorLocation()});
        Points.Add({TEXT("Red"), Red->GetActorLocation()});
        for (const TPair<FString, FVector>& Pt : Points)
        {
            FVector2D S2;
            if (!Cam->Project(Pt.Value, S2) || FMath::Abs(S2.X) > 1.f || FMath::Abs(S2.Y) > 1.f)
            {
                ++Off;
                if (Off < 4) St.Note += FString::Printf(TEXT("[%s %s off at %s] "), ArenaName(Arena), *Pt.Key, *S2.ToString());
            }
        }
        Drift = FMath::Max(Drift, float(FMath::Abs(FRotator::NormalizeAxis(Cam->GetActorRotation().Yaw - Yaw0))));
        ++Frames;
    }
    if (Test.Seconds - St.T0 < 6.2) return EGameTest::Running;
    if (Frames < 30 || Off > 0 || Drift > .01f) PYR_FAIL(TEXT("%s: %d frames, %d points off screen, yaw drift %.3f; %s"), ArenaName(Arena), Frames, Off, Drift, *St.Note);
    St.Note += FString::Printf(TEXT("%s (radius %.0f%s): %d frames, rim/fighters/Red all on screen, yaw drift %.3f; "), ArenaName(Arena), R,
        Mgr->HasCeiling() ? *FString::Printf(TEXT(", ceiling %.0f cm, camera %.0f cm above the floor"), Mgr->GetCeilingZ() - Mgr->GetFloorZ(), Cam->GetActorLocation().Z - Mgr->GetFloorZ()) : TEXT(""),
        Frames, Drift);
    ++St.I; St.Step = 0;
    return EGameTest::Running;
}

GAME_TEST(Battle_NoEffectCaption, 90)
{
    // In a Mt. Moon cave ring: Thundershock on Geodude does nothing and says so.
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { FightIn(Test, EArena::Cave, {Mon(Pikachu, 10)}, {Mon(Geodude, 10)}, EPYRBattleKind::Wild, FVector(0, 500, 0)); Mgr->CaptionLog.Reset(); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    if (St.Step == 1)
    {
        Place(P, F->GetActorLocation() + FVector(0, -500, 0));
        Face(P, F);
        St.A = F->Hp;
        Mgr->PressMove(0);
        if (P->LastMove != ThunderShock) PYR_FAIL(TEXT("slot 1 is %s"), *P->LastMove.ToString());
        St.Step = 2; St.T0 = Test.Seconds - 1.2;
        return EGameTest::Running;
    }
    if (!CaptionSeen(Mgr, TEXT("It doesn't affect Geodude"))) { if (Test.Seconds - St.T0 > 6) PYR_FAIL(TEXT("no 'It doesn't affect Geodude...' (last '%s')"), *Mgr->LastCaption().ToString()); return EGameTest::Running; }
    if (F->Hp != St.A) PYR_FAIL(TEXT("Geodude took %.0f"), St.A - F->Hp);
    PYR_PASS(TEXT("cave ring (ceiling %.0f cm): Thundershock on Geodude did 0 and showed '%s'"), Mgr->GetCeilingZ() - Mgr->GetFloorZ(), TEXT("It doesn't affect Geodude..."));
}

GAME_TEST(Battle_SuperEffectiveCaption, 90)
{
    // In the Cerulean pool: Thundershock on Misty's Starmie (Water/Psychic) is super effective.
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { FightIn(Test, EArena::Pool, {Mon(Pikachu, 10)}, {Mon(Starmie, 21)}, EPYRBattleKind::Gym, PoolWater); Mgr->CaptionLog.Reset(); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    if (St.Step == 1)
    {
        Face(P, F);
        St.A = F->Hp;
        Mgr->PressMove(0);
        St.Step = 2; St.T0 = Test.Seconds - 1.2;
        return EGameTest::Running;
    }
    if (!CaptionSeen(Mgr, TEXT("It's super effective!"))) { if (Test.Seconds - St.T0 > 6) PYR_FAIL(TEXT("no super-effective caption (last '%s')"), *Mgr->LastCaption().ToString()); return EGameTest::Running; }
    if (Mgr->LastFactor < 2.f || F->Hp >= St.A) PYR_FAIL(TEXT("factor %.1f, damage %.0f"), Mgr->LastFactor, St.A - F->Hp);
    PYR_PASS(TEXT("pool: Thundershock from a platform on Starmie in the water: x%.0f, %.0f damage, 'It's super effective!'"), Mgr->LastFactor, St.A - F->Hp);
}

// ------------------------------------------------------------------------------------------------ water and caves
GAME_TEST(Battle_SwimmerDivesOutOfReach, 120)
{
    // Misty's Starmie in the pool: dived, it can't be hit and only a ripple shows; it comes up with a 0.6 s splash tell
    // (still out of reach), strikes from below, then stays up and can be hit (x2 from Thundershock).
    static FState St;
    static double SurfaceAt = 0;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { FightIn(Test, EArena::Pool, {Mon(Pikachu, 14, {ThunderShock, QuickAttack, ThunderWave, DoubleTeam})}, {Mon(Starmie, 21)}, EPYRBattleKind::Gym, PoolWater); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    const double Now = Test.World->GetTimeSeconds();
    switch (St.Step)
    {
    case 1:
    {
        PYR_WAIT(1.2);
        if (!F->IsInWater()) PYR_FAIL(TEXT("Starmie is not swimming (z %.0f, surface %.0f)"), F->GetActorLocation().Z, Mgr->GetPool().SurfaceZ());
        if (!F->TryDive()) PYR_FAIL(TEXT("Starmie could not dive"));
        St.A = F->Hp;
        St.Step = 2; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    case 2:
    {
        PYR_WAIT(.6);  // Sink under the surface.
        if (!F->IsUntargetable()) PYR_FAIL(TEXT("a dived Starmie can be targeted"));
        if (!F->IsBodyHidden() || !F->Ripple || !F->Ripple->IsVisible()) PYR_FAIL(TEXT("dived: the body shows or no ripple"));
        if (F->TopZ() > Mgr->GetPool().SurfaceZ()) PYR_FAIL(TEXT("dived but its top is %.0f cm above the surface"), F->TopZ() - Mgr->GetPool().SurfaceZ());
        // A direct hit and a real Thundershock both pass it by.
        if (Mgr->ResolveHit(P, F, *PYRData::Move(ThunderShock), F->GetActorLocation()) > 0.f) PYR_FAIL(TEXT("ResolveHit damaged a dived Starmie"));
        Face(P, F);
        Mgr->PressMove(0);
        if (P->LastMove != ThunderShock) PYR_FAIL(TEXT("slot 1 is %s"), *P->LastMove.ToString());
        St.Step = 3; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    case 3:
    {
        if (AnyLiveAttack(Test.World)) { if (Test.Seconds - St.T0 > 3) PYR_FAIL(TEXT("Thundershock never finished")); return EGameTest::Running; }
        if (F->Hp != St.A) PYR_FAIL(TEXT("a dived Starmie took %.0f"), St.A - F->Hp);
        St.Note = FString::Printf(TEXT("dived: untargetable, body hidden, ripple shown, top %.0f cm under the surface, Thundershock passed over; "),
            Mgr->GetPool().SurfaceZ() - F->TopZ());
        // Now it strikes from below.
        Mgr->DebugResetCooldowns();
        const int32 Logged = Mgr->TellLog.Num();
        Mgr->DebugForceFoeMove(WaterGun);
        APYRAttack* A = AttackBy(Test.World, F);
        if (Mgr->TellLog.Num() != Logged + 1 || !A) PYR_FAIL(TEXT("Starmie did not start Water Gun"));
        if (!A->bFromBelow) PYR_FAIL(TEXT("a dived Starmie's Water Gun is not from below"));
        const double Lead = A->HitStart - A->TellStart;
        if (!FMath::IsNearlyEqual(float(Lead), .6f, .01f)) PYR_FAIL(TEXT("splash tell %.2f s, want 0.6"), Lead);
        SurfaceAt = A->HitStart;
        St.Note += FString::Printf(TEXT("Water Gun from below with a %.2f s splash tell; "), Lead);
        St.Step = 4; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    case 4:
        // During the splash tell it is still out of reach.
        if (Now < SurfaceAt - .05)
        {
            if (!F->IsUntargetable()) PYR_FAIL(TEXT("targetable during its splash tell"));
            APYRAttack* A = AttackBy(Test.World, F);
            if (!A || !A->IsTelling()) PYR_FAIL(TEXT("no splash tell showing"));
            return EGameTest::Running;
        }
        if (Now < SurfaceAt + .05) return EGameTest::Running;
        if (F->bDived || F->IsUntargetable()) PYR_FAIL(TEXT("did not surface with its strike"));
        if (F->IsBodyHidden()) PYR_FAIL(TEXT("surfaced but the body is hidden"));
        St.Step = 5; St.T0 = Test.Seconds;
        return EGameTest::Running;
    case 5:
    {
        // The window after it surfaces: it can't dive again for about 2 s, and a Thundershock lands.
        if (F->TryDive()) PYR_FAIL(TEXT("dived again at once after surfacing"));
        Mgr->DebugResetCooldowns();
        Face(P, F);
        St.A = F->Hp;
        Mgr->CaptionLog.Reset();
        Mgr->PressMove(0);
        St.Step = 6; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    default:
        if (F->Hp >= St.A) { if (Test.Seconds - St.T0 > 2) PYR_FAIL(TEXT("no damage in the surfaced window (last '%s')"), *Mgr->LastCaption().ToString()); return EGameTest::Running; }
        if (!CaptionSeen(Mgr, TEXT("It's super effective!"))) PYR_FAIL(TEXT("no super-effective caption"));
        if (F->SurfacedUntil - Now < .5) PYR_FAIL(TEXT("the surfaced window was already over"));
        PYR_PASS(TEXT("%ssurfaced, then Thundershock hit for %.0f (x2) inside its %.1f s window"), *St.Note, St.A - F->Hp, float(F->SurfacedUntil - SurfaceAt));
    }
}

GAME_TEST(Battle_PoolPlatforms, 150)
{
    // Pikachu can't walk on water: it falls in, takes no damage, loses 1 s and pops up on the nearest platform; a hop
    // clears the 3 m gap to the next platform; Pidgey hovers over open water.
    static FState St;
    static double FellAt = 0, UpAt = 0;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    const TArray<FVector>& Plat = PYRArenas::PlatformOffsets();
    if (St.Step == 0) { FightIn(Test, EArena::Pool, {Mon(Pikachu, 12), Mon(Pidgey, 12)}, {Mon(Staryu, 18)}, EPYRBattleKind::Gym, FVector(900, 700, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P) PYR_FAIL(TEXT("no fighter"));
    const FPYRPoolRules& Pool = Mgr->GetPool();
    if (!Pool.IsActive()) PYR_FAIL(TEXT("no pool rules (water tagged PYR.Water not found)"));
    const double Now = Test.World->GetTimeSeconds();
    switch (St.Step)
    {
    case 1:
        PYR_WAIT(1.2);
        if (Pool.Platforms.Num() != 5) PYR_FAIL(TEXT("%d platforms found"), Pool.Platforms.Num());
        // Walk off platform P1 (-X side) toward the water.
        Stand(P, AtPool(Plat[0]));
        St.A = P->Hp;
        St.I = P->WaterFalls;
        St.Step = 2; St.T0 = Test.Seconds;
        return EGameTest::Running;
    case 2:
        if (P->WaterFalls == St.I)
        {
            P->Steer(FVector(-1, 0, 0));
            if (Test.Seconds - St.T0 > 4) PYR_FAIL(TEXT("walked off the platform but never fell in (x %.0f, floor %s)"), P->GetActorLocation().X - PYRArenas::PoolCentre.X,
                P->GetCharacterMovement()->CurrentFloor.HitResult.GetActor() ? *P->GetCharacterMovement()->CurrentFloor.HitResult.GetActor()->GetName() : TEXT("none"));
            return EGameTest::Running;
        }
        if (!Pool.InFootprint(P->LastFallAt)) PYR_FAIL(TEXT("counted a fall outside the pool"));
        FellAt = Now;
        St.Step = 3;
        return EGameTest::Running;
    case 3:
        P->Steer(FVector(-1, 0, 0));  // Held input does nothing while it is in the water.
        if (P->FellInUntil > 0) { if (!P->IsStunned() || !P->IsUntargetable()) PYR_FAIL(TEXT("in the water but not held and out of reach")); return EGameTest::Running; }
        UpAt = Now;
        St.Step = 4; St.T0 = Test.Seconds;
        return EGameTest::Running;
    case 4:
    {
        PYR_WAIT(.4);
        const double Lost = UpAt - FellAt;
        if (Lost < .9 || Lost > 1.2) PYR_FAIL(TEXT("lost %.2f s in the water, want 1"), Lost);
        if (P->Hp != St.A) PYR_FAIL(TEXT("the fall did %.0f damage"), St.A - P->Hp);
        if (!OnPlatform(P)) PYR_FAIL(TEXT("did not pop up on a platform (at %s)"), *(P->GetActorLocation() - PYRArenas::PoolCentre).ToCompactString());
        St.Note = FString::Printf(TEXT("walked off P1: fell in, 0 damage, %.2f s lost, back on a platform; "), Lost);
        // Hop the 3 m gap from P1's +Y edge to P2.
        Stand(P, AtPool(Plat[0] + FVector(0, 160, 0)));
        P->LastSteer = FVector(0, 1, 0);
        St.I = P->WaterFalls;
        St.Step = 5; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    case 5:
        PYR_WAIT(.3);
        if (!P->Hop()) PYR_FAIL(TEXT("no hop"));
        St.Step = 6; St.T0 = Test.Seconds;
        return EGameTest::Running;
    case 6:
    {
        PYR_WAIT(1.6);
        if (P->WaterFalls != St.I) PYR_FAIL(TEXT("the hop fell short into the water"));
        const FVector Rel = P->GetActorLocation() - AtPool(Plat[1]);
        if (!OnPlatform(P) || FMath::Abs(Rel.X) > 220 || FMath::Abs(Rel.Y) > 220) PYR_FAIL(TEXT("the hop did not land on P2 (rel %s)"), *Rel.ToCompactString());
        St.Note += TEXT("hopped the 3 m gap from P1 to P2; ");
        // Pidgey over open water.
        Mgr->DebugResetCooldowns();
        Mgr->PressSwap(1);
        St.Step = 7; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    case 7:
    {
        if (P->Species != Pidgey) PYR_FAIL(TEXT("swap to Pidgey failed"));
        PYR_WAIT(1.0);
        const FVector Water = AtPool(PoolWater + FVector(-400, 0, 0));
        if (!Pool.IsOpenWater(Water)) PYR_FAIL(TEXT("test spot is not open water"));
        P->SetActorLocation(FVector(Water.X, Water.Y, Pool.SurfaceZ() + 150.f), false, nullptr, ETeleportType::TeleportPhysics);
        St.A = 1e9f;
        St.I = P->WaterFalls;
        St.Step = 8; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    default:
    {
        P->Steer(FVector(0, FMath::Sin(float(Test.Seconds) * 2.f) * .3f, 0));
        St.A = FMath::Min(St.A, P->BottomZ() - Pool.SurfaceZ());
        PYR_WAIT(2.5);
        if (P->WaterFalls != St.I || P->IsUntargetable()) PYR_FAIL(TEXT("Pidgey fell into the water"));
        if (!Pool.IsOpenWater(P->GetActorLocation())) PYR_FAIL(TEXT("Pidgey drifted off the water"));
        if (St.A < 20.f) PYR_FAIL(TEXT("Pidgey sank to %.0f cm over the surface"), St.A);
        PYR_PASS(TEXT("%sPidgey hovered over open water for 2.5 s (lowest %.0f cm above the surface)"), *St.Note, St.A);
    }
    }
}

GAME_TEST(Battle_CaveCeilingCapsHover, 120)
{
    // A Zubat in a Mt. Moon ring under a 400 cm ceiling never pokes into the rock (Red's and a wild one); under a 180 cm
    // ceiling the cap bites: it hovers lower than its usual 150 cm.
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    const float Ceiling = St.I == 0 ? 400.f : 180.f;
    if (St.I >= 2) PYR_PASS(TEXT("%s"), *St.Note);
    if (St.Step == 0)
    {
        FightIn(Test, EArena::Cave, {Mon(Zubat, 12)}, {Mon(Zubat, 10)}, EPYRBattleKind::Wild, FVector(0, 600, 0), 0.f, Ceiling);
        Mgr->bFoePassive = false;  // The wild Zubat flits about too.
        St.A = -1e9f; St.B = -1e9f;
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    if (!Mgr->HasCeiling() || !FMath::IsNearlyEqual(Mgr->GetCeilingZ() - Mgr->GetFloorZ(), Ceiling, 1.f)) PYR_FAIL(TEXT("ceiling %.0f, want %.0f"), Mgr->GetCeilingZ() - Mgr->GetFloorZ(), Ceiling);
    P->Hp = P->MaxHp; F->Hp = F->MaxHp;
    // Fly about: circles, dodges and the foe's swoops.
    const float T = float(Test.Seconds - St.T0);
    P->Steer(FVector(FMath::Cos(T), FMath::Sin(T), 0));
    if (FMath::Frac(T * .7f) < .02f) Mgr->PressDodge();
    if (T > 1.2f)
    {
        St.A = FMath::Max(St.A, P->TopZ() - Mgr->GetFloorZ());
        St.B = FMath::Max(St.B, F->TopZ() - Mgr->GetFloorZ());
        St.C = P->GetActorLocation().Z - Mgr->GetFloorZ();
    }
    if (T < 5.f) return EGameTest::Running;
    if (St.A > Ceiling + 1.f || St.B > Ceiling + 1.f) PYR_FAIL(TEXT("under %.0f cm: tops reached %.0f (Red's) and %.0f (wild)"), Ceiling, St.A, St.B);
    if (Ceiling < 200.f && St.C > 140.f) PYR_FAIL(TEXT("under %.0f cm the cap never bit (centre at %.0f cm)"), Ceiling, St.C);
    St.Note += FString::Printf(TEXT("ceiling %.0f cm: Zubat tops peaked at %.0f (Red's) and %.0f (wild), centre %.0f; "), Ceiling, St.A, St.B, St.C);
    ++St.I; St.Step = 0;
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ Yellow data
GAME_TEST(Battle_M3SpeciesYellowData, 60)
{
    // Every Pokemon added for Pewter to Cerulean has Yellow's types, base stats and catch rate, a body family, and a
    // fresh one at any level up to the table's cap knows exactly Yellow's moves for that level (WriteMonMoves).
    // Expected values: pret/pokeyellow data/pokemon/base_stats and evos_moves.asm.
    PYR_READY();
    struct FRow { const TCHAR* Id; EPYRType T1, T2; int32 Stats[5]; int32 Catch; TArray<FLearnRow> Learn; };
    static const TArray<FRow> Rows = {
        {TEXT("bulbasaur"), EPYRType::Grass, EPYRType::Poison, {45, 49, 49, 45, 65}, 45, {{1, TEXT("tackle")}, {1, TEXT("growl")}, {7, TEXT("leech-seed")}, {13, TEXT("vine-whip")}, {20, TEXT("poison-powder")}, {27, TEXT("razor-leaf")}, {34, TEXT("growth")}, {41, TEXT("sleep-powder")}, {48, TEXT("solarbeam")}}},
        {TEXT("charmander"), EPYRType::Fire, EPYRType::None, {39, 52, 43, 65, 50}, 45, {{1, TEXT("scratch")}, {1, TEXT("growl")}, {9, TEXT("ember")}, {15, TEXT("leer")}, {22, TEXT("rage")}, {30, TEXT("slash")}, {38, TEXT("flamethrower")}, {46, TEXT("fire-spin")}}},
        {TEXT("ekans"), EPYRType::Poison, EPYRType::None, {35, 60, 44, 55, 40}, 255, {{1, TEXT("wrap")}, {1, TEXT("leer")}, {10, TEXT("poison-sting")}, {17, TEXT("bite")}, {24, TEXT("glare")}, {31, TEXT("screech")}, {38, TEXT("acid")}}},
        {TEXT("clefairy"), EPYRType::Normal, EPYRType::None, {70, 45, 48, 35, 60}, 150, {{1, TEXT("pound")}, {1, TEXT("growl")}, {13, TEXT("sing")}, {18, TEXT("doubleslap")}, {24, TEXT("minimize")}, {31, TEXT("metronome")}, {39, TEXT("defense-curl")}, {48, TEXT("light-screen")}}},
        {TEXT("jigglypuff"), EPYRType::Normal, EPYRType::None, {115, 45, 20, 20, 25}, 170, {{1, TEXT("sing")}, {9, TEXT("pound")}, {14, TEXT("disable")}, {19, TEXT("defense-curl")}, {24, TEXT("doubleslap")}, {29, TEXT("rest")}, {34, TEXT("body-slam")}, {39, TEXT("double-edge")}}},
        {TEXT("zubat"), EPYRType::Poison, EPYRType::Flying, {40, 45, 35, 55, 40}, 255, {{1, TEXT("leech-life")}, {10, TEXT("supersonic")}, {15, TEXT("bite")}, {21, TEXT("confuse-ray")}, {28, TEXT("wing-attack")}, {36, TEXT("haze")}}},
        {TEXT("oddish"), EPYRType::Grass, EPYRType::Poison, {45, 50, 55, 30, 75}, 255, {{1, TEXT("absorb")}, {15, TEXT("poison-powder")}, {17, TEXT("stun-spore")}, {19, TEXT("sleep-powder")}, {24, TEXT("acid")}, {33, TEXT("petal-dance")}, {46, TEXT("solarbeam")}}},
        {TEXT("paras"), EPYRType::Bug, EPYRType::Grass, {35, 70, 55, 25, 55}, 190, {{1, TEXT("scratch")}, {13, TEXT("stun-spore")}, {20, TEXT("leech-life")}, {27, TEXT("spore")}, {34, TEXT("slash")}, {41, TEXT("growth")}}},
        {TEXT("parasect"), EPYRType::Bug, EPYRType::Grass, {60, 95, 80, 30, 80}, 75, {{1, TEXT("scratch")}, {1, TEXT("stun-spore")}, {1, TEXT("leech-life")}, {13, TEXT("stun-spore")}, {20, TEXT("leech-life")}, {30, TEXT("spore")}, {39, TEXT("slash")}, {48, TEXT("growth")}}},
        {TEXT("venonat"), EPYRType::Bug, EPYRType::Poison, {60, 55, 50, 45, 40}, 190, {{1, TEXT("tackle")}, {1, TEXT("disable")}, {11, TEXT("supersonic")}, {19, TEXT("confusion")}, {22, TEXT("poison-powder")}, {27, TEXT("leech-life")}, {30, TEXT("stun-spore")}, {35, TEXT("psybeam")}, {38, TEXT("sleep-powder")}, {43, TEXT("psychic-m")}}},
        {TEXT("meowth"), EPYRType::Normal, EPYRType::None, {40, 45, 35, 90, 40}, 255, {{1, TEXT("scratch")}, {1, TEXT("growl")}, {12, TEXT("bite")}, {17, TEXT("pay-day")}, {24, TEXT("screech")}, {33, TEXT("fury-swipes")}, {44, TEXT("slash")}}},
        {TEXT("machop"), EPYRType::Fighting, EPYRType::None, {70, 80, 50, 35, 35}, 180, {{1, TEXT("karate-chop")}, {20, TEXT("low-kick")}, {25, TEXT("leer")}, {32, TEXT("focus-energy")}, {39, TEXT("seismic-toss")}, {46, TEXT("submission")}}},
        {TEXT("bellsprout"), EPYRType::Grass, EPYRType::Poison, {50, 75, 35, 40, 70}, 255, {{1, TEXT("vine-whip")}, {1, TEXT("growth")}, {13, TEXT("wrap")}, {15, TEXT("poison-powder")}, {18, TEXT("sleep-powder")}, {21, TEXT("stun-spore")}, {26, TEXT("acid")}, {33, TEXT("razor-leaf")}, {42, TEXT("slam")}}},
        {TEXT("slowpoke"), EPYRType::Water, EPYRType::Psychic, {90, 65, 65, 15, 40}, 190, {{1, TEXT("confusion")}, {18, TEXT("disable")}, {22, TEXT("headbutt")}, {27, TEXT("growl")}, {33, TEXT("water-gun")}, {40, TEXT("amnesia")}, {48, TEXT("psychic-m")}}},
        {TEXT("magnemite"), EPYRType::Electric, EPYRType::None, {25, 35, 70, 45, 95}, 190, {{1, TEXT("tackle")}, {21, TEXT("sonicboom")}, {25, TEXT("thundershock")}, {29, TEXT("supersonic")}, {35, TEXT("thunder-wave")}, {41, TEXT("swift")}, {47, TEXT("screech")}}},
        {TEXT("grimer"), EPYRType::Poison, EPYRType::None, {80, 80, 50, 25, 40}, 190, {{1, TEXT("pound")}, {1, TEXT("disable")}, {30, TEXT("poison-gas")}, {33, TEXT("minimize")}, {37, TEXT("sludge")}, {42, TEXT("harden")}, {48, TEXT("screech")}, {55, TEXT("acid-armor")}}},
        {TEXT("shellder"), EPYRType::Water, EPYRType::None, {30, 65, 100, 40, 45}, 190, {{1, TEXT("tackle")}, {1, TEXT("withdraw")}, {18, TEXT("supersonic")}, {23, TEXT("clamp")}, {30, TEXT("aurora-beam")}, {39, TEXT("leer")}, {50, TEXT("ice-beam")}}},
        {TEXT("drowzee"), EPYRType::Psychic, EPYRType::None, {60, 48, 45, 42, 90}, 190, {{1, TEXT("pound")}, {1, TEXT("hypnosis")}, {12, TEXT("disable")}, {17, TEXT("confusion")}, {24, TEXT("headbutt")}, {29, TEXT("poison-gas")}, {32, TEXT("psychic-m")}, {37, TEXT("meditate")}}},
        {TEXT("voltorb"), EPYRType::Electric, EPYRType::None, {40, 30, 50, 100, 55}, 190, {{1, TEXT("tackle")}, {1, TEXT("screech")}, {17, TEXT("sonicboom")}, {22, TEXT("selfdestruct")}, {29, TEXT("light-screen")}, {36, TEXT("swift")}, {43, TEXT("explosion")}}},
        {TEXT("koffing"), EPYRType::Poison, EPYRType::None, {40, 65, 95, 35, 60}, 190, {{1, TEXT("tackle")}, {1, TEXT("smog")}, {32, TEXT("sludge")}, {37, TEXT("smokescreen")}, {40, TEXT("selfdestruct")}, {45, TEXT("haze")}, {48, TEXT("explosion")}}},
        {TEXT("horsea"), EPYRType::Water, EPYRType::None, {30, 40, 70, 60, 70}, 225, {{1, TEXT("bubble")}, {19, TEXT("smokescreen")}, {24, TEXT("leer")}, {30, TEXT("water-gun")}, {37, TEXT("agility")}, {45, TEXT("hydro-pump")}}},
        {TEXT("goldeen"), EPYRType::Water, EPYRType::None, {45, 67, 60, 63, 50}, 225, {{1, TEXT("peck")}, {1, TEXT("tail-whip")}, {19, TEXT("supersonic")}, {24, TEXT("horn-attack")}, {30, TEXT("fury-attack")}, {37, TEXT("waterfall")}, {45, TEXT("horn-drill")}, {54, TEXT("agility")}}},
        {TEXT("staryu"), EPYRType::Water, EPYRType::None, {30, 45, 55, 85, 70}, 225, {{1, TEXT("tackle")}, {17, TEXT("water-gun")}, {22, TEXT("harden")}, {27, TEXT("recover")}, {32, TEXT("swift")}, {37, TEXT("minimize")}, {42, TEXT("light-screen")}, {47, TEXT("hydro-pump")}}},
        {TEXT("starmie"), EPYRType::Water, EPYRType::Psychic, {60, 75, 85, 115, 100}, 60, {{1, TEXT("tackle")}, {1, TEXT("water-gun")}, {1, TEXT("harden")}}},
        {TEXT("magikarp"), EPYRType::Water, EPYRType::None, {20, 10, 55, 80, 20}, 255, {{1, TEXT("splash")}, {15, TEXT("tackle")}}},
    };
    const TMap<FName, EPYRBody> Bodies = {
        {Clefairy, EPYRBody::Runner}, {Jigglypuff, EPYRBody::Runner}, {Meowth, EPYRBody::Runner}, {Bellsprout, EPYRBody::Runner}, {Charmander, EPYRBody::Runner},
        {Bulbasaur, EPYRBody::Runner}, {Slowpoke, EPYRBody::Runner}, {Machop, EPYRBody::Runner}, {Drowzee, EPYRBody::Runner},
        {Zubat, EPYRBody::Flier}, {Koffing, EPYRBody::Flier}, {Magnemite, EPYRBody::Flier},
        {Paras, EPYRBody::Crawler}, {Parasect, EPYRBody::Crawler}, {Ekans, EPYRBody::Crawler}, {Oddish, EPYRBody::Crawler}, {Venonat, EPYRBody::Crawler},
        {Voltorb, EPYRBody::Crawler}, {Grimer, EPYRBody::Crawler},
        {Staryu, EPYRBody::Swimmer}, {Starmie, EPYRBody::Swimmer}, {Goldeen, EPYRBody::Swimmer}, {Horsea, EPYRBody::Swimmer}, {Shellder, EPYRBody::Swimmer},
        {Magikarp, EPYRBody::Swimmer}};
    if (M3Species().Num() != Rows.Num()) PYR_FAIL(TEXT("%d m3 species, want %d"), M3Species().Num(), Rows.Num());
    // m2's Mankey and Sandshrew (Route 3 and Mt. Moon in Yellow) stay runners.
    if (PYRSpecies::Find(Mankey)->Body != EPYRBody::Runner || PYRSpecies::Find(Sandshrew)->Body != EPYRBody::Runner) PYR_FAIL(TEXT("Mankey/Sandshrew body"));
    int32 Checked = 0;
    for (const FRow& Row : Rows)
    {
        const FName Id(Row.Id);
        const FPYRSpeciesInfo* S = PYRSpecies::Find(Id);
        if (!S) PYR_FAIL(TEXT("%s missing"), Row.Id);
        if (!M3Species().Contains(Id)) PYR_FAIL(TEXT("%s not listed in M3Species"), Row.Id);
        if (S->Type1 != Row.T1 || S->Type2 != Row.T2) PYR_FAIL(TEXT("%s types %s/%s"), Row.Id, *TypeName(S->Type1).ToString(), *TypeName(S->Type2).ToString());
        const int32 Got[5] = {S->BaseHp, S->BaseAttack, S->BaseDefence, S->BaseSpeed, S->BaseSpecial};
        for (int32 k = 0; k < 5; ++k) if (Got[k] != Row.Stats[k]) PYR_FAIL(TEXT("%s base stat %d is %d, Yellow %d"), Row.Id, k, Got[k], Row.Stats[k]);
        if (S->CatchRate != Row.Catch) PYR_FAIL(TEXT("%s catch rate %d, Yellow %d"), Row.Id, S->CatchRate, Row.Catch);
        const EPYRBody* Want = Bodies.Find(Id);
        if (!Want || S->Body != *Want) PYR_FAIL(TEXT("%s body %s"), Row.Id, *BodyName(S->Body).ToString());
        if (S->HeightM <= 0 || S->Learnset.Num() == 0 || S->BattleHeightCm() < 120.f) PYR_FAIL(TEXT("%s has no height or learnset"), Row.Id);
        for (const FPYRLearn& L : S->Learnset) if (!PYRSpecies::FindMove(L.Move)) PYR_FAIL(TEXT("%s learns unknown %s"), Row.Id, *L.Move.ToString());
        for (int32 Level = 1; Level <= LearnsetCap(Id); ++Level)
        {
            const TArray<FName> Mine = DefaultMoves(Id, Level), Yellow = YellowMoves(Row.Learn, Level);
            if (Mine != Yellow) PYR_FAIL(TEXT("%s Lv %d knows %s, Yellow %s"), Row.Id, Level, *Join(Mine), *Join(Yellow));
            ++Checked;
        }
    }
    // Misty's Starmie (Yellow special_moves.asm: Bubblebeam, TM11, in slot 4).
    if (PYRSpecies::MovesFor(Mon(Starmie, 21)) != TArray<FName>{Tackle, WaterGun, Harden, BubbleBeam}) PYR_FAIL(TEXT("Misty's Starmie moves %s"), *Join(PYRSpecies::MovesFor(Mon(Starmie, 21))));
    // A few new moves against moves.asm.
    struct FMoveWant { FName Id; EPYRType Type; float Power, Accuracy; };
    const FMoveWant Wants[] = {{WaterGun, EPYRType::Water, 40, 100}, {BubbleBeam, EPYRType::Water, 65, 100}, {Bubble, EPYRType::Water, 20, 100}, {Supersonic, EPYRType::Normal, 0, 55},
        {LeechLife, EPYRType::Bug, 20, 100}, {Wrap, EPYRType::Normal, 15, 85}, {Bind, EPYRType::Normal, 15, 75}, {Smog, EPYRType::Poison, 20, 70}, {PoisonGas, EPYRType::Poison, 0, 55},
        {Sing, EPYRType::Normal, 0, 55}, {Pound, EPYRType::Normal, 40, 100}, {Recover, EPYRType::Normal, 0, 0}, {Swift, EPYRType::Normal, 60, 0}, {Ember, EPYRType::Fire, 40, 100},
        {ConfuseRay, EPYRType::Ghost, 0, 100}, {Glare, EPYRType::Normal, 0, 75}, {Slam, EPYRType::Normal, 80, 75}};
    for (const FMoveWant& W : Wants)
    {
        const FPYRMoveInfo* Mv = PYRSpecies::FindMove(W.Id);
        if (!Mv || Mv->Type != W.Type || Mv->Power != W.Power || Mv->Accuracy != W.Accuracy) PYR_FAIL(TEXT("%s data"), *W.Id.ToString());
    }
    if (PYRSpecies::FindMove(SonicBoom)->FixedDamage != 20.f) PYR_FAIL(TEXT("Sonicboom is not a fixed 20"));
    PYR_PASS(TEXT("%d m3 species: Yellow types, base stats, catch rates and body families; %d species-levels match Yellow's move lists; Misty's Starmie has Bubblebeam; 18 move rows match moves.asm (Swift never misses, Sonicboom 20)"),
        Rows.Num(), Checked);
}

// ------------------------------------------------------------------------------------------------ catching
GAME_TEST(Catch_MeterRedToGreen, 90)
{
    // Clefairy in Mt. Moon (Yellow catch rate 150): the meter goes red -> yellow -> green as its HP drops, never back.
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { FightIn(Test, EArena::Cave, {Mon(Pikachu, 12)}, {Mon(Clefairy, 10)}, EPYRBattleKind::Wild, FVector(0, 700, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!F || !F->Info || F->Info->CatchRate != 150) PYR_FAIL(TEXT("no Clefairy with Yellow's catch rate"));
    int32 Last = -1, Seen = 0;
    float LastChance = -1;
    FString Trace;
    F->Hp = F->MaxHp;
    if (Mgr->CatchMeterBand() != 0) PYR_FAIL(TEXT("full HP is not red"));
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
    // Asleep it is easier still (Gen 1: +25/256).
    const float Awake = Mgr->CatchChance();
    F->Status = EPYRStatus::Sleep;
    if (Mgr->CatchChance() <= Awake) PYR_FAIL(TEXT("sleep did not raise the catch chance"));
    F->Status = EPYRStatus::None;
    PYR_PASS(TEXT("Clefairy (catch rate 150) in a cave ring: %s asleep at 1 HP %.2f"), *Trace, Mgr->CatchChance());
}

GAME_TEST(Catch_UnstunnedDodges, 90)
{
    // A ball thrown at a wild Zubat that isn't stunned is dodged; the same throw at a stunned one isn't.
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { FightIn(Test, EArena::Cave, {Mon(Pikachu, 12)}, {Mon(Zubat, 8)}, EPYRBattleKind::Wild, FVector(0, 700, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    if (St.Step == 1)
    {
        Mgr->ForcedBallDodgeRoll = .1f;
        Mgr->ForcedCatchRoll = .99f;
        St.I = S(Test)->ItemCount(UPYRTrainerState::PokeBall);
        Mgr->BeginAim();
        if (!Mgr->IsAiming()) PYR_FAIL(TEXT("F did not start aiming"));
        Mgr->ReleaseAim();
        if (S(Test)->ItemCount(UPYRTrainerState::PokeBall) != St.I - 1) PYR_FAIL(TEXT("ball not thrown"));
        St.Step = 2; St.T0 = Test.Seconds - 1.2;
        return EGameTest::Running;
    }
    if (Mgr->IsBallBusy()) { if (Test.Seconds - St.T0 > 6) PYR_FAIL(TEXT("ball never resolved")); return EGameTest::Running; }
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (St.Step == 2)
    {
        if (!Mgr->IsBattleActive() || !F || F->IsHidden()) PYR_FAIL(TEXT("the ball reached an unstunned Zubat"));
        if (Mgr->BallsDodged != 1 || !CaptionSeen(Mgr, TEXT("It dodged the thrown BALL!"))) PYR_FAIL(TEXT("no dodge counted"));
        if (!F->BodyComp || F->BodyComp->IsDodgeReady()) PYR_FAIL(TEXT("Zubat did not use its dodge"));
        // Stunned (a crouched approach): no dodge; the ball reaches it and wobbles.
        F->StunnedUntil = Test.World->GetTimeSeconds() + 10;
        Mgr->PressBall();
        St.Step = 3; St.T0 = Test.Seconds - 1.2;
        return EGameTest::Running;
    }
    if (Mgr->BallsDodged != 1 || Mgr->WobblesShown() < 1) PYR_FAIL(TEXT("the stunned Zubat dodged or the ball never wobbled (dodges %d, wobbles %d)"), Mgr->BallsDodged, Mgr->WobblesShown());
    PYR_PASS(TEXT("unstunned Zubat dodged the ball ('It dodged the thrown BALL!'); stunned, the ball reached it and wobbled %d time(s)"), Mgr->WobblesShown());
}

GAME_TEST(Catch_PartyOrStorage, 120)
{
    // A caught Clefairy joins the party and the Pokedex; with a full party a caught Zubat goes to Bill's PC.
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    UPYRTrainerState* State = S(Test);
    if (St.Step == 0 || St.Step == 3)
    {
        if (St.Step == 0) FightIn(Test, EArena::Cave, {Mon(Pikachu, 12)}, {Mon(Clefairy, 10)}, EPYRBattleKind::Wild, FVector(0, 700, 0));
        else FightIn(Test, EArena::Cave, {Mon(Pikachu, 12), Mon(Pidgey, 10), Mon(Rattata, 10), Mon(Spearow, 10), Mon(Mankey, 10), Mon(Sandshrew, 10)}, {Mon(Zubat, 9)},
            EPYRBattleKind::Wild, FVector(0, 700, 0));
        if (St.Step == 0) { State->Storage.Reset(); State->Caught.Reset(); State->Seen.Reset(); }
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 1 || St.Step == 4)
    {
        PYR_WAIT(1.2);
        Mgr->DebugSetFoeHp(.1f);
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
        if (State->Party.Num() != 2 || State->Party[1].Species != Clefairy) PYR_FAIL(TEXT("Clefairy not in the party"));
        if (!State->Caught.Contains(Clefairy) || !State->Seen.Contains(Clefairy)) PYR_FAIL(TEXT("Clefairy not in the Pokedex"));
        if (State->Party[1].IsTraded()) PYR_FAIL(TEXT("a caught Pokemon is not Red's own"));
        if (!CaptionSeen(Mgr, TEXT("All right! Clefairy was caught!"))) PYR_FAIL(TEXT("no catch caption"));
        St.Note = FString::Printf(TEXT("Clefairy Lv %d joined the party (OT Red) and the Pokedex; "), State->Party[1].Level);
        St.Step = 3;
        return EGameTest::Running;
    }
    if (State->Party.Num() != 6 || State->Storage.Num() != 1 || State->Storage[0].Species != Zubat) PYR_FAIL(TEXT("party %d, storage %d"), State->Party.Num(), State->Storage.Num());
    if (!State->Caught.Contains(Zubat)) PYR_FAIL(TEXT("Zubat not in the Pokedex"));
    PYR_PASS(TEXT("%swith a full party Zubat went to Bill's PC ('%s')"), *St.Note, *Mgr->Banner);
}

GAME_TEST(Catch_TrainerBallBlocked, 90)
{
    // A ball at a Team Rocket Zubat is blocked with Yellow's line.
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { FightIn(Test, EArena::Cave, {Mon(Pikachu, 14)}, {Mon(Zubat, 12), Mon(Ekans, 12)}, EPYRBattleKind::Trainer, FVector(0, 700, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    if (St.Step == 1)
    {
        Mgr->GetFoeMon()->StunnedUntil = Test.World->GetTimeSeconds() + 10;
        Mgr->ForcedCatchRoll = 0.f;
        St.I = S(Test)->ItemCount(UPYRTrainerState::PokeBall);
        Mgr->PressBall();
        St.Step = 2; St.T0 = Test.Seconds - 1.2;
        return EGameTest::Running;
    }
    if (Mgr->IsBallBusy()) { if (Test.Seconds - St.T0 > 5) PYR_FAIL(TEXT("ball never resolved")); return EGameTest::Running; }
    if (!Mgr->IsBattleActive() || S(Test)->Party.Num() != 1 || S(Test)->Caught.Contains(Zubat)) PYR_FAIL(TEXT("the Rocket's Zubat was caught"));
    if (Mgr->BallsBlocked != 1 || !CaptionSeen(Mgr, TEXT("The trainer blocked the BALL!"))) PYR_FAIL(TEXT("no block (caption '%s')"), *Mgr->LastCaption().ToString());
    PYR_PASS(TEXT("'%s' (balls %d -> %d)"), *Mgr->LastCaption().ToString(), St.I, S(Test)->ItemCount(UPYRTrainerState::PokeBall));
}

// ------------------------------------------------------------------------------------------------ obedience and badges
GAME_TEST(Obey_TradedAboveLevelIgnores, 240)
{
    // Yellow's obedience check on every press. A traded Charmander at level 20 with no Cascade Badge (obedience level
    // 10) ignores some of 200 presses: Yellow's odds are 1/3 obey, 2/9 another move, the rest naps, self-hits and
    // loafing; an ignored press runs that move's ring for 1 s only. A traded one at level 8 obeys all 200.
    static FState St;
    static TMap<int32, int32> Counts;
    static FString Lines;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    UPYRTrainerState* State = S(Test);
    const int32 Level = St.Step < 10 ? 20 : 8;
    if (St.Step == 0 || St.Step == 10)
    {
        FightIn(Test, EArena::Open, {Mon(Charmander, Level)}, {Mon(Rattata, 30)}, EPYRBattleKind::Wild, FVector(0, 900, 0));
        State->Party[0].OriginalTrainer = FName(TEXT("Trader"));
        State->Badges.Reset();
        State->SetItemCount(UPYRTrainerState::BoulderBadge, 0);
        Mgr->ObeyChecks = Mgr->ObeyIgnored = 0;
        Counts.Reset();
        St.I = 0;
        ++St.Step; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(1.2);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    if (State->ObedienceLevel() != 10) PYR_FAIL(TEXT("obedience level %d with no badge"), State->ObedienceLevel());
    const int32 GrowlSlot = P->SlotOf(Growl);
    if (GrowlSlot == INDEX_NONE) PYR_FAIL(TEXT("Charmander %d has no Growl"), Level);
    // Ten presses a frame, each from a clean state (awake, unhurt, rings ready, back at its spot).
    for (int32 k = 0; k < 10 && St.I < 200; ++k, ++St.I)
    {
        P->Hp = P->MaxHp; F->Hp = F->MaxHp = 100000.f;
        P->Status = EPYRStatus::None; P->SleepUntil = 0; P->StunnedUntil = 0; P->BusyUntil = 0; P->ConfusedUntil = 0;
        Mgr->DebugResetCooldowns();
        const int32 Before = Mgr->ObeyChecks;
        Mgr->PressMove(GrowlSlot);
        if (Mgr->ObeyChecks != Before + 1) PYR_FAIL(TEXT("press %d was not checked"), St.I);
        Counts.FindOrAdd(int32(Mgr->LastObey)) += 1;
        if (Mgr->LastObey != EPYRObey::Obeys)
        {
            // An ignored press: that ring runs 1 s only.
            if (!FMath::IsNearlyEqual(P->CooldownTotal[GrowlSlot], 1.f)) PYR_FAIL(TEXT("ignored press left a %.1f s ring"), P->CooldownTotal[GrowlSlot]);
            const FString Line = Mgr->LastCaption().ToString();
            if (Mgr->LastObey != EPYRObey::OtherMove && !Lines.Contains(Line.Replace(TEXT("Charmander "), TEXT("")))) Lines += Line + TEXT(" | ");
        }
        Place(P, F->GetActorLocation() - FVector(0, 600, 0));
    }
    if (St.I < 200) return EGameTest::Running;
    const int32 Obeyed = Counts.FindRef(int32(EPYRObey::Obeys));
    if (Level == 20)
    {
        const float Frac = Obeyed / 200.f;
        if (Mgr->ObeyIgnored == 0) PYR_FAIL(TEXT("level 20 traded never ignored a press"));
        if (Frac < .2f || Frac > .47f) PYR_FAIL(TEXT("level 20 obeyed %.0f%% of presses, Yellow 33%%"), Frac * 100.f);
        St.Note = FString::Printf(TEXT("Lv 20 traded, no badge: obeyed %d, other move %d, napped %d, hurt itself %d, did nothing %d of 200 (Yellow: 67, 44, 3, 3, 82); lines: %s; "),
            Obeyed, Counts.FindRef(int32(EPYRObey::OtherMove)), Counts.FindRef(int32(EPYRObey::Naps)), Counts.FindRef(int32(EPYRObey::HurtsItself)),
            Counts.FindRef(int32(EPYRObey::Loafs)) + Counts.FindRef(int32(EPYRObey::WontObey)) + Counts.FindRef(int32(EPYRObey::TurnsAway)) + Counts.FindRef(int32(EPYRObey::IgnoresOrders)), *Lines);
        St.Step = 10;
        return EGameTest::Running;
    }
    if (Obeyed != 200 || Mgr->ObeyIgnored != 0) PYR_FAIL(TEXT("level 8 traded ignored %d presses"), Mgr->ObeyIgnored);
    PYR_PASS(TEXT("%sLv 8 traded: obeyed 200 of 200"), *St.Note);
}

GAME_TEST(Obey_BadgeRaisesLevel, 60)
{
    // AwardBadge(Cascade) raises the obedience level from 10 to 30 (Rainbow 50, Marsh 70, Earth all), and a traded level
    // 20 then always obeys. Boulder's Attack boost (x1.125) applies in battle.
    PYR_READY();
    UPYRTrainerState* State = S(Test);
    State->Badges.Reset();
    State->SetItemCount(UPYRTrainerState::BoulderBadge, 0);
    if (State->ObedienceLevel() != 10) PYR_FAIL(TEXT("no badge: %d"), State->ObedienceLevel());
    int32 Ignored = 0;
    for (int32 i = 0; i < 1000; ++i) Ignored += PYRObedience::Roll(20, State->ObedienceLevel(), true) != EPYRObey::Obeys ? 1 : 0;
    if (Ignored == 0) PYR_FAIL(TEXT("level 20 at cap 10 never disobeyed in 1000 rolls"));
    State->AwardBadge(EPYRBadge::Cascade);
    if (!State->HasBadge(EPYRBadge::Cascade) || !State->HasBadge(FName(TEXT("Cascade")))) PYR_FAIL(TEXT("Cascade not held"));
    if (State->ObedienceLevel() != 30) PYR_FAIL(TEXT("Cascade: %d"), State->ObedienceLevel());
    for (int32 i = 0; i < 1000; ++i) if (PYRObedience::Roll(20, State->ObedienceLevel(), true) != EPYRObey::Obeys) PYR_FAIL(TEXT("level 20 disobeyed at cap 30"));
    State->AwardBadge(EPYRBadge::Rainbow);
    const int32 Rainbow = State->ObedienceLevel();
    State->AwardBadge(EPYRBadge::Marsh);
    const int32 Marsh = State->ObedienceLevel();
    State->AwardBadge(EPYRBadge::Earth);
    const int32 Earth = State->ObedienceLevel();
    if (Rainbow != 50 || Marsh != 70 || Earth < 101) PYR_FAIL(TEXT("Rainbow %d, Marsh %d, Earth %d"), Rainbow, Marsh, Earth);
    // Boulder: Red's Pokemon's Attack x1.125 (the Pewter exit reads the same badge).
    State->Badges.Reset();
    const FPYRSpeciesInfo& Pk = *PYRSpecies::Find(Pikachu);
    const float Plain = StatAt(Pk, EPYRStat::Attack, 20);
    State->AwardBadge(EPYRBadge::Boulder);
    if (!State->HasBadge(EPYRBadge::Boulder) || !State->HasItem(UPYRTrainerState::BoulderBadge)) PYR_FAIL(TEXT("Boulder not held as badge and item"));
    if (State->BadgeBoost(EPYRStat::Attack) != 1.125f || State->BadgeBoost(EPYRStat::Defence) != 1.f) PYR_FAIL(TEXT("badge boosts"));
    PYR_PASS(TEXT("obedience level: none 10 (level 20 disobeyed %d of 1000 rolls), Cascade 30 (0 of 1000), Rainbow %d, Marsh %d, Earth %d; Boulder: Attack %.0f -> %.0f"),
        Ignored, Rainbow, Marsh, Earth, Plain, FMath::FloorToFloat(Plain * 1.125f));
}

GAME_TEST(Badges_CaseShowsEarned, 60)
{
    // After Boulder and Cascade, the badge case shows both lit and the other six empty (PYRShowBadgeCase is the pause
    // menu's seam; B opens it too).
    static FState St;
    PYR_READY();
    UPYRTrainerState* State = S(Test);
    APlayerController* PC = Test.World->GetFirstPlayerController();
    if (!PC) PYR_FAIL(TEXT("no player controller"));
    if (St.Step == 0)
    {
        State->Badges.Reset();
        State->SetItemCount(UPYRTrainerState::BoulderBadge, 0);
        State->AwardBadge(EPYRBadge::Boulder);
        State->AwardBadge(EPYRBadge::Cascade);
        PYRShowBadgeCase(PC);
        St.Step = 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    PYR_WAIT(.5);
    if (!UPYRBadgeCase::IsOpen()) PYR_FAIL(TEXT("the badge case did not open"));
    const TArray<bool> Shown = UPYRBadgeCase::ShownSlots();
    const TArray<bool> Want = {true, true, false, false, false, false, false, false};
    if (Shown != Want) PYR_FAIL(TEXT("shows %s"), *UPYRBadgeCase::Describe());
    const FString Text = UPYRBadgeCase::Describe();
    UPYRBadgeCase::Close();
    if (UPYRBadgeCase::IsOpen()) PYR_FAIL(TEXT("did not close"));
    PYR_PASS(TEXT("badge case: %s"), *Text);
}

// ------------------------------------------------------------------------------------------------ extra checks
GAME_TEST(Party_SnapshotRoundTrip, 60)
{
    // The save seam for worker `core`: MakeSnapshot then ApplySnapshot on a wiped state gives everything back.
    PYR_READY();
    UPYRTrainerState* State = S(Test);
    State->DebugSetParty(TArray<FPYRMonSpec>{Mon(Pikachu, 14), Mon(Charmander, 12), Mon(Staryu, 18)});
    State->Party[1].OriginalTrainer = FName(TEXT("Trader"));
    State->Party[2].Status = EPYRStatus::Burn;
    State->Party[0].Hp = 7;
    State->Party[0].Pending = {QuickAttack};
    State->Storage = {UPYRTrainerState::MakeMon(Zubat, 9)};
    State->Money = 1234; State->SetItemCount(UPYRTrainerState::Potion, 3); State->SetItemCount(UPYRTrainerState::PokeBall, 7);
    State->AddItem(FName(TEXT("tm11")));
    State->Badges.Reset();
    State->AwardBadge(EPYRBadge::Boulder);
    State->AwardBadge(EPYRBadge::Cascade);
    State->MarkSeen(Zubat); State->MarkCaught(Clefairy);
    State->LastCentre = FName(TEXT("cerulean-centre"));
    State->BondPoints = 177;
    const FPYRTrainerSnapshot Snap = State->MakeSnapshot();
    // Wipe, then restore.
    State->Party.Reset(); State->Storage.Reset(); State->Money = 0; State->Bag.Reset();
    State->Badges.Reset(); State->Seen.Reset(); State->Caught.Reset(); State->LastCentre = NAME_None; State->BondPoints = 0;
    State->ApplySnapshot(Snap);
    if (State->Party.Num() != 3 || State->Party[1].Species != Charmander || !State->Party[1].IsTraded() || State->Party[2].Status != EPYRStatus::Burn
        || State->Party[0].Hp != 7 || State->Party[0].Pending != TArray<FName>{QuickAttack} || State->Party[0].Carried != Snap.Party[0].Carried)
        PYR_FAIL(TEXT("party not restored"));
    if (State->Storage.Num() != 1 || State->Money != 1234 || State->ItemCount(UPYRTrainerState::Potion) != 3 || State->ItemCount(UPYRTrainerState::PokeBall) != 7 || !State->HasItem(FName(TEXT("tm11")))) PYR_FAIL(TEXT("storage, money or items"));
    if (!State->HasBadge(EPYRBadge::Cascade) || State->BadgeCount() != 2 || !State->Seen.Contains(Zubat) || !State->Caught.Contains(Clefairy)) PYR_FAIL(TEXT("badges or Pokedex"));
    if (State->LastCentre != FName(TEXT("cerulean-centre")) || State->BondPoints != 177) PYR_FAIL(TEXT("centre or friendship"));
    PYR_PASS(TEXT("snapshot v%d: 3 party (OT, status, HP, moves, pending), 1 in storage, money, items, 2 badges, Pokedex, last centre and friendship round-trip"), Snap.Version);
}

GAME_TEST(Battle_StatusMovesYellow, 120)
{
    // The m3 effects through the real hit path: Sing (sleep, then "woke up!"), Supersonic (confusion: "... is
    // confused!" on the next press), Leech Seed (saps 1/16), Wrap (held, then "... attack continues!"), Disable,
    // Absorb (drain), Sonicboom (a fixed 20), Recover (half max HP) and Selfdestruct (the user faints).
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    if (St.Step == 0) { FightIn(Test, EArena::Open, {Mon(Pikachu, 20), Mon(Pidgey, 20)}, {Mon(Voltorb, 22)}, EPYRBattleKind::Wild, FVector(0, 600, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_WAIT(1.2);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
    auto Hit = [&](APYRMonPawn* User, APYRMonPawn* Victim, FName Move) { Victim->InvulnerableUntil = 0; return Mgr->ResolveHit(User, Victim, *PYRData::Move(Move), Victim->GetActorLocation()); };
    const double Now = Test.World->GetTimeSeconds();
    switch (St.Step)
    {
    case 1:
        Hit(F, P, Sing);
        if (!P->IsAsleep() || !CaptionSeen(Mgr, TEXT("Pikachu fell asleep!"))) PYR_FAIL(TEXT("Sing: not asleep"));
        Mgr->DebugResetCooldowns();
        Mgr->PressMove(0);
        if (P->LastMoveTime >= Now) PYR_FAIL(TEXT("a sleeping Pikachu used a move"));
        St.Step = 2;
        return EGameTest::Running;
    case 2:
        if (P->IsAsleep()) { if (Test.Seconds - St.T0 > 8) PYR_FAIL(TEXT("never woke")); return EGameTest::Running; }
        if (!CaptionSeen(Mgr, TEXT("Pikachu woke up!"))) PYR_FAIL(TEXT("no 'woke up!'"));
        St.Note = TEXT("Sing: asleep, no moves, woke up; ");
        Hit(F, P, Supersonic);
        if (Now >= P->ConfusedUntil || !CaptionSeen(Mgr, TEXT("Pikachu became confused!"))) PYR_FAIL(TEXT("Supersonic: not confused"));
        Mgr->DebugResetCooldowns();
        Mgr->PressMove(0);
        if (!CaptionSeen(Mgr, TEXT("Pikachu is confused!"))) PYR_FAIL(TEXT("no 'Pikachu is confused!' on a press"));
        St.Note += TEXT("Supersonic: confused, 'Pikachu is confused!'; ");
        // Leech Seed on Voltorb from Pikachu.
        St.A = F->Hp; St.B = P->Hp = P->MaxHp - 30;
        Hit(P, F, LeechSeed);
        if (!F->bSeeded) PYR_FAIL(TEXT("Leech Seed did not seed"));
        St.Step = 3; St.T0 = Test.Seconds;
        return EGameTest::Running;
    case 3:
        PYR_WAIT(2.3);
        if (F->Hp >= St.A || P->Hp <= St.B || !CaptionSeen(Mgr, TEXT("Leech Seed saps Voltorb!"))) PYR_FAIL(TEXT("Leech Seed did not sap (%.0f -> %.0f)"), St.A, F->Hp);
        St.Note += FString::Printf(TEXT("Leech Seed sapped %.0f; "), St.A - F->Hp);
        F->bSeeded = false;
        // Wrap: Pikachu held, squeezed again.
        F->Carried = {Wrap, Disable, SonicBoom, SelfDestruct};
        St.A = P->Hp = P->MaxHp;
        if (Hit(F, P, Wrap) <= 0 || Now >= P->TrappedUntil || !P->IsStunned()) PYR_FAIL(TEXT("Wrap did not hold Pikachu"));
        St.B = P->Hp;
        St.Step = 4; St.T0 = Test.Seconds;
        return EGameTest::Running;
    case 4:
        PYR_WAIT(1.8);
        if (P->Hp >= St.B || !CaptionSeen(Mgr, TEXT("Voltorb's attack continues!")) || P->TrappedUntil > 0) PYR_FAIL(TEXT("no second squeeze or still held"));
        St.Note += TEXT("Wrap: held 1.6 s and squeezed again; ");
        // Disable, Sonicboom, Absorb, Recover.
        Hit(F, P, Disable);
        if (P->DisabledSlot == INDEX_NONE || P->IsMoveReady(P->DisabledSlot) || !CaptionSeen(Mgr, TEXT("was disabled!"))) PYR_FAIL(TEXT("Disable"));
        P->DisabledUntil = 0;
        St.A = P->Hp;
        if (Hit(F, P, SonicBoom) != 20.f || St.A - P->Hp != 20.f) PYR_FAIL(TEXT("Sonicboom did %.0f"), St.A - P->Hp);
        F->Hp = F->MaxHp * .5f;
        St.A = P->Hp = P->MaxHp * .5f;
        if (Hit(P, F, Absorb) <= 0 || P->Hp <= St.A || !CaptionSeen(Mgr, TEXT("Sucked health from Voltorb!"))) PYR_FAIL(TEXT("Absorb did not drain"));
        P->Hp = 10;
        Mgr->ResolveSelf(P, *PYRData::Move(Recover));
        if (P->Hp != 10 + FMath::FloorToFloat(P->MaxHp / 2.f) || !CaptionSeen(Mgr, TEXT("Pikachu regained health!"))) PYR_FAIL(TEXT("Recover healed to %.0f"), P->Hp);
        St.Note += TEXT("Disable locked a move; Sonicboom did 20; Absorb drained; Recover healed half; ");
        // Selfdestruct: the foe blows up and faints.
        P->Hp = P->MaxHp;
        Mgr->DebugForceFoeMove(SelfDestruct);
        St.Step = 5; St.T0 = Test.Seconds;
        return EGameTest::Running;
    default:
        // m4t: a blast that knocks Pikachu out opens the forced switch with the fight paused (Voltorb's own faint at
        // the end of its blast waits too): send the next one in, as a player would.
        if (Mgr->IsWheelOpen() && Mgr->IsWheelForced()) Mgr->ConfirmWheel();
        if (!F->bFainted) { if (Test.Seconds - St.T0 > 4) PYR_FAIL(TEXT("Selfdestruct's user did not faint")); return EGameTest::Running; }
        PYR_PASS(TEXT("%sSelfdestruct: Voltorb fainted after its blast"), *St.Note);
    }
}

GAME_TEST(Battle_HopNoLedgeClimb, 90)
{
    // The hop (E) clears gaps but never climbs a one-way ledge: at the foot of the pad's 100 cm ledge Pikachu hops at
    // it and stays below.
    static FState St;
    PYR_READY();
    APYRBattleManager* Mgr = M(Test);
    const FVector Pad = UPYRBattleWorld::PadCentre;
    if (St.Step == 0) { Fight(Test, {Mon(Pikachu, 12)}, {Mon(Caterpie, 3)}, EPYRBattleKind::Wild, FVector(-500, 1200, 0)); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (!P) PYR_FAIL(TEXT("no fighter"));
    if (St.Step == 1 || St.Step == 3)
    {
        PYR_WAIT(1.2);
        // The ledge face is at X +400 from the pad centre (top at 100 cm).
        Place(P, Pad + FVector(St.Step == 1 ? 330.f : 250.f, 0, 0));
        P->LastSteer = FVector(1, 0, 0);
        P->SetActorRotation(FRotator::ZeroRotator);
        St.I = P->Hops;
        St.Step += 1; St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (Test.Seconds - St.T0 < .3) return EGameTest::Running;
    if (P->Hops == St.I) { if (!P->Hop()) PYR_FAIL(TEXT("no hop")); return EGameTest::Running; }
    P->Steer(FVector(1, 0, 0));
    if (Test.Seconds - St.T0 < 2.3) return EGameTest::Running;
    if (P->FloorZ() > 50.f || P->GetActorLocation().X - Pad.X > 400.f) PYR_FAIL(TEXT("a hop climbed the 100 cm ledge (floor %.0f, x %.0f)"), P->FloorZ(), P->GetActorLocation().X - Pad.X);
    if (St.Step == 2) { St.Note = FString::Printf(TEXT("hop from 70 cm off the face: still below (floor %.0f); "), P->FloorZ()); St.Step = 3; St.T0 = Test.Seconds; return EGameTest::Running; }
    PYR_PASS(TEXT("%shop from 150 cm off: still below (floor %.0f)"), *St.Note, P->FloorZ());
}

// ------------------------------------------------------------------------------------------------ M4 names
// The M4 task names these checks; they are the M3 tests above, run under the new names too.
static struct FPYRM4NamesM3
{
    FPYRM4NamesM3()
    {
        GameTest::Register(TEXT("Battle_DodgeInWindow"), &GameTest_Battle_DodgeWindowNoDamage, 240);
        GameTest::Register(TEXT("Catch_UnstunnedCanDodge"), &GameTest_Catch_UnstunnedDodges, 90);
        GameTest::Register(TEXT("Catch_PartyOrPcAndDex"), &GameTest_Catch_PartyOrStorage, 120);
        GameTest::Register(TEXT("Catch_TrainerBlocksBall"), &GameTest_Catch_TrainerBallBlocked, 90);
        GameTest::Register(TEXT("Badges_TradedDisobeys"), &GameTest_Obey_TradedAboveLevelIgnores, 240);
        GameTest::Register(TEXT("Badges_ObedienceRises"), &GameTest_Obey_BadgeRaisesLevel, 60);
        GameTest::Register(TEXT("Badges_CaseShows"), &GameTest_Badges_CaseShowsEarned, 60);
    }
} GPYRM4NamesM3;

// m4q's names for two of these checks.
static struct FM4QBattleNames
{
    FM4QBattleNames()
    {
        GameTest::Register(TEXT("Battle_DodgeInHitWindowTakesNoDamage"), &GameTest_Battle_DodgeWindowNoDamage, 240);
        GameTest::Register(TEXT("Battle_EveryEnemyAttackHasTell"), &GameTest_Battle_EveryAttackHasTell, 480);
    }
} GM4QBattleNames;
