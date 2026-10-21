#include "World/PYRPaint.h"
#include "World/PYRKit.h"

#include "GameTest.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "UObject/StrongObjectPtr.h"

UMaterialInterface* UPYRPaint::Get(const UObject* Ctx, const FLinearColor& Color, bool bSway)
{
    static TStrongObjectPtr<UMaterialInterface> Engine(LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
    static TStrongObjectPtr<UMaterialInterface> Flat(Cast<UMaterialInterface>(PYR::LoadOptional(UMaterialInterface::StaticClass(), TEXT("/Game/Look/M_Flat"))));
    static TStrongObjectPtr<UMaterialInterface> Sway(Cast<UMaterialInterface>(PYR::LoadOptional(UMaterialInterface::StaticClass(), TEXT("/Game/Look/M_Grass"))));
    UMaterialInterface* Base = bSway && Sway.IsValid() ? Sway.Get() : Flat.IsValid() ? Flat.Get() : Engine.Get();
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    UPYRPaint* Self = World ? World->GetSubsystem<UPYRPaint>() : nullptr;
    if (!Self || !Base) return Base;
    const uint64 Key = uint64(Color.ToFColor(false).ToPackedARGB()) | (uint64(Base == Sway.Get()) << 32);
    if (TObjectPtr<UMaterialInstanceDynamic>* Found = Self->Cache.Find(Key)) return *Found;
    UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Self);
    Mid->SetVectorParameterValue(TEXT("Color"), Color);
    Self->Cache.Add(Key, Mid);
    return Mid;
}

UMaterialInterface* UPYRPaint::GetTextured(const UObject* Ctx, const FLinearColor& Color, const TCHAR* Texture, float TileCm, float Strength)
{
    static TStrongObjectPtr<UMaterialInterface> Base(Cast<UMaterialInterface>(PYR::LoadOptional(UMaterialInterface::StaticClass(), TEXT("/Game/Look/M_PaintTex"))));
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    UPYRPaint* Self = World ? World->GetSubsystem<UPYRPaint>() : nullptr;
    if (!Self || !Base.IsValid()) return nullptr;
    const FString Key = FString::Printf(TEXT("%s|%08x|%.0f|%.2f"), Texture, Color.ToFColor(false).ToPackedARGB(), TileCm, Strength);
    if (TObjectPtr<UMaterialInstanceDynamic>* Found = Self->TexCache.Find(Key)) return *Found;
    UTexture* Tex = Cast<UTexture>(PYR::LoadOptional(UTexture::StaticClass(), FString::Printf(TEXT("/Game/Look/Textures/T_%s"), Texture)));
    if (!Tex) return nullptr;
    UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base.Get(), Self);
    Mid->SetVectorParameterValue(TEXT("Color"), Color);
    Mid->SetTextureParameterValue(TEXT("Tex"), Tex);
    Mid->SetScalarParameterValue(TEXT("Tile"), TileCm);
    Mid->SetScalarParameterValue(TEXT("Strength"), Strength);
    Self->TexCache.Add(Key, Mid);
    return Mid;
}

bool UPYRPaint::ColorOf(const UPrimitiveComponent* C, FLinearColor& Out)
{
    UMaterialInterface* M = C ? C->GetMaterial(0) : nullptr;
    // m4v: a kit surface's colour is its painted average times its tint (World/PYRKit).
    EPYRKit Surface;
    if (M && PYRKit::SurfaceOf(M, Surface))
    {
        FLinearColor Tint = FLinearColor::White;
        M->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Tint")), Tint);
        const FLinearColor Avg = PYRKit::PaintAverage(Surface);
        Out = FLinearColor(Avg.R * Tint.R, Avg.G * Tint.G, Avg.B * Tint.B, 1.f);
        return true;
    }
    const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(M);
    return Mid && Mid->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Color")), Out);
}

bool UPYRPaint::Texture(UPrimitiveComponent* C, const TCHAR* Texture, float TileCm, const FLinearColor* Tint, float Strength)
{
    FLinearColor Color = FLinearColor::White;
    if (Tint) Color = *Tint;
    else if (!ColorOf(C, Color)) return false;
    UMaterialInterface* M = GetTextured(C, Color, Texture, TileCm, Strength);
    if (!M) return false;
    C->SetMaterial(0, M);
    return true;
}

namespace PYR
{
UStaticMesh* Mesh(EShape Shape)
{
    static const TCHAR* Paths[] = {TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("/Engine/BasicShapes/Sphere.Sphere"),
        TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("/Engine/BasicShapes/Cone.Cone"), TEXT("/Engine/BasicShapes/Plane.Plane")};
    static TStrongObjectPtr<UStaticMesh> Meshes[5];
    TStrongObjectPtr<UStaticMesh>& Slot = Meshes[uint8(Shape)];
    if (!Slot.IsValid()) Slot.Reset(LoadObject<UStaticMesh>(nullptr, Paths[uint8(Shape)]));
    return Slot.Get();
}

static void SetCollide(UPrimitiveComponent* C, bool bCollide)
{
    if (bCollide) C->SetCollisionProfileName(TEXT("BlockAll"));
    else C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

USceneComponent* Group(AActor* Owner, USceneComponent* Parent, const FVector& Location, const FRotator& Rotation)
{
    USceneComponent* C = NewObject<USceneComponent>(Owner);
    C->SetMobility(EComponentMobility::Movable);
    C->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
    C->SetRelativeLocationAndRotation(Location, Rotation);
    C->RegisterComponent();
    Owner->AddInstanceComponent(C);
    return C;
}

UStaticMeshComponent* Shape(AActor* Owner, USceneComponent* Parent, EShape Kind, const FVector& Location, const FVector& SizeCm,
    const FLinearColor& Color, bool bCollide, const FRotator& Rotation)
{
    UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
    C->SetMobility(EComponentMobility::Movable);
    C->SetStaticMesh(Mesh(Kind));
    C->SetMaterial(0, UPYRPaint::Get(Owner, Color));
    SetCollide(C, bCollide);
    C->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
    C->SetRelativeLocationAndRotation(Location, Rotation);
    C->SetRelativeScale3D(SizeCm / 100.f);
    C->RegisterComponent();
    Owner->AddInstanceComponent(C);
    return C;
}

UInstancedStaticMeshComponent* Instanced(AActor* Owner, EShape Kind, const FLinearColor& Color, bool bCollide, bool bShadow, bool bSway)
{
    UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(Owner);
    C->SetMobility(EComponentMobility::Movable);
    C->SetStaticMesh(Mesh(Kind));
    C->SetMaterial(0, UPYRPaint::Get(Owner, Color, bSway));
    SetCollide(C, bCollide);
    C->SetCastShadow(bShadow);
    C->SetupAttachment(Owner->GetRootComponent());
    C->RegisterComponent();
    Owner->AddInstanceComponent(C);
    return C;
}

namespace
{
// Automated runs only (m4s): every instance as placed, for the tests' rays. A cooked game keeps no usable CPU copy of
// an instanced mesh's instances, so the tests can't read them back from the component there.
TMap<TWeakObjectPtr<const UInstancedStaticMeshComponent>, TArray<FTransform>>& Placed()
{
    static TMap<TWeakObjectPtr<const UInstancedStaticMeshComponent>, TArray<FTransform>> Map;
    return Map;
}
}

int32 Instance(UInstancedStaticMeshComponent* Ism, const FVector& WorldLocation, const FVector& SizeCm, const FRotator& Rotation)
{
    const FTransform T(Rotation, WorldLocation, SizeCm / 100.f);
    if (GameTest::Automated()) Placed().FindOrAdd(Ism).Add(T);
    return Ism->AddInstance(T, true);
}

const TArray<FTransform>* PlacedInstances(const UInstancedStaticMeshComponent* Ism)
{
    return Placed().Find(Ism);
}

UObject* LoadOptional(UClass* Class, const FString& PackagePath)
{
    if (!FPackageName::DoesPackageExist(PackagePath)) return nullptr;
    const FString Name = FPackageName::GetShortName(PackagePath);
    return StaticLoadObject(Class, nullptr, *(PackagePath + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void Sfx(const UObject* Ctx, const TCHAR* Id, const FVector* At)
{
    USoundBase* Sound = Cast<USoundBase>(LoadOptional(USoundBase::StaticClass(), FString(TEXT("/Game/Audio/SFX/")) + Id));
    if (!Sound || !Ctx) return;
    if (At) UGameplayStatics::PlaySoundAtLocation(Ctx, Sound, *At);
    else UGameplayStatics::PlaySound2D(Ctx, Sound);
}

UStaticMeshComponent* FitStaticBody(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, float HeightCm, float FeetZ, float Yaw)
{
    if (!Mesh) return nullptr;
    const FBox Box = Mesh->GetBoundingBox();
    const float Height = FMath::Max(1.f, float(Box.GetSize().Z));
    const float Scale = HeightCm / Height;
    UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
    C->SetMobility(EComponentMobility::Movable);
    C->SetStaticMesh(Mesh);
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
    const FRotator Rotation(0, Yaw, 0);
    const FVector Base = Rotation.RotateVector(FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z)) * Scale;
    C->SetRelativeLocationAndRotation(FVector(-Base.X, -Base.Y, FeetZ - Base.Z), Rotation);
    C->SetRelativeScale3D(FVector(Scale));
    PYRKit::ApplyProp(C);
    C->RegisterComponent();
    Owner->AddInstanceComponent(C);
    return C;
}
}
