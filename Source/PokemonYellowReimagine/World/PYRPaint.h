// The painted palette and the primitive shapes Kanto, the people and Pikachu are built from:
// /Engine/BasicShapes meshes with one shared BasicShapeMaterial dynamic instance per colour.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PYRPaint.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;

namespace PYRColor
{
    inline FLinearColor Hex(uint32 Rgb) { return FLinearColor::FromSRGBColor(FColor((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255)); }
    inline FLinearColor Shade(const FLinearColor& C, float Factor) { return FLinearColor(C.R * Factor, C.G * Factor, C.B * Factor, 1.f); }
    inline FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T) { return FLinearColor::LerpUsingHSV(A, B, T); }
    // m2: the bright original-TV-anime palette (#ffd733, #e3342f, #6ec6f0, #5cb85c, #fff4d6, #1f1f2e). The m1 names
    // are kept so every folder's primitives brighten with it.
    const FLinearColor Yellow = Hex(0xffd733);
    const FLinearColor Red = Hex(0xe3342f);
    const FLinearColor Sky = Hex(0x6ec6f0);
    const FLinearColor Green = Hex(0x5cb85c);
    const FLinearColor Cream = Hex(0xfff4d6);
    const FLinearColor Ink = Hex(0x1f1f2e);
    const FLinearColor Ochre = Hex(0xf0b440);
    const FLinearColor Sage = Green;
    const FLinearColor Blue = Hex(0x4f9fdc);
    const FLinearColor Umber = Hex(0x8a5a3a);
    const FLinearColor Pikachu = Yellow;  // Only Pikachu (and Red's cap in red) should be this bright.
    const FLinearColor Stone = Hex(0xb4b4bc);
    const FLinearColor Dirt = Hex(0xecca8a);
    const FLinearColor Skin = Hex(0xffd9b8);
    const FLinearColor Cheek = Red;
    const FLinearColor Leaf = Hex(0x3f9a4a);
    const FLinearColor Grass = Hex(0x7cc85a);
    const FLinearColor Water = Hex(0x4aa8e8);
    const FLinearColor White = Hex(0xfcfcf6);
}

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRPaint : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    // One shared material per colour per world: /Game/Look/M_Flat (flat, rough, no specular) when it exists, else
    // the engine's BasicShapeMaterial. bSway picks /Game/Look/M_Grass (the same with a wind sway).
    static UMaterialInterface* Get(const UObject* Ctx, const FLinearColor& Color, bool bSway = false);
    // m2 art pass: /Game/Look/M_PaintTex, a painted texture (/Game/Look/Textures/T_<Texture>) projected in world space
    // every TileCm (so boxes of any size tile without UVs), times Color. Null when the material or texture is missing.
    static UMaterialInterface* GetTextured(const UObject* Ctx, const FLinearColor& Color, const TCHAR* Texture, float TileCm, float Strength = 1.f);
    // Swap a painted primitive to its textured version, keeping its colour (a no-op when the texture is missing).
    static bool Texture(UPrimitiveComponent* C, const TCHAR* Texture, float TileCm, const FLinearColor* Tint = nullptr, float Strength = 1.f);
    // The colour a primitive was painted with (its material's Color parameter, or a kit surface's painted average times its
    // tint); false for other materials.
    static bool ColorOf(const UPrimitiveComponent* C, FLinearColor& Out);

    // m4v: the anime kit's tinted surfaces (World/PYRKit), one per surface, colour and tiling.
    UPROPERTY(Transient) TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> KitCache;

private:
    UPROPERTY(Transient) TMap<uint64, TObjectPtr<UMaterialInstanceDynamic>> Cache;
    UPROPERTY(Transient) TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> TexCache;
};

namespace PYR
{
    enum class EShape : uint8 { Cube, Sphere, Cylinder, Cone, Plane };
    POKEMONYELLOWREIMAGINE_API UStaticMesh* Mesh(EShape Shape);
    // A painted primitive under Parent (or the owner's root). Size is in cm (the shape's full extent), Location and
    // Rotation are relative to Parent. Collide: BlockAll, else no collision.
    POKEMONYELLOWREIMAGINE_API UStaticMeshComponent* Shape(AActor* Owner, USceneComponent* Parent, EShape Shape, const FVector& Location,
        const FVector& SizeCm, const FLinearColor& Color, bool bCollide = false, const FRotator& Rotation = FRotator::ZeroRotator);
    // An instanced primitive for repeats (grass, trees, fences): add instances with Instance().
    POKEMONYELLOWREIMAGINE_API UInstancedStaticMeshComponent* Instanced(AActor* Owner, EShape Shape, const FLinearColor& Color, bool bCollide, bool bShadow = true, bool bSway = false);
    POKEMONYELLOWREIMAGINE_API int32 Instance(UInstancedStaticMeshComponent* Ism, const FVector& WorldLocation, const FVector& SizeCm, const FRotator& Rotation = FRotator::ZeroRotator);
    // Automated runs (tests, shots): the world transforms Instance() placed on Ism, in order; null otherwise.
    POKEMONYELLOWREIMAGINE_API const TArray<FTransform>* PlacedInstances(const UInstancedStaticMeshComponent* Ism);
    // A plain scene component to group parts (a pose root).
    POKEMONYELLOWREIMAGINE_API USceneComponent* Group(AActor* Owner, USceneComponent* Parent, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator);
    // Load an optional asset by package path (/Game/.../Name); null when missing.
    POKEMONYELLOWREIMAGINE_API UObject* LoadOptional(UClass* Class, const FString& PackagePath);
    // Play /Game/Audio/SFX/<Id> (owned by encounters) if it exists.
    POKEMONYELLOWREIMAGINE_API void Sfx(const UObject* Ctx, const TCHAR* Id, const FVector* At = nullptr);
    // Fit a generated static character (SM_<id>) under Parent: uniform scale to HeightCm, feet at FeetZ, centred. A generated
    // prop (/Game/Props/) is drawn through the anime kit (m4v, PYRKit::ApplyProp); characters keep their own materials.
    POKEMONYELLOWREIMAGINE_API UStaticMeshComponent* FitStaticBody(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, float HeightCm, float FeetZ, float Yaw);
}
