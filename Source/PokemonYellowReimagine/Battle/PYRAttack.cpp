#include "PYRAttack.h"

#include "PYRBattleFx.h"
#include "PYRBattleManager.h"
#include "PYRMonPawn.h"
#include "Anim/PYRMonRigAnimatorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

using namespace PYRData;

namespace
{
const FLinearColor TellDark(.1f, .08f, .07f);
const FLinearColor BideOrange(1.f, .45f, .05f);
float Flat(const FVector& V) { return FVector(V.X, V.Y, 0).Size(); }

// Shortest distance from a point to a pawn's capsule axis segment.
float DistanceToCapsule(const APYRMonPawn* Pawn, FVector P)
{
    const float Half = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - Pawn->CapsuleRadius();
    const FVector C = Pawn->GetActorLocation();
    const FVector A = C - FVector(0, 0, FMath::Max(0.f, Half)), B = C + FVector(0, 0, FMath::Max(0.f, Half));
    return FMath::PointDistToSegment(P, A, B);
}

bool IsClawMove(FName Id)
{
    return Id == Scratch || Id == Peck || Id == Bite || Id == FuryAttack || Id == HyperFang || Id == KarateChop || Id == LowKick || Id == DoubleKick
        || Id == FurySwipes || Id == Slash || Id == Pound || Id == DoubleSlap;
}

const FLinearColor WaterBlue(.43f, .78f, .94f);
}

APYRAttack::APYRAttack()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

// m4t: the battle clock (it stops while the battle is paused).
double APYRAttack::Now() const { return APYRBattleManager::BattleNow(this); }

APYRMonPawn* APYRAttack::Target() const
{
    const APYRMonPawn* U = User.Get();
    return U ? U->Opponent() : nullptr;
}

UStaticMeshComponent* APYRAttack::AddMesh(const TCHAR* ShapeName, FVector Scale, FLinearColor Colour, bool bWorld)
{
    UStaticMeshComponent* Mesh = PYRFx::AddPrimitive(this, nullptr, ShapeName, FVector::ZeroVector, Scale, Colour, FRotator::ZeroRotator, true);
    if (bWorld) { Mesh->SetUsingAbsoluteLocation(true); Mesh->SetUsingAbsoluteRotation(true); Mesh->SetUsingAbsoluteScale(true); }
    Mesh->CastShadow = false;
    return Mesh;
}

APYRMoveFx* APYRAttack::Fx(uint8 Kind, FVector At, FVector FxDir, FLinearColor Colour, float Size, float Life)
{
    APYRMoveFx* F = APYRMoveFx::Spawn(GetWorld(), EPYRFx(Kind), At, FxDir, Colour, Size, Life);
    if (!F) return nullptr;
    F->MoveId = Move.Id;
    Effects.Add(F);
    if (APYRBattleManager* M = APYRBattleManager::Find(this)) M->NoteFx(Move.Id, F);
    return F;
}

void APYRAttack::Init(APYRMonPawn* InUser, const FPYRMoveInfo& InMove, bool bTell)
{
    User = InUser;
    Move = InMove;
    InUser->CurrentAttack = this;
    // A dived swimmer comes up with a 0.6 s splash tell and strikes from below (the player's swimmer too).
    bFromBelow = InUser->IsSwimmer() && InUser->bDived;
    if (bFromBelow) { bTell = true; Move.TellSeconds = .6f; }
    // Giants swing their tail wide instead of charging (Onix's Tackle); fliers turn melee into a swooping dive.
    if (Move.Shape == EPYRMoveShape::Dash && InUser->GetBody() == EPYRBody::Giant)
    {
        Move.Shape = EPYRMoveShape::Sweep;
        Move.Range = 950.f;
        Move.TellSeconds = 1.f;
        Move.HitSeconds = .45f;
        Move.HeightMin = 90.f;
        Move.HeightMax = 320.f;
    }
    else if (Move.Shape == EPYRMoveShape::Dash && InUser->GetBody() == EPYRBody::Flier)
    {
        Move.Shape = EPYRMoveShape::Dive;
        Move.Radius = 120.f;
        Move.TellSeconds = FMath::Max(Move.TellSeconds, .7f);
        Move.HitSeconds = .35f;
    }
    else if (Move.Shape == EPYRMoveShape::Dash && InUser->IsSwimmer() && !InUser->IsInWater())
        Move.Range *= .5f;  // A swimmer on dry ground only flops forward.
    Move.TellSeconds = FMath::Clamp(Move.TellSeconds, .4f, 1.2f);
    Origin = InUser->GetActorLocation();
    FloorZ = InUser->FloorZ();
    APYRMonPawn* T = Target();
    const FVector Facing = InUser->GetActorForwardVector();
    Dir = T ? (T->GetActorLocation() - Origin) * FVector(1, 1, 0) : Facing;
    Dir = Dir.IsNearlyZero() ? Facing : Dir.GetSafeNormal();
    Aim = T ? T->GetActorLocation() : Origin + Dir * 400.f;
    float AimFloor = FloorZ;
    if (T) APYRBattleManager::GroundZ(GetWorld(), T->GetActorLocation(), AimFloor, T);
    Aim.Z = AimFloor;
    if (Move.Shape != EPYRMoveShape::Circle && Move.Shape != EPYRMoveShape::Dive && Move.Shape != EPYRMoveShape::Ring && Move.Shape != EPYRMoveShape::Bide
        && Move.Shape != EPYRMoveShape::Burrow)
        InUser->SetActorRotation(FRotator(0, Dir.Rotation().Yaw, 0));

    ChargeStart = Now();
    bHasTell = bTell;
    // Bide glows orange for 3 s; its tell (a rear-up and a ground ring) fills the last TellSeconds of that. Dig spends
    // TellSeconds underground (the player's too) while its mound runs to the target.
    const double Charge = Move.Shape == EPYRMoveShape::Bide ? 3.0 : Move.Shape == EPYRMoveShape::Burrow ? double(Move.TellSeconds) : 0.0;
    const float TellFor = bTell ? Move.TellSeconds : 0.f;
    HitStart = ChargeStart + FMath::Max(Charge, double(TellFor));
    TellStart = HitStart - TellFor;
    HitEnd = HitStart + Move.HitSeconds;
    SetActorLocation(Origin);

    if (Move.Shape == EPYRMoveShape::Bide)
    {
        InUser->BideUntil = HitStart;
        InUser->BideStored = 0;
        InUser->SetGlow(true, BideOrange);
        Fx(uint8(EPYRFx::Glint), Origin, Dir, BideOrange, InUser->GetBattleScreenHeightM() * 100.f, 3.f);
    }
    if (Move.Shape == EPYRMoveShape::Burrow)
    {
        // Down it goes: a spray of earth where it was, then only the mound shows.
        InUser->BurrowUntil = HitStart;
        InUser->BusyUntil = HitEnd;
        Fx(uint8(EPYRFx::DustPuff), FVector(Origin.X, Origin.Y, FloorZ), Dir, PYRFx::Palette(TEXT("c9b089")), 160.f, .6f);
        Mound = Fx(uint8(EPYRFx::DirtMound), FVector(Origin.X, Origin.Y, FloorZ), Dir, PYRFx::Palette(TEXT("9a7a50")), 110.f, float(HitStart - ChargeStart) + .1f);
    }
    if (bFromBelow) InUser->BusyUntil = FMath::Max(InUser->BusyUntil, HitStart);
    if (Move.Shape == EPYRMoveShape::Dash || Move.Shape == EPYRMoveShape::Dive)
        InUser->BusyUntil = HitEnd;
    if (Move.Id == QuickAttack && User->bPlayerSide)
        InUser->InvulnerableUntil = FMath::Max(InUser->InvulnerableUntil, HitEnd);  // Quick Attack goes first.
    if (APYRBattleManager* M = APYRBattleManager::Find(this))
        if (!InUser->bPlayerSide) M->TellAdded(Move.Id, InUser->Species, TellStart, HitStart, bTell);
    if (!bTell && Charge <= 0) StartActive();
}

void APYRAttack::Cancel()
{
    if (APYRMonPawn* U = User.Get()) U->SetGlow(false);
    Phase = EPhase::Done;
    SetLifeSpan(.05f);
}

void APYRAttack::Tick(float Dt)
{
    Super::Tick(Dt);
    // m4t: a paused battle holds every attack where it is: no tell advancing, no flight, no hit.
    if (const APYRBattleManager* Held = APYRBattleManager::Find(this)) if (Held->IsBattlePaused()) return;
    APYRMonPawn* U = User.Get();
    if (Phase == EPhase::Done) return;
    if (!U || U->bFainted) { Finish(); return; }
    if (Phase == EPhase::Tell)
    {
        const double T = Now();
        if (bHasTell && T >= TellStart && !bTellShown)
        {
            // The tell: a crouch and glow for most moves, a ground shadow where a blow will land, a dark stripe
            // over the half-disc a tail will cross, a ring on the floor before a pulse.
            bTellShown = true;
            const float TellFor = float(HitStart - TellStart);
            const FLinearColor Glow = Move.Shape == EPYRMoveShape::Bide ? BideOrange : PYRData::TypeColour(Move.Type) * 1.3f;
            if (bFromBelow)
            {
                // The splash tell: rings spreading and bubbles boiling up where the swimmer will break the surface.
                const APYRBattleManager* M = APYRBattleManager::Find(this);
                const float Surface = M && M->GetPool().IsActive() ? M->GetPool().SurfaceZ() : FloorZ;
                const FVector Up(U->GetActorLocation().X, U->GetActorLocation().Y, Surface + 2.f);
                Tell = AddMesh(TEXT("Cylinder"), FVector(.2f, .2f, .01f), FLinearColor(.95f, .98f, 1.f, .9f));
                Tell->SetWorldLocation(Up + FVector(0, 0, 1));
                Fx(uint8(EPYRFx::Ripple), Up, Dir, WaterBlue, 220.f, TellFor + .1f);
                Fx(uint8(EPYRFx::Bubbles), Up + FVector(0, 0, 10), FVector::UpVector, WaterBlue, 90.f, TellFor + .1f);
            }
            else switch (Move.Shape)
            {
            case EPYRMoveShape::Burrow:
                Tell = AddMesh(TEXT("Cylinder"), FVector(.2f, .2f, .01f), TellDark);
                Tell->SetWorldLocation(Aim + FVector(0, 0, 2));
                break;
            case EPYRMoveShape::Dive:
            case EPYRMoveShape::Circle:
                Tell = AddMesh(TEXT("Cylinder"), FVector(.2f, .2f, .01f), TellDark);
                Tell->SetWorldLocation(Aim + FVector(0, 0, 2));
                break;
            case EPYRMoveShape::Sweep:
                for (int32 i = 0; i < 9; ++i)
                {
                    const float Angle = -80.f + i * 20.f;
                    const FVector Radial = Dir.RotateAngleAxis(Angle, FVector::UpVector);
                    const float Mid = (Move.Range + U->CapsuleRadius()) * .5f;
                    UStaticMeshComponent* Seg = AddMesh(TEXT("Cube"), FVector((Move.Range - U->CapsuleRadius()) / 100.f, Mid * .36f / 100.f, .02f), TellDark);
                    Seg->SetWorldLocationAndRotation(FVector(Origin.X, Origin.Y, FloorZ + 2) + Radial * Mid, Radial.Rotation());
                    Stripe.Add(Seg);
                }
                U->SetPose(EPYRPose::Crouch, TellFor);
                break;
            case EPYRMoveShape::Bide:
            case EPYRMoveShape::Ring:
                U->SetPose(EPYRPose::Rear, TellFor);
                U->SetGlow(true, Glow);
                Tell = AddMesh(TEXT("Cylinder"), FVector(.5f, .5f, .01f), TellDark);
                Tell->SetWorldLocation(FVector(Origin.X, Origin.Y, FloorZ + 2));
                break;
            default:
                U->SetPose(EPYRPose::Crouch, TellFor);
                U->SetGlow(true, Glow);
                break;
            }
        }
        const float A = FMath::Clamp(float((T - TellStart) / FMath::Max(.01, HitStart - TellStart)), 0.f, 1.f);
        if (Move.Shape == EPYRMoveShape::Burrow)
        {
            // The mound runs under the ground to the target, homing for most of the way, then commits.
            const float Run = FMath::Clamp(float((T - ChargeStart) / FMath::Max(.01, HitStart - ChargeStart)), 0.f, 1.f);
            if (APYRMonPawn* Tg = Target())
                if (Run < .7f && !Tg->IsUntargetable())
                {
                    float TargetFloor = Aim.Z;
                    APYRBattleManager::GroundZ(GetWorld(), Tg->GetActorLocation(), TargetFloor, Tg);
                    Aim = FVector(Tg->GetActorLocation().X, Tg->GetActorLocation().Y, TargetFloor);
                }
            if (APYRMoveFx* Mo = Mound.Get()) Mo->SetActorLocation(FMath::Lerp(FVector(Origin.X, Origin.Y, FloorZ), Aim, FMath::Min(1.f, Run * 1.15f)));
            // The burrower travels with its mound, out of sight.
            U->SetActorLocation(FVector(FMath::Lerp(Origin.X, Aim.X, Run), FMath::Lerp(Origin.Y, Aim.Y, Run), U->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
            if (Tell)
            {
                Tell->SetWorldLocation(Aim + FVector(0, 0, 2));
                const float R = Move.Radius * (.25f + .75f * A) / 50.f;
                Tell->SetWorldScale3D(FVector(R, R, .01f));
            }
        }
        if (bFromBelow && Tell)
        {
            const float R = (U->CapsuleRadius() * 2.f + 120.f * A) / 100.f;
            Tell->SetWorldScale3D(FVector(R, R, .01f));
            if (T >= NextTellFx)
            {
                NextTellFx = T + .15;
                Fx(uint8(EPYRFx::Bubbles), Tell->GetComponentLocation() + FVector(0, 0, 8), FVector::UpVector, WaterBlue, 70.f, .4f);
            }
        }
        else if (Tell && (Move.Shape == EPYRMoveShape::Dive || Move.Shape == EPYRMoveShape::Circle))
        {
            const float R = Move.Radius * (.25f + .75f * A) / 50.f;
            Tell->SetWorldScale3D(FVector(R, R, .01f));
        }
        if (Tell && (Move.Shape == EPYRMoveShape::Ring || Move.Shape == EPYRMoveShape::Bide))
        {
            const float R = (U->CapsuleRadius() * 2.f + 150.f * A) / 100.f;
            Tell->SetWorldScale3D(FVector(R, R, .01f));
        }
        for (UStaticMeshComponent* Seg : Stripe) PYRFx::SetColour(Seg, FMath::Lerp(FLinearColor(.35f, .3f, .25f), TellDark, A));
        if (U->IsStunned() && !U->bPlayerSide && Move.Shape != EPYRMoveShape::Bide) { Finish(); return; }  // A stunned foe loses its attack.
        if (T >= HitStart) StartActive();
        return;
    }
    TickActive(Dt);
}

void APYRAttack::StartActive()
{
    Phase = EPhase::Active;
    APYRMonPawn* U = User.Get();
    if (!U) { Finish(); return; }
    U->SetGlow(false);
    if (bFromBelow) U->SurfaceNow(true);
    if (U->Rig) U->Rig->PlayAttack(.3f);
    APYRMonPawn* T = Target();
    const FLinearColor Col = PYRData::TypeColour(Move.Type);
    const float BodyH = U->GetBattleScreenHeightM() * 100.f;
    const FVector Chest = U->GetActorLocation();
    const FName Id = Move.Id;
    switch (Move.Shape)
    {
    case EPYRMoveShape::Projectile:
    {
        Head = Chest + Dir * (U->CapsuleRadius() + 10.f);
        if (T) Dir = (T->GetActorLocation() - Head).GetSafeNormal();  // Homes on the target's centre, fliers included.
        const float Life = Move.HitSeconds + .15f;
        if (Id == ThunderShock)
        {
            Carrier = Fx(uint8(EPYRFx::Bolt), Head, Dir, Col, 60.f, Life);
            Fx(uint8(EPYRFx::CheekSpark), Chest + U->GetActorForwardVector() * U->CapsuleRadius() + FVector(0, 0, BodyH * .15f), U->GetActorForwardVector(), Col, 40.f, .35f);
        }
        else if (Id == StringShot) Carrier = Fx(uint8(EPYRFx::Threads), Head, Dir, FLinearColor::White, 60.f, Move.HitSeconds + .4f);
        else if (Id == Confusion || Id == Hypnosis) Carrier = Fx(uint8(EPYRFx::PsychicWave), Head, Dir, Id == Hypnosis ? PYRFx::Palette(TEXT("c080f0")) : Col, 0.f, Life);
        else if (Id == WaterGun) Carrier = Fx(uint8(EPYRFx::WaterJet), Head, Dir, Col, 80.f, Life);
        else if (Id == Bubble || Id == BubbleBeam) Carrier = Fx(uint8(EPYRFx::Bubbles), Head, Dir, PYRFx::Palette(TEXT("a8dcf8")), Id == BubbleBeam ? 130.f : 90.f, Life);
        else if (Id == Acid) Carrier = Fx(uint8(EPYRFx::Bubbles), Head, Dir, PYRFx::Palette(TEXT("a8d048")), 70.f, Life);
        else if (Id == Ember) Carrier = Fx(uint8(EPYRFx::Flame), Head, Dir, Col, 60.f, Life);
        else if (Id == Absorb) Carrier = Fx(uint8(EPYRFx::Orb), Head, Dir, PYRFx::Palette(TEXT("7ad05a")), 60.f, Life);
        else if (Id == LeechSeed) Carrier = Fx(uint8(EPYRFx::Seeds), Head, Dir, Col, 60.f, Life);
        else if (Id == Supersonic) Carrier = Fx(uint8(EPYRFx::SoundWave), Head, Dir, PYRFx::Palette(TEXT("d8d0ff")), 80.f, Life);
        else if (Id == ConfuseRay) Carrier = Fx(uint8(EPYRFx::Orb), Head, Dir, PYRFx::Palette(TEXT("b070e0")), 80.f, Life);
        else if (Id == SonicBoom) Carrier = Fx(uint8(EPYRFx::Crescent), Head, Dir, PYRFx::Palette(TEXT("e8f0ff")), 80.f, Life);
        else if (Id == PayDay) Carrier = Fx(uint8(EPYRFx::Orb), Head, Dir, PYRFx::Palette(TEXT("f0c030")), 50.f, Life);
        else if (Id == Swift) Carrier = Fx(uint8(EPYRFx::Stars), Head, Dir, PYRFx::Palette(TEXT("ffd733")), 80.f, Life);
        else Carrier = Fx(uint8(EPYRFx::Needle), Head, Dir, Col, 0.f, Move.HitSeconds + .1f);
        break;
    }
    case EPYRMoveShape::Cone:
    {
        const FVector Low = Chest - FVector(0, 0, BodyH * .3f);
        if (Id == SandAttack) Fx(uint8(EPYRFx::SandSpray), Low, Dir, PYRFx::Palette(TEXT("e0c068")), Move.Range * .5f, .6f);
        else if (Id == Smog || Id == PoisonGas) Fx(uint8(EPYRFx::GasCloud), Low, Dir, PYRFx::Palette(TEXT("9a70b8")), Move.Range, .9f);
        else if (Id == Smokescreen) Fx(uint8(EPYRFx::GasCloud), Low, Dir, PYRFx::Palette(TEXT("3a3a48")), Move.Range, 1.f);
        else if (Id == VineWhip) Fx(uint8(EPYRFx::VineLash), Low, Dir, Col, Move.Range, .35f);
        else if (Id == Whirlwind)
        {
            Fx(uint8(EPYRFx::WindSwirl), Chest, Dir, FLinearColor::White, Move.Range * .45f, .55f);
            Fx(uint8(EPYRFx::Tornado), T ? FVector(T->GetActorLocation().X, T->GetActorLocation().Y, FloorZ) : Chest + Dir * Move.Range * .6f, Dir, PYRFx::Palette(TEXT("e8f4ff")), 220.f, .7f);
        }
        else Fx(uint8(EPYRFx::WindSwirl), Chest, Dir, FLinearColor::White, Move.Range * .45f, .55f);
        if (T && !T->IsUntargetable() && Flat(T->GetActorLocation() - U->GetActorLocation()) <= Move.Range + T->CapsuleRadius())
        {
            const FVector To = (T->GetActorLocation() - U->GetActorLocation()).GetSafeNormal2D();
            const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(To, Dir), -1.f, 1.f)));
            if (Angle <= Move.Radius + 10.f && T->BottomZ() - FloorZ < 300.f) TryHit(T, T->GetActorLocation());
        }
        break;
    }
    case EPYRMoveShape::Circle:
        if (Tell) Tell->SetVisibility(false);
        if (Id == PoisonPowder) Fx(uint8(EPYRFx::PowderCloud), Aim, Dir, Col, Move.Radius * 1.2f, .8f);
        else if (Id == StunSpore) Fx(uint8(EPYRFx::PowderCloud), Aim, Dir, PYRFx::Palette(TEXT("f0d840")), Move.Radius * 1.2f, .8f);
        else if (Id == SleepPowder) Fx(uint8(EPYRFx::PowderCloud), Aim, Dir, PYRFx::Palette(TEXT("7ad0b0")), Move.Radius * 1.2f, .8f);
        else if (Id == Spore) Fx(uint8(EPYRFx::PowderCloud), Aim, Dir, PYRFx::Palette(TEXT("c8a070")), Move.Radius * 1.3f, .9f);
        else { Fx(uint8(EPYRFx::RockFall), Aim, Dir, PYRFx::Palette(TEXT("8c7a5a")), Move.Radius, .35f); Fx(uint8(EPYRFx::DustPuff), Aim, Dir, PYRFx::Palette(TEXT("c9b089")), Move.Radius * 1.2f, .6f); }
        if (T && !T->IsUntargetable() && Flat(T->GetActorLocation() - Aim) <= Move.Radius + T->CapsuleRadius() * .5f) TryHit(T, T->GetActorLocation());
        break;
    case EPYRMoveShape::Burrow:
    {
        // Up it comes under the target in a burst of earth.
        if (Tell) Tell->SetVisibility(false);
        if (APYRMoveFx* Mo = Mound.Get()) Mo->Destroy();
        U->BurrowUntil = 0;
        const FVector Out(Aim.X, Aim.Y, Aim.Z + U->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f);
        U->SetActorLocation(Out, false, nullptr, ETeleportType::TeleportPhysics);
        Fx(uint8(EPYRFx::DustPuff), Aim, Dir, PYRFx::Palette(TEXT("c9b089")), Move.Radius * 1.6f, .7f);
        Fx(uint8(EPYRFx::Impact), Aim + FVector(0, 0, 40), Dir, Col, 180.f, .35f);
        if (T && !T->IsUntargetable() && Flat(T->GetActorLocation() - Aim) <= Move.Radius + T->CapsuleRadius() * .5f) TryHit(T, T->GetActorLocation());
        break;
    }
    case EPYRMoveShape::Debuff:
        // Growl's sound rings, Tail Whip's wag glint, Leer's and Glare's glare flash, Disable's purple flash.
        if (Id == Growl) Fx(uint8(EPYRFx::RingPulse), Chest, Dir, FLinearColor::White, 260.f, .5f);
        else if (Id == Disable) Fx(uint8(EPYRFx::Glint), Chest, Dir, PYRFx::Palette(TEXT("b070e0")), 260.f, .5f);
        else if (Id == Glare) { Fx(uint8(EPYRFx::Glint), Chest + FVector(0, 0, BodyH * .2f), Dir, PYRFx::Palette(TEXT("ffd733")), 320.f, .5f); Fx(uint8(EPYRFx::RingPulse), Chest, Dir, PYRFx::Palette(TEXT("e8d060")), 300.f, .4f); }
        else Fx(uint8(EPYRFx::Glint), Chest, Dir, PYRFx::Palette(TEXT("ffd733")), 260.f, .5f);
        if (T && !T->IsUntargetable() && Flat(T->GetActorLocation() - U->GetActorLocation()) <= Move.Range) TryHit(T, T->GetActorLocation());
        break;
    case EPYRMoveShape::Self:
        if (Id == Recover) Fx(uint8(EPYRFx::HealSparkle), FVector(Chest.X, Chest.Y, FloorZ), Dir, PYRFx::Palette(TEXT("7ad05a")), BodyH, .9f);
        else if (Id == Splash)
        {
            Fx(uint8(EPYRFx::Splash), FVector(Chest.X, Chest.Y, FloorZ), Dir, PYRFx::Palette(TEXT("6ec6f0")), 70.f, .6f);
            U->SetPose(EPYRPose::Flop, .6f);
        }
        else if (Id == FocusEnergy) { Fx(uint8(EPYRFx::StatUp), FVector(Chest.X, Chest.Y, FloorZ), Dir, PYRFx::Palette(TEXT("e3342f")), BodyH, .8f); Fx(uint8(EPYRFx::Glint), Chest, Dir, PYRFx::Palette(TEXT("f08030")), BodyH, .4f); }
        else if (Id == MirrorMove) Fx(uint8(EPYRFx::Glint), Chest, Dir, PYRFx::Palette(TEXT("e8f4ff")), BodyH * 1.4f, .5f);
        else
        {
            Fx(uint8(EPYRFx::StatUp), FVector(Chest.X, Chest.Y, FloorZ), Dir, PYRData::StatColour(Move.Stat), BodyH, .8f);
            Fx(uint8(EPYRFx::Glint), Chest, Dir, PYRData::StatColour(Move.Stat), BodyH, .4f);
        }
        if (Id == Harden || Id == DefenseCurl || Id == Withdraw) U->SetPose(EPYRPose::Harden, .6f);
        if (APYRBattleManager* M = APYRBattleManager::Find(this)) M->ResolveSelf(U, Move);
        break;
    case EPYRMoveShape::Ring:
        if (Tell) Tell->SetVisibility(false);
        if (Id == Sing) Fx(uint8(EPYRFx::Notes), FVector(Chest.X, Chest.Y, FloorZ), Dir, PYRFx::Palette(TEXT("f8a0c0")), Move.Range, Move.HitSeconds + .3f);
        else if (Id == SelfDestruct)
        {
            Fx(uint8(EPYRFx::Explosion), FVector(Chest.X, Chest.Y, FloorZ), Dir, Col, Move.Range, Move.HitSeconds + .3f);
            Fx(uint8(EPYRFx::Shockwave), FVector(Chest.X, Chest.Y, FloorZ), Dir, PYRFx::Palette(TEXT("f08030")), Move.Range, Move.HitSeconds + .1f);
            if (APYRBattleManager* M = APYRBattleManager::Find(this)) { M->Shake(18.f); M->HitStop(.1f); }
        }
        else Fx(uint8(EPYRFx::RingPulse), FVector(Chest.X, Chest.Y, FloorZ), Dir, Id == Screech ? PYRFx::Palette(TEXT("f0f0ff")) : Col, Move.Range, Move.HitSeconds + .1f);
        if (Id == Screech) Fx(uint8(EPYRFx::RingPulse), FVector(Chest.X, Chest.Y, FloorZ + 80.f), Dir, PYRFx::Palette(TEXT("c8a8ff")), Move.Range * .8f, Move.HitSeconds);
        break;
    case EPYRMoveShape::Bide:
        if (Tell) Tell->SetVisibility(false);
        Fx(uint8(EPYRFx::Shockwave), FVector(Chest.X, Chest.Y, FloorZ), Dir, BideOrange, Move.Range, Move.HitSeconds + .1f);
        Fx(uint8(EPYRFx::DustPuff), FVector(Chest.X, Chest.Y, FloorZ), Dir, PYRFx::Palette(TEXT("c9b089")), 300.f, .7f);
        if (APYRBattleManager* M = APYRBattleManager::Find(this)) M->Shake(14.f);
        break;
    case EPYRMoveShape::Dive:
        if (UPYRFlierBody* F = Cast<UPYRFlierBody>(U->BodyComp)) F->DiveUntil = HitEnd + .15;
        Fx(uint8(EPYRFx::SpeedLines), Chest, (Aim - Chest).GetSafeNormal(), FLinearColor::White, BodyH, .4f);
        break;
    case EPYRMoveShape::Sweep:
        Fx(uint8(EPYRFx::TailArc), FVector(Origin.X, Origin.Y, FloorZ), Dir, U->Info ? U->Info->Colour : FLinearColor::Gray, Move.Range, Move.HitSeconds + .15f);
        Body = AddMesh(TEXT("Cube"), FVector(Move.Range / 100.f, .6f, .6f), U->Info ? U->Info->Colour : FLinearColor::Gray);
        break;
    case EPYRMoveShape::Dash:
        Fx(uint8(EPYRFx::SpeedLines), Chest, Dir, FLinearColor::White, BodyH, Move.HitSeconds + .15f);
        if (bFromBelow) Fx(uint8(EPYRFx::Splash), Chest, Dir, WaterBlue, 90.f, .5f);
        break;
    default: break;
    }
}

bool APYRAttack::HeightOverlaps(const APYRMonPawn* T) const
{
    const float Lo = FloorZ + Move.HeightMin, Hi = FloorZ + Move.HeightMax;
    return T->TopZ() >= Lo && T->BottomZ() <= Hi;
}

void APYRAttack::TickActive(float Dt)
{
    APYRMonPawn* U = User.Get();
    APYRMonPawn* T = Target();
    if (T && T->IsUntargetable()) T = nullptr;  // Under the water or underground: the attack passes it by.
    const double N = Now();
    const float Alpha = FMath::Clamp(float((N - HitStart) / FMath::Max(.01f, Move.HitSeconds)), 0.f, 1.f);
    switch (Move.Shape)
    {
    case EPYRMoveShape::Projectile:
    {
        const float Speed = Move.Range / FMath::Max(.05f, Move.HitSeconds);
        const float Step = Speed * Dt;
        Head += Dir * Step;
        Travelled += Step;
        if (APYRMoveFx* C = Carrier.Get())
        {
            if (Move.Id == ThunderShock) C->SetEnds(U->GetActorLocation() + Dir * U->CapsuleRadius(), Head);
            else C->SetActorLocation(Head - Dir * (Move.Id == StringShot ? FMath::Min(Travelled, 300.f) : 0.f));
        }
        if (T && !bHit && DistanceToCapsule(T, Head) <= Move.Radius + T->CapsuleRadius()) { TryHit(T, Head); Finish(); return; }
        if (Travelled >= Move.Range) { Finish(); return; }
        return;
    }
    case EPYRMoveShape::Dash:
    {
        const float Speed = Move.Range / FMath::Max(.05f, Move.HitSeconds);
        if (!bHit && N <= HitEnd)
        {
            U->AddActorWorldOffset(Dir * Speed * Dt, true);
            if (T)
            {
                const float Reach = U->CapsuleRadius() + T->CapsuleRadius() + Move.Radius * .5f;
                const bool bClose = Flat(T->GetActorLocation() - U->GetActorLocation()) <= Reach;
                const bool bLevel = T->BottomZ() <= U->TopZ() + 40.f && T->TopZ() >= U->BottomZ() - 20.f;
                if (bClose && bLevel) TryHit(T, (T->GetActorLocation() + U->GetActorLocation()) * .5f);
            }
        }
        break;
    }
    case EPYRMoveShape::Dive:
    {
        // Swoop from the start to just above the landing point, then strike there.
        const FVector Land = Aim + FVector(0, 0, U->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f);
        U->SetActorLocation(FMath::Lerp(Origin, Land, FMath::Min(1.f, Alpha * 1.25f)), false, nullptr, ETeleportType::TeleportPhysics);
        U->SetActorRotation(FRotator(0, (Land - Origin).Rotation().Yaw, 0));
        if (Tell) Tell->SetWorldScale3D(FVector(Move.Radius / 50.f, Move.Radius / 50.f, .01f));
        if (!bHit && Alpha >= .8f && T && Flat(T->GetActorLocation() - Aim) <= Move.Radius + T->CapsuleRadius() * .5f && T->BottomZ() - Aim.Z < 260.f)
            TryHit(T, T->GetActorLocation());
        break;
    }
    case EPYRMoveShape::Sweep:
    {
        const float Angle = -90.f + 180.f * Alpha;
        const FVector Radial = Dir.RotateAngleAxis(Angle, FVector::UpVector);
        const FVector Base(Origin.X, Origin.Y, FloorZ + (Move.HeightMin + FMath::Min(Move.HeightMax, Move.HeightMin + 120.f)) * .5f);
        if (Body) Body->SetWorldLocationAndRotation(Base + Radial * Move.Range * .5f, Radial.Rotation());
        if (T && !bHit)
        {
            const FVector To = (T->GetActorLocation() - Origin) * FVector(1, 1, 0);
            const float TargetAngle = FMath::FindDeltaAngleDegrees(Dir.Rotation().Yaw, To.Rotation().Yaw);
            if (To.Size() <= Move.Range + T->CapsuleRadius() && FMath::Abs(TargetAngle) <= 90.f && TargetAngle <= Angle && HeightOverlaps(T))
                TryHit(T, Base + To.GetSafeNormal() * To.Size());
        }
        break;
    }
    case EPYRMoveShape::Ring:
    case EPYRMoveShape::Bide:
    {
        const float Ring = FMath::Max(U->CapsuleRadius(), Move.Range * Alpha);
        if (T && !bHit)
        {
            const float D = Flat(T->GetActorLocation() - Origin);
            if (FMath::Abs(D - Ring) <= Move.Radius + T->CapsuleRadius() && HeightOverlaps(T)) TryHit(T, T->GetActorLocation());
        }
        break;
    }
    default: break;
    }
    if (N >= HitEnd) Finish();
}

bool APYRAttack::TryHit(APYRMonPawn* T, FVector At)
{
    if (bHit || !T || T->bFainted || T->IsUntargetable()) return false;
    APYRBattleManager* M = APYRBattleManager::Find(this);
    APYRMonPawn* U = User.Get();
    if (!M || !U) return false;
    bHit = true;
    const float Dmg = M->ResolveHit(U, T, Move, At);
    // Contact effects where the blow lands.
    if (M->LastHitLanded && Move.Power > 0)
    {
        if (IsClawMove(Move.Id)) Fx(uint8(EPYRFx::Claw), At, Dir, PYRFx::Palette(TEXT("fff4d6")), U->GetBattleScreenHeightM() * 80.f, .3f);
        else if (Move.Shape == EPYRMoveShape::Dash) Fx(uint8(EPYRFx::DustPuff), FVector(At.X, At.Y, FloorZ), Dir, PYRFx::Palette(TEXT("d8c8a0")), 120.f, .45f);
        // Drain moves: green orbs flow from the target back into the user.
        if (Move.Effect == EPYREffect::Drain)
            Fx(uint8(EPYRFx::DrainOrbs), T->GetActorLocation(), (U->GetActorLocation() - T->GetActorLocation()).GetSafeNormal(), PYRFx::Palette(TEXT("7ad05a")),
                float(FVector::Dist(U->GetActorLocation(), T->GetActorLocation())), .6f);
        if (Move.Effect == EPYREffect::Trap)
            Fx(uint8(EPYRFx::Coil), FVector(T->GetActorLocation().X, T->GetActorLocation().Y, T->BottomZ()), Dir, U->Info ? U->Info->Colour : FLinearColor::Gray,
                FMath::Max(90.f, T->CapsuleRadius() * 2.4f), .6f);
        if (Move.Effect == EPYREffect::PayDay && U->bPlayerSide)
            Fx(uint8(EPYRFx::Coins), At, Dir, PYRFx::Palette(TEXT("f0c030")), 160.f, .9f);
    }
    return Dmg > 0.f;
}

void APYRAttack::Finish()
{
    if (Phase == EPhase::Done) return;
    Phase = EPhase::Done;
    if (APYRMoveFx* Mo = Mound.Get()) Mo->Destroy();
    if (APYRMonPawn* U = User.Get())
    {
        U->SetGlow(false);
        if (Move.Shape == EPYRMoveShape::Bide) U->BideUntil = 0;
        if (Move.Shape == EPYRMoveShape::Burrow) U->BurrowUntil = 0;  // Cut short: it pops up where it is.
        if (U->CurrentAttack.Get() == this) U->CurrentAttack = nullptr;
        // Selfdestruct: the user faints once the blast is over (Gen 1: hit or miss).
        if (Move.Effect == EPYREffect::Explode && Now() >= HitStart && !U->bFainted && !bSelfDestructed)
            if (APYRBattleManager* M = APYRBattleManager::Find(this)) { bSelfDestructed = true; M->ExplodeFaint(U); }
    }
    SetLifeSpan(.2f);
}

void APYRAttack::GetTellPoints(TArray<FVector>& Out) const
{
    if (!bHasTell || Phase == EPhase::Done) return;
    const APYRMonPawn* U = User.Get();
    if (U) Out.Add(U->GetActorLocation());
    switch (Move.Shape)
    {
    case EPYRMoveShape::Burrow:
    case EPYRMoveShape::Dive:
    case EPYRMoveShape::Circle: Out.Add(Aim); break;
    case EPYRMoveShape::Sweep:
        for (int32 A = -90; A <= 90; A += 45) Out.Add(FVector(Origin.X, Origin.Y, FloorZ) + Dir.RotateAngleAxis(A, FVector::UpVector) * Move.Range);
        break;
    case EPYRMoveShape::Ring:
    case EPYRMoveShape::Bide:
        for (int32 A = 0; A < 360; A += 90) Out.Add(FVector(Origin.X, Origin.Y, FloorZ) + Dir.RotateAngleAxis(A, FVector::UpVector) * Move.Range);
        break;
    default: break;
    }
}

double APYRAttack::SecondsToHit() const { return FMath::Max(0.0, HitStart - Now()); }

bool APYRAttack::Threatens(const APYRMonPawn* T) const
{
    const APYRMonPawn* U = User.Get();
    if (!T || !U || Phase == EPhase::Done || bHit) return false;
    const FVector P = T->GetActorLocation();
    const float R = T->CapsuleRadius();
    switch (Move.Shape)
    {
    case EPYRMoveShape::Projectile:
    {
        const FVector From = Phase == EPhase::Active ? Head : U->GetActorLocation();
        const FVector To = P - From;
        return FVector::DotProduct(Dir.GetSafeNormal2D(), To.GetSafeNormal2D()) > .8f && To.Size() <= Move.Range + R;
    }
    case EPYRMoveShape::Dash: return Flat(P - U->GetActorLocation()) <= Move.Range + R + U->CapsuleRadius();
    case EPYRMoveShape::Burrow:
    case EPYRMoveShape::Dive:
    case EPYRMoveShape::Circle: return Flat(P - Aim) <= Move.Radius + R + 40.f;
    case EPYRMoveShape::Cone: return Flat(P - U->GetActorLocation()) <= Move.Range + R;
    case EPYRMoveShape::Sweep: return Flat(P - Origin) <= Move.Range + R && FVector::DotProduct((P - Origin).GetSafeNormal2D(), Dir) > -.1f && HeightOverlaps(T);
    case EPYRMoveShape::Ring:
    case EPYRMoveShape::Bide: return Flat(P - Origin) <= Move.Range + R && HeightOverlaps(T);
    case EPYRMoveShape::Debuff: return Flat(P - U->GetActorLocation()) <= Move.Range;
    default: return false;
    }
}

double APYRAttack::TimeToImpact(const APYRMonPawn* T) const
{
    const APYRMonPawn* U = User.Get();
    if (!T || !U || Phase == EPhase::Done || bHit) return -1;
    const double ToWindow = HitStart - Now();
    const float Speed = Move.Range / FMath::Max(.05f, Move.HitSeconds);
    const float D = Flat(T->GetActorLocation() - (Move.Shape == EPYRMoveShape::Projectile || Move.Shape == EPYRMoveShape::Dash ? U->GetActorLocation() : Origin));
    switch (Move.Shape)
    {
    case EPYRMoveShape::Projectile:
        if (Phase == EPhase::Active) return (DistanceToCapsule(T, Head) - Move.Radius - T->CapsuleRadius()) / Speed;
        return ToWindow + FMath::Max(0.f, D - Move.Radius - T->CapsuleRadius()) / Speed;
    case EPYRMoveShape::Dash: return ToWindow + FMath::Max(0.f, D - U->CapsuleRadius() - T->CapsuleRadius()) / Speed;
    case EPYRMoveShape::Dive: return ToWindow + Move.HitSeconds * .8;
    case EPYRMoveShape::Sweep:
    {
        const float Angle = FMath::FindDeltaAngleDegrees(Dir.Rotation().Yaw, ((T->GetActorLocation() - Origin) * FVector(1, 1, 0)).Rotation().Yaw);
        return ToWindow + Move.HitSeconds * (Angle + 90.f) / 180.f;
    }
    case EPYRMoveShape::Ring:
    case EPYRMoveShape::Bide: return ToWindow + Move.HitSeconds * FMath::Clamp((D - Move.Radius - T->CapsuleRadius()) / Move.Range, 0.f, 1.f);
    default: return ToWindow;
    }
}
