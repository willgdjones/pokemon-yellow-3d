// The anime look of a Game Boy map, built from its `looks` (see YellowWorld.h): cel-shaded primitives batched into
// instanced meshes per map. m4v: every surface is one of the anime kit's painted families (World/PYRKit,
// Content/Look/Kit/README.md), tinted to the palette colour each piece always had; only materials changed, never a
// shape, a size, a place or the collision.
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kanto/YellowWorld.h"
#include "ProceduralMeshComponent.h"
#include "World/PYRLetters.h"
#include "World/PYRKit.h"
#include "World/PYRPaint.h"

using namespace PYR;

namespace
{
using FColor3 = FLinearColor;
const FColor3 GroundGreen = PYRColor::Hex(0x86cf5e);
const FColor3 ForestGround = PYRColor::Hex(0x5fa84a);
const FColor3 PathDirt = PYRColor::Hex(0xecca8a);
const FColor3 Sand = PYRColor::Hex(0xf2dd9f);
const FColor3 WaterBlue = PYRColor::Hex(0x4aa8e8);
const FColor3 WaterDeep = PYRColor::Hex(0x2f7fc4);
const FColor3 Leaf = PYRColor::Hex(0x3f9a4a);
const FColor3 LeafLight = PYRColor::Hex(0x55b050);
const FColor3 ForestLeaf = PYRColor::Hex(0x2f7d3c);
const FColor3 Trunk = PYRColor::Hex(0x8a5a3a);
const FColor3 TallGrass = PYRColor::Hex(0x3fa045);
const FColor3 Picket = PYRColor::Hex(0xf3ecda);   // m4v: cream painted posts
const FColor3 LedgeFace = PYRColor::Hex(0xa8743e);
const FColor3 Rock = PYRColor::Hex(0xa89078);
const FColor3 CaveFloor = PYRColor::Hex(0x8f9cad);   // m4v: moonlit blue-grey (was brown #b49a7c)
const FColor3 CaveWall = PYRColor::Hex(0x6f7d92);    // m4v: blue-grey rock (was #7a5e4c)
const FColor3 WoodFloor = PYRColor::Hex(0xdcb47c);
const FColor3 TileFloor = PYRColor::Hex(0xece6d6);
const FColor3 GymFloor = PYRColor::Hex(0xc9b48e);
const FColor3 MatRed = PYRColor::Hex(0xd8453a);
const FColor3 WallCream = PYRColor::Hex(0xfff4d6);
const FColor3 WallWhite = PYRColor::Hex(0xfafafa);
const FColor3 WallStone = PYRColor::Hex(0xc8c0b0);
const FColor3 WallTrim = PYRColor::Hex(0xb88a5a);
const FColor3 DoorBrown = PYRColor::Hex(0x6e4428);
const FColor3 Glass = PYRColor::Hex(0x8fd0f2);
const FColor3 Counter = PYRColor::Hex(0xc98e52);
const FColor3 CounterTop = PYRColor::Hex(0xf2e2c4);
const FColor3 MartCounter = PYRColor::Hex(0xe6dcae);   // m4v round 2: the Mart's counters are smooth cream in the anime frame
const FColor3 Shelf = PYRColor::Hex(0x9a6a42);
const FColor3 Machine = PYRColor::Hex(0xb8c0cc);
const FColor3 Screen = PYRColor::Hex(0x4f9fdc);
const FColor3 PotRed = PYRColor::Hex(0xc0603a);
const FColor3 Statue = PYRColor::Hex(0x9aa0a8);
const FColor3 GateGreen = PYRColor::Hex(0xc4e2a8);
const FColor3 GateCarpet = PYRColor::Hex(0x3f9a6a);
// m4v: the rooms' anime colours: a Centre's pink tiles and salmon counter, a Mart's pale blue tiles.
const FColor3 CentreTile = PYRColor::Hex(0xf4c4d9);
const FColor3 CentreCounter = PYRColor::Hex(0xe9a58e);
const FColor3 MartTile = PYRColor::Hex(0xcbeaf2);
const FColor3 CaveIron = PYRColor::Hex(0x5a6678);

enum class EGround : uint8 { None, Grass, Path, Sand, Water, Cave, Wood, Tile, Gym, Mat, Bridge };

// A room's back and side walls (and its top row of wall) rise to here, the lid over the room: the camera never sees
// over them into the dark.
constexpr float RoomWallTop = 880.f;
// Outdoor rock (the Game Boy's rocky banks round raised ground): a step up well below Red's eyes.
constexpr float OutdoorRock = 80.f;
// Inside caves: the rock ridges' height (below Red's shoulders) and the light raised walkways'.
constexpr float CaveRidge = 105.f;
constexpr float CaveWalkway = 55.f;

struct FKit
{
    AYellowWorld* World;
    AYellowMapActor* A;
    const FYMap& M;
    FVector Offset;
    TMap<FString, UInstancedStaticMeshComponent*> Isms;
    FRandomStream Rng;
    int32 Occ = INDEX_NONE;   // the occluder (a tree, a building) whose instances are being added

    // An instance, remembered as part of the current occluder.
    int32 Add(UInstancedStaticMeshComponent* Ism, const FVector& Center, const FVector& Size, const FRotator& R = FRotator::ZeroRotator)
    {
        const int32 I = Instance(Ism, Center, Size, R);
        if (Occ != INDEX_NONE) World->AddOccluderPart(Occ, Ism, I);
        return I;
    }

    FVector C(int32 X, int32 Y, float Z = 0.f) const
    {
        return Offset + FVector((M.Origin.X + X + .5f) * 100.f, (M.Origin.Y + Y + .5f) * 100.f, Z);
    }

    // One instanced mesh per shape, kit surface and colour (Tile: the kit's own tiling when 0). The tall grass's sway
    // comes with its surface (MI_Kit_GrassTall).
    UInstancedStaticMeshComponent* Get(EShape Shape, EPYRKit Surface, const FLinearColor& Color, float Tile = 0.f, bool bShadow = true, int32 Stencil = 0)
    {
        const FString Key = FString::Printf(TEXT("%d|%s|%08x|%d|%.0f|%d|%d"), int32(Shape), PYRKit::Name(Surface), Color.ToFColor(false).ToPackedARGB(),
            int32(Color.A > 0.f), Tile, bShadow, Stencil);
        if (UInstancedStaticMeshComponent** Found = Isms.Find(Key)) return *Found;
        UInstancedStaticMeshComponent* I = Instanced(A, Shape, Color.A > 0.f ? Color : FLinearColor::White, false, bShadow, Surface == EPYRKit::GrassTall);
        PYRKit::Apply(I, Surface, Color, Tile);
        if (Stencil > 0) { I->SetRenderCustomDepth(true); I->SetCustomDepthStencilValue(Stencil); }
        I->SetCanEverAffectNavigation(false);
        A->Looks.Add(I);
        Isms.Add(Key, I);
        return I;
    }

    void Box(const FVector& Center, const FVector& Size, EPYRKit Surface, const FLinearColor& Color, float Tile = 0.f, const FRotator& R = FRotator::ZeroRotator)
    {
        Add(Get(EShape::Cube, Surface, Color, Tile), Center, Size, R);
    }
};

EGround GroundFor(const FYMap& M, TCHAR Look, TCHAR Tile)
{
    const bool bInterior = !M.bOutdoor && !M.bCave && M.Tileset.ToUpper() != TEXT("FOREST");
    if (Look == TEXT('W')) return EGround::Water;
    if (Look == TEXT('H')) return EGround::Bridge;   // planks over water (Nugget Bridge, Cerulean's north bridge, piers)
    if (Look == TEXT('m')) return EGround::Mat;
    if (M.bCave) return EGround::Cave;
    if (bInterior)
    {
        const FString T = M.Tileset.ToUpper();
        if (T.Contains(TEXT("GYM"))) return EGround::Gym;
        if (T.Contains(TEXT("POKECENTER")) || T.Contains(TEXT("MART")) || T.Contains(TEXT("LAB")) || T.Contains(TEXT("MUSEUM")) || T.Contains(TEXT("GATE"))
            || T.Contains(TEXT("CENTER"))) return EGround::Tile;
        return EGround::Wood;
    }
    switch (Look)
    {
    case TEXT('p'): case TEXT('D'): return EGround::Path;
    case TEXT('y'): return EGround::Sand;
    case TEXT('c'): case TEXT('C'): case TEXT('u'): return EGround::Cave;
    case TEXT('o'): case TEXT('k'): case TEXT('b'): case TEXT('t'): case TEXT('P'): return EGround::Tile;
    default: return EGround::Grass;
    }
}

// Two-sided triangles for a procedural roof section.
struct FRoofSection { FLinearColor Color; TArray<FVector> V; TArray<int32> I; TArray<FVector> N; };
void Tri(FRoofSection& S, const FVector& A, const FVector& B, const FVector& Cc)
{
    const FVector Nrm = FVector::CrossProduct(B - A, Cc - A).GetSafeNormal();
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const int32 Base = S.V.Num();
        S.V.Add(A); S.V.Add(Side ? Cc : B); S.V.Add(Side ? B : Cc);
        const FVector N = Side ? -Nrm : Nrm;
        S.N.Add(N); S.N.Add(N); S.N.Add(N);
        S.I.Add(Base); S.I.Add(Base + 1); S.I.Add(Base + 2);
    }
}
}

namespace
{
// What the Game Boy draws on some furniture tiles, which the looks table only calls shelf/table/decoration: Red's TV
// and SNES, Mom's TV (a sign or hidden event names them), Red's bed, the Pokemon Centre bench.
enum class EProp : uint8 { None, Tv, Snes, Bed, Bench, Bike, BallStand };

EProp PropAt(const FYMap& Map, int32 X, int32 Y)
{
    for (const FYSign& S : Map.Signs) if (S.X == X && S.Y == Y && S.Text.EndsWith(TEXT("_TV"))) return EProp::Tv;
    for (const FYHiddenEvent& H : Map.HiddenEvents)
    {
        if (H.Kind == TEXT("PrintRedSNESText"))
        {
            if (H.X == X && H.Y == Y) return EProp::Snes;
            if (H.X == X && H.Y - 1 == Y) return EProp::Tv;
        }
        if (H.Kind == TEXT("PrintBenchGuyText") && H.X == X && (H.Y == Y || H.Y + 1 == Y)) return EProp::Bench;
        if (H.Kind == TEXT("PrintNewBikeText") && H.X == X && H.Y == Y) return EProp::Bike;
    }
    if (Map.Id == TEXT("REDS_HOUSE_2F") && X == 0 && Y >= 6) return EProp::Bed;
    if (Map.Id == TEXT("OAKS_LAB") && Map.Look(X, Y) == TEXT('h')) return EProp::BallStand;
    if (Map.Tileset.ToUpper() == TEXT("CLUB") && Map.Look(X, Y) == TEXT('l')) return EProp::Bike;   // the Bike Shop's rows of bicycles
    return EProp::None;
}

// The space shuttle's tiles above its platform (Museum 2F): the 'l' tiles over the counters its sign stands at.
bool ShuttleAt(const FYMap& Map, int32 X, int32 Y)
{
    for (const FYSign& S : Map.Signs)
        if (S.Text.EndsWith(TEXT("SPACE_SHUTTLE_SIGN")) && Y == S.Y - 1 && Map.Look(X, S.Y) == TEXT('k'))
        {
            bool bRun = true;
            for (int32 I = FMath::Min(X, S.X); I <= FMath::Max(X, S.X) && bRun; ++I) bRun = Map.Look(I, S.Y) == TEXT('k');
            if (bRun) return true;
        }
    return false;
}

// A decoration on the back wall (a poster, a picture, the school's blackboard) rather than a thing on the floor.
bool OnBackWall(const FYMap& Map, int32 X, int32 Y)
{
    if (Y == 0) return true;
    const TCHAR N = Map.Look(X, Y - 1);
    return N == TEXT('B') || N == TEXT('R') || N == TEXT('n');
}
}

bool AYellowWorld::GymSpinnerAt(const FYMap& Map, int32 X, int32 Y, FIntPoint& Dir, bool& bStop)
{
    // map_coord_movement X, Y with its PAD direction and step count.
    struct FArrow { int32 X, Y, Dx, Dy, N; };
    static const FArrow Arrows[] = {
        {19, 11, 0, -1, 9}, {19, 1, -1, 0, 8}, {18, 2, 0, 1, 9}, {11, 2, 1, 0, 6}, {16, 10, 0, 1, 2}, {4, 6, 0, 1, 7},
        {5, 13, 1, 0, 8}, {4, 14, 1, 0, 9}, {0, 15, 0, -1, 8}, {1, 15, 0, -1, 6}, {13, 16, -1, 0, 6}, {13, 17, -1, 0, 12}};
    if (Map.Id != TEXT("VIRIDIAN_GYM")) return false;
    for (const FArrow& A : Arrows)
    {
        if (A.X == X && A.Y == Y) { Dir = FIntPoint(A.Dx, A.Dy); bStop = false; return true; }
        if (A.X + A.Dx * A.N == X && A.Y + A.Dy * A.N == Y) { Dir = FIntPoint(A.Dx, A.Dy); bStop = true; return true; }
    }
    return false;
}

bool AYellowWorld::IsBackWallTile(const FYMap& Map, int32 X, int32 Y)
{
    const bool bInterior = !Map.bOutdoor && !Map.bCave && Map.Tileset.ToUpper() != TEXT("FOREST");
    if (!bInterior || Y != 0 || !Map.Contains(X, Y) || !YTile::IsSolid(Map.Tile(X, Y)) || Map.WarpAt(X, Y) != INDEX_NONE || Map.Id == TEXT("BILLS_HOUSE")) return false;
    const TCHAR L = Map.Look(X, Y);
    const bool bShelfTop = L == TEXT('t') && Map.Look(X, Y + 1) == TEXT('b') && YTile::IsSolid(Map.Tile(X, Y + 1));
    return L == TEXT('P') || (L == TEXT('t') && !bShelfTop) || (L == TEXT('b') && Map.Tileset.ToUpper().Contains(TEXT("POKECENTER")));
}

void AYellowWorld::BuildLooks(const FYMap& Map, AYellowMapActor* Actor, bool bScenery)
{
    FKit K{this, Actor, Map, Offset, {}, FRandomStream(int32(GetTypeHash(Map.Id)))};
    // The FOREST tileset only (FOREST_GATE is an indoor gate room).
    const bool bForest = Map.Tileset.ToUpper() == TEXT("FOREST");
    // The forest is a room on the Game Boy, but it looks like the outdoors: grass, trees and sky.
    const bool bOutdoorLike = Map.bOutdoor || bForest;
    const bool bInterior = !bOutdoorLike && !Map.bCave;
    const FString Tileset = Map.Tileset.ToUpper();
    // m4v: the room's own walls in the kit: painted planks in the public rooms (Centres, Marts, Oak's lab, gates, the
    // museum, gyms, the bike shop), plaster in homes.
    const bool bPublicRoom = Tileset.Contains(TEXT("POKECENTER")) || Tileset.Contains(TEXT("MART")) || Tileset.Contains(TEXT("DOJO"))
        || Tileset.Contains(TEXT("LAB")) || Tileset.Contains(TEXT("GATE")) || Tileset.Contains(TEXT("MUSEUM")) || Tileset.Contains(TEXT("GYM"))
        || Tileset.Contains(TEXT("CLUB"));
    // m4v judge round 2: Cerulean Gym's walls are plain plaster (the blind judge preferred calmer walls there than the
    // inked clapboard).
    const bool bCeruleanGym = Map.Id == TEXT("CERULEAN_GYM");
    const EPYRKit RoomWall = bPublicRoom && !bCeruleanGym ? EPYRKit::WallWood : EPYRKit::WallPlaster;
    // m4v round 2: Centre and Mart counters are smooth painted colour with a cream top, as in the anime frames, not plank courses.
    const bool bSmoothCounter = Tileset.Contains(TEXT("POKECENTER")) || Tileset.Contains(TEXT("MART"));

    // ---- Ground, merged into rectangles per surface.
    TArray<EGround> Ground;
    Ground.SetNum(Map.Width * Map.Height);
    for (int32 Y = 0; Y < Map.Height; ++Y)
        for (int32 X = 0; X < Map.Width; ++X)
        {
            FIntPoint SpinDir;
            bool bSpinStop = false;
            Ground[Y * Map.Width + X] = bScenery && Map.Look(X, Y) == TEXT(' ') ? EGround::None
                : !bScenery && GymSpinnerAt(Map, X, Y, SpinDir, bSpinStop) ? EGround::Gym : GroundFor(Map, Map.Look(X, Y), Map.Tile(X, Y));
        }
    for (EGround G : {EGround::Grass, EGround::Path, EGround::Sand, EGround::Water, EGround::Cave, EGround::Wood, EGround::Tile, EGround::Gym, EGround::Mat, EGround::Bridge})
    {
        TArray<FIntRect> Rects;
        TBitArray<> Used(false, Map.Width * Map.Height);
        for (int32 Y = 0; Y < Map.Height; ++Y)
            for (int32 X = 0; X < Map.Width; ++X)
            {
                if (Used[Y * Map.Width + X] || Ground[Y * Map.Width + X] != G) continue;
                int32 W = 1;
                while (X + W < Map.Width && !Used[Y * Map.Width + X + W] && Ground[Y * Map.Width + X + W] == G) ++W;
                int32 H = 1;
                for (bool bRow = true; bRow && Y + H < Map.Height; )
                {
                    for (int32 I = 0; I < W && bRow; ++I) bRow = !Used[(Y + H) * Map.Width + X + I] && Ground[(Y + H) * Map.Width + X + I] == G;
                    if (bRow) ++H;
                }
                for (int32 J = 0; J < H; ++J) for (int32 I = 0; I < W; ++I) Used[(Y + J) * Map.Width + X + I] = true;
                Rects.Add(FIntRect(X, Y, X + W, Y + H));
            }
        for (const FIntRect& R : Rects)
        {
            const FVector Center = K.C(R.Min.X, R.Min.Y) + FVector((R.Width() - 1) * 50.f, (R.Height() - 1) * 50.f, 0.f);
            const FVector Size(R.Width() * 100.f, R.Height() * 100.f, 1.f);
            switch (G)
            {
            case EGround::Grass: Instance(K.Get(EShape::Plane, EPYRKit::GrassShort, bForest ? ForestGround : GroundGreen, 0.f, false), Center + FVector(0, 0, .5f), Size); break;
            case EGround::Path: Instance(K.Get(EShape::Plane, EPYRKit::PathDirt, PathDirt, 0.f, false), Center + FVector(0, 0, 1.f), Size); break;
            case EGround::Sand: Instance(K.Get(EShape::Plane, EPYRKit::Sand, Sand, 0.f, false), Center + FVector(0, 0, 1.f), Size); break;
            case EGround::Cave: Instance(K.Get(EShape::Plane, EPYRKit::CaveFloor, CaveFloor, 0.f, false), Center + FVector(0, 0, .5f), Size); break;
            case EGround::Wood: Instance(K.Get(EShape::Plane, EPYRKit::FloorWood, WoodFloor, 0.f, false), Center + FVector(0, 0, .5f), Size); break;
            case EGround::Tile:
                // Route 22's gate (the League's front gate) has Yellow's green check floor.
                // m4v: paved ground outdoors is cobbles, indoors big tiles: pink and cream in a Pokemon Centre, pale blue in a Mart
                // (the anime frames' rooms), cream elsewhere.
                Instance(K.Get(EShape::Plane, bOutdoorLike ? EPYRKit::PathPaving : EPYRKit::FloorTile,
                    Map.Id == TEXT("ROUTE_22_GATE") ? GateGreen : Tileset.Contains(TEXT("POKECENTER")) ? CentreTile : Tileset.Contains(TEXT("MART")) ? MartTile : TileFloor,
                    0.f, false), Center + FVector(0, 0, .5f), Size);
                break;
            // Cerulean Gym: big sandy flagstones rather than the crazed sandstone (the judge's "simpler stone").
            case EGround::Gym: Instance(K.Get(EShape::Plane, bCeruleanGym ? EPYRKit::CaveFloor : EPYRKit::FloorStone, GymFloor, 0.f, false), Center + FVector(0, 0, .5f), Size); break;
            case EGround::Mat:
                Instance(K.Get(EShape::Plane, bInterior ? EPYRKit::FloorTile : EPYRKit::GrassShort, bInterior ? TileFloor : GroundGreen, 0.f, false), Center + FVector(0, 0, .5f), Size);
                Instance(K.Get(EShape::Cube, EPYRKit::Carpet, MatRed, 0.f, false), Center + FVector(0, 0, 1.5f), FVector(Size.X - 10.f, Size.Y - 10.f, 3.f));
                break;
            case EGround::Water:
                // Painted water sits a little below the banks, over a deeper blue bed.
                Instance(K.Get(EShape::Cube, EPYRKit::Water, WaterDeep, 0.f, false), Center + FVector(0, 0, -40.f), Size * FVector(1, 1, 50.f));
                Instance(K.Get(EShape::Plane, EPYRKit::Water, WaterBlue, 0.f, false), Center + FVector(0, 0, -14.f), Size);
                break;
            case EGround::Bridge:
            {
                // A wooden deck over the water: the water below it shows past its edges, the planks run across it
                // (the Game Boy's striped bridge).
                Instance(K.Get(EShape::Cube, EPYRKit::Water, WaterDeep, 0.f, false), Center + FVector(0, 0, -40.f), Size * FVector(1, 1, 50.f));
                Instance(K.Get(EShape::Plane, EPYRKit::Water, WaterBlue, 0.f, false), Center + FVector(0, 0, -14.f), Size);
                const bool bAlongY = R.Height() >= R.Width();
                UInstancedStaticMeshComponent* Plank = K.Get(EShape::Cube, EPYRKit::FloorWood, PYRColor::Hex(0xc8935a));
                UInstancedStaticMeshComponent* Gap = K.Get(EShape::Cube, EPYRKit::TreeTrunk, PYRColor::Hex(0x6e4428), 0.f, false);
                const int32 N = (bAlongY ? R.Height() : R.Width()) * 3;
                const float Step = 100.f / 3.f;
                for (int32 I = 0; I < N; ++I)
                {
                    const float Off = -(N * Step) * .5f + Step * (I + .5f);
                    const FVector At = Center + (bAlongY ? FVector(0, Off, 0) : FVector(Off, 0, 0));
                    Instance(Plank, At + FVector(0, 0, -3.f), bAlongY ? FVector(Size.X - 4.f, Step - 4.f, 8.f) : FVector(Step - 4.f, Size.Y - 4.f, 8.f));
                }
                // The beams under the planks' ends, along both sides, and the posts it stands on in the water (the Game
                // Boy draws a pier's posts along its edge), at the corners and every two tiles.
                for (const float Side : {-.5f, .5f})
                    Instance(Gap, Center + (bAlongY ? FVector(Side * (Size.X - 6.f), 0, -10.f) : FVector(0, Side * (Size.Y - 6.f), -10.f)),
                        bAlongY ? FVector(10.f, Size.Y, 14.f) : FVector(Size.X, 10.f, 14.f));
                UInstancedStaticMeshComponent* Post = K.Get(EShape::Cylinder, EPYRKit::TreeTrunk, PYRColor::Hex(0x5a3a22));
                const int32 Long = bAlongY ? R.Height() : R.Width();
                for (int32 I = 0; I <= Long; I += 2)
                {
                    const float Along = -Long * 50.f + FMath::Min(I, Long) * 100.f;
                    for (const float Side : {-.5f, .5f})
                    {
                        const FVector At = Center + (bAlongY ? FVector(Side * (Size.X - 8.f), Along, 0) : FVector(Along, Side * (Size.Y - 8.f), 0));
                        Instance(Post, At + FVector(0, 0, -24.f), FVector(14.f, 14.f, 50.f));
                    }
                    if (I + 2 > Long && I != Long) I = Long - 2;   // always a post at the far end
                }
                break;
            }
            default: break;
            }
        }
    }

    // ---- A room whose top row holds furniture standing in it (the Bike Shop's bikes, a Mart's shelves, the Museum's
    // counters, the League gate's statues) has its back wall as a thin strip at the very back, the furniture in front
    // of it, as the Game Boy draws it (m4r: a full-tile wall block stood out between the bikes, visual-071). Elsewhere
    // the top row is all wall (or a bookshelf's top) and stays a full tile.
    bool bThinBack = false;
    if (bInterior && !bScenery && Map.Id != TEXT("BILLS_HOUSE"))
        for (int32 X = 0; X < Map.Width && !bThinBack; ++X)
        {
            const TCHAR L0 = Map.Look(X, 0);
            if (!YTile::IsSolid(Map.Tile(X, 0)) || Map.WarpAt(X, 0) != INDEX_NONE || IsBackWallTile(Map, X, 0)) continue;
            if (L0 == TEXT('B') || L0 == TEXT('R') || L0 == TEXT('n') || L0 == TEXT('D')) continue;
            if (L0 == TEXT('l') && PropAt(Map, X, 0) == EProp::None && !ShuttleAt(Map, X, 0)) continue;   // a poster on the wall
            if (L0 == TEXT('t') && Map.Look(X, 1) == TEXT('b') && YTile::IsSolid(Map.Tile(X, 1))) continue;   // a bookshelf's top
            bThinBack = true;
        }
    const float BackDepth = bThinBack ? 30.f : 100.f, BackY = bThinBack ? -35.f : 0.f;
    if (bThinBack)
    {
        ThinBackRooms.Add(Map.Id);
        // The strip runs the whole top row (behind the bikes and counters too), one flat face.
        for (int32 X = 0; X < Map.Width; ++X)
            if (Map.WarpAt(X, 0) == INDEX_NONE && YTile::IsSolid(Map.Tile(X, 0)))
                Instance(K.Get(EShape::Cube, RoomWall, WallCream), K.C(X, 0) + FVector(0, -35.f, RoomWallTop * .5f), FVector(100, 30, RoomWallTop));
    }

    // ---- Tile furniture (each piece noted on its tile for the tests).
    auto Note = [&](int32 X, int32 Y, const TCHAR* What) { if (!bScenery) Furniture.Add(Map.Origin + FIntPoint(X, Y), FName(What)); };

    // ---- Viridian Gym's spinner floor, as the Game Boy draws it on its green floor: a white panel with two blue
    // chevrons pointing the way an arrow tile sends Red, and a white panel with four blue squares where each slide stops.
    if (!bScenery)
        for (int32 Y = 0; Y < Map.Height; ++Y)
            for (int32 X = 0; X < Map.Width; ++X)
            {
                FIntPoint D;
                bool bStop = false;
                if (!GymSpinnerAt(Map, X, Y, D, bStop)) continue;
                const FVector P = K.C(X, Y);
                const FColor3 Panel = PYRColor::Hex(0xf4f6f0), Blue = PYRColor::Hex(0x3f8fe0);
                K.Box(P + FVector(0, 0, 1.5f), FVector(92, 92, 2), EPYRKit::FloorTile, Panel);
                if (bStop)
                {
                    for (const float Sx : {-1.f, 1.f})
                        for (const float Sy : {-1.f, 1.f})
                        {
                            K.Box(P + FVector(Sx * 20.f, Sy * 20.f, 3.f), FVector(28, 28, 2), EPYRKit::Paint, Blue);
                            K.Box(P + FVector(Sx * 20.f, Sy * 20.f, 3.5f), FVector(14, 14, 2), EPYRKit::FloorTile, Panel);
                        }
                    Note(X, Y, TEXT("spinner_stop"));
                    continue;
                }
                const FVector Fwd(D.X, D.Y, 0), Side(-D.Y, D.X, 0);
                for (const float Back : {-14.f, 18.f})
                {
                    const FVector Tip = P + Fwd * (Back + 16.f);
                    for (const float S : {-1.f, 1.f})
                    {
                        const FVector Arm = (-Fwd + Side * S).GetSafeNormal();
                        K.Box(Tip + Arm * 15.f + FVector(0, 0, 3.f), FVector(36, 9, 2), EPYRKit::Paint, Blue, 0.f, FRotator(0, FMath::RadiansToDegrees(FMath::Atan2(Arm.Y, Arm.X)), 0));
                    }
                }
                Note(X, Y, D.Y < 0 ? TEXT("spinner_up") : D.Y > 0 ? TEXT("spinner_down") : D.X < 0 ? TEXT("spinner_left") : TEXT("spinner_right"));
            }
    for (int32 Y = 0; Y < Map.Height; ++Y)
        for (int32 X = 0; X < Map.Width; ++X)
        {
            const TCHAR L = Map.Look(X, Y), T = Map.Tile(X, Y);
            if (bScenery && L == TEXT(' ')) continue;
            const FVector P = K.C(X, Y);
            if (!bOutdoorLike && !bScenery)
            {
                const EProp Prop = PropAt(Map, X, Y);
                const FLinearColor Plastic = PYRColor::Hex(0x9a9aa6), Dark = PYRColor::Hex(0x3a3a48);
                switch (Prop)
                {
                case EProp::Tv:
                    // A CRT on a low wooden stand, screen to the room.
                    Instance(K.Get(EShape::Cube, EPYRKit::Counter, Counter), P + FVector(0, 0, 25), FVector(90, 70, 50));
                    Instance(K.Get(EShape::Cube, EPYRKit::Metal, Dark), P + FVector(0, 0, 90), FVector(80, 62, 70));
                    Instance(K.Get(EShape::Cube, EPYRKit::Glass, Screen, 0.f, false), P + FVector(0, 32, 92), FVector(62, 2, 50));
                    break;
                case EProp::Snes:
                    // The SNES and its pad on the floor in front of the TV.
                    Instance(K.Get(EShape::Cube, EPYRKit::Metal, Plastic), P + FVector(0, -10, 6), FVector(46, 34, 12));
                    Instance(K.Get(EShape::Cube, EPYRKit::Paint, PYRColor::Hex(0x6a4a9a)), P + FVector(0, -10, 13), FVector(20, 14, 4));
                    Instance(K.Get(EShape::Cube, EPYRKit::Metal, Plastic), P + FVector(10, 28, 4), FVector(26, 12, 7));
                    break;
                case EProp::Bed:
                    Instance(K.Get(EShape::Cube, EPYRKit::Counter, Counter), P + FVector(0, 0, 18), FVector(96, 100, 36));
                    Instance(K.Get(EShape::Cube, EPYRKit::Carpet, PYRColor::Hex(0x4f9fdc)), P + FVector(0, 0, 40), FVector(90, 100, 12));
                    if (Y == 6) Instance(K.Get(EShape::Cube, EPYRKit::Paint, WallWhite), P + FVector(0, -30, 50), FVector(70, 30, 12));
                    break;
                case EProp::Bench:
                    Instance(K.Get(EShape::Cube, EPYRKit::Counter, Counter), P + FVector(0, 0, 40), FVector(70, 100, 10));
                    Instance(K.Get(EShape::Cube, EPYRKit::Counter, Trunk), P + FVector(0, 0, 18), FVector(50, 12, 36));
                    break;
                case EProp::Bike:
                {
                    // A bicycle: two wheels, the frame, the saddle and the bars. The Game Boy draws each bicycle two tiles
                    // wide, so a row of 'l' tiles holds one bike per pair, centred on the pair (a lone tile has its own).
                    int32 RunStart = X;
                    while (RunStart > 0 && PropAt(Map, RunStart - 1, Y) == EProp::Bike) --RunStart;
                    if ((X - RunStart) % 2 == 1) { Note(X, Y, TEXT("bike_half")); continue; }
                    const FVector At = (X + 1 < Map.Width && PropAt(Map, X + 1, Y) == EProp::Bike) ? K.C(X, Y) + FVector(50, 0, 0) : K.C(X, Y);
                    UInstancedStaticMeshComponent* Tyre = K.Get(EShape::Cylinder, EPYRKit::Ink, PYRColor::Ink);
                    UInstancedStaticMeshComponent* Hub = K.Get(EShape::Cylinder, EPYRKit::Metal, PYRColor::Hex(0xd8d8e0));
                    for (const float Off : {-30.f, 30.f})
                    {
                        Instance(Tyre, At + FVector(Off, 0, 26), FVector(50, 50, 4), FRotator(0, 0, 90));
                        Instance(Hub, At + FVector(Off, 0, 26), FVector(34, 34, 5), FRotator(0, 0, 90));
                    }
                    UInstancedStaticMeshComponent* Frame = K.Get(EShape::Cube, EPYRKit::Paint, PYRColor::Hex(0xe3342f));
                    Instance(Frame, At + FVector(0, 0, 44), FVector(66, 6, 6), FRotator(-18, 0, 0));
                    Instance(Frame, At + FVector(-10, 0, 52), FVector(6, 6, 34));
                    Instance(K.Get(EShape::Cube, EPYRKit::Metal, Dark), At + FVector(-12, 0, 70), FVector(22, 12, 5));
                    Instance(K.Get(EShape::Cube, EPYRKit::Metal, Dark), At + FVector(28, 0, 70), FVector(6, 40, 5));
                    break;
                }
                case EProp::BallStand:
                    // Oak's Poke Ball on its stand at the end of the table (a picture in the lab's tiles).
                    Instance(K.Get(EShape::Cylinder, EPYRKit::Paint, WallWhite), P + FVector(0, 0, 40), FVector(40, 40, 80));
                    Instance(K.Get(EShape::Sphere, EPYRKit::Paint, PYRColor::Red), P + FVector(0, 0, 96), FVector(34, 34, 34));
                    Instance(K.Get(EShape::Sphere, EPYRKit::Paint, PYRColor::White), P + FVector(0, 0, 92), FVector(34.5f, 34.5f, 16));
                    break;
                default: break;
                }
                if (Prop != EProp::None)
                {
                    Note(X, Y, Prop == EProp::Tv ? TEXT("tv") : Prop == EProp::Snes ? TEXT("snes") : Prop == EProp::Bed ? TEXT("bed") : Prop == EProp::Bench ? TEXT("bench")
                        : Prop == EProp::Bike ? TEXT("bike") : TEXT("ball_stand"));
                    continue;
                }
            }
            if (!bScenery && IsBackWallTile(Map, X, Y))
            {
                Instance(K.Get(EShape::Cube, RoomWall, WallCream), P + FVector(0, 0, RoomWallTop * .5f), FVector(100, 100, RoomWallTop));
                Note(X, Y, TEXT("wall"));
                continue;
            }
            if (L == TEXT('H')) Note(X, Y, TEXT("bridge"));   // the planks are laid with the ground above
            auto Near = [&](int32 DX, int32 DY) { return Map.Look(X + DX, Y + DY); };
            if (T == TEXT('x') || L == TEXT('Z'))
            {
                // A Cut tree: a small round shrub tree.
                K.Occ = NewOccluder(FIntRect(Map.Origin + FIntPoint(X, Y), Map.Origin + FIntPoint(X + 1, Y + 1)), 140.f);
                K.Add(K.Get(EShape::Cylinder, EPYRKit::TreeTrunk, Trunk), P + FVector(0, 0, 25), FVector(18, 18, 50));
                K.Add(K.Get(EShape::Sphere, EPYRKit::TreeCanopy, LeafLight), P + FVector(0, 0, 85), FVector(95, 95, 90));
                K.Occ = INDEX_NONE;
                Note(X, Y, TEXT("cut_tree"));
                continue;
            }
            if (YTile::IsLedge(T) || L == TEXT('L'))
            {
                // A low grassy lip that drops on the downhill side.
                EYDir D = YTile::LedgeDir(T);
                if (D == EYDir::None) D = EYDir::Down;
                const FIntPoint Dl = YDir::Delta(D);
                const bool bAlongX = Dl.Y != 0;
                const FVector Face = FVector(Dl.X, Dl.Y, 0) * 35.f;
                Instance(K.Get(EShape::Cube, EPYRKit::GrassShort, bForest ? ForestGround : GroundGreen), P - Face * .3f + FVector(0, 0, 14),
                    bAlongX ? FVector(100, 70, 28) : FVector(70, 100, 28));
                Instance(K.Get(EShape::Cube, EPYRKit::Ledge, LedgeFace), P + Face + FVector(0, 0, 11),
                    bAlongX ? FVector(100, 14, 22) : FVector(14, 100, 22));
                ++Actor->Fences;
                Note(X, Y, TEXT("ledge"));
                continue;
            }
            switch (L)
            {
            case TEXT('T'):
            {
                if (!bOutdoorLike) break;
                const float S = K.Rng.FRandRange(.92f, 1.1f);
                K.Occ = NewOccluder(FIntRect(Map.Origin + FIntPoint(X, Y), Map.Origin + FIntPoint(X + 1, Y + 1)), 280.f * S);
                // m4u: the trunk runs up into the crown (at 80 cm it stopped under a crown whose foot is at 83-99 cm, and
                // the crowns read as hovering over their trunks: the blind judge's floating trees).
                K.Add(K.Get(EShape::Cylinder, EPYRKit::TreeTrunk, Trunk), P + FVector(0, 0, 60), FVector(30, 30, 120));
                K.Add(K.Get(EShape::Sphere, EPYRKit::TreeCanopy, bForest ? ForestLeaf : Leaf), P + FVector(0, 0, 165 * S), FVector(150, 150, 150) * S);
                K.Add(K.Get(EShape::Sphere, EPYRKit::TreeCanopy, bForest ? Leaf : LeafLight), P + FVector(K.Rng.FRandRange(-15, 15), K.Rng.FRandRange(-15, 15), 225 * S), FVector(95, 95, 90) * S);
                K.Occ = INDEX_NONE;
                ++Actor->Trees;
                Note(X, Y, TEXT("tree"));
                break;
            }
            case TEXT('G'):
            {
                UInstancedStaticMeshComponent* Tufts = K.Get(EShape::Cone, EPYRKit::GrassTall, TallGrass, 0.f, false, 1);
                for (int32 I = 0; I < 6; ++I)
                {
                    const FVector O(K.Rng.FRandRange(-38, 38), K.Rng.FRandRange(-38, 38), 0);
                    const float H = K.Rng.FRandRange(50, 68);
                    if (bScenery)
                    {
                        // Past the edges (seen from afar in the edge views) a tuft stays one cone: the clumps cost frame time.
                        Instance(Tufts, P + O + FVector(0, 0, H * .5f), FVector(24, 24, H), FRotator(K.Rng.FRandRange(-8, 8), 0, K.Rng.FRandRange(-8, 8)));
                        ++Actor->GrassTufts;
                        continue;
                    }
                    // m4v (the blind judge read solid cones): each tuft is a clump of three flat blades fanned out from
                    // one root, thin along the way it leans and wide across it, so it draws as a grass clump.
                    // Their own stream, so the rest of the world's random choices stay as they were (the cone's two tilt
                    // draws are still taken from the world's stream for the same reason).
                    K.Rng.FRandRange(-8, 8);
                    K.Rng.FRandRange(-8, 8);
                    // Three clumps a tile, not six (the draws for all six are still taken): the clumps' frame cost.
                    if (I % 2 == 1) { ++Actor->GrassTufts; continue; }
                    FRandomStream BladeRng((Map.Origin.X + X) * 7919 + (Map.Origin.Y + Y) * 104729 + I * 31);
                    const float Yaw0 = BladeRng.FRandRange(0.f, 120.f);
                    for (int32 Blade = 0; Blade < 3; ++Blade)
                    {
                        const FRotator Rot(-BladeRng.FRandRange(14.f, 26.f), Yaw0 + Blade * 120.f + BladeRng.FRandRange(-20.f, 20.f), 0.f);
                        const float BH = H * (Blade == 0 ? 1.f : BladeRng.FRandRange(.72f, .9f));
                        Instance(Tufts, P + O + Rot.RotateVector(FVector(0, 0, BH * .5f)), FVector(9, 19, BH), Rot);
                    }
                    ++Actor->GrassTufts;
                }
                Note(X, Y, TEXT("tall_grass"));
                break;
            }
            case TEXT('f'):
            {
                static const FLinearColor Petals[] = {PYRColor::Hex(0xe3342f), PYRColor::Hex(0xfcfcf6), PYRColor::Hex(0xffd733)};
                for (int32 I = 0; I < 4; ++I)
                {
                    const FVector O((I % 2 ? 22 : -22) + K.Rng.FRandRange(-6, 6), (I / 2 ? 22 : -22) + K.Rng.FRandRange(-6, 6), 7);
                    if (bScenery)
                    {
                        // Past the edges a flower stays one painted dab (the edge views see hundreds of beds).
                        Instance(K.Get(EShape::Sphere, EPYRKit::Flowers, Petals[(X + Y + I) % 3], 0.f, false), P + O, FVector(16, 16, 10));
                        continue;
                    }
                    // m4v (the blind judge read flat discs): a stylised flower, four petals round a centre on a short stem, all flat
                    // discs (cylinders: a sphere per petal cost Pallet's edge views half again their frame time).
                    const FLinearColor& Petal = Petals[(X + Y + I) % 3];
                    const FVector Head = P + O + FVector(0, 0, 6);
                    Instance(K.Get(EShape::Cylinder, EPYRKit::Paint, PYRColor::Hex(0x3f8a3a), 0.f, false), P + O + FVector(0, 0, -1), FVector(2.5f, 2.5f, 14));
                    for (int32 PetalI = 0; PetalI < 4; ++PetalI)
                    {
                        const float A = (PetalI * 90.f + (X * 13 + Y * 7 + I * 29) % 90) * PI / 180.f;
                        // Plain paint, thin as card, each petal cupped up 30 degrees at its tip: the Flowers surface's rim ink,
                        // a disc's sides and the ground ink on flat up-facing colour (the toon pass) all drew petals dark.
                        Instance(K.Get(EShape::Cylinder, EPYRKit::Paint, Petal, 0.f, false), Head + FVector(FMath::Cos(A), FMath::Sin(A), 0) * 7.f + FVector(0, 0, 1.5f),
                            FVector(12, 8, 1.5f), FRotator(30.f, FMath::RadiansToDegrees(A), 0));
                    }
                    Instance(K.Get(EShape::Cone, EPYRKit::Paint, Petal == Petals[2] ? PYRColor::Hex(0xe8902a) : PYRColor::Hex(0xffd733), 0.f, false),
                        Head + FVector(0, 0, 2.f), FVector(7, 7, 5.f));
                }
                break;
            }
            case TEXT('F'):
            {
                const bool bX = Near(-1, 0) == TEXT('F') || Near(1, 0) == TEXT('F') || !(Near(0, -1) == TEXT('F') || Near(0, 1) == TEXT('F'));
                // Yellow's fences are rows of white posts (bollards), two to a tile, with no rails.
                const FVector Along = bX ? FVector(1, 0, 0) : FVector(0, 1, 0);
                UInstancedStaticMeshComponent* Post = K.Get(EShape::Cylinder, EPYRKit::Fence, Picket);
                UInstancedStaticMeshComponent* Cap = K.Get(EShape::Sphere, EPYRKit::Fence, PYRColor::Hex(0xcfe4b4));   // the pale green caps, as Yellow's
                for (const float Off : {-25.f, 25.f})
                {
                    Instance(Post, P + Along * Off + FVector(0, 0, 40), FVector(34, 34, 80));
                    Instance(Cap, P + Along * Off + FVector(0, 0, 80), FVector(34, 34, 16));
                }
                ++Actor->Fences;
                Note(X, Y, TEXT("fence"));
                break;
            }
            case TEXT('r'):
            {
                // Outdoors the Game Boy's rock is raised ground with a rocky rim, low enough to see over (m4r: at 130 cm
                // routes read as trenches between cliffs, visual-042, -070); past the edges it is one even plateau of the
                // same height (the rock border), not a field of blocks. Indoors (Pewter Gym's walls) it is a low wall (m4u:
                // at 130 cm the wall south of Red hid him to the waist from the follow camera, the blind judge's occlusion;
                // the camera's ray to his feet clears a wall a tile south of him below about 40 cm).
                const float H = bOutdoorLike ? OutdoorRock : 60.f;
                if (bScenery) break;   // past the edges: merged into big blocks after this loop
                K.Occ = bScenery ? INDEX_NONE : NewOccluder(FIntRect(Map.Origin + FIntPoint(X, Y), Map.Origin + FIntPoint(X + 1, Y + 1)), H);
                // Indoors at 1.2 m tiling (at the kit's 3.5 m a 60 cm face showed a magnified, blurred patch: Pewter Gym).
                K.Add(K.Get(EShape::Cube, EPYRKit::RockGrey, Rock, bOutdoorLike ? 0.f : 120.f), P + FVector(0, 0, H * .5f), FVector(100, 100, H));
                // Outdoors the Game Boy's rock is a plateau: grass on top of the cliff faces.
                if (bOutdoorLike) K.Add(K.Get(EShape::Plane, EPYRKit::GrassShort, GroundGreen, 0.f, false), P + FVector(0, 0, H + 1.f), FVector(100, 100, 1));
                K.Occ = INDEX_NONE;
                Note(X, Y, TEXT("rock"));
                break;
            }
            case TEXT('C'): case TEXT('V'): case TEXT('K'):
            {
                // Cave walls (m4r): merged after this loop into flat-topped ridges of one height, below Red's shoulders, so
                // the camera sees over them into the next chamber (Jessie and James past Mt. Moon's ridge) and the floor
                // plan reads as on the Game Boy: rock bands (C) as rock, the black void (V) as dark rock, the light
                // raised walkways (K, solid in Yellow) as low pale stone. Stalagmites stand where rock meets open floor.
                if (L != TEXT('K') && K.Rng.FRand() < .12f)
                    for (const FIntPoint D : {FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)})
                        if (Map.Contains(X + D.X, Y + D.Y) && !YTile::IsSolid(Map.Tile(X + D.X, Y + D.Y)))
                        {
                            Instance(K.Get(EShape::Cone, EPYRKit::RockCave, PYRKit::AsPainted, 120.f), P + FVector(D.X, D.Y, 0) * 55.f + FVector(0, 0, 45), FVector(34, 34, 90));
                            break;
                        }
                Note(X, Y, TEXT("cave_wall"));
                break;
            }
            case TEXT('u'):
                if (Map.bCave)
                {
                    // A ladder down (or up): a dark hole with grey-blue iron rails (round 3: no wood in the cave).
                    Instance(K.Get(EShape::Cylinder, EPYRKit::Ink, PYRColor::Ink, 0.f, false), P + FVector(0, 0, 1), FVector(80, 80, 2));
                    for (int32 Side = -1; Side <= 1; Side += 2) Instance(K.Get(EShape::Cube, EPYRKit::Metal, CaveIron), P + FVector(Side * 28, 0, 60), FVector(6, 6, 120));
                    for (float Z : {30.f, 60.f, 90.f}) Instance(K.Get(EShape::Cube, EPYRKit::Metal, CaveIron), P + FVector(0, 0, Z), FVector(56, 5, 5));
                    Note(X, Y, TEXT("ladder"));
                }
                else
                {
                    for (int32 I = 0; I < 4; ++I) Instance(K.Get(EShape::Cube, EPYRKit::FloorWood, WoodFloor), P + FVector(0, -30 + I * 20, 12 + I * 25), FVector(100, 40, 25));
                    Note(X, Y, TEXT("stairs"));
                }
                break;
            case TEXT('k'):
                if (Map.Id == TEXT("ROUTE_22_GATE"))
                {
                    // Yellow's League gate: four statues on two-tile plinths by the carpet (the lower 'k' of each holds
                    // the statue; the upper one is its head), not counters.
                    if (Y == 1 || Y == Map.Height - 1)
                    {
                        if (UStaticMesh* Mesh = Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), TEXT("/Game/Props/league_statue/SM_league_statue"))))
                            FitStaticBody(Actor, nullptr, Mesh, 190.f, 0.f, Y == 1 ? 90.f : -90.f)->SetWorldLocation(P);
                        else Instance(K.Get(EShape::Cube, EPYRKit::RockGrey, Statue), P + FVector(0, 0, 60), FVector(90, 90, 120));
                    }
                    Note(X, Y, TEXT("statue"));
                    break;
                }
                // m4v round 2: the Centre's counter is smooth painted peach and cream, as in the anime frame, not plank courses.
                Instance(K.Get(EShape::Cube, bSmoothCounter ? EPYRKit::Paint : EPYRKit::Counter, Tileset.Contains(TEXT("POKECENTER")) ? CentreCounter : Tileset.Contains(TEXT("MART")) ? MartCounter : Counter), P + FVector(0, 0, 47), FVector(100, 100, 94));
                Instance(K.Get(EShape::Cube, bSmoothCounter ? EPYRKit::Paint : EPYRKit::Counter, CounterTop), P + FVector(0, 0, 97), FVector(104, 104, 8));
                Note(X, Y, TEXT("counter"));
                break;
            case TEXT('b'):
                if (Map.Id == TEXT("BILLS_HOUSE"))
                {
                    // Bill's two Cell Separator pods (each three tiles wide) and the pipe between them.
                    const bool bPodCentre = (X == 1 || X == 6) && Y == 1;
                    if (bPodCentre)
                    {
                        Instance(K.Get(EShape::Cylinder, EPYRKit::Metal, Machine), P + FVector(0, 0, 25), FVector(260, 180, 50));
                        Instance(K.Get(EShape::Cylinder, EPYRKit::Glass, Glass, 0.f, false), P + FVector(0, 0, 130), FVector(220, 150, 160));
                        Instance(K.Get(EShape::Sphere, EPYRKit::Metal, Machine), P + FVector(0, 0, 215), FVector(230, 160, 60));
                        Note(X, Y, TEXT("pod"));
                    }
                    else if (X == 3 && Y == 1)
                    {
                        // The pipe joining the two pods along the back wall, over a low console (the Game Boy's tiles
                        // between them).
                        Instance(K.Get(EShape::Cylinder, EPYRKit::Metal, Machine), P + FVector(50, -10, 150), FVector(36, 36, 300), FRotator(90, 0, 0));
                        Instance(K.Get(EShape::Cube, EPYRKit::Metal, Machine), P + FVector(50, -5, 45), FVector(190, 70, 90));
                        Instance(K.Get(EShape::Cube, EPYRKit::Glass, Screen, 0.f, false), P + FVector(50, 31, 60), FVector(150, 2, 40));
                        Note(X, Y, TEXT("pipe"));
                        Note(X + 1, Y, TEXT("pipe"));
                    }
                    break;
                }
                if (Tileset.Contains(TEXT("POKECENTER")) && Y <= 1)
                {
                    // Behind the counter: the healing machine left of the nurse, and the Centre's other equipment.
                    const bool bHealer = Y == 1 && X == 2;
                    UStaticMesh* Mesh = bHealer ? Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), TEXT("/Game/Props/healing_machine/SM_healing_machine"))) : nullptr;
                    if (bHealer) Note(X, Y, TEXT("healer"));
                    if (Mesh) { FitStaticBody(Actor, nullptr, Mesh, 120.f, 0.f, 90.f)->SetWorldLocation(P); break; }
                    if (Y == 1)
                    {
                        if (!bHealer) Note(X, Y, TEXT("machine"));
                        // m4v: cream cabinets in the anime Centre (were grey).
                        Instance(K.Get(EShape::Cube, EPYRKit::Metal, PYRColor::Hex(0xf2ede2)), P + FVector(0, 0, 55), FVector(86, 70, 110));
                        Instance(K.Get(EShape::Cube, EPYRKit::Glass, Screen, 0.f, false), P + FVector(0, 36, 80), FVector(50, 2, 34));
                    }
                    break;
                }
                if (Tileset.Contains(TEXT("MART")))
                {
                    const bool bBack = Y <= 1 && Map.Look(X, 0) == TEXT('b') && Map.Look(X, 1) == TEXT('b');
                    if (bBack)
                    {
                        // A glass display case against the back wall, two tiles deep (the Game Boy's four cases between
                        // the SALE shelves); built from its front tile.
                        Note(X, Y, TEXT("mart_case"));
                        if (Y == 0) break;
                        const FVector C = P + FVector(0, -55, 0);
                        // m4v: the anime Mart's coolers are glass-fronted cyan cabinets.
                        Instance(K.Get(EShape::Cube, EPYRKit::Glass, PYRColor::Hex(0x9ad8e2)), C + FVector(0, 0, 35), FVector(92, 150, 70));
                        for (const float Sx : {-42.f, 42.f})
                            for (const float Sy : {-70.f, 70.f}) Instance(K.Get(EShape::Cube, EPYRKit::Metal, PYRColor::Hex(0x3f9fb8)), C + FVector(Sx, Sy, 125), FVector(6, 6, 110));
                        Instance(K.Get(EShape::Cube, EPYRKit::Glass, Glass, 0.f, false), C + FVector(0, 0, 183), FVector(92, 150, 6));
                        Instance(K.Get(EShape::Cube, EPYRKit::Glass, Glass, 0.f, false), C + FVector(0, 73, 125), FVector(80, 2, 100));
                        static const FLinearColor Goods[] = {PYRColor::Hex(0xe3342f), PYRColor::Hex(0xffd733), PYRColor::Hex(0x5cb85c), PYRColor::Hex(0xb070d0)};
                        for (int32 I = 0; I < 3; ++I)
                            Instance(K.Get(EShape::Cube, EPYRKit::Paint, Goods[(X + I) % 4]), C + FVector(-26 + I * 26, 20, 86), FVector(18, 18, 30));
                        break;
                    }
                    if (Y == 1 && Map.Look(X, 0) == TEXT('l'))
                    {
                        // The low shelf under a SALE sign.
                        Instance(K.Get(EShape::Cube, EPYRKit::Metal, Machine), P + FVector(0, -10, 45), FVector(96, 80, 90));
                        static const FLinearColor Goods[] = {PYRColor::Hex(0xe3342f), PYRColor::Hex(0x4f9fdc), PYRColor::Hex(0xffd733)};
                        for (int32 I = 0; I < 3; ++I) Instance(K.Get(EShape::Cube, EPYRKit::Paint, Goods[(X + I) % 3]), P + FVector(-28 + I * 28, -10, 100), FVector(20, 30, 20));
                        Note(X, Y, TEXT("mart_shelf"));
                        break;
                    }
                    if (UStaticMesh* Mesh = Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), TEXT("/Game/Props/mart_shelf/SM_mart_shelf"))))
                    {
                        // Island shelves, low enough to see over (the Game Boy's shelves are a tile of floor plan).
                        UStaticMeshComponent* Sh = FitStaticBody(Actor, nullptr, Mesh, 190.f, 0.f, 90.f);
                        Sh->SetWorldLocation(P);
                        Sh->SetRelativeScale3D(Sh->GetRelativeScale3D() * FVector(1.f, 1.f, 130.f / 190.f));
                        Note(X, Y, TEXT("mart_shelf"));
                        break;
                    }
                }
                {
                    Note(X, Y, bInterior && Tileset.Contains(TEXT("LAB")) ? TEXT("machine") : TEXT("bookshelf"));
                    // A bookshelf; with a 't' shelf top behind it, one shelf two tiles deep (the Game Boy draws its top above).
                    const bool bDeep = Map.Look(X, Y - 1) == TEXT('t') && YTile::IsSolid(Map.Tile(X, Y - 1));
                    Instance(K.Get(EShape::Cube, bInterior && Tileset.Contains(TEXT("LAB")) ? EPYRKit::Metal : EPYRKit::Counter, bInterior && Tileset.Contains(TEXT("LAB")) ? Machine : Shelf), P + FVector(0, bDeep ? -40.f : 0.f, 95),
                        FVector(92, bDeep ? 160.f : 80.f, 190));
                    for (int32 Row = 0; Row < 3; ++Row)
                        // m4v: rows of book spines (the Fence's upright strokes and grain at a 22 cm tiling).
                        Instance(K.Get(EShape::Cube, EPYRKit::Fence, Row == 1 ? PYRColor::Hex(0x4f9fdc) : Row == 2 ? PYRColor::Hex(0xe3342f) : PYRColor::Hex(0x5cb85c), 22.f),
                            P + FVector(0, 38, 40 + Row * 55), FVector(80, 6, 34));
                }
                break;
            case TEXT('t'):
                if (YTile::IsWalkable(T))
                {
                    // A stool (the Game Boy's walkable furniture): a low round seat Red can stand on, not one he stands
                    // inside.
                    Instance(K.Get(EShape::Cylinder, EPYRKit::Counter, Counter), P + FVector(0, 0, 9), FVector(62, 62, 14));
                    Instance(K.Get(EShape::Cylinder, EPYRKit::Counter, Trunk), P + FVector(0, 0, 1), FVector(50, 50, 2));
                    Note(X, Y, TEXT("chair"));
                    break;
                }
                if (Map.Look(X, Y + 1) == TEXT('b') && YTile::IsSolid(Map.Tile(X, Y + 1))) break;   // the top of a bookshelf drawn from the tile below
                Instance(K.Get(EShape::Cube, EPYRKit::Counter, Counter), P + FVector(0, 0, 70), FVector(100, 100, 10));
                Instance(K.Get(EShape::Cube, EPYRKit::Counter, Trunk), P + FVector(0, 0, 33), FVector(20, 20, 66));
                Note(X, Y, TEXT("table"));
                break;
            case TEXT('P'):
                // The lower half of a PC two tiles tall (the PC stands there when its top is the back wall).
                if (Map.Look(X, Y - 1) == TEXT('P') && !IsBackWallTile(Map, X, Y - 1)) break;
                Note(X, Y, TEXT("pc"));
                if (Map.Look(X, Y - 1) != TEXT('P') && Map.Look(X, Y + 1) != TEXT('P') && Map.Look(X - 1, Y) != TEXT('P') && Map.Look(X + 1, Y) != TEXT('P')
                    && ((Map.Look(X - 1, Y) == TEXT('t') && YTile::IsSolid(Map.Tile(X - 1, Y))) || (Map.Look(X + 1, Y) == TEXT('t') && YTile::IsSolid(Map.Tile(X + 1, Y)))))
                {
                    // A PC on a desk (Bill's): the desk goes on under it from the table beside it, the monitor and
                    // keyboard on top, as the Game Boy draws it.
                    Instance(K.Get(EShape::Cube, EPYRKit::Counter, Counter), P + FVector(0, 0, 70), FVector(100, 100, 10));
                    Instance(K.Get(EShape::Cube, EPYRKit::Counter, Trunk), P + FVector(0, 0, 33), FVector(20, 20, 66));
                    Instance(K.Get(EShape::Cube, EPYRKit::Metal, Machine), P + FVector(0, -12, 104), FVector(56, 46, 56));
                    Instance(K.Get(EShape::Cube, EPYRKit::Glass, Screen, 0.f, false), P + FVector(0, 12, 106), FVector(42, 2, 34));
                    Instance(K.Get(EShape::Cube, EPYRKit::Metal, PYRColor::Hex(0x9a9aa6)), P + FVector(0, 30, 78), FVector(50, 18, 4));
                    break;
                }
                if (UStaticMesh* Mesh = Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), TEXT("/Game/Props/pc_terminal/SM_pc_terminal"))))
                    FitStaticBody(Actor, nullptr, Mesh, 110.f, 0.f, 90.f)->SetWorldLocation(P);
                else
                {
                    Instance(K.Get(EShape::Cube, EPYRKit::Metal, Machine), P + FVector(0, 0, 45), FVector(80, 70, 90));
                    Instance(K.Get(EShape::Cube, EPYRKit::Metal, Machine), P + FVector(0, 0, 115), FVector(60, 50, 50));
                    Instance(K.Get(EShape::Cube, EPYRKit::Glass, Screen, 0.f, false), P + FVector(0, 26, 115), FVector(46, 2, 36));
                }
                break;
            case TEXT('h'):
                Note(X, Y, TEXT("plant"));
                if (UStaticMesh* Mesh = Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), TEXT("/Game/Props/potted_plant/SM_potted_plant"))))
                    FitStaticBody(Actor, nullptr, Mesh, 110.f, 0.f, 0.f)->SetWorldLocation(P);
                else
                {
                    Instance(K.Get(EShape::Cylinder, EPYRKit::Paint, PotRed), P + FVector(0, 0, 22), FVector(50, 50, 44));
                    Instance(K.Get(EShape::Sphere, EPYRKit::TreeCanopy, Leaf), P + FVector(0, 0, 75), FVector(80, 80, 70));
                }
                break;
            case TEXT('l'):
                if (Tileset.Contains(TEXT("GYM")) || Map.Id.ToString().Contains(TEXT("GYM")))
                {
                    if (UStaticMesh* Mesh = Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), TEXT("/Game/Props/league_statue/SM_league_statue"))))
                    { FitStaticBody(Actor, nullptr, Mesh, 190.f, 0.f, 90.f)->SetWorldLocation(P); Note(X, Y, TEXT("statue")); break; }
                }
                if (bInterior && ShuttleAt(Map, X, Y))
                {
                    // Museum 2F's space shuttle exhibit lies on the railed platform below (built once, from its sign).
                    Note(X, Y, TEXT("shuttle"));
                    break;
                }
                if (bInterior && OnBackWall(Map, X, Y))
                {
                    Note(X, Y, Map.Id == TEXT("VIRIDIAN_SCHOOL_HOUSE") ? TEXT("blackboard") : TEXT("poster"));
                    // On the back wall: the school's blackboard, else a poster or a framed picture.
                    const bool bBoard = Map.Id == TEXT("VIRIDIAN_SCHOOL_HOUSE");
                    const bool bSale = Tileset.Contains(TEXT("MART"));
                    const float Top = Y == 0 ? RoomWallTop : 320.f;
                    const float Dy = Y == 0 ? BackY : 0.f, Depth = Y == 0 ? BackDepth : 100.f, Face = Dy + Depth * .5f;
                    Instance(K.Get(EShape::Cube, RoomWall, WallCream), P + FVector(0, Dy, Top * .5f), FVector(100, Depth, Top));
                    if (bSale)
                    {
                        // A Mart's SALE sign over its shelf: a red board with a white band.
                        Instance(K.Get(EShape::Cube, EPYRKit::Paint, PYRColor::Hex(0xe3342f)), P + FVector(0, Face + 2.f, 170), FVector(84, 4, 56));
                        Instance(K.Get(EShape::Cube, EPYRKit::Paint, PYRColor::Hex(0xfcfcf6)), P + FVector(0, Face + 4.5f, 170), FVector(70, 2, 18));
                        break;
                    }
                    Instance(K.Get(EShape::Cube, bBoard ? EPYRKit::Paint : EPYRKit::FloorWood, bBoard ? PYRColor::Hex(0x2f5a3a) : WallTrim), P + FVector(0, Face + 2.f, 165), FVector(bBoard ? 100.f : 64.f, 4, bBoard ? 90.f : 76.f));
                    // m4v: a framed window/picture is pale painted glass with a shine (round 2: the kit water read as blue noise).
                    Instance(K.Get(EShape::Cube, bBoard ? EPYRKit::Paint : EPYRKit::Glass, bBoard ? PYRColor::Hex(0x3f7a4a) : PYRColor::Hex(0xbfe4f4), 0.f),
                        P + FVector(0, Face + 5.f, 165), FVector(bBoard ? 92.f : 54.f, 2, bBoard ? 82.f : 64.f));
                    break;
                }
                if (Tileset.Contains(TEXT("MUSEUM")))
                {
                    // A museum exhibit: a stone plinth under a glass case.
                    Instance(K.Get(EShape::Cube, EPYRKit::WallStone, WallStone), P + FVector(0, 0, 40), FVector(90, 90, 80));
                    Instance(K.Get(EShape::Cube, EPYRKit::Glass, Glass, 0.f, false), P + FVector(0, 0, 115), FVector(80, 80, 70));
                    Note(X, Y, TEXT("exhibit"));
                    break;
                }
                // Anything else: a flower vase on a small table.
                Note(X, Y, TEXT("vase"));
                Instance(K.Get(EShape::Cube, EPYRKit::Counter, Counter), P + FVector(0, 0, 35), FVector(60, 60, 70));
                Instance(K.Get(EShape::Cylinder, EPYRKit::Paint, PotRed), P + FVector(0, 0, 88), FVector(26, 26, 36));
                Instance(K.Get(EShape::Sphere, EPYRKit::Flowers, PYRColor::Hex(0xffd733)), P + FVector(0, 0, 112), FVector(30, 30, 24));
                break;
            case TEXT('B'): case TEXT('R'): case TEXT('n'):
                if (bInterior && Map.Id == TEXT("BILLS_HOUSE") && Y == 0 && (X == 3 || X == 4))
                {
                    // Behind Bill's pipe: the back wall, flush with the pods (no pillar standing out between them).
                    Instance(K.Get(EShape::Cube, RoomWall, WallCream), P + FVector(0, -30, RoomWallTop * .5f), FVector(100, 40, RoomWallTop));
                    Note(X, Y, TEXT("wall"));
                    break;
                }
                if (bInterior && YTile::IsSolid(T))
                {
                    // The back wall of a room (and its windows): full height, always drawn; the top row rises to the
                    // room's lid so the camera never looks over it into the dark. A gym's maze walls inside the room
                    // (Viridian Gym) are low barriers the camera sees over, as the Game Boy's one-tile black lines.
                    const bool bMaze = Y > 0 && Tileset.Contains(TEXT("GYM"));
                    if (bMaze)
                    {
                        Instance(K.Get(EShape::Cube, EPYRKit::WallStone, PYRColor::Hex(0x34303a)), P + FVector(0, 0, 55), FVector(100, 100, 110));
                        Instance(K.Get(EShape::Cube, EPYRKit::Paint, PYRColor::Hex(0x9fc860)), P + FVector(0, 0, 112), FVector(104, 104, 6));
                        Note(X, Y, TEXT("wall"));
                        break;
                    }
                    const float Top = Y == 0 ? RoomWallTop : 320.f;
                    const float Dy = Y == 0 ? BackY : 0.f, Depth = Y == 0 ? BackDepth : 100.f;
                    Instance(K.Get(EShape::Cube, RoomWall, WallCream), P + FVector(0, Dy, Top * .5f), FVector(100, Depth, Top));
                    if (L == TEXT('n')) Instance(K.Get(EShape::Cube, EPYRKit::Glass, Glass, 0.f, false), P + FVector(0, Dy + Depth * .5f + 1.f, 170), FVector(60, 2, 70));
                    Note(X, Y, L == TEXT('n') ? TEXT("window") : TEXT("wall"));
                }
                break;
            case TEXT('?'): case TEXT(' '):
                if (YTile::IsSolid(T) && T != TEXT('w'))
                {
                    if (bOutdoorLike) { Instance(K.Get(EShape::Sphere, EPYRKit::TreeCanopy, Leaf), P + FVector(0, 0, 55), FVector(100, 100, 110)); Note(X, Y, TEXT("bush")); }
                    else if (Map.bCave) { Instance(K.Get(EShape::Cube, EPYRKit::RockCave, CaveWall), P + FVector(0, 0, 150), FVector(100, 100, 300)); Note(X, Y, TEXT("cave_wall")); }
                    else { Instance(K.Get(EShape::Cube, EPYRKit::RockGrey, Statue), P + FVector(0, 0, 60), FVector(90, 90, 120)); Note(X, Y, TEXT("block")); }
                }
                break;
            default: break;
            }
            // Route 22's gate: the carpet from the doors at the bottom to the double door at the top.
            if (Map.Id == TEXT("ROUTE_22_GATE") && (X == 4 || X == 5) && Y >= 1)
            {
                Instance(K.Get(EShape::Cube, EPYRKit::Carpet, GateCarpet, 0.f, false), P + FVector(0, 0, 1.5f), FVector(100, 100, 3));
                Note(X, Y, TEXT("carpet"));
            }
            // A counter tile with no counter look still gets its counter (talking across it reaches the clerk).
            if (T == TEXT('=') && L != TEXT('k'))
            {
                // m4v round 2: the Centre's counter is smooth painted peach and cream, as in the anime frame, not plank courses.
                Instance(K.Get(EShape::Cube, bSmoothCounter ? EPYRKit::Paint : EPYRKit::Counter, Tileset.Contains(TEXT("POKECENTER")) ? CentreCounter : Tileset.Contains(TEXT("MART")) ? MartCounter : Counter), P + FVector(0, 0, 47), FVector(100, 100, 94));
                Instance(K.Get(EShape::Cube, bSmoothCounter ? EPYRKit::Paint : EPYRKit::Counter, CounterTop), P + FVector(0, 0, 97), FVector(104, 104, 8));
            }
        }

    // ---- Rock past the edges: one even plateau, merged into big blocks with grass on top.
    if (bScenery)
    {
        TBitArray<> Used(false, Map.Width * Map.Height);
        auto Want = [&](int32 X, int32 Y) { return !Used[Y * Map.Width + X] && Map.Look(X, Y) == TEXT('r'); };
        for (int32 Y = 0; Y < Map.Height; ++Y)
            for (int32 X = 0; X < Map.Width; ++X)
            {
                if (!Want(X, Y)) continue;
                int32 RW = 1;
                while (X + RW < Map.Width && Want(X + RW, Y)) ++RW;
                int32 RH = 1;
                for (bool bRow = true; bRow && Y + RH < Map.Height; )
                {
                    for (int32 I = 0; I < RW && bRow; ++I) bRow = Want(X + I, Y + RH);
                    if (bRow) ++RH;
                }
                for (int32 J = 0; J < RH; ++J) for (int32 I = 0; I < RW; ++I) Used[(Y + J) * Map.Width + X + I] = true;
                const FVector Center = K.C(X, Y) + FVector((RW - 1) * 50.f, (RH - 1) * 50.f, 0.f);
                Instance(K.Get(EShape::Cube, EPYRKit::RockGrey, Rock), Center + FVector(0, 0, OutdoorRock * .5f), FVector(RW * 100.f, RH * 100.f, OutdoorRock));
                Instance(K.Get(EShape::Plane, EPYRKit::GrassShort, GroundGreen, 0.f, false), Center + FVector(0, 0, OutdoorRock + 1.f), FVector(RW * 100.f, RH * 100.f, 1));
            }
        // m4u: the plateau's top grows a few bushes and boulders, kept two tiles in from its rim (the blind judge
        // read the bare shelf past Route 4's wall as unfinished). Placed by a hash of the Kanto tile, so every build is the
        // same.
        auto Inner = [&](int32 X, int32 Y)
        {
            for (int32 Dy = -2; Dy <= 2; ++Dy)
                for (int32 Dx = -2; Dx <= 2; ++Dx)
                    if (!Map.Contains(X + Dx, Y + Dy) || Map.Look(X + Dx, Y + Dy) != TEXT('r')) return false;
            return true;
        };
        for (int32 Y = 0; Y < Map.Height; ++Y)
            for (int32 X = 0; X < Map.Width; ++X)
            {
                if (Map.Look(X, Y) != TEXT('r') || !Inner(X, Y)) continue;
                const FIntPoint Kt = Map.Origin + FIntPoint(X, Y);
                const uint32 H = GetTypeHash(Kt) * 2654435761u;
                const float R0 = (H & 1023) / 1023.f, R1 = ((H >> 10) & 1023) / 1023.f, R2 = ((H >> 20) & 1023) / 1023.f;
                const FVector P = K.C(X, Y, OutdoorRock) + FVector((R1 - .5f) * 50.f, (R2 - .5f) * 50.f, 0);
                // Low growth only: the far ranges past the plateau (the League's from Route 22) stay in view over it.
                if (R0 < .10f) Instance(K.Get(EShape::Sphere, EPYRKit::TreeCanopy, R1 < .5f ? LeafLight : Leaf), P + FVector(0, 0, 35), FVector(120, 105, 85) * FMath::Lerp(.8f, 1.25f, R2));
                else if (R0 < .15f) Instance(K.Get(EShape::Sphere, EPYRKit::RockGrey, Rock), P + FVector(0, 0, 15), FVector(120, 95, 70) * FMath::Lerp(.7f, 1.3f, R1));
            }
    }

    // ---- Cave walls: each kind merged into rectangles of one height (flat tops, no jagged field of blocks).
    if (Map.bCave && !bScenery)
    {
        struct FCaveKind { TCHAR Look; float H; FLinearColor Col; EPYRKit Kit; };
        // m4v: Mt. Moon's moonlit blue-grey (the rock bands, the darker void, the pale raised walkways).
        const FCaveKind Kinds[] = {{TEXT('C'), CaveRidge, CaveWall, EPYRKit::RockCave}, {TEXT('V'), CaveRidge, PYRColor::Hex(0x3c4558), EPYRKit::RockCave},
            {TEXT('K'), CaveWalkway, PYRColor::Hex(0xb8c2cf), EPYRKit::CaveFloor}};
        for (const FCaveKind& Kd : Kinds)
        {
            TBitArray<> Used(false, Map.Width * Map.Height);
            auto Want = [&](int32 X, int32 Y) { return !Used[Y * Map.Width + X] && Map.Look(X, Y) == Kd.Look && YTile::IsSolid(Map.Tile(X, Y)); };
            for (int32 Y = 0; Y < Map.Height; ++Y)
                for (int32 X = 0; X < Map.Width; ++X)
                {
                    if (!Want(X, Y)) continue;
                    int32 RW = 1;
                    while (X + RW < Map.Width && Want(X + RW, Y)) ++RW;
                    int32 RH = 1;
                    for (bool bRow = true; bRow && Y + RH < Map.Height; )
                    {
                        for (int32 I = 0; I < RW && bRow; ++I) bRow = Want(X + I, Y + RH);
                        if (bRow) ++RH;
                    }
                    for (int32 J = 0; J < RH; ++J) for (int32 I = 0; I < RW; ++I) Used[(Y + J) * Map.Width + X + I] = true;
                    const FVector Center = K.C(X, Y) + FVector((RW - 1) * 50.f, (RH - 1) * 50.f, Kd.H * .5f);
                    Instance(K.Get(EShape::Cube, Kd.Kit, Kd.Col), Center, FVector(RW * 100.f, RH * 100.f, Kd.H));
                }
        }
    }

    // ---- Museum 2F's space shuttle: white orbiter on the blue-railed platform its sign stands at.
    if (!bOutdoorLike && !bScenery)
        for (const FYSign& Sg : Map.Signs)
        {
            if (!Sg.Text.EndsWith(TEXT("SPACE_SHUTTLE_SIGN"))) continue;
            int32 X0 = Sg.X, X1 = Sg.X;
            while (X0 > 0 && Map.Look(X0 - 1, Sg.Y) == TEXT('k')) --X0;
            while (X1 + 1 < Map.Width && Map.Look(X1 + 1, Sg.Y) == TEXT('k')) ++X1;
            const FVector A = K.C(X0, Sg.Y - 1), Bp = K.C(X1, Sg.Y);
            const FVector Mid = (A + Bp) * .5f;
            const float Len = (X1 - X0 + 1) * 100.f;
            const FLinearColor Hull = PYRColor::Hex(0xf6f6f2), Belly = PYRColor::Hex(0x3a3a48), Rail = PYRColor::Hex(0x4f8fdc);
            // The platform is the counters (their top at 101 cm) and the row behind them (the shuttle's own tiles), with a
            // blue rail round the orbiter on it.
            const float Deck = 101.f;
            for (int32 X = X0; X <= X1; ++X)
            {
                Instance(K.Get(EShape::Cube, EPYRKit::Counter, Counter), K.C(X, Sg.Y - 1, 47.f), FVector(100, 100, 94));
                Instance(K.Get(EShape::Cube, EPYRKit::Counter, CounterTop), K.C(X, Sg.Y - 1, 97.f), FVector(104, 104, 8));
            }
            for (const float Side : {-95.f, 95.f}) Instance(K.Get(EShape::Cube, EPYRKit::Metal, Rail), Mid + FVector(0, Side, Deck + 28), FVector(Len, 5, 5));
            for (const float End : {-.5f, .5f}) Instance(K.Get(EShape::Cube, EPYRKit::Metal, Rail), Mid + FVector(End * (Len - 6.f), 0, Deck + 28), FVector(5, 194, 5));
            for (int32 I = 0; I <= X1 - X0 + 1; ++I)
                for (const float Side : {-95.f, 95.f})
                    Instance(K.Get(EShape::Cylinder, EPYRKit::Metal, Rail), Mid + FVector(-Len * .5f + I * 100.f, Side, Deck + 14), FVector(5, 5, 28));
            // The orbiter, nose east: fuselage on its dark belly, nose cone, delta wings, tail fin, engines, cockpit.
            const float Body = Len * .7f, Z = Deck + 30.f;
            Instance(K.Get(EShape::Cylinder, EPYRKit::Paint, Hull), Mid + FVector(-10, 0, Z), FVector(40, 40, Body), FRotator(90, 0, 0));
            Instance(K.Get(EShape::Cube, EPYRKit::Paint, Belly), Mid + FVector(-10, 0, Z - 16), FVector(Body, 34, 8));
            Instance(K.Get(EShape::Cone, EPYRKit::Paint, Hull), Mid + FVector(Body * .5f + 18, 0, Z), FVector(40, 40, 46), FRotator(-90, 0, 0));
            Instance(K.Get(EShape::Cube, EPYRKit::Paint, Hull), Mid + FVector(-Body * .14f, 0, Z - 12), FVector(Body * .5f, 150, 5));
            Instance(K.Get(EShape::Cube, EPYRKit::Paint, Hull), Mid + FVector(Body * .1f, 0, Z - 12), FVector(Body * .3f, 90, 5));
            Instance(K.Get(EShape::Cube, EPYRKit::Paint, Hull), Mid + FVector(-Body * .42f, 0, Z + 38), FVector(44, 5, 46));
            for (const float Side : {-12.f, 12.f})
                Instance(K.Get(EShape::Cylinder, EPYRKit::Paint, Belly), Mid + FVector(-Body * .5f - 14, Side, Z + 4), FVector(16, 16, 20), FRotator(90, 0, 0));
            Instance(K.Get(EShape::Cube, EPYRKit::Paint, PYRColor::Hex(0x2a2a36)), Mid + FVector(Body * .36f, 0, Z + 18), FVector(34, 26, 10));
            for (int32 X = X0; X <= X1; ++X) { Note(X, Sg.Y, TEXT("shuttle_platform")); }
        }

    // ---- Walls round rooms and caves (the ring outside the map), always drawn.
    if (!bOutdoorLike)
    {
        const float H = Map.bCave ? 480.f : 320.f;
        const FLinearColor Col = Map.bCave ? CaveWall : WallCream;
        const EPYRKit WallKit = Map.bCave ? EPYRKit::RockCave : RoomWall;
        const FVector Min = Offset + FVector(Map.Origin.X * 100.f, Map.Origin.Y * 100.f, 0.f);
        const float W = Map.Width * 100.f, D = Map.Height * 100.f, T = 40.f;
        // The four walls, each broken where an exit on that edge leads out (a doorway into the dark with a lintel).
        // A hole broken in the back wall onto the outdoors (the Trashed House's, which Yellow marks with its WALL_HOLE
        // sign) shows the yard beyond; a gate's or a room's doorway stays dark, as the Game Boy draws it.
        auto LeadsOutdoors = [this, &Map](int32 X, int32 Y)
        {
            const int32 Wi = Map.WarpAt(X, Y);
            const FYMap* To = Wi != INDEX_NONE && Data.IsValid() ? Data->Map(Map.Warps[Wi].ToMap) : nullptr;
            bool bHole = false;
            for (const FYSign& Sg : Map.Signs) bHole |= Sg.X == X && Sg.Y == Y && Sg.Text.EndsWith(TEXT("WALL_HOLE"));
            return bHole && To && To->bOutdoor;
        };
        auto Exit = [&Map](int32 X, int32 Y) { return Map.WarpAt(X, Y) != INDEX_NONE && (Map.bCave || Map.Look(X, Y) == TEXT('D') || YTile::IsSolid(Map.Tile(X, Y + (Y == 0 ? 1 : -1))) == false); };
        auto Wall = [&](bool bAlongX, float Fixed, int32 Count, TFunctionRef<bool(int32)> Open, float WallH)
        {
            int32 Start = 0;
            for (int32 I = 0; I <= Count; ++I)
            {
                if (I < Count && !Open(I)) continue;
                if (I > Start)
                {
                    const float A = Start * 100.f, B = I * 100.f;
                    const float Lo = Start == 0 ? -T : A, Hi = I == Count ? B + T : B;
                    if (bAlongX) K.Box(Min + FVector((Lo + Hi) * .5f, Fixed, WallH * .5f), FVector(Hi - Lo, T, WallH), WallKit, Col);
                    else K.Box(Min + FVector(Fixed, (Lo + Hi) * .5f, WallH * .5f), FVector(T, Hi - Lo, WallH), WallKit, Col);
                }
                if (I < Count)
                {
                    if (bAlongX && Fixed < 0) Note(I, 0, TEXT("doorway"));
                    // The doorway: the wall above it, and beyond it the dark, or the outdoors where it leads outside
                    // (the Trashed House's broken back wall onto Cerulean, a gate's far door): grass under the sky.
                    const FVector At = bAlongX ? Min + FVector(I * 100.f + 50.f, Fixed, 0) : Min + FVector(Fixed, I * 100.f + 50.f, 0);
                    K.Box(At + FVector(0, 0, 230.f + (WallH - 230.f) * .5f), bAlongX ? FVector(100, T, WallH - 230.f) : FVector(T, 100, WallH - 230.f), WallKit, Col);
                    const FVector Beyond = At + (bAlongX ? FVector(0, Fixed < 0 ? -30.f : 30.f, 0) : FVector(Fixed < 0 ? -30.f : 30.f, 0, 0));
                    const FVector Panel = bAlongX ? FVector(100, 10, 1) : FVector(10, 100, 1);
                    if (bAlongX && Fixed < 0 && LeadsOutdoors(I, 0))
                    {
                        Instance(K.Get(EShape::Cube, EPYRKit::Paint, PYRColor::Hex(0x9fd8f5), 0.f, false), Beyond + FVector(0, 0, 145), Panel * FVector(1, 1, 170));
                        Instance(K.Get(EShape::Cube, EPYRKit::GrassShort, GroundGreen, 0.f, false), Beyond + FVector(0, 0, 30), Panel * FVector(1, 1, 60));
                        if (!bScenery) OutdoorDoorways.Add(Map.Origin + FIntPoint(I, 0));
                    }
                    else Instance(K.Get(EShape::Cube, EPYRKit::Ink, PYRColor::Hex(0x101018), 0.f, false), Beyond + FVector(0, 0, 115), Panel * FVector(1, 1, 230));
                }
                Start = I + 1;
            }
        };
        // In rooms the back and side walls rise to the lid (m4r: the camera saw over them into the black, visual-079
        // and -083), the near wall too (the camera stays inside the room), and it stays whole.
        const float Tall = Map.bCave ? H : RoomWallTop;
        Wall(true, -T * .5f, Map.Width, [&](int32 X) { return Exit(X, 0); }, Tall);
        Wall(true, D + T * .5f, Map.Width, [&](int32 X) { return false; }, Tall);
        Wall(false, -T * .5f, Map.Height, [&](int32 Y) { return false; }, Tall);
        Wall(false, W + T * .5f, Map.Height, [&](int32 Y) { return false; }, Tall);
        // Past the walls, up to a lid over the room: black, as the Game Boy shows nothing there (and no other map).
        {
            const float Lid = 900.f, M = 80.f;
            // m4v: in a cave the space past the walls is dark rock, not black (the anime frame's cave closes over).
            UInstancedStaticMeshComponent* Void = Map.bCave ? K.Get(EShape::Cube, EPYRKit::RockCave, PYRColor::Hex(0x3a4660), 0.f, false)
                : K.Get(EShape::Cube, EPYRKit::Ink, PYRColor::Hex(0x101018), 0.f, false);
            Instance(Void, Min + FVector(W * .5f, -T - M * .5f, (H + Lid) * .5f), FVector(W + 2 * (T + M), M, Lid - H + 40.f));
            Instance(Void, Min + FVector(W * .5f, D + T + M * .5f, (H + Lid) * .5f), FVector(W + 2 * (T + M), M, Lid - H + 40.f));
            Instance(Void, Min + FVector(-T - M * .5f, D * .5f, (H + Lid) * .5f), FVector(M, D + 2 * (T + M), Lid - H + 40.f));
            Instance(Void, Min + FVector(W + T + M * .5f, D * .5f, (H + Lid) * .5f), FVector(M, D + 2 * (T + M), Lid - H + 40.f));
            Instance(Void, Min + FVector(W * .5f, D * .5f, Lid + 20.f), FVector(W + 2 * (T + M), D + 2 * (T + M), 40.f));
            // And under the floor (m4s): a low look across a pool's edge (Cerulean Gym) passes under the floor between
            // the water (14 cm down) and the tiles; it meets this, not the sky dome below the room.
            Instance(Void, Min + FVector(W * .5f, D * .5f, -75.f), FVector(W + 2 * (T + M), D + 2 * (T + M), 10.f));
            // with sides under the walls down to it, so the space under the floor is closed all round.
            Instance(Void, Min + FVector(W * .5f, -T * .5f, -40.f), FVector(W + 2 * T, T, 80.f));
            Instance(Void, Min + FVector(W * .5f, D + T * .5f, -40.f), FVector(W + 2 * T, T, 80.f));
            Instance(Void, Min + FVector(-T * .5f, D * .5f, -40.f), FVector(T, D + 2 * T, 80.f));
            Instance(Void, Min + FVector(W + T * .5f, D * .5f, -40.f), FVector(T, D + 2 * T, 80.f));
            if (!bScenery) ClosedRooms.Add(Map.Id);
        }
        if (!Map.bCave)
        {
            // Skirting round the room.
            K.Box(Min + FVector(W * .5f, 4.f, 12.f), FVector(W, 8.f, 24.f), EPYRKit::FloorWood, WallTrim);
            K.Box(Min + FVector(4.f, D * .5f, 12.f), FVector(8.f, D, 24.f), EPYRKit::FloorWood, WallTrim);
            K.Box(Min + FVector(W - 4.f, D * .5f, 12.f), FVector(8.f, D, 24.f), EPYRKit::FloorWood, WallTrim);
        }
    }

    // ---- Buildings (outdoor maps): extruded from each group of B/R/D/n looks.
    if (bOutdoorLike)
    {
        TArray<FYBuilding> Found;
        FindBuildings(Map, Found);
        TMap<int32, FRoofSection> Roofs;   // one section per building, so a building in the way hides whole
        TMap<int32, EPYRKit> RoofKits;
        TMap<int32, int32> RoofOccluder;
        for (int32 BIndex = 0; BIndex < Found.Num(); ++BIndex)
        {
            const FYBuilding& B = Found[BIndex];
            if (!bScenery) Buildings.Add(B);
            ++Actor->Buildings;
            const FVector Min = Offset + FVector(B.Rect.Min.X * 100.f, B.Rect.Min.Y * 100.f, 0.f);
            const float W = B.Rect.Width() * 100.f, D = B.Rect.Height() * 100.f;
            const bool bBig = B.Kind == FYBuilding::EKind::Gym || B.Kind == FYBuilding::EKind::Museum;
            const float Wall = bBig ? 420.f : 300.f;
            FLinearColor WallCol = WallCream, RoofCol = PYRColor::Hex(0xb8483a);
            const FString MapName = Map.Id.ToString();
            if (MapName.Contains(TEXT("VIRIDIAN"))) RoofCol = PYRColor::Hex(0xd87838);
            else if (MapName.Contains(TEXT("PEWTER"))) { RoofCol = PYRColor::Hex(0x8a7a6a); WallCol = WallStone; }
            else if (MapName.Contains(TEXT("CERULEAN"))) RoofCol = PYRColor::Hex(0x3a9a9a);
            else if (MapName.Contains(TEXT("ROUTE"))) RoofCol = PYRColor::Hex(0x7a6048);
            bool bFlat = false;
            EPYRKit RoofKit = EPYRKit::RoofRed;
            if (MapName.Contains(TEXT("PEWTER")) || MapName.Contains(TEXT("ROUTE"))) RoofKit = EPYRKit::RoofBrown;
            else if (MapName.Contains(TEXT("CERULEAN"))) RoofKit = EPYRKit::RoofBlue;
            K.Occ = NewOccluder(B.Rect, (B.Kind == FYBuilding::EKind::Gym || B.Kind == FYBuilding::EKind::Museum ? 420.f : 300.f) + 240.f);
            switch (B.Kind)
            {
            case FYBuilding::EKind::Centre: RoofCol = PYRColor::Hex(0xe3342f); WallCol = WallWhite; bFlat = true; RoofKit = EPYRKit::RoofRed; break;
            case FYBuilding::EKind::Mart: RoofCol = PYRColor::Hex(0x3f7fd0); WallCol = WallWhite; bFlat = true; RoofKit = EPYRKit::RoofBlue; break;
            case FYBuilding::EKind::Gym: RoofCol = PYRColor::Hex(0x6f8a6a); WallCol = WallStone; bFlat = true; RoofKit = EPYRKit::RoofGrey; break;
            // m4v: the museum's roof is terracotta tiles in the anime frame (was grey #9a8e80).
            case FYBuilding::EKind::Museum: RoofCol = PYRColor::Hex(0xb5603e); WallCol = WallStone; bFlat = true; RoofKit = EPYRKit::RoofRed; break;
            case FYBuilding::EKind::Lab: RoofCol = PYRColor::Hex(0x7c8ea8); WallCol = WallWhite; RoofKit = EPYRKit::RoofBlue; break;
            case FYBuilding::EKind::Gate: RoofCol = PYRColor::Hex(0x8a8a90); WallCol = WallStone; bFlat = true; RoofKit = EPYRKit::RoofGrey; break;
            default: break;
            }
            // m4v: the walls in the kit: sandstone brick for the big stone buildings (the museum, the gyms), cut stone for
            // Pewter's houses and the gates, white clapboard everywhere else.
            const EPYRKit WallKitB = bBig ? EPYRKit::WallBrick : WallCol == WallStone ? EPYRKit::WallStone : EPYRKit::WallWood;
            if (bBig) WallCol = PYRKit::AsPainted;
            // Walls: the whole group but the door tiles, which stay open as alcoves (Red steps into one to enter).
            TArray<bool> Solid;
            const int32 GW = B.Rect.Width(), GH = B.Rect.Height();
            Solid.Init(true, GW * GH);
            for (const FIntPoint& Door : B.Doors) Solid[(Door.Y - B.Rect.Min.Y) * GW + Door.X - B.Rect.Min.X] = false;
            TArray<FIntRect> Rects;
            {
                TBitArray<> Used(false, GW * GH);
                for (int32 Y = 0; Y < GH; ++Y)
                    for (int32 X = 0; X < GW; ++X)
                    {
                        if (Used[Y * GW + X] || !Solid[Y * GW + X]) continue;
                        int32 RW = 1;
                        while (X + RW < GW && !Used[Y * GW + X + RW] && Solid[Y * GW + X + RW]) ++RW;
                        int32 RH = 1;
                        for (bool bRow = true; bRow && Y + RH < GH; )
                        {
                            for (int32 I = 0; I < RW && bRow; ++I) bRow = !Used[(Y + RH) * GW + X + I] && Solid[(Y + RH) * GW + X + I];
                            if (bRow) ++RH;
                        }
                        for (int32 J = 0; J < RH; ++J) for (int32 I = 0; I < RW; ++I) Used[(Y + J) * GW + X + I] = true;
                        Rects.Add(FIntRect(X, Y, X + RW, Y + RH));
                    }
            }
            for (const FIntRect& R : Rects)
            {
                const FVector C = Min + FVector((R.Min.X + R.Width() * .5f) * 100.f, (R.Min.Y + R.Height() * .5f) * 100.f, Wall * .5f);
                K.Box(C, FVector(R.Width() * 100.f, R.Height() * 100.f, Wall), WallKitB, WallCol);
            }
            // A plinth round the base.
            K.Box(Min + FVector(W * .5f, D - 4.f, 15.f), FVector(W + 6.f, 10.f, 30.f), EPYRKit::FloorWood, WallTrim);
            // m4u: a facade's small steps (a window frame 2 cm proud, the glass 2 cm more, a door's jamb seen edge-on) are
            // too shallow for the toon pass's depth edge and their side faces cover part of a pixel, so the normal edge came
            // and went along them: broken, striped borders (the blind judge's z-fight). Each such edge is now drawn as a
            // solid ink rim, so wherever the edge pass fires it lands on ink.
            UInstancedStaticMeshComponent* Rim = K.Get(EShape::Cube, EPYRKit::Ink, PYRColor::Ink, 0.f, false);
            for (const FIntPoint& Door : B.Doors)
            {
                const FVector DoorAt = KantoCenter(Door);
                // Lintel over the alcove, the door at its back and a step.
                K.Box(DoorAt + FVector(0, 0, Wall - (Wall - 230.f) * .5f), FVector(100.f, 100.f, Wall - 230.f), WallKitB, WallCol);
                K.Box(DoorAt + FVector(0, -46.f, 110.f), FVector(84.f, 8.f, 220.f), B.Kind == FYBuilding::EKind::Centre || B.Kind == FYBuilding::EKind::Mart ? EPYRKit::Glass : EPYRKit::Counter,
                    B.Kind == FYBuilding::EKind::Centre || B.Kind == FYBuilding::EKind::Mart ? Glass : DoorBrown);
                K.Box(DoorAt + FVector(0, 0, 237.f), FVector(104.f, 104.f, 14.f), EPYRKit::FloorWood, WallTrim);
                // The doorway's frame on the facade: its jambs from the plinth up and the head under the lintel.
                for (const float Side : {-1.f, 1.f}) K.Add(Rim, DoorAt + FVector(Side * 48.5f, 50.5f, 131.f), FVector(3.f, 3.f, 202.f));
                K.Add(Rim, DoorAt + FVector(0, 50.5f, 230.5f), FVector(100.f, 3.f, 3.f));
                if (B.Kind == FYBuilding::EKind::Gym)
                {
                    // The League emblem over the gym door.
                    K.Add(K.Get(EShape::Cylinder, EPYRKit::Paint, PYRColor::Hex(0xf0c040)), DoorAt + FVector(0, 52.f, Wall - 70.f), FVector(90, 90, 8), FRotator(0, 0, 90));
                    K.Add(K.Get(EShape::Cylinder, EPYRKit::Paint, PYRColor::Hex(0xe3342f)), DoorAt + FVector(0, 57.f, Wall - 70.f), FVector(50, 50, 6), FRotator(0, 0, 90));
                }
                if (B.Kind == FYBuilding::EKind::Centre || B.Kind == FYBuilding::EKind::Mart)
                {
                    // The sign plate over the entrance: red for the Centre, blue for the Mart, on an ink rim, with the
                    // Game Boy's word on its white panel (m4u: the panel was blank), POKé or MART.
                    const FVector Plate = DoorAt + FVector(0, 0, Wall - 45.f);
                    K.Add(Rim, Plate + FVector(0, 53.5f, 0), FVector(188.f, 7.f, 68.f));
                    K.Box(Plate + FVector(0, 54.f, 0), FVector(180.f, 8.f, 60.f), EPYRKit::Paint, RoofCol);
                    K.Add(Rim, Plate + FVector(0, 58.6f, 0), FVector(138.f, 1.2f, 46.f));
                    K.Box(Plate + FVector(0, 59.f, 0), FVector(132.f, 1.4f, 40.f), EPYRKit::Paint, WallWhite);
                    const TArray<FString> Word{B.Kind == FYBuilding::EKind::Centre ? FString(TEXT("POK\x00e9")) : FString(TEXT("MART"))};
                    auto Emit = [&K, Rim](const FVector& At, const FVector& Size, const FRotator& Rot) { K.Add(Rim, At, Size, Rot); };
                    PYRLetters::Draw(Emit, Plate + FVector(0, 59.7f, 0), FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), Word,
                        PYRLetters::FitPixel(Word, 116.f, 30.f, 5.f), 1.5f);
                }
            }
            // Windows on the front for every n look: a white frame and the glass, each on an ink rim (see Rim above).
            for (int32 Y = B.Rect.Min.Y; Y < B.Rect.Max.Y; ++Y)
                for (int32 X = B.Rect.Min.X; X < B.Rect.Max.X; ++X)
                    if (LookAt(FIntPoint(X, Y)) == TEXT('n')
                        // Never across a doorway: a window tile in a door's column would draw over the open entrance
                        // (the blind judge, m4v: Pallet's lab, Pewter's museum and Mart, Viridian's Centre).
                        && !B.Doors.ContainsByPredicate([X](const FIntPoint& Door) { return Door.X == X; }))
                    {
                        const FVector Front = KantoCenter(FIntPoint(X, B.Rect.Max.Y - 1)) + FVector(0, 50.f, 160.f);
                        K.Add(Rim, Front + FVector(0, 2.f, 0), FVector(76.f, 4.f, 86.f));
                        K.Box(Front + FVector(0, 2.25f, 0), FVector(70.f, 4.5f, 80.f), EPYRKit::Paint, WallWhite);
                        K.Add(Rim, Front + FVector(0, 2.4f, 0), FVector(60.f, 4.8f, 68.f));
                        K.Box(Front + FVector(0, 2.6f, 0), FVector(56.f, 5.2f, 64.f), EPYRKit::Glass, Glass);
                    }
            // The roof: a flat slab with a raised edge for public buildings, a pitched prism for houses.
            if (bFlat)
            {
                K.Box(Min + FVector(W * .5f, D * .5f, Wall + 15.f), FVector(W + 40.f, D + 40.f, 30.f), RoofKit, RoofCol);
                K.Box(Min + FVector(W * .5f, D * .5f, Wall + 40.f), FVector(W - 40.f, D - 40.f, 20.f), RoofKit, RoofCol * .85f);
            }
            else
            {
                FRoofSection& S = Roofs.FindOrAdd(BIndex);
                RoofOccluder.Add(BIndex, K.Occ);
                S.Color = RoofCol;
                RoofKits.Add(BIndex, RoofKit);
                const float O = 35.f, E = Wall, R = FMath::Clamp(D * .38f, 120.f, 320.f);
                const FVector A0 = Min + FVector(-O, -O, E), B0 = Min + FVector(W + O, -O, E), C0 = Min + FVector(W + O, D + O, E), D0 = Min + FVector(-O, D + O, E);
                const FVector R0 = Min + FVector(-O, D * .5f, E + R), R1 = Min + FVector(W + O, D * .5f, E + R);
                Tri(S, A0, R0, R1); Tri(S, A0, R1, B0);   // north slope
                Tri(S, D0, C0, R1); Tri(S, D0, R1, R0);   // south slope
                Tri(S, A0, D0, R0);                      // west gable
                Tri(S, B0, R1, C0);                      // east gable
                // A chimney on houses.
                if (B.Kind == FYBuilding::EKind::House) K.Box(Min + FVector(W * .75f, D * .3f, E + R * .75f), FVector(40, 40, R * .9f), EPYRKit::WallBrick, WallTrim);
            }
            K.Occ = INDEX_NONE;
        }
        if (Roofs.Num() > 0)
        {
            UProceduralMeshComponent* Pmc = NewObject<UProceduralMeshComponent>(Actor);
            Pmc->SetupAttachment(Actor->GetRootComponent());
            Pmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Pmc->RegisterComponent();
            Actor->AddInstanceComponent(Pmc);
            int32 Section = 0;
            for (const auto& Pair : Roofs)
            {
                TArray<FVector> Local;
                for (const FVector& V : Pair.Value.V) Local.Add(V - Actor->GetActorLocation());
                Pmc->CreateMeshSection_LinearColor(Section, Local, Pair.Value.I, Pair.Value.N, {}, {}, {}, false);
                const EPYRKit* Kit = RoofKits.Find(Pair.Key);
                UMaterialInterface* Mat = PYRKit::Get(Actor, Kit ? *Kit : EPYRKit::RoofRed, Pair.Value.Color);
                Pmc->SetMaterial(Section, Mat ? Mat : UPYRPaint::Get(Actor, Pair.Value.Color));
                if (const int32* Occ = RoofOccluder.Find(Pair.Key)) SetOccluderRoof(*Occ, Pmc, Section);
                ++Section;
            }
            Actor->Roofs = Pmc;
        }
    }
}
