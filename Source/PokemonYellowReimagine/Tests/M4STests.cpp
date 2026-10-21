// m4s: the last five items.
// - EdgeSceneryRoute4, EdgeSceneryRoute22, EdgeSceneryRoute24, EdgeSceneryRoute25 (visual-078, warp-015, visual-072,
//   visual-084): Red on a route's outer tiles looking out; rays across the camera's view, at and under the horizon, hit
//   Kanto's scenery (the neighbouring maps, the landmarks' massifs, the sea), never the old bare meadow or nothing; and
//   walking out is still stopped by the Game Boy's collision.
// - PewterGymRoofClosed (visual-091) and InteriorsRoofClosed: no ray from a room's floor, from the walking camera or from
//   the battle camera (Brock's, higher for Onix) reaches the sky.
// - EdgeSceneryFrameRate (rendered): stat unit's numbers at each edge against a view inside Pallet Town.
//
// The rays are cast against what is drawn (the look instances and the stage pieces), not against collision: the scenery
// and the rooms' looks have no collision, and the sky must not show wherever the camera can look.
#include "Tests/QAKit.h"
#include "World/PYRPaint.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Encounters/PYRBattleCamera.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Library/FollowCameraComponent.h"
#include "Menus/PYRMenus.h"
#include "PokemonYellowReimagineCharacter.h"
#include "ProceduralMeshComponent.h"
#include "RenderCore.h"
#include "RHI.h"
#include "Stage/PYREncounters.h"
#include "Stage/PYRStage.h"

using namespace M4;

namespace
{
// ------------------------------------------------------------------------------------------------ rays
struct FRayHit
{
    float T = TNumericLimits<float>::Max();
    const UPrimitiveComponent* C = nullptr;
    int32 Instance = INDEX_NONE;
    FVector Point = FVector::ZeroVector;
    bool IsHit() const { return C != nullptr; }
};

// A ray (local space of a BasicShapes mesh, any length direction) against the shape inscribed in the mesh's box:
// Sphere an ellipsoid, Cylinder and Cone round about Z (the cone's tip up), anything else (Cube, Plane, a model) its box.
bool RayLocal(const FBox& B, const FString& Shape, const FVector& O, const FVector& D, float& OutT)
{
    const FVector C = B.GetCenter();
    const FVector E = B.GetExtent().ComponentMax(FVector(.01f));
    auto Best = [&OutT](float T) { if (T >= 0.f && T < OutT) OutT = T; };
    const float Start = OutT;
    if (Shape == TEXT("Sphere") || Shape == TEXT("Cylinder") || Shape == TEXT("Cone"))
    {
        const FVector P = (O - C) / E, Dn = D / E;
        if (Shape == TEXT("Sphere"))
        {
            const float A = Dn.SizeSquared(), Bq = 2.f * FVector::DotProduct(P, Dn), Cq = P.SizeSquared() - 1.f, Disc = Bq * Bq - 4.f * A * Cq;
            if (Disc < 0.f || A < 1e-12f) return false;
            const float S = FMath::Sqrt(Disc);
            const float T0 = (-Bq - S) / (2.f * A), T1 = (-Bq + S) / (2.f * A);
            Best(T0 >= 0.f ? T0 : T1);
            return OutT < Start;
        }
        if (Shape == TEXT("Cylinder"))
        {
            const float A = Dn.X * Dn.X + Dn.Y * Dn.Y, Bq = 2.f * (P.X * Dn.X + P.Y * Dn.Y), Cq = P.X * P.X + P.Y * P.Y - 1.f;
            if (A > 1e-12f)
            {
                const float Disc = Bq * Bq - 4.f * A * Cq;
                if (Disc >= 0.f)
                    for (const float Sg : {-1.f, 1.f})
                    {
                        const float T = (-Bq + Sg * FMath::Sqrt(Disc)) / (2.f * A);
                        if (FMath::Abs(P.Z + T * Dn.Z) <= 1.f) Best(T);
                    }
            }
            if (FMath::Abs(Dn.Z) > 1e-9f)
                for (const float Cap : {-1.f, 1.f})
                {
                    const float T = (Cap - P.Z) / Dn.Z;
                    const float X = P.X + T * Dn.X, Y = P.Y + T * Dn.Y;
                    if (X * X + Y * Y <= 1.f) Best(T);
                }
            return OutT < Start;
        }
        // Cone: radius (1 - z) / 2 for z from -1 (the base, radius 1) to 1 (the tip).
        const float K0 = (1.f - P.Z) * .5f;
        const float A = Dn.X * Dn.X + Dn.Y * Dn.Y - Dn.Z * Dn.Z * .25f;
        const float Bq = 2.f * (P.X * Dn.X + P.Y * Dn.Y) + K0 * Dn.Z;
        const float Cq = P.X * P.X + P.Y * P.Y - K0 * K0;
        auto OnCone = [&](float T) { const float Z = P.Z + T * Dn.Z; return Z >= -1.f && Z <= 1.f; };
        if (FMath::Abs(A) > 1e-12f)
        {
            const float Disc = Bq * Bq - 4.f * A * Cq;
            if (Disc >= 0.f)
                for (const float Sg : {-1.f, 1.f})
                {
                    const float T = (-Bq + Sg * FMath::Sqrt(Disc)) / (2.f * A);
                    if (OnCone(T)) Best(T);
                }
        }
        else if (FMath::Abs(Bq) > 1e-12f) { const float T = -Cq / Bq; if (OnCone(T)) Best(T); }
        if (FMath::Abs(Dn.Z) > 1e-9f)
        {
            const float T = (-1.f - P.Z) / Dn.Z;
            const float X = P.X + T * Dn.X, Y = P.Y + T * Dn.Y;
            if (X * X + Y * Y <= 1.f) Best(T);
        }
        return OutT < Start;
    }
    // The box (slabs).
    float T0 = 0.f, T1 = OutT;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const float Lo = B.Min[Axis], Hi = B.Max[Axis];
        if (FMath::Abs(D[Axis]) < 1e-9f)
        {
            if (O[Axis] < Lo - .01f || O[Axis] > Hi + .01f) return false;
            continue;
        }
        float A = (Lo - O[Axis]) / D[Axis], Bt = (Hi - O[Axis]) / D[Axis];
        if (A > Bt) Swap(A, Bt);
        T0 = FMath::Max(T0, A);
        T1 = FMath::Min(T1, Bt);
        if (T0 > T1) return false;
    }
    Best(T0);
    return OutT < Start;
}

bool BoxHit(const FBox& B, const FVector& O, const FVector& D, float MaxT)
{
    float T = MaxT;
    return RayLocal(B, TEXT("Cube"), O, D, T);
}

bool Drawn(const UPrimitiveComponent* C)
{
    const AActor* A = C ? C->GetOwner() : nullptr;
    return A && !A->IsHidden() && C->IsVisible() && !C->bHiddenInGame && !A->ActorHasTag(TEXT("SkyDome"));
}

// The first drawn thing a ray meets within MaxT (cm): the look instances, stage pieces, models and roofs of the actors
// Pick lets in.
FRayHit CastRay(UWorld* World, const FVector& O, const FVector& Dir, float MaxT, TFunctionRef<bool(const AActor*)> Pick)
{
    FRayHit Hit;
    Hit.T = MaxT;
    const FVector D = Dir.GetSafeNormal();
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        const AActor* A = *It;
        if (!A || A->IsHidden() || !Pick(A)) continue;
        TInlineComponentArray<UPrimitiveComponent*> Comps(A);
        for (const UPrimitiveComponent* C : Comps)
        {
            if (!Drawn(C)) continue;
            if (const UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(C))
            {
                // The instances as placed (PYR::PlacedInstances: a cooked game keeps no CPU copy to read back), their
                // box first.
                const UStaticMesh* Mesh = I->GetStaticMesh();
                const TArray<FTransform>* Placed = PYR::PlacedInstances(I);
                if (!Mesh || !Placed || !Placed->Num()) continue;
                const FBox B = Mesh->GetBoundingBox();
                const FString Shape = Mesh->GetName();
                FBox All(ForceInit);
                for (const FTransform& T : *Placed) All += B.TransformBy(T);
                if (!BoxHit(All.ExpandBy(1.f), O, D, Hit.T)) continue;
                for (int32 K = 0; K < Placed->Num(); ++K)
                {
                    const FTransform& T = (*Placed)[K];
                    float Tl = Hit.T;
                    // t is the same in the instance's space: P(t) = O + t D maps to Lo + t Ld.
                    if (RayLocal(B, Shape, T.InverseTransformPosition(O), T.InverseTransformVector(D), Tl) && Tl < Hit.T)
                    {
                        Hit.T = Tl;
                        Hit.C = C;
                        Hit.Instance = K;
                    }
                }
            }
            else if (!BoxHit(C->Bounds.GetBox(), O, D, Hit.T)) continue;
            else if (const UStaticMeshComponent* S = Cast<UStaticMeshComponent>(C))
            {
                const UStaticMesh* Mesh = S->GetStaticMesh();
                if (!Mesh) continue;
                const FTransform T = S->GetComponentTransform();
                float Tl = Hit.T;
                if (RayLocal(Mesh->GetBoundingBox(), Mesh->GetName(), T.InverseTransformPosition(O), T.InverseTransformVector(D), Tl) && Tl < Hit.T)
                {
                    Hit.T = Tl;
                    Hit.C = C;
                    Hit.Instance = INDEX_NONE;
                }
            }
            else if (Cast<UProceduralMeshComponent>(C))
            {
                // The roofs' prisms: their box.
                float Tl = Hit.T;
                if (RayLocal(C->Bounds.GetBox(), TEXT("Cube"), O, D, Tl) && Tl < Hit.T) { Hit.T = Tl; Hit.C = C; Hit.Instance = INDEX_NONE; }
            }
        }
    }
    if (Hit.C) Hit.Point = O + D * Hit.T;
    return Hit;
}

// ------------------------------------------------------------------------------------------------ what a hit is
struct FSeen
{
    FName Source;   // a map id, a landmark (MT_MOON...), SEA, KANTO_GROUND, "border", MEADOW
    FName Kind;     // ground, sea or feature
};

FSeen Classify(FGameTestContext& Test, const FRayHit& H)
{
    AYellowWorld* W = World(Test);
    const UPrimitiveComponent* C = H.C;
    if (C->ComponentHasTag(TEXT("KantoFar")) && C->ComponentTags.Num() >= 3) return {C->ComponentTags[1], C->ComponentTags[2]};
    const UStaticMesh* Mesh = nullptr;
    FTransform T = C->GetComponentTransform();
    if (const UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(C))
    {
        Mesh = I->GetStaticMesh();
        if (const TArray<FTransform>* Placed = PYR::PlacedInstances(I); Placed && Placed->IsValidIndex(H.Instance)) T = (*Placed)[H.Instance];
    }
    const bool bPlane = Mesh && Mesh->GetName() == TEXT("Plane");
    const AYellowMapActor* A = Cast<AYellowMapActor>(C->GetOwner());
    if (A && A == W->GetSceneryActor())
    {
        if (bPlane && T.GetScale3D().X > 1000.f) return {TEXT("MEADOW"), TEXT("ground")};
        // The tile under the hit, or (a piece overhanging a map's edge tile) the nearest scenery tile round it.
        const FIntPoint At = W->ToKanto(H.Point);
        FName Src = W->ScenerySourceAt(At);
        for (int32 R = 1; R <= 3 && Src.IsNone(); ++R)
            for (int32 Dy = -R; Dy <= R && Src.IsNone(); ++Dy)
                for (int32 Dx = -R; Dx <= R && Src.IsNone(); ++Dx) Src = W->ScenerySourceAt(At + FIntPoint(Dx, Dy));
        // The plateau's grass top and anything standing is a feature; ground at the foot is ground.
        const FName Kind = bPlane && H.Point.Z - W->GetOffset().Z < 40.f ? FName(TEXT("ground")) : FName(TEXT("feature"));
        return {Src.IsNone() ? FName(TEXT("SCENERY")) : Src, Kind};
    }
    if (A) return {A->MapId, bPlane ? FName(TEXT("ground")) : FName(TEXT("feature"))};
    return {C->GetOwner() ? C->GetOwner()->GetFName() : NAME_None, TEXT("feature")};
}

// A source that is Kanto: a map (built or out of the range), a landmark's massif, the sea or the far ground. Not the old
// bare meadow, nor a scenery tile with no source.
bool IsKantoSource(FGameTestContext& Test, FName Source)
{
    const FYellowDataSet* D = Data(Test);
    if (Source == TEXT("SEA") || Source == TEXT("KANTO_GROUND") || Source == TEXT("border") || D->Map(Source) || D->Landmarks.Contains(Source)) return true;
    for (const FYMap& M : D->Kanto) if (M.Id == Source) return true;
    return false;
}

bool IsWorldActor(const AActor* A)
{
    return A->IsA<AYellowMapActor>() && !A->ActorHasTag(TEXT("SkyDome"));
}

// ------------------------------------------------------------------------------------------------ the edges
struct FEdgeView
{
    FName Map;
    FIntPoint Tile;      // local
    float Yaw;           // 0 east, 90 south
    bool bSea;           // the sea lies ahead: no land rises over the horizon
    TArray<FString> Required; // each must be met by some ray (any of the names separated by "|")
    TArray<FName> Allowed;    // every source met must be one of these
};

struct FEdgeState
{
    int32 View = 0, Step = 0;
    double T0 = 0;
    FString Note;
    int32 MaxOut = 0;
};

// One view: the camera's rays, then a push outward.
EGameTest RunEdge(FGameTestContext& Test, FEdgeState& St, const TArray<FEdgeView>& Views, const TCHAR* Name)
{
    M4_READY();
    if (St.View >= Views.Num()) M4_PASS(TEXT("%s"), *St.Note);
    const FEdgeView& V = Views[St.View];
    AYellowWorld* W = World(Test);
    UPYRField* F = Field(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    APlayerController* PC = Test.World->GetFirstPlayerController();
    const FYMap* M = Data(Test)->Map(V.Map);
    if (!M || !PC || !W->GetFarActor()) M4_FAIL(TEXT("%s: no map %s, controller or far Kanto"), Name, *V.Map.ToString());
    const FVector Out = FRotator(0, V.Yaw, 0).Vector();
    if (St.Step == 0)
    {
        AfterPokedex(Test);
        Quiet(true, true);
        if (YTile::IsSolid(M->Tile(V.Tile.X, V.Tile.Y))) M4_FAIL(TEXT("%s %d,%d is solid"), *V.Map.ToString(), V.Tile.X, V.Tile.Y);
        Place(Test, V.Map, V.Tile.X, V.Tile.Y, YDir::FromYaw(V.Yaw));
        PC->SetControlRotation(FRotator(-10.f, V.Yaw, 0));
        St.T0 = Test.Seconds;
        St.Step = 1;
        return EGameTest::Running;
    }
    if (St.Step == 1)
    {
        if (Test.Seconds - St.T0 < 1.2) return EGameTest::Running;
        // The camera's own view: rays across its width, at and under the horizon.
        const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
        const FRotator Rot = PC->PlayerCameraManager->GetCameraRotation();
        const float HalfFov = PC->PlayerCameraManager->GetFOVAngle() * .5f * .9f;
        TMap<FName, int32> Sources;
        int32 Rays = 0, Flat = 0, Above = 0, AboveHit = 0;
        TArray<FString> Bad;
        for (int32 Col = 0; Col < 7; ++Col)
        {
            const float Yaw = Rot.Yaw + FMath::Lerp(-HalfFov, HalfFov, Col / 6.f);
            for (const float Pitch : {.4f, -.3f, -1.f, -2.f, -4.f})
            {
                const FVector Dir = FRotator(Pitch, Yaw, 0).Vector();
                const FRayHit H = CastRay(Test.World, Eye, Dir, 240000.f, IsWorldActor);
                if (Pitch > 0.f)
                {
                    // Over the horizon: land rises there (mountains, hills, trees); over the sea it is sky.
                    ++Above;
                    if (H.IsHit()) { ++AboveHit; Sources.FindOrAdd(Classify(Test, H).Source)++; }
                    continue;
                }
                ++Rays;
                if (!H.IsHit()) { if (Bad.Num() < 4) Bad.Add(FString::Printf(TEXT("yaw %.0f pitch %.1f: nothing"), Yaw, Pitch)); continue; }
                const FSeen S = Classify(Test, H);
                Sources.FindOrAdd(S.Source)++;
                if (S.Source == TEXT("MEADOW")) { if (Bad.Num() < 4) Bad.Add(FString::Printf(TEXT("yaw %.0f pitch %.1f: the bare meadow at %.0f m"), Yaw, Pitch, H.T / 100.f)); continue; }
                if (!V.Allowed.Contains(S.Source) && !IsKantoSource(Test, S.Source) && Bad.Num() < 4)
                    Bad.Add(FString::Printf(TEXT("yaw %.0f pitch %.1f: %s (%s) at %.0f m, not a neighbour of %s"), Yaw, Pitch, *S.Source.ToString(), *S.Kind.ToString(), H.T / 100.f, *V.Map.ToString()));
                Flat += S.Source == TEXT("KANTO_GROUND") ? 1 : 0;
            }
        }
        TArray<FString> Met;
        for (const auto& P : Sources) Met.Add(FString::Printf(TEXT("%s %d"), *P.Key.ToString(), P.Value));
        const FString MetText = FString::Join(Met, TEXT(", "));
        if (Bad.Num()) M4_FAIL(TEXT("%s view %d (%s %d,%d yaw %.0f): %s; met %s"), Name, St.View, *V.Map.ToString(), V.Tile.X, V.Tile.Y, V.Yaw, *FString::Join(Bad, TEXT("; ")), *MetText);
        for (const FString& Req : V.Required)
        {
            TArray<FString> Any;
            Req.ParseIntoArray(Any, TEXT("|"));
            bool bMet = false;
            for (const FString& A : Any) bMet |= Sources.Contains(FName(*A));
            if (!bMet) M4_FAIL(TEXT("%s view %d (%s yaw %.0f): no ray meets %s; met %s"), Name, St.View, *V.Map.ToString(), V.Yaw, *Req, *MetText);
        }
        if (Flat * 10 > Rays * 3) M4_FAIL(TEXT("%s view %d: %d of %d rays end on flat far ground (a plain); met %s"), Name, St.View, Flat, Rays, *MetText);
        if (!V.bSea && AboveHit * 10 < Above * 6) M4_FAIL(TEXT("%s view %d: only %d of %d rays just over the horizon meet land rising; met %s"), Name, St.View, AboveHit, Above, *MetText);
        St.Note += FString::Printf(TEXT("%s %d,%d yaw %.0f: %d rays under the horizon all on scenery (%d on flat far ground), %d of %d over it on land; met %s; "),
            *V.Map.ToString(), V.Tile.X, V.Tile.Y, V.Yaw, Rays, Flat, AboveHit, Above, *MetText);
        St.T0 = Test.Seconds;
        St.Step = 2;
        return EGameTest::Running;
    }
    // Walking out: the Game Boy's collision keeps Red on the map.
    R->TestInput = Out;
    const FIntPoint K = KantoTile(Test);
    if (!M->WorldRect().Contains(K)) M4_FAIL(TEXT("%s view %d: walking out took Red to Kanto %d,%d, off %s"), Name, St.View, K.X, K.Y, *V.Map.ToString());
    if (F->CurrentMap != V.Map) M4_FAIL(TEXT("%s view %d: walking out took Red into %s"), Name, St.View, *F->CurrentMap.ToString());
    if (Test.Seconds - St.T0 < 2.5) return EGameTest::Running;
    StopWalking(Test);
    St.Note += FString::Printf(TEXT("walking out for 2.5 s Red stayed on %s (at %d,%d); "), *V.Map.ToString(), K.X - M->Origin.X, K.Y - M->Origin.Y);
    ++St.View;
    St.Step = 0;
    return EGameTest::Running;
}

TArray<FName> Names(std::initializer_list<const TCHAR*> L)
{
    TArray<FName> Out;
    for (const TCHAR* S : L) Out.Add(FName(S));
    return Out;
}

TArray<FString> Strs(std::initializer_list<const TCHAR*> L)
{
    TArray<FString> Out;
    for (const TCHAR* S : L) Out.Add(S);
    return Out;
}
}

// Route 22 (warp-015): north and north-west, Route 23's land, the League gate and the Plateau's ridges (Victory Road's
// and the League's massifs); never the old bare meadow.
GAME_TEST(EdgeSceneryRoute22, 240)
{
    static FEdgeState St;
    const TArray<FName> Allowed = Names({TEXT("ROUTE_22"), TEXT("ROUTE_22_GATE"), TEXT("border"), TEXT("ROUTE_23"), TEXT("INDIGO_PLATEAU"), TEXT("POKEMON_LEAGUE"),
        TEXT("VICTORY_ROAD"), TEXT("VIRIDIAN_CITY"), TEXT("ROUTE_2"), TEXT("KANTO_GROUND"), TEXT("PEWTER_CITY")});
    const TArray<FEdgeView> Views = {
        {TEXT("ROUTE_22"), FIntPoint(32, 2), -90.f, false, Strs({TEXT("VICTORY_ROAD|POKEMON_LEAGUE")}), Allowed},
        {TEXT("ROUTE_22"), FIntPoint(32, 2), -135.f, false, Strs({TEXT("ROUTE_23"), TEXT("VICTORY_ROAD|POKEMON_LEAGUE|INDIGO_PLATEAU")}), Allowed},
    };
    return RunEdge(Test, St, Views, TEXT("Route 22"));
}

// Route 4 (visual-078): west, the Mt. Moon massif past the rock; north, Kanto's mountains and tree lines to the horizon.
GAME_TEST(EdgeSceneryRoute4, 240)
{
    static FEdgeState St;
    const TArray<FName> Allowed = Names({TEXT("ROUTE_4"), TEXT("border"), TEXT("MT_MOON"), TEXT("ROUTE_3"), TEXT("PEWTER_CITY"), TEXT("ROUTE_24"), TEXT("ROUTE_25"),
        TEXT("CERULEAN_CITY"), TEXT("KANTO_GROUND"), TEXT("ROUTE_2"), TEXT("INDIGO_PLATEAU"), TEXT("ROUTE_23"), TEXT("VICTORY_ROAD"), TEXT("POKEMON_LEAGUE")});
    const TArray<FEdgeView> Views = {
        {TEXT("ROUTE_4"), FIntPoint(4, 8), 180.f, false, Strs({TEXT("MT_MOON")}), Allowed},
        {TEXT("ROUTE_4"), FIntPoint(46, 4), -90.f, false, Strs({TEXT("MT_MOON|ROUTE_4|ROUTE_24")}), Allowed},
    };
    return RunEdge(Test, St, Views, TEXT("Route 4"));
}

// Route 24 (visual-072): west of Nugget Bridge, the land beyond the rock rising to hills and Mt. Moon's peaks.
GAME_TEST(EdgeSceneryRoute24, 240)
{
    static FEdgeState St;
    const TArray<FName> Allowed = Names({TEXT("ROUTE_24"), TEXT("border"), TEXT("ROUTE_4"), TEXT("ROUTE_25"), TEXT("CERULEAN_CITY"), TEXT("MT_MOON"), TEXT("KANTO_GROUND"),
        TEXT("ROUTE_3"), TEXT("PEWTER_CITY"), TEXT("SEA")});
    const TArray<FEdgeView> Views = {
        {TEXT("ROUTE_24"), FIntPoint(4, 18), 180.f, false, Strs({TEXT("ROUTE_24|ROUTE_4|ROUTE_25|CERULEAN_CITY|MT_MOON")}), Allowed},
        {TEXT("ROUTE_24"), FIntPoint(5, 5), -135.f, false, Strs({TEXT("ROUTE_24|ROUTE_4|ROUTE_25|CERULEAN_CITY|MT_MOON")}), Allowed},
    };
    return RunEdge(Test, St, Views, TEXT("Route 24"));
}

// Route 25 (visual-084): north and east of Bill's cape, the open sea to the horizon.
GAME_TEST(EdgeSceneryRoute25, 240)
{
    static FEdgeState St;
    const TArray<FName> Allowed = Names({TEXT("ROUTE_25"), TEXT("border"), TEXT("SEA"), TEXT("ROUTE_24"), TEXT("KANTO_GROUND"), TEXT("CERULEAN_CITY"), TEXT("ROUTE_9"),
        TEXT("ROUTE_4"), TEXT("MT_MOON")});
    const TArray<FEdgeView> Views = {
        {TEXT("ROUTE_25"), FIntPoint(40, 2), -90.f, true, Strs({TEXT("SEA")}), Allowed},
        {TEXT("ROUTE_25"), FIntPoint(54, 6), 0.f, true, Strs({TEXT("SEA")}), Allowed},
    };
    return RunEdge(Test, St, Views, TEXT("Route 25"));
}

// ------------------------------------------------------------------------------------------------ closed rooms
namespace
{
// Every ray from a room's floor (straight up from each tile) and from the walking camera (across its view, turned
// four ways and tilted as high as it goes) meets the room's own geometry within 30 m: no sky.
bool RoomClosed(FGameTestContext& Test, FName Map, FString& Why, int32& Rays)
{
    AYellowWorld* W = World(Test);
    const FYMap* M = Data(Test)->Map(Map);
    if (!M) { Why = TEXT("no map"); return false; }
    auto Room = [Map](const AActor* A) { const AYellowMapActor* Ma = Cast<AYellowMapActor>(A); return Ma && Ma->MapId == Map; };
    for (int32 Y = 0; Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X)
        {
            const FVector From = W->TileCenter(Map, X, Y) + FVector(0, 0, 60.f);
            ++Rays;
            if (!CastRay(Test.World, From, FVector::UpVector, 3000.f, Room).IsHit())
            {
                Why = FString::Printf(TEXT("%s: straight up from tile %d,%d the sky shows"), *Map.ToString(), X, Y);
                return false;
            }
        }
    return true;
}

bool ViewClosed(FGameTestContext& Test, const FVector& Eye, const FRotator& Rot, float Fov, TFunctionRef<bool(const AActor*)> Pick, float MaxT, FString& Why, int32& Rays)
{
    const float HalfH = Fov * .5f, HalfV = FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(HalfH)) * 9.f / 16.f));
    for (int32 Row = 0; Row < 7; ++Row)
        for (int32 Col = 0; Col < 9; ++Col)
        {
            // Through the picture's grid (the camera's own rays, corners included).
            const float U = FMath::Lerp(-1.f, 1.f, Col / 8.f), Vv = FMath::Lerp(1.f, -1.f, Row / 6.f);
            const FVector Local(1.f, U * FMath::Tan(FMath::DegreesToRadians(HalfH)), Vv * FMath::Tan(FMath::DegreesToRadians(HalfV)));
            const FVector Dir = Rot.RotateVector(Local).GetSafeNormal();
            ++Rays;
            const FRayHit H = CastRay(Test.World, Eye, Dir, MaxT, Pick);
            if (!H.IsHit())
            {
                // What it would meet among everything drawn (for the report).
                const FRayHit Any = CastRay(Test.World, Eye, Dir, 1000000.f, [](const AActor*) { return true; });
                Why = FString::Printf(TEXT("from %s looking %s, the ray %s through the picture at (%.2f, %.2f) meets nothing within %.0f m: the sky shows (beyond: %s at %.0f m)"),
                    *Eye.ToCompactString(), *Rot.ToCompactString(), *Dir.ToCompactString(), U, Vv, MaxT / 100.f,
                    Any.C ? *FString::Printf(TEXT("%s.%s #%d"), *GetNameSafe(Any.C->GetOwner()), *Any.C->GetName(), Any.Instance) : TEXT("nothing"), Any.T / 100.f);
                return false;
            }
        }
    return true;
}

struct FRoomState
{
    int32 Step = 0, Room = 0, Turn = 0;
    double T0 = 0;
    int32 Rays = 0;
    FString Note;
};

// The walking camera in a room: four turns, each tilted as high as the player can (20 degrees up), the arm as long as
// the room lets it (the follow camera's only length, 650 cm: the game has no zoom). One turn per call; true when done.
bool WalkingCamera(FGameTestContext& Test, FRoomState& St, FName Map, FString& Why, bool& bFailed)
{
    APlayerController* PC = Test.World->GetFirstPlayerController();
    auto Room = [Map](const AActor* A) { const AYellowMapActor* Ma = Cast<AYellowMapActor>(A); return Ma && Ma->MapId == Map; };
    if (Test.Seconds - St.T0 < .8) return false;
    if (St.Turn > 0)
    {
        if (!ViewClosed(Test, PC->PlayerCameraManager->GetCameraLocation(), PC->PlayerCameraManager->GetCameraRotation(), PC->PlayerCameraManager->GetFOVAngle(), Room,
                3000.f, Why, St.Rays))
        {
            Why = FString::Printf(TEXT("%s, walking camera turn %d: %s"), *Map.ToString(), St.Turn, *Why);
            bFailed = true;
            return true;
        }
        // Looking straight up from the camera too.
        ++St.Rays;
        if (!CastRay(Test.World, PC->PlayerCameraManager->GetCameraLocation(), FVector::UpVector, 3000.f, Room).IsHit())
        {
            Why = FString::Printf(TEXT("%s: straight up from the walking camera at %s the sky shows"), *Map.ToString(), *PC->PlayerCameraManager->GetCameraLocation().ToCompactString());
            bFailed = true;
            return true;
        }
    }
    if (St.Turn >= 4) return true;
    PC->SetControlRotation(FRotator(20.f, St.Turn * 90.f + 45.f, 0));
    ++St.Turn;
    St.T0 = Test.Seconds;
    return false;
}

// A stage's battle camera, framed as StartFight frames it (and at its pan limits): every ray across its picture meets
// the stage's hall within 200 m.
bool StageClosed(FGameTestContext& Test, EPYRStageKind Kind, const FVector& Eye, const FRotator& Rot, float Fov, FString& Why, int32& Rays)
{
    auto Stage = [](const AActor* A) { return A->ActorHasTag(TEXT("BattleStage")); };
    for (const FVector Pan : {FVector::ZeroVector, FVector(450, 0, 0), FVector(-450, 0, 0), FVector(0, 450, 0), FVector(0, -450, 0)})
        if (!ViewClosed(Test, Eye + Pan, Rot, Fov, Stage, 20000.f, Why, Rays))
        {
            Why = FString::Printf(TEXT("the %s stage's battle camera (panned %s): %s"), APYRBattleStage::Name(Kind), *Pan.ToCompactString(), *Why);
            return false;
        }
    return true;
}

bool FramedStageClosed(FGameTestContext& Test, EPYRStageKind Kind, FString& Why, int32& Rays, FString& Note)
{
    UPYREncounters* E = UPYREncounters::Get(Test.World);
    APYRBattleStage* S = E ? E->GetStage(Kind) : nullptr;
    if (!S) { Why = FString::Printf(TEXT("no %s stage"), APYRBattleStage::Name(Kind)); return false; }
    FActorSpawnParameters P;
    P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APYRBattleCamera* Cam = Test.World->SpawnActor<APYRBattleCamera>(S->Centre, FRotator::ZeroRotator, P);
    Cam->Frame(S->Centre, S->Radius, 0.f, S->IsGym());
    Cam->SnapToFinal();
    const FVector Eye = Cam->GetActorLocation();
    const FRotator Rot = Cam->GetActorRotation();
    const bool bOk = StageClosed(Test, Kind, Eye, Rot, APYRBattleCamera::Fov, Why, Rays);
    Cam->Release();
    Cam->Destroy();
    Note += FString::Printf(TEXT("%s stage: camera %.0f m back, %.0f m up, inside its hall (%.0f x %.0f x %.0f m); "), APYRBattleStage::Name(Kind),
        FVector::Dist2D(Eye, S->Centre) / 100.f, (Eye.Z - S->Centre.Z) / 100.f, S->HallHalf.X / 50.f, S->HallHalf.Y / 50.f, S->HallHalf.Z / 100.f);
    return bOk;
}
}

// Pewter Gym (visual-091): Red walks in through its door; no ray from its floor or from the walking camera meets the
// sky, nor from Brock's battle camera (framed for Onix, at its pan limits), which now stands inside the gym's hall.
GAME_TEST(PewterGymRoofClosed, 180)
{
    static FRoomState St;
    static FIntPoint Door;
    M4_READY();
    UPYRField* F = Field(Test);
    UPYREncounters* E = UPYREncounters::Get(Test.World);
    const FYMap* Pewter = Data(Test)->Map(TEXT("PEWTER_CITY"));
    if (!Pewter || !E) M4_FAIL(TEXT("no Pewter City or encounters"));
    if (St.Step == 0)
    {
        AfterPokedex(Test);
        Quiet(true, true);
        bool bFound = false;
        for (const FYWarp& Wp : Pewter->Warps)
            if (Wp.ToMap == TEXT("PEWTER_GYM")) { Door = FIntPoint(Wp.X, Wp.Y); bFound = true; }
        if (!bFound) M4_FAIL(TEXT("Pewter City has no door into the gym"));
        Place(Test, TEXT("PEWTER_CITY"), Door.X, Door.Y + 1, EYDir::Up);
        St.T0 = Test.Seconds;
        St.Step = 1;
        return EGameTest::Running;
    }
    if (St.Step == 1)
    {
        if (Test.Seconds - St.T0 < .3) return EGameTest::Running;
        // Through the door, as a player walks in.
        if (F->CurrentMap != TEXT("PEWTER_GYM"))
        {
            Red(Test)->TestInput = FVector(0, -1, 0);
            if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("walking up into the gym's door at %d,%d never took Red in (on %s)"), Door.X, Door.Y, *F->CurrentMap.ToString());
            return EGameTest::Running;
        }
        StopWalking(Test);
        St.Note = FString::Printf(TEXT("in through the door at %d,%d onto PEWTER_GYM %d,%d; "), Door.X, Door.Y, F->CurrentTile.X, F->CurrentTile.Y);
        St.T0 = Test.Seconds;
        St.Step = 2;
        return EGameTest::Running;
    }
    FString Why;
    if (St.Step == 2)
    {
        if (Test.Seconds - St.T0 < 1.0) return EGameTest::Running;
        if (!World(Test)->IsRoomClosed(TEXT("PEWTER_GYM"))) M4_FAIL(TEXT("Pewter Gym has no lid"));
        if (!RoomClosed(Test, TEXT("PEWTER_GYM"), Why, St.Rays)) M4_FAIL(TEXT("%s"), *Why);
        St.Turn = 0;
        St.T0 = Test.Seconds;
        St.Step = 3;
        return EGameTest::Running;
    }
    if (St.Step == 3)
    {
        bool bFailed = false;
        if (!WalkingCamera(Test, St, TEXT("PEWTER_GYM"), Why, bFailed)) return EGameTest::Running;
        if (bFailed) M4_FAIL(TEXT("%s"), *Why);
        St.Note += FString::Printf(TEXT("the floor and the walking camera (4 turns, 20 degrees up) closed; "));
        // Brock's Onix, as his trainer data has it, on the gym's stage.
        FPYRFight Fight;
        if (!UPYRScripts::Get(Test.World)->TrainerFight(F->FindPerson(TEXT("PEWTER_GYM"), TEXT("PEWTERGYM_BROCK")), Fight)) M4_FAIL(TEXT("Brock has no fight"));
        Fight.Foes.RemoveAll([](const FPYRMonSpec& Mn) { return Mn.Species != FName(TEXT("onix")); });
        Fight.bNoBlackout = true;
        Party(Test, {{TEXT("pikachu"), 30}});
        if (!E->Begin(Fight)) M4_FAIL(TEXT("Brock's fight did not start"));
        St.T0 = Test.Seconds;
        St.Step = 4;
        return EGameTest::Running;
    }
    APYRBattleCamera* Cam = E->GetCamera();
    if (E->GetPhase() != EPYREncounterPhase::Fight || !Cam || !Cam->IsSettled())
    {
        if (Test.Seconds - St.T0 > 20) M4_FAIL(TEXT("Brock's fight never settled (phase %d)"), int32(E->GetPhase()));
        return EGameTest::Running;
    }
    if (Test.Seconds - St.T0 < 2.0) return EGameTest::Running;
    APYRBattleStage* S = E->GetCurrentStage();
    if (!S || S->Kind != EPYRStageKind::PewterGym) M4_FAIL(TEXT("Brock's fight is on the %s stage"), S ? APYRBattleStage::Name(S->Kind) : TEXT("no"));
    const FVector Eye = Cam->GetActorLocation();
    const FVector L = Eye - S->Centre;
    if (FMath::Abs(L.X) > S->HallHalf.X || FMath::Abs(L.Y) > S->HallHalf.Y || L.Z > S->HallHalf.Z) M4_FAIL(TEXT("the battle camera at %s stands outside the gym's hall (%s)"), *L.ToCompactString(), *S->HallHalf.ToCompactString());
    if (!StageClosed(Test, EPYRStageKind::PewterGym, Eye, Cam->GetActorRotation(), Cam->GetCameraComponent()->FieldOfView, Why, St.Rays)) M4_FAIL(TEXT("%s"), *Why);
    M4_PASS(TEXT("%sBrock's Onix fight: the camera (%.0f m back, %.0f m up, %.0f degrees down) stays in the hall and sees no sky at its pan limits; %d rays, all on the gym"),
        *St.Note, FVector::Dist2D(Eye, S->Centre) / 100.f, L.Z / 100.f, Cam->GetPitchDown(), St.Rays);
}

// The other rooms on the way (Cerulean Gym, the Pokemon Centres, the Marts, the Museum, Oak's lab) and the indoor
// battle stages (the lab's, Cerulean Gym's pool): no ray from a floor, the walking camera or the battle camera meets the sky.
GAME_TEST(InteriorsRoofClosed, 300)
{
    static FRoomState St;
    static const TCHAR* Rooms[] = {TEXT("CERULEAN_GYM"), TEXT("VIRIDIAN_POKECENTER"), TEXT("PEWTER_POKECENTER"), TEXT("CERULEAN_POKECENTER"), TEXT("MT_MOON_POKECENTER"),
        TEXT("VIRIDIAN_MART"), TEXT("PEWTER_MART"), TEXT("CERULEAN_MART"), TEXT("MUSEUM_1F"), TEXT("MUSEUM_2F"), TEXT("OAKS_LAB")};
    M4_READY();
    FString Why;
    if (St.Room < int32(UE_ARRAY_COUNT(Rooms)))
    {
        const FName Map(Rooms[St.Room]);
        const FYMap* M = Data(Test)->Map(Map);
        if (!M) M4_FAIL(TEXT("no %s"), Rooms[St.Room]);
        if (St.Step == 0)
        {
            AfterPokedex(Test);
            Quiet(true, true);
            // On the tile in front of the exit, as Red stands after coming in.
            FIntPoint At(-1, -1);
            for (const FYWarp& Wp : M->Warps)
                for (const FIntPoint D : {FIntPoint(0, -1), FIntPoint(0, -2), FIntPoint(1, -1), FIntPoint(-1, -1)})
                    if (At.X < 0 && M->Contains(Wp.X + D.X, Wp.Y + D.Y) && !YTile::IsSolid(M->Tile(Wp.X + D.X, Wp.Y + D.Y)) && M->WarpAt(Wp.X + D.X, Wp.Y + D.Y) == INDEX_NONE)
                        At = FIntPoint(Wp.X + D.X, Wp.Y + D.Y);
            if (At.X < 0) M4_FAIL(TEXT("%s: nowhere to stand by its door"), Rooms[St.Room]);
            Place(Test, Map, At.X, At.Y, EYDir::Up);
            St.T0 = Test.Seconds;
            St.Step = 1;
            return EGameTest::Running;
        }
        if (St.Step == 1)
        {
            if (Test.Seconds - St.T0 < .6) return EGameTest::Running;
            if (Field(Test)->CurrentMap != Map) M4_FAIL(TEXT("Red is on %s, not %s"), *Field(Test)->CurrentMap.ToString(), Rooms[St.Room]);
            if (!World(Test)->IsRoomClosed(Map)) M4_FAIL(TEXT("%s has no lid"), Rooms[St.Room]);
            if (!RoomClosed(Test, Map, Why, St.Rays)) M4_FAIL(TEXT("%s"), *Why);
            St.Turn = 0;
            St.T0 = Test.Seconds;
            St.Step = 2;
            return EGameTest::Running;
        }
        bool bFailed = false;
        if (!WalkingCamera(Test, St, Map, Why, bFailed)) return EGameTest::Running;
        if (bFailed) M4_FAIL(TEXT("%s"), *Why);
        St.Note += FString::Printf(TEXT("%s; "), Rooms[St.Room]);
        ++St.Room;
        St.Step = 0;
        return EGameTest::Running;
    }
    FString Stages;
    for (const EPYRStageKind K : {EPYRStageKind::Indoor, EPYRStageKind::CeruleanGym, EPYRStageKind::PewterGym})
        if (!FramedStageClosed(Test, K, Why, St.Rays, Stages)) M4_FAIL(TEXT("%s"), *Why);
    M4_PASS(TEXT("rooms closed from the floor and the walking camera: %s%s%d rays, none met the sky"), *St.Note, *Stages, St.Rays);
}

// ------------------------------------------------------------------------------------------------ frame rate
// stat unit's numbers (frame, game, draw, GPU) over 3 s at each edge view, against a view across Pallet Town.
GAME_TEST(EdgeSceneryFrameRate, 240)
{
    struct FAt { const TCHAR* Map; int32 X, Y; float Yaw; const TCHAR* Name; };
    static const FAt Views[] = {{TEXT("PALLET_TOWN"), 10, 12, -90.f, TEXT("pallet (baseline)")}, {TEXT("ROUTE_22"), 32, 2, -135.f, TEXT("route 22 north-west")},
        {TEXT("ROUTE_4"), 4, 8, 180.f, TEXT("route 4 west")}, {TEXT("ROUTE_4"), 46, 4, -90.f, TEXT("route 4 north")}, {TEXT("ROUTE_24"), 4, 18, 180.f, TEXT("route 24 west")},
        {TEXT("ROUTE_25"), 40, 2, -90.f, TEXT("route 25 north")}};
    static FState St;
    static double Sum[4];
    static int32 Frames = 0;
    static double LastWall = 0;
    static double Base = 0;
    M4_READY();
    APlayerController* PC = Test.World->GetFirstPlayerController();
    if (St.I >= int32(UE_ARRAY_COUNT(Views))) M4_PASS(TEXT("%s"), *St.Note);
    const FAt& V = Views[St.I];
    if (St.Step == 0)
    {
        AfterPokedex(Test);
        Quiet(true, true);
        Place(Test, V.Map, V.X, V.Y, YDir::FromYaw(V.Yaw));
        PC->SetControlRotation(FRotator(-10.f, V.Yaw, 0));
        FMemory::Memzero(Sum);
        Frames = 0;
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(2.0);
        LastWall = FPlatformTime::Seconds();
        M4_NEXT();
    }
    const double Now = FPlatformTime::Seconds();
    Sum[0] += (Now - LastWall) * 1000.0;
    Sum[1] += FPlatformTime::ToMilliseconds(GGameThreadTime);
    Sum[2] += FPlatformTime::ToMilliseconds(GRenderThreadTime);
    Sum[3] += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
    LastWall = Now;
    ++Frames;
    M4_WAIT(3.0);
    const double Frame = Sum[0] / Frames, Game = Sum[1] / Frames, Draw = Sum[2] / Frames, Gpu = Sum[3] / Frames;
    UE_LOG(LogTemp, Display, TEXT("PYR_STAT_UNIT %s: frame %.2f ms, game %.2f ms, draw %.2f ms, gpu %.2f ms (%d frames)"), V.Name, Frame, Game, Draw, Gpu, Frames);
    St.Note += FString::Printf(TEXT("%s: frame %.1f ms, game %.1f, draw %.1f, GPU %.1f; "), V.Name, Frame, Game, Draw, Gpu);
    if (St.I == 0) Base = FMath::Max(Frame, 1.0);
    // No noticeable drop: within half again the town's frame time plus 3 ms, or under 60 fps either way.
    else if (Frame > FMath::Max(Base * 1.5 + 3.0, 16.7)) M4_FAIL(TEXT("%sat %s the frame takes %.1f ms against Pallet's %.1f ms"), *St.Note, V.Name, Frame, Base);
    ++St.I;
    St.Step = 0;
    St.T0 = Test.Seconds;
    return EGameTest::Running;
}
