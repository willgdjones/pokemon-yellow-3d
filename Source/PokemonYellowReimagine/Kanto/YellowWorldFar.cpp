// Kanto far past the edges (m4s: visual-072, -078, -084, warp-015). BuildScenery draws, close by, what the Game Boy
// shows past an edge (an out-of-range neighbour's own tiles, else the map's border block). Further out this draws the
// rest of Kanto from YellowData's kanto.json, with less detail the further it is:
// - every outdoor map out of the range at its place (Route 23 and Indigo Plateau, Route 9 and 10, Saffron, the sea
//   routes...), its tiles merged into big pieces: ground and water as planes, rock as the low plateau, trees as forest
//   masses under round crowns, buildings as blocks with roofs;
// - between and beyond them, the Town Map's land and sea: the sea runs to the horizon, and the land rises from tree lines
//   through rolling hills to layered mountains, with Mt. Moon's and the League's massifs round their landmarks.
// All of it is primitives painted with the anime kit (m4v, World/PYRKit: the same surfaces as the maps, at larger
// tilings), inked by the toon pass, with no collision and no shadows. Each piece carries its source in its component tags: "KantoFar", then the map id (ROUTE_23...), the landmark
// (MT_MOON...) or SEA, then its kind ("ground", "sea" or "feature").
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Kanto/YellowWorld.h"
#include "World/PYRKit.h"
#include "World/PYRPaint.h"

using namespace PYR;

namespace
{
const FLinearColor FarGrass = PYRColor::Hex(0x86cf5e);
const FLinearColor FarTallGrass = PYRColor::Hex(0x5fae4a);
const FLinearColor FarPath = PYRColor::Hex(0xecca8a);
const FLinearColor FarSand = PYRColor::Hex(0xf2dd9f);
const FLinearColor FarWater = PYRColor::Hex(0x4aa8e8);
const FLinearColor FarWaterDeep = PYRColor::Hex(0x2f7fc4);
const FLinearColor FarRock = PYRColor::Hex(0xa89078);
const FLinearColor FarLeaf = PYRColor::Hex(0x3f9a4a);
const FLinearColor FarLeafLight = PYRColor::Hex(0x55b050);
const FLinearColor FarForest = PYRColor::Hex(0x2f7d3c);
const FLinearColor FarTrunk = PYRColor::Hex(0x8a5a3a);
const FLinearColor FarWall = PYRColor::Hex(0xfff4d6);
const FLinearColor FarRoof = PYRColor::Hex(0xb8483a);
const FLinearColor FarHill = PYRColor::Hex(0x6fbf4f);
const FLinearColor FarHillDark = PYRColor::Hex(0x5aa848);
const FLinearColor MountGreen = PYRColor::Hex(0x7a9f62);
const FLinearColor MountBlue = PYRColor::Hex(0x8fa9c4);
const FLinearColor MountRock = PYRColor::Hex(0xa08a72);
constexpr float FarRockHeight = 80.f;

// m4v: the kit surface for a far piece, from the texture it had (tex_*) or its colour.
EPYRKit FarSurface(EShape Shape, const FLinearColor& C, const TCHAR* Tex)
{
    if (Tex)
    {
        const FString T(Tex);
        if (T == TEXT("tex_grass")) return EPYRKit::GrassShort;
        if (T == TEXT("tex_path")) return C == FarSand ? EPYRKit::Sand : EPYRKit::PathDirt;
        if (T == TEXT("tex_water")) return EPYRKit::Water;
        if (T == TEXT("tex_rock_cliff")) return EPYRKit::RockGrey;
        if (T == TEXT("tex_house_wall")) return EPYRKit::WallWood;
        if (T == TEXT("tex_stone_wall")) return EPYRKit::WallStone;
        if (T == TEXT("tex_roof_tiles")) return C.B > C.R ? EPYRKit::RoofBlue : EPYRKit::RoofRed;
    }
    if (C == FarWaterDeep) return EPYRKit::Water;
    if (C == FarTrunk) return EPYRKit::TreeTrunk;
    if (C == PYRColor::Hex(0x4f6fa8)) return EPYRKit::RoofBlue;
    if (Shape == EShape::Cone) return EPYRKit::RockGrey;   // the mountain ranges, tinted green to blue
    return EPYRKit::TreeCanopy;                            // forest masses and crowns
}   // the same low plateau as the route rock (YellowWorldLook.cpp OutdoorRock)

// A stable 0..1 hash and smooth value noise over Kanto tiles.
float Hash01(int32 X, int32 Y, uint32 Seed)
{
    uint32 H = uint32(X) * 0x8da6b343u ^ uint32(Y) * 0xd8163841u ^ Seed * 0xcb1ab31fu;
    H ^= H >> 13;
    H *= 0x5bd1e995u;
    H ^= H >> 15;
    return (H & 0xFFFFFF) / float(0x1000000);
}

float Noise(float X, float Y, float Scale, uint32 Seed)
{
    const float Fx = X / Scale, Fy = Y / Scale;
    const int32 Ix = FMath::FloorToInt(Fx), Iy = FMath::FloorToInt(Fy);
    float Tx = Fx - Ix, Ty = Fy - Iy;
    Tx = Tx * Tx * (3.f - 2.f * Tx);
    Ty = Ty * Ty * (3.f - 2.f * Ty);
    const float A = FMath::Lerp(Hash01(Ix, Iy, Seed), Hash01(Ix + 1, Iy, Seed), Tx);
    const float B = FMath::Lerp(Hash01(Ix, Iy + 1, Seed), Hash01(Ix + 1, Iy + 1, Seed), Tx);
    return FMath::Lerp(A, B, Ty);
}

// Greedy rectangles over a W x H grid where Want(x, y).
void Rects(int32 W, int32 H, TFunctionRef<bool(int32, int32)> Want, TArray<FIntRect>& Out)
{
    TBitArray<> Used(false, W * H);
    for (int32 Y = 0; Y < H; ++Y)
        for (int32 X = 0; X < W; ++X)
        {
            if (Used[Y * W + X] || !Want(X, Y)) continue;
            int32 RW = 1;
            while (X + RW < W && !Used[Y * W + X + RW] && Want(X + RW, Y)) ++RW;
            int32 RH = 1;
            for (bool bRow = true; bRow && Y + RH < H; )
            {
                for (int32 I = 0; I < RW && bRow; ++I) bRow = !Used[(Y + RH) * W + X + I] && Want(X + I, Y + RH);
                if (bRow) ++RH;
            }
            for (int32 J = 0; J < RH; ++J) for (int32 I = 0; I < RW; ++I) Used[(Y + J) * W + X + I] = true;
            Out.Add(FIntRect(X, Y, X + RW, Y + RH));
        }
}

enum class EFar : uint8 { None, Grass, TallGrass, Path, Sand, Water, Tree, Rock, Building };

EFar FarClass(TCHAR L, bool bPlateau)
{
    switch (L)
    {
    case TEXT('W'): case TEXT('H'): return EFar::Water;
    case TEXT('T'): case TEXT('Z'): return EFar::Tree;
    case TEXT('r'): case TEXT('x'): return EFar::Rock;
    case TEXT('B'): case TEXT('R'): case TEXT('n'): case TEXT('D'): return EFar::Building;
    case TEXT('G'): return EFar::TallGrass;
    case TEXT('p'): return bPlateau ? EFar::Grass : EFar::Path;   // the Plateau's open ground is grass
    case TEXT('y'): return EFar::Sand;
    case TEXT(' '): return EFar::None;
    default: return EFar::Grass;
    }
}

struct FFarKit
{
    AYellowMapActor* A;
    FVector Offset;
    TMap<FString, UInstancedStaticMeshComponent*> Isms;
    int32 Instances = 0;

    UInstancedStaticMeshComponent* Get(EShape Shape, const FLinearColor& Color, FName Source, const TCHAR* Kind, const TCHAR* Tex = nullptr, float Tile = 400.f)
    {
        const FString Key = FString::Printf(TEXT("%d|%08x|%s|%s|%s"), int32(Shape), Color.ToFColor(false).ToPackedARGB(), *Source.ToString(), Kind, Tex ? Tex : TEXT(""));
        if (UInstancedStaticMeshComponent** Found = Isms.Find(Key)) return *Found;
        UInstancedStaticMeshComponent* I = Instanced(A, Shape, Color, false, false);
        // m4v: painted with the kit at the tiling the piece had (far pieces are big), else the kit's own.
        // (The far sea's ripples at four times its old tiling, so they still read from the shore.)
        const EPYRKit Surface = FarSurface(Shape, Color, Tex);
        PYRKit::Apply(I, Surface, Color, Tex ? Tile * (Surface == EPYRKit::Water ? 4.f : 1.5f) : 0.f);
        I->SetCanEverAffectNavigation(false);
        I->ComponentTags = {TEXT("KantoFar"), Source, FName(Kind)};
        A->Looks.Add(I);
        Isms.Add(Key, I);
        return I;
    }
    void Add(UInstancedStaticMeshComponent* I, const FVector& At, const FVector& Size, const FRotator& R = FRotator::ZeroRotator)
    {
        Instance(I, At, Size, R);
        ++Instances;
    }
    // A Kanto tile rectangle's centre (world), at height Z.
    FVector Centre(const FIntRect& R, float Z) const
    {
        return Offset + FVector((R.Min.X + R.Width() * .5f) * YTile::Size, (R.Min.Y + R.Height() * .5f) * YTile::Size, Z);
    }
};
}

bool AYellowWorld::IsKantoSea(FIntPoint K) const
{
    const FYellowDataSet* D = Data.Get();
    if (!D || D->TownMapRows.Num() < 2) return false;
    const int32 Cx = FMath::FloorToInt((K.X - D->TownMapSquare0.X) / D->TownMapSquareTiles.X + .5f);
    const int32 Cy = FMath::FloorToInt((K.Y - D->TownMapSquare0.Y) / D->TownMapSquareTiles.Y + .5f);
    const int32 W = D->TownMapRows[0].Len(), H = D->TownMapRows.Num();
    if (Cy <= 0)
    {
        // North of the Town Map (and its top row, the frame's route line): Route 25 runs out along the cape to Bill's Sea
        // Cottage with the open sea north of it (from two squares west of the cottage); west of that, Kanto's northern
        // land goes on.
        const FIntPoint* Cottage = D->Landmarks.Find(TEXT("SEA_COTTAGE"));
        return Cottage && K.X >= Cottage->X - 2.f * D->TownMapSquareTiles.X;
    }
    if (Cy >= H) return true;   // south of the Town Map: the open sea
    const FString& Row = D->TownMapRows[Cy];
    return Row[FMath::Clamp(Cx, 0, W - 1)] == TEXT('~');
}

void AYellowWorld::BuildKantoFar()
{
    const FYellowDataSet* D = Data.Get();
    if (!D || D->TownMapRows.Num() < 2) return;
    const double Start = FPlatformTime::Seconds();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.Owner = this;
    FarActor = GetWorld()->SpawnActor<AYellowMapActor>(Offset, FRotator::ZeroRotator, Params);
    FarActor->MapId = TEXT("KANTO_FAR");
    FarActor->Tags.Add(TEXT("YellowScenery"));
    FarActor->Tags.Add(TEXT("KantoFar"));
#if WITH_EDITOR
    FarActor->SetActorLabel(TEXT("Map_KANTO_FAR"));
#endif
    FFarKit K{FarActor, Offset, {}};

    // What is already drawn: a map, or the scenery close by.
    TArray<FIntRect> Built;
    FIntRect Box(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max(), TNumericLimits<int32>::Lowest(), TNumericLimits<int32>::Lowest());
    auto Grow = [&Box](const FIntRect& R) { Box.Min = Box.Min.ComponentMin(R.Min); Box.Max = Box.Max.ComponentMax(R.Max); };
    for (const FYMap* M : MapList) if (M->bOutdoor) { Built.Add(M->WorldRect()); Grow(M->WorldRect()); }
    for (const FYMap& M : D->Kanto) Grow(M.WorldRect());
    for (const auto& P : SceneryLooks) Grow(FIntRect(P.Key, P.Key + FIntPoint(1, 1)));
    auto Covered = [this](FIntPoint T) { return SceneryLooks.Contains(T) || MapAtKanto(T) != nullptr; };

    // ---- 1. The out-of-range maps past the scenery close by, merged per kind.
    int32 MapTiles = 0;
    for (const FYMap& M : D->Kanto)
    {
        const bool bPlateau = M.Tileset.ToUpper() == TEXT("PLATEAU");
        TArray<EFar> Cls;
        Cls.SetNumZeroed(M.Width * M.Height);
        for (int32 Y = 0; Y < M.Height; ++Y)
            for (int32 X = 0; X < M.Width; ++X)
            {
                const FIntPoint T = M.Origin + FIntPoint(X, Y);
                const TPair<TCHAR, FName>* Own = KantoMapTiles.Find(T);
                if (!Own || Own->Value != M.Id || Covered(T)) continue;
                Cls[Y * M.Width + X] = FarClass(M.Look(X, Y), bPlateau);
                ++MapTiles;
            }
        for (const EFar C : {EFar::Grass, EFar::TallGrass, EFar::Path, EFar::Sand, EFar::Water, EFar::Tree, EFar::Rock, EFar::Building})
        {
            TArray<FIntRect> Out;
            Rects(M.Width, M.Height, [&](int32 X, int32 Y) { return Cls[Y * M.Width + X] == C; }, Out);
            for (const FIntRect& L : Out)
            {
                const FIntRect R(M.Origin + L.Min, M.Origin + L.Max);
                const FVector Size(R.Width() * 100.f, R.Height() * 100.f, 1.f);
                switch (C)
                {
                case EFar::Grass: K.Add(K.Get(EShape::Plane, FarGrass, M.Id, TEXT("ground"), TEXT("tex_grass"), 300.f), K.Centre(R, .5f), Size); break;
                case EFar::TallGrass: K.Add(K.Get(EShape::Plane, FarTallGrass, M.Id, TEXT("ground"), TEXT("tex_grass"), 300.f), K.Centre(R, .5f), Size); break;
                case EFar::Path: K.Add(K.Get(EShape::Plane, FarPath, M.Id, TEXT("ground"), TEXT("tex_path"), 300.f), K.Centre(R, 1.f), Size); break;
                case EFar::Sand: K.Add(K.Get(EShape::Plane, FarSand, M.Id, TEXT("ground"), TEXT("tex_path"), 300.f), K.Centre(R, 1.f), Size); break;
                case EFar::Water:
                    K.Add(K.Get(EShape::Cube, FarWaterDeep, M.Id, TEXT("sea")), K.Centre(R, -40.f), Size * FVector(1, 1, 50.f));
                    K.Add(K.Get(EShape::Plane, FarWater, M.Id, TEXT("sea"), TEXT("tex_water"), 400.f), K.Centre(R, -14.f), Size);
                    break;
                case EFar::Rock:
                    K.Add(K.Get(EShape::Cube, FarRock, M.Id, TEXT("feature"), TEXT("tex_rock_cliff"), 250.f), K.Centre(R, FarRockHeight * .5f), Size * FVector(1, 1, FarRockHeight));
                    K.Add(K.Get(EShape::Plane, FarGrass, M.Id, TEXT("feature"), TEXT("tex_grass"), 300.f), K.Centre(R, FarRockHeight + 1.f), Size);
                    break;
                case EFar::Tree:
                {
                    // A forest mass: a dark body to the crowns' feet, round crowns over every 2x2 tiles of it.
                    K.Add(K.Get(EShape::Plane, FarForest, M.Id, TEXT("ground")), K.Centre(R, .5f), Size);
                    K.Add(K.Get(EShape::Cube, FarForest, M.Id, TEXT("feature")), K.Centre(R, 110.f), FVector(Size.X - 30.f, Size.Y - 30.f, 220.f));
                    for (int32 Y = R.Min.Y; Y < R.Max.Y; Y += 2)
                        for (int32 X = R.Min.X; X < R.Max.X; X += 2)
                        {
                            const FIntRect Q(X, Y, FMath::Min(X + 2, R.Max.X), FMath::Min(Y + 2, R.Max.Y));
                            const float S = FMath::Lerp(.9f, 1.15f, Hash01(X, Y, 7));
                            K.Add(K.Get(EShape::Sphere, (X + Y) % 4 ? FarLeaf : FarLeafLight, M.Id, TEXT("feature")), K.Centre(Q, 240.f * S),
                                FVector(Q.Width() * 120.f + 40.f, Q.Height() * 120.f + 40.f, 220.f) * S);
                        }
                    break;
                }
                case EFar::Building:
                    K.Add(K.Get(EShape::Cube, FarWall, M.Id, TEXT("feature"), TEXT("tex_house_wall"), 250.f), K.Centre(R, 150.f), Size * FVector(1, 1, 300.f));
                    K.Add(K.Get(EShape::Cube, FarRoof, M.Id, TEXT("feature"), TEXT("tex_roof_tiles"), 200.f), K.Centre(R, 315.f), FVector(Size.X + 40.f, Size.Y + 40.f, 30.f));
                    break;
                default: break;
                }
            }
        }
    }
    // The League's building on Indigo Plateau (the plateau's tiles name only rock and ground): the solid tiles round the
    // lobby's door, drawn as Yellow's big hall with its roof.
    if (const FIntPoint* League = D->Landmarks.Find(TEXT("POKEMON_LEAGUE")))
    {
        FIntRect R(*League, *League + FIntPoint(1, 1));
        const FYMap* Plateau = nullptr;
        for (const FYMap& M : D->Kanto) if (M.WorldRect().Contains(*League)) Plateau = &M;
        if (Plateau)
        {
            auto Solid = [&](FIntPoint T) { const FIntPoint L = T - Plateau->Origin; return Plateau->Contains(L.X, L.Y) && Plateau->Look(L.X, L.Y) == TEXT('x'); };
            // Up from the door, and out sideways along the hall's front, while the tiles stay solid.
            while (Solid(FIntPoint(League->X, R.Min.Y - 1)) && R.Height() < 8) --R.Min.Y;
            while (Solid(FIntPoint(R.Min.X - 1, League->Y - 1)) && R.Width() < 16) --R.Min.X;
            while (Solid(FIntPoint(R.Max.X, League->Y - 1)) && R.Width() < 16) ++R.Max.X;
            const FName Id(TEXT("POKEMON_LEAGUE"));
            const FVector Size(R.Width() * 100.f, R.Height() * 100.f, 1.f);
            K.Add(K.Get(EShape::Cube, FarWall, Id, TEXT("feature"), TEXT("tex_stone_wall"), 250.f), K.Centre(R, 300.f), Size * FVector(1, 1, 600.f));
            K.Add(K.Get(EShape::Cube, PYRColor::Hex(0x4f6fa8), Id, TEXT("feature"), TEXT("tex_roof_tiles"), 200.f), K.Centre(R, 630.f), FVector(Size.X + 60.f, Size.Y + 60.f, 60.f));
            K.Add(K.Get(EShape::Cone, PYRColor::Hex(0x4f6fa8), Id, TEXT("feature")), K.Centre(R, 800.f), FVector(Size.X * .6f, Size.Y * .6f, 300.f));
        }
    }

    // ---- 2. Between and beyond the maps: the Town Map's land and sea, to the horizon.
    constexpr int32 Cell = 4, Coarse = 16, Reach = 1300, NearBand = 170;
    const FIntRect Far(Box.Min - FIntPoint(Reach, Reach), Box.Max + FIntPoint(Reach, Reach));
    const FIntRect Near(Box.Min - FIntPoint(NearBand, NearBand), Box.Max + FIntPoint(NearBand, NearBand));
    const int32 GW = Far.Width() / Cell + 1, GH = Far.Height() / Cell + 1;
    TArray<uint8> Grid;   // 0: drawn already, 1: land, 2: sea, 3: the far part of a rock border (its plateau drawn already)
    Grid.SetNumZeroed(GW * GH);
    auto CellRect = [&](int32 Gx, int32 Gy) { const FIntPoint A = Far.Min + FIntPoint(Gx * Cell, Gy * Cell); return FIntRect(A, A + FIntPoint(Cell, Cell)); };
    auto Dist = [&Built](FIntPoint T)
    {
        int32 Best = TNumericLimits<int32>::Max();
        for (const FIntRect& R : Built)
        {
            const int32 Dx = FMath::Max3(R.Min.X - T.X, 0, T.X - (R.Max.X - 1)), Dy = FMath::Max3(R.Min.Y - T.Y, 0, T.Y - (R.Max.Y - 1));
            Best = FMath::Min(Best, FMath::Max(Dx, Dy));
        }
        return Best;
    };
    // Room round a tile before any map drawn (built or out of the range): no peak or hill reaches over a map, as
    // Rock Tunnel's did over Route 9 east of Cerulean (warp-016).
    TArray<FIntRect> Drawn = Built;
    for (const FYMap& M : D->Kanto) Drawn.Add(M.WorldRect());
    auto Room = [&Drawn](FIntPoint T)
    {
        int32 Best = TNumericLimits<int32>::Max();
        for (const FIntRect& R : Drawn)
        {
            const int32 Dx = FMath::Max3(R.Min.X - T.X, 0, T.X - (R.Max.X - 1)), Dy = FMath::Max3(R.Min.Y - T.Y, 0, T.Y - (R.Max.Y - 1));
            Best = FMath::Min(Best, FMath::Max(Dx, Dy));
        }
        return Best;
    };
    for (int32 Gy = 0; Gy < GH; ++Gy)
        for (int32 Gx = 0; Gx < GW; ++Gx)
        {
            const FIntRect R = CellRect(Gx, Gy);
            bool bFree = true, bRim = Near.Intersect(R);
            if (bRim)
                for (int32 Y = R.Min.Y; Y < R.Max.Y; ++Y)
                    for (int32 X = R.Min.X; X < R.Max.X; ++X)
                    {
                        const FIntPoint T(X, Y);
                        bFree &= !Covered(T) && !KantoMapTiles.Contains(T);
                        const TCHAR* L = SceneryLooks.Find(T);
                        const FName* Src = ScenerySource.Find(T);
                        bRim &= L && *L == TEXT('r') && Src && *Src == TEXT("border");
                    }
            if (bFree) Grid[Gy * GW + Gx] = IsKantoSea(R.Min + FIntPoint(Cell / 2, Cell / 2)) ? 2 : 1;
            // A rock border's plateau runs on (YellowWorld.cpp's RockRing) past a dozen tiles out: the land's features
            // stand on it from there, so past the wall the view isn't a flat plain (visual-078, -072, -084).
            // Not where the sea lies beyond: the rock ends there as a cape (Route 25 east of Bill's).
            else if (bRim && Dist(R.Min + FIntPoint(Cell / 2, Cell / 2)) >= 12 && !IsKantoSea(R.Min + FIntPoint(Cell / 2, Cell / 2))) Grid[Gy * GW + Gx] = 3;
        }
    // The ground and the sea, merged.
    for (const uint8 C : {uint8(1), uint8(2)})
    {
        TArray<FIntRect> Out;
        Rects(GW, GH, [&](int32 X, int32 Y) { return Grid[Y * GW + X] == C; }, Out);
        for (const FIntRect& G : Out)
        {
            const FIntRect R(Far.Min + G.Min * Cell, Far.Min + G.Max * Cell);
            const FVector Size(R.Width() * 100.f, R.Height() * 100.f, 1.f);
            if (C == 1) K.Add(K.Get(EShape::Plane, FarGrass, TEXT("KANTO_GROUND"), TEXT("ground"), TEXT("tex_grass"), 300.f), K.Centre(R, .5f), Size);
            else
            {
                K.Add(K.Get(EShape::Cube, FarWaterDeep, TEXT("SEA"), TEXT("sea")), K.Centre(R, -40.f), Size * FVector(1, 1, 50.f));
                K.Add(K.Get(EShape::Plane, FarWater, TEXT("SEA"), TEXT("sea"), TEXT("tex_water"), 400.f), K.Centre(R, -14.f), Size);
            }
        }
    }
    // Which map or landmark a piece of land belongs to: a landmark's massif round it, else the nearest map.
    struct FMark { FName Id; FIntPoint At; float Radius; };
    TArray<FMark> Marks;
    // Mt. Moon's massif spreads round the cave between Route 3 and Route 4 (the Town Map's Mt. Moon square and the next);
    // the League's and Victory Road's over the Plateau north-west of Route 22; Rock Tunnel's by Route 10.
    if (const FIntPoint* P = D->Landmarks.Find(TEXT("MT_MOON"))) Marks.Add({TEXT("MT_MOON"), *P, 48.f});
    if (const FIntPoint* P = D->Landmarks.Find(TEXT("POKEMON_LEAGUE"))) Marks.Add({TEXT("POKEMON_LEAGUE"), *P, 100.f});
    if (const FIntPoint* P = D->Landmarks.Find(TEXT("VICTORY_ROAD"))) Marks.Add({TEXT("VICTORY_ROAD"), *P, 80.f});
    if (const FIntPoint* P = D->Landmarks.Find(TEXT("ROCK_TUNNEL"))) Marks.Add({TEXT("ROCK_TUNNEL"), *P, 55.f});
    TArray<TPair<FName, FIntRect>> AllMaps;
    for (const FYMap* M : MapList) if (M->bOutdoor) AllMaps.Add({M->Id, M->WorldRect()});
    for (const FYMap& M : D->Kanto) AllMaps.Add({M.Id, M.WorldRect()});
    auto OwnerOf = [&](FIntPoint T, const FMark*& OutMark) -> FName
    {
        OutMark = nullptr;
        float BestMark = TNumericLimits<float>::Max();
        for (const FMark& Mk : Marks)
        {
            const float Dm = FVector2D::Distance(FVector2D(T), FVector2D(Mk.At));
            if (Dm < Mk.Radius && Dm / Mk.Radius < BestMark) { BestMark = Dm / Mk.Radius; OutMark = &Mk; }
        }
        if (OutMark) return OutMark->Id;
        FName Best;
        int64 BestD = TNumericLimits<int64>::Max();
        for (const auto& P : AllMaps)
        {
            const int64 Dx = FMath::Max3(P.Value.Min.X - T.X, 0, T.X - (P.Value.Max.X - 1)), Dy = FMath::Max3(P.Value.Min.Y - T.Y, 0, T.Y - (P.Value.Max.Y - 1));
            if (Dx * Dx + Dy * Dy < BestD) { BestD = Dx * Dx + Dy * Dy; Best = P.Key; }
        }
        return Best;
    };
    // The land's features, by distance from the maps built: tree lines close by, rolling hills further, then layered
    // mountains to the horizon (taller and bluer the further they stand); a landmark's massif round it.
    int32 Trees = 0, Hills = 0, Mountains = 0, Massif = 0;
    auto GridAt = [&](FIntPoint T) -> uint8 { const int32 Gx = (T.X - Far.Min.X) / Cell, Gy = (T.Y - Far.Min.Y) / Cell; return Gx >= 0 && Gy >= 0 && Gx < GW && Gy < GH ? Grid[Gy * GW + Gx] : 0; };
    auto Land = [&](FIntPoint T) { const uint8 G = GridAt(T); return G == 1 || G == 3; };
    auto Base = [&](FIntPoint T) { return GridAt(T) == 3 ? FarRockHeight : 0.f; };
    for (int32 Gy = 0; Gy < GH; ++Gy)
        for (int32 Gx = 0; Gx < GW; ++Gx)
        {
            // Every 4 tiles a tree, every 8 a massif peak.
            const FIntPoint C = Far.Min + FIntPoint(Gx * Cell + Cell / 2, Gy * Cell + Cell / 2);
            if (!Land(C) || !Near.Contains(C)) continue;
            const int32 Dc = Dist(C);
            const FMark* Mark = nullptr;
            const FName Src = OwnerOf(C, Mark);
            if (Mark)
            {
                // A massif: rocky peaks, tallest toward the landmark, never reaching over the maps (the foot of a peak
                // stays 6 tiles short of them, so the low ones stand closest).
                if (Dc < 12 || Gx % 2 || Gy % 2 || Hash01(C.X, C.Y, 11) > .75f) continue;
                const int32 Rm = Room(C);
                if (Rm < 8) continue;
                const FVector At = Offset + FVector((C.X + Hash01(C.X, C.Y, 3) * 4.f - 2.f) * 100.f, (C.Y + Hash01(C.X, C.Y, 4) * 4.f - 2.f) * 100.f, Base(C));
                const float Near01 = 1.f - FVector2D::Distance(FVector2D(C), FVector2D(Mark->At)) / Mark->Radius;
                float Wd = FMath::Min(FMath::Lerp(1800.f, 7000.f, Near01) * FMath::Lerp(.8f, 1.2f, Hash01(C.X, C.Y, 13)), (FMath::Min(Dc, Rm) - 8) * 200.f);
                // m4u: Mt. Moon's no taller than half its distance from the maps (about 27 degrees up from their edge),
                // keeping its shape. Its near peaks stood up to 44 degrees and, from inside Pewter, filled the top of the
                // picture with flat tan where the sky should be (the blind judge's empty horizon). The League's and Victory
                // Road's stay as tall, seen from Route 22. All painted rock rather than flat colour.
                const float Tall = Wd * FMath::Lerp(.55f, .8f, Hash01(C.X, C.Y, 12));
                const float H = Mark->Id == TEXT("MT_MOON") ? FMath::Min(Tall, (Dc - 6) * 50.f) : Tall;
                Wd *= H / Tall;
                const FRotator Yaw(0, Hash01(C.X, C.Y, 14) * 360.f, 0);
                K.Add(K.Get(EShape::Cone, (Gx / 2 + Gy / 2) % 3 ? MountRock : MountRock * .9f, Src, TEXT("feature"), TEXT("tex_rock_cliff"), 900.f),
                    At + FVector(0, 0, H * .5f - 40.f), FVector(Wd, Wd * .85f, H), Yaw);
                // m4v: no lighter cap over the top any more: its rim drew a hard horizontal band across every peak's
                // painted rock (the blind judge's texture seams, Route 4 by Mt. Moon).
                ++Massif;
                continue;
            }
            if (Dc < 6 || Dc > 140) continue;
            // Not under a massif's slopes, except on the strip between the maps and Mt. Moon's first peaks (their feet
            // stay 8 tiles out): m4u, the grass shelf past Route 4's wall stood bare (the blind judge's empty horizon), so
            // it gets trees and boulders like the rest of the land. (The League's massif keeps its clear foot: Route 22
            // looks over it to Victory Road.)
            bool bUnder = false, bMoon = false;
            for (const FMark& Mk : Marks)
            {
                const bool bIn = FVector2D::Distance(FVector2D(C), FVector2D(Mk.At)) < Mk.Radius + 10.f;
                bUnder |= bIn;
                bMoon |= bIn && Mk.Id == TEXT("MT_MOON");
            }
            if (bUnder && (Dc >= 9 || !bMoon)) continue;
            if (bUnder && Hash01(C.X, C.Y, 23) < .35f)
            {
                const FVector Rk = Offset + FVector((C.X + Hash01(C.X, C.Y, 24) * 2.f - 1.f) * 100.f, (C.Y + Hash01(C.X, C.Y, 25) * 2.f - 1.f) * 100.f, Base(C));
                const float Rs = FMath::Lerp(90.f, 200.f, Hash01(C.X, C.Y, 26));
                K.Add(K.Get(EShape::Sphere, MountRock, Src, TEXT("feature"), TEXT("tex_rock_cliff"), 300.f), Rk + FVector(0, 0, Rs * .25f), FVector(Rs * 1.3f, Rs, Rs * .8f));
                continue;
            }
            if (Noise(C.X, C.Y, 22.f, 21) < (bUnder ? .25f : .4f)) continue;
            const FVector At = Offset + FVector((C.X + Hash01(C.X, C.Y, 3) * 2.f - 1.f) * 100.f, (C.Y + Hash01(C.X, C.Y, 4) * 2.f - 1.f) * 100.f, Base(C));
            // A round anime tree, big enough to read as a tree line from the road.
            const float S = FMath::Lerp(1.5f, 2.2f, Hash01(C.X, C.Y, 5));
            K.Add(K.Get(EShape::Cylinder, FarTrunk, Src, TEXT("feature")), At + FVector(0, 0, 60.f * S), FVector(40.f, 40.f, 120.f) * S);
            K.Add(K.Get(EShape::Sphere, Hash01(C.X, C.Y, 6) < .5f ? FarLeaf : FarLeafLight, Src, TEXT("feature")), At + FVector(0, 0, 210.f * S), FVector(260.f, 260.f, 230.f) * S);
            ++Trees;
        }
    for (int32 Y = Far.Min.Y; Y < Far.Max.Y; Y += Coarse)
        for (int32 X = Far.Min.X; X < Far.Max.X; X += Coarse)
        {
            const FIntPoint C(X + Coarse / 2, Y + Coarse / 2);
            if (!Land(C)) continue;
            const int32 Dc = Dist(C);
            const FMark* Mark = nullptr;
            if (Dc < 28) continue;
            const FName Src = OwnerOf(C, Mark);
            if (Mark && Dc < NearBand) continue;   // the massif's own peaks stand there
            const FVector At = Offset + FVector((C.X + (Hash01(X, Y, 31) - .5f) * Coarse) * 100.f, (C.Y + (Hash01(X, Y, 32) - .5f) * Coarse) * 100.f, Base(C));
            const float Out01 = FMath::Clamp((Dc - 180.f) / 800.f, 0.f, 1.f);
            // Past 500 tiles the ranges are wider, one on every other square of the grid.
            const bool bWide = Dc > 500;
            if (bWide && ((X - Far.Min.X) / Coarse % 2 || (Y - Far.Min.Y) / Coarse % 2)) continue;
            if (Dc >= 180 && Hash01(X, Y, 33) < FMath::Lerp(.35f, .9f, Out01))
            {
                // Mountains: wider and taller with distance, the far ranges bluer (the air between).
                const float H = FMath::Lerp(4000.f, 16000.f, Out01) * FMath::Lerp(.7f, 1.3f, Hash01(X, Y, 34));
                const float Wd = FMath::Min(H * FMath::Lerp(1.6f, 2.6f, Hash01(X, Y, 35)), (FMath::Min(Dc, Room(C)) - Coarse) * 200.f);
                if (Wd < 1500.f) continue;
                const float Hm = FMath::Min(H, Wd * .9f);
                const FLinearColor Col = FLinearColor::LerpUsingHSV(MountGreen, MountBlue, FMath::RoundToInt(FMath::Clamp(Out01 * 1.2f, 0.f, 1.f) * 3.f) / 3.f);
                const FRotator Yaw(0, Hash01(X, Y, 36) * 360.f, 0);
                K.Add(K.Get(EShape::Cone, Col, Src, TEXT("feature")), At + FVector(0, 0, Hm * .5f - 100.f), FVector(Wd, Wd * .85f, Hm), Yaw);
                ++Mountains;
            }
            else if (Dc < 520 && Noise(C.X, C.Y, 70.f, 41) > .4f)
            {
                // A rolling hill, half sunk in the ground.
                const float Wd = FMath::Min(FMath::Lerp(2200.f, 4800.f, Hash01(X, Y, 42)), (Room(C) - Coarse) * 200.f), H = FMath::Lerp(700.f, 1900.f, Hash01(X, Y, 43));
                if (Wd < 1500.f) continue;
                K.Add(K.Get(EShape::Sphere, Hash01(X, Y, 44) < .5f ? FarHill : FarHillDark, Src, TEXT("feature"), TEXT("tex_grass"), 600.f), At + FVector(0, 0, -H * .2f),
                    FVector(Wd, Wd * FMath::Lerp(.7f, 1.f, Hash01(X, Y, 45)), H));
                ++Hills;
            }
        }
    UE_LOG(LogYellow, Display, TEXT("YELLOW_KANTO_FAR %d Kanto maps (%d tiles), %d trees, %d hills, %d mountains, %d massif peaks, %d instances in %d components, %.2f s"),
        D->Kanto.Num(), MapTiles, Trees, Hills, Mountains, Massif, K.Instances, K.Isms.Num(), FPlatformTime::Seconds() - Start);
}
