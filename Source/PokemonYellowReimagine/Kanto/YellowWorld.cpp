#include "Kanto/YellowWorld.h"
#include "World/PYRAreaLook.h"
#include "World/PYRKit.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/OverlapResult.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "PokemonYellowReimagineCharacter.h"
#include "ProceduralMeshComponent.h"
#include "World/PYRPaint.h"

using namespace PYR;

namespace
{
constexpr int32 ChunkSize = 16;
constexpr float DomeRadius = 250000.f;

FIntPoint ChunkOf(FIntPoint K)
{
    return FIntPoint(FMath::FloorToInt(K.X / float(ChunkSize)), FMath::FloorToInt(K.Y / float(ChunkSize)));
}

// Greedy rectangles over a W x H grid of cells where In(x, y) is true.
void Greedy(int32 W, int32 H, TFunctionRef<bool(int32, int32)> In, TArray<FIntRect>& Out)
{
    TBitArray<> Used(false, W * H);
    for (int32 Y = 0; Y < H; ++Y)
        for (int32 X = 0; X < W; ++X)
        {
            if (Used[Y * W + X] || !In(X, Y)) continue;
            int32 RW = 1;
            while (X + RW < W && !Used[Y * W + X + RW] && In(X + RW, Y)) ++RW;
            int32 RH = 1;
            for (;;)
            {
                if (Y + RH >= H) break;
                bool bRow = true;
                for (int32 I = 0; I < RW && bRow; ++I) bRow = !Used[(Y + RH) * W + X + I] && In(X + I, Y + RH);
                if (!bRow) break;
                ++RH;
            }
            for (int32 J = 0; J < RH; ++J) for (int32 I = 0; I < RW; ++I) Used[(Y + J) * W + X + I] = true;
            Out.Add(FIntRect(X, Y, X + RW, Y + RH));
        }
}
}

// ------------------------------------------------------------------------------------------------ map actor
AYellowMapActor::AYellowMapActor()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent->SetMobility(EComponentMobility::Movable);
}

// ------------------------------------------------------------------------------------------------ world
AYellowWorld::AYellowWorld()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent->SetMobility(EComponentMobility::Movable);
}

AYellowWorld* AYellowWorld::Get(const UObject* Ctx)
{
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    if (!World) return nullptr;
    for (TActorIterator<AYellowWorld> It(World); It; ++It)
        if (It->bMain) return *It;
    return nullptr;
}

void AYellowWorld::Build(TSharedPtr<const FYellowDataSet> InData, const FVector& InOffset, bool bInMain)
{
    Data = InData;
    Offset = InOffset;
    bMain = bInMain;
    if (bMain) Tags.AddUnique(TEXT("YellowWorld.Main"));
    SetActorLocation(Offset);
    if (!Data.IsValid() || !Data->IsLoaded())
    {
        UE_LOG(LogYellow, Error, TEXT("YellowData missing: no maps to build (%s)"), Data.IsValid() ? *Data->Root : TEXT("no data"));
        if (bMain) BuildLook();
        bBuilt = true;
        return;
    }
    Index();
    const double Start = FPlatformTime::Seconds();
    for (const FYMap* Map : MapList) BuildMap(*Map);
    BuildScenery();
    if (bMain) BuildLook();
    bBuilt = true;
    UE_LOG(LogYellow, Display, TEXT("YELLOW_WORLD_BUILT maps=%d collision_rects=%d buildings=%d in %.2f s at %s"),
        MapActors.Num(), TotalCollisionRects(), Buildings.Num(), FPlatformTime::Seconds() - Start, *Offset.ToCompactString());
}

void AYellowWorld::Index()
{
    MapList.Reset();
    Chunks.Reset();
    for (const FName& Id : Data->MapOrder)
    {
        const FYMap* M = Data->Map(Id);
        if (!M) continue;
        const int32 Index = MapList.Add(M);
        const FIntRect R = M->WorldRect();
        const FIntPoint C0 = ChunkOf(R.Min), C1 = ChunkOf(R.Max - FIntPoint(1, 1));
        for (int32 Y = C0.Y; Y <= C1.Y; ++Y)
            for (int32 X = C0.X; X <= C1.X; ++X) Chunks.FindOrAdd(FIntPoint(X, Y)).Add(Index);
    }
}

FIntPoint AYellowWorld::ToKanto(const FVector& W) const
{
    return FIntPoint(FMath::FloorToInt((W.X - Offset.X) / YTile::Size), FMath::FloorToInt((W.Y - Offset.Y) / YTile::Size));
}

FVector AYellowWorld::KantoCenter(FIntPoint K) const
{
    return Offset + FVector((K.X + .5f) * YTile::Size, (K.Y + .5f) * YTile::Size, 0.f);
}

FVector AYellowWorld::TileCenter(FName Map, int32 X, int32 Y) const
{
    const FYMap* M = Data.IsValid() ? Data->Map(Map) : nullptr;
    return M ? KantoCenter(M->Origin + FIntPoint(X, Y)) : Offset;
}

const FYMap* AYellowWorld::MapAtKanto(FIntPoint K) const
{
    const TArray<int32>* In = Chunks.Find(ChunkOf(K));
    if (!In) return nullptr;
    for (int32 I : *In)
    {
        const FYMap* M = MapList[I];
        const FIntPoint L = K - M->Origin;
        if (M->Contains(L.X, L.Y)) return M;
    }
    return nullptr;
}

const FYMap* AYellowWorld::MapAt(const FVector& W, FIntPoint* OutLocal) const
{
    const FIntPoint K = ToKanto(W);
    const FYMap* M = MapAtKanto(K);
    if (M && OutLocal) *OutLocal = K - M->Origin;
    return M;
}

TCHAR AYellowWorld::TileAt(FIntPoint K) const
{
    const FYMap* M = MapAtKanto(K);
    return M ? M->Tile(K.X - M->Origin.X, K.Y - M->Origin.Y) : TEXT('#');
}

TCHAR AYellowWorld::LookAt(FIntPoint K) const
{
    const FYMap* M = MapAtKanto(K);
    return M ? M->Look(K.X - M->Origin.X, K.Y - M->Origin.Y) : TEXT(' ');
}

bool AYellowWorld::ProbeBlocked(FIntPoint K) const
{
    UWorld* World = GetWorld();
    if (!World) return false;
    // A trace down through the tile's middle from above the walls (UE 5.8's instanced collision answers traces, not overlaps).
    TArray<FHitResult> Hits;
    FCollisionObjectQueryParams Objects(ECC_WorldStatic);
    const FVector At = KantoCenter(K);
    World->LineTraceMultiByObjectType(Hits, At + FVector(0, 0, YTile::WallHeight + 200.f), At + FVector(0, 0, 20.f), Objects);
    for (const FHitResult& H : Hits)
        for (const auto& Pair : MapActors)
            if (Pair.Value && H.GetComponent() == Pair.Value->Collision) return true;
    return false;
}

bool AYellowWorld::WarpDestination(FName MapId, int32 WarpIndex, FName LastOutdoor, FName& OutMap, int32& OutWarp) const
{
    const FYMap* M = Data.IsValid() ? Data->Map(MapId) : nullptr;
    if (!M || !M->Warps.IsValidIndex(WarpIndex)) return false;
    const FYWarp& W = M->Warps[WarpIndex];
    FName To = W.ToMap;
    if (To.IsNone() || To == TEXT("LAST_MAP") || To == TEXT("-1")) To = LastOutdoor;
    const FYMap* Dest = Data->Map(To);
    if (!Dest) return false;
    const int32 Index = Data->WarpIndex(*Dest, W.ToWarp);
    if (Index == INDEX_NONE) return false;
    OutMap = To;
    OutWarp = Index;
    return true;
}

bool AYellowWorld::IsEdgeBlocked(FIntPoint K, EYDir Dir) const
{
    return Edges.Contains(FIntVector(K.X, K.Y, int32(Dir)));
}

bool AYellowWorld::CanMove(FIntPoint K, EYDir Dir) const
{
    const FIntPoint To = K + YDir::Delta(Dir);
    return !IsSolidAt(To) && MapAtKanto(To) != nullptr && !IsEdgeBlocked(K, Dir);
}

AYellowMapActor* AYellowWorld::MapActor(FName Map) const
{
    const TObjectPtr<AYellowMapActor>* Found = MapActors.Find(Map);
    return Found ? Found->Get() : nullptr;
}

const FYBuilding* AYellowWorld::BuildingWithDoorTo(FName LeadsTo) const
{
    for (const FYBuilding& B : Buildings) if (B.Leads == LeadsTo) return &B;
    return nullptr;
}

int32 AYellowWorld::TotalCollisionRects() const
{
    int32 N = 0;
    for (const auto& Pair : MapActors) if (Pair.Value) N += Pair.Value->CollisionRects;
    return N;
}

// ------------------------------------------------------------------------------------------------ building
void AYellowWorld::BuildMap(const FYMap& Map)
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.Owner = this;
    AYellowMapActor* Actor = GetWorld()->SpawnActor<AYellowMapActor>(Offset, FRotator::ZeroRotator, Params);
    Actor->MapId = Map.Id;
    Actor->Tags.Add(FName(*FString::Printf(TEXT("Map.%s"), *Map.Id.ToString())));
    Actor->Tags.Add(TEXT("YellowMap"));
#if WITH_EDITOR
    Actor->SetActorLabel(FString::Printf(TEXT("Map_%s"), *Map.Id.ToString()));
#endif
    MapActors.Add(Map.Id, Actor);
    BuildCollision(Map, Actor);
    BuildLooks(Map, Actor);
}

void AYellowWorld::BuildCollision(const FYMap& Map, AYellowMapActor* Actor)
{
    // The floor: one box under the whole map, its top at Z = 0.
    Actor->Floor = Instanced(Actor, EShape::Cube, PYRColor::Grass, true, false);
    Actor->Floor->SetVisibility(false);
    Actor->Floor->ComponentTags.Add(TEXT("YellowFloor"));
    const FVector Min = Offset + FVector(Map.Origin.X * YTile::Size, Map.Origin.Y * YTile::Size, 0.f);
    Instance(Actor->Floor, Min + FVector(Map.Width * 50.f, Map.Height * 50.f, -30.f), FVector(Map.Width * 100.f + 200.f, Map.Height * 100.f + 200.f, 60.f));

    // Solid tiles and ledges, plus a ring round the map wherever no map continues it, merged into rectangles.
    Actor->Collision = Instanced(Actor, EShape::Cube, PYRColor::Stone, true, false);
    Actor->Collision->SetVisibility(false);
    Actor->Collision->ComponentTags.Add(TEXT("YellowCollision"));
    Actor->Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);   // The camera swings through walls and tree lines.
    const int32 W = Map.Width + 2, H = Map.Height + 2;
    TArray<bool> Solid;
    Solid.SetNumZeroed(W * H);
    for (int32 Y = 0; Y < H; ++Y)
        for (int32 X = 0; X < W; ++X)
        {
            const int32 LX = X - 1, LY = Y - 1;
            if (Map.Contains(LX, LY)) Solid[Y * W + X] = YTile::IsSolid(Map.Tile(LX, LY));
            else Solid[Y * W + X] = MapAtKanto(Map.Origin + FIntPoint(LX, LY)) == nullptr;
        }
    TArray<FIntRect> Rects;
    Greedy(W, H, [&](int32 X, int32 Y) { return Solid[Y * W + X]; }, Rects);
    for (const FIntRect& R : Rects)
    {
        const FVector Size((R.Width()) * YTile::Size, (R.Height()) * YTile::Size, YTile::WallHeight);
        const FVector Center = Offset + FVector((Map.Origin.X + R.Min.X - 1 + R.Width() * .5f) * YTile::Size,
                                                (Map.Origin.Y + R.Min.Y - 1 + R.Height() * .5f) * YTile::Size, YTile::WallHeight * .5f - 50.f);
        Instance(Actor->Collision, Center, Size);
    }
    Actor->CollisionRects = Rects.Num();
    // Blocked edges: a thin wall on the tile's side, and the edge remembered both ways for people and sight lines.
    for (const FYEdge& E : Map.BlockedEdges)
    {
        const FIntPoint K = Map.Origin + FIntPoint(E.X, E.Y);
        const FIntPoint D = YDir::Delta(E.Dir);
        Edges.Add(FIntVector(K.X, K.Y, int32(E.Dir)));
        Edges.Add(FIntVector(K.X + D.X, K.Y + D.Y, int32(YDir::Opposite(E.Dir))));
        const FVector Center = KantoCenter(K) + FVector(D.X * 50.f, D.Y * 50.f, YTile::WallHeight * .5f - 50.f);
        Instance(Actor->Collision, Center, D.X != 0 ? FVector(12.f, 100.f, YTile::WallHeight) : FVector(100.f, 12.f, YTile::WallHeight));
        ++Actor->CollisionRects;
    }
}

void AYellowWorld::FindBuildings(const FYMap& Map, TArray<FYBuilding>& Out) const
{
    auto IsBuildingLook = [](TCHAR L) { return L == TEXT('B') || L == TEXT('R') || L == TEXT('D') || L == TEXT('n'); };
    // Where a door tile leads (a warp on it or right below it), or None.
    auto DoorLeads = [&Map](const FIntPoint& Local)
    {
        for (const FYWarp& Wp : Map.Warps)
            if (Wp.X == Local.X && (Wp.Y == Local.Y || Wp.Y == Local.Y + 1)) return Wp.ToMap;
        return FName();
    };
    // One building from its tiles: its box, doors, where it leads and so its kind.
    auto Make = [&Map, &DoorLeads](const TArray<FIntPoint>& Cells)
    {
        FYBuilding B;
        B.Map = Map.Id;
        FIntRect Box(Cells[0], Cells[0] + FIntPoint(1, 1));
        for (const FIntPoint& P : Cells)
        {
            Box.Min = Box.Min.ComponentMin(P);
            Box.Max = Box.Max.ComponentMax(P + FIntPoint(1, 1));
            if (Map.Look(P.X, P.Y) == TEXT('D')) B.Doors.Add(Map.Origin + P);
        }
        B.Rect = FIntRect(Map.Origin + Box.Min, Map.Origin + Box.Max);
        // What its door leads to decides its kind (a warp on or right below a door tile, else any warp inside).
        for (const FIntPoint& D : B.Doors) if (!DoorLeads(D - Map.Origin).IsNone()) { B.Leads = DoorLeads(D - Map.Origin); break; }
        if (B.Leads.IsNone())
            for (const FYWarp& Wp : Map.Warps)
                if (B.Rect.Contains(Map.Origin + FIntPoint(Wp.X, Wp.Y))) { B.Leads = Wp.ToMap; break; }
        const FString Leads = B.Leads.ToString();
        B.Kind = Leads.Contains(TEXT("POKECENTER")) ? FYBuilding::EKind::Centre
            : Leads.Contains(TEXT("MART")) ? FYBuilding::EKind::Mart
            : Leads.Contains(TEXT("GYM")) ? FYBuilding::EKind::Gym
            : Leads.Contains(TEXT("OAKS_LAB")) ? FYBuilding::EKind::Lab
            : Leads.Contains(TEXT("MUSEUM")) ? FYBuilding::EKind::Museum
            : Leads.Contains(TEXT("GATE")) ? FYBuilding::EKind::Gate
            : Leads.IsEmpty() || Leads == TEXT("None") ? FYBuilding::EKind::Other : FYBuilding::EKind::House;
        return B;
    };
    TBitArray<> Seen(false, Map.Width * Map.Height);
    for (int32 Y = 0; Y < Map.Height; ++Y)
        for (int32 X = 0; X < Map.Width; ++X)
        {
            if (Seen[Y * Map.Width + X] || !IsBuildingLook(Map.Look(X, Y))) continue;
            // Flood fill the connected group.
            TArray<FIntPoint> Cells;
            TArray<FIntPoint> Stack = {FIntPoint(X, Y)};
            Seen[Y * Map.Width + X] = true;
            while (Stack.Num())
            {
                const FIntPoint P = Stack.Pop();
                Cells.Add(P);
                for (const FIntPoint D : {FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)})
                {
                    const FIntPoint Q = P + D;
                    if (!Map.Contains(Q.X, Q.Y) || Seen[Q.Y * Map.Width + Q.X] || !IsBuildingLook(Map.Look(Q.X, Q.Y))) continue;
                    Seen[Q.Y * Map.Width + Q.X] = true;
                    Stack.Add(Q);
                }
            }
            // Buildings that touch on the Game Boy (Cerulean's Pokemon Center against Melanie's house) are one group of
            // tiles; doors leading to different places split it, at the column between them where the walls' height
            // changes (the halfway column if it never does).
            TArray<TPair<int32, FName>> Doors;
            for (const FIntPoint& P : Cells)
                if (Map.Look(P.X, P.Y) == TEXT('D'))
                {
                    const FName To = DoorLeads(P);
                    if (!To.IsNone() && !Doors.ContainsByPredicate([&](const TPair<int32, FName>& E) { return E.Value == To || E.Key == P.X; })) Doors.Add({P.X, To});
                }
            Doors.Sort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B) { return A.Key < B.Key; });
            TArray<int32> Splits;
            if (Doors.Num() > 1)
            {
                TMap<int32, int32> Height;
                for (const FIntPoint& P : Cells) Height.FindOrAdd(P.X)++;
                for (int32 I = 1; I < Doors.Num(); ++I)
                {
                    int32 Best = (Doors[I - 1].Key + Doors[I].Key + 1) / 2, BestJump = 0;
                    for (int32 C = Doors[I - 1].Key + 1; C <= Doors[I].Key; ++C)
                    {
                        const int32 Jump = FMath::Abs(Height.FindRef(C) - Height.FindRef(C - 1));
                        if (Jump > BestJump) { BestJump = Jump; Best = C; }
                    }
                    Splits.Add(Best);
                }
            }
            TArray<TArray<FIntPoint>> Parts;
            Parts.SetNum(Splits.Num() + 1);
            for (const FIntPoint& P : Cells)
            {
                int32 Part = 0;
                while (Part < Splits.Num() && P.X >= Splits[Part]) ++Part;
                Parts[Part].Add(P);
            }
            for (const TArray<FIntPoint>& Part : Parts) if (Part.Num()) Out.Add(Make(Part));
        }
}

// ------------------------------------------------------------------------------------------------ past the edges
// What the Game Boy shows beyond the maps built: where a connection leads out of range, that map's own tiles at
// pokeyellow's offset (the sea south of Pallet Town, Route 23's rocks, Route 9, Route 5); elsewhere each map's border
// block, repeated on its 2x2 grid. All of it is scenery: the collision ring round every map stays closed.
void AYellowWorld::BuildScenery()
{
    constexpr int32 BorderRing = 14, RockRing = 44;
    SceneryLooks.Reset();
    ScenerySource.Reset();
    TArray<const FYMap*> Outdoor;
    for (const FYMap* M : MapList) if (M->bOutdoor) Outdoor.Add(M);
    // A map's border block round it, where no map or neighbour is.
    auto Ring = [this](const FYMap* M, int32 Depth, TMap<FIntPoint, TCHAR>& Out, TMap<FIntPoint, FName>* Source)
    {
        if (M->BorderLooks.Num() < 2) return;
        for (int32 Y = -Depth; Y < M->Height + Depth; ++Y)
            for (int32 X = -Depth; X < M->Width + Depth; ++X)
            {
                if (M->Contains(X, Y)) continue;
                const FIntPoint K = M->Origin + FIntPoint(X, Y);
                if (MapAtKanto(K) || Out.Contains(K) || KantoMapTiles.Contains(K)) continue;
                const FString& Row = M->BorderLooks[((Y % 2) + 2) % 2];
                const int32 Col = ((X % 2) + 2) % 2;
                // A rock border stops BorderRing tiles out where the Town Map has the sea beyond (north and east of
                // Route 25, Bill's cape): the far sea (YellowWorldFar.cpp) runs from there to the horizon (m4s, visual-084).
                const int32 Out2 = FMath::Max(X < 0 ? -X : X >= M->Width ? X - M->Width + 1 : 0, Y < 0 ? -Y : Y >= M->Height ? Y - M->Height + 1 : 0);
                if (Depth > BorderRing && Out2 > BorderRing && Row.IsValidIndex(Col) && Row[Col] == TEXT('r') && Data->TownMapRows.Num() && IsKantoSea(K)) continue;
                Out.Add(K, Row.IsValidIndex(Col) ? Row[Col] : TEXT('T'));
                if (Source) Source->Add(K, TEXT("border"));
            }
    };
    // Kanto: the out-of-range neighbours first (their own tiles win over any border block), then each outdoor map's
    // border block (the first map in the manifest order owns a shared corner).
    PlateauScenery.Reset();
    for (const FYMap& S : Data->Scenery)
    {
        if (S.Tileset.ToUpper() == TEXT("PLATEAU")) PlateauScenery.Add(S.Id);
        for (int32 Y = 0; Y < S.Height; ++Y)
            for (int32 X = 0; X < S.Width; ++X)
            {
                const FIntPoint K = S.Origin + FIntPoint(X, Y);
                const TCHAR L = S.Look(X, Y);
                if (L == TEXT(' ') || MapAtKanto(K) || SceneryLooks.Contains(K)) continue;
                SceneryLooks.Add(K, L);
                ScenerySource.Add(K, S.Id);
            }
    }
    // The rest of Kanto (kanto.json, m4s): every out-of-range map's tiles. Within KantoNear tiles of a map built they are
    // drawn here with the full look; further out YellowWorldFar.cpp draws them merged. A border block never covers them.
    constexpr int32 KantoNear = 40;
    KantoMapTiles.Reset();
    TArray<FIntRect> BuiltRects;
    for (const FYMap* M : Outdoor) BuiltRects.Add(M->WorldRect());
    for (const FYMap& S : Data->Kanto)
    {
        if (S.Tileset.ToUpper() == TEXT("PLATEAU")) PlateauScenery.Add(S.Id);
        for (int32 Y = 0; Y < S.Height; ++Y)
            for (int32 X = 0; X < S.Width; ++X)
            {
                const FIntPoint K = S.Origin + FIntPoint(X, Y);
                const TCHAR L = S.Look(X, Y);
                if (L == TEXT(' ') || MapAtKanto(K) || KantoMapTiles.Contains(K)) continue;
                KantoMapTiles.Add(K, TPair<TCHAR, FName>(L, S.Id));
                if (SceneryLooks.Contains(K)) continue;
                bool bNear = false;
                for (const FIntRect& R : BuiltRects)
                    bNear |= K.X >= R.Min.X - KantoNear && K.X < R.Max.X + KantoNear && K.Y >= R.Min.Y - KantoNear && K.Y < R.Max.Y + KantoNear;
                if (!bNear) continue;
                SceneryLooks.Add(K, L);
                ScenerySource.Add(K, S.Id);
            }
    }
    // A rock border runs on to the horizon (RockRing): a raised plateau ending close by stood up like a slab against
    // the low meadow beyond it (m4r, visual-078).
    // So does a sea border (Route 21's water south of Pallet Town runs on to the horizon, not into a meadow: warp-002).
    auto Depth = [&](const FYMap* M, int32 Normal)
    {
        bool bRock = M->BorderLooks.Num() >= 2, bSea = bRock;
        for (const FString& Row : M->BorderLooks) for (const TCHAR C : Row) { bRock &= C == TEXT('r'); bSea &= C == TEXT('W'); }
        return bRock || bSea ? RockRing : Normal;
    };
    for (const FYMap* M : Outdoor) Ring(M, Depth(M, BorderRing), SceneryLooks, &ScenerySource);
    // Then each out-of-range route's own border block round it, further out (Route 21's sea either side of it and
    // beyond, Route 23's rock), so the view down a route never ends in a bare meadow beside it.
    for (const FYMap& S : Data->Scenery) Ring(&S, Depth(&S, 20), SceneryLooks, &ScenerySource);
    if (SceneryLooks.Num())
    {
        SceneryActor = BuildSceneryCanvas(TEXT("KANTO_SCENERY"), SceneryLooks, PYRColor::Hex(0x86cf5e), false);
        SceneryActor->Tags.Add(TEXT("YellowScenery"));
    }
    BuildKantoFar();
    // Viridian Forest (a room on the Game Boy, drawn as the outdoors): its own ring of border trees.
    for (const FYMap* M : MapList)
    {
        if (M->bOutdoor || M->Tileset.ToUpper() != TEXT("FOREST")) continue;
        TMap<FIntPoint, TCHAR> Looks;
        Ring(M, BorderRing, Looks, nullptr);
        if (Looks.Num()) ForestScenery.Add(M->Id, BuildSceneryCanvas(FName(*(M->Id.ToString() + TEXT("_SCENERY"))), Looks, PYRColor::Hex(0x5fa84a), true));
    }
    UE_LOG(LogYellow, Display, TEXT("YELLOW_SCENERY %d tiles past the edges (%d from %d neighbours), %d forest rings"), SceneryLooks.Num(),
        SceneryLooks.Num() - ScenerySource.FilterByPredicate([](const TPair<FIntPoint, FName>& P) { return P.Value == TEXT("border"); }).Num(), Data->Scenery.Num(),
        ForestScenery.Num());
}

AYellowMapActor* AYellowWorld::BuildSceneryCanvas(FName Id, const TMap<FIntPoint, TCHAR>& Looks, const FLinearColor& MeadowColour, bool bForest)
{
    FIntRect Box(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max(), TNumericLimits<int32>::Lowest(), TNumericLimits<int32>::Lowest());
    for (const auto& Pair : Looks)
    {
        Box.Min = Box.Min.ComponentMin(Pair.Key);
        Box.Max = Box.Max.ComponentMax(Pair.Key + FIntPoint(1, 1));
    }
    // One scenery "map" over the whole box: ' ' where a real map (or nothing) is, else the look; every tile solid.
    FYMap Canvas;
    Canvas.Id = Id;
    Canvas.Name = Id.ToString();
    Canvas.bOutdoor = true;
    Canvas.Tileset = bForest ? TEXT("FOREST") : TEXT("OVERWORLD");
    Canvas.Origin = Box.Min;
    Canvas.Width = Box.Width();
    Canvas.Height = Box.Height();
    for (int32 Y = 0; Y < Canvas.Height; ++Y)
    {
        FString Row = FString::ChrN(Canvas.Width, TEXT(' '));
        for (int32 X = 0; X < Canvas.Width; ++X)
            if (const TCHAR* L = Looks.Find(Canvas.Origin + FIntPoint(X, Y)))
            {
                // PLATEAU rock (Route 23, Indigo Plateau) is drawn as a rock face, its open ground as grass (m4s: the
                // Plateau's tiles are green, warp-015).
                const FName* Src = bForest ? nullptr : ScenerySource.Find(Canvas.Origin + FIntPoint(X, Y));
                Row[X] = *L == TEXT('x') ? TEXT('r') : *L == TEXT('p') && Src && PlateauScenery.Contains(*Src) ? TEXT('s') : *L;
            }
        Canvas.Looks.Add(Row);
        Canvas.Tiles.Add(FString::ChrN(Canvas.Width, TEXT('#')));
    }
    // m4v judge round 2: past the edges a fence run out in open water (Route 21's border columns south of Pallet) stood
    // on a one-tile strip and read as fences floating across the sea. Water beside such a fence becomes a grass bank, so
    // each run sits on continuous land; not within 8 tiles of a real map, so the near views keep their water as it was.
    if (!bForest)
    {
        auto NearMap = [&](int32 X, int32 Y)
        {
            for (int32 Dy = -8; Dy <= 8; ++Dy)
                for (int32 Dx = -8; Dx <= 8; ++Dx)
                {
                    const FIntPoint At = Canvas.Origin + FIntPoint(X + Dx, Y + Dy);
                    if (!Looks.Contains(At)) return true;   // a real map (or nothing) is there
                }
            return false;
        };
        TArray<FIntPoint> Bank;
        for (int32 Y = 0; Y < Canvas.Height; ++Y)
            for (int32 X = 0; X < Canvas.Width; ++X)
            {
                if (Canvas.Looks[Y][X] != TEXT('W')) continue;
                bool bByFence = false;
                for (const FIntPoint D : {FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)})
                {
                    const int32 Nx = X + D.X, Ny = Y + D.Y;
                    bByFence |= Nx >= 0 && Ny >= 0 && Nx < Canvas.Width && Ny < Canvas.Height && Canvas.Looks[Ny][Nx] == TEXT('F');
                }
                if (bByFence && !NearMap(X, Y)) Bank.Add(FIntPoint(X, Y));
            }
        for (const FIntPoint& B : Bank) Canvas.Looks[B.Y][B.X] = TEXT('s');
    }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.Owner = this;
    AYellowMapActor* Actor = GetWorld()->SpawnActor<AYellowMapActor>(Offset, FRotator::ZeroRotator, Params);
    Actor->MapId = Canvas.Id;
#if WITH_EDITOR
    Actor->SetActorLabel(FString::Printf(TEXT("Map_%s"), *Id.ToString()));
#endif
    BuildLooks(Canvas, Actor, true);
    // Far beyond: a meadow to the horizon, a little below the maps, so no view ever ends in the sky. It lies under the
    // water too (the water's surface is 14 cm under the banks, over a bed down to 65 cm), or it hides every pond.
    // (m4s: past the scenery the far Kanto of YellowWorldFar.cpp covers it to the horizon; it still catches a look
    // under the ground at a pond's edge.)
    const FVector Center = Offset + FVector((Box.Min.X + Box.Width() * .5f) * YTile::Size, (Box.Min.Y + Box.Height() * .5f) * YTile::Size, -30.f);
    UInstancedStaticMeshComponent* Meadow = Instanced(Actor, EShape::Plane, MeadowColour, false, false);
    PYRKit::Apply(Meadow, EPYRKit::GrassShort, MeadowColour);
    // m4v: the forest's meadow runs about 5 km out, so it meets the sky dome (2.5 km) instead of ending 300 m out in a
    // depth jump the toon pass inked as a straight line along the horizon under the painted canopy sky (the blind
    // judge's backdrop seam; the forest's grade also raises DepthEdge for the crease that is left).
    // Round 2: the forest's meadow stops 15 tiles past its border trees instead, so past the last crowns the view
    // meets the dome's painted canopy (below the horizon too) rather than a hazed, flat green strip of distant ground.
    const int32 Reach = bForest ? 30 : 600;
    Instance(Meadow, Center, FVector((Box.Width() + Reach) * YTile::Size, (Box.Height() + Reach) * YTile::Size, 1.f));
    return Actor;
}

// ------------------------------------------------------------------------------------------------ what is drawn
bool AYellowWorld::IsMapDrawn(FName Map) const
{
    const FYMap* M = Data.IsValid() ? Data->Map(Map) : nullptr;
    const FYMap* F = Data.IsValid() ? Data->Map(Focus) : nullptr;
    if (!M || !F) return true;
    return F->bOutdoor ? M->bOutdoor : M->Id == F->Id;
}

void AYellowWorld::SetFocus(FName Map)
{
    if (Map == Focus || !Data.IsValid() || !Data->Map(Map)) return;
    const FYMap* Was = Data->Map(Focus);
    Focus = Map;
    const FYMap* F = Data->Map(Map);
    // m4v: each area's light and colour grade (World/PYRAreaLook), switched here as the maps drawn are.
    if (bMain)
        if (UPYRAreaLook* Look = UPYRAreaLook::Get(this))
        {
            auto OutdoorLike = [](const FYMap* M) { return M && (M->bOutdoor || M->Tileset.ToUpper() == TEXT("FOREST")); };
            Look->OnFocus(Map, OutdoorLike(Was), OutdoorLike(F));
        }
    for (const auto& Pair : MapActors) if (Pair.Value) Pair.Value->SetActorHiddenInGame(!IsMapDrawn(Pair.Key));
    if (SceneryActor) SceneryActor->SetActorHiddenInGame(!F->bOutdoor);
    if (FarActor) FarActor->SetActorHiddenInGame(!F->bOutdoor);
    for (const auto& Pair : ForestScenery) if (Pair.Value) Pair.Value->SetActorHiddenInGame(Pair.Key != Map);
    if (UPYRField* Field = UPYRField::Get(this))
    {
        for (const TWeakObjectPtr<APYRPerson>& P : Field->People) if (P.IsValid()) P->SetCulled(!IsMapDrawn(P->Map));
        for (const TWeakObjectPtr<APYRSignpost>& S : Field->Signs)
            if (S.IsValid() && S->GetRootComponent()) S->GetRootComponent()->SetVisibility(IsMapDrawn(S->Map), true);
        for (const TWeakObjectPtr<APYRItemBall>& B : Field->Balls)
            if (B.IsValid() && B->GetRootComponent()) B->GetRootComponent()->SetVisibility(IsMapDrawn(B->Map), true);
    }
    ShowAllOccluders();
    UE_LOG(LogYellow, Verbose, TEXT("YELLOW_FOCUS %s"), *Map.ToString());
}

// ------------------------------------------------------------------------------------------------ occluders
int32 AYellowWorld::NewOccluder(const FIntRect& KantoTiles, float HeightCm)
{
    FOccluder O;
    O.Tiles = KantoTiles;
    O.Height = HeightCm;
    const int32 Index = Occluders.Add(O);
    for (int32 Y = KantoTiles.Min.Y; Y < KantoTiles.Max.Y; ++Y)
        for (int32 X = KantoTiles.Min.X; X < KantoTiles.Max.X; ++X) OccluderAt.FindOrAdd(FIntPoint(X, Y)).Add(Index);
    return Index;
}

void AYellowWorld::AddOccluderPart(int32 Occluder, UInstancedStaticMeshComponent* Component, int32 Instance)
{
    if (Occluders.IsValidIndex(Occluder) && Component && Instance != INDEX_NONE) Occluders[Occluder].Parts.Add({Component, Instance});
}

void AYellowWorld::SetOccluderRoof(int32 Occluder, UProceduralMeshComponent* Roof, int32 Section)
{
    if (!Occluders.IsValidIndex(Occluder)) return;
    Occluders[Occluder].Roof = Roof;
    Occluders[Occluder].RoofSection = Section;
}

void AYellowWorld::HideOccluder(FOccluder& O, bool bHide)
{
    if (O.bHidden == bHide) return;
    O.bHidden = bHide;
    if (bHide) O.Saved.SetNum(O.Parts.Num());
    for (int32 I = 0; I < O.Parts.Num(); ++I)
    {
        UInstancedStaticMeshComponent* C = O.Parts[I].Key.Get();
        if (!C) continue;
        const bool bLast = I == O.Parts.Num() - 1;
        if (bHide)
        {
            FTransform T;
            C->GetInstanceTransform(O.Parts[I].Value, T, true);
            O.Saved[I] = T;
            T.SetScale3D(FVector(.0001f));
            C->UpdateInstanceTransform(O.Parts[I].Value, T, true, true, true);
        }
        else if (O.Saved.IsValidIndex(I)) C->UpdateInstanceTransform(O.Parts[I].Value, O.Saved[I], true, true, true);
        (void)bLast;
    }
    if (UProceduralMeshComponent* R = O.Roof.Get()) R->SetMeshSectionVisible(O.RoofSection, !bHide);
}

void AYellowWorld::ShowAllOccluders()
{
    for (FOccluder& O : Occluders) if (O.bHidden) HideOccluder(O, false);
}

FName AYellowWorld::FurnitureAt(FName MapId, int32 X, int32 Y) const
{
    const FYMap* M = Data ? Data->Map(MapId) : nullptr;
    const FName* What = M ? Furniture.Find(M->Origin + FIntPoint(X, Y)) : nullptr;
    return What ? *What : NAME_None;
}

int32 AYellowWorld::NumHiddenOccluders() const
{
    int32 N = 0;
    for (const FOccluder& O : Occluders) N += O.bHidden ? 1 : 0;
    return N;
}

bool AYellowWorld::IsOccluderHiddenAt(FIntPoint Kanto) const
{
    if (const TArray<int32>* L = OccluderAt.Find(Kanto)) for (int32 I : *L) if (Occluders[I].bHidden) return true;
    return false;
}

void AYellowWorld::UpdateOccluders(const FVector& Eye, const FVector& Target, const TArray<FVector>& Also)
{
    // Along the lines from the camera to Red's feet and head (and a little to each side), whatever stands higher than
    // the line is in the way: trees and buildings on those tiles are hidden until the view is clear again.
    TSet<int32> Want;
    TArray<FVector> Aims = {Target + FVector(0, 0, -60), Target + FVector(0, 0, 60)};
    for (const FVector& A : Also) { Aims.Add(A + FVector(0, 0, -60)); Aims.Add(A + FVector(0, 0, 50)); }
    for (const FVector& T : Aims)
    {
        const FVector D = Eye - T;
        const float Len = D.Size();
        if (Len < 1.f) continue;
        const FVector Dir = D / Len;
        for (float S = 55.f; S <= Len + 1.f; S += 25.f)
        {
            const FVector P = T + Dir * FMath::Min(S, Len);
            // Near Red the lane is a tile wider each side (m4r: in Viridian Forest the trees of the row behind him,
            // beside the line, walled in the path he stands on, visual-057).
            for (const FVector2D Off : {FVector2D(0, 0), FVector2D(35, 0), FVector2D(-35, 0), FVector2D(0, 35), FVector2D(0, -35)})
                if (const TArray<int32>* L = OccluderAt.Find(ToKanto(P + FVector(Off.X, Off.Y, 0))))
                    for (int32 I : *L) if (P.Z - Offset.Z < Occluders[I].Height) Want.Add(I);
            // A single tree a tile to either side counts too (buildings beside Red stay: Oak's lab beside him is no
            // more in the way than a wall).
            if (S >= 100.f && S < 450.f)
            {
                const FVector2D Side = FVector2D(-Dir.Y, Dir.X).GetSafeNormal() * 110.f;
                for (const FVector2D Off : {Side, -Side})
                    if (const TArray<int32>* L = OccluderAt.Find(ToKanto(P + FVector(Off.X, Off.Y, 0))))
                        for (int32 I : *L)
                            if (Occluders[I].Tiles.Area() == 1 && P.Z - Offset.Z < Occluders[I].Height) Want.Add(I);
            }
        }
    }
    // Right beside the camera too (a roof's eaves a hand's breadth from the lens).
    for (int32 DY = -2; DY <= 2; ++DY)
        for (int32 DX = -2; DX <= 2; ++DX)
            if (const TArray<int32>* L = OccluderAt.Find(ToKanto(Eye + FVector(DX * 90.f, DY * 90.f, 0))))
                for (int32 I : *L) if (Eye.Z - Offset.Z < Occluders[I].Height + 60.f) Want.Add(I);
    for (int32 I = 0; I < Occluders.Num(); ++I)
    {
        const bool bHide = Want.Contains(I);
        if (Occluders[I].bHidden != bHide) HideOccluder(Occluders[I], bHide);
    }
}

// ------------------------------------------------------------------------------------------------ sky and light
void AYellowWorld::BuildLook()
{
    UWorld* World = GetWorld();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // A warm, high summer sun (lux pi: a sunlit surface reads about 1x its base colour) and a captured sky fill.
    Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 3000), FRotator(-52, -125, 0), Params);
    Sun->SetMobility(EComponentMobility::Movable);
    Sun->Tags.Add(TEXT("Sun"));
    UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    SunLight->SetIntensity(3.14159f);
    SunLight->SetLightColor(FLinearColor(1.f, .95f, .86f));
    SunLight->SetDynamicShadowDistanceMovableLight(6000.f);
    SunLight->SetAtmosphereSunLight(false);
    ASkyLight* Fill = World->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    Fill->Tags.Add(TEXT("SkyFill"));
    SkyFill = Fill;
    USkyLightComponent* Sky = Fill->GetLightComponent();
    Sky->SetMobility(EComponentMobility::Movable);
    Sky->SourceType = SLS_CapturedScene;
    Sky->bLowerHemisphereIsBlack = false;
    Sky->SetLowerHemisphereColor(FLinearColor(.35f, .45f, .3f));
    Sky->SetIntensity(1.f);

    // The painted sky: a dome that follows the camera, #6ec6f0 to near-white at the horizon, with summer cumulus.
    SkyDome = World->SpawnActor<AYellowMapActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    SkyDome->Tags.Add(TEXT("SkyDome"));
    UStaticMeshComponent* Dome = Shape(SkyDome, nullptr, EShape::Sphere, FVector::ZeroVector, FVector(DomeRadius * 2.f), PYRColor::Sky, false);
    if (UMaterialInterface* SkyMat = Cast<UMaterialInterface>(LoadOptional(UMaterialInterface::StaticClass(), TEXT("/Game/Look/M_Sky"))))
    {
        // m4v: a dynamic instance, so each area can paint its own sky (PYRAreaLook: Top, Horizon).
        SkyMaterial = UMaterialInstanceDynamic::Create(SkyMat, Dome);
        Dome->SetMaterial(0, SkyMaterial);
    }
    Dome->SetCastShadow(false);
    const FLinearColor CloudWhite = PYRColor::Hex(0xffffff);
    for (int32 i = 0; i < 16; ++i)
    {
        const float Yaw = i * (360.f / 16.f) + Rng.FRandRange(-8, 8);
        const float Dist = Rng.FRandRange(150000, 200000);
        const FVector Base = FRotator(0, Yaw, 0).RotateVector(FVector(Dist, 0, 0)) + FVector(0, 0, Rng.FRandRange(14000, 30000));
        USceneComponent* Cloud = Group(SkyDome, nullptr, Base, FRotator(0, Yaw + 90, 0));
        const float S = Rng.FRandRange(.8f, 1.5f);
        UStaticMeshComponent* Slab = Shape(SkyDome, Cloud, EShape::Cube, FVector(0, 0, 400) * S, FVector(34000, 9000, 900) * S, CloudWhite, false);
        Slab->SetCastShadow(false);
        PYRKit::Apply(Slab, EPYRKit::Paint, CloudWhite);   // m4v: painted, not a flat colour
        for (int32 k = 0; k < 7; ++k)
        {
            const float X = FMath::Lerp(-13000.f, 13000.f, k / 6.f) + Rng.FRandRange(-1500, 1500);
            const float Hh = (1.f - FMath::Abs(X) / 16000.f) * Rng.FRandRange(9000, 15000) + 4000.f;
            UStaticMeshComponent* Puff = Shape(SkyDome, Cloud, EShape::Sphere, FVector(X, Rng.FRandRange(-1500, 1500), Hh * .5f) * S, FVector(Hh * 1.25f, Hh * .9f, Hh) * S, CloudWhite, false);
            Puff->SetCastShadow(false);
            PYRKit::Apply(Puff, EPYRKit::Paint, CloudWhite);
        }
    }
    Sky->RecaptureSky();

    // The global toon pass (two flat tones and ink outlines), unbound and low priority.
    ToonVolume = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    ToonVolume->bUnbound = true;
    ToonVolume->Priority = -10.f;
    ToonVolume->BlendWeight = 1.f;
    ToonVolume->Tags.Add(TEXT("ToonPost"));
    FPostProcessSettings& G = ToonVolume->Settings;
    G.bOverride_AutoExposureMethod = true;
    G.AutoExposureMethod = AEM_Manual;
    G.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    G.AutoExposureApplyPhysicalCameraExposure = false;
    G.bOverride_AutoExposureBias = true;
    G.AutoExposureBias = 0.f;
    G.bOverride_BloomIntensity = true;
    G.BloomIntensity = .15f;
    G.bOverride_VignetteIntensity = true;
    G.VignetteIntensity = .12f;
    G.bOverride_ColorSaturation = true;
    G.ColorSaturation = FVector4(1.12f, 1.12f, 1.12f, 1.f);
    G.bOverride_AmbientOcclusionIntensity = true;
    G.AmbientOcclusionIntensity = 0.f;
    G.bOverride_LocalExposureHighlightContrastScale = true;
    G.LocalExposureHighlightContrastScale = 1.f;
    G.bOverride_LocalExposureShadowContrastScale = true;
    G.LocalExposureShadowContrastScale = 1.f;
    if (UMaterialInterface* Toon = Cast<UMaterialInterface>(LoadOptional(UMaterialInterface::StaticClass(), TEXT("/Game/Look/M_ToonPost"))))
    {
        ToonMaterial = UMaterialInstanceDynamic::Create(Toon, ToonVolume);
        G.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, ToonMaterial));
    }
    else UE_LOG(LogYellow, Error, TEXT("/Game/Look/M_ToonPost is missing: run World/Editor/author_look.py"));
}

void AYellowWorld::SetDomeClouds(bool bShow)
{
    if (!SkyDome) return;
    TArray<UStaticMeshComponent*> Parts;
    SkyDome->GetComponents<UStaticMeshComponent>(Parts);
    for (UStaticMeshComponent* C : Parts)
        if (C && C->GetMaterial(0) != SkyMaterial && C->IsVisible() != bShow) C->SetVisibility(bShow);
}

void AYellowWorld::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bMain) return;
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (SkyDome && PC && PC->PlayerCameraManager)
    {
        const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
        SkyDome->SetActorLocation(FVector(Cam.X, Cam.Y, 0.f));
    }
    // Draw what can be seen from Red's map; hide what stands between the camera and him (outdoors, on his camera).
    APokemonYellowReimagineCharacter* Red = APokemonYellowReimagineCharacter::Find(this);
    if (!Red || !Data.IsValid()) return;
    if (const FYMap* M = MapAt(Red->GetActorLocation())) SetFocus(M->Id);
    const FYMap* F = Data->Map(Focus);
    const bool bOutdoorView = F && (F->bOutdoor || F->Tileset.ToUpper() == TEXT("FOREST"));
    // m4v: a fixed look camera (the look tests' frames) that stands in for Red's follow camera hides what is in the way too.
    const AActor* View = PC ? PC->GetViewTarget() : nullptr;
    if (bOutdoorView && PC && PC->PlayerCameraManager && (View == Red || (View && View->ActorHasTag(TEXT("PYR.FollowRed")))))
    {
        // Whoever stands near Red (the one he talks to, a rival walking up) must be seen too.
        TArray<FVector> Near;
        if (const UPYRField* Field = UPYRField::Get(this))
            for (const TWeakObjectPtr<APYRPerson>& P : Field->People)
                if (P.IsValid() && P->IsShown() && P->Map == Focus && FVector::Dist2D(P->GetActorLocation(), Red->GetActorLocation()) < 260.f) Near.Add(P->GetActorLocation());
        UpdateOccluders(PC->PlayerCameraManager->GetCameraLocation(), Red->GetActorLocation(), Near);
    }
    else if (NumHiddenOccluders()) ShowAllOccluders();
}
