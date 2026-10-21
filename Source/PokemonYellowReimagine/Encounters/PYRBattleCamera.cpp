#include "PYRBattleCamera.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "World/PYRQuery.h"

APYRBattleCamera::APYRBattleCamera()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCameraComponent()->SetFieldOfView(Fov);
    GetCameraComponent()->bConstrainAspectRatio = false;
    Tags.Add(TEXT("BattleCamera"));
}

bool APYRBattleCamera::Project(const FVector& Point, FVector2D& Out) const
{
    const FVector Local = GetActorRotation().UnrotateVector(Point - GetActorLocation());
    if (Local.X <= 1.f) return false;
    const float HalfW = FMath::Tan(FMath::DegreesToRadians(GetCameraComponent()->FieldOfView * .5f));
    const float HalfH = HalfW / Aspect;
    Out = FVector2D(Local.Y / (Local.X * HalfW), -Local.Z / (Local.X * HalfH));
    return true;
}

FVector APYRBattleCamera::FinalLocation() const
{
    return Aim + Pan - FRotator(-PitchDown, Yaw, 0).Vector() * Distance;
}

void APYRBattleCamera::Frame(const FVector& InCenter, float InRadius, float InYaw, bool bGym)
{
    Center = InCenter;
    Radius = InRadius;
    Yaw = InYaw;
    PitchDown = bGym ? GymPitch : RingPitch;
    Pan = FVector::ZeroVector;
    GetCameraComponent()->SetFieldOfView(Fov);
    const FRotator Rot(-PitchDown, Yaw, 0);
    const FVector Fwd2D = FRotator(0, Yaw, 0).Vector();

    // What must fit: 24 rim points on the ground, the same points 2 m up (a flier or a grown Pokemon standing on the
    // rim), and Onix's height in the gym.
    TArray<FVector> Points;
    // The gym frames its whole ring of stones too (m4u: at 70% of it the near edge ran off the bottom of the picture, the
    // blind judge's Pewter Gym battle; look.json: "keeping the whole ring, Red and every shadow in view"), and the pan
    // follows the fighters further.
    const float FitRadius = Radius;
    PanLimit = bGym ? 450.f : MaxPan;
    for (int32 I = 0; I < 24; ++I)
    {
        const float A = I * 2.f * PI / 24;
        const FVector P = Center + FVector(FMath::Cos(A), FMath::Sin(A), 0) * FitRadius;
        Points.Add(P);
        Points.Add(P + FVector(0, 0, 200.f));
        if (bGym) Points.Add(Center + (P - Center) * .5f + FVector(0, 0, 900.f));
    }
    auto Fit = [&](float D, float Offset, float& OutMinY, float& OutMaxY, float& OutMaxX)
    {
        const FVector Loc = Center + Fwd2D * Offset - Rot.Vector() * D;
        const float HalfW = FMath::Tan(FMath::DegreesToRadians(Fov * .5f)), HalfH = HalfW / Aspect;
        OutMinY = 10, OutMaxY = -10, OutMaxX = 0;
        for (const FVector& P : Points)
        {
            const FVector L = Rot.UnrotateVector(P - Loc);
            if (L.X <= 1.f) { OutMinY = -10; OutMaxY = 10; return; }
            const float Y = -L.Z / (L.X * HalfH);
            OutMinY = FMath::Min(OutMinY, Y);
            OutMaxY = FMath::Max(OutMaxY, Y);
            OutMaxX = FMath::Max(OutMaxX, FMath::Abs(L.Y / (L.X * HalfW)));
        }
    };
    // Start close and back off until the foreshortened ring, centred vertically, fits inside 86% of the height and
    // 90% of the width (room for panning and the HUD corners). At 30 degrees the width decides, so the camera ends
    // up about 11 m from a 16 m ring instead of 25 m above it.
    float D = .5f * Radius;
    float BestOffset = 0;
    for (int32 Step = 0; Step < 160; ++Step, D *= 1.02f)
    {
        float BestSpan = 100;
        for (float Offset = -.8f * FitRadius; Offset <= .8f * FitRadius; Offset += .05f * FitRadius)
        {
            float MinY, MaxY, MaxX;
            Fit(D, Offset, MinY, MaxY, MaxX);
            const float Imbalance = FMath::Abs(MinY + MaxY);
            if (Imbalance < BestSpan) { BestSpan = Imbalance; BestOffset = Offset; }
        }
        float MinY, MaxY, MaxX;
        Fit(D, BestOffset, MinY, MaxY, MaxX);
        if (FMath::Max(-MinY, MaxY) <= .86f && MaxX <= .9f) break;
    }
    if (bGym)
    {
        // The gym camera looks over the cut-away near wall, where the east cliffs and hills would cover the arena.
        for (const TCHAR* Tag : {TEXT("Cliff"), TEXT("Backdrop")})
        {
            TArray<AActor*> Found;
            PYRWorld::FindAllTagged(GetWorld(), Tag, Found);
            for (AActor* A : Found) if (A && !A->IsHidden()) { A->SetActorHiddenInGame(true); Canopy.Add(A); }
        }
    }
    Distance = D;
    Aim = Center + Fwd2D * BestOffset;
    ClearTrees();
    UE_LOG(LogTemp, Display, TEXT("PYR_BATTLE_CAMERA radius=%.0f pitch=%.0f distance=%.0f trees cleared=%d"), Radius, PitchDown, Distance, ClearedTrees);
}

void APYRBattleCamera::ClearTrees()
{
    // m4: fights happen on the classic stages below the world, with nothing between the camera and the stage.
    ClearedTrees = 0;
}

void APYRBattleCamera::BeginSweep(const FVector& FromLocation, const FRotator& FromRotation, float InFromFov, const FVector& LowFocus,
    float LowSeconds, float RiseSeconds)
{
    FromLoc = FromLocation;
    FromRot = FromRotation;
    FromFov = InFromFov;
    LowTime = LowSeconds;
    RiseTime = RiseSeconds;
    SweepTime = 0;
    // Low pass: just behind and beside Red, at knee-to-shoulder height, looking across the grass to the centre.
    const FVector Back = -FRotator(0, Yaw, 0).Vector();
    const FVector Side = FRotator(0, Yaw + 90.f, 0).Vector();
    LowLoc = LowFocus + Back * 380.f + Side * 120.f + FVector(0, 0, 90.f);
    LowRot = (Center + FVector(0, 0, 60.f) - LowLoc).Rotation();
    SetActorLocationAndRotation(FromLoc, FromRot);
    GetCameraComponent()->SetFieldOfView(FromFov);
    // Canopy is hidden for the whole fight so the high view stays clear.
    TArray<AActor*> Found;
    PYRWorld::FindAllTagged(GetWorld(), TEXT("Canopy"), Found);
    for (AActor* A : Found) if (!A->IsHidden()) { A->SetActorHiddenInGame(true); Canopy.Add(A); }
}

void APYRBattleCamera::SnapToFinal()
{
    SweepTime = LowTime + RiseTime;
    SetActorLocationAndRotation(FinalLocation(), FRotator(-PitchDown, Yaw, 0));
    GetCameraComponent()->SetFieldOfView(Fov);
}

void APYRBattleCamera::SetCave(bool bInCave, float CeilingZ, const FVector& RingCenter, float RingRadius)
{
    bCave = bInCave;
    CaveCeilingZ = CeilingZ;
    CaveCenter = RingCenter;
    CaveRadius = RingRadius;
}

void APYRBattleCamera::CutCave()
{
    // Lines from the camera to the ring's floor (centre, rim, fighters): whatever rock they pass through above the
    // ring's middle height or outside the ring is cut away.
    TArray<FVector> Targets = Subjects;
    Targets.Add(CaveCenter + FVector(0, 0, 60.f));
    for (int32 I = 0; I < 12; ++I)
        Targets.Add(CaveCenter + FVector(FMath::Cos(I * PI / 6) * CaveRadius, FMath::Sin(I * PI / 6) * CaveRadius, 60.f));
    FCollisionObjectQueryParams Objects(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Params(TEXT("PYRCameraCave"), false, this);
    const float CutZ = CaveCenter.Z + (CaveCeilingZ - CaveCenter.Z) * .5f;
    for (const FVector& Target : Targets)
    {
        TArray<FHitResult> Hits;
        GetWorld()->LineTraceMultiByObjectType(Hits, GetActorLocation(), Target, Objects, Params);
        for (const FHitResult& Hit : Hits)
        {
            UPrimitiveComponent* Comp = Hit.GetComponent();
            const AActor* A = Hit.GetActor();
            if (!Comp || !Comp->IsVisible() || (A && (A->IsA<APawn>() || A->ActorHasTag(TEXT("WildMonBody")) || A->GetClass()->GetName().Contains(TEXT("PYRRing")))))
                continue;
            if (Hit.ImpactPoint.Z < CutZ && FVector::Dist2D(Hit.ImpactPoint, CaveCenter) < CaveRadius - 30.f) continue;  // Pillars in the ring stay.
            Comp->SetVisibility(false);
            CutAway.Add(Comp);
        }
    }
}

void APYRBattleCamera::Release()
{
    for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : CutAway) if (UPrimitiveComponent* C = Weak.Get()) C->SetVisibility(true);
    CutAway.Reset();
    bCave = false;
    ClearedTrees = 0;
    for (auto& Pair : Hidden) if (AActor* A = Pair.Key.Get()) A->SetActorHiddenInGame(false);
    Hidden.Reset();
    for (auto& Weak : Canopy) if (AActor* A = Weak.Get()) A->SetActorHiddenInGame(false);
    Canopy.Reset();
    Subjects.Reset();
}

void APYRBattleCamera::UpdateOccluders()
{
    const double Now = GetWorld()->GetTimeSeconds();
    FCollisionQueryParams Params(TEXT("PYRCameraOcclusion"), false, this);
    for (const FVector& Subject : Subjects)
    {
        TArray<FHitResult> Hits;
        GetWorld()->LineTraceMultiByChannel(Hits, GetActorLocation(), Subject, ECC_Camera, Params);
        TArray<FHitResult> Static;
        FCollisionObjectQueryParams Objects(ECC_WorldStatic);
        Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        GetWorld()->LineTraceMultiByObjectType(Static, GetActorLocation(), Subject, Objects, Params);
        Hits.Append(Static);
        for (const FHitResult& Hit : Hits)
        {
            AActor* A = Hit.GetActor();
            if (A && (A->ActorHasTag(TEXT("Occluder")) || A->ActorHasTag(TEXT("Trunk"))))
            {
                A->SetActorHiddenInGame(true);
                Hidden.Add(A, Now);
            }
        }
    }
    for (auto It = Hidden.CreateIterator(); It; ++It)
    {
        AActor* A = It.Key().Get();
        if (!A) { It.RemoveCurrent(); continue; }
        if (Now - It.Value() > .6) { A->SetActorHiddenInGame(false); It.RemoveCurrent(); }
    }
}

void APYRBattleCamera::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float Total = LowTime + RiseTime;
    if (SweepTime < Total)
    {
        SweepTime += DeltaSeconds;
        FVector Loc;
        FRotator Rot;
        float FovNow;
        if (SweepTime < LowTime)
        {
            const float A = FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp(SweepTime / LowTime, 0.f, 1.f), 2.f);
            Loc = FMath::Lerp(FromLoc, LowLoc, A);
            Rot = FQuat::Slerp(FromRot.Quaternion(), LowRot.Quaternion(), A).Rotator();
            FovNow = FMath::Lerp(FromFov, 75.f, A);
        }
        else
        {
            const float A = FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp((SweepTime - LowTime) / RiseTime, 0.f, 1.f), 2.f);
            Loc = FMath::Lerp(LowLoc, FinalLocation(), A);
            Rot = FQuat::Slerp(LowRot.Quaternion(), FRotator(-PitchDown, Yaw, 0).Quaternion(), A).Rotator();
            FovNow = FMath::Lerp(75.f, Fov, A);
        }
        SetActorLocationAndRotation(Loc, Rot);
        GetCameraComponent()->SetFieldOfView(FovNow);
        if (IsCave()) CutCave();
        return;
    }
    // Gentle pan toward the middle of everything that matters; the yaw and pitch never change.
    if (Subjects.Num())
    {
        FVector Mid = FVector::ZeroVector;
        for (const FVector& P : Subjects) Mid += P;
        Mid /= Subjects.Num();
        FVector Want = (Mid - Center) * .5f;
        Want.Z = 0;
        Want = Want.GetClampedToMaxSize(PanLimit);
        Pan = FMath::VInterpTo(Pan, Want, DeltaSeconds, 1.5f);
    }
    SetActorLocationAndRotation(FinalLocation(), FRotator(-PitchDown, Yaw, 0));
    OccluderClock -= DeltaSeconds;
    if (OccluderClock <= 0) { OccluderClock = .2f; UpdateOccluders(); if (IsCave()) CutCave(); }
}
