#include "Partner/PYRPikachuFollower.h"

#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/PYRField.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "World/PYRPaint.h"

using namespace PYR;

namespace
{
constexpr float HalfHeight = 22.f;
}

APYRPikachuFollower::APYRPikachuFollower()
{
    PrimaryActorTick.bCanEverTick = true;
    Tags.Add(TEXT("PikachuFollower"));
    AutoPossessAI = EAutoPossessAI::Disabled;
    GetCapsuleComponent()->InitCapsuleSize(18.f, HalfHeight);
    // It walks the Game Boy's tile trail itself and never blocks Red.
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->SetMovementMode(MOVE_None);
    GetCharacterMovement()->GravityScale = 0.f;
    GetMesh()->SetVisibility(false);
    Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
    Visual->SetupAttachment(GetCapsuleComponent());
    Visual->SetRelativeLocation(FVector(0, 0, -HalfHeight));
}

APYRPikachuFollower* APYRPikachuFollower::Find(const UObject* Ctx)
{
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    if (!World) return nullptr;
    for (TActorIterator<APYRPikachuFollower> It(World); It; ++It) return *It;
    return nullptr;
}

void APYRPikachuFollower::BeginPlay()
{
    Super::BeginPlay();
    BuildBody();
    GetCharacterMovement()->SetMovementMode(MOVE_None);
}

ACharacter* APYRPikachuFollower::Red() const
{
    return APokemonYellowReimagineCharacter::Find(this);
}

FIntPoint APYRPikachuFollower::Tile() const
{
    const AYellowWorld* W = AYellowWorld::Get(this);
    return W ? W->ToKanto(GetActorLocation()) : FIntPoint::ZeroValue;
}

void APYRPikachuFollower::BuildBody()
{
    const FLinearColor Y = PYRColor::Pikachu, Brown = PYRColor::Umber;
    if (USkeletalMesh* Rigged = Cast<USkeletalMesh>(LoadOptional(USkeletalMesh::StaticClass(), TEXT("/Game/Pokemon/pikachu/SK_pikachu"))))
    {
        Skin = NewObject<USkeletalMeshComponent>(this);
        Skin->SetSkeletalMesh(Rigged);
        Skin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Skin->SetupAttachment(Visual);
        Skin->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        const FBox Box = Rigged->GetBounds().GetBox();
        const float Scale = 40.f / FMath::Max(1.f, float(Box.GetSize().Z));
        Skin->SetRelativeScale3D(FVector(Scale));
        Skin->SetRelativeLocationAndRotation(FVector(0, 0, -Box.Min.Z * Scale), FRotator::ZeroRotator);  // Rigs face +X.
        Skin->RegisterComponent();
        AddInstanceComponent(Skin);
        if (UAnimSequence* Walk = Cast<UAnimSequence>(LoadOptional(UAnimSequence::StaticClass(), TEXT("/Game/Pokemon/pikachu/A_pikachu_walk"))))
        {
            Skin->SetAnimationMode(EAnimationMode::AnimationSingleNode);
            Walk->bForceRootLock = false;
            Skin->PlayAnimation(Walk, true);
        }
        Head = Group(this, Visual, FVector(4, 0, 34));
        EarL = Group(this, Head, FVector::ZeroVector);
        EarR = Group(this, Head, FVector::ZeroVector);
        Tail = Group(this, Visual, FVector::ZeroVector);
        return;
    }
    Shape(this, Visual, EShape::Sphere, FVector(0, 0, 17), FVector(30, 28, 30), Y);
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        Shape(this, Visual, EShape::Sphere, FVector(8, 8 * Side, 3), FVector(10, 7, 6), Y);
        Shape(this, Visual, EShape::Sphere, FVector(-6, 9 * Side, 4), FVector(10, 7, 8), Y);
    }
    Head = Group(this, Visual, FVector(5, 0, 36));
    Shape(this, Head, EShape::Sphere, FVector::ZeroVector, FVector(28, 30, 25), Y);
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        Shape(this, Head, EShape::Sphere, FVector(12, 6.5f * Side, 3), FVector(4.5f, 4.5f, 5.5f), PYRColor::Ink);
        Shape(this, Head, EShape::Sphere, FVector(9, 11.f * Side, -4), FVector(4, 7, 7), PYRColor::Cheek);
        USceneComponent* Ear = Group(this, Head, FVector(-2, 8 * Side, 9), FRotator(0, 0, 18 * Side));
        Shape(this, Ear, EShape::Cone, FVector(0, 0, 11), FVector(7, 5, 22), Y);
        Shape(this, Ear, EShape::Cone, FVector(0, 0, 20), FVector(4.5f, 3.5f, 8), PYRColor::Ink);
        (Side < 0 ? EarL : EarR) = Ear;
    }
    Tail = Group(this, Visual, FVector(-15, 0, 14), FRotator(-35, 0, 0));
    Shape(this, Tail, EShape::Cube, FVector(0, 0, 5), FVector(3, 5, 10), Brown);
    Shape(this, Tail, EShape::Cube, FVector(-2, 3, 24), FVector(3, 16, 11), Y, false, FRotator(0, 0, -25));
}

void APYRPikachuFollower::SnapBehindRed()
{
    const AYellowWorld* W = AYellowWorld::Get(this);
    const UPYRField* Field = UPYRField::Get(this);
    ACharacter* R = Red();
    if (!W || !R) return;
    const FIntPoint RedTile = W->ToKanto(R->GetActorLocation());
    const EYDir Face = Field ? Field->Facing : YDir::FromYaw(R->GetActorRotation().Yaw);
    // Behind him, else beside him, else on his own tile.
    FIntPoint Best = RedTile;
    for (const EYDir D : {YDir::Opposite(Face), EYDir::Left, EYDir::Right, EYDir::Down, EYDir::Up})
    {
        if (D == EYDir::None) continue;
        const FIntPoint T = RedTile + YDir::Delta(D);
        if (W->MapAtKanto(T) && !W->IsSolidAt(T)) { Best = T; break; }
    }
    const FVector At = W->KantoCenter(Best) + FVector(0, 0, HalfHeight + 1.f);
    SetActorLocationAndRotation(At, FRotator(0, YDir::Yaw(Face), 0), false, nullptr, ETeleportType::TeleportPhysics);
    Target = At;
    LastTile = Best;
    HopEnd = HopStart = -1;
}

void APYRPikachuFollower::BeginScript()
{
    Mode = EPYRPikachuMode::Script;
}

void APYRPikachuFollower::Release()
{
    Mode = EPYRPikachuMode::Follow;
    SnapBehindRed();
}

void APYRPikachuFollower::HopTo(const FVector& Ground, float Seconds)
{
    HopFrom = GetActorLocation();
    HopTo_ = Ground + FVector(0, 0, HalfHeight + 1.f);
    HopStart = GetWorld()->GetTimeSeconds();
    HopEnd = HopStart + FMath::Max(.05f, Seconds);
}

void APYRPikachuFollower::Worried(float Seconds)
{
    WorriedUntil = GetWorld()->GetTimeSeconds() + Seconds;
    LastMood = TEXT("worried");
}

bool APYRPikachuFollower::IsWorried() const { return GetWorld() && GetWorld()->GetTimeSeconds() < WorriedUntil; }

void APYRPikachuFollower::PlayMood(FName Mood)
{
    LastMood = Mood;
    if (Mood == TEXT("worried")) { Worried(4.f); return; }
    if (Mood == TEXT("refuse-ball"))
    {
        // Yellow's lab beat: Pikachu turns its back, swats the ball away and sparks its cheeks.
        if (Red()) SetActorRotation((GetActorLocation() - Red()->GetActorLocation()).GetSafeNormal2D().Rotation());
        SwatUntil = GetWorld()->GetTimeSeconds() + .6;
        Sfx(this, TEXT("S_pikachu_cry"));
    }
}

bool APYRPikachuFollower::TryTalk()
{
    ACharacter* R = Red();
    if (!R || IsHidden() || Mode == EPYRPikachuMode::Waiting) return false;
    if (FVector::Dist2D(GetActorLocation(), R->GetActorLocation()) > 250.f) return false;
    SetActorRotation((R->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation());
    Sfx(this, TEXT("S_pikachu_cry"));
    PYRScreens::ShowPikachu(this);
    return true;
}

void APYRPikachuFollower::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UWorld* World = GetWorld();
    const double Now = World->GetTimeSeconds();
    if (IsHidden() && Mode != EPYRPikachuMode::Waiting) return;   // In battle: battle owns it.
    ACharacter* R = Red();
    const UPYRTrainerState* State = UPYRTrainerState::Get(this);
    const AYellowWorld* W = AYellowWorld::Get(this);
    const UPYRField* Field = UPYRField::Get(this);

    // ---- Pose: ears, tail, trot bob.
    const float Speed = (Target - GetActorLocation()).Size2D() > 5.f ? 350.f : 0.f;
    Phase += DeltaTime * (Speed > 0.f ? 14.f : 2.f);
    const bool bWorried = Now < WorriedUntil;
    const float Droop = bWorried ? 70.f : 0.f;
    if (EarL) EarL->SetRelativeRotation(FRotator(0, 0, -18 - Droop + 4 * FMath::Sin(Now * 3.1)));
    if (EarR) EarR->SetRelativeRotation(FRotator(0, 0, 18 + Droop - 4 * FMath::Sin(Now * 2.7)));
    if (Tail) Tail->SetRelativeRotation(FRotator(-35 + (Now < SwatUntil ? 80.f * FMath::Sin((SwatUntil - Now) * 12.0) : 6.f * FMath::Sin(Now * 4.0)), 0, 0));
    Visual->SetRelativeLocation(FVector(0, 0, -HalfHeight + (Speed > 0.f ? 4.f * FMath::Abs(FMath::Sin(Phase)) : 0.f)));
    if (Skin) Skin->SetPlayRate(Speed > 0.f ? 1.4f : 0.f);

    if (HopEnd > 0)
    {
        const float T = FMath::Clamp(float((Now - HopStart) / (HopEnd - HopStart)), 0.f, 1.f);
        SetActorLocation(FMath::Lerp(HopFrom, HopTo_, T) + FVector(0, 0, 70.f * 4.f * T * (1 - T)), false, nullptr, ETeleportType::TeleportPhysics);
        if (T >= 1.f) { HopEnd = HopStart = -1; Target = GetActorLocation(); }
        return;
    }
    if (!R || !W || !Field) return;
    switch (Mode)
    {
    case EPYRPikachuMode::Waiting:
        // Not out until Red has it (Yellow: Oak gives it in the lab).
        SetActorHiddenInGame(true);
        if (State && State->HasStarter())
        {
            bool bInParty = false;
            for (const FPYRMon& M : State->Party) bInParty |= M.Species == FName(TEXT("pikachu"));
            if (bInParty) { Mode = EPYRPikachuMode::Follow; SetActorHiddenInGame(false); SnapBehindRed(); }
        }
        return;
    case EPYRPikachuMode::Script:
        return;
    default:
        break;
    }
    // ---- Tile-true: walk onto the tile Red last left; if he is far (a warp, a placement), pop behind him.
    const FIntPoint RedTile = W->ToKanto(R->GetActorLocation());
    if (!W->MapAtKanto(RedTile)) return;   // Red is off the map (a battle stage, a test pad).
    const FIntPoint Mine = W->ToKanto(GetActorLocation());
    const int32 Apart = FMath::Abs(RedTile.X - Mine.X) + FMath::Abs(RedTile.Y - Mine.Y);
    if (Apart > 4 || FMath::Abs(R->GetActorLocation().Z - GetActorLocation().Z) > 300.f) { SnapBehindRed(); return; }
    FIntPoint Want = Mine;
    if (Field->Trail.Num() > 0 && Field->Trail.Last() != RedTile && W->MapAtKanto(Field->Trail.Last())) Want = Field->Trail.Last();
    if (Want == RedTile) Want = Mine;
    const FVector Goal = W->KantoCenter(Want) + FVector(0, 0, HalfHeight + 1.f);
    const float RedSpeed = FMath::Max(300.f, float(R->GetVelocity().Size2D()) * 1.05f);
    const FVector To = Goal - GetActorLocation();
    const float Step = RedSpeed * DeltaTime;
    if (To.Size2D() > 1.f)
    {
        // Down a ledge (two tiles in one go): a hop.
        if (To.Size2D() > 150.f) { HopTo(Goal - FVector(0, 0, HalfHeight + 1.f), .35f); return; }
        const FVector Move = To.Size2D() <= Step ? To : To.GetSafeNormal2D() * Step;
        SetActorLocation(GetActorLocation() + FVector(Move.X, Move.Y, Goal.Z - GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), To.GetSafeNormal2D().Rotation(), DeltaTime, 12.f));
    }
    else if (R->GetVelocity().Size2D() < 20.f)
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), (R->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation(), DeltaTime, 4.f));
    Target = Goal;
    const FIntPoint Now2 = W->ToKanto(GetActorLocation());
    if (Now2 != LastTile) { LastTile = Now2; ++TilesWalked; }
}
