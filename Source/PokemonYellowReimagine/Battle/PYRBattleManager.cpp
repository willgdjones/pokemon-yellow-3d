#include "PYRBattleManager.h"

#include "PYRAttack.h"
#include "PYRBattleFx.h"
#include "PYRBattleHUD.h"
#include "PYRMonPawn.h"
#include "Anim/PYRMonRigAnimatorComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "../GameTest.h"
#include "../Party/PYRSpecies.h"
#include "../Party/PYRTrainerState.h"
#include "../Partner/PYRBond.h"
#include "../Kanto/PYRProgress.h"
#include "../Menu/PYRPlayerController.h"
#include "../Menus/PYRMenus.h"
#include "../Menus/PYRMoveLearning.h"
#include "../Menus/PYRSysText.h"

DEFINE_LOG_CATEGORY_STATIC(LogPYRBattle, Log, All);

using namespace PYRData;

namespace
{
// Gen 1 friendship steps: bigger while friendship is low.
int32 FriendStep(int32 Friendship, int32 Low, int32 Mid, int32 High)
{
    return Friendship < 100 ? Low : Friendship < 200 ? Mid : High;
}
}

APYRBattleManager::APYRBattleManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

APYRBattleManager* APYRBattleManager::Find(const UObject* Ctx)
{
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    if (!World) return nullptr;
    for (TActorIterator<APYRBattleManager> It(World); It; ++It) if (IsValid(*It)) return *It;
    return nullptr;
}

APYRBattleManager* APYRBattleManager::Get(const UObject* Ctx)
{
    if (APYRBattleManager* Found = Find(Ctx)) return Found;
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    if (!World) return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<APYRBattleManager>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}


double APYRBattleManager::Now() const
{
    const UWorld* World = GetWorld();
    if (!World) return 0.0;
    // The battle clock: world time less every second it was held (and the hold now running).
    const double T = World->GetTimeSeconds();
    return T - PausedTotal - (PausedSince >= 0 ? T - PausedSince : 0.0);
}

double APYRBattleManager::PausedSeconds() const
{
    const UWorld* World = GetWorld();
    return PausedTotal + (World && PausedSince >= 0 ? World->GetTimeSeconds() - PausedSince : 0.0);
}

double APYRBattleManager::BattleNow(const UObject* Ctx)
{
    static TWeakObjectPtr<APYRBattleManager> Cached;
    const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    if (!World) return 0.0;
    const APYRBattleManager* M = Cached.Get();
    if (!M || M->GetWorld() != World)
    {
        APYRBattleManager* Found = Find(Ctx);
        Cached = Found;
        M = Found;
    }
    return M ? M->Now() : World->GetTimeSeconds();
}

void APYRBattleManager::SetBattlePaused(bool bPause, FName Reason)
{
    UWorld* World = GetWorld();
    if (!World) return;
    const bool bWas = IsBattlePaused();
    if (bPause) PauseReasons.Add(Reason);
    else if (PauseReasons.RemoveSingle(Reason) == 0) return;
    const bool bIs = IsBattlePaused();
    if (bIs == bWas) return;
    if (bIs)
    {
        PausedSince = World->GetTimeSeconds();
        bAiming = false;
        FreezeActors(true);
    }
    else
    {
        PausedTotal += FMath::Max(0.0, World->GetTimeSeconds() - PausedSince);
        PausedSince = -1;
        FreezeActors(false);
    }
    UE_LOG(LogPYRBattle, Display, TEXT("PYR_BATTLE_PAUSE %s %s (held %.2f s in all)"), bIs ? TEXT("on") : TEXT("off"), *Reason.ToString(), PausedSeconds());
}

void APYRBattleManager::FreezeActors(bool bFreeze)
{
    // Custom time dilation 0: the fighters' movement, the attacks' flight and the effects' ages stand still; each battle
    // tick also returns early while paused. The camera, Red and the HUD keep their own time.
    // Life spans run on world timers, which time dilation doesn't slow: they are held and given back too.
    if (!bFreeze)
    {
        for (int32 I = 0; I < Frozen.Num(); ++I)
            if (AActor* A = Frozen[I].Get())
            {
                A->CustomTimeDilation = 1.f;
                if (FrozenLife[I] > 0.f) A->SetLifeSpan(FrozenLife[I]);
            }
        Frozen.Reset();
        FrozenLife.Reset();
        return;
    }
    UWorld* World = GetWorld();
    auto Hold = [this](AActor* A)
    {
        if (!IsValid(A) || A->CustomTimeDilation == 0.f) return;
        A->CustomTimeDilation = 0.f;
        Frozen.Add(A);
        const float Life = A->GetLifeSpan();
        FrozenLife.Add(Life);
        if (Life > 0.f) A->SetLifeSpan(0.f);
    };
    for (TActorIterator<APYRMonPawn> It(World); It; ++It) Hold(*It);
    for (TActorIterator<APYRAttack> It(World); It; ++It) Hold(*It);
    for (TActorIterator<APYRMoveFx> It(World); It; ++It) Hold(*It);
    for (TActorIterator<APYRFxBurst> It(World); It; ++It) Hold(*It);
    Hold(BallActor);
}

APlayerController* APYRBattleManager::PC() const { return GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr; }

FString APYRBattleManager::MonName(FName Species)
{
    const FPYRSpeciesInfo* S = PYRData::Species(Species);
    return S ? S->Name.ToString() : Species.ToString();
}

FString APYRBattleManager::TrainerTitle() const
{
    // Yellow's own name for the trainer when the fight has one ("BUG CATCHER"); else "brock" -> "Brock",
    // "bug-catcher-2" -> "Bug Catcher".
    if (!Setup.TrainerTitle.IsEmpty()) return Setup.TrainerTitle;
    FString Id = Setup.TrainerId.IsNone() ? FString(TEXT("The trainer")) : Setup.TrainerId.ToString();
    Id = Id.Replace(TEXT("-"), TEXT(" ")).Replace(TEXT("_"), TEXT(" ")).Replace(TEXT("."), TEXT(" "));
    TArray<FString> Words;
    Id.ParseIntoArrayWS(Words);
    while (Words.Num() > 1 && Words.Last().IsNumeric()) Words.Pop();
    for (FString& W : Words) if (W.Len() > 0) W[0] = FChar::ToUpper(W[0]);
    return FString::Join(Words, TEXT(" "));
}

bool APYRBattleManager::GroundZ(UWorld* World, FVector At, float& OutZ, const AActor* Ignore)
{
    if (!World) return false;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PYRGround), false, Ignore);
    FHitResult Hit;
    // Skip overlap-only volumes (area trigger boxes): object-type traces hit them too.
    for (int32 Try = 0; Try < 8; ++Try)
    {
        if (!World->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 20), At - FVector(0, 0, 20000), Objects, Query)) return false;
        const UPrimitiveComponent* Comp = Hit.GetComponent();
        if (!Comp || Comp->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
        {
            OutZ = Hit.ImpactPoint.Z;
            return true;
        }
        Query.AddIgnoredComponent(Comp);
    }
    return false;
}

ACharacter* APYRBattleManager::SpawnMonVisual(UWorld* World, FName Species, const FTransform& Transform)
{
    const FPYRSpeciesInfo* Info = PYRData::Species(Species);
    if (!World || !Info) return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    // The transform is the ground point the body stands on.
    const FVector At = Transform.GetLocation() + FVector(0, 0, Info->CapsuleHalfHeight() + 2.f);
    APYRMonPawn* Mon = World->SpawnActor<APYRMonPawn>(At, FRotator(0, Transform.Rotator().Yaw, 0), Params);
    if (!Mon) return nullptr;
    Mon->InitVisualOnly(Species);
    Mon->Tags.AddUnique(TEXT("MonVisual"));
    return Mon;
}

// ------------------------------------------------------------------------------------------------ start
FVector APYRBattleManager::RedSpot() const
{
    const ACharacter* R = Red.Get();
    FVector To = R ? (R->GetActorLocation() - Setup.Center) * FVector(1, 1, 0) : FVector(-1, 0, 0);
    const float D = To.Size();
    if (To.IsNearlyZero()) To = FVector(-1, 0, 0);
    // Red keeps his spot when he already stands at the ring's edge; otherwise he steps to the nearest edge point.
    if (R && D >= Setup.Radius - 300.f && D <= Setup.Radius + 100.f) return R->GetActorLocation();
    return Setup.Center + To.GetSafeNormal() * FMath::Max(200.f, Setup.Radius - 150.f);
}

FVector APYRBattleManager::MonSpot(bool bPlayerSide) const
{
    FVector Dir = (RedEdge - Setup.Center) * FVector(1, 1, 0);
    Dir = Dir.IsNearlyZero() ? FVector(-1, 0, 0) : Dir.GetSafeNormal();
    return Setup.Center + Dir * Setup.Radius * (bPlayerSide ? .38f : -.38f);
}

void APYRBattleManager::Begin()
{
    UWorld* World = GetWorld();
    bInBattle = true;
    bHasResult = false;
    EndAt = FoeNextAt = -1;
    FoeIndex = 0;
    Fought.Reset(); FoughtAll.Reset();
    bAutoDodgeUsed = false;
    AutoDodges = 0;
    // A hold left from an earlier fight (it ended under a menu) lets go first.
    while (PauseReasons.Num()) SetBattlePaused(false, PauseReasons.Last());
    SwapReadyAt = PotionReadyAt = 0;
    bWheelOpen = bWheelForced = bWheelHeld = bAiming = false;
    RunPressedAt = -100;
    BallPhase = EBall::None;
    bBallDemo = false;
    DemoStep = 0;
    DemoAt = Now();
    RunInUntil = 0;
    ActivePartyIndex = INDEX_NONE;
    TellLog.Reset();
    Shout.Reset(); Banner.Reset();
    PayDayMoney = 0;
    // m3: the cave ceiling (from the floor at the centre) and the pool rules.
    CenterFloorZ = Setup.Center.Z;
    CeilingZ = 1e9f;
    const float StartProbe = Setup.MaxHoverHeightCm > 0.f ? FMath::Clamp(Setup.MaxHoverHeightCm * .4f, 40.f, 400.f) : 400.f;
    GroundZ(World, Setup.Center + FVector(0, 0, StartProbe), CenterFloorZ);
    if (Setup.MaxHoverHeightCm > 0.f) CeilingZ = CenterFloorZ + Setup.MaxHoverHeightCm;
    if (Pool.Setup(World, Setup.Center, Setup.Radius))
        UE_LOG(LogPYRBattle, Display, TEXT("PYR_POOL water=%d platforms=%d surface=%.0f"), Pool.Water.Num(), Pool.Platforms.Num(), Pool.SurfaceZ());

    APlayerController* Controller = PC();
    ACharacter* RedPawn = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
    if (Cast<APYRMonPawn>(RedPawn)) RedPawn = Red.Get();  // Already fighting: keep the Red we know.
    Red = RedPawn;
    for (TActorIterator<AActor> It(World); It; ++It)
        if (It->ActorHasTag(TEXT("PikachuFollower")))
        {
            Follower = *It;
            LastPikachuPos = It->GetActorLocation();
            It->SetActorHiddenInGame(true);
            It->SetActorEnableCollision(false);
            break;
        }
    RedEdge = RedSpot();
    // A pool longer than the ring (Cerulean Gym): Red steps back out of the water onto the walkway.
    if (Pool.IsActive() && Pool.InFootprint(RedEdge))
    {
        const FVector Out = ((RedEdge - Setup.Center) * FVector(1, 1, 0)).GetSafeNormal();
        for (int32 Step = 0; Step < 40 && !Out.IsNearlyZero() && Pool.InFootprint(RedEdge); ++Step) RedEdge += Out * 50.f;
        RedEdge += Out * 120.f;
    }
    if (RedPawn)
    {
        float Floor = RedEdge.Z;
        if (GroundZ(World, RedEdge + FVector(0, 0, Probe()), Floor, RedPawn))
            RedEdge.Z = Floor + RedPawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
        const FRotator Face(0, (Setup.Center - RedEdge).Rotation().Yaw, 0);
        RedPawn->GetCharacterMovement()->StopMovementImmediately();
        RedPawn->SetActorLocationAndRotation(RedEdge, Face, false, nullptr, ETeleportType::TeleportPhysics);
    }
    if (Controller) Controller->bAutoManageActiveCameraTarget = false;
    ShowHud(true);
}

APYRMonPawn* APYRBattleManager::SpawnFighter(const FPYRMon& Mon, bool bPlayerSide, int32 PartyIndex, FVector At, FVector Face)
{
    const FPYRSpeciesInfo* Info = PYRData::Species(Mon.Species);
    if (!Info) return nullptr;
    UWorld* World = GetWorld();
    At = StandingSpot(Mon.Species, At);
    float Floor = At.Z;
    GroundZ(World, At + FVector(0, 0, Probe()), Floor);
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    APYRMonPawn* Fighter = World->SpawnActor<APYRMonPawn>(FVector(At.X, At.Y, Floor + Info->CapsuleHalfHeight() + 2.f), FRotator(0, (Face - At).Rotation().Yaw, 0), Params);
    if (!Fighter) return nullptr;
    Fighter->InitMon(Mon.Species, Mon.Level, Mon.Hp, Mon.MaxHp, Mon.Carried, bPlayerSide, PartyIndex, Mon.Status);
    // A Pokemon sent out asleep sleeps on for a few seconds (Gen 1 keeps the sleep counter).
    if (Mon.Status == EPYRStatus::Sleep) Fighter->FallAsleep(3.f);
    // Starts at field size and grows to its battle display height over a second.
    Fighter->GrowTo(Fighter->FieldScale(), 0.f);
    Fighter->GrowTo(1.f, 1.f);
    return Fighter;
}

void APYRBattleManager::StartBattle(const FPYRBattleSetup& InSetup)
{
    if (bInBattle) { UE_LOG(LogPYRBattle, Warning, TEXT("StartBattle while already in battle")); return; }
    if (InSetup.Foes.Num() == 0 || !PYRData::Species(InSetup.Foes[0].Species)) { UE_LOG(LogPYRBattle, Warning, TEXT("StartBattle without a known foe")); return; }
    Setup = InSetup;
    if (Setup.Radius <= 100.f) Setup.Radius = 800.f;
    FoeTeam = Setup.Foes;
    TrainerName = Setup.TrainerId;
    Begin();
    UE_LOG(LogPYRBattle, Display, TEXT("PYR_BATTLE_START kind=%d foes=%d trainer=%s center=%s radius=%.0f"),
        int32(Setup.Kind), FoeTeam.Num(), *Setup.TrainerId.ToString(), *Setup.Center.ToCompactString(), Setup.Radius);
    SendOutFoe(0, MonSpot(false));
    if (Setup.Kind == EPYRBattleKind::Demo) return;  // The old man's lesson: no fighter on Red's side.
    if (Setup.bFoeStunned && Foe.IsValid())
    {
        Foe->StunnedUntil = Now() + 2.0;
        Foe->SetPose(EPYRPose::Sit, 2.f);
    }
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    const int32 First = State ? State->FirstAble() : INDEX_NONE;
    if (First == INDEX_NONE) { Decide(EPYRBattleOutcome::Lost, 0.f); return; }
    SendOutPlayer(First, MonSpot(true), false);
}

void APYRBattleManager::StartWild(const FPYRArena& Arena, const FPYRFoe& InFoe, FVector FoeStart, bool bFoeStunned2s)
{
    FPYRBattleSetup S;
    S.Kind = EPYRBattleKind::Wild;
    FPYRMonSpec Spec; Spec.Species = InFoe.Species; Spec.Level = InFoe.Level;
    S.Foes = {Spec};
    S.Center = Arena.Center;
    S.Radius = Arena.Radius;
    S.bFoeStunned = bFoeStunned2s;
    StartBattle(S);
    if (Foe.IsValid() && !FoeStart.IsZero())
    {
        float Floor = FoeStart.Z;
        GroundZ(GetWorld(), FoeStart + FVector(0, 0, 400), Floor, Foe.Get());
        Foe->SetActorLocation(FVector(FoeStart.X, FoeStart.Y, Floor + Foe->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
    }
}

void APYRBattleManager::StartTrainer(const FPYRArena& Arena, const TArray<FPYRFoe>& Foes, FName InTrainerId, int32 InPrize)
{
    FPYRBattleSetup S;
    S.Kind = Arena.bGym ? EPYRBattleKind::Gym : EPYRBattleKind::Trainer;
    for (const FPYRFoe& F : Foes) { FPYRMonSpec Spec; Spec.Species = F.Species; Spec.Level = F.Level; S.Foes.Add(Spec); }
    S.TrainerId = InTrainerId;
    S.Prize = InPrize;
    S.Center = Arena.Center;
    S.Radius = Arena.Radius;
    StartBattle(S);
}

void APYRBattleManager::SendOutFoe(int32 Index, FVector At)
{
    if (!FoeTeam.IsValidIndex(Index)) return;
    FoeIndex = Index;
    const FPYRMonSpec& F = FoeTeam[Index];
    const FPYRMon Fresh = UPYRTrainerState::MakeMon(F);
    const FVector Face = Player.IsValid() ? Player->GetActorLocation() : RedEdge;
    Foe = SpawnFighter(Fresh, false, INDEX_NONE, At, Face);
    if (Foe.IsValid()) Foe->BrainClock = 1.4f;
    if (UPYRTrainerState* State = UPYRTrainerState::Get(this)) State->MarkSeen(F.Species);
    Fought.Reset();
    if (ActivePartyIndex != INDEX_NONE && Player.IsValid() && !Player->bFainted) Fought.Add(ActivePartyIndex);
    if (IsTrainerKind())
    {
        ++BallEffectsSpawned;
        APYRFxBurst::Spawn(GetWorld(), At + FVector(0, 0, 60), FLinearColor(1, .15f, .1f), 14, 14, 320, .4f, TEXT("Sphere"), 0);
        Caption(FString::Printf(TEXT("%s sent out %s!"), *TrainerTitle(), *MonName(F.Species)));
    }
    else Caption(FString::Printf(TEXT("Wild %s appeared!"), *MonName(F.Species)));
}

void APYRBattleManager::SendOutPlayer(int32 Index, FVector At, bool bFreeDodge)
{
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    if (!State || !State->Party.IsValidIndex(Index)) return;
    const FPYRMon& Mon = State->Party[Index];
    const FVector Face = Foe.IsValid() ? Foe->GetActorLocation() : Setup.Center;
    FVector SpawnAt = At;
    const bool bPikachu = Mon.Species == PYRData::Pikachu;
    if (bPikachu)
    {
        // Pikachu never uses a ball: it runs in from Red's side. In the pool it hops straight onto its platform
        // (running in from the walkway would cross the water).
        if (PikachuSide.IsValid()) { if (!Pool.IsActive()) SpawnAt = PikachuSide->GetActorLocation(); PikachuSide->Destroy(); PikachuSide = nullptr; }
        else if (Red.IsValid() && !Pool.IsActive()) SpawnAt = Red->GetActorLocation() + Red->GetActorRightVector() * 110.f;
        if (!Pool.IsActive())
        {
            RunInTo = StandingSpot(Mon.Species, At);
            RunInUntil = Now() + 1.2;
        }
    }
    APYRMonPawn* Fighter = SpawnFighter(Mon, true, Index, SpawnAt, Face);
    if (!Fighter) return;
    Player = Fighter;
    ActivePartyIndex = Index;
    Fought.Add(Index);
    FoughtAll.Add(Index);
    if (!bPikachu)
    {
        ++BallEffectsSpawned;  // The ball pops open where it lands.
        APYRFxBurst::Spawn(GetWorld(), SpawnAt + FVector(0, 0, 60), FLinearColor(1, .15f, .1f), 14, 14, 320, .4f, TEXT("Sphere"), 0);
    }
    // The incoming Pokemon can't be touched for its first half second.
    if (bFreeDodge) Fighter->InvulnerableUntil = Now() + .5;
    if (APlayerController* Controller = PC())
    {
        Controller->bAutoManageActiveCameraTarget = false;
        Controller->Possess(Fighter);
    }
    SayRed(FString::Printf(TEXT("Go! %s!"), *MonName(Mon.Species)));
}

// ------------------------------------------------------------------------------------------------ player handlers
float APYRBattleManager::GetCooldownFraction(int32 Slot) const
{
    const APYRMonPawn* P = Player.Get();
    return P ? P->GetCooldownFraction(Slot) : 0.f;
}

float APYRBattleManager::SwapSecondsLeft() const { return FMath::Max(0.f, float(SwapReadyAt - Now())); }
float APYRBattleManager::PotionSecondsLeft() const { return FMath::Max(0.f, float(PotionReadyAt - Now())); }

void APYRBattleManager::PressMove(int32 Slot)
{
    APYRMonPawn* P = Player.Get();
    if (!bInBattle || EndAt >= 0 || !P || P->bFainted || bWheelOpen || IsBattlePaused()) return;
    if (P->IsAsleep())
    {
        // Gen 1: a sleeping Pokemon can't act; say so (at most once a second).
        if (Now() - CaptionAt > 1.0 || !CaptionText.Contains(TEXT("asleep"))) Caption(FString::Printf(TEXT("%s is fast asleep!"), *MonName(P->Species)));
        return;
    }
    if (P->IsSlotDisabled(Slot) && P->Carried.IsValidIndex(Slot))
    {
        if (const FPYRMoveInfo* Info = PYRData::Move(P->Carried[Slot]))
            if (!CaptionText.Contains(TEXT("is disabled")) || Now() - CaptionAt > 1.0)
                Caption(FString::Printf(TEXT("%s's %s is disabled!"), *MonName(P->Species), *Info->Name.ToString()));
        return;
    }
    if (P->IsStunned() || P->IsBiding()) return;
    if (!P->IsMoveReady(Slot) || Now() < P->BusyUntil) return;
    FireMove(P, Slot);
}

void APYRBattleManager::HurtInConfusion(APYRMonPawn* P)
{
    // Gen 1: a typeless 40-power physical hit on itself.
    Caption(TEXT("It hurt itself in its confusion!"));
    const float Hurt = PYRData::Damage(P->Level, P->EffectiveStat(EPYRStat::Attack), P->EffectiveStat(EPYRStat::Defence), 40, 1, 1);
    P->Hp = FMath::Max(0.f, P->Hp - Hurt);
    P->FlashHit();
    APYRMoveFx::Spawn(GetWorld(), EPYRFx::Impact, P->GetActorLocation(), FVector::ForwardVector, FLinearColor(1, .9f, .6f), 90.f, .25f);
    if (P->Hp <= 0) Faint(P);
}

bool APYRBattleManager::PreMove(APYRMonPawn* P, int32& Slot, bool bPlayer)
{
    const FString Name = MonName(P->Species);
    // A lost turn still runs the pressed move's ring, for 1 s only.
    auto LoseTurn = [P, &Slot]()
    {
        P->CooldownTotal[Slot] = P->Cooldown[Slot] = 1.f;
        return false;
    };
    // Yellow's order: confusion, then full paralysis, then (Red's traded Pokemon) obedience.
    if (P->Now() < P->ConfusedUntil)
    {
        Caption(FString::Printf(TEXT("%s is confused!"), *Name));
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::DizzyStars, P->GetActorLocation(), FVector::ForwardVector, PYRFx::Palette(TEXT("ffd733")), 60.f, .8f);
        if (!bNoRandom && FMath::FRand() < .5f) { HurtInConfusion(P); return LoseTurn(); }
    }
    if (P->Status == EPYRStatus::Paralysis && !bNoRandom && FMath::FRand() < .25f)
    {
        Caption(FString::Printf(TEXT("%s's fully paralyzed!"), *Name));
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::CheekSpark, P->GetActorLocation(), FVector::ForwardVector, FLinearColor(1, .9f, .2f), 40.f, .4f);
        return LoseTurn();
    }
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    if (!bPlayer || !State || !State->Party.IsValidIndex(P->PartyIndex) || !State->Party[P->PartyIndex].IsTraded()) return true;
    TArray<int32> Others;
    for (int32 i = 0; i < FMath::Min(4, P->Carried.Num()); ++i) if (i != Slot && P->IsMoveReady(i)) Others.Add(i);
    ++ObeyChecks;
    LastObey = PYRObedience::Roll(P->Level, State->ObedienceLevel(), Others.Num() > 0);
    if (LastObey == EPYRObey::Obeys) return true;
    ++ObeyIgnored;
    const int32 Pressed = Slot;
    LoseTurn();
    if (LastObey == EPYRObey::OtherMove)
    {
        // It does something else: one of its other ready moves.
        Slot = Others[FMath::RandRange(0, Others.Num() - 1)];
        SayRed(FString::Printf(TEXT("%s, not that one!"), *Name), 1.4f);
        return Pressed != Slot;
    }
    Caption(PYRObedience::Line(LastObey, Name));
    APYRMoveFx::Spawn(GetWorld(), EPYRFx::Glint, P->GetActorLocation() + FVector(0, 0, P->GetBattleScreenHeightM() * 50.f), FVector::ForwardVector, PYRFx::Palette(TEXT("c0c0c8")), 80.f, .5f);
    if (LastObey == EPYRObey::Naps)
    {
        // Gen 1: 1-7 turns of sleep.
        P->FallAsleep(PYRObedience::NapSeconds(FMath::RandRange(1, 7)));
        SyncParty();
    }
    else if (LastObey == EPYRObey::HurtsItself) HurtInConfusion(P);
    else P->SetPose(EPYRPose::Sit, .6f);  // Loafing, turning away, ignoring orders.
    return false;
}

APYRAttack* APYRBattleManager::LaunchMove(APYRMonPawn* User, const FPYRMoveInfo& InMove, bool bTell)
{
    const FPYRMoveInfo* Move = &InMove;
    // Mirror Move: the foe's last move, used with this user's stats (it fails when there's nothing to copy).
    if (InMove.Effect == EPYREffect::Mirror)
        if (const APYRMonPawn* Opp = User->Opponent())
            if (const FPYRMoveInfo* Copy = PYRData::Move(Opp->LastMove))
                if (Copy->Effect != EPYREffect::Mirror) Move = Copy;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APYRAttack* A = GetWorld()->SpawnActor<APYRAttack>(User->GetActorLocation(), FRotator::ZeroRotator, Params);
    if (!A) return nullptr;
    RegisterAttack(A);
    A->Init(User, *Move, bTell);
    if (Move->Effect == EPYREffect::Bide) Caption(FString::Printf(TEXT("%s\nis saving energy!"), *MonName(User->Species)));
    if (Move->Shape == EPYRMoveShape::Burrow) Caption(FString::Printf(TEXT("%s dug a hole!"), *MonName(User->Species)));
    return A;
}

void APYRBattleManager::FireMove(APYRMonPawn* P, int32 Slot)
{
    if (!PYRData::Move(P->Carried[Slot])) return;
    P->LastMove = P->Carried[Slot];
    P->LastMoveTime = Now();
    if (!PreMove(P, Slot, true)) return;
    const FPYRMoveInfo* Move = PYRData::Move(P->Carried[Slot]);
    if (!Move) return;
    P->CooldownTotal[Slot] = P->MoveCooldown(*Move);
    P->Cooldown[Slot] = P->CooldownTotal[Slot];
    P->LastMove = Move->Id;
    LaunchMove(P, *Move, false);
}

bool APYRBattleManager::FoeUseMove(APYRMonPawn* User, FName MoveId)
{
    if (!bInBattle || IsBattlePaused() || !User || User->bFainted || User->IsStunned() || User->IsBiding()) return false;
    int32 Slot = User->SlotOf(MoveId);
    const FPYRMoveInfo* Move = PYRData::Move(MoveId);
    if (Slot == INDEX_NONE || !Move || !User->IsMoveReady(Slot)) return false;
    User->CooldownTotal[Slot] = User->Cooldown[Slot] = Move->Cooldown;
    User->LastMove = MoveId;
    User->LastMoveTime = Now();
    if (!PreMove(User, Slot, false)) return true;  // The turn is gone, but the brain moved on.
    return LaunchMove(User, *Move, true) != nullptr;
}

void APYRBattleManager::PressHop()
{
    APYRMonPawn* P = Player.Get();
    if (!bInBattle || EndAt >= 0 || !P || P->bFainted || bWheelOpen || IsBattlePaused()) return;
    if (P->IsSwimmer())
    {
        if (P->bDived) P->SurfaceNow(false);
        else P->TryDive();
        return;
    }
    P->Hop();
}

void APYRBattleManager::PressDodge()
{
    APYRMonPawn* P = Player.Get();
    if (!bInBattle || !P || bWheelOpen || IsBattlePaused()) return;
    FVector Dir = P->LastSteer;
    if (Dir.IsNearlyZero() && Foe.IsValid())
        Dir = FVector::CrossProduct(FVector::UpVector, (Foe->GetActorLocation() - P->GetActorLocation()).GetSafeNormal2D());
    P->Dodge(Dir);
}

// ------------------------------------------------------------------------------------------------ m4t switch menu
void APYRBattleManager::OpenWheel(bool bHeld)
{
    if (!bInBattle || EndAt >= 0 || Setup.Kind == EPYRBattleKind::Demo || bWheelOpen || IsPausedFor(PauseLearn())) return;
    bWheelOpen = true;
    bWheelHeld = bHeld;
    WheelCursor = FMath::Max(0, ActivePartyIndex);
    MoveWheelCursor(1);
    SetBattlePaused(true, PauseSwitch());
}

void APYRBattleManager::ShutWheel()
{
    if (!bWheelOpen) return;
    bWheelOpen = bWheelForced = bWheelHeld = false;
    SetBattlePaused(false, PauseSwitch());
}

void APYRBattleManager::CloseWheel(FVector2D Direction)
{
    if (!bWheelOpen) return;
    bWheelHeld = false;
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    if (State && Direction.Size() > .5f && State->Party.Num() > 0)
    {
        // Slots sit clockwise from the top.
        const float Angle = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Direction.X, Direction.Y)) + 360.f, 360.f);
        const float Step = 360.f / State->Party.Num();
        PressSwap(FMath::RoundToInt(Angle / Step) % State->Party.Num());
    }
    // Let go without a direction: the menu stays open, paused, for a calm pick.
}

void APYRBattleManager::MoveWheelCursor(int32 Step)
{
    const UPYRTrainerState* State = UPYRTrainerState::Get(this);
    const int32 N = State ? State->Party.Num() : 0;
    if (N == 0) return;
    // Only Pokemon that can come in: not the one fighting, not a fainted one.
    for (int32 I = 1; I <= N; ++I)
    {
        const int32 At = ((WheelCursor + Step * I) % N + N) % N;
        if (At != ActivePartyIndex && State->Party[At].IsAble()) { WheelCursor = At; return; }
    }
}

void APYRBattleManager::ConfirmWheel()
{
    if (bWheelOpen) PressSwap(WheelCursor);
}

void APYRBattleManager::CancelWheel()
{
    if (!bWheelOpen) return;
    if (bWheelForced) { SayRed(TEXT("Who's next?"), 1.2f); return; }
    ShutWheel();
}

void APYRBattleManager::PressSwap(int32 Index)
{
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    if (!bInBattle || EndAt >= 0 || !State || BallPhase != EBall::None || IsPausedFor(PauseLearn())) return;
    if (!State->Party.IsValidIndex(Index) || Index == ActivePartyIndex || !State->Party[Index].IsAble()) return;
    const bool bForced = bWheelForced;
    if (!bForced && !IsSwapReady())
    {
        SayRed(FString::Printf(TEXT("Not yet... %d s!"), FMath::CeilToInt(SwapSecondsLeft())), 1.2f);
        return;
    }
    APYRMonPawn* P = Player.Get();
    const FVector At = P && !P->bFainted ? P->GetActorLocation() : MonSpot(true);
    RecallPlayer(P && P->bFainted);
    SendOutPlayer(Index, At, true);
    if (!bForced) SwapReadyAt = Now() + 30.0;
    // The fight goes on: the incoming Pokemon has its half second untouchable from now.
    ShutWheel();
}

// ------------------------------------------------------------------------------------------------ m4t run
void APYRBattleManager::PressRun()
{
    if (!bInBattle || EndAt >= 0 || IsBattlePaused()) return;
    ++RunPresses;
    RunPressedAt = Now();
    if (Setup.Kind != EPYRBattleKind::Wild)
    {
        // pokeyellow data/text/text_3.asm, _NoRunningText: trainers, gym leaders, the rival and scripted fights.
        ++RunRefusals;
        Caption(TEXT("No! There's no\nrunning from a\ntrainer battle!"));
        if (UPYRMenus* Menus = UPYRMenus::Get(this)) Menus->TextLog.Add(TEXT("No! There's no\nrunning from a\ntrainer battle!"));
        UE_LOG(LogPYRBattle, Display, TEXT("PYR_RUN refused (kind %d)"), int32(Setup.Kind));
        return;
    }
    // Any wild battle: the designer's rule (no Yellow speed roll). A ball in the air or wobbling is dropped.
    if (BallPhase != EBall::None)
    {
        if (BallActor) BallActor->Destroy();
        BallActor = nullptr;
        BallPhase = EBall::None;
        if (APYRMonPawn* F = Foe.Get()) { F->SetActorHiddenInGame(false); F->SetActorEnableCollision(true); }
    }
    bAiming = false;
    Caption(PYRSysText::Get(TEXT("GotAwayText")));
    if (UPYRMenus* Menus = UPYRMenus::Get(this)) Menus->TextLog.Add(PYRSysText::Get(TEXT("GotAwayText")));
    UE_LOG(LogPYRBattle, Display, TEXT("PYR_RUN got away"));
    Decide(EPYRBattleOutcome::Fled, 1.2f);
}

void APYRBattleManager::RecallPlayer(bool bFainted)
{
    APYRMonPawn* P = Player.Get();
    if (!P) return;
    SyncParty();
    const FVector Where = P->GetActorLocation();
    if (P->Species == PYRData::Pikachu)
    {
        // Pikachu never enters a ball: it runs back to Red's side and sits.
        float Floor = Where.Z;
        GroundZ(GetWorld(), Where, Floor, P);
        if (ACharacter* Side = SpawnMonVisual(GetWorld(), PYRData::Pikachu, FTransform(P->GetActorRotation(), FVector(Where.X, Where.Y, Floor))))
        {
            PikachuSide = Cast<APYRMonPawn>(Side);
            if (bFainted && PikachuSide.IsValid()) PikachuSide->SetPose(EPYRPose::Sit, 0);
        }
        LastPikachuPos = Where;
    }
    else
    {
        ++BallEffectsSpawned;
        APYRFxBurst::Spawn(GetWorld(), Where, FLinearColor(.95f, .12f, .1f), 16, 16, 260, .45f, TEXT("Sphere"), 0);
        if (Red.IsValid())
        {
            // The red light: a beam back to Red's ball.
            const FVector From = Red->GetActorLocation() + FVector(0, 0, 40);
            for (int32 i = 1; i < 8; ++i)
                APYRFxBurst::Spawn(GetWorld(), FMath::Lerp(Where, From, i / 8.f), FLinearColor(1, .15f, .1f), 1, 14, 20, .3f, TEXT("Sphere"), 0);
        }
    }
    P->Destroy();
    Player = nullptr;
}

void APYRBattleManager::PressPotion()
{
    APYRMonPawn* P = Player.Get();
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    if (!bInBattle || EndAt >= 0 || !P || P->bFainted || !State || bWheelOpen || IsBattlePaused()) return;
    if (!IsPotionReady()) return;
    if (State->ItemCount(UPYRTrainerState::Potion) <= 0) { SayRed(TEXT("Out of Potions!"), 1.2f); return; }
    if (P->Hp >= P->MaxHp) return;
    State->RemoveItem(UPYRTrainerState::Potion);
    ++PotionsUsed;
    PotionReadyAt = Now() + 10.0;
    P->HealLeft = 20.f;
    P->HealRate = 10.f;  // 20 HP over 2 s; a hit stops the rest.
    SayRed(FString::Printf(TEXT("Hang in there, %s, Potion coming!"), *MonName(P->Species)), 2.f);
    APYRFxBurst::Spawn(GetWorld(), P->GetActorLocation() + FVector(0, 0, 40), FLinearColor(.75f, .45f, .95f), 14, 10, 220, .6f, TEXT("Sphere"), 150);
    APYRMoveFx::Spawn(GetWorld(), EPYRFx::StatUp, FVector(P->GetActorLocation().X, P->GetActorLocation().Y, P->FloorZ()), FVector::ForwardVector,
        FLinearColor(.4f, .9f, .5f), P->GetBattleScreenHeightM() * 100.f, 1.f);
    // Yellow: using an item on Pikachu raises its friendship.
    if (P->Species == PYRData::Pikachu) PYRBond::Apply(this, TEXT("used_item"));
}

// ------------------------------------------------------------------------------------------------ catching
float APYRBattleManager::CatchChance() const
{
    const APYRMonPawn* F = Foe.Get();
    if (!F || !F->Info) return 0.f;
    return PYRData::CatchChance(F->Info->CatchRate, F->Hp, F->MaxHp, F->Status);
}

int32 APYRBattleManager::CatchMeterBand() const
{
    const APYRMonPawn* F = Foe.Get();
    if (!F || !F->Info) return 0;
    const float Best = PYRData::CatchChance(F->Info->CatchRate, 1.f, F->MaxHp, F->Status);
    const float Rel = CatchChance() / FMath::Max(.001f, Best);
    return Rel < .4f ? 0 : Rel < .8f ? 1 : 2;
}

void APYRBattleManager::BeginAim()
{
    if (!bInBattle || EndAt >= 0 || !Foe.IsValid() || Foe->bFainted || BallPhase != EBall::None || Setup.Kind == EPYRBattleKind::Demo || bWheelOpen || IsBattlePaused()) return;
    bAiming = true;
    AimSince = Now();
}

void APYRBattleManager::ReleaseAim()
{
    if (!bAiming || IsBattlePaused()) return;
    bAiming = false;
    PressBall();
}

void APYRBattleManager::PressBall()
{
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    if (!bInBattle || EndAt >= 0 || !Foe.IsValid() || Foe->bFainted || !State || BallPhase != EBall::None || Setup.Kind == EPYRBattleKind::Demo || IsBattlePaused()) return;
    if (!State->RemoveItem(UPYRTrainerState::PokeBall)) { SayRed(TEXT("Out of Poke Balls!"), 1.2f); return; }
    bAiming = false;
    bBallDemo = false;
    ThrowBall(Red.IsValid() ? Red->GetActorLocation() : RedEdge);
    SayRed(TEXT("Go, Poke Ball!"), 1.6f);
}

void APYRBattleManager::ThrowBall(FVector From)
{
    BallFrom = From + FVector(0, 0, 60);
    BallPhase = EBall::Windup;
    BallPhaseAt = Now();
}

void APYRBattleManager::TickBall(float Dt)
{
    if (BallPhase == EBall::None) return;
    APYRMonPawn* F = Foe.Get();
    ACharacter* R = Red.Get();
    const double T = Now() - BallPhaseAt;
    if (!F) { BallPhase = EBall::None; if (BallActor) BallActor->Destroy(); BallActor = nullptr; return; }
    if (BallPhase == EBall::Windup)
    {
        if (R && !bBallDemo) R->SetActorRotation(FRotator(0, (F->GetActorLocation() - R->GetActorLocation()).Rotation().Yaw, 0));
        if (T < .6) return;  // Red's wind-up.
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        BallActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(BallFrom), Params);
        if (BallActor)
        {
            PYRFx::AddPrimitive(BallActor, nullptr, TEXT("Sphere"), FVector::ZeroVector, FVector(.3f), FLinearColor(.9f, .1f, .08f));
            PYRFx::AddPrimitive(BallActor, nullptr, TEXT("Sphere"), FVector(0, 0, -4), FVector(.29f, .29f, .22f), FLinearColor(.97f, .97f, .95f));
            PYRFx::AddPrimitive(BallActor, nullptr, TEXT("Cylinder"), FVector(0, 0, 0), FVector(.305f, .305f, .03f), FLinearColor(.05f, .05f, .05f));
        }
        BallPhase = EBall::Flight;
        BallPhaseAt = Now();
        return;
    }
    if (BallPhase == EBall::Flight)
    {
        BallTo = F->GetActorLocation();
        const float A = FMath::Clamp(float(T / .6), 0.f, 1.f);
        if (BallActor) BallActor->SetActorLocation(FMath::Lerp(BallFrom, BallTo, A) + FVector(0, 0, FMath::Sin(A * PI) * 260.f));
        if (A < 1.f) return;
        if (IsTrainerKind())
        {
            // Yellow: "The trainer blocked the BALL! Don't be a thief!" The ball is gone.
            ++BallsBlocked;
            Caption(TEXT("The trainer\nblocked the BALL!\fDon't be a thief!"));
            APYRFxBurst::Spawn(GetWorld(), BallTo, FLinearColor(1, 1, .9f), 8, 10, 400, .3f, TEXT("Cube"), -600);
            if (BallActor) BallActor->Destroy();
            BallActor = nullptr;
            BallPhase = EBall::None;
            return;
        }
        // A foe that isn't stunned may dodge the ball (60% when its dodge is ready; giants never dodge).
        const float Roll = ForcedBallDodgeRoll >= 0 ? ForcedBallDodgeRoll : FMath::FRand();
        if (!bBallDemo && !F->IsStunned() && F->IsDodgeReady() && Roll < .6f)
        {
            ++BallsDodged;
            const FVector Side = FVector::CrossProduct(FVector::UpVector, (BallTo - BallFrom).GetSafeNormal2D());
            F->Dodge(Side);
            Caption(TEXT("It dodged the\nthrown BALL!"));
            if (BallActor) BallActor->Destroy();
            BallActor = nullptr;
            BallPhase = EBall::None;
            return;
        }
        float Floor = BallTo.Z;
        GroundZ(GetWorld(), BallTo, Floor, F);
        if (BallActor) BallActor->SetActorLocation(FVector(BallTo.X, BallTo.Y, Floor + 16.f));
        F->SetActorHiddenInGame(true);
        F->SetActorEnableCollision(false);
        APYRFxBurst::Spawn(GetWorld(), F->GetActorLocation(), FLinearColor(1, .2f, .15f), 12, 12, 200, .3f, TEXT("Sphere"), 0);
        const float CatchRoll = ForcedCatchRoll >= 0 ? ForcedCatchRoll : FMath::FRand();
        bBallHolds = bBallDemo || CatchRoll < CatchChance();
        BreakAtWobble = bBallHolds ? 99 : FMath::RandRange(1, 3);
        Wobbles = 0;
        BallPhase = EBall::Wobble;
        BallPhaseAt = Now();
        return;
    }
    // Three tense wobbles.
    const int32 Due = FMath::FloorToInt(T / .6);
    if (BallActor) BallActor->SetActorRotation(FRotator(0, 0, FMath::Sin(float(T) * 2.f * PI * 2.f) * 25.f * (T - Due * .6 < .3 ? 1.f : 0.f)));
    if (Due <= Wobbles) return;
    Wobbles = Due;
    if (!bBallHolds && Wobbles >= BreakAtWobble)
    {
        F->SetActorHiddenInGame(false);
        F->SetActorEnableCollision(true);
        F->StunnedUntil = FMath::Max(F->StunnedUntil, Now() + .5);
        APYRFxBurst::Spawn(GetWorld(), F->GetActorLocation(), FLinearColor(1, .2f, .15f), 16, 14, 380, .4f, TEXT("Sphere"), 0);
        Caption(Wobbles >= 3 ? TEXT("Shoot! It was so close too!") : TEXT("Darn! The POKéMON\nbroke free!"));
        if (BallActor) BallActor->Destroy();
        BallActor = nullptr;
        BallPhase = EBall::None;
        return;
    }
    if (bBallHolds && Wobbles >= 3)
    {
        UPYRTrainerState* State = UPYRTrainerState::Get(this);
        if (State && !bBallDemo)
        {
            FPYRMon Mon = UPYRTrainerState::MakeMon(F->Species, F->Level);
            Mon.Hp = FMath::Max(1.f, F->Hp);
            Mon.Status = F->Status;
            Mon.Carried = F->Carried;
            Mon.Learned = F->Carried;
            State->MarkCaught(F->Species);
            const bool bParty = State->AddMonValue(Mon);
            if (!bParty)
            {
                const UPYRProgress* Prog = UPYRProgress::Get(this);
                const FString Name = MonName(F->Species);
                Banner = (Prog && Prog->Has(TEXT("EVENT_MET_BILL")) ? FString::Printf(TEXT("%s was\ntransferred to\nBILL's PC!"), *Name)
                    : FString::Printf(TEXT("%s was\ntransferred to\nsomeone's PC!"), *Name)).Replace(TEXT("\n"), TEXT(" "));
                BannerUntil = Now() + 3.5;
            }
        }
        APYRFxBurst::Spawn(GetWorld(), BallActor ? BallActor->GetActorLocation() : F->GetActorLocation(), FLinearColor(1, .95f, .5f), 18, 10, 300, .6f, TEXT("Sphere"), -200);
        Caption(FString::Printf(TEXT("All right! %s was caught!"), *MonName(F->Species)));
        if (!bBallDemo) SayRed(TEXT("All right!"), 2.f);
        BallPhase = EBall::None;
        Decide(EPYRBattleOutcome::Caught, 1.5f);
    }
}

void APYRBattleManager::TickDemo(float Dt)
{
    if (Setup.Kind != EPYRBattleKind::Demo || EndAt >= 0) return;
    APYRMonPawn* F = Foe.Get();
    if (!F) return;
    const double T = Now() - DemoAt;
    if (DemoStep == 0 && T > 1.2)
    {
        // The old man weakens it (a whack of his stick), then throws.
        DemoStep = 1;
        Caption(TEXT("First, you need to weaken the target Pokemon."));
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::Impact, F->GetActorLocation(), FVector::ForwardVector, FLinearColor(1, .9f, .6f), 120.f, .35f);
        F->Hp = FMath::Max(1.f, FMath::RoundToFloat(F->MaxHp * .3f));
        F->FlashHit();
        HitStop(.08f);
    }
    if (DemoStep == 1 && T > 3.2)
    {
        DemoStep = 2;
        bBallDemo = true;
        const FVector Edge = Setup.Center + (RedEdge - Setup.Center).RotateAngleAxis(40.f, FVector::UpVector);
        ThrowBall(Edge);
    }
}

// ------------------------------------------------------------------------------------------------ hits
void APYRBattleManager::ResolveSelf(APYRMonPawn* User, const FPYRMoveInfo& Move)
{
    if (!User) return;
    const FString Name = MonName(User->Species);
    switch (Move.Effect)
    {
    case EPYREffect::SelfStat:
    {
        const int32 Made = User->AddStage(Move.Stat, Move.Stages);
        if (Made == 0) { Caption(TEXT("Nothing happened!")); return; }
        Caption(FString::Printf(TEXT("%s's %s %s!"), *Name, *PYRData::StatName(Move.Stat).ToString(), FMath::Abs(Made) > 1 ? TEXT("greatly rose") : TEXT("rose")));
        return;
    }
    case EPYREffect::Heal:
        // Recover: half its max HP (Gen 1 fails at full HP).
        if (User->Hp >= User->MaxHp) { Caption(TEXT("But, it failed!")); return; }
        User->Hp = FMath::Min(User->MaxHp, User->Hp + FMath::FloorToFloat(User->MaxHp / 2.f));
        User->HealLeft = 0;
        Caption(FString::Printf(TEXT("%s regained health!"), *Name));
        return;
    case EPYREffect::FocusEnergy:
        if (User->bFocused) { Caption(TEXT("But, it failed!")); return; }
        User->bFocused = true;
        Caption(FString::Printf(TEXT("%s's getting pumped!"), *Name));
        return;
    case EPYREffect::Splash: Caption(TEXT("No effect!")); return;
    case EPYREffect::Mirror: Caption(TEXT("The Mirror Move failed!")); return;  // Nothing to copy yet.
    default: return;
    }
}

void APYRBattleManager::Faint(APYRMonPawn* Victim)
{
    if (!Victim || Victim->bFainted) return;
    if (Victim->bPlayerSide) PlayerFainted();
    else FoeFainted();
}

void APYRBattleManager::ExplodeFaint(APYRMonPawn* User)
{
    if (!bInBattle || !User || User->bFainted) return;
    User->Hp = 0;
    Faint(User);
}

float APYRBattleManager::ResolveHit(APYRMonPawn* User, APYRMonPawn* Victim, const FPYRMoveInfo& Move, FVector At)
{
    LastHitLanded = false;
    if (!bInBattle || !User || !Victim || Victim->bFainted || Victim->IsHidden() || EndAt >= 0 || IsBattlePaused()) return 0.f;
    if (Victim->IsUntargetable())
    {
        // Dived, underground or down in the water: it passes over.
        Caption(FString::Printf(TEXT("%s's attack missed!"), *MonName(User->Species)));
        return 0.f;
    }
    if (Victim->IsInvulnerable())
    {
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::SpeedLines, Victim->GetActorLocation(), Victim->GetActorForwardVector(), FLinearColor::White, 100.f, .25f);
        return 0.f;
    }
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    // Close friendship: once per battle Pikachu dodges by itself.
    if (Victim->bPlayerSide && Victim->Species == PYRData::Pikachu && State && State->GetBond() >= EPYRBond::Close && !bAutoDodgeUsed && Victim->BodyComp
        && Victim->BodyComp->CanDodge())
    {
        bAutoDodgeUsed = true;
        ++AutoDodges;
        Victim->BodyComp->DodgeReadyAt = 0;
        Victim->Dodge(FVector::CrossProduct(FVector::UpVector, (Victim->GetActorLocation() - User->GetActorLocation()).GetSafeNormal2D()));
        Victim->InvulnerableUntil = FMath::Max(Victim->InvulnerableUntil, Now() + .3);
        SayRed(TEXT("Whoa! Pikachu dodged on its own!"), 2.f);
        return 0.f;
    }
    const FPYRSpeciesInfo* UI = User->Info;
    const FPYRSpeciesInfo* VI = Victim->Info;
    if (!UI || !VI) return 0.f;
    const FString UserName = MonName(User->Species), VictimName = MonName(Victim->Species);

    // Accuracy (Gen 1 stages on accuracy and evasion).
    if (Move.Accuracy > 0 && !bNoRandom)
    {
        const float Chance = Move.Accuracy / 100.f * PYRData::StageMultiplier(User->GetStage(EPYRStat::Accuracy)) / PYRData::StageMultiplier(Victim->GetStage(EPYRStat::Evasion));
        if (FMath::FRand() >= FMath::Min(Chance, 255.f / 256.f))
        {
            Caption(FString::Printf(TEXT("%s's attack missed!"), *UserName));
            return 0.f;
        }
    }
    const double T = Now();
    Victim->HealLeft = 0;  // Any landed move stops a Potion's heal.

    // Bide's release: double the damage stored, whatever the type.
    if (Move.Effect == EPYREffect::Bide)
    {
        const float Dmg = FMath::RoundToFloat(User->BideStored * 2.f);
        Caption(FString::Printf(TEXT("%s unleashed energy!"), *UserName));
        User->BideStored = 0;
        if (Dmg <= 0) { Caption(TEXT("But it failed!")); return 0.f; }
        LastHitLanded = true;
        LastDamage = Dmg;
        LastFactor = 1.f;
        Victim->Hp = FMath::Max(0.f, Victim->Hp - Dmg);
        Victim->FlashHit();
        APYRMoveFx::Spawn(GetWorld(), EPYRFx::Impact, At, FVector::ForwardVector, FLinearColor(1, .5f, .1f), 160.f, .35f);
        HitStop(.08f);
        Shake(16.f);
        if (Victim->Hp <= 0.f) Faint(Victim);
        return Dmg;
    }

    const float Factor = PYRData::Effectiveness(Move.Type, *VI);
    LastFactor = Factor;
    if (Factor <= 0.f)
    {
        LastDamage = 0;
        Caption(FString::Printf(TEXT("It doesn't affect %s..."), *VictimName));
        APYRFxBurst::Spawn(GetWorld(), At, PYRData::TypeColour(Move.Type) * .6f, 5, 8, 120, .25f, TEXT("Cube"), 0);
        return 0.f;
    }
    const bool bVictimPoison = VI->Type1 == EPYRType::Poison || VI->Type2 == EPYRType::Poison;
    const bool bVictimGrass = VI->Type1 == EPYRType::Grass || VI->Type2 == EPYRType::Grass;
    const bool bVictimFire = VI->Type1 == EPYRType::Fire || VI->Type2 == EPYRType::Fire;
    auto Failed = [this]() { Caption(TEXT("It didn't affect it!")); };
    auto LowerStat = [this, Victim, &VictimName, &Move]()
    {
        const int32 Made = Victim->AddStage(Move.Stat, Move.Stages);
        if (Made == 0) { Caption(TEXT("Nothing happened!")); return; }
        Caption(FString::Printf(TEXT("%s's %s %s!"), *VictimName, *PYRData::StatName(Move.Stat).ToString(), FMath::Abs(Made) > 1 ? TEXT("greatly fell") : TEXT("fell")));
        if (APYRMoveFx* Down = APYRMoveFx::Spawn(GetWorld(), EPYRFx::StatDown, FVector(Victim->GetActorLocation().X, Victim->GetActorLocation().Y, Victim->FloorZ()),
            FVector::ForwardVector, PYRData::StatColour(Move.Stat), Victim->GetBattleScreenHeightM() * 100.f, .8f))
            Down->MoveId = Move.Id;
    };
    auto Confuse = [this, Victim, &VictimName, T]()
    {
        // Gen 1: 2-5 turns; live, 3-6 s (4 s without dice).
        Victim->ConfusedUntil = T + (bNoRandom ? 4.0 : FMath::FRandRange(3.f, 6.f));
        Victim->bConfused = true;
        Victim->NextStatusFx = 0;
        Caption(FString::Printf(TEXT("%s became confused!"), *VictimName));
    };

    // Status-only moves.
    if (Move.Power <= 0)
    {
        LastHitLanded = true;
        LastDamage = 0;
        switch (Move.Effect)
        {
        case EPYREffect::FoeStat: LowerStat(); break;
        case EPYREffect::Paralyse:
            if (Victim->Status != EPYRStatus::None) { Failed(); break; }
            Victim->Status = EPYRStatus::Paralysis;
            Victim->StunnedUntil = FMath::Max(Victim->StunnedUntil, T + .6);
            Caption(FString::Printf(TEXT("%s's\nparalyzed! It may\nnot attack!"), *VictimName));
            break;
        case EPYREffect::Poison:
            if (Victim->Status != EPYRStatus::None || bVictimPoison) { Failed(); break; }
            Victim->Status = EPYRStatus::Poison;
            Victim->NextPoisonTick = T + 2.0;
            Caption(FString::Printf(TEXT("%s was poisoned!"), *VictimName));
            break;
        case EPYREffect::Sleep:
            if (Victim->Status != EPYRStatus::None) { Failed(); break; }
            // Gen 1: 1-7 turns asleep; live, 1.2 s a turn (3 turns without dice).
            Victim->FallAsleep(PYRObedience::NapSeconds(bNoRandom ? 3 : FMath::RandRange(1, 7)));
            Caption(FString::Printf(TEXT("%s fell asleep!"), *VictimName));
            break;
        case EPYREffect::Confuse:
            if (T < Victim->ConfusedUntil) { Failed(); break; }
            Confuse();
            break;
        case EPYREffect::LeechSeed:
            if (Victim->bSeeded || bVictimGrass) { Failed(); break; }
            Victim->bSeeded = true;
            Victim->Seeder = User;
            Victim->NextSeedTick = T + 2.0;
            Caption(FString::Printf(TEXT("%s was seeded!"), *VictimName));
            break;
        case EPYREffect::Disable:
        {
            TArray<int32> Slots;
            for (int32 i = 0; i < FMath::Min(4, Victim->Carried.Num()); ++i) Slots.Add(i);
            if (Slots.Num() == 0 || (Victim->DisabledSlot != INDEX_NONE && T < Victim->DisabledUntil)) { Failed(); break; }
            // Gen 1: a random move for 1-8 turns; live, 3-8 s (6 s without dice).
            Victim->DisabledSlot = bNoRandom ? 0 : Slots[FMath::RandRange(0, Slots.Num() - 1)];
            Victim->DisabledUntil = T + (bNoRandom ? 6.0 : FMath::FRandRange(3.f, 8.f));
            const FPYRMoveInfo* Locked = PYRData::Move(Victim->Carried[Victim->DisabledSlot]);
            Caption(FString::Printf(TEXT("%s's %s was disabled!"), *VictimName, Locked ? *Locked->Name.ToString() : TEXT("move")));
            break;
        }
        case EPYREffect::Whirlwind:
            if (IsTrainerKind() || Setup.Kind == EPYRBattleKind::Demo) { Caption(TEXT("But, it failed!")); break; }
            // A wild fight: the target is blown out of the ring and the fight is over.
            Caption(FString::Printf(TEXT("%s was blown away!"), *VictimName));
            Victim->LaunchCharacter((Victim->GetActorLocation() - User->GetActorLocation()).GetSafeNormal2D() * 900.f + FVector(0, 0, 500), true, true);
            Decide(EPYRBattleOutcome::Fled, 1.0f);
            break;
        default: break;
        }
        return 0.f;
    }

    // Damage (Gen 1): physical types use Attack/Defence, special types Special; a crit doubles the level and ignores
    // stages and badges. Focus Energy keeps Gen 1's bug (it quarters the crit rate). Selfdestruct halves Defence.
    const bool bSpecial = Move.Type == EPYRType::Electric || Move.Type == EPYRType::Psychic || Move.Type == EPYRType::Fire || Move.Type == EPYRType::Water
        || Move.Type == EPYRType::Grass || Move.Type == EPYRType::Ice || Move.Type == EPYRType::Dragon;
    const EPYRStat AStat = bSpecial ? EPYRStat::Special : EPYRStat::Attack, DStat = bSpecial ? EPYRStat::Special : EPYRStat::Defence;
    float CritChance = FMath::Min(255.f, FMath::FloorToFloat(UI->BaseSpeed / 2.f) * (Move.bHighCrit ? 8.f : 1.f)) / 256.f;
    if (User->bFocused) CritChance *= .25f;
    const bool bCrit = !bNoRandom && Move.FixedDamage <= 0 && FMath::FRand() < CritChance;
    const float Atk = bCrit ? PYRData::StatAt(*UI, AStat, User->Level) : User->EffectiveStat(AStat);
    float Def = bCrit ? PYRData::StatAt(*VI, DStat, Victim->Level) : Victim->EffectiveStat(DStat);
    if (Move.Effect == EPYREffect::Explode) Def = FMath::Max(1.f, FMath::FloorToFloat(Def / 2.f));
    const float Stab = (Move.Type == UI->Type1 || Move.Type == UI->Type2) ? 1.5f : 1.f;
    int32 Hits = Move.MinHits;
    if (Move.MaxHits > Move.MinHits && !bNoRandom)
    {
        // Gen 1: 2 or 3 hits 3/8 each, 4 or 5 hits 1/8 each.
        const float R = FMath::FRand();
        Hits = R < .375f ? 2 : R < .75f ? 3 : R < .875f ? 4 : 5;
    }
    float Dmg = 0;
    if (Move.FixedDamage > 0) Dmg = Move.FixedDamage;  // Sonicboom: always 20.
    else
        for (int32 i = 0; i < Hits; ++i)
        {
            const float Roll = bNoRandom ? 1.f : FMath::RandRange(217, 255) / 255.f;
            Dmg += PYRData::Damage(User->Level * (bCrit ? 2 : 1), Atk, Def, Move.Power, Stab, Factor, Roll);
        }
    LastDamage = Dmg;
    LastHitLanded = true;
    Victim->Hp = FMath::Max(0.f, Victim->Hp - Dmg);
    if (Victim->IsBiding()) Victim->BideStored += Dmg;
    Victim->FlashHit();
    if (Victim->Rig) Victim->Rig->PlayHit(.25f);
    if (bCrit) Caption(TEXT("Critical hit!"));
    if (Hits > 1) Caption(FString::Printf(TEXT("Hit %d times!"), Hits));
    if (Move.FixedDamage <= 0)
    {
        if (Factor > 1.f) Caption(TEXT("It's super effective!"));
        else if (Factor < 1.f) Caption(TEXT("It's not very effective..."));
    }
    // Rage: every hit taken while raging raises Attack.
    if (T < Victim->RageUntil && Victim->Hp > 0 && Victim->AddStage(EPYRStat::Attack, 1) != 0)
        Caption(FString::Printf(TEXT("%s's RAGE is building!"), *VictimName));

    // What the move does besides damage.
    switch (Move.Effect)
    {
    case EPYREffect::Drain:
    {
        // Absorb, Leech Life: half the damage flows back.
        const float Back = FMath::Max(1.f, FMath::FloorToFloat(Dmg / 2.f));
        User->Hp = FMath::Min(User->MaxHp, User->Hp + Back);
        Caption(FString::Printf(TEXT("Sucked health from %s!"), *VictimName));
        break;
    }
    case EPYREffect::Trap:
        // Wrap, Bind, Clamp: the target is held (it can't move or act) and squeezed once more before it slips free;
        // the user holds on too.
        if (Victim->Hp > 0)
        {
            Victim->TrappedUntil = T + 1.6;
            Victim->SqueezeAt = T + 1.4;
            Victim->SqueezeDamage = Dmg;
            Victim->TrappedBy = User;
            Victim->StunnedUntil = FMath::Max(Victim->StunnedUntil, Victim->TrappedUntil);
            User->BusyUntil = FMath::Max(User->BusyUntil, Victim->TrappedUntil);
            Victim->NextStatusFx = 0;
        }
        break;
    case EPYREffect::Rage:
        User->RageUntil = T + 6.0;
        break;
    case EPYREffect::PayDay:
        if (User->bPlayerSide) { PayDayMoney += 2 * User->Level; Caption(TEXT("Coins scattered everywhere!")); }
        break;
    default: break;
    }

    // Side effects by chance.
    const bool bEffect = bNoRandom ? Move.EffectChance >= 1.f : FMath::FRand() < Move.EffectChance;
    if (bEffect && Victim->Hp > 0)
    {
        if (Move.Effect == EPYREffect::Paralyse && Victim->Status == EPYRStatus::None)
        { Victim->Status = EPYRStatus::Paralysis; Caption(FString::Printf(TEXT("%s's\nparalyzed! It may\nnot attack!"), *VictimName)); }
        else if (Move.Effect == EPYREffect::Poison && Victim->Status == EPYRStatus::None && !bVictimPoison)
        { Victim->Status = EPYRStatus::Poison; Victim->NextPoisonTick = T + 2.0; Caption(FString::Printf(TEXT("%s was poisoned!"), *VictimName)); }
        else if (Move.Effect == EPYREffect::Burn && Victim->Status == EPYRStatus::None && !bVictimFire)
        { Victim->Status = EPYRStatus::Burn; Victim->NextBurnTick = T + 2.0; Victim->NextStatusFx = 0; Caption(FString::Printf(TEXT("%s was burned!"), *VictimName)); }
        else if (Move.Effect == EPYREffect::Confuse && T >= Victim->ConfusedUntil) Confuse();
        else if (Move.Effect == EPYREffect::FoeStat) LowerStat();
        else if (Move.Effect == EPYREffect::Flinch)
            Victim->StunnedUntil = FMath::Max(Victim->StunnedUntil, T + .5);
    }
    // A Rock move knocks a flier out of the air for about a second.
    if (Move.Type == EPYRType::Rock && Victim->GetBody() == EPYRBody::Flier)
        if (UPYRFlierBody* Flier = Cast<UPYRFlierBody>(Victim->BodyComp)) Flier->Ground(1.f);

    // Big hits freeze for 0.08 s with radial speed lines and flat sparks.
    const bool bBig = bCrit || Factor > 1.f || Move.Tier == EPYRMoveTier::Big || Dmg >= Victim->MaxHp * .2f;
    const float Size = FMath::Clamp(60.f + Dmg * 6.f, 80.f, 220.f) * (Factor > 1.f ? 1.3f : 1.f);
    APYRMoveFx* Impact = APYRMoveFx::Spawn(GetWorld(), EPYRFx::Impact, At, (At - User->GetActorLocation()).GetSafeNormal(), PYRData::TypeColour(Move.Type), Size, bBig ? .35f : .22f);
    if (Impact) Impact->MoveId = Move.Id;
    if (bBig) { HitStop(.08f); Shake(Factor > 1.f ? 14.f : 9.f); }
    else Shake(4.f);
    if (Victim->Hp <= 0.f) Faint(Victim);
    return Dmg;
}

void APYRBattleManager::PlayerFainted()
{
    APYRMonPawn* P = Player.Get();
    if (!P || P->bFainted) return;
    P->Hp = 0;
    P->SetFaintLook();
    SyncParty();
    Caption(FString::Printf(TEXT("%s fainted!"), *MonName(P->Species)));
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    if (State && P->Species == PYRData::Pikachu) PYRBond::Apply(this, P->Status == EPYRStatus::Poison ? TEXT("poison_fainted") : TEXT("fainted"));  // Yellow: fainting costs Pikachu friendship.
    Fought.Remove(ActivePartyIndex);
    if (!State || State->FirstAble() == INDEX_NONE)
    {
        Decide(EPYRBattleOutcome::Lost, 1.5f);
        return;
    }
    // A faint forces the switch menu open, cooldown or not, with the battle paused until a Pokemon is chosen.
    OpenWheel();
    bWheelForced = bWheelOpen;
    SayRed(FString::Printf(TEXT("%s, come back! Who's next?"), *MonName(P->Species)), 2.5f);
}

void APYRBattleManager::FoeFainted()
{
    APYRMonPawn* F = Foe.Get();
    if (!F || F->bFainted) return;
    F->SetFaintLook();
    Caption(FString::Printf(TEXT("%s fainted!"), *MonName(F->Species)));
    if (Setup.Kind == EPYRBattleKind::Demo) { Decide(EPYRBattleOutcome::Won, 1.5f); return; }
    if (FoeTeam.IsValidIndex(FoeIndex)) AwardXp(FoeTeam[FoeIndex]);
    if (IsTrainerKind() && FoeTeam.IsValidIndex(FoeIndex + 1))
    {
        FoeNextAt = Now() + 1.5;  // The trainer sends out the next Pokemon.
        return;
    }
    Decide(EPYRBattleOutcome::Won, 2.f);
    SayRed(IsTrainerKind() ? TEXT("We won!") : TEXT("All right!"), 2.f);
}

void APYRBattleManager::AwardXp(const FPYRMonSpec& Beaten)
{
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    if (!State) return;
    SyncParty();
    TArray<int32> Earners;
    for (int32 I : Fought) if (State->Party.IsValidIndex(I) && State->Party[I].IsAble()) Earners.Add(I);
    if (Earners.Num() == 0) return;
    const int32 Each = PYRData::XpYield(Beaten.Species, Beaten.Level, IsTrainerKind(), Earners.Num());
    for (int32 I : Earners)
    {
        FPYRMon& Mon = State->Party[I];
        const FString Name = MonName(Mon.Species);
        // Yellow: a traded Pokemon (another trainer's ID) earns half as much again.
        const int32 Gain = Mon.IsTraded() ? Each * 3 / 2 : Each;
        Caption(FString::Printf(TEXT("%s gained %s%d EXP. Points!"), *Name, Mon.IsTraded() ? TEXT("a boosted ") : TEXT(""), Gain));
        TArray<FPYRGrowthEvent> Events;
        State->GiveXp(Mon, Gain, &Events);
        for (const FPYRGrowthEvent& E : Events)
        {
            Caption(FString::Printf(TEXT("%s grew to level %d!"), *MonName(Mon.Species), E.NewLevel));
            Banner = FString::Printf(TEXT("%s grew to level %d!"), *MonName(Mon.Species), E.NewLevel);
            BannerUntil = Now() + 3.5;
            if (Mon.Species == PYRData::Pikachu) PYRBond::Apply(this, TEXT("levelup"));
            // m4: an evolution waits for its scene after the battle (FPYRMon::EvolvePending).
            for (const FName& M : E.Learned)
                if (const FPYRMoveInfo* Info = PYRData::Move(M)) Caption(FString::Printf(TEXT("%s learned %s!"), *MonName(Mon.Species), *Info->Name.ToString()));
            // A fifth move (E.Pending) goes to the learn-move prompt below, which says Yellow's lines itself.
        }
        if (APYRMonPawn* P = Player.Get())
            if (P->PartyIndex == I)
            {
                if (P->Species != Mon.Species)
                {
                    // Evolution in the ring: new body, mesh and hitbox.
                    P->ChangeSpecies(Mon.Species);
                    APYRMoveFx::Spawn(GetWorld(), EPYRFx::Glint, P->GetActorLocation(), FVector::ForwardVector, FLinearColor(1, 1, .9f), 200.f, .8f);
                    APYRFxBurst::Spawn(GetWorld(), P->GetActorLocation(), FLinearColor(1, 1, .95f), 20, 12, 360, .6f, TEXT("Sphere"), 0);
                }
                P->Level = Mon.Level; P->MaxHp = Mon.MaxHp; P->Hp = Mon.Hp;
                if (P->Carried != Mon.Carried)
                {
                    P->Carried = Mon.Carried;
                    for (int32 S = 0; S < 4; ++S)
                        if (const FPYRMoveInfo* Info = P->Carried.IsValidIndex(S) ? PYRData::Move(P->Carried[S]) : nullptr)
                            P->CooldownTotal[S] = P->MoveCooldown(*Info);
                }
            }
    }
    // m4t: a fifth move asks at once, with the fight held (m3's in-fight 1-4 prompt is gone: attack keys answered it).
    if (!bDeferLearn && bInBattle && State->FirstPending() != INDEX_NONE) OpenLearnPrompt();
}

void APYRBattleManager::OpenLearnPrompt()
{
    if (IsPausedFor(PauseLearn())) return;
    // PYRMoveLearning pauses the battle (PauseLearn) for each prompt and gives the fighter its new moves after.
    PYRMoveLearning::LearnPending(this, nullptr);
}

void APYRBattleManager::SyncCarried(int32 PartyIndex)
{
    const UPYRTrainerState* State = UPYRTrainerState::Get(this);
    APYRMonPawn* P = Player.Get();
    if (!State || !P || P->PartyIndex != PartyIndex || !State->Party.IsValidIndex(PartyIndex)) return;
    const TArray<FName>& Now4 = State->Party[PartyIndex].Carried;
    if (P->Carried == Now4) return;
    for (int32 S = 0; S < 4; ++S)
    {
        const FName Was = P->Carried.IsValidIndex(S) ? P->Carried[S] : NAME_None;
        const FName Is = Now4.IsValidIndex(S) ? Now4[S] : NAME_None;
        if (Was == Is) continue;
        // A new move in a slot starts ready.
        P->Cooldown[S] = 0;
        if (const FPYRMoveInfo* Info = PYRData::Move(Is)) P->CooldownTotal[S] = P->MoveCooldown(*Info);
    }
    P->Carried = Now4;
}

void APYRBattleManager::SyncParty()
{
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    const APYRMonPawn* P = Player.Get();
    if (State && P && State->Party.IsValidIndex(P->PartyIndex))
    {
        FPYRMon& Mon = State->Party[P->PartyIndex];
        Mon.Hp = FMath::Max(0.f, P->Hp);
        Mon.Status = P->Status;
    }
}

// ------------------------------------------------------------------------------------------------ end
void APYRBattleManager::Decide(EPYRBattleOutcome Outcome, float Delay)
{
    if (EndAt >= 0) return;
    PendingOutcome = Outcome;
    EndAt = Now() + Delay;
    ShutWheel();
    bAiming = false;
    // The fighters shrink back to field size as the ring clears.
    for (APYRMonPawn* P : {Player.Get(), Foe.Get()})
        if (P && !P->bFainted) P->GrowTo(P->FieldScale(), FMath::Clamp(Delay, .2f, 1.f));
}

void APYRBattleManager::RequestFlee()
{
    if (bInBattle) End(EPYRBattleOutcome::Fled);
}

void APYRBattleManager::DebugFinish(EPYRBattleOutcome Outcome)
{
    if (!bInBattle) return;
    if (Outcome == EPYRBattleOutcome::Won)
    {
        // Every foe beaten in turn, experience included; a fifth move waits for the Game Boy screen after the fight.
        bDeferLearn = true;
        for (int32 i = FoeIndex; i < FoeTeam.Num(); ++i) { FoeIndex = i; AwardXp(FoeTeam[i]); }
        bDeferLearn = false;
    }
    End(Outcome);
}

void APYRBattleManager::End(EPYRBattleOutcome Outcome)
{
    if (!bInBattle) return;
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    SyncParty();
    // Pay Day: Red picks up the coins unless he lost.
    if (State && PayDayMoney > 0 && Outcome != EPYRBattleOutcome::Lost)
    {
        State->AddMoney(PayDayMoney);
        UE_LOG(LogPYRBattle, Display, TEXT("PYR_PAYDAY %d"), PayDayMoney);
        PayDayMoney = 0;
    }
    if (Outcome == EPYRBattleOutcome::Won && State && Setup.Kind != EPYRBattleKind::Demo)
    {
        if (IsTrainerKind() && Setup.Prize > 0) State->AddMoney(Setup.Prize);
        for (int32 I : FoughtAll)
            if (State->Party.IsValidIndex(I) && State->Party[I].Species == PYRData::Pikachu)
            {
                // Yellow: a gym leader win raises friendship most; any win with Pikachu fighting helps a little.
                if (Setup.Kind == EPYRBattleKind::Gym) PYRBond::Apply(this, TEXT("gym_leader"));
                break;
            }
    }
    // Where Pikachu last was: fighting, sitting by Red, or at Red's feet.
    if (Player.IsValid() && Player->Species == PYRData::Pikachu) LastPikachuPos = Player->GetActorLocation();
    else if (PikachuSide.IsValid()) LastPikachuPos = PikachuSide->GetActorLocation();
    else if (Red.IsValid()) LastPikachuPos = Red->GetActorLocation() + Red->GetActorRightVector() * 120.f;
    Cleanup();
    APlayerController* Controller = PC();
    if (ACharacter* R = Red.Get())
    {
        if (Controller) Controller->Possess(R);
        R->SetActorLocation(RedEdge, false, nullptr, ETeleportType::TeleportPhysics);
    }
    if (Controller) Controller->bAutoManageActiveCameraTarget = true;
    if (AActor* F = Follower.Get())
    {
        float Floor = LastPikachuPos.Z;
        FVector At = LastPikachuPos;
        if (GroundZ(GetWorld(), LastPikachuPos + FVector(0, 0, 200), Floor, F))
        {
            float Half = 30.f;
            if (const ACharacter* C = Cast<ACharacter>(F)) Half = C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            At.Z = Floor + Half + 2.f;
        }
        F->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
        F->SetActorHiddenInGame(false);
        F->SetActorEnableCollision(true);
    }
    bInBattle = false;
    ShutWheel();
    bAiming = false;
    // Nothing holds the clock once the fight is over (a learn prompt still up finishes on the field).
    while (PauseReasons.Num()) SetBattlePaused(false, PauseReasons.Last());
    LastOutcome = Outcome;
    LastResult = EPYRBattleResult(uint8(Outcome));
    bHasResult = true;
    ActivePartyIndex = INDEX_NONE;
    UE_LOG(LogPYRBattle, Display, TEXT("PYR_BATTLE_ENDED:%d %s"), int32(Outcome), *Setup.TrainerId.ToString());
    OnBattleEnded.Broadcast(Outcome, Setup.TrainerId);
}

void APYRBattleManager::Cleanup()
{
    for (const TWeakObjectPtr<APYRAttack>& A : Attacks) if (A.IsValid()) A->Destroy();
    Attacks.Reset();
    if (Player.IsValid()) Player->Destroy();
    if (Foe.IsValid()) Foe->Destroy();
    if (PikachuSide.IsValid()) PikachuSide->Destroy();
    Player = Foe = PikachuSide = nullptr;
    if (BallActor) BallActor->Destroy();
    BallActor = nullptr;
    BallPhase = EBall::None;
    if (HitStopEndReal > 0) { UGameplayStatics::SetGlobalTimeDilation(this, 1.f); HitStopEndReal = 0; }
    ShowHud(false);
}

void APYRBattleManager::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HitStopEndReal > 0) UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
    ShowHud(false);
    Super::EndPlay(Reason);
}

// ------------------------------------------------------------------------------------------------ feel
void APYRBattleManager::Caption(const FString& InLine)
{
    // Yellow's words keep the Game Boy's breaks in the source; the caption band shows them on one line.
    const FString Line = InLine.Replace(TEXT("\f"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
    CaptionText = Line;
    CaptionAt = Now();
    CaptionLog.Add(Line);
    if (CaptionLog.Num() > 64) CaptionLog.RemoveAt(0);
    UE_LOG(LogPYRBattle, Display, TEXT("PYR_CAPTION %s"), *Line);
}

void APYRBattleManager::SayRed(const FString& Line, float Seconds)
{
    Shout = Line;
    ShoutUntil = Now() + Seconds;
}

void APYRBattleManager::HitStop(float RealSeconds)
{
    UGameplayStatics::SetGlobalTimeDilation(this, .02f);
    HitStopEndReal = FPlatformTime::Seconds() + RealSeconds;
}

void APYRBattleManager::Shake(float Strength)
{
    APlayerController* Controller = PC();
    APlayerCameraManager* Cam = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
    if (!Cam) return;
    UPYRShakeModifier* Mod = Cast<UPYRShakeModifier>(Cam->FindCameraModifierByClass(UPYRShakeModifier::StaticClass()));
    if (!Mod) Mod = Cast<UPYRShakeModifier>(Cam->AddNewCameraModifier(UPYRShakeModifier::StaticClass()));
    if (Mod) Mod->Kick(Strength, .25f);
}

void APYRBattleManager::NoteFx(FName Move, APYRMoveFx* Fx)
{
    if (Fx && Fx->VisiblePieces() > 0) FxLog.FindOrAdd(Move) += 1;
}

void APYRBattleManager::RegisterAttack(APYRAttack* Attack) { Attacks.Add(Attack); }

void APYRBattleManager::TellAdded(FName Move, FName Species, double TellStart, double HitStart, bool bHadTell)
{
    FPYRTellRecord R;
    R.Move = Move; R.Species = Species; R.TellStart = TellStart; R.HitStart = HitStart; R.bHadTell = bHadTell;
    TellLog.Add(R);
}

void APYRBattleManager::GetActiveTells(TArray<FVector>& Out) const
{
    for (const TWeakObjectPtr<APYRAttack>& A : Attacks)
        if (A.IsValid() && A->User.IsValid() && !A->User->bPlayerSide) A->GetTellPoints(Out);
}

void APYRBattleManager::DebugForceFoeMove(FName Move)
{
    APYRMonPawn* F = Foe.Get();
    if (!F) return;
    const int32 Slot = F->SlotOf(Move);
    if (Slot != INDEX_NONE) F->Cooldown[Slot] = 0;
    if (F->IsSlotDisabled(Slot)) F->DisabledUntil = 0;
    F->StunnedUntil = 0;
    F->BusyUntil = 0;
    F->BideUntil = 0;
    F->SleepUntil = F->TrappedUntil = 0;
    const EPYRStatus Was = F->Status;
    const double WasConfused = F->ConfusedUntil;
    F->Status = EPYRStatus::None;  // A forced move always goes (no full paralysis, no sleep, no confusion).
    F->ConfusedUntil = 0;
    FoeUseMove(F, Move);
    F->Status = Was == EPYRStatus::Sleep ? EPYRStatus::None : Was;
    F->ConfusedUntil = WasConfused;
}

void APYRBattleManager::DebugSetFoeHp(float Fraction)
{
    if (APYRMonPawn* F = Foe.Get()) F->Hp = FMath::Max(1.f, FMath::RoundToFloat(F->MaxHp * Fraction));
}

void APYRBattleManager::DebugResetCooldowns()
{
    if (APYRMonPawn* P = Player.Get())
    {
        for (float& C : P->Cooldown) C = 0;
        if (P->BodyComp) P->BodyComp->DodgeReadyAt = 0;
    }
    SwapReadyAt = PotionReadyAt = 0;
}

// ------------------------------------------------------------------------------------------------ m3 status
void APYRBattleManager::TickStatuses(double T)
{
    for (APYRMonPawn* M : {Player.Get(), Foe.Get()})
    {
        if (!M || M->bFainted) continue;
        const FString Name = MonName(M->Species);
        // Sleep ends on its own.
        if (M->Status == EPYRStatus::Sleep && T >= M->SleepUntil)
        {
            M->Status = EPYRStatus::None;
            M->SetPose(EPYRPose::None, 0);
            Caption(FString::Printf(TEXT("%s woke up!"), *Name));
        }
        // Burn: 1/16 of max HP every 2 s ("... hurt by the burn!").
        if (M->Status == EPYRStatus::Burn && T >= M->NextBurnTick)
        {
            M->NextBurnTick = T + 2.0;
            M->Hp = FMath::Max(0.f, M->Hp - FMath::Max(1.f, FMath::FloorToFloat(M->MaxHp / 16.f)));
            APYRMoveFx::Spawn(GetWorld(), EPYRFx::Flame, M->GetActorLocation(), FVector::UpVector, PYRFx::Palette(TEXT("f08030")), 50.f, .4f);
            if (M->Hp <= 0) { Faint(M); continue; }
        }
        // Leech Seed: 1/16 of max HP flows to the seeder every 2 s.
        if (M->bSeeded && T >= M->NextSeedTick)
        {
            M->NextSeedTick = T + 2.0;
            const float Sap = FMath::Min(M->Hp, FMath::Max(1.f, FMath::FloorToFloat(M->MaxHp / 16.f)));
            M->Hp -= Sap;
            APYRMonPawn* To = M->Seeder.Get();
            if (To && !To->bFainted) To->Hp = FMath::Min(To->MaxHp, To->Hp + Sap);
            Caption(FString::Printf(TEXT("LEECH SEED saps\n%s!"), *Name));
            if (To) APYRMoveFx::Spawn(GetWorld(), EPYRFx::DrainOrbs, M->GetActorLocation(), (To->GetActorLocation() - M->GetActorLocation()).GetSafeNormal(),
                PYRFx::Palette(TEXT("7ad05a")), float(FVector::Dist(M->GetActorLocation(), To->GetActorLocation())), .6f);
            if (M->Hp <= 0) { Faint(M); continue; }
        }
        // Wrap, Bind, Clamp: the second squeeze, then free.
        if (M->TrappedUntil > 0)
        {
            APYRMonPawn* By = M->TrappedBy.Get();
            if (!By || By->bFainted) M->TrappedUntil = M->SqueezeAt = 0;
            else if (M->SqueezeAt > 0 && T >= M->SqueezeAt)
            {
                M->SqueezeAt = 0;
                const float Dmg = FMath::Min(M->Hp, M->SqueezeDamage);
                M->Hp -= Dmg;
                M->FlashHit();
                Caption(FString::Printf(TEXT("%s's attack continues!"), *MonName(By->Species)));
                APYRMoveFx::Spawn(GetWorld(), EPYRFx::Coil, FVector(M->GetActorLocation().X, M->GetActorLocation().Y, M->BottomZ()), FVector::ForwardVector,
                    By->Info ? By->Info->Colour : FLinearColor::Gray, FMath::Max(90.f, M->CapsuleRadius() * 2.4f), .5f);
                if (M->Hp <= 0) { Faint(M); continue; }
            }
            if (T >= M->TrappedUntil) { M->TrappedUntil = 0; M->TrappedBy = nullptr; }
        }
        if (M->DisabledSlot != INDEX_NONE && T >= M->DisabledUntil)
        {
            M->DisabledSlot = INDEX_NONE;
            Caption(FString::Printf(TEXT("%s's disabled no more!"), *Name));
        }
        if (M->bConfused && T >= M->ConfusedUntil)
        {
            M->bConfused = false;
            Caption(FString::Printf(TEXT("%s's confused no more!"), *Name));
        }
    }
}

FVector APYRBattleManager::StandingSpot(FName Species, FVector At) const
{
    if (!Pool.IsActive()) return At;
    const FPYRSpeciesInfo* Info = PYRData::Species(Species);
    FVector Out;
    if (Info && Info->Body == EPYRBody::Swimmer) { if (Pool.NearestOpenWater(At, Out)) return Out; return At; }
    if (Info && (Info->Body == EPYRBody::Runner || Info->Body == EPYRBody::Crawler) && Pool.IsOpenWater(At) && Pool.NearestPlatform(At, Out)) return Out;
    return At;
}

float APYRBattleManager::Probe() const
{
    // Ground traces start this far above a point: under a cave ceiling, well below the rock.
    return HasCeiling() ? FMath::Clamp((CeilingZ - CenterFloorZ) * .4f, 40.f, 400.f) : 400.f;
}

// ------------------------------------------------------------------------------------------------ tick
void APYRBattleManager::TickPikachuSide(float Dt)
{
    APYRMonPawn* Side = PikachuSide.Get();
    ACharacter* R = Red.Get();
    if (!Side || !R) return;
    const FVector Spot = R->GetActorLocation() + R->GetActorRightVector() * 110.f + R->GetActorForwardVector() * 30.f;
    const FVector To = (Spot - Side->GetActorLocation()) * FVector(1, 1, 0);
    if (To.Size() > 50.f)
    {
        Side->AddMovementInput(To.GetSafeNormal(), 1.f);
        Side->SetActorRotation(FRotator(0, To.Rotation().Yaw, 0));
    }
    else
    {
        Side->SetActorRotation(FRotator(0, (Setup.Center - Side->GetActorLocation()).Rotation().Yaw, 0));
        Side->SetPose(EPYRPose::Sit, .2f);
    }
}

void APYRBattleManager::Tick(float Dt)
{
    Super::Tick(Dt);
    if (HitStopEndReal > 0 && FPlatformTime::Seconds() >= HitStopEndReal)
    {
        UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
        HitStopEndReal = 0;
    }
    if (!bInBattle) return;
    const double T = Now();
    Attacks.RemoveAll([](const TWeakObjectPtr<APYRAttack>& A) { return !A.IsValid(); });
    // m4t: held by the switch menu or the learn prompt: no wobble, status tick, poison, next Pokemon or ending.
    if (IsBattlePaused()) return;
    TickBall(Dt);
    TickDemo(Dt);
    TickPikachuSide(Dt);
    // Pikachu runs in from Red's side.
    if (APYRMonPawn* P = Player.Get())
        if (T < RunInUntil)
        {
            const FVector To = (RunInTo - P->GetActorLocation()) * FVector(1, 1, 0);
            if (To.Size() > 60.f && P->InputAxes.IsNearlyZero()) P->Steer(To.GetSafeNormal());
            else RunInUntil = 0;
        }
    // Gen 1 poison: 1/16 of max HP every 2 s (never below 1 in the ring... Yellow lets it faint; so do we).
    for (APYRMonPawn* M : {Player.Get(), Foe.Get()})
        if (M && !M->bFainted && M->Status == EPYRStatus::Poison && T >= M->NextPoisonTick)
        {
            M->NextPoisonTick = T + 2.0;
            M->Hp = FMath::Max(0.f, M->Hp - FMath::Max(1.f, FMath::FloorToFloat(M->MaxHp / 16.f)));
            APYRMoveFx::Spawn(GetWorld(), EPYRFx::PowderCloud, M->GetActorLocation(), FVector::ForwardVector, FLinearColor(.66f, .38f, .78f), 60.f, .4f);
            if (M->Hp <= 0) { if (M->bPlayerSide) PlayerFainted(); else FoeFainted(); }
        }
    TickStatuses(T);
    SyncParty();
    if (ACharacter* R = Red.Get())
        if (BallPhase == EBall::None && Player.IsValid())
        {
            const FVector Mid = (Player->GetActorLocation() + (Foe.IsValid() ? Foe->GetActorLocation() : Setup.Center)) * .5f;
            R->SetActorRotation(FRotator(0, (Mid - R->GetActorLocation()).Rotation().Yaw, 0));
        }
    if (FoeNextAt >= 0 && T >= FoeNextAt)
    {
        FoeNextAt = -1;
        FVector At = MonSpot(false);
        if (Foe.IsValid()) { At = Foe->GetActorLocation(); Foe->Destroy(); }
        SendOutFoe(FoeIndex + 1, At);
    }
    if (EndAt >= 0 && T >= EndAt && !bHoldEnd)
    {
        // (A fifth move's prompt holds the battle clock, so the fight never ends under it.)
        EndAt = -1;
        End(PendingOutcome);
    }
}

void APYRBattleManager::ShowHud(bool bShow)
{
    // m4t: the cursor is free while the HUD is up (RUN and the switch menu's slots can be clicked or tapped).
    if (GetWorld() && !GetWorld()->bIsTearingDown)
        if (APYRPlayerController* Controller = Cast<APYRPlayerController>(PC())) Controller->SetBattleMouse(bShow);
    if (!GEngine || !GEngine->GameViewport) { if (!bShow) Hud.Reset(); return; }
    UGameViewportClient* Viewport = GEngine->GameViewport;
    if (Hud.IsValid())
    {
        Viewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
        Hud.Reset();
    }
    if (bShow)
    {
        Hud = SNew(SPYRBattleHUD).Manager(this);
        Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), 20);
    }
}
