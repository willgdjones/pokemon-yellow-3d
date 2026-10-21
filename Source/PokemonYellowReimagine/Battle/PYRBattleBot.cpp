#include "PYRBattleBot.h"

#include "PYRAttack.h"
#include "PYRBattleManager.h"
#include "PYRMonPawn.h"
#include "EngineUtils.h"
#include "../Party/PYRSpecies.h"
#include "../Party/PYRTrainerState.h"
#include "../Menus/PYRMoveLearning.h"

namespace
{
// Can a Pokemon carrying Moves hurt Foe with anything (a damaging move whose type affects it)?
bool CanHurt(const TArray<FName>& Moves, const FPYRSpeciesInfo* Foe, bool bElectricOk)
{
    if (!Foe) return false;
    for (const FName& Id : Moves)
        if (const FPYRMoveInfo* M = PYRData::Move(Id))
        {
            if (M->Power <= 0) continue;
            if (M->Type == EPYRType::Electric && !bElectricOk) continue;
            if (PYRData::Effectiveness(M->Type, *Foe) > 0) return true;
        }
    return false;
}
}

int32 FPYRBattleBot::PickSwap(APYRBattleManager* M, EPYRBotPlan Plan) const
{
    const UPYRTrainerState* State = UPYRTrainerState::Get(M);
    if (!State) return INDEX_NONE;
    int32 Best = INDEX_NONE;
    int32 BestScore = -100;
    const APYRMonPawn* Foe = M->FoeMon();
    for (int32 i = 0; i < State->Party.Num(); ++i)
    {
        const FPYRMon& Mon = State->Party[i];
        if (!Mon.IsAble() || i == M->ActiveIndex()) continue;
        int32 Score = 1 + FMath::RoundToInt(Mon.Hp / FMath::Max(1.f, Mon.MaxHp) * 3);
        if (Foe && CanHurt(Mon.Carried, Foe->Info, Plan != EPYRBotPlan::NoElectric)) Score += 4;
        const FPYRSpeciesInfo* Info = PYRData::Species(Mon.Species);
        if (Foe && Foe->GetBody() == EPYRBody::Giant && Info && Info->Body == EPYRBody::Crawler) Score += 2;  // Tail sweeps pass over.
        if (Score > BestScore) { BestScore = Score; Best = i; }
    }
    return Best;
}

bool FPYRBattleBot::Useless(APYRBattleManager* M, APYRMonPawn* Mine, EPYRBotPlan Plan) const
{
    const APYRMonPawn* Foe = M->FoeMon();
    return Foe && !CanHurt(Mine->Carried, Foe->Info, Plan != EPYRBotPlan::NoElectric);
}

void FPYRBattleBot::Step(APYRBattleManager* M, EPYRBotPlan Plan, float Dt)
{
    if (!M || !M->IsBattleActive()) return;
    // m4t: a fifth move: the learn prompt (the battle is paused), answered the way a player keeps the old four: NO, then
    // YES to abandon, through the menu keys.
    if (M->IsMenuOpen()) { PYRMoveLearning::StepKeep(M); return; }
    if (M->IsWheelOpen() && M->IsWheelForced())
    {
        const int32 Pick = PickSwap(M, Plan);
        if (Pick != INDEX_NONE) { M->PressSwap(Pick); ++Swaps; }
        return;
    }
    APYRMonPawn* P = M->PlayerMon();
    APYRMonPawn* F = M->FoeMon();
    if (!P || !F || P->bFainted || F->bFainted || F->IsHidden()) return;
    UWorld* World = M->GetWorld();
    const UPYRTrainerState* State = UPYRTrainerState::Get(M);

    // 1. Dodge whatever is about to land.
    bool bThreat = false;
    for (TActorIterator<APYRAttack> It(World); It; ++It)
    {
        APYRAttack* A = *It;
        if (!A->IsLive() || A->bHit || !A->User.IsValid() || A->User->bPlayerSide || !A->Threatens(P)) continue;
        const double Impact = A->TimeToImpact(P);
        bThreat = true;
        const double Window = P->GetBody() == EPYRBody::Crawler ? .3 : P->GetBody() == EPYRBody::Flier ? .15 : .12;
        if (Impact < -0.05 || Impact > Window || !P->IsDodgeReady()) continue;
        FVector Dir;
        switch (A->Move.Shape)
        {
        case EPYRMoveShape::Circle:
        case EPYRMoveShape::Dive: Dir = (P->GetActorLocation() - A->Aim).GetSafeNormal2D(); break;
        case EPYRMoveShape::Ring:
        case EPYRMoveShape::Bide:
        case EPYRMoveShape::Sweep: Dir = (P->GetActorLocation() - A->Origin).GetSafeNormal2D(); break;
        default: Dir = FVector::CrossProduct(FVector::UpVector, A->Dir.GetSafeNormal2D()); break;
        }
        if (Dir.IsNearlyZero()) Dir = P->GetActorRightVector();
        P->LastSteer = Dir;
        M->PressDodge();
        ++Dodges;
        return;
    }

    // 2. Potion when low and nothing is incoming.
    if (bUsePotions && !bThreat && State && State->ItemCount(UPYRTrainerState::Potion) > 0 && M->IsPotionReady() && P->Hp < P->MaxHp * .4f && P->HealLeft <= 0)
    {
        M->PressPotion();
        ++Potions;
        return;
    }

    // 3. Swap when this Pokemon can't hurt the foe at all.
    if (bAllowSwaps && M->IsSwapReady() && Useless(M, P, Plan))
    {
        const int32 Pick = PickSwap(M, Plan);
        if (Pick != INDEX_NONE && State && CanHurt(State->Party[Pick].Carried, F->Info, Plan != EPYRBotPlan::NoElectric)) { M->PressSwap(Pick); ++Swaps; return; }
    }

    // 4. Steer: keep a range that suits the moves; stay out of a Bide's reach while it charges, unless it has stored
    // so little that its release (double the stored damage) can't hurt much: a foe that bides again and again would
    // otherwise never be hit (Brock's Onix, hurt, keeps biding).
    const bool bAvoidBide = F->IsBiding() && F->BideStored * 2.f >= P->Hp * .5f;
    const FVector ToFoe = (F->GetActorLocation() - P->GetActorLocation()) * FVector(1, 1, 0);
    const float Dist = ToFoe.Size();
    float Want = 550.f;
    for (const FName& Id : P->Carried)
        if (const FPYRMoveInfo* Mv = PYRData::Move(Id))
            if (Mv->Power > 0 && PYRData::Effectiveness(Mv->Type, *F->Info) > 0) Want = FMath::Min(Want, Mv->Shape == EPYRMoveShape::Dash ? Mv->Range * .7f : 550.f);
    if (F->GetBody() == EPYRBody::Giant) Want = P->GetBody() == EPYRBody::Crawler ? 420.f : FMath::Max(Want, 380.f);
    if (bAvoidBide) Want = 1250.f;  // Stop attacking and get out of the shockwave.
    const FVector Side = FVector::CrossProduct(FVector::UpVector, ToFoe.GetSafeNormal());
    if (Dist > Want + 120.f) P->Steer(ToFoe.GetSafeNormal() + Side * .3f);
    else if (Dist < Want - 150.f) P->Steer(-ToFoe.GetSafeNormal() + Side * .3f);
    else P->Steer(Side * .6f);
    // Stay inside the ring.
    const FVector FromCentre = (P->GetActorLocation() - M->GetCenter()) * FVector(1, 1, 0);
    if (FromCentre.Size() > M->GetRadius() - 200.f) P->Steer(-FromCentre.GetSafeNormal());
    if (bAvoidBide) return;

    // 5. Fire the first useful move that's ready.
    MoveClock -= Dt;
    if (MoveClock > 0) return;
    const double Now = World->GetTimeSeconds();
    for (int32 i = 0; i < FMath::Min(4, P->Carried.Num()); ++i)
    {
        const FPYRMoveInfo* Move = PYRData::Move(P->Carried[i]);
        if (!Move || !P->IsMoveReady(i)) continue;
        if (Move->Type == EPYRType::Electric && Plan == EPYRBotPlan::NoElectric && F->Info && PYRData::Effectiveness(Move->Type, *F->Info) <= 0) continue;
        const float Factor = F->Info ? PYRData::Effectiveness(Move->Type, *F->Info) : 1.f;
        if ((Move->Power > 0 || Move->Effect == EPYREffect::Paralyse) && Factor <= 0) continue;
        if (Move->Effect == EPYREffect::FoeStat && F->GetStage(Move->Stat) <= -2) continue;
        if (Move->Effect == EPYREffect::SelfStat && P->GetStage(Move->Stat) >= 2) continue;
        if ((Move->Effect == EPYREffect::Paralyse || Move->Effect == EPYREffect::Poison) && Move->Power <= 0 && F->Status != EPYRStatus::None) continue;
        if (Move->Effect == EPYREffect::Bide) continue;
        const float Reach = Move->Shape == EPYRMoveShape::Dash ? Move->Range + 60.f : Move->Shape == EPYRMoveShape::Self ? 1e6f : Move->Range - 50.f;
        if (Dist > Reach + F->CapsuleRadius()) continue;
        M->PressMove(i);
        ++Moves;
        MoveClock = .15f;
        (void)Now;
        return;
    }
}
