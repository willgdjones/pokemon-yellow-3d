#include "PYRMonPawn.h"

#include "Anim/PYRMonRigAnimatorComponent.h"
#include "PYRAttack.h"
#include "PYRBattleFx.h"
#include "PYRBattleManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/PackageName.h"
#include "../Party/PYRTrainerState.h"

using namespace PYRData;

// ------------------------------------------------------------------------------------------------ bodies
UPYRBodyComponent::UPYRBodyComponent() { PrimaryComponentTick.bCanEverTick = false; }

void UPYRBodyComponent::Configure(APYRMonPawn* Pawn)
{
    UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
    Move->bOrientRotationToMovement = true;
    Move->RotationRate = FRotator(0, Pawn->Info ? Pawn->Info->TurnRate() : 720.f, 0);
    Move->MaxWalkSpeed = Move->MaxFlySpeed = Pawn->Info ? Pawn->Info->MoveSpeed() : 500.f;
    Move->MaxAcceleration = 5000.f;
    Move->BrakingDecelerationWalking = 4000.f;
    Move->BrakingDecelerationFlying = 4000.f;
    Move->GroundFriction = 10.f;
}

APYRMonPawn* UPYRBodyComponent::Mon() const { return Cast<APYRMonPawn>(GetOwner()); }
// m4t: the battle clock (it stops while the battle is paused).
double UPYRBodyComponent::Now() const { return APYRBattleManager::BattleNow(this); }
bool UPYRBodyComponent::IsDodgeReady() const { return Now() >= DodgeReadyAt; }
bool UPYRBodyComponent::IsDodging() const { return Now() < DodgeEndsAt; }

float UPYRBodyComponent::DodgeCooldownFraction() const
{
    return FMath::Clamp(float(DodgeReadyAt - Now()) / FMath::Max(.01f, DodgeCooldown), 0.f, 1.f);
}

void UPYRBodyComponent::TickDash(float Dt)
{
    APYRMonPawn* Pawn = Mon();
    if (!Pawn || DashLeft <= 0) return;
    const float Step = FMath::Min(DashLeft, float(DodgeVelocity.Size()) * Dt);
    DashLeft -= Step;
    FHitResult Hit;
    Pawn->AddActorWorldOffset(DodgeVelocity.GetSafeNormal() * Step, true, &Hit);
    if (Hit.bBlockingHit && Hit.Normal.Z < .5f)
    {
        // Slide along walls and trunks instead of stopping dead.
        const FVector Slide = FVector::VectorPlaneProject(DodgeVelocity.GetSafeNormal() * Step * (1.f - Hit.Time), Hit.Normal);
        Pawn->AddActorWorldOffset(FVector(Slide.X, Slide.Y, 0), true);
    }
}

static FVector FlatDodgeDir(APYRMonPawn* Pawn, FVector Dir)
{
    Dir.Z = 0;
    if (Dir.IsNearlyZero()) Dir = Pawn->GetActorRightVector();
    return Dir.GetSafeNormal();
}

void UPYRRunnerBody::Configure(APYRMonPawn* Pawn)
{
    Super::Configure(Pawn);
    DodgeCooldown = 1.2f; DodgeInvulnerable = DashSeconds;
    UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
    Move->SetMovementMode(MOVE_Walking);
    Move->MaxStepHeight = 40.f;  // Hops down ledges, can't climb them.
    Move->bCanWalkOffLedges = true;
    Move->AirControl = .6f;      // A hop (E) can be steered a little in the air.
}

bool UPYRRunnerBody::TryDodge(FVector Dir)
{
    APYRMonPawn* Pawn = Mon();
    if (!Pawn || !IsDodgeReady()) return false;
    Dir = FlatDodgeDir(Pawn, Dir);
    DodgeStart = Pawn->GetActorLocation();
    DodgeVelocity = Dir * (DashDistance / DashSeconds);
    DashLeft = DashDistance;
    DodgeEndsAt = Now() + DashSeconds;
    DodgeReadyAt = Now() + DodgeCooldown;
    Pawn->InvulnerableUntil = FMath::Max(Pawn->InvulnerableUntil, Now() + DodgeInvulnerable);
    Pawn->GetCharacterMovement()->StopMovementImmediately();
    Pawn->SetActorRotation(Dir.Rotation());
    APYRMoveFx::Spawn(GetWorld(), EPYRFx::SpeedLines, Pawn->GetActorLocation(), -Dir, FLinearColor::White, Pawn->GetBattleScreenHeightM() * 100.f, .3f);
    return true;
}

void UPYRFlierBody::Configure(APYRMonPawn* Pawn)
{
    Super::Configure(Pawn);
    DodgeCooldown = 1.8f; DodgeInvulnerable = DashSeconds;
    UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
    Move->SetMovementMode(MOVE_Flying);
    float Floor;
    if (APYRBattleManager::GroundZ(GetWorld(), Pawn->GetActorLocation(), Floor, Pawn))
    {
        float Z = Floor + Pawn->Info->HoverHeight();
        // Under a cave ceiling it starts below the rock too.
        const APYRBattleManager* M = Pawn->Manager();
        if (M && M->IsBattleActive() && M->HasCeiling() && !Pawn->bVisualOnly)
            Z = FMath::Max(Floor + Pawn->Info->CapsuleHalfHeight() + 2.f, FMath::Min(Z, M->GetCeilingZ() - Pawn->Info->CapsuleHalfHeight() - 5.f));
        Pawn->SetActorLocation(FVector(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
    }
}

bool UPYRFlierBody::TryDodge(FVector Dir)
{
    APYRMonPawn* Pawn = Mon();
    if (!Pawn || !IsDodgeReady() || IsGrounded()) return false;
    Dir = FlatDodgeDir(Pawn, Dir);
    DodgeStart = Pawn->GetActorLocation();
    DodgeVelocity = Dir * (DashDistance / DashSeconds);
    DashLeft = DashDistance;
    DodgeEndsAt = Now() + DashSeconds;
    DodgeReadyAt = Now() + DodgeCooldown;
    Pawn->InvulnerableUntil = FMath::Max(Pawn->InvulnerableUntil, Now() + DodgeInvulnerable);
    Pawn->GetCharacterMovement()->StopMovementImmediately();
    APYRMoveFx::Spawn(GetWorld(), EPYRFx::WindSwirl, Pawn->GetActorLocation(), -Dir, FLinearColor::White, 90.f, .35f);
    return true;
}

void UPYRFlierBody::Ground(float Seconds)
{
    APYRMonPawn* Pawn = Mon();
    if (!Pawn) return;
    GroundedUntil = Now() + Seconds;
    Pawn->StunnedUntil = FMath::Max(Pawn->StunnedUntil, GroundedUntil);
    DashLeft = 0;
    // Knocked straight down onto the floor.
    float Floor;
    if (APYRBattleManager::GroundZ(GetWorld(), Pawn->GetActorLocation(), Floor, Pawn))
    {
        const float Drop = Pawn->GetActorLocation().Z - (Floor + Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f);
        if (Drop > 0) Pawn->AddActorWorldOffset(FVector(0, 0, -Drop), true);
    }
    Pawn->GetCharacterMovement()->StopMovementImmediately();
    Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Pawn->SetPose(EPYRPose::Grounded, Seconds);
    bWasGrounded = true;
    APYRFxBurst::Spawn(GetWorld(), Pawn->GetActorLocation(), Pawn->Info ? Pawn->Info->Colour : FLinearColor::Gray, 10, 10, 260, .6f, TEXT("Sphere"), -300);
}

void UPYRFlierBody::TickBody(float Dt)
{
    APYRMonPawn* Pawn = Mon();
    if (!Pawn) return;
    TickDash(Dt);
    UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
    if (IsGrounded() || Pawn->bFainted)
    {
        if (Move->MovementMode == MOVE_Flying) Move->SetMovementMode(MOVE_Falling);
        return;
    }
    if (bWasGrounded || Move->MovementMode != MOVE_Flying)
    {
        bWasGrounded = false;
        Move->SetMovementMode(MOVE_Flying);
    }
    if (Now() < DiveUntil) return;  // The dive attack moves it.
    float Floor;
    if (!APYRBattleManager::GroundZ(GetWorld(), Pawn->GetActorLocation(), Floor, Pawn)) return;
    const APYRBattleManager* M = Pawn->Manager();
    const bool bBattle = M && M->IsBattleActive() && !Pawn->bVisualOnly;
    // Over the pool it hovers above the surface, whatever lies under the water.
    if (bBattle && M->GetPool().IsActive() && M->GetPool().InFootprint(Pawn->GetActorLocation())) Floor = FMath::Max(Floor, M->GetPool().SurfaceZ());
    // Hover height grows with the body (field size hovers lower).
    const float Half = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float Hover = Pawn->Info->HoverHeight() * FMath::Clamp(Pawn->GetDisplayScale(), .3f, 1.f);
    float Target = Floor + FMath::Max(Hover, Half + 5.f);
    // Cave rings: never higher than the ceiling minus the half-height (the top stays under the rock).
    if (bBattle && M->HasCeiling()) Target = FMath::Max(Floor + Half + 2.f, FMath::Min(Target, M->GetCeilingZ() - Half - 5.f));
    const float Step = FMath::Clamp(Target - float(Pawn->GetActorLocation().Z), -700.f * Dt, 700.f * Dt);
    if (FMath::Abs(Step) > .01f) Pawn->AddActorWorldOffset(FVector(0, 0, Step), true);
    Move->Velocity.Z = 0;
}

void UPYRCrawlerBody::Configure(APYRMonPawn* Pawn)
{
    Super::Configure(Pawn);
    DodgeCooldown = 1.f; DodgeInvulnerable = TuckSeconds;
    UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
    Move->SetMovementMode(MOVE_Walking);
    Move->MaxStepHeight = 20.f;
    Move->AirControl = .6f;
}

bool UPYRCrawlerBody::TryDodge(FVector Dir)
{
    APYRMonPawn* Pawn = Mon();
    if (!Pawn || !IsDodgeReady()) return false;
    DodgeStart = Pawn->GetActorLocation();
    DodgeEndsAt = Now() + TuckSeconds;
    DodgeReadyAt = Now() + DodgeCooldown;
    Pawn->InvulnerableUntil = FMath::Max(Pawn->InvulnerableUntil, Now() + TuckSeconds);
    Pawn->GetCharacterMovement()->StopMovementImmediately();
    Pawn->SetPose(EPYRPose::Tuck, TuckSeconds);
    APYRMoveFx::Spawn(GetWorld(), EPYRFx::DustPuff, Pawn->GetActorLocation(), FVector::ForwardVector, FLinearColor(.9f, .85f, .7f), 70.f, .35f);
    return true;
}

void UPYRCrawlerBody::TickBody(float Dt)
{
    if (IsDodging()) if (APYRMonPawn* Pawn = Mon()) Pawn->GetCharacterMovement()->StopMovementImmediately();
}

void UPYRGiantBody::Configure(APYRMonPawn* Pawn)
{
    Super::Configure(Pawn);
    UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
    Move->SetMovementMode(MOVE_Walking);
    Move->bOrientRotationToMovement = false;  // Turns itself, slowly, toward its foe.
    Move->MaxStepHeight = 60.f;
}

void UPYRSwimmerBody::Configure(APYRMonPawn* Pawn)
{
    Super::Configure(Pawn);
    DodgeCooldown = 1.4f; DodgeInvulnerable = DashSeconds;
    UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
    Move->SetMovementMode(MOVE_Walking);
    Move->MaxStepHeight = 20.f;
    Move->MaxWalkSpeed = Pawn->Info ? Pawn->Info->LandSpeed() : 150.f;
}

bool UPYRSwimmerBody::TryDodge(FVector Dir)
{
    APYRMonPawn* Pawn = Mon();
    if (!Pawn || !IsDodgeReady() || Pawn->bDived) return false;
    Dir = FlatDodgeDir(Pawn, Dir);
    // In the water a fast dart; on land a short flop.
    const float Distance = bInWater ? DashDistance : 150.f;
    DodgeStart = Pawn->GetActorLocation();
    DodgeVelocity = Dir * (Distance / DashSeconds);
    DashLeft = Distance;
    DodgeEndsAt = Now() + DashSeconds;
    DodgeReadyAt = Now() + DodgeCooldown;
    Pawn->InvulnerableUntil = FMath::Max(Pawn->InvulnerableUntil, Now() + DodgeInvulnerable);
    Pawn->GetCharacterMovement()->StopMovementImmediately();
    Pawn->SetActorRotation(Dir.Rotation());
    if (bInWater)
    {
        const APYRBattleManager* M = Pawn->Manager();
        const float Surface = M ? M->GetPool().SurfaceZ() : Pawn->FloorZ();
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::Splash, FVector(DodgeStart.X, DodgeStart.Y, Surface), -Dir, PYRFx::Palette(TEXT("6ec6f0")), 60.f, .5f);
    }
    else APYRMoveFx::Spawn(GetWorld(), EPYRFx::DustPuff, Pawn->GetActorLocation(), FVector::ForwardVector, FLinearColor(.9f, .85f, .7f), 60.f, .35f);
    return true;
}

void UPYRSwimmerBody::TickBody(float Dt)
{
    APYRMonPawn* Pawn = Mon();
    if (!Pawn || !Pawn->Info) return;
    TickDash(Dt);
    UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
    const APYRBattleManager* M = Pawn->Manager();
    const FPYRPoolRules* Pool = M && M->IsBattleActive() && !Pawn->bVisualOnly && M->GetPool().IsActive() ? &M->GetPool() : nullptr;
    const FVector At = Pawn->GetActorLocation();
    // Once in, it stays in while over the pool (platform sides and the walkway edge hold it in the water).
    bInWater = Pool && (bInWater ? Pool->InFootprint(At) : Pool->IsOpenWater(At));
    if (Pawn->bFainted)
    {
        if (bInWater && Move->MovementMode != MOVE_Flying) Move->SetMovementMode(MOVE_Flying);
        if (bInWater) Move->Velocity = FVector::ZeroVector;
        return;
    }
    if (bInWater)
    {
        if (!bIgnoring) { Pool->IgnoreWater(Pawn->GetCapsuleComponent()); bIgnoring = true; }
        if (Move->MovementMode != MOVE_Flying) Move->SetMovementMode(MOVE_Flying);
        Move->MaxFlySpeed = Pawn->Info->MoveSpeed() * Pawn->SpeedFactor();
        const float Half = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        // At the surface the body rides half out of the water; dived, it is fully under.
        const float Target = Pawn->bDived ? Pool->SurfaceZ() - Half - 70.f : Pool->SurfaceZ() + Half * SurfaceRide;
        const float Step = FMath::Clamp(Target - float(At.Z), -500.f * Dt, 500.f * Dt);
        if (FMath::Abs(Step) > .01f) Pawn->AddActorWorldOffset(FVector(0, 0, Step), true);
        Move->Velocity.Z = 0;
        return;
    }
    // Dry ground: a slow Magikarp flop.
    if (Pawn->bDived) Pawn->SurfaceNow(false);
    if (Move->MovementMode == MOVE_Flying) Move->SetMovementMode(MOVE_Falling);
    Move->MaxWalkSpeed = Pawn->Info->LandSpeed() * Pawn->SpeedFactor();
    FlopClock += Dt;
    if (Pawn->GetVelocity().Size2D() > 20.f && FlopClock > .1f) { FlopClock = 0; Pawn->SetPose(EPYRPose::Flop, .15f); }
}

// ------------------------------------------------------------------------------------------------ pawn
APYRMonPawn::APYRMonPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    AutoPossessAI = EAutoPossessAI::Disabled;
    AIControllerClass = nullptr;
    bUseControllerRotationYaw = false;
    GetCapsuleComponent()->InitCapsuleSize(30.f, 45.f);
    UCharacterMovementComponent* Move = GetCharacterMovement();
    Move->bRunPhysicsWithNoController = true;
    Move->bOrientRotationToMovement = true;
    Move->RotationRate = FRotator(0, 720, 0);
    Move->MaxWalkSpeed = 500.f;
    Move->NavAgentProps.bCanFly = true;
    Move->bUseControllerDesiredRotation = false;
    VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
    VisualRoot->SetupAttachment(GetCapsuleComponent());
    if (USkeletalMeshComponent* Unused = GetMesh()) Unused->SetVisibility(false);
}

APYRBattleManager* APYRMonPawn::Manager() const { return APYRBattleManager::Find(this); }
double APYRMonPawn::Now() const { return APYRBattleManager::BattleNow(this); }

APYRMonPawn* APYRMonPawn::Opponent() const
{
    APYRBattleManager* M = Manager();
    if (!M) return nullptr;
    return bPlayerSide ? M->FoeMon() : M->PlayerMon();
}

float APYRMonPawn::FieldScale() const
{
    if (!Info) return 1.f;
    return FMath::Clamp(Info->FieldHeightCm() / FMath::Max(1.f, Info->BattleHeightCm()), .1f, 1.f);
}

float APYRMonPawn::GetBattleScreenHeightM() const
{
    return Info ? Info->BattleHeightCm() * DisplayScale / 100.f : 0.f;
}

void APYRMonPawn::GrowTo(float Scale, float Seconds)
{
    GrowFrom = DisplayScale;
    GrowTarget = Scale;
    GrowAge = 0;
    GrowSeconds = FMath::Max(0.f, Seconds);
    if (GrowSeconds <= 0) ApplyScale(Scale);
}

void APYRMonPawn::ApplyScale(float Scale)
{
    UCapsuleComponent* Cap = GetCapsuleComponent();
    const float OldHalf = Cap->GetScaledCapsuleHalfHeight();
    const FVector At = GetActorLocation();
    DisplayScale = Scale;
    SetActorScale3D(FVector(Scale));
    // Keep the feet where they were (fliers re-hover in their body tick).
    const float NewHalf = Cap->GetScaledCapsuleHalfHeight();
    if (!FMath::IsNearlyEqual(OldHalf, NewHalf)) SetActorLocation(At + FVector(0, 0, NewHalf - OldHalf), false, nullptr, ETeleportType::TeleportPhysics);
}

void APYRMonPawn::ApplySpecies()
{
    Info = PYRData::Species(Species);
    if (!Info) return;
    GetCapsuleComponent()->SetCapsuleSize(Info->CapsuleRadius(), Info->CapsuleHalfHeight());
}

void APYRMonPawn::InitVisualOnly(FName InSpecies)
{
    bVisualOnly = true;
    Species = InSpecies;
    ApplySpecies();
    if (Info) GetCharacterMovement()->MaxWalkSpeed = Info->MoveSpeed();
    // Field bodies walk; whoever drives them decides where (no hover, no brain).
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    GetCharacterMovement()->MaxStepHeight = 20.f;
    BuildVisual();
    ApplyScale(FieldScale());
}

void APYRMonPawn::InitMon(FName InSpecies, int32 InLevel, float InHp, float InMaxHp, const TArray<FName>& InCarried, bool bInPlayerSide, int32 InPartyIndex, EPYRStatus InStatus)
{
    Species = InSpecies;
    Level = InLevel;
    MaxHp = InMaxHp;
    Hp = InHp;
    Status = InStatus;
    Carried = InCarried;
    bPlayerSide = bInPlayerSide;
    PartyIndex = InPartyIndex;
    Tags.AddUnique(bPlayerSide ? TEXT("BattleMon.Player") : TEXT("BattleMon.Foe"));
    for (int32 i = 0; i < 4; ++i)
    {
        Cooldown[i] = 0;
        const FPYRMoveInfo* M = Carried.IsValidIndex(i) ? PYRData::Move(Carried[i]) : nullptr;
        CooldownTotal[i] = M ? MoveCooldown(*M) : 1.f;
    }
    ChangeSpecies(InSpecies);
    StrafeSign = FMath::RandBool() ? 1.f : -1.f;
}

void APYRMonPawn::ChangeSpecies(FName NewSpecies)
{
    Species = NewSpecies;
    ApplySpecies();
    if (!Info) return;
    if (BodyComp) { BodyComp->DestroyComponent(); BodyComp = nullptr; }
    switch (Info->Body)
    {
    case EPYRBody::Runner: BodyComp = NewObject<UPYRRunnerBody>(this); break;
    case EPYRBody::Flier: BodyComp = NewObject<UPYRFlierBody>(this); break;
    case EPYRBody::Crawler: BodyComp = NewObject<UPYRCrawlerBody>(this); break;
    case EPYRBody::Swimmer: BodyComp = NewObject<UPYRSwimmerBody>(this); break;
    default: BodyComp = NewObject<UPYRGiantBody>(this); break;
    }
    BodyComp->RegisterComponent();
    AddInstanceComponent(BodyComp);
    BodyComp->Configure(this);
    GetCharacterMovement()->bOrientRotationToMovement = Info->Body != EPYRBody::Giant;
    ClearVisual();
    BuildVisual();
    if (Shadow) { Shadow->DestroyComponent(); Shadow = nullptr; }
    if (Info->Body == EPYRBody::Flier)
    {
        // Fliers cast a crisp shadow straight down.
        Shadow = PYRFx::AddPrimitive(this, nullptr, TEXT("Cylinder"), FVector::ZeroVector, FVector(Info->CapsuleRadius() * 1.6f / 100.f, Info->CapsuleRadius() * 1.6f / 100.f, .01f), FLinearColor(.08f, .07f, .06f));
        Shadow->SetUsingAbsoluteLocation(true);
        Shadow->SetUsingAbsoluteRotation(true);
        Shadow->CastShadow = false;
    }
    ApplyScale(DisplayScale);
}

float APYRMonPawn::MoveCooldown(const FPYRMoveInfo& Move) const
{
    float Seconds = Move.Cooldown;
    if (bPlayerSide && Species == PYRData::Pikachu && Move.Id == PYRData::ThunderShock)
        if (const UPYRTrainerState* State = UPYRTrainerState::Get(this))
            if (State->GetBond() == EPYRBond::BestFriends) Seconds *= 2.f / 3.f;
    return Seconds;
}

float APYRMonPawn::GetCooldownFraction(int32 Slot) const
{
    if (Slot < 0 || Slot > 3 || !Carried.IsValidIndex(Slot)) return 0.f;
    return FMath::Clamp(Cooldown[Slot] / FMath::Max(.01f, CooldownTotal[Slot]), 0.f, 1.f);
}

bool APYRMonPawn::IsMoveReady(int32 Slot) const
{
    return Carried.IsValidIndex(Slot) && Slot < 4 && Cooldown[Slot] <= 0.f && !IsSlotDisabled(Slot);
}

int32 APYRMonPawn::SlotOf(FName Move) const
{
    for (int32 i = 0; i < FMath::Min(4, Carried.Num()); ++i) if (Carried[i] == Move) return i;
    return INDEX_NONE;
}

int32 APYRMonPawn::AddStage(EPYRStat Stat, int32 Delta)
{
    int32& S = Stages[(int32)Stat];
    const int32 Before = S;
    S = FMath::Clamp(S + Delta, -6, 6);
    return S - Before;
}

float APYRMonPawn::EffectiveStat(EPYRStat Stat) const
{
    if (!Info) return 10.f;
    float V = PYRData::StatAt(*Info, Stat, Level);
    // Yellow's badge boosts apply to Red's Pokemon only (x1.125, capped at 999).
    if (bPlayerSide && Stat <= EPYRStat::Special)
        if (const UPYRTrainerState* State = UPYRTrainerState::Get(this)) V = FMath::Min(999.f, FMath::FloorToFloat(V * State->BadgeBoost(Stat)));
    V *= PYRData::StageMultiplier(GetStage(Stat));
    if (Stat == EPYRStat::Speed && Status == EPYRStatus::Paralysis) V *= .25f;
    if (Stat == EPYRStat::Attack && Status == EPYRStatus::Burn) V *= .5f;  // Gen 1: a burn halves Attack.
    return FMath::Max(1.f, V);
}

void APYRMonPawn::FallAsleep(float Seconds)
{
    Status = EPYRStatus::Sleep;
    SleepUntil = Now() + Seconds;
    StunnedUntil = FMath::Max(StunnedUntil, SleepUntil);
    SetPose(EPYRPose::Sleep, Seconds);
    SetGlow(false);
    NextStatusFx = 0;
}

bool APYRMonPawn::IsAttacking() const
{
    const APYRAttack* A = CurrentAttack.Get();
    return A && A->IsLive();
}

bool APYRMonPawn::IsInWater() const
{
    const UPYRSwimmerBody* Swim = Cast<UPYRSwimmerBody>(BodyComp);
    return Swim && Swim->IsInWater();
}

bool APYRMonPawn::TryDive()
{
    if (!IsSwimmer() || bDived || bFainted || !IsInWater() || Now() < SurfacedUntil || IsAttacking()) return false;
    bDived = true;
    DivedAt = Now();
    SetGlow(false);
    if (const APYRBattleManager* M = Manager())
    {
        const FVector At(GetActorLocation().X, GetActorLocation().Y, M->GetPool().SurfaceZ() + 2.f);
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::Splash, At, FVector::ForwardVector, PYRFx::Palette(TEXT("6ec6f0")), 50.f, .45f);
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::Ripple, At, FVector::ForwardVector, PYRFx::Palette(TEXT("6ec6f0")), 160.f, .8f);
    }
    return true;
}

void APYRMonPawn::SurfaceNow(bool bBigSplash)
{
    if (!bDived) return;
    bDived = false;
    SurfacedUntil = Now() + 2.0;  // Targetable for about 2 s after coming up.
    if (const APYRBattleManager* M = Manager())
    {
        const FVector At(GetActorLocation().X, GetActorLocation().Y, M->GetPool().IsActive() ? M->GetPool().SurfaceZ() + 2.f : FloorZ());
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::Splash, At, FVector::ForwardVector, PYRFx::Palette(TEXT("6ec6f0")), bBigSplash ? 110.f : 50.f, .6f);
    }
    // Pop straight up to the surface line.
    if (const APYRBattleManager* M = Manager())
        if (M->GetPool().IsActive())
        {
            const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            FVector At = GetActorLocation();
            At.Z = M->GetPool().SurfaceZ() + Half * UPYRSwimmerBody::SurfaceRide;
            SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
        }
    UpdateDivedLook();
}

bool APYRMonPawn::IsUntargetable() const
{
    return bDived || Now() < BurrowUntil || FellInUntil > 0;
}

bool APYRMonPawn::IsAirborne() const
{
    const UCharacterMovementComponent* Move = GetCharacterMovement();
    return Move && Move->IsFalling();
}

bool APYRMonPawn::Hop()
{
    if (!Info || bFainted || IsStunned() || Now() < BusyUntil || FellInUntil > 0) return false;
    const EPYRBody B = GetBody();
    if (B != EPYRBody::Runner && B != EPYRBody::Crawler) return false;
    UCharacterMovementComponent* Move = GetCharacterMovement();
    if (!Move->IsMovingOnGround()) return false;
    FVector Dir = LastSteer.IsNearlyZero() ? GetActorForwardVector() : LastSteer;
    Dir.Z = 0;
    Dir = Dir.GetSafeNormal();
    // Runners leap about 5.4 m at full run, crawlers about 4.1 m: either clears a gap under 4 m.
    const float Ahead = B == EPYRBody::Runner ? 700.f : 560.f;
    const float Up = B == EPYRBody::Runner ? 380.f : 360.f;  // Peaks under 75 cm: no climbing a 100 cm ledge.
    HopFrom = GetActorLocation();
    HopFloorZ = BottomZ();
    bHopping = true;
    LaunchCharacter(Dir * Ahead * FMath::Clamp(SpeedFactor(), .6f, 1.f) + FVector(0, 0, Up), true, true);
    SetActorRotation(Dir.Rotation());
    ++Hops;
    APYRMoveFx::Spawn(GetWorld(), EPYRFx::DustPuff, FVector(GetActorLocation().X, GetActorLocation().Y, BottomZ()), FVector::ForwardVector, FLinearColor(.9f, .85f, .7f), 60.f, .3f);
    return true;
}

bool APYRMonPawn::IsBiding() const { return Now() < BideUntil; }
bool APYRMonPawn::IsInvulnerable() const { return Now() < InvulnerableUntil; }
bool APYRMonPawn::IsStunned() const { return Now() < StunnedUntil; }

float APYRMonPawn::SpeedFactor() const
{
    // Speed stages move the body faster or slower; paralysis halves it (a quarter would pin it in place).
    float F = FMath::Clamp(PYRData::StageMultiplier(GetStage(EPYRStat::Speed)), .4f, 1.6f);
    if (Status == EPYRStatus::Paralysis) F *= .5f;
    return F;
}

float APYRMonPawn::FloorZ() const
{
    float Z;
    if (APYRBattleManager::GroundZ(GetWorld(), GetActorLocation(), Z, this)) return Z;
    return BottomZ();
}

float APYRMonPawn::BottomZ() const { return GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); }
float APYRMonPawn::TopZ() const { return GetActorLocation().Z + GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); }
float APYRMonPawn::CapsuleRadius() const { return GetCapsuleComponent()->GetScaledCapsuleRadius(); }

void APYRMonPawn::Steer(FVector WorldDir)
{
    WorldDir.Z = 0;
    if (WorldDir.IsNearlyZero() || bFainted || IsStunned() || Now() < BusyUntil) return;
    if (BodyComp && BodyComp->IsDodging()) return;
    WorldDir = WorldDir.GetClampedToMaxSize(1.f);
    LastSteer = WorldDir.GetSafeNormal();
    AddMovementInput(WorldDir, 1.f);
}

bool APYRMonPawn::Dodge(FVector Dir)
{
    if (!IsDodgeReady() || Now() < BusyUntil) return false;
    if (Dir.IsNearlyZero()) Dir = LastSteer;
    return BodyComp->TryDodge(Dir);
}

void APYRMonPawn::SetPose(EPYRPose InPose, float Seconds)
{
    Pose = InPose;
    PoseUntil = Seconds > 0 ? Now() + Seconds : 1e12;
}

void APYRMonPawn::SetGlow(bool bOn, FLinearColor Colour)
{
    if (!Glow)
    {
        // A flat glow disc on the floor and an aura shell around the body (the anime charge-up).
        Glow = PYRFx::AddPrimitive(this, nullptr, TEXT("Cylinder"), FVector::ZeroVector, FVector(1, 1, .012f), Colour, FRotator::ZeroRotator, true);
        Glow->SetUsingAbsoluteLocation(true);
        Glow->SetUsingAbsoluteScale(true);
        Glow->CastShadow = false;
        Aura = PYRFx::AddPrimitive(this, VisualRoot, TEXT("Sphere"), FVector::ZeroVector, FVector(1), Colour, FRotator::ZeroRotator, true);
        Aura->CastShadow = false;
        Aura->SetTranslucentSortPriority(10);
    }
    PYRFx::SetColour(Glow, Colour);
    PYRFx::SetColour(Aura, Colour * FLinearColor(1, 1, 1, .45f));
    Glow->SetVisibility(bOn);
    Aura->SetVisibility(bOn);
}

bool APYRMonPawn::IsGlowing() const { return Glow && Glow->IsVisible(); }

void APYRMonPawn::FlashHit() { FlashUntil = Now() + .12; }

void APYRMonPawn::SetFaintLook()
{
    if (bDived) SurfaceNow(false);
    BurrowUntil = 0;
    bFainted = true;
    SetPose(EPYRPose::Faint, 0);
    SetGlow(false);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    if (BodyComp && GetBody() == EPYRBody::Flier) GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    if (Rig) Rig->PlayDeath();
}

// ------------------------------------------------------------------------------------------------ look
UStaticMeshComponent* APYRMonPawn::AddPart(const TCHAR* ShapeName, FVector Loc, FVector Scale, FLinearColor Colour, FRotator Rot, bool bWing)
{
    UStaticMeshComponent* Part = PYRFx::AddPrimitive(this, VisualRoot, ShapeName, Loc, Scale, Colour, Rot);
    Parts.Add(Part);
    if (bWing) Wings.Add(Part);
    return Part;
}

void APYRMonPawn::ClearVisual()
{
    for (UStaticMeshComponent* P : Parts) if (P) P->DestroyComponent();
    Parts.Reset();
    Wings.Reset();
    if (Rig)
    {
        if (Rig->GetRig()) Rig->GetRig()->DestroyComponent();
        Rig->DestroyComponent();
        Rig = nullptr;
    }
    bUsesImportedMesh = false;
}

void APYRMonPawn::BuildVisual()
{
    if (!Info) return;
    // Everything is built at battle size; the actor scale shrinks it to field size outside battle.
    const FString Id = PYRSpecies::AssetId(Species);
    const float Show = Info->BattleHeightCm();
    const float Half = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    const FString SkPackage = FString::Printf(TEXT("/Game/Pokemon/%s/SK_%s"), *Id, *Id);
    if (FPackageName::DoesPackageExist(SkPackage))
    {
        Rig = NewObject<UPYRMonRigAnimatorComponent>(this);
        Rig->bHidePlaceholders = false;
        Rig->RegisterComponent();
        AddInstanceComponent(Rig);
        if (Rig->FitPath(FString::Printf(TEXT("%s.SK_%s"), *SkPackage, *Id), EPYRMonRigFamily::Creature, Show) && Rig->GetRig())
        {
            Rig->GetRig()->AttachToComponent(VisualRoot, FAttachmentTransformRules::KeepRelativeTransform);
            bUsesImportedMesh = true;
            return;
        }
        Rig->DestroyComponent();
        Rig = nullptr;
    }
    const FString SmPackage = FString::Printf(TEXT("/Game/Pokemon/%s/SM_%s"), *Id, *Id);
    if (FPackageName::DoesPackageExist(SmPackage))
        if (UStaticMesh* Found = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.SM_%s"), *SmPackage, *Id)))
        {
            UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(this);
            Body->SetStaticMesh(Found);
            Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Body->SetupAttachment(VisualRoot);
            const FBoxSphereBounds B = Found->GetBounds();
            // Crawlers are sized by their length (they are long and low); the rest by height.
            const float Extent = Info->Body == EPYRBody::Crawler ? float(FMath::Max3(B.BoxExtent.X, B.BoxExtent.Y, B.BoxExtent.Z)) : float(B.BoxExtent.Z);
            const float K = Show / FMath::Max(1.f, Extent * 2.f);
            Body->SetRelativeScale3D(FVector(K));
            Body->SetRelativeLocation(FVector(-B.Origin.X * K, -B.Origin.Y * K, -Half - (B.Origin.Z - B.BoxExtent.Z) * K));
            Body->RegisterComponent();
            AddInstanceComponent(Body);
            Parts.Add(Body);
            bUsesImportedMesh = true;
            return;
        }
    BuildPrimitives();
}

void APYRMonPawn::BuildPrimitives()
{
    // Clean flat-colour shapes in the species colours, sized to the battle display height H.
    const float H = Info->BattleHeightCm();
    const float Z0 = -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    const FLinearColor C = Info->Colour, A = Info->Accent;
    const FLinearColor Black(.03f, .03f, .03f), White(.97f, .95f, .9f), Red = PYRFx::Palette(TEXT("e3342f"));
    auto S = [H](float X, float Y, float Z) { return FVector(X, Y, Z) * H / 10000.f; };  // Percent of H, as a scale.
    auto P = [H, Z0](float X, float Y, float Z) { return FVector(X * H / 100.f, Y * H / 100.f, Z0 + Z * H / 100.f); };
    switch (Info->Body)
    {
    case EPYRBody::Runner:
    {
        const bool bPikachu = Species == Pikachu;
        AddPart(TEXT("Sphere"), P(-4, 0, 30), S(60, 42, 44), C);                                  // Body.
        AddPart(TEXT("Sphere"), P(24, 0, 58), S(44, 46, 42), C);                                  // Head.
        for (int32 Side = -1; Side <= 1; Side += 2)
        {
            AddPart(TEXT("Cone"), P(18, Side * 13.f, 88), S(11, 10, 34), bPikachu ? C : A, FRotator(0, 0, Side * 18.f));   // Ear.
            if (bPikachu) AddPart(TEXT("Cone"), P(18, Side * 18.f, 102), S(7, 7, 12), Black, FRotator(0, 0, Side * 18.f));
            AddPart(TEXT("Sphere"), P(42, Side * 14.f, 64), S(8, 8, 9), Black);                    // Eye.
            AddPart(TEXT("Sphere"), P(43, Side * 15.f, 66), S(3, 3, 3), White);                    // Eye shine.
            if (bPikachu) AddPart(TEXT("Sphere"), P(40, Side * 17.f, 52), S(11, 11, 11), Red);    // Cheek pouch.
            AddPart(TEXT("Sphere"), P(12, Side * 13.f, 7), S(15, 11, 13), C);                      // Front paw.
            AddPart(TEXT("Sphere"), P(-24, Side * 13.f, 7), S(17, 12, 13), C);                     // Hind paw.
        }
        if (bPikachu)
        {
            const FLinearColor Brown = PYRFx::Palette(TEXT("7a4a26"));
            AddPart(TEXT("Cube"), P(-6, 0, 50), S(6, 32, 3), Brown);                               // Back stripes.
            AddPart(TEXT("Cube"), P(-18, 0, 47), S(6, 30, 3), Brown);
            AddPart(TEXT("Cube"), P(-38, 0, 40), S(6, 4, 28), Brown, FRotator(-30, 0, 0));         // Lightning tail.
            AddPart(TEXT("Cube"), P(-46, 0, 62), S(24, 4, 11), C, FRotator(20, 0, 0));
            AddPart(TEXT("Cube"), P(-54, 0, 80), S(9, 4, 28), C, FRotator(-30, 0, 0));
        }
        else AddPart(TEXT("Cone"), P(-40, 0, 36), S(12, 12, 36), A, FRotator(-60, 0, 0));        // Tail.
        break;
    }
    case EPYRBody::Flier:
    {
        const bool bButterfree = Species == Butterfree;
        const bool bBird = Species == Pidgey || Species == Pidgeotto || Species == Spearow;
        const float Mid = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
        auto Q = [H, Mid, Z0](float X, float Y, float Z) { return FVector(X * H / 100.f, Y * H / 100.f, Z0 + Mid + Z * H / 100.f); };
        AddPart(TEXT("Sphere"), Q(0, 0, 0), S(56, 46, 50), bButterfree ? PYRFx::Palette(TEXT("3a3a70")) : C);
        if (!bButterfree) AddPart(TEXT("Sphere"), Q(10, 0, -6), S(42, 36, 38), A);               // Cream belly.
        AddPart(TEXT("Sphere"), Q(24, 0, 24), S(36, 36, 36), bButterfree ? PYRFx::Palette(TEXT("5a5aa8")) : C);  // Head.
        if (bBird) AddPart(TEXT("Cone"), Q(44, 0, 22), S(10, 10, 15), PYRFx::Palette(TEXT("f0a0a0")), FRotator(-90, 0, 0));  // Beak.
        for (int32 Side = -1; Side <= 1; Side += 2)
        {
            AddPart(TEXT("Sphere"), Q(36, Side * 10.f, 28), S(bButterfree ? 14.f : 7.f, bButterfree ? 14.f : 7.f, bButterfree ? 14.f : 8.f), bButterfree ? Red : Black);  // Eye.
            AddPart(TEXT("Sphere"), Q(-2, Side * 34.f, 10), bButterfree ? S(46, 52, 6) : S(44, 44, 8), bButterfree ? White : C, FRotator(0, 0, Side * -15.f), true);  // Wing.
        }
        AddPart(TEXT("Cube"), Q(-32, 0, 0), S(22, 20, 5), A, FRotator(15, 0, 0));                 // Tail.
        break;
    }
    case EPYRBody::Crawler:
    {
        if (Species == Metapod || Species == Kakuna)
        {
            // An upright cocoon.
            AddPart(TEXT("Sphere"), P(0, 0, 50), S(52, 50, 100), C);
            AddPart(TEXT("Sphere"), P(18, 0, 70), S(26, 40, 20), A);
            for (int32 Side = -1; Side <= 1; Side += 2) AddPart(TEXT("Cube"), P(22, Side * 9.f, 72), S(10, 6, 3), Black, FRotator(0, 0, Side * 20.f));
        }
        else if (Species == Geodude)
        {
            AddPart(TEXT("Sphere"), P(0, 0, 45), S(70, 70, 64), C);
            for (int32 Side = -1; Side <= 1; Side += 2)
            {
                AddPart(TEXT("Sphere"), P(10, Side * 50.f, 45), S(30, 26, 26), C);                // Fist.
                AddPart(TEXT("Cube"), P(30, Side * 12.f, 56), S(10, 8, 4), Black, FRotator(0, 0, Side * -15.f));  // Brow.
            }
        }
        else if (Species == Diglett)
        {
            AddPart(TEXT("Cylinder"), P(0, 0, 30), S(36, 36, 60), C);
            AddPart(TEXT("Sphere"), P(0, 0, 60), S(36, 36, 30), C);
            AddPart(TEXT("Sphere"), P(18, 0, 45), S(12, 16, 9), A);                               // Pink nose.
            AddPart(TEXT("Cylinder"), P(0, 0, 2), S(60, 60, 6), PYRFx::Palette(TEXT("6a5030")));  // Dirt.
        }
        else
        {
            const bool bWeedle = Species == Weedle;
            for (int32 i = 0; i < 4; ++i)
            {
                const float X = -38.f + i * 16.f;
                AddPart(TEXT("Sphere"), P(X, 0, 18), S(24, 26, 26), C);
                AddPart(TEXT("Sphere"), P(X + 2, 0, 9), S(18, 22, 12), A);
            }
            AddPart(TEXT("Sphere"), P(30, 0, 26), S(32, 32, 32), C);                              // Head.
            for (int32 Side = -1; Side <= 1; Side += 2) AddPart(TEXT("Sphere"), P(42, Side * 9.f, 32), S(10, 10, 12), Black);
            if (bWeedle)
            {
                AddPart(TEXT("Cone"), P(32, 0, 46), S(7, 7, 16), White);                          // Head horn.
                AddPart(TEXT("Cone"), P(-52, 0, 20), S(6, 6, 15), White, FRotator(90, 0, 0));      // Tail sting.
            }
            else
            {
                AddPart(TEXT("Cylinder"), P(32, 0, 44), S(4, 4, 13), Red);                        // Forked antenna.
                AddPart(TEXT("Cylinder"), P(32, -5, 52), S(3, 3, 9), Red, FRotator(0, 0, -30));
                AddPart(TEXT("Cylinder"), P(32, 5, 52), S(3, 3, 9), Red, FRotator(0, 0, 30));
            }
        }
        break;
    }
    case EPYRBody::Swimmer:
    {
        if (Species == Staryu || Species == Starmie)
        {
            // An upright star facing forward (Starmie: two stars, the back one turned) around a red jewel.
            const int32 StarLayers = Species == Starmie ? 2 : 1;
            for (int32 L = 0; L < StarLayers; ++L)
                for (int32 i = 0; i < 5; ++i)
                {
                    const float Ang = FMath::DegreesToRadians(90.f + i * 72.f + L * 36.f);
                    const FVector D(0, FMath::Cos(Ang), FMath::Sin(Ang));
                    AddPart(TEXT("Cone"), P(-L * 8.f, D.Y * 24.f, 50 + D.Z * 24.f), S(22, 12, 30), L == 0 ? C : C * .75f, FRotationMatrix::MakeFromZ(D).Rotator());
                }
            AddPart(TEXT("Sphere"), P(0, 0, 50), S(16, 36, 36), C);
            AddPart(TEXT("Sphere"), P(7, 0, 50), S(10, 16, 16), A);                              // Jewel core.
            AddPart(TEXT("Sphere"), P(11, -3, 54), S(4, 5, 5), White);                           // Its shine.
        }
        else if (Species == Shellder)
        {
            AddPart(TEXT("Sphere"), P(0, 0, 45), S(70, 70, 40), C);                            // Upper shell.
            AddPart(TEXT("Sphere"), P(0, 0, 25), S(66, 66, 30), C * .85f);                     // Lower shell.
            AddPart(TEXT("Sphere"), P(24, 0, 34), S(24, 30, 18), White);                       // Pearl face.
            AddPart(TEXT("Sphere"), P(34, 0, 26), S(16, 10, 5), A);                            // Tongue.
            for (int32 Side = -1; Side <= 1; Side += 2) AddPart(TEXT("Sphere"), P(34, Side * 7.f, 38), S(6, 6, 7), Black);
        }
        else
        {
            // A fish: body, tail fin, dorsal fin and big eyes (Goldeen gets its horn, Magikarp its whiskers).
            AddPart(TEXT("Sphere"), P(0, 0, 40), S(80, 40, 50), C);
            AddPart(TEXT("Sphere"), P(4, 0, 32), S(56, 30, 26), A);                           // Belly.
            AddPart(TEXT("Cone"), P(-48, 0, 40), S(30, 8, 30), A, FRotator(90, 0, 0));        // Tail fin.
            AddPart(TEXT("Cone"), P(-4, 0, 66), S(18, 5, 22), A);                             // Dorsal fin.
            for (int32 Side = -1; Side <= 1; Side += 2)
            {
                AddPart(TEXT("Sphere"), P(26, Side * 14.f, 48), S(11, 9, 11), White);
                AddPart(TEXT("Sphere"), P(30, Side * 16.f, 48), S(5, 4, 6), Black);
                if (Species == Magikarp) AddPart(TEXT("Cylinder"), P(36, Side * 10.f, 36), S(2, 2, 18), A, FRotator(0, 0, Side * 50.f));
            }
            if (Species == Goldeen) AddPart(TEXT("Cone"), P(40, 0, 56), S(7, 7, 22), White, FRotator(-60, 0, 0));
        }
        break;
    }
    default:
    {
        // Onix: a chain of boulders from the tail on the floor to the rearing head.
        const int32 Count = 9;
        for (int32 i = 0; i < Count; ++i)
        {
            const float T = i / float(Count - 1);
            const FVector Pos(FMath::Lerp(-620.f, 60.f, T), FMath::Sin(T * PI * 1.5f) * 120.f, Z0 + 70.f + FMath::Pow(T, 1.6f) * 620.f);
            const float Size = FMath::Lerp(110.f, 200.f, T);
            AddPart(TEXT("Sphere"), Pos, FVector(Size, Size * .92f, Size * .88f) / 100.f, FMath::Lerp(A, C, .55f + .45f * FMath::Frac(i * .37f)));
        }
        const FVector Head(150, 0, Z0 + 720);
        AddPart(TEXT("Sphere"), Head, FVector(230, 170, 150) / 100.f, C);
        AddPart(TEXT("Cone"), Head + FVector(10, 0, 110), FVector(40, 40, 110) / 100.f, A, FRotator(-25, 0, 0));  // Horn.
        for (int32 Side = -1; Side <= 1; Side += 2)
            AddPart(TEXT("Sphere"), Head + FVector(100, Side * 45.f, 20), FVector(22, 22, 14) / 100.f, Black);
        break;
    }
    }
}

// ------------------------------------------------------------------------------------------------ tick
void APYRMonPawn::Tick(float Dt)
{
    Super::Tick(Dt);
    // m4t: a paused battle holds its fighters exactly as they are (no steering, cooldowns, heal, brain or pose change).
    if (!bVisualOnly)
        if (const APYRBattleManager* Held = Manager())
            if (Held->IsBattleActive() && Held->IsBattlePaused())
            {
                PendingAxes = FVector2D::ZeroVector;
                return;
            }
    const double T = Now();
    BobClock += Dt;

    if (GrowSeconds > 0 && GrowAge < GrowSeconds)
    {
        GrowAge = FMath::Min(GrowSeconds, GrowAge + Dt);
        const float A = FMath::SmoothStep(0.f, 1.f, GrowAge / GrowSeconds);
        ApplyScale(FMath::Lerp(GrowFrom, GrowTarget, A));
    }

    // Pose: squash and tilt the visual root.
    if (Pose != EPYRPose::None && T > PoseUntil) Pose = EPYRPose::None;
    FVector PoseScale(1), PoseLoc(0);
    FRotator PoseRot(0);
    const float H = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    switch (Pose)
    {
    case EPYRPose::Crouch: PoseScale = FVector(1.15f, 1.15f, .7f); PoseLoc.Z = -H * .3f; break;
    case EPYRPose::Rear: PoseScale = FVector(1, 1, 1.12f); PoseRot.Pitch = 14.f; break;
    case EPYRPose::Tuck: PoseScale = FVector(.72f, .8f, .55f); PoseLoc.Z = -H * .45f; PoseRot.Roll = 360.f * FMath::Frac(float(T) * 2.f); break;
    case EPYRPose::Sit: PoseScale = FVector(.9f, 1, .8f); PoseLoc.Z = -H * .2f; break;
    case EPYRPose::Faint: PoseRot.Roll = 80.f; PoseLoc.Z = -H * .4f; break;
    case EPYRPose::Grounded: PoseRot.Roll = 55.f; PoseLoc.Z = -H * .2f; break;
    case EPYRPose::Harden: PoseScale = FVector(1.06f, 1.06f, .94f); break;
    case EPYRPose::Sleep: PoseScale = FVector(1.05f, 1.05f, .78f + .03f * FMath::Sin(float(T) * 3.f)); PoseLoc.Z = -H * .25f; PoseRot.Roll = 12.f; break;
    case EPYRPose::Sink: PoseLoc.Z = -H * 1.6f; PoseRot.Roll = 20.f * FMath::Sin(float(T) * 12.f); break;
    case EPYRPose::Flop: PoseRot.Roll = 35.f * FMath::Sin(float(T) * 20.f); PoseLoc.Z = FMath::Abs(FMath::Sin(float(T) * 20.f)) * H * .3f; break;
    default: break;
    }
    if (T < FlashUntil) PoseScale *= 1.12f;
    // Imported static meshes and primitives bob and tilt; rigged bodies animate themselves.
    if (!Rig && Pose == EPYRPose::None && Info)
    {
        const float Speed = GetVelocity().Size2D();
        if (Info->Body == EPYRBody::Flier) PoseLoc.Z += FMath::Sin(BobClock * 9.f) * 5.f;
        else if (Speed > 20.f) { PoseLoc.Z += FMath::Abs(FMath::Sin(BobClock * 14.f)) * 5.f; PoseRot.Pitch += FMath::Sin(BobClock * 14.f) * 4.f; }
        else PoseScale.Z *= 1.f + FMath::Sin(BobClock * 3.f) * .025f;
    }
    VisualRoot->SetRelativeLocationAndRotation(PoseLoc, PoseRot);
    VisualRoot->SetRelativeScale3D(PoseScale);
    for (int32 i = 0; i < Wings.Num(); ++i)
    {
        const float Side = Wings[i]->GetRelativeLocation().Y > 0 ? 1.f : -1.f;
        const float Flap = Pose == EPYRPose::Grounded || bFainted ? 5.f : FMath::Sin(BobClock * 22.f) * 40.f;
        Wings[i]->SetRelativeRotation(FRotator(0, 0, Side * (Flap - 10.f)));
    }
    if (Glow && Glow->IsVisible())
    {
        const float R = CapsuleRadius() * (2.2f + .5f * FMath::Sin(BobClock * 18.f)) / 100.f;
        Glow->SetWorldLocation(FVector(GetActorLocation().X, GetActorLocation().Y, FloorZ() + 2.f));
        Glow->SetWorldScale3D(FVector(R, R, .012f));
        const float K = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() * 2.3f / 100.f * (1.f + .06f * FMath::Sin(BobClock * 20.f));
        Aura->SetRelativeScale3D(FVector(K * .9f, K * .9f, K));
    }
    if (Shadow)
    {
        Shadow->SetWorldLocation(FVector(GetActorLocation().X, GetActorLocation().Y, FloorZ() + 1.5f));
        Shadow->SetVisibility(!bFainted && !IsHidden());
    }
    if (bVisualOnly || !Info) return;

    for (int32 i = 0; i < 4; ++i) Cooldown[i] = FMath::Max(0.f, Cooldown[i] - Dt);
    if (HealLeft > 0 && !bFainted)
    {
        const float Step = FMath::Min(HealLeft, HealRate * Dt);
        HealLeft -= Step;
        Hp = FMath::Min(MaxHp, Hp + Step);
    }
    UCharacterMovementComponent* Move = GetCharacterMovement();
    Move->MaxWalkSpeed = Move->MaxFlySpeed = Info->MoveSpeed() * SpeedFactor();
    if (BodyComp) BodyComp->TickBody(Dt);
    // A hop that lands more than a step above where it took off (a ledge top) goes back where it started.
    if (bHopping)
    {
        if (IsAirborne()) bHopAirborne = true;
        else if (bHopAirborne)
        {
            bHopping = bHopAirborne = false;
            if (BottomZ() > HopFloorZ + 45.f)  // Feet, not the trace under the centre: it may perch on a ledge lip.
            {
                SetActorLocation(HopFrom, false, nullptr, ETeleportType::TeleportPhysics);
                Move->StopMovementImmediately();
            }
        }
    }
    TickWater(Dt);
    TickStatusLook();
    // Stunned, asleep, bound or in the water: no steering (a hop already in the air keeps flying).
    if ((IsStunned() || bFainted || IsBiding()) && !(IsAirborne() && FellInUntil <= 0)) Move->Velocity.X = Move->Velocity.Y = 0;

    // Giants turn toward their foe at their own slow rate.
    if (Info->Body == EPYRBody::Giant && !bFainted)
        if (APYRMonPawn* Opp = Opponent())
        {
            const float Want = (Opp->GetActorLocation() - GetActorLocation()).Rotation().Yaw;
            const float Delta = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Want);
            const float Rate = Info->TurnRate() * SpeedFactor() * Dt;
            SetActorRotation(FRotator(0, GetActorRotation().Yaw + FMath::Clamp(Delta, -Rate, Rate), 0));
        }

    // Player steering: camera-relative, using the view target's yaw. WASD and the arrows are read here (UE 5.8 only
    // binds analog keys as axes); tests add to PendingAxes through AxisW/S/A/D.
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        auto Down = [PC](FKey A, FKey B) { return PC->IsInputKeyDown(A) || PC->IsInputKeyDown(B) ? 1.f : 0.f; };
        PendingAxes.Y += Down(EKeys::W, EKeys::Up) - Down(EKeys::S, EKeys::Down);
        PendingAxes.X += Down(EKeys::D, EKeys::Right) - Down(EKeys::A, EKeys::Left);
    }
    InputAxes = PendingAxes;
    PendingAxes = FVector2D::ZeroVector;
    APYRBattleManager* M = Manager();
    if (bPlayerSide && M && !M->IsWheelOpen() && !M->IsMenuOpen() && !InputAxes.IsNearlyZero())
    {
        float Yaw = 0;
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
            Yaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : PC->GetControlRotation().Yaw;
        const FRotator View(0, Yaw, 0);
        FVector Dir = View.RotateVector(FVector(InputAxes.Y, InputAxes.X, 0));
        if (T < ConfusedUntil) Dir = Dir.RotateAngleAxis(FMath::Sin(BobClock * 3.f) * 120.f, FVector::UpVector);  // Confused: it wobbles off course.
        Steer(Dir);
    }
    if (!bPlayerSide) TickBrain(Dt);
}

// ------------------------------------------------------------------------------------------------ water and status look
void APYRMonPawn::UpdateDivedLook()
{
    const bool bHide = bDived || Now() < BurrowUntil;
    if (bHide != bShownDived)
    {
        bShownDived = bHide;
        VisualRoot->SetHiddenInGame(bHide, true);
        if (Shadow) Shadow->SetHiddenInGame(bHide);
        if (bHide) SetGlow(false);
    }
    const APYRBattleManager* M = Manager();
    const bool bRipple = bDived && M && M->GetPool().IsActive();
    if (bRipple && !Ripple)
    {
        // Under the water only a ring of ripples and a dark shadow show where it is.
        const float R = FMath::Max(95.f, CapsuleRadius() * 3.f) / 100.f;
        WaterShadow = PYRFx::AddPrimitive(this, nullptr, TEXT("Cylinder"), FVector::ZeroVector, FVector(R * .9f, R * .9f, .01f), FLinearColor(.05f, .1f, .2f, .55f), FRotator::ZeroRotator, true);
        Ripple = PYRFx::AddPrimitive(this, nullptr, TEXT("Cylinder"), FVector::ZeroVector, FVector(R, R, .01f), FLinearColor(.95f, .98f, 1.f, .85f), FRotator::ZeroRotator, true);
        RippleInner = PYRFx::AddPrimitive(this, nullptr, TEXT("Cylinder"), FVector::ZeroVector, FVector(R * .8f, R * .8f, .01f), PYRFx::Palette(TEXT("4fa8e0")), FRotator::ZeroRotator, true);
        for (UStaticMeshComponent* C : {WaterShadow.Get(), Ripple.Get(), RippleInner.Get()})
        {
            C->SetUsingAbsoluteLocation(true);
            C->SetUsingAbsoluteRotation(true);
            C->SetUsingAbsoluteScale(true);
            C->CastShadow = false;
        }
    }
    if (Ripple)
    {
        Ripple->SetVisibility(bRipple);
        RippleInner->SetVisibility(bRipple);
        WaterShadow->SetVisibility(bRipple);
        if (bRipple)
        {
            const float Surface = M->GetPool().SurfaceZ();
            const FVector At = GetActorLocation();
            const float R = FMath::Max(95.f, CapsuleRadius() * 3.f) / 100.f * (1.f + .12f * FMath::Sin(BobClock * 7.f));
            Ripple->SetWorldLocation(FVector(At.X, At.Y, Surface + 1.5f));
            Ripple->SetWorldScale3D(FVector(R, R, .01f));
            RippleInner->SetWorldLocation(FVector(At.X, At.Y, Surface + 2.5f));
            RippleInner->SetWorldScale3D(FVector(R * .82f, R * .82f, .01f));
            WaterShadow->SetWorldLocation(FVector(At.X, At.Y, Surface + .8f));
            WaterShadow->SetWorldScale3D(FVector(R * .7f, R * .7f, .01f));
            // A moving swimmer trails spreading rings.
            if (GetVelocity().Size2D() > 60.f && Now() >= NextRippleFx)
            {
                NextRippleFx = Now() + .35;
                APYRMoveFx::Spawn(GetWorld(), EPYRFx::Ripple, FVector(At.X, At.Y, Surface + 1.f), FVector::ForwardVector, PYRFx::Palette(TEXT("cfeaff")), 120.f, .7f);
            }
        }
    }
}

void APYRMonPawn::TickWater(float Dt)
{
    UpdateDivedLook();
    APYRBattleManager* M = Manager();
    if (!M || !M->IsBattleActive() || bFainted) return;
    const FPYRPoolRules& Pool = M->GetPool();
    if (!Pool.IsActive()) return;
    const EPYRBody B = GetBody();
    if (B != EPYRBody::Runner && B != EPYRBody::Crawler) return;
    if (FellInUntil > 0)
    {
        if (Now() < FellInUntil) return;
        // Back up on the nearest platform with a splash.
        FellInUntil = 0;
        FVector Top;
        if (Pool.NearestPlatform(GetActorLocation(), Top))
        {
            SetActorLocation(Top + FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), false, nullptr, ETeleportType::TeleportPhysics);
            LastPopUpAt = GetActorLocation();
        }
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        SetPose(EPYRPose::None, 0);
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::Splash, FVector(GetActorLocation().X, GetActorLocation().Y, BottomZ()), FVector::ForwardVector, PYRFx::Palette(TEXT("6ec6f0")), 70.f, .5f);
        return;
    }
    if (BodyComp && BodyComp->IsDodging()) return;  // A dash across a gap is in the air.
    if (!Pool.IsStandingInWater(this)) return;
    // In the water: no damage, 1 s lost, then back onto a platform.
    FellInUntil = Now() + 1.0;
    StunnedUntil = FMath::Max(StunnedUntil, FellInUntil);
    InvulnerableUntil = FMath::Max(InvulnerableUntil, FellInUntil);
    ++WaterFalls;
    LastFallAt = GetActorLocation();
    if (BodyComp) BodyComp->DashLeft = 0;
    GetCharacterMovement()->StopMovementImmediately();
    SetPose(EPYRPose::Sink, 1.f);
    SetGlow(false);
    const FVector SplashAt(GetActorLocation().X, GetActorLocation().Y, Pool.SurfaceZ() + 2.f);
    APYRMoveFx::Spawn(GetWorld(), EPYRFx::Splash, SplashAt, FVector::ForwardVector, PYRFx::Palette(TEXT("6ec6f0")), 100.f, .6f);
    APYRMoveFx::Spawn(GetWorld(), EPYRFx::Ripple, SplashAt, FVector::ForwardVector, PYRFx::Palette(TEXT("cfeaff")), 180.f, .9f);
    if (bPlayerSide) M->SayRed(FString::Printf(TEXT("%s! Get out of the water!"), *APYRBattleManager::MonName(Species)), 1.4f);
}

void APYRMonPawn::TickStatusLook()
{
    const double T = Now();
    if (T < NextStatusFx || bFainted || bDived) return;
    const FVector Head = GetActorLocation() + FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    if (IsAsleep())
    {
        NextStatusFx = T + 1.1;
        if (Pose != EPYRPose::Sleep) SetPose(EPYRPose::Sleep, float(SleepUntil - T));
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::SleepZ, Head, FVector::ForwardVector, PYRFx::Palette(TEXT("cfe0ff")), 40.f, 1.1f);
    }
    else if (T < ConfusedUntil)
    {
        NextStatusFx = T + 1.0;
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::DizzyStars, Head - FVector(0, 0, 40), FVector::ForwardVector, PYRFx::Palette(TEXT("ffd733")), 60.f, 1.0f);
    }
    else if (T < TrappedUntil)
    {
        NextStatusFx = T + .5;
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::Coil, FVector(GetActorLocation().X, GetActorLocation().Y, BottomZ()), FVector::ForwardVector,
            TrappedBy.IsValid() && TrappedBy->Info ? TrappedBy->Info->Colour : FLinearColor(.6f, .5f, .7f), FMath::Max(90.f, CapsuleRadius() * 2.4f), .5f);
    }
    else if (Status == EPYRStatus::Burn)
    {
        NextStatusFx = T + 1.6;
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::Flame, Head - FVector(0, 0, 30), FVector::UpVector, PYRFx::Palette(TEXT("f08030")), 40.f, .5f);
    }
    else if (bSeeded)
    {
        NextStatusFx = T + 2.0;
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::Seeds, Head, FVector::ForwardVector, PYRFx::Palette(TEXT("5cb85c")), 40.f, .6f);
    }
}

// ------------------------------------------------------------------------------------------------ foe brain
void APYRMonPawn::TickSwimBrain(float Dt, APYRMonPawn* Opp)
{
    // Misty's swimmers: dive, slip under the water toward Red's Pokemon (only a ripple shows), surface beside it with a
    // 0.6 s splash tell and strike from below, then stay up and hittable for about 2 s before diving again.
    APYRBattleManager* M = Manager();
    const FPYRPoolRules& Pool = M->GetPool();
    FVector Goal;
    if (!Pool.NearestOpenWater(Opp->GetActorLocation(), Goal)) Goal = Opp->GetActorLocation();
    const FVector To = (Goal - GetActorLocation()) * FVector(1, 1, 0);
    const FVector Side = FVector::CrossProduct(FVector::UpVector, To.GetSafeNormal()) * StrafeSign;
    if (bDived) { if (To.Size() > 60.f) Steer(To.GetSafeNormal()); }
    else if (To.Size() > 300.f) Steer(To.GetSafeNormal() * .7f + Side * .3f);
    else Steer(Side * .5f);
    if (FMath::Frac(BobClock / 2.3f) < Dt / 2.3f) StrafeSign = -StrafeSign;
    const FVector ToOpp = (Opp->GetActorLocation() - GetActorLocation()) * FVector(1, 1, 0);
    if (!bDived && ToOpp.Size() > 30) SetActorRotation(FRotator(0, ToOpp.Rotation().Yaw, 0));
    if (!bDived && Now() >= SurfacedUntil && !IsAttacking() && FMath::FRand() < Dt * 2.f) { TryDive(); BrainClock = FMath::Max(BrainClock, 1.5f); }
    BrainClock -= Dt;
    if (BrainClock > 0 || IsAttacking()) return;
    const float Dist = ToOpp.Size();
    FName Best; float BestScore = 0;
    for (int32 i = 0; i < FMath::Min(4, Carried.Num()); ++i)
    {
        if (!IsMoveReady(i)) continue;
        const FPYRMoveInfo* Mv = PYRData::Move(Carried[i]);
        if (!Mv) continue;
        const float Reach = Mv->Shape == EPYRMoveShape::Dash ? Mv->Range + 60.f : Mv->Range;
        const bool bSelf = Mv->Shape == EPYRMoveShape::Self;
        if (bSelf && bDived) continue;  // Harden and Recover are used at the surface.
        if (!bSelf && Dist > Reach + Opp->CapsuleRadius()) continue;
        const float Factor = Opp->Info ? PYRData::Effectiveness(Mv->Type, *Opp->Info) : 1.f;
        float Score = 0;
        if (Mv->Power > 0) Score = Factor <= 0 ? 0 : 3.f + (Factor > 1 ? 1.f : 0.f) + Mv->Power / 100.f;
        else if (Mv->Effect == EPYREffect::FoeStat) Score = Opp->GetStage(Mv->Stat) > -2 ? 1.4f : .2f;
        else if (Mv->Effect == EPYREffect::SelfStat) Score = GetStage(Mv->Stat) < 2 ? 1.2f : .1f;
        else if (Mv->Effect == EPYREffect::Heal) Score = Hp < MaxHp * .5f ? 3.5f : 0.f;
        else Score = 1.5f;
        Score *= FMath::FRandRange(.8f, 1.2f);
        if (Score > BestScore) { BestScore = Score; Best = Mv->Id; }
    }
    if (!Best.IsNone() && M->FoeUseMove(this, Best)) BrainClock = FMath::FRandRange(1.4f, 2.4f);
    else BrainClock = .3f;
}

void APYRMonPawn::TickBrain(float Dt)
{
    APYRBattleManager* M = Manager();
    APYRMonPawn* Opp = Opponent();
    if (!M || !M->IsInBattle() || M->bFoePassive || !Opp || Opp->bFainted || bFainted || IsStunned() || M->IsBallBusy() || IsHidden()) return;
    if (Now() < BusyUntil || IsBiding() || (BodyComp && BodyComp->IsDodging())) return;
    if (IsSwimmer() && IsInWater() && M->GetPool().IsActive()) { TickSwimBrain(Dt, Opp); return; }
    // An opponent under the water or underground can't be targeted: circle and wait for it.
    if (Opp->IsUntargetable()) BrainClock = FMath::Max(BrainClock, .4f);
    const FVector To = (Opp->GetActorLocation() - GetActorLocation()) * FVector(1, 1, 0);
    const float Dist = To.Size();
    const FVector Dir = To.GetSafeNormal();
    const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir) * StrafeSign;
    if (FMath::Frac(BobClock / 2.7f) < Dt / 2.7f) StrafeSign = -StrafeSign;

    // How close it likes to be: the reach of its shortest damaging move.
    float Want = 900.f;
    for (const FName& Id : Carried)
        if (const FPYRMoveInfo* Mv = PYRData::Move(Id))
            if (Mv->Power > 0) Want = FMath::Min(Want, Mv->Shape == EPYRMoveShape::Dash ? Mv->Range * .8f : Mv->Range * .6f);
    switch (GetBody())
    {
    case EPYRBody::Flier:
        Want = FMath::Max(Want, 500.f);
        if (Dist < Want - 150.f) Steer(-Dir + Side * .5f);
        else if (Dist > Want + 250.f) Steer(Dir);
        else Steer(Side);
        break;
    case EPYRBody::Giant:
        if (Dist > 900) Steer(Dir * .6f);
        break;
    default:
        if (Dist > Want + 60.f) Steer(Dir + Side * .2f);
        else if (Dist < Want * .5f) Steer(-Dir);
        else Steer(Side * .6f);
        break;
    }
    if (GetBody() != EPYRBody::Giant && Dist > 30) SetActorRotation(FRotator(0, Dir.Rotation().Yaw, 0));

    // Sometimes read the player's attack and dodge it (wild foes follow the same body rules).
    if (IsDodgeReady())
        for (TActorIterator<APYRAttack> It(GetWorld()); It; ++It)
        {
            APYRAttack* A = *It;
            if (!A->IsLive() || !A->User.IsValid() || !A->User->bPlayerSide || A->bHit) continue;
            if (A->Phase == APYRAttack::EPhase::Active && A->Threatens(this) && FMath::FRand() < .02f) { Dodge(Side); break; }
        }

    BrainClock -= Dt;
    if (BrainClock > 0) return;
    FName Best; float BestScore = 0;
    const float Facing = FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Dir.Rotation().Yaw));
    for (int32 i = 0; i < FMath::Min(4, Carried.Num()); ++i)
    {
        if (!IsMoveReady(i)) continue;
        const FPYRMoveInfo* Mv = PYRData::Move(Carried[i]);
        if (!Mv) continue;
        float Score = 0;
        const float Reach = Mv->Shape == EPYRMoveShape::Dash ? Mv->Range + 60.f : Mv->Shape == EPYRMoveShape::Sweep ? 900.f : Mv->Range;
        const bool bInReach = Mv->Shape == EPYRMoveShape::Self || Mv->Shape == EPYRMoveShape::Bide || Dist <= Reach + Opp->CapsuleRadius();
        const float Factor = Opp->Info ? PYRData::Effectiveness(Mv->Type, *Opp->Info) : 1.f;
        if (!bInReach) Score = 0;
        else if (Mv->Effect == EPYREffect::Bide) Score = Hp < MaxHp ? 3.2f : 1.2f;
        else if (Mv->Effect == EPYREffect::Explode) Score = Hp < MaxHp * .3f && Factor > 0 ? 3.6f : 0.f;  // Selfdestruct as a last resort.
        else if (Mv->Power > 0) Score = Factor <= 0 ? 0 : 3.f + (Factor > 1 ? 1.f : 0.f);
        else if (Mv->Effect == EPYREffect::FoeStat) Score = Opp->GetStage(Mv->Stat) > -2 ? 1.6f : .2f;
        else if (Mv->Effect == EPYREffect::SelfStat) Score = GetStage(Mv->Stat) < 2 ? 1.3f : .1f;
        else if (Mv->Effect == EPYREffect::Paralyse || Mv->Effect == EPYREffect::Poison || Mv->Effect == EPYREffect::Sleep)
            Score = Opp->Status == EPYRStatus::None && Factor > 0 ? 2.2f : 0;
        else if (Mv->Effect == EPYREffect::Confuse) Score = Now() >= Opp->ConfusedUntil ? 2.f : 0.f;
        else if (Mv->Effect == EPYREffect::LeechSeed) Score = !Opp->bSeeded ? 2.f : 0.f;
        else if (Mv->Effect == EPYREffect::Disable) Score = Opp->DisabledSlot == INDEX_NONE || Now() >= Opp->DisabledUntil ? 1.5f : 0.f;
        else if (Mv->Effect == EPYREffect::Heal) Score = Hp < MaxHp * .5f ? 3.4f : 0.f;
        else if (Mv->Effect == EPYREffect::FocusEnergy) Score = bFocused ? 0.f : .8f;
        else if (Mv->Effect == EPYREffect::Mirror) Score = Opp->LastMove.IsNone() ? 0.f : 1.4f;
        else if (Mv->Effect == EPYREffect::Whirlwind) Score = .4f;
        else if (Mv->Effect == EPYREffect::Splash) Score = .5f;
        // Onix sweeps its tail when its foe is in front of it.
        if (GetBody() == EPYRBody::Giant && Mv->Id == PYRData::Tackle) Score = Facing < 70 && Dist < 950 ? 3.5f : 0;
        Score *= FMath::FRandRange(.8f, 1.2f);
        if (Score > BestScore) { BestScore = Score; Best = Mv->Id; }
    }
    if (!Best.IsNone() && M->FoeUseMove(this, Best))
        BrainClock = GetBody() == EPYRBody::Giant ? FMath::FRandRange(1.8f, 2.8f) : FMath::FRandRange(1.1f, 2.f);
    else
        BrainClock = .3f;
}

// ------------------------------------------------------------------------------------------------ input
void APYRMonPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindKey(EKeys::One, IE_Pressed, this, &APYRMonPawn::Key1);
    Input->BindKey(EKeys::Two, IE_Pressed, this, &APYRMonPawn::Key2);
    Input->BindKey(EKeys::Three, IE_Pressed, this, &APYRMonPawn::Key3);
    Input->BindKey(EKeys::Four, IE_Pressed, this, &APYRMonPawn::Key4);
    Input->BindKey(EKeys::Five, IE_Pressed, this, &APYRMonPawn::Key5);
    Input->BindKey(EKeys::Six, IE_Pressed, this, &APYRMonPawn::Key6);
    Input->BindKey(EKeys::SpaceBar, IE_Pressed, this, &APYRMonPawn::KeyDodge);
    Input->BindKey(EKeys::Tab, IE_Pressed, this, &APYRMonPawn::KeyWheelDown);
    Input->BindKey(EKeys::Tab, IE_Released, this, &APYRMonPawn::KeyWheelUp);
    Input->BindKey(EKeys::Q, IE_Pressed, this, &APYRMonPawn::KeyWheelDown);
    Input->BindKey(EKeys::Q, IE_Released, this, &APYRMonPawn::KeyWheelUp);
    Input->BindKey(EKeys::R, IE_Pressed, this, &APYRMonPawn::KeyPotion);
    Input->BindKey(EKeys::F, IE_Pressed, this, &APYRMonPawn::KeyAimDown);
    Input->BindKey(EKeys::F, IE_Released, this, &APYRMonPawn::KeyAimUp);
    Input->BindKey(EKeys::E, IE_Pressed, this, &APYRMonPawn::KeyHop);  // m3: hop between platforms, or dive/surface.
    // m4t: Run (R is the Potion): X, the pad's Y; the pad's left shoulder holds the switch menu like Q.
    Input->BindKey(EKeys::X, IE_Pressed, this, &APYRMonPawn::KeyRun);
    Input->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Pressed, this, &APYRMonPawn::KeyRun);
    Input->BindKey(EKeys::Gamepad_LeftShoulder, IE_Pressed, this, &APYRMonPawn::KeyWheelDown);
    Input->BindKey(EKeys::Gamepad_LeftShoulder, IE_Released, this, &APYRMonPawn::KeyWheelUp);
}

void APYRMonPawn::AxisW(float V) { PendingAxes.Y += V; }
void APYRMonPawn::AxisS(float V) { PendingAxes.Y -= V; }
void APYRMonPawn::AxisA(float V) { PendingAxes.X -= V; }
void APYRMonPawn::AxisD(float V) { PendingAxes.X += V; }
void APYRMonPawn::NumberKey(int32 Index)
{
    APYRBattleManager* M = Manager();
    if (!M) return;
    // m4t: the learn-move prompt never hears a move key (the battle is paused under it; PressMove refuses too).
    if (M->IsMenuOpen()) return;
    else if (M->IsWheelOpen()) M->PressSwap(Index);
    else if (Index < 4) M->PressMove(Index);
}
void APYRMonPawn::Key1() { NumberKey(0); }
void APYRMonPawn::Key2() { NumberKey(1); }
void APYRMonPawn::Key3() { NumberKey(2); }
void APYRMonPawn::Key4() { NumberKey(3); }
void APYRMonPawn::Key5() { NumberKey(4); }
void APYRMonPawn::Key6() { NumberKey(5); }
void APYRMonPawn::KeyDodge() { if (APYRBattleManager* M = Manager()) M->PressDodge(); }
void APYRMonPawn::KeyWheelDown() { if (APYRBattleManager* M = Manager()) M->OpenWheel(true); }
void APYRMonPawn::KeyWheelUp()
{
    APYRBattleManager* M = Manager();
    if (!M) return;
    // The battle is paused under the menu, so the direction is read from the keys held right now (and the tests' axes).
    FVector2D Dir = PendingAxes + InputAxes;
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        auto Down = [PC](FKey A, FKey B, FKey C) { return PC->IsInputKeyDown(A) || PC->IsInputKeyDown(B) || PC->IsInputKeyDown(C) ? 1.f : 0.f; };
        Dir.Y += Down(EKeys::W, EKeys::Up, EKeys::Gamepad_DPad_Up) - Down(EKeys::S, EKeys::Down, EKeys::Gamepad_DPad_Down);
        Dir.X += Down(EKeys::D, EKeys::Right, EKeys::Gamepad_DPad_Right) - Down(EKeys::A, EKeys::Left, EKeys::Gamepad_DPad_Left);
    }
    M->CloseWheel(Dir.GetClampedToMaxSize(1.f));
}
void APYRMonPawn::KeyRun() { if (APYRBattleManager* M = Manager()) M->PressRun(); }
void APYRMonPawn::KeyPotion() { if (APYRBattleManager* M = Manager()) M->PressPotion(); }
void APYRMonPawn::KeyAimDown() { if (APYRBattleManager* M = Manager()) M->BeginAim(); }
void APYRMonPawn::KeyAimUp() { if (APYRBattleManager* M = Manager()) M->ReleaseAim(); }
void APYRMonPawn::KeyHop() { if (APYRBattleManager* M = Manager()) M->PressHop(); }
