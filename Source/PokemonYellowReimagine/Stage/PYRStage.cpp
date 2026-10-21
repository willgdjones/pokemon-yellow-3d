#include "Stage/PYRStage.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Battle/Arena/PYRPoolRules.h"
#include "World/PYRKit.h"
#include "World/PYRPaint.h"

using namespace PYR;

namespace
{
const FLinearColor StageGrass = PYRColor::Hex(0x8fd46a);
const FLinearColor StageGrassDark = PYRColor::Hex(0x5fae4a);
const FLinearColor PlatformTop = PYRColor::Hex(0xeadcb8);   // m4v: sandstone tops, as the anime ring's (was green #b8e08a)
const FLinearColor PlatformRim = PYRColor::Hex(0x8a6a44);
const FLinearColor ForestFloor = PYRColor::Hex(0x5c9a48);
const FLinearColor CaveFloor = PYRColor::Hex(0x8f9cad);   // m4v: Mt. Moon's moonlit blue-grey (was #a88e72)
const FLinearColor CaveRock = PYRColor::Hex(0x5f6d84);    // (was #6e5646)
const FLinearColor Wood = PYRColor::Hex(0xdcb47c);
const FLinearColor Wall = PYRColor::Hex(0xfff4d6);
const FLinearColor Stone = PYRColor::Hex(0xbfb4a0);
const FLinearColor Leaf = PYRColor::Hex(0x3f9a4a);
const FLinearColor Trunk = PYRColor::Hex(0x8a5a3a);
}

APYRBattleStage::APYRBattleStage()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent->SetMobility(EComponentMobility::Movable);
    Tags.Add(TEXT("BattleStage"));
}

FVector APYRBattleStage::CentreFor(EPYRStageKind K)
{
    // Side by side along X, far below and away from the world, the test pads and the m3 test arenas. The indoor stages
    // (the lab, Pewter Gym, Cerulean Gym) stand in a row of their own 20 m apart, each in its closed hall (m4s).
    if (K == EPYRStageKind::Indoor || K == EPYRStageKind::PewterGym || K == EPYRStageKind::CeruleanGym)
        return FVector(-60000.f + (int32(K) - int32(EPYRStageKind::Indoor)) * 20000.f, -90000.f, StageZ);
    return FVector(-60000.f + int32(K) * 9000.f, -60000.f, StageZ);
}

bool APYRBattleStage::IsIndoor(EPYRStageKind K)
{
    return K == EPYRStageKind::Indoor || K == EPYRStageKind::PewterGym || K == EPYRStageKind::CeruleanGym;
}

void APYRBattleStage::BuildHall(float HalfX, float HalfY, float Height, const FLinearColor& WallColour, EPYRKit WallKit, const FLinearColor& CeilingColour,
    float TilingCm)
{
    // The room the fight is in (m4s, visual-091): four walls and a ceiling all round the stage and the battle camera,
    // with a floor under it all, so no camera of the fight ever sees the sky. No collision (the ring keeps the fighters
    // in) and no shadows (the ceiling would put the stage in the dark under the sun).
    HallHalf = FVector(HalfX, HalfY, Height);
    auto Part = [&](const FVector& At, const FVector& Size, const FLinearColor& Col, EPYRKit Kit)
    {
        UStaticMeshComponent* C = Piece(TEXT("Cube"), At, Size, Kit, Col, false, FRotator::ZeroRotator, TilingCm);
        C->SetCastShadow(false);
        C->ComponentTags.Add(TEXT("Hall"));
        return C;
    };
    const float T = 200.f;
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        Part(Centre + FVector(Side * (HalfX + T * .5f), 0, Height * .5f - 100.f), FVector(T, HalfY * 2.f + 2.f * T, Height + 200.f), WallColour, WallKit);
        Part(Centre + FVector(0, Side * (HalfY + T * .5f), Height * .5f - 100.f), FVector(HalfX * 2.f + 2.f * T, T, Height + 200.f), WallColour, WallKit);
    }
    Part(Centre + FVector(0, 0, Height + T * .5f), FVector(HalfX * 2.f + 2.f * T, HalfY * 2.f + 2.f * T, T), CeilingColour, WallKit);
    Part(Centre + FVector(0, 0, -60.f - T * .5f), FVector(HalfX * 2.f + 2.f * T, HalfY * 2.f + 2.f * T, T), WallColour * .7f, WallKit);
}

const TCHAR* APYRBattleStage::Name(EPYRStageKind K)
{
    static const TCHAR* Names[] = {TEXT("grass"), TEXT("forest"), TEXT("cave"), TEXT("indoor"), TEXT("pewter-gym"), TEXT("cerulean-gym")};
    return Names[FMath::Clamp(int32(K), 0, 5)];
}

FVector APYRBattleStage::RedSpot() const
{
    if (Kind == EPYRStageKind::CeruleanGym) return Centre + FVector(-1700.f, 0, 0);
    return Centre + FVector(-(Radius - 120.f), 0, 0);
}

FVector APYRBattleStage::NearPlatform() const { return Centre + FVector(-Radius * .38f, 0, NearTopZ); }
FVector APYRBattleStage::FarPlatform() const { return Centre + FVector(Radius * .38f, 0, FarTopZ); }

UStaticMeshComponent* APYRBattleStage::Piece(const TCHAR* ShapeName, const FVector& At, const FVector& Size, EPYRKit Kit, const FLinearColor& Colour,
    bool bCollide, const FRotator& Rot, float TilingCm)
{
    const EShape S = FCString::Strcmp(ShapeName, TEXT("Sphere")) == 0 ? EShape::Sphere : FCString::Strcmp(ShapeName, TEXT("Cylinder")) == 0 ? EShape::Cylinder
        : FCString::Strcmp(ShapeName, TEXT("Cone")) == 0 ? EShape::Cone : EShape::Cube;
    UStaticMeshComponent* C = Shape(this, nullptr, S, At - GetActorLocation(), Size, Colour, bCollide, Rot);
    PYRKit::Apply(C, Kit, Colour, TilingCm);   // m4v: painted with the anime kit (the big pieces at a larger tiling)
    ++Pieces;
    return C;
}

void APYRBattleStage::BuildPlatforms(const FLinearColor& Top, const FLinearColor& Rim)
{
    // Red's platform: one low oval tier. The foe's: two tiers, farther and higher (each step under a body's step height).
    const FVector Near = Centre + FVector(-Radius * .38f, 0, 0), Far = Centre + FVector(Radius * .38f, 0, 0);
    Piece(TEXT("Cylinder"), Near + FVector(0, 0, 10), FVector(560, 420, 20), EPYRKit::Ledge, Rim, true);
    Piece(TEXT("Cylinder"), Near + FVector(0, 0, 11), FVector(520, 385, 20), EPYRKit::FloorStone, Top, false);
    NearTopZ = 21.f;
    Piece(TEXT("Cylinder"), Far + FVector(0, 0, 12), FVector(620, 470, 24), EPYRKit::Ledge, Rim, true);
    Piece(TEXT("Cylinder"), Far + FVector(0, 0, 36), FVector(470, 350, 24), EPYRKit::Ledge, Rim, true);
    Piece(TEXT("Cylinder"), Far + FVector(0, 0, 37), FVector(440, 325, 24), EPYRKit::FloorStone, Top, false);
    FarTopZ = 49.f;
}

void APYRBattleStage::BuildPool()
{
    // m3's pool arena, kept: a 26 x 18 m basin, water 40 cm under the walkway (PYR.Water), five 4 m platforms
    // (PYR.Platform) with 3 m of water between, a tiled walkway all round and the hall's walls.
    UWorld* World = GetWorld();
    auto Block = [&](FVector At, FVector Size, EPYRKit Kit, FLinearColor Colour, FName Tag)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator, Params);
        A->SetMobility(EComponentMobility::Movable);
        A->GetStaticMeshComponent()->SetStaticMesh(Mesh(EShape::Cube));
        A->GetStaticMeshComponent()->SetMaterial(0, UPYRPaint::Get(this, Colour));
        PYRKit::Apply(A->GetStaticMeshComponent(), Kit, Colour);
        A->SetActorScale3D(Size / 100.f);
        A->Tags.Add(TEXT("BattleStage"));
        if (!Tag.IsNone()) A->Tags.Add(Tag);
        ++Pieces;
        return A;
    };
    // m4v judge round 2: the walkway in the gym's sandy flagstones and the hall in plaster (the FloorTile checker read
    // as a default checkerboard round the pool and past the room).
    const FLinearColor Tile = PYRColor::Hex(0xd8c49c), Deep = PYRColor::Hex(0x3a78b0);
    Block(Centre + FVector(0, 0, -350), FVector(2600, 1800, 100), EPYRKit::Water, Deep, NAME_None);
    Block(Centre + FVector(-1700, 0, -200), FVector(800, 3400, 400), EPYRKit::CaveFloor, Tile, TEXT("Walkway"));
    Block(Centre + FVector(1700, 0, -200), FVector(800, 3400, 400), EPYRKit::CaveFloor, Tile, TEXT("Walkway"));
    Block(Centre + FVector(0, -1300, -200), FVector(2600, 800, 400), EPYRKit::CaveFloor, Tile, TEXT("Walkway"));
    Block(Centre + FVector(0, 1300, -200), FVector(2600, 800, 400), EPYRKit::CaveFloor, Tile, TEXT("Walkway"));
    Block(Centre + FVector(0, 0, -50), FVector(2600, 1800, 20), EPYRKit::Water, PYRColor::Hex(0x6ec6f0), FPYRPoolRules::WaterTag);
    static const FVector Plats[] = {FVector(-750, -500, 0), FVector(-750, 200, 0), FVector(-50, -150, 0), FVector(650, -500, 0), FVector(650, 250, 0)};
    for (const FVector& P : Plats) Block(Centre + P + FVector(0, 0, -40), FVector(400, 400, 60), EPYRKit::FloorWood, PYRColor::Hex(0xc9a24a), FPYRPoolRules::PlatformTag);
    // The hall.
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        Piece(TEXT("Cube"), Centre + FVector(0, Side * 1760.f, 300), FVector(4200, 80, 600), EPYRKit::WallPlaster, Wall, false);
        // The camera's side (-X) is a low kerb, not a wall: a 6 m wall there hid Red and the ring's near edge (judge, m4v).
        if (Side < 0) Piece(TEXT("Cube"), Centre + FVector(Side * 2160.f, 0, 30), FVector(80, 3600, 60), EPYRKit::WallWood, Wall, false);
        else Piece(TEXT("Cube"), Centre + FVector(Side * 2160.f, 0, 300), FVector(80, 3600, 600), EPYRKit::WallPlaster, Wall, false);
    }
    NearTopZ = FarTopZ = -10.f;
    // The gym round the pool hall (the battle camera stands back past its near wall).
    BuildHall(6500.f, 6500.f, 4000.f, PYRColor::Hex(0xece4d2), EPYRKit::WallPlaster, PYRColor::Hex(0xd8d0c0));
}

void APYRBattleStage::Build(EPYRStageKind InKind)
{
    Kind = InKind;
    Centre = CentreFor(Kind);
    SetActorLocation(Centre);
    Tags.Add(FName(*FString::Printf(TEXT("Stage.%s"), Name(Kind))));
    if (Kind == EPYRStageKind::CeruleanGym) { Radius = 1350.f; BuildPool(); return; }
    Radius = Kind == EPYRStageKind::PewterGym ? 1100.f : 900.f;
    const float Floor = Radius + 700.f;
    FLinearColor Ground = StageGrass;
    EPYRKit GroundKit = EPYRKit::GrassShort;
    switch (Kind)
    {
    case EPYRStageKind::Forest: Ground = ForestFloor; break;
    case EPYRStageKind::Cave: Ground = CaveFloor; GroundKit = EPYRKit::CaveFloor; break;
    case EPYRStageKind::Indoor: Ground = PYRColor::Hex(0xc8b8e0); GroundKit = EPYRKit::FloorTile; break;   // Oak's lab's lilac floor
    // m4v: Brock's arena floor is dry cracked sandstone in the anime frame (was grey stone).
    case EPYRStageKind::PewterGym: Ground = PYRColor::Hex(0xd8c49a); GroundKit = EPYRKit::FloorStone; break;   // pale sandstone
    default: break;
    }
    // The stage floor (a wide disc) and a wider painted ground out to the backdrop.
    // The battle camera stands 20-30 m off, so the stage's ground is painted at about four times the field's tiling (a few big
    // cracks and tufts rather than a fine repeat).
    Piece(TEXT("Cylinder"), Centre + FVector(0, 0, -25), FVector(Floor * 2.f, Floor * 2.f, 50), GroundKit, Ground, true, FRotator::ZeroRotator,
        PYRKit::PaintTiling(GroundKit) * 4.f);
    Piece(TEXT("Cylinder"), Centre + FVector(0, 0, -30), FVector(9000, 9000, 50), GroundKit,
        Kind == EPYRStageKind::Grass ? StageGrassDark : Kind == EPYRStageKind::PewterGym ? PYRColor::Hex(0xb08a5a) : Ground * .8f, false,
        FRotator::ZeroRotator, PYRKit::PaintTiling(GroundKit) * 2.6f);
    switch (Kind)
    {
    case EPYRStageKind::Grass:
    case EPYRStageKind::Forest:
    {
        const bool bForest = Kind == EPYRStageKind::Forest;
        BuildPlatforms(bForest ? PYRColor::Hex(0x8cc86a) : PlatformTop, PlatformRim);
        // A ring of round anime trees behind the stage (none between the camera and the stage on the west side).
        const int32 N = bForest ? 34 : 22;
        for (int32 I = 0; I < N; ++I)
        {
            const float A = I * 2.f * PI / N + Rng.FRandRange(-.05f, .05f);
            const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0);
            if (Dir.X < -.55f) continue;   // keep the camera's side open
            const float R = Radius + Rng.FRandRange(500, bForest ? 800 : 1100);
            const float S = Rng.FRandRange(1.f, bForest ? 2.2f : 1.6f);
            const FVector P = Centre + Dir * R;
            Piece(TEXT("Cylinder"), P + FVector(0, 0, 80 * S), FVector(50, 50, 160) * S, EPYRKit::TreeTrunk, Trunk, false);
            Piece(TEXT("Sphere"), P + FVector(0, 0, 250 * S), FVector(260, 260, 240) * S, EPYRKit::TreeCanopy, bForest ? PYRColor::Hex(0x2f7d3c) : Leaf, false);
            Piece(TEXT("Sphere"), P + FVector(30, -20, 360 * S), FVector(160, 160, 150) * S, EPYRKit::TreeCanopy, bForest ? Leaf : PYRColor::Hex(0x55b050), false);
        }
        // Tall grass tufts round the edge and flowers.
        // m4v: each tuft a clump of three flat blades fanned from one root (as in Kanto), not one solid cone. All the
        // blades are one instanced mesh: 180 separate components (each in the custom-depth pass) cost the edge views
        // about 1.3 ms a frame even with the stage out of sight.
        UInstancedStaticMeshComponent* Blades = Instanced(this, EShape::Cone, PYRColor::Hex(0x3fa045), false, false, true);
        PYRKit::Apply(Blades, EPYRKit::GrassTall, PYRColor::Hex(0x3fa045));
        Blades->SetRenderCustomDepth(true);
        Blades->SetCustomDepthStencilValue(1);
        ++Pieces;
        for (int32 I = 0; I < 60; ++I)
        {
            const float A = Rng.FRandRange(0.f, 2.f * PI);
            const FVector P = Centre + FVector(FMath::Cos(A), FMath::Sin(A), 0) * Rng.FRandRange(Radius + 80.f, Radius + 400.f);
            FRandomStream BladeRng(I * 7919 + 17);   // its own stream: the hills after keep their places
            const float Yaw0 = BladeRng.FRandRange(0.f, 120.f);
            for (int32 Blade = 0; Blade < 3; ++Blade)
            {
                const FRotator Rot(-BladeRng.FRandRange(14.f, 26.f), Yaw0 + Blade * 120.f + BladeRng.FRandRange(-20.f, 20.f), 0.f);
                const float BH = Blade == 0 ? 64.f : BladeRng.FRandRange(46.f, 58.f);
                Instance(Blades, P + Rot.RotateVector(FVector(0, 0, BH * .5f)), FVector(8, 17, BH), Rot);
            }
        }
        // Rolling hills far off.
        for (int32 I = 0; I < 7; ++I)
        {
            const float A = FMath::DegreesToRadians(-70.f + I * 23.f);
            const FVector P = Centre + FVector(FMath::Cos(A), FMath::Sin(A), 0) * 5200.f;
            Piece(TEXT("Sphere"), P + FVector(0, 0, -300), FVector(3200, 3200, 1600) * Rng.FRandRange(.8f, 1.3f), EPYRKit::GrassShort, StageGrassDark, false);
        }
        break;
    }
    case EPYRStageKind::Cave:
    {
        BuildPlatforms(PYRColor::Hex(0xc8b090), CaveRock);
        HoverLimit = 520.f;
        for (int32 I = 0; I < 30; ++I)
        {
            const float A = I * 2.f * PI / 30;
            const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0);
            if (Dir.X < -.6f) continue;
            const float H = Rng.FRandRange(500, 1100);
            Piece(TEXT("Cube"), Centre + Dir * (Radius + 650.f) + FVector(0, 0, H * .5f), FVector(500, 500, H), EPYRKit::RockCave, CaveRock, false,
                FRotator(0, FMath::RadiansToDegrees(A), Rng.FRandRange(-6, 6)));
        }
        for (int32 I = 0; I < 14; ++I)
        {
            const float A = Rng.FRandRange(0.f, 2.f * PI);
            const FVector P = Centre + FVector(FMath::Cos(A), FMath::Sin(A), 0) * Rng.FRandRange(Radius + 120.f, Radius + 450.f);
            if (P.X < Centre.X - Radius * .6f) continue;
            const float H = Rng.FRandRange(120, 260);
            Piece(TEXT("Cone"), P + FVector(0, 0, H * .5f), FVector(80, 80, H), EPYRKit::RockCave, PYRColor::Hex(0x7a889c), false);
        }
        break;
    }
    case EPYRStageKind::Indoor:
    {
        BuildPlatforms(PYRColor::Hex(0xece6d6), PYRColor::Hex(0xb88a5a));
        const float Half = Radius + 450.f;
        for (int32 Side = -1; Side <= 1; Side += 2)
            Piece(TEXT("Cube"), Centre + FVector(0, Side * Half, 175), FVector(Half * 2.f, 40, 350), EPYRKit::WallWood, Wall, false);
        Piece(TEXT("Cube"), Centre + FVector(Half, 0, 175), FVector(40, Half * 2.f, 350), EPYRKit::WallWood, Wall, false);
        // Oak's lab round the walls: rows of bookshelves, and the machines and PC along the far wall.
        for (int32 Side = -1; Side <= 1; Side += 2)
            for (int32 I = -4; I <= 4; ++I)
            {
                const FVector At = Centre + FVector(I * 260.f, Side * (Half - 60.f), 100);
                Piece(TEXT("Cube"), At, FVector(240, 70, 200), EPYRKit::Counter, PYRColor::Hex(0x9a6a42), false);
                for (int32 Row = 0; Row < 3; ++Row)
                    Piece(TEXT("Cube"), At + FVector(0, -Side * 36.f, -60 + Row * 55), FVector(220, 4, 34), EPYRKit::Paint,
                        Row == 1 ? PYRColor::Hex(0x4f9fdc) : Row == 2 ? PYRColor::Hex(0xe3342f) : PYRColor::Hex(0x5cb85c), false);
            }
        for (int32 I = -2; I <= 2; ++I)
        {
            Piece(TEXT("Cube"), Centre + FVector(Half - 60.f, I * 420.f, 60), FVector(70, 200, 120), EPYRKit::Metal, PYRColor::Hex(0xb8c0cc), false);
            Piece(TEXT("Cube"), Centre + FVector(Half - 96.f, I * 420.f, 90), FVector(4, 120, 50), EPYRKit::Glass, PYRColor::Hex(0x4f9fdc), false);
        }
        // The lab's room round it all, the camera inside.
        BuildHall(3800.f, 3800.f, 2600.f, Wall, EPYRKit::WallWood, PYRColor::Hex(0xe8dcc4));
        break;
    }
    case EPYRStageKind::PewterGym:
    {
        BuildPlatforms(PYRColor::Hex(0xd8ccb4), PYRColor::Hex(0x8a7a66));
        UStaticMesh* Boulder = Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), TEXT("/Game/Props/gym_boulder/SM_gym_boulder")));
        for (int32 I = 0; I < 16; ++I)
        {
            const float A = I * 2.f * PI / 16 + .1f;
            const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0);
            if (Dir.X < -.6f) continue;
            const FVector P = Centre + Dir * (Radius + Rng.FRandRange(250, 600));
            const float S = Rng.FRandRange(1.2f, 2.4f);
            if (Boulder) { UStaticMeshComponent* C = FitStaticBody(this, nullptr, Boulder, 110.f * S, 0.f, Rng.FRandRange(0, 360)); C->SetWorldLocation(P); ++Pieces; }
            else Piece(TEXT("Sphere"), P + FVector(0, 0, 50 * S), FVector(140, 130, 110) * S, EPYRKit::RockGrey, PYRColor::Hex(0x9a8e80), false);
        }
        // Brock's hall (m4s, visual-091): a curved stone wall of one height round the floor (open on the camera's side),
        // and the gym's walls and ceiling round that, so the high battle camera (higher for Onix) never sees the sky.
        // The old ring of separate blocks of different heights read as roof slabs floating over the wall.
        const int32 Segments = 40;
        const float WallR = Radius + 1100.f, WallH = 800.f, Seg = 2.f * PI * WallR / Segments * 1.08f;
        for (int32 I = 0; I < Segments; ++I)
        {
            const float A = (I + .5f) * 2.f * PI / Segments;
            const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0);
            if (Dir.X < -.6f) continue;
            // m4v: dark cracked rock in the anime frame (was pale cut stone).
            Piece(TEXT("Cube"), Centre + Dir * WallR + FVector(0, 0, WallH * .5f), FVector(400, Seg, WallH), EPYRKit::RockGrey, PYRColor::Hex(0x6a6e78), false,
                FRotator(0, FMath::RadiansToDegrees(A), 0), 900.f);
        }
        BuildHall(5800.f, 5800.f, 3600.f, PYRColor::Hex(0x5a5e68), EPYRKit::RockGrey, PYRColor::Hex(0x8a909c), 1200.f);
        // The skylight: the hall's ceiling glazed and pale, where the shafts come from.
        TArray<UStaticMeshComponent*> Parts;
        GetComponents<UStaticMeshComponent>(Parts);
        for (UStaticMeshComponent* C : Parts)
            if (C->ComponentTags.Contains(TEXT("Hall")) && C->GetComponentLocation().Z > Centre.Z + 3000.f)
                PYRKit::Apply(C, EPYRKit::Glass, PYRColor::Hex(0xc8dcec), 900.f);
        break;
    }
    default: break;
    }
}
