// m4v: the anime material kit (Content/Look/Kit/README.md). Every painted surface of the world, the rooms and the battle
// stages is one of these surface families: /Game/Look/Kit/MI_Kit_<Surface>, a material instance of M_AnimeKit (a painted
// texture with its ink lines, projected in world space) whose texture is /Game/Look/Kit/Textures/T_Kit_<Surface>.
// A later area picks its look by surface name: PYRKit::Apply(Component, EPYRKit::RoofRed, Colour).
//
// Colour: a surface is tinted so that its painted average becomes Colour (the place's palette colour: a Centre's red
// roof, a forest's darker grass); PYRKit::AsPainted keeps the texture's own colours. One dynamic instance per surface,
// colour and tiling per world, like UPYRPaint's flat colours, so instanced meshes still batch.
#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMeshComponent;

enum class EPYRKit : uint8
{
    WallWood, WallPlaster, WallStone, WallBrick, FloorWood, FloorTile, FloorStone, RoofRed, RoofBlue, RoofGrey, RoofBrown,
    PathDirt, PathPaving, GrassShort, GrassTall, Flowers, RockGrey, RockCave, CaveFloor, Water, Sand, TreeCanopy, TreeTrunk,
    Fence, Ledge, Sign, Counter, Carpet,
    // Beyond the brief's 28: plain painted colour (props, books, plates, clouds), window glass, machine panels, and the
    // outline ink itself (window rims, the dark past a doorway).
    Paint, Glass, Metal, Ink,
    Count
};

namespace PYRKit
{
    // Alpha 0: use the texture's own painted colours (no tint).
    const FLinearColor AsPainted(1.f, 1.f, 1.f, 0.f);

    POKEMONYELLOWREIMAGINE_API const TCHAR* Name(EPYRKit Surface);
    POKEMONYELLOWREIMAGINE_API bool FromName(const FString& Name, EPYRKit& Out);
    // /Game/Look/Kit/MI_Kit_<Name>.
    POKEMONYELLOWREIMAGINE_API FString Path(EPYRKit Surface);
    // The surface's material instance itself (null when the kit isn't authored: then callers keep their old material).
    POKEMONYELLOWREIMAGINE_API UMaterialInterface* Base(EPYRKit Surface);
    // The surface's own tiling (cm per texture repeat, the MI's Tiling).
    POKEMONYELLOWREIMAGINE_API float PaintTiling(EPYRKit Surface);
    // The texture's average linear colour (the MI's PaintAverage).
    POKEMONYELLOWREIMAGINE_API FLinearColor PaintAverage(EPYRKit Surface);
    // The surface tinted to Colour (alpha 0: as painted) with an optional tiling override (cm, 0: the surface's own).
    POKEMONYELLOWREIMAGINE_API UMaterialInterface* Get(const UObject* Ctx, EPYRKit Surface, const FLinearColor& Colour, float TilingCm = 0.f);
    // Sets every material slot of C; false when the kit is missing.
    POKEMONYELLOWREIMAGINE_API bool Apply(UPrimitiveComponent* C, EPYRKit Surface, const FLinearColor& Colour, float TilingCm = 0.f);
    // A generated prop (/Game/Props/<id>/SM_<id>) drawn through the kit: /Game/Look/Kit/Props/MI_KitProp_<id>, its own
    // picture on its own UVs with the kit's flat tones. False for anything that isn't a prop.
    POKEMONYELLOWREIMAGINE_API bool ApplyProp(UStaticMeshComponent* C);
    // The material asset at the root of M's parent chain (a dynamic instance's material).
    POKEMONYELLOWREIMAGINE_API const UMaterialInterface* RootAsset(const UMaterialInterface* M);
    // M is the kit's (its root asset under /Game/Look/Kit/).
    POKEMONYELLOWREIMAGINE_API bool IsKit(const UMaterialInterface* M);
    // The surface M paints (false for props and anything else).
    POKEMONYELLOWREIMAGINE_API bool SurfaceOf(const UMaterialInterface* M, EPYRKit& Out);
}
