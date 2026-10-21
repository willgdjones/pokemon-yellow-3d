#include "PokemonYellowReimagineCharacter.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/YellowWorld.h"
#include "Library/FollowCameraComponent.h"
#include "Menus/PYRMenus.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Music/GbMusicPlayer.h"
#include "Partner/PYRPikachuFollower.h"
#include "World/PYRPaint.h"

using namespace PYR;

namespace
{
constexpr float StandHalfHeight = 88.f;
constexpr float CrouchHalfHeight = 52.f;  // 40% lower than standing.
constexpr float RedHeight = 145.f;
constexpr float HopSeconds = .38f;
}

APokemonYellowReimagineCharacter::APokemonYellowReimagineCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34.f, StandHalfHeight);
    bUseControllerRotationYaw = false;
    UCharacterMovementComponent* Move = GetCharacterMovement();
    Move->bOrientRotationToMovement = true;
    Move->RotationRate = FRotator(0, 720, 0);
    Move->MaxWalkSpeed = WalkSpeed;
    Move->MaxWalkSpeedCrouched = WalkSpeed * .5f;
    Move->MaxStepHeight = 30.f;
    Move->SetWalkableFloorAngle(40.f);
    Move->NavAgentProps.bCanCrouch = true;
    Move->SetCrouchedHalfHeight(CrouchHalfHeight);
    Move->bCanWalkOffLedges = true;
    Move->bCanWalkOffLedgesWhenCrouching = true;
    Move->BrakingDecelerationWalking = 2400.f;
    Move->MaxAcceleration = 3000.f;
    GetMesh()->SetVisibility(false);

    Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
    Visual->SetupAttachment(GetCapsuleComponent());
    Visual->SetRelativeLocation(FVector(0, 0, -StandHalfHeight));
    VisualBaseZ = -StandHalfHeight;

    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(Visual);
    Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
    Body->SetRelativeLocation(FVector(0, 0, 75));
    Body->SetRelativeScale3D(FVector(.36f, .42f, .5f));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Boom"));
    Boom->SetupAttachment(GetCapsuleComponent());
    Boom->TargetArmLength = 650.f;
    Boom->bUsePawnControlRotation = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Boom);

    // The road camera: the library follow camera in third person, turned by the (captured) mouse.
    FollowCamera = CreateDefaultSubobject<UFollowCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->Mode = EFollowCameraMode::ThirdPerson;
    FollowCamera->FollowArmLength = 650.f;
    FollowCamera->FollowSocketOffset = FVector(0, 0, 90);
    FollowCamera->FollowFov = 75.f;
    Tags.Add(TEXT("Red"));
}

APokemonYellowReimagineCharacter* APokemonYellowReimagineCharacter::Find(const UObject* Ctx)
{
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    if (!World) return nullptr;
    for (TActorIterator<APokemonYellowReimagineCharacter> It(World); It; ++It) return *It;
    return nullptr;
}

void APokemonYellowReimagineCharacter::BeginPlay()
{
    Super::BeginPlay();
    BuildBody();
}

void APokemonYellowReimagineCharacter::BuildBody()
{
    // The rigged anime Red (idle, walk, run clips) when it is imported, else the static one, else primitives.
    if (USkeletalMesh* RedMesh = Cast<USkeletalMesh>(LoadOptional(USkeletalMesh::StaticClass(), TEXT("/Game/Characters/red/SK_red"))))
    {
        Rigged = NewObject<USkeletalMeshComponent>(this, TEXT("RedRig"));
        Rigged->SetSkeletalMesh(RedMesh);
        Rigged->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Rigged->SetupAttachment(Visual);
        Rigged->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        const FBox Box = RedMesh->GetBounds().GetBox();
        const float Scale = RedHeight / FMath::Max(1.f, float(Box.GetSize().Z));
        Rigged->SetRelativeScale3D(FVector(Scale));
        Rigged->SetRelativeLocationAndRotation(FVector(0, 0, -Box.Min.Z * Scale), FRotator::ZeroRotator);  // Imported rigs face +X.
        Rigged->RegisterComponent();
        AddInstanceComponent(Rigged);
        for (const TPair<const TCHAR*, const TCHAR*>& C : {TPair<const TCHAR*, const TCHAR*>(TEXT("idle"), TEXT("standing")),
                 TPair<const TCHAR*, const TCHAR*>(TEXT("walk"), TEXT("walking")), TPair<const TCHAR*, const TCHAR*>(TEXT("run"), TEXT("run"))})
            for (const TCHAR* Asset : {C.Key, C.Value})
                if (UAnimSequence* A = Cast<UAnimSequence>(LoadOptional(UAnimSequence::StaticClass(), FString::Printf(TEXT("/Game/Characters/red/A_red_%s"), Asset))))
                {
                    // The clips are already in place; locking the Hips root to the reference pose tips Red over.
                    A->bForceRootLock = false;
                    Clips.Add(FName(C.Key), A);
                }
        Rigged->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Body->SetVisibility(false);
        bPainted = true;
        UpdateClip();
        return;
    }
    if (UStaticMesh* Painted = Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), TEXT("/Game/Characters/red/SM_red"))))
        if (FitStaticBody(this, Visual, Painted, RedHeight, 0.f, 90.f)) { Body->SetVisibility(false); bPainted = true; return; }
    // Primitive Red in palette colours: jeans, blue vest over a black tee, a cap.
    Body->SetVisibility(false);
    const float s = RedHeight / 170.f;
    const FLinearColor Jeans = PYRColor::Shade(PYRColor::Blue, .8f), Cap = PYRColor::Red;
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        Shape(this, Visual, EShape::Cylinder, FVector(0, 9 * s * Side, 39 * s), FVector(18, 18, 78) * s, Jeans);
        Shape(this, Visual, EShape::Cylinder, FVector(0, 24 * s * Side, 108 * s), FVector(11, 11, 54) * s, PYRColor::Skin);
    }
    Shape(this, Visual, EShape::Cube, FVector(2 * s, 0, 106 * s), FVector(24, 42, 56) * s, PYRColor::Blue);
    Shape(this, Visual, EShape::Sphere, FVector(0, 0, 152 * s), FVector(30, 30, 32) * s, PYRColor::Skin);
    Shape(this, Visual, EShape::Sphere, FVector(-2 * s, 0, 160 * s), FVector(32, 32, 20) * s, Cap);
}

void APokemonYellowReimagineCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("MoveForward"), this, &APokemonYellowReimagineCharacter::MoveForward);
    Input->BindAxis(TEXT("MoveRight"), this, &APokemonYellowReimagineCharacter::MoveRight);
    Input->BindAxis(TEXT("Turn"), this, &APawn::AddControllerYawInput);
    Input->BindAxis(TEXT("LookUp"), this, &APawn::AddControllerPitchInput);
    Input->BindKey(EKeys::LeftShift, IE_Pressed, this, &APokemonYellowReimagineCharacter::RunPressed);
    Input->BindKey(EKeys::LeftShift, IE_Released, this, &APokemonYellowReimagineCharacter::RunReleased);
    Input->BindKey(EKeys::RightShift, IE_Pressed, this, &APokemonYellowReimagineCharacter::RunPressed);
    Input->BindKey(EKeys::RightShift, IE_Released, this, &APokemonYellowReimagineCharacter::RunReleased);
    Input->BindKey(EKeys::LeftControl, IE_Pressed, this, &APokemonYellowReimagineCharacter::CrouchPressed);
    Input->BindKey(EKeys::C, IE_Pressed, this, &APokemonYellowReimagineCharacter::CrouchPressed);
    Input->BindKey(EKeys::E, IE_Pressed, this, &APokemonYellowReimagineCharacter::DoInteract);
    Input->BindKey(EKeys::SpaceBar, IE_Pressed, this, &APokemonYellowReimagineCharacter::DoInteract);
    Input->BindKey(EKeys::T, IE_Pressed, this, &APokemonYellowReimagineCharacter::TalkToPikachu);
}

void APokemonYellowReimagineCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    NewController->SetControlRotation(FRotator(-22, GetActorRotation().Yaw, 0));
}

namespace
{
// QA hook (off unless -QAMenuLog or PYR_QA_MENU_LOG=1): the walk input Red gets, once a second at most.
void QAMoveLog(const AActor* Red, const TCHAR* Axis, float Value, bool bLocked, bool bHopping)
{
    static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("QAMenuLog")) || FPlatformMisc::GetEnvironmentVariable(TEXT("PYR_QA_MENU_LOG")) == TEXT("1");
    static double Last = -10;
    if (!bOn || Value == 0.f || !Red || !Red->GetWorld() || Red->GetWorld()->GetTimeSeconds() - Last < 1.0) return;
    Last = Red->GetWorld()->GetTimeSeconds();
    UE_LOG(LogTemp, Display, TEXT("QA_MOVE %s %.2f locked=%d hopping=%d"), Axis, Value, bLocked ? 1 : 0, bHopping ? 1 : 0);
}
}

void APokemonYellowReimagineCharacter::MoveForward(float Value)
{
    QAMoveLog(this, TEXT("forward"), Value, bInputLocked, bHopping);
    if (!Controller || Value == 0.f || bInputLocked || bHopping) return;
    AddMovementInput(FollowCamera->GetMoveDirection(EAxis::X), Value);
}

void APokemonYellowReimagineCharacter::MoveRight(float Value)
{
    QAMoveLog(this, TEXT("right"), Value, bInputLocked, bHopping);
    if (!Controller || Value == 0.f || bInputLocked || bHopping) return;
    AddMovementInput(FollowCamera->GetMoveDirection(EAxis::Y), Value);
}

void APokemonYellowReimagineCharacter::DoInteract()
{
    if (bInputLocked) return;
    if (UPYRField* Field = UPYRField::Get(this))
    {
        // Pikachu right in front answers E too, unless someone stands on the tile Red faces: on the Game Boy that tile
        // decides who answers (Pikachu catching up beside Red must not take a person's talk).
        const AYellowWorld* W = AYellowWorld::Get(this);
        const FIntPoint Ahead = (W ? W->ToKanto(GetActorLocation()) : Field->KantoTile) + YDir::Delta(YDir::FromYaw(GetActorRotation().Yaw));
        if (Field->IsOccupied(Ahead, this)) { Field->Interact(); return; }
        if (APYRPikachuFollower* Pika = APYRPikachuFollower::Find(this))
            if (!Pika->IsHidden() && FVector::Dist2D(Pika->GetActorLocation(), GetActorLocation()) < 130.f
                && FVector::DotProduct((Pika->GetActorLocation() - GetActorLocation()).GetSafeNormal2D(), GetActorForwardVector()) > .6f)
            {
                TalkToPikachu();
                return;
            }
        Field->Interact();
    }
}

void APokemonYellowReimagineCharacter::TalkToPikachu()
{
    if (bInputLocked) return;
    if (APYRPikachuFollower* Pika = APYRPikachuFollower::Find(this)) Pika->TryTalk();
}

void APokemonYellowReimagineCharacter::ScriptWalkPath(const TArray<FVector>& Points)
{
    ScriptPath = Points;
    if (ScriptPath.Num() == 0) { bScriptWalk = false; return; }
    ScriptTarget = ScriptPath[0];
    ScriptPath.RemoveAt(0);
    bScriptWalk = true;
}

float APokemonYellowReimagineCharacter::GetGroundSpeed() const
{
    return GetVelocity().Size2D();
}

void APokemonYellowReimagineCharacter::UpdateLedgeHop(float DeltaTime)
{
    UCharacterMovementComponent* Move = GetCharacterMovement();
    if (bHopping)
    {
        // A short arc over the ledge tile to the one beyond, ignoring collision.
        HopT += DeltaTime / HopSeconds;
        const float A = FMath::Clamp(HopT, 0.f, 1.f);
        FVector P = FMath::Lerp(HopFrom, HopTo, A);
        P.Z += FMath::Sin(A * PI) * 70.f;
        SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
        if (A >= 1.f)
        {
            bHopping = false;
            Move->SetMovementMode(MOVE_Walking);
        }
        return;
    }
    const AYellowWorld* World = AYellowWorld::Get(this);
    if (!World || !Move->IsMovingOnGround() || bInputLocked) { LedgePush = 0.f; return; }
    // Which way Red is pushing: the stick, else a scripted walk.
    const FVector In = !TestInput.IsNearlyZero() ? TestInput : GetLastMovementInputVector();
    if (In.SizeSquared2D() < .04f) { LedgePush = 0.f; return; }
    const FVector Dir2 = In.GetSafeNormal2D();
    const EYDir D = YDir::FromVector(Dir2);
    const FIntPoint Delta = YDir::Delta(D);
    // Mostly along the hop axis (diagonals within about 40 degrees still hop).
    if (FMath::Abs(Delta.X ? Dir2.X : Dir2.Y) < .75f) { LedgePush = 0.f; return; }
    const FIntPoint Here = World->ToKanto(GetActorLocation());
    const FIntPoint Ledge = Here + Delta;
    if (YTile::LedgeDir(World->TileAt(Ledge)) != D) { LedgePush = 0.f; return; }
    const FIntPoint Land = Ledge + Delta;
    if (World->IsSolidAt(Land)) { LedgePush = 0.f; return; }
    // Up against the ledge's edge (its collision stops him there): hop after a tick of pushing.
    const FVector Edge = World->KantoCenter(Here) + FVector(Delta.X, Delta.Y, 0) * 50.f;
    const float Gap = FMath::Abs(Delta.X ? Edge.X - GetActorLocation().X : Edge.Y - GetActorLocation().Y);
    if (Gap > GetCapsuleComponent()->GetScaledCapsuleRadius() + 12.f) { LedgePush = 0.f; return; }
    LedgePush += DeltaTime;
    if (LedgePush < .05f) return;
    LedgePush = 0.f;
    bHopping = true;
    ++Hops;
    HopT = 0.f;
    HopFrom = GetActorLocation();
    // Land in the tile beyond, keeping his place across the ledge but inside the tile.
    const FVector LandCenter = World->KantoCenter(Land);
    FVector To = HopFrom;
    if (Delta.X) { To.X = LandCenter.X; To.Y = FMath::Clamp(HopFrom.Y, LandCenter.Y - 30.f, LandCenter.Y + 30.f); }
    else { To.Y = LandCenter.Y; To.X = FMath::Clamp(HopFrom.X, LandCenter.X - 30.f, LandCenter.X + 30.f); }
    HopTo = To;
    Move->StopMovementImmediately();
    Move->SetMovementMode(MOVE_Flying);
    SetActorRotation(FRotator(0, YDir::Yaw(D), 0));
    UGbMusicPlayer::PlaySfx(this, TEXT("ledge"));
    UE_LOG(LogYellow, Display, TEXT("YELLOW_LEDGE_HOP %s over (%d,%d)"), YDir::Name(D), Ledge.X, Ledge.Y);
}

void APokemonYellowReimagineCharacter::UpdateClip()
{
    if (!Rigged || Clips.Num() == 0) return;
    const float Speed = GetGroundSpeed();
    FName Want = Speed > WalkSpeed + 40.f ? FName(TEXT("run")) : Speed > 30.f || bHopping ? FName(TEXT("walk")) : FName(TEXT("idle"));
    if (!Clips.Contains(Want)) Want = Clips.Contains(TEXT("walk")) ? FName(TEXT("walk")) : Clips.CreateConstIterator()->Key;
    if (Want == Clip) return;
    Clip = Want;
    Rigged->PlayAnimation(Clips[Want], true);
}

void APokemonYellowReimagineCharacter::UpdateRoomCamera()
{
    // Indoors (and in caves) the camera stays inside the room: every wall stays drawn and none blocks the view.
    const AYellowWorld* World = AYellowWorld::Get(this);
    const FYMap* Map = World ? World->MapAt(GetActorLocation()) : nullptr;
    const bool bRoom = Map && !Map->bOutdoor && Map->Tileset.ToUpper() != TEXT("FOREST");
    if (World && !FollowCamera->ViewBlocked)
    {
        // What the camera must not look through at Red: in rooms and caves their walls and tall furniture; anywhere,
        // a person standing between (outdoors trees and buildings in the way are hidden instead).
        TWeakObjectPtr<const AYellowWorld> WeakWorld = World;
        TWeakObjectPtr<const APokemonYellowReimagineCharacter> Self = this;
        FollowCamera->ViewBlocked = [WeakWorld, Self](const FVector& From, const FVector& To)
        {
            const AYellowWorld* W = WeakWorld.Get();
            if (!W) return false;
            const FVector D = To - From;
            const float Len = D.Size();
            if (Len < 1.f) return false;
            const FYMap* Here = Self.IsValid() ? W->MapAt(Self->GetActorLocation()) : nullptr;
            const bool bInside = Here && !Here->bOutdoor && Here->Tileset.ToUpper() != TEXT("FOREST");
            if (bInside)
                for (float S = 40.f; S < Len; S += 20.f)
                {
                    const FVector P = From + D * (S / Len);
                    const FIntPoint K = W->ToKanto(P);
                    if (!W->IsSolidAt(K)) continue;
                    const TCHAR L = W->LookAt(K);
                    const float Z = P.Z - W->GetOffset().Z;
                    const float H = L == TEXT('C') ? 130.f : L == TEXT('?') || L == TEXT(' ') ? 320.f : L == TEXT('b') ? 200.f : L == TEXT('B') || L == TEXT('R') || L == TEXT('n') ? 330.f : 0.f;
                    if (Z < H) return true;
                }
            // People (not Red, not a step away from him) whose body the line passes through.
            if (const UPYRField* F = UPYRField::Get(W))
                for (const TWeakObjectPtr<APYRPerson>& PW : F->People)
                {
                    const APYRPerson* P = PW.Get();
                    if (!P || !P->IsShown() || (Here && P->Map != Here->Id)) continue;
                    const FVector At = P->GetActorLocation();
                    const float Along = FVector::DotProduct(At - From, D / Len);
                    if (Along < 70.f || Along > Len) continue;
                    const FVector Closest = From + D / Len * Along;
                    const float Top = At.Z + 90.f;
                    if (FVector::Dist2D(Closest, At) < 55.f && Closest.Z < Top) return true;
                }
            // Someone right behind Red (a rival walking up to him): Red must not hide them from the camera either.
            if (const UPYRField* F = UPYRField::Get(W))
                for (const TWeakObjectPtr<APYRPerson>& PW : F->People)
                {
                    const APYRPerson* P = PW.Get();
                    if (!P || !P->IsShown() || (Here && P->Map != Here->Id) || FVector::Dist2D(P->GetActorLocation(), From) > 260.f) continue;
                    const FVector Head = P->GetActorLocation() + FVector(0, 0, 50.f);
                    const FVector Seg = Head - To;
                    const float T = FMath::Clamp(FVector::DotProduct(From - To, Seg) / FMath::Max(1.f, Seg.SizeSquared()), 0.f, 1.f);
                    if (T < .98f && FVector::Dist(To + Seg * T, From + FVector(0, 0, 10.f)) < 45.f) return true;
                }
            // While someone talks to Red (a text box is up), Red must not stand in front of them either: a person a
            // step or two beyond him (Brock, Oak, the rival face to face) whose body the camera would see through his
            // (From is Red, To the camera). The camera tilts down until the two stand apart, as on the Game Boy.
            const UPYRMenus* Menus = UPYRMenus::Get(W);
            if (Menus && Menus->IsOpen(TEXT("text")))
                if (const UPYRField* F = UPYRField::Get(W))
                    for (const TWeakObjectPtr<APYRPerson>& PW : F->People)
                    {
                        const APYRPerson* P = PW.Get();
                        if (!P || !P->IsShown() || (Here && P->Map != Here->Id)) continue;
                        const FVector At = P->GetActorLocation();
                        if (FVector::Dist2D(At, From) > 220.f || FVector::DotProduct(At - From, From - To) <= 0.f) continue;
                        for (const float PersonUp : {-50.f, 0.f, 50.f})
                        {
                            const FVector Seg = At + FVector(0, 0, PersonUp) - To;
                            for (const float RedUp : {-50.f, 0.f, 50.f})
                            {
                                const FVector Body = From + FVector(0, 0, RedUp);
                                const float T = FMath::Clamp(FVector::DotProduct(Body - To, Seg) / FMath::Max(1.f, Seg.SizeSquared()), 0.f, 1.f);
                                if (T < .97f && FVector::Dist(To + Seg * T, Body) < 40.f) return true;
                            }
                        }
                    }
            return false;
        };
    }
    if (!bRoom) { if (FollowCamera->IsConfined()) FollowCamera->ClearConfine(); return; }
    const FVector Min = World->GetOffset() + FVector(Map->Origin.X * 100.f + 30.f, Map->Origin.Y * 100.f + 30.f, 40.f);
    // Up to just under the room's black lid (900): in a cave's narrow corridor the camera rises over the rock walls.
    const FVector Max = World->GetOffset() + FVector((Map->Origin.X + Map->Width) * 100.f - 30.f, (Map->Origin.Y + Map->Height) * 100.f - 30.f, 850.f);
    FollowCamera->SetConfine(FBox(Min, Max));
}

void APokemonYellowReimagineCharacter::UpdateTalkCamera(float DeltaTime)
{
    AController* Ctrl = GetController();
    if (!Ctrl) return;
    // Someone a step or so in front of Red while a text box is up.
    bool bFace = false;
    FVector ToTalker = FVector::ZeroVector;
    const UPYRMenus* Menus = UPYRMenus::Get(this);
    if (Menus && Menus->IsOpen(TEXT("text")) && !bScriptWalk)
        if (const UPYRField* F = UPYRField::Get(this))
            for (const TWeakObjectPtr<APYRPerson>& PW : F->People)
            {
                const APYRPerson* P = PW.Get();
                if (!P || !P->IsShown() || P->Map != F->CurrentMap) continue;
                const FVector To = P->GetActorLocation() - GetActorLocation();
                if (To.Size2D() < 160.f && FVector::DotProduct(To.GetSafeNormal2D(), GetActorForwardVector()) > .6f) { bFace = true; ToTalker = To; break; }
            }
    FRotator Rot = Ctrl->GetControlRotation();
    if (bFace && !bTalkTurn)
    {
        // Look across the line between the two, 60 degrees off it, whichever way of the four is nearest the camera's own
        // yaw (so it keeps looking much the way it was), and they stand well apart in the picture (m4r: turning a fixed
        // 40 degrees from wherever the camera was left them touching, visual-074).
        bTalkTurn = true;
        bTalkYawSet = false;
        TalkYawFrom = Rot.Yaw;
        const float Line = ToTalker.Rotation().Yaw;
        float Best = Line + 60.f;
        for (const float Cand : {Line + 60.f, Line - 60.f, Line + 120.f, Line - 120.f})
            if (FMath::Abs(FRotator::NormalizeAxis(Cand - Rot.Yaw)) < FMath::Abs(FRotator::NormalizeAxis(Best - Rot.Yaw))) Best = Cand;
        TalkYawWant = Best;
    }
    if (!bTalkTurn) return;
    // Someone else turned the camera since (the player's mouse, a script or a staged view): the turn is theirs now.
    if (bTalkYawSet && FMath::Abs(FRotator::NormalizeAxis(Rot.Yaw - TalkYawSet)) > 2.f && !bFace) { bTalkTurn = bTalkYawSet = false; return; }
    const float Want = bFace ? TalkYawWant : TalkYawFrom;
    const float Yaw = FMath::FInterpTo(Rot.Yaw, Rot.Yaw + FRotator::NormalizeAxis(Want - Rot.Yaw), DeltaTime, bFace ? 4.f : 6.f);
    Rot.Yaw = Yaw;
    Ctrl->SetControlRotation(Rot);
    TalkYawSet = Yaw;
    bTalkYawSet = true;
    if (!bFace && FMath::Abs(FRotator::NormalizeAxis(Want - Yaw)) < .5f) bTalkTurn = bTalkYawSet = false;
}

void APokemonYellowReimagineCharacter::SetRun(bool bRun)
{
    bRunning = bRun;
    GetCharacterMovement()->MaxWalkSpeed = bRun ? RunSpeed : WalkSpeed;
}

void APokemonYellowReimagineCharacter::SetCrouch(bool bCrouch)
{
    if (bCrouch) Crouch();
    else UnCrouch();
}

void APokemonYellowReimagineCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    VisualBaseZ = -CrouchHalfHeight;
    Visual->SetRelativeScale3D(FVector(1.08f, 1.08f, .6f));
}

void APokemonYellowReimagineCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    VisualBaseZ = -StandHalfHeight;
    Visual->SetRelativeScale3D(FVector::OneVector);
}

void APokemonYellowReimagineCharacter::LockInput(bool bLock)
{
    if (bInputLocked == bLock) return;
    bInputLocked = bLock;
    if (bLock) SetRun(false);
}

void APokemonYellowReimagineCharacter::PlaceAt(const FVector& Ground, float Yaw)
{
    bHopping = false;
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    SetActorLocationAndRotation(Ground + FVector(0, 0, Half + 3.f), FRotator(0, Yaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
    GetCharacterMovement()->StopMovementImmediately();
    if (Controller) Controller->SetControlRotation(FRotator(-22, Yaw, 0));
}

void APokemonYellowReimagineCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (bScriptWalk)
    {
        const FVector To = ScriptTarget - GetActorLocation();
        if (To.Size2D() < 12.f)
        {
            if (ScriptPath.Num() > 0) { ScriptTarget = ScriptPath[0]; ScriptPath.RemoveAt(0); }
            else { bScriptWalk = false; GetCharacterMovement()->StopMovementImmediately(); }
        }
        else AddMovementInput(To.GetSafeNormal2D(), FMath::Clamp(To.Size2D() / 60.f, .35f, 1.f));
    }
    else if (!TestInput.IsNearlyZero() && !bInputLocked && !bHopping) AddMovementInput(TestInput.GetSafeNormal2D(), FMath::Min(1.f, float(TestInput.Size2D())));
    UpdateLedgeHop(DeltaTime);
    UpdateClip();
    UpdateRoomCamera();
    UpdateTalkCamera(DeltaTime);
    const float Speed = GetVelocity().Size2D();
    StepPhase += DeltaTime * FMath::Clamp(Speed / 38.f, 0.f, 20.f);
    const float Bob = (bIsCrouched || Rigged) ? 0.f : FMath::Clamp(Speed / WalkSpeed, 0.f, 1.3f) * 4.f * FMath::Abs(FMath::Sin(StepPhase));
    Visual->SetRelativeLocation(FVector(0, 0, VisualBaseZ + Bob));
}
