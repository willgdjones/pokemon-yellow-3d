// m4q: one regression test per QA bug (from the QA bug list), named QA_<bugid> with the
// dashes as underscores (warp-001 -> QA_warp_001). Each checks the thing the bug was about, where the harness saw it:
// visual bugs stage their visual tour shot (QA::ShotCheck) and judge the scene where the picture was taken; warp, text
// and story bugs replay the step that went wrong. The table at the bottom registers them all.
#include "Battle/BattleTestKit.h"
#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRIntroStage.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Menus/PYRSysText.h"
#include "Party/PYRTrainerState.h"
#include "Partner/PYRPikachuFollower.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Stage/PYREncounters.h"
#include "Stage/PYRStage.h"
#include "Tests/QAKit.h"
#include "Yellow/YellowImages.h"
#include "Layout/Children.h"
#include "Widgets/Text/STextBlock.h"

using namespace M4;

namespace
{
// ------------------------------------------------------------------------------------------------ what is drawn
// The words the top screen really draws (every text block in its widget, one per line), not its test description.
void CollectText(const TSharedRef<SWidget>& W, FString& Out)
{
    if (W->GetType() == FName(TEXT("STextBlock"))) Out += StaticCastSharedRef<STextBlock>(W)->GetText().ToString() + TEXT("\n");
    if (FChildren* C = W->GetChildren())
        for (int32 I = 0; I < C->Num(); ++I) CollectText(C->GetChildAt(I), Out);
}

FString DrawnWords(FGameTestContext& Test)
{
    const TSharedPtr<FPYRScreen> Top = Menus(Test)->Top();
    FString Out;
    if (Top.IsValid() && Top->Widget.IsValid()) CollectText(Top->Widget.ToSharedRef(), Out);
    return Out;
}

FName Furn(FGameTestContext& Test, FName Map, int32 X, int32 Y) { return World(Test)->FurnitureAt(Map, X, Y); }

int32 CountFurn(FGameTestContext& Test, FName Map, FName What)
{
    const FYMap* M = Data(Test)->Map(Map);
    int32 N = 0;
    for (int32 Y = 0; M && Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X) N += Furn(Test, Map, X, Y) == What;
    return N;
}

// A blocking hit that is really drawn (not a hidden occluder instance, not an invisible capsule).
bool Visible(const FHitResult& H)
{
    const UPrimitiveComponent* C = H.GetComponent();
    if (!C || !C->IsVisible() || C->bHiddenInGame || (C->GetOwner() && C->GetOwner()->IsHidden())) return false;
    if (const UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(C))
    {
        FTransform T;
        if (H.Item != INDEX_NONE && I->GetInstanceTransform(H.Item, T, true) && T.GetScale3D().GetMin() < .01) return false;
    }
    return true;
}

bool Eye(FGameTestContext& Test, FVector& OutEye, FRotator& OutRot, float& OutFov)
{
    APlayerController* PC = Test.World->GetFirstPlayerController();
    if (!PC || !PC->PlayerCameraManager) return false;
    OutEye = PC->PlayerCameraManager->GetCameraLocation();
    OutRot = PC->PlayerCameraManager->GetCameraRotation();
    OutFov = PC->PlayerCameraManager->GetFOVAngle();
    return true;
}

// Who is in the picture: inside the 16:9 frame and not behind anything drawn (checked at the body and the head).
bool InView(FGameTestContext& Test, const AActor* A, const TCHAR* Who, FString& Why)
{
    FVector E;
    FRotator R;
    float Fov = 90.f;
    if (!A) { Why += FString::Printf(TEXT("%s is not there; "), Who); return false; }
    if (A->IsHidden()) { Why += FString::Printf(TEXT("%s is hidden; "), Who); return false; }
    if (!Eye(Test, E, R, Fov)) { Why += TEXT("no camera; "); return false; }
    const float HalfH = Fov * .5f;
    const float HalfV = FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(HalfH)) * 9.f / 16.f));
    FCollisionQueryParams Params(TEXT("QAInView"), false);
    Params.AddIgnoredActor(A);
    if (AActor* Red = M4::Red(Test)) Params.AddIgnoredActor(Red);
    for (TActorIterator<APYRPikachuFollower> It(Test.World); It; ++It) Params.AddIgnoredActor(*It);
    FString Blocker;
    for (const float Up : {0.f, 50.f})
    {
        const FVector T = A->GetActorLocation() + FVector(0, 0, Up);
        const FVector L = R.UnrotateVector(T - E);
        if (L.X <= 0) { Blocker = TEXT("behind the camera"); continue; }
        const float Across = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(L.Y), L.X)), Vert = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(L.Z), L.X));
        if (Across > HalfH || Vert > HalfV) { Blocker = FString::Printf(TEXT("out of frame (%.0f across, %.0f up/down)"), Across, Vert); continue; }
        TArray<FHitResult> Hits;
        Test.World->LineTraceMultiByChannel(Hits, E, T, ECC_Visibility, Params);
        const FHitResult* Hit = Hits.FindByPredicate([](const FHitResult& H) { return H.bBlockingHit && Visible(H); });
        if (!Hit) return true;
        Blocker = FString::Printf(TEXT("behind %s"), *GetNameSafe(Hit->GetActor()));
    }
    Why += FString::Printf(TEXT("%s not seen: %s; "), Who, *Blocker);
    return false;
}

// The camera stands in the open (not inside a wall, a roof or a tree) at least MinDist from Of.
bool CameraClear(FGameTestContext& Test, const AActor* Of, float MinDist, FString& Why)
{
    FVector E;
    FRotator R;
    float Fov;
    if (!Eye(Test, E, R, Fov) || !Of) { Why += TEXT("no camera; "); return false; }
    const float D = FVector::Dist(E, Of->GetActorLocation());
    if (D < MinDist) { Why += FString::Printf(TEXT("the camera is %.0f cm from %s (at least %.0f wanted); "), D, *Of->GetName(), MinDist); return false; }
    TArray<FOverlapResult> Over;
    FCollisionQueryParams Params(TEXT("QACamera"), false);
    Params.AddIgnoredActor(Of);
    Test.World->OverlapMultiByChannel(Over, E, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(8.f), Params);
    for (const FOverlapResult& O : Over)
    {
        FHitResult H;
        H.Component = O.Component;
        H.Item = O.ItemIndex;
        H.bBlockingHit = true;
        if (O.bBlockingHit && Visible(H)) { Why += FString::Printf(TEXT("the camera is inside %s; "), *GetNameSafe(O.GetActor())); return false; }
    }
    return true;
}

bool RedSeen(FGameTestContext& Test, FString& Why)
{
    const bool bOk = InView(Test, Red(Test), TEXT("Red"), Why) && CameraClear(Test, Red(Test), 150.f, Why);
    if (bOk) Why += TEXT("Red in the picture, the camera in the open");
    return bOk;
}

// Every person shown on Map draws their model (never the grey stand-in).
bool PeopleModels(FGameTestContext& Test, FName Map, FString& Why)
{
    int32 N = 0;
    TArray<FString> Bad;
    for (const TWeakObjectPtr<APYRPerson>& PW : Field(Test)->People)
    {
        const APYRPerson* P = PW.Get();
        if (!P || P->Map != Map || !P->IsShown() || P->Model.bProp) continue;
        ++N;
        TArray<UMeshComponent*> Meshes;
        P->GetComponents<UMeshComponent>(Meshes);
        bool bModel = false;
        for (const UMeshComponent* C : Meshes)
        {
            const FString Path = C->IsA<USkeletalMeshComponent>() ? GetPathNameSafe(Cast<USkeletalMeshComponent>(C)->GetSkeletalMeshAsset())
                : C->IsA<UStaticMeshComponent>() ? GetPathNameSafe(Cast<UStaticMeshComponent>(C)->GetStaticMesh()) : FString();
            bModel |= Path.StartsWith(TEXT("/Game/Characters/")) || Path.StartsWith(TEXT("/Game/Pokemon/"));
        }
        if (!bModel) Bad.Add(FString::Printf(TEXT("%s (%s)"), *P->Id, *P->Model.Model));
    }
    if (Bad.Num()) { Why += FString::Printf(TEXT("no model for %s; "), *FString::Join(Bad, TEXT(", "))); return false; }
    Why += FString::Printf(TEXT("%d people, every one a model; "), N);
    return true;
}

// Chairs where Yellow's walkable 't' tiles are, tables on its solid ones (not the tops of bookshelves), and nobody
// standing inside a table.
bool ChairsAndTables(FGameTestContext& Test, FName Map, FString& Why)
{
    const FYMap* M = Data(Test)->Map(Map);
    if (!M) { Why = TEXT("no map"); return false; }
    TArray<FString> Bad;
    int32 Chairs = 0, Tables = 0;
    for (int32 Y = 0; Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X)
        {
            const FName F = Furn(Test, Map, X, Y);
            const bool bT = M->Look(X, Y) == TEXT('t');
            const bool bSolid = YTile::IsSolid(M->Tile(X, Y));
            const bool bShelfTop = bT && bSolid && M->Look(X, Y + 1) == TEXT('b') && YTile::IsSolid(M->Tile(X, Y + 1));
            FName Want = NAME_None;
            if (bT && !bSolid) Want = TEXT("chair");
            else if (bT && !bShelfTop) Want = AYellowWorld::IsBackWallTile(*M, X, Y) ? TEXT("wall") : TEXT("table");   // a desk's top in the back wall row
            if (F == TEXT("table") || F == TEXT("chair") || (Want != NAME_None && Want != TEXT("wall")))
                if (F != Want) Bad.Add(FString::Printf(TEXT("%d,%d %s (Yellow: %s)"), X, Y, *F.ToString(), *Want.ToString()));
            Chairs += F == TEXT("chair");
            Tables += F == TEXT("table");
        }
    const FIntPoint RedAt = Field(Test)->CurrentMap == Map ? Field(Test)->CurrentTile : FIntPoint(-1, -1);
    if (RedAt.X >= 0 && Furn(Test, Map, RedAt.X, RedAt.Y) == TEXT("table")) Bad.Add(TEXT("Red stands in a table"));
    for (const TWeakObjectPtr<APYRPerson>& PW : Field(Test)->People)
        if (PW.IsValid() && PW->Map == Map && PW->IsShown() && !PW->Model.bProp)
        {
            const FIntPoint L = PW->Tile - M->Origin;
            if (Furn(Test, Map, L.X, L.Y) == TEXT("table")) Bad.Add(PW->Id + TEXT(" stands in a table"));
        }
    if (Bad.Num()) { Why += FString::Join(Bad, TEXT("; ")); return false; }
    Why += FString::Printf(TEXT("%d chairs and %d table tiles where Yellow has them; "), Chairs, Tables);
    return true;
}

// What stands on each tile follows Yellow's looks: trees on T, posts on F, tall grass on G, rock on r, and nothing
// solid on a tile Red can walk.
bool LooksMatch(FGameTestContext& Test, FName Map, FString& Why)
{
    const FYMap* M = Data(Test)->Map(Map);
    if (!M) { Why = TEXT("no map"); return false; }
    static const TSet<FName> Solid = {TEXT("tree"), TEXT("rock"), TEXT("fence"), TEXT("bush"), TEXT("bookshelf"), TEXT("table"), TEXT("block"),
        TEXT("cave_wall"), TEXT("wall"), TEXT("machine"), TEXT("pod"), TEXT("counter"), TEXT("mart_shelf"), TEXT("exhibit"), TEXT("statue")};
    const bool bOutdoorLike = M->bOutdoor || M->Tileset.ToUpper() == TEXT("FOREST");
    TArray<FString> Bad;
    for (int32 Y = 0; Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X)
        {
            const TCHAR L = M->Look(X, Y), T = M->Tile(X, Y);
            const FName F = Furn(Test, Map, X, Y);
            FName Want = NAME_None;
            if (T == TEXT('x')) Want = TEXT("cut_tree");
            else if (YTile::IsLedge(T) || L == TEXT('L')) Want = TEXT("ledge");
            else if (L == TEXT('T') && bOutdoorLike) Want = TEXT("tree");
            else if (L == TEXT('F')) Want = TEXT("fence");
            else if (L == TEXT('G')) Want = TEXT("tall_grass");
            else if (L == TEXT('r')) Want = TEXT("rock");
            if (Want != NAME_None && F != Want) Bad.Add(FString::Printf(TEXT("%d,%d '%c' has %s"), X, Y, L, *F.ToString()));
            if (Want == NAME_None && YTile::IsWalkable(T) && Solid.Contains(F)) Bad.Add(FString::Printf(TEXT("%d,%d walkable '%c' has %s"), X, Y, L, *F.ToString()));
        }
    if (Bad.Num())
    {
        if (Bad.Num() > 6) { const int32 More = Bad.Num() - 6; Bad.SetNum(6); Bad.Add(FString::Printf(TEXT("%d more"), More)); }
        Why += FString::Join(Bad, TEXT("; "));
        return false;
    }
    Why += FString::Printf(TEXT("%s: every tile drawn as its Yellow look; "), *Map.ToString());
    return true;
}

// Every solid tile of a room or cave has its wall, rock or furniture (none left as open floor).
bool SolidDrawn(FGameTestContext& Test, FName Map, FString& Why)
{
    const FYMap* M = Data(Test)->Map(Map);
    if (!M) { Why = TEXT("no map"); return false; }
    TArray<FString> Bad;
    int32 N = 0;
    for (int32 Y = 0; Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X)
        {
            const TCHAR T = M->Tile(X, Y);
            if (!YTile::IsSolid(T) || T == TEXT('w') || T == TEXT('=') || YTile::IsLedge(T)) continue;
            // The top of a two-tile piece (a PC, a bookshelf top) is drawn from its other tile.
            const TCHAR L = M->Look(X, Y);
            if ((L == TEXT('P') && M->Look(X, Y - 1) == TEXT('P')) || (L == TEXT('t') && M->Look(X, Y + 1) == TEXT('b'))) continue;
            ++N;
            if (Furn(Test, Map, X, Y) == NAME_None) Bad.Add(FString::Printf(TEXT("%d,%d '%c'"), X, Y, L));
        }
    if (Bad.Num())
    {
        if (Bad.Num() > 8) { const int32 More = Bad.Num() - 8; Bad.SetNum(8); Bad.Add(FString::Printf(TEXT("%d more"), More)); }
        Why += TEXT("solid tiles drawn as open floor: ") + FString::Join(Bad, TEXT(", "));
        return false;
    }
    Why += FString::Printf(TEXT("all %d solid tiles of %s drawn; "), N, *Map.ToString());
    return true;
}

// A room (gate, gym, house) closed over with its lid, and no other map drawn while Red is in it.
bool RoomOnly(FGameTestContext& Test, FName Map, FString& Why)
{
    AYellowWorld* W = World(Test);
    if (!W->IsRoomClosed(Map)) { Why += Map.ToString() + TEXT(" has no lid; "); return false; }
    TArray<FString> Drawn;
    for (const FName& Other : Data(Test)->MapOrder) if (Other != Map && W->IsMapDrawn(Other)) Drawn.Add(Other.ToString());
    if (Drawn.Num()) { Why += FString::Printf(TEXT("drawn while Red is in %s: %s; "), *Map.ToString(), *FString::Join(Drawn, TEXT(", "))); return false; }
    if (AActor* Sc = W->GetSceneryActor()) if (!Sc->IsHidden()) { Why += TEXT("the Kanto scenery is drawn; "); return false; }
    Why += FString::Printf(TEXT("%s closed, nothing outside it drawn; "), *Map.ToString());
    return true;
}

// A Pokemon Center: one nurse, one healing machine beside her, the PC at the east end.
bool Centre(FGameTestContext& Test, FName Map, FString& Why)
{
    int32 Nurses = 0;
    for (const TWeakObjectPtr<APYRPerson>& PW : Field(Test)->People)
        if (PW.IsValid() && PW->Map == Map && PW->IsShown() && PW->Model.Model == TEXT("nurse_joy")) ++Nurses;
    const int32 Healers = CountFurn(Test, Map, TEXT("healer")), Pcs = CountFurn(Test, Map, TEXT("pc"));
    Why += FString::Printf(TEXT("%d nurse(s), %d healing machine(s), %d PC(s); "), Nurses, Healers, Pcs);
    return Nurses == 1 && Healers == 1 && Pcs >= 1;
}

// Nothing missing round Map: every tile within Ring of it that no map covers has scenery drawn.
bool NoVoidNear(FGameTestContext& Test, FName Map, int32 Ring, FString& Why)
{
    const FYMap* M = Data(Test)->Map(Map);
    if (!M) { Why = TEXT("no map"); return false; }
    const TMap<FIntPoint, uint8> Off = QA::DrawnOffMap(Test, Ring);
    int32 Empty = 0, N = 0;
    FIntPoint First(0, 0);
    for (const auto& Pair : Off)
    {
        const FIntPoint K = Pair.Key;
        if (K.X < M->Origin.X - Ring || K.Y < M->Origin.Y - Ring || K.X >= M->Origin.X + M->Width + Ring || K.Y >= M->Origin.Y + M->Height + Ring) continue;
        ++N;
        if (Pair.Value == 0) { if (!Empty) First = K; ++Empty; }
    }
    if (Empty) { Why += FString::Printf(TEXT("%d of %d tiles round %s show nothing (first at Kanto %d,%d); "), Empty, N, *Map.ToString(), First.X, First.Y); return false; }
    Why += FString::Printf(TEXT("all %d tiles within %d of %s drawn; "), N, Ring, *Map.ToString());
    return N > 0;
}

APYRPerson* Shown(FGameTestContext& Test, const TCHAR* Map, const TCHAR* Id)
{
    APYRPerson* P = Person(Test, Map, Id);
    return P && P->IsShown() ? P : nullptr;
}

// ------------------------------------------------------------------------------------------------ warps
struct FWarpState { int32 Step = 0; double T0 = 0; int32 Blocked = 0; };

// Red walks onto Map's warp at (X, Y) from the side Approach names; he must land on (ToX, ToY) of ToMap.
EGameTest TakeWarp(FGameTestContext& Test, FWarpState& St, FName Map, int32 X, int32 Y, EYDir Approach, FName ToMap, int32 ToX, int32 ToY)
{
    UPYRField* F = Field(Test);
    if (St.Step == 0)
    {
        AfterPokedex(Test);
        Quiet(true, true);
        const FIntPoint D = YDir::Delta(Approach);
        if (!Place(Test, Map, X - D.X, Y - D.Y, Approach)) M4_FAIL(TEXT("can't stand at %d,%d"), X - D.X, Y - D.Y);
        St.T0 = Test.Seconds;
        St.Step = 1;
        return EGameTest::Running;
    }
    if (St.Step == 1)
    {
        if (Test.Seconds - St.T0 < .6) return EGameTest::Running;
        if (F->CurrentMap != ToMap) { WalkToTile(Test, Map, X, Y, 8.f); if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the warp never took Red to %s (at %s %d,%d)"), *ToMap.ToString(), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y); return EGameTest::Running; }
        StopWalking(Test);
        St.Step = 2;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (!QA::Idle(Test) && Test.Seconds - St.T0 < 6) return EGameTest::Running;
    if (Test.Seconds - St.T0 < 1.5) return EGameTest::Running;
    if (F->CurrentTile != FIntPoint(ToX, ToY)) M4_FAIL(TEXT("landed on %s %d,%d, Yellow puts Red on %d,%d"), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y, ToX, ToY);
    M4_PASS(TEXT("%s %d,%d -> %s %d,%d, as Yellow"), *Map.ToString(), X, Y, *ToMap.ToString(), ToX, ToY);
}

// A warp out of the range (Route 23, Diglett's Cave, Cerulean Cave) is blocked: Red is told why and stays.
EGameTest BlockedWarp(FGameTestContext& Test, FWarpState& St, FName Map, int32 X, int32 Y, const TCHAR* Words)
{
    UPYRField* F = Field(Test);
    const FYMap* M = Data(Test)->Map(Map);
    if (!M) M4_FAIL(TEXT("no map"));
    if (St.Step == 0)
    {
        AfterPokedex(Test);
        Quiet(true, true);
        bool bPlaced = false;
        for (const EYDir Dir : {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right})
        {
            const FIntPoint From = FIntPoint(X, Y) - YDir::Delta(Dir);
            if (!M->Contains(From.X, From.Y) || YTile::IsSolid(M->Tile(From.X, From.Y)) || M->WarpAt(From.X, From.Y) != INDEX_NONE) continue;
            bPlaced = Place(Test, Map, From.X, From.Y, Dir);
            if (bPlaced) break;
        }
        if (!bPlaced) M4_FAIL(TEXT("no tile beside the warp at %d,%d"), X, Y);
        St.Blocked = F->WarpsBlocked;
        St.T0 = Test.Seconds;
        St.Step = 1;
        return EGameTest::Running;
    }
    if (St.Step == 1)
    {
        if (Test.Seconds - St.T0 < .6) return EGameTest::Running;
        if (F->WarpsBlocked == St.Blocked)
        {
            if (F->CurrentMap != Map) M4_FAIL(TEXT("the warp took Red to %s"), *F->CurrentMap.ToString());
            WalkToTile(Test, Map, X, Y, 8.f);
            if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("stepping onto %d,%d was neither blocked nor a warp (Red at %d,%d)"), X, Y, F->CurrentTile.X, F->CurrentTile.Y);
            return EGameTest::Running;
        }
        StopWalking(Test);
        const FString Text = Menus(Test)->ScreenText();
        // Words "" : Yellow has no line here, so nothing may be said.
        if (Words && !*Words && (Menus(Test)->AnyOpen() || !Text.IsEmpty())) M4_FAIL(TEXT("blocked, but Red was told '%s' (Yellow has no line here)"), *Text.Left(120));
        if (Words && *Words && !Text.Contains(Words)) M4_FAIL(TEXT("blocked, but Red was told '%s' (want '%s')"), *Text.Left(120), Words);
        St.Step = 2;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (!QA::Idle(Test)) { QA::Drive(Test); if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("Red was never let go after the warning")); return EGameTest::Running; }
    // He walks back off the warp after the words.
    if (F->CurrentMap == Map && F->CurrentTile == FIntPoint(X, Y) && Test.Seconds - St.T0 < 8) return EGameTest::Running;
    if (F->CurrentMap != Map || F->CurrentTile == FIntPoint(X, Y)) M4_FAIL(TEXT("Red ended at %s %d,%d"), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y);
    M4_PASS(TEXT("%s %d,%d is out of the range: Red is %s and steps back to %d,%d"), *Map.ToString(), X, Y, Words && !*Words ? TEXT("told nothing (Yellow has no line)") : TEXT("told so"),
        F->CurrentTile.X, F->CurrentTile.Y);
}

// The band past one edge of an outdoor map (4 tiles deep, as the harness samples it): scenery on every tile, trees
// where the neighbouring map (or its border block) has trees and ground where it has open ground or water.
EGameTest EdgeScenery(FGameTestContext& Test, FName Map, EYDir Out)
{
    const FYMap* M = Data(Test)->Map(Map);
    AYellowWorld* W = World(Test);
    if (!M) M4_FAIL(TEXT("no map"));
    const TMap<FIntPoint, uint8> Off = QA::DrawnOffMap(Test, 6);
    int32 N = 0, Match = 0;
    TArray<FString> Bad;
    const FIntRect R = M->WorldRect();
    for (int32 Depth = 0; Depth < 4; ++Depth)
    {
        const int32 Len = Out == EYDir::Up || Out == EYDir::Down ? M->Width : M->Height;
        for (int32 I = 0; I < Len; ++I)
        {
            FIntPoint K = Out == EYDir::Up ? FIntPoint(R.Min.X + I, R.Min.Y - 1 - Depth) : Out == EYDir::Down ? FIntPoint(R.Min.X + I, R.Max.Y + Depth)
                : Out == EYDir::Left ? FIntPoint(R.Min.X - 1 - Depth, R.Min.Y + I) : FIntPoint(R.Max.X + Depth, R.Min.Y + I);
            const uint8* Bits = Off.Find(K);
            if (!Bits) continue;   // another map is there
            ++N;
            const TCHAR L = W->SceneryLookAt(K);
            const FString Kind = QA::LookKind(L);
            const bool bOk = L != 0 && (Kind == TEXT("trees") ? (*Bits & 1) != 0 : (*Bits & 6) != 0);
            if (bOk) ++Match;
            else if (Bad.Num() < 6) Bad.Add(FString::Printf(TEXT("Kanto %d,%d look '%c' (%s) drawn %d"), K.X, K.Y, L ? L : TEXT('0'), *Kind, *Bits));
        }
    }
    if (N == 0) M4_FAIL(TEXT("no open tiles past %s's %s edge"), *Map.ToString(), YDir::Name(Out));
    if (Match < N) M4_FAIL(TEXT("%d of %d tiles past %s's %s edge unlike Yellow: %s"), N - Match, N, *Map.ToString(), YDir::Name(Out), *FString::Join(Bad, TEXT("; ")));
    M4_PASS(TEXT("all %d tiles past %s's %s edge drawn as Yellow's scenery"), N, *Map.ToString(), YDir::Name(Out));
}

// ------------------------------------------------------------------------------------------------ text
// text.json's line for Label is pokeyellow's, word for word.
EGameTest TextIs(FGameTestContext& Test, const TCHAR* Label, const TCHAR* Want)
{
    M4_READY();
    const FString* Got = Data(Test)->Text.Find(Label);
    if (!Got) M4_FAIL(TEXT("no text %s"), Label);
    if (*Got != Want) M4_FAIL(TEXT("%s is '%s', pokeyellow '%s'"), Label, *Got->ReplaceCharWithEscapedChar(), *FString(Want).ReplaceCharWithEscapedChar());
    M4_PASS(TEXT("%s as pokeyellow: '%s'"), Label, *Got->ReplaceCharWithEscapedChar());
}

// ------------------------------------------------------------------------------------------------ the table
struct FBug
{
    const TCHAR* Id;
    float Timeout;
    const TCHAR* Shot;                                         // a visual tour shot, judged by Check
    TFunction<bool(FGameTestContext&, FString&)> Check;
    TFunction<EGameTest(FGameTestContext&)> Run;         // or a test of its own
};

using FCheck = TFunction<bool(FGameTestContext&, FString&)>;
FCheck MapCheck(const TCHAR* Map, bool (*Fn)(FGameTestContext&, FName, FString&)) { const FName M(Map); return [M, Fn](FGameTestContext& T, FString& W) { return Fn(T, M, W); }; }
FCheck RedCheck() { return [](FGameTestContext& T, FString& W) { return RedSeen(T, W); }; }
FCheck Both(FCheck A, FCheck B) { return [A, B](FGameTestContext& T, FString& W) { const bool bA = A(T, W); const bool bB = B(T, W); return bA && bB; }; }
FCheck FurnAt(const TCHAR* Map, int32 X, int32 Y, const TCHAR* What)
{
    const FName M(Map), Wh(What);
    return [M, X, Y, Wh](FGameTestContext& T, FString& W)
    {
        const FName F = Furn(T, M, X, Y);
        W += FString::Printf(TEXT("%s %d,%d: %s; "), *M.ToString(), X, Y, *F.ToString());
        return F == Wh;
    };
}
// Every tile with look L on Map carries What (and there is at least one).
FCheck EveryLook(const TCHAR* Map, TCHAR L, const TCHAR* What)
{
    const FName M(Map), Wh(What);
    return [M, L, Wh](FGameTestContext& T, FString& W)
    {
        const FYMap* Mp = Data(T)->Map(M);
        int32 N = 0;
        TArray<FString> Bad;
        for (int32 Y = 0; Mp && Y < Mp->Height; ++Y)
            for (int32 X = 0; X < Mp->Width; ++X)
                if (Mp->Look(X, Y) == L)
                {
                    ++N;
                    if (Furn(T, M, X, Y) != Wh) Bad.Add(FString::Printf(TEXT("%d,%d %s"), X, Y, *Furn(T, M, X, Y).ToString()));
                }
        W += FString::Printf(TEXT("%d '%c' tiles on %s drawn as %s%s%s; "), N, L, *M.ToString(), *Wh.ToString(), Bad.Num() ? TEXT(", except ") : TEXT(""), *FString::Join(Bad, TEXT(", ")));
        return N > 0 && Bad.Num() == 0;
    };
}
FCheck NoFurn(const TCHAR* Map, std::initializer_list<const TCHAR*> Whats)
{
    const FName M(Map);
    TArray<FName> List;
    for (const TCHAR* S : Whats) List.Add(S);
    return [M, List](FGameTestContext& T, FString& W)
    {
        bool bOk = true;
        for (const FName& Wh : List) { const int32 N = CountFurn(T, M, Wh); if (N) { W += FString::Printf(TEXT("%d %s on %s; "), N, *Wh.ToString(), *M.ToString()); bOk = false; } }
        if (bOk) W += FString::Printf(TEXT("no %s on %s; "), *FString::JoinBy(List, TEXT("/"), [](const FName& N) { return N.ToString(); }), *M.ToString());
        return bOk;
    };
}
FCheck Seen(const TCHAR* Map, const TCHAR* Id)
{
    return [Map, Id](FGameTestContext& T, FString& W)
    {
        const bool bOk = InView(T, Shown(T, Map, Id), Id, W);
        if (bOk) W += FString::Printf(TEXT("%s in the picture; "), Id);
        return bOk;
    };
}
FCheck StageShows(const TCHAR* Who)
{
    return [Who](FGameTestContext& T, FString& W)
    {
        AYellowIntroStage* S = AYellowIntroStage::Find(T.World);
        if (!S) { W = TEXT("no intro stage"); return false; }
        W += FString::Printf(TEXT("the 3D stage shows %s (%d models loaded); "), *FString::JoinBy(S->Shown(), TEXT(","), [](const FName& N) { return N.ToString(); }), S->ModelsLoaded());
        return S->IsShown(Who) && S->ModelsLoaded() > 0;
    };
}
// The screen's words use only characters the Game Boy font draws (no replacement box).
FCheck FontOk()
{
    return [](FGameTestContext& T, FString& W)
    {
        const FString S = DrawnWords(T);
        for (const TCHAR C : S)
            if (C == 0xFFFD || (C >= 0xE000 && C <= 0xF8FF)) { W += FString::Printf(TEXT("character U+%04X on screen: '%s'"), int32(C), *S.Left(80)); return false; }
        W += TEXT("no replacement glyphs; ");
        return S.Len() > 0;
    };
}

// ---- m4q integration (the harness's second visual run)
// Who talks to Red is not hidden behind him: the camera's lines to their body and head miss Red's.
FCheck NotBehindRed(const TCHAR* Map, const TCHAR* Id)
{
    return [Map, Id](FGameTestContext& T, FString& W)
    {
        const APYRPerson* P = Shown(T, Map, Id);
        const AActor* R = Red(T);
        FVector E;
        FRotator Rot;
        float Fov;
        if (!P || !R || !Eye(T, E, Rot, Fov)) { W += FString::Printf(TEXT("%s, Red or the camera missing; "), Id); return false; }
        // Their body and head against Red's legs, middle and chest (the camera keeps 40 cm clear in UpdateRoomCamera).
        for (const float PersonUp : {-50.f, 0.f, 50.f})
        {
            const FVector Seg = P->GetActorLocation() + FVector(0, 0, PersonUp) - E;
            for (const float RedUp : {-50.f, 0.f, 50.f})
            {
                const FVector Body = R->GetActorLocation() + FVector(0, 0, RedUp);
                const float A = FMath::Clamp(FVector::DotProduct(Body - E, Seg) / FMath::Max(1.f, Seg.SizeSquared()), 0.f, 1.f);
                const float D = FVector::Dist(E + Seg * A, Body);
                if (A < .97f && D < 30.f) { W += FString::Printf(TEXT("%s stands behind Red (the camera's line to them passes %.0f cm from him); "), Id, D); return false; }
            }
        }
        W += FString::Printf(TEXT("%s is not behind Red; "), Id);
        return true;
    };
}
// Nobody standing still beside Red or in the two tiles in front of him (a person a step ahead stands inside him in the
// picture).
FCheck ClearAroundRed()
{
    return [](FGameTestContext& T, FString& W)
    {
        const FIntPoint R = Field(T)->KantoTile;
        for (const TWeakObjectPtr<APYRPerson>& PW : Field(T)->People)
        {
            const APYRPerson* P = PW.Get();
            if (!P || !P->IsShown() || P->bWalker || P->Map != Field(T)->CurrentMap) continue;   // walkers wander up to him, as on the Game Boy
            const FIntPoint D = P->Tile - R;
            if ((FMath::Abs(D.X) <= 1 && FMath::Abs(D.Y) <= 1) || (D.X == 0 && D.Y == -2))
            { W += FString::Printf(TEXT("%s stands at %d,%d from Red; "), *P->Id, D.X, D.Y); return false; }
        }
        W += TEXT("nobody crowds Red; ");
        return true;
    };
}
FCheck WaterCheck(const TCHAR* Map) { return [Map](FGameTestContext& T, FString& W) { int32 N = 0; return QA::WaterShows(T, Map, W, &N) && N > 0; }; }

EGameTest RunBug(FGameTestContext& Test);

TArray<FBug>& Bugs()
{
    static TArray<FBug> List;
    if (List.Num()) return List;
    auto Shot = [](const TCHAR* Id, const TCHAR* S, FCheck C) { List.Add({Id, 240.f, S, MoveTemp(C), nullptr}); };
    auto Own = [](const TCHAR* Id, float Timeout, TFunction<EGameTest(FGameTestContext&)> R) { List.Add({Id, Timeout, nullptr, nullptr, MoveTemp(R)}); };

    // ---- warps and edges
    Own(TEXT("warp-001"), 60, [](FGameTestContext& Test)
    {
        M4_READY();
        const FYMap* M = Data(Test)->Map(TEXT("REDS_HOUSE_2F"));
        if (!M) M4_FAIL(TEXT("no map"));
        if (M->Warps.Num() != 1 || M->Warps[0].X != 7 || M->Warps[0].Y != 1 || M->Warps[0].ToMap != TEXT("REDS_HOUSE_1F"))
            M4_FAIL(TEXT("REDS_HOUSE_2F has %d warps (first %d,%d -> %s)"), M->Warps.Num(), M->Warps.Num() ? M->Warps[0].X : -1, M->Warps.Num() ? M->Warps[0].Y : -1, M->Warps.Num() ? *M->Warps[0].ToMap.ToString() : TEXT("-"));
        for (int32 Y = 2; Y <= 5; ++Y) if (M->WarpAt(7, Y) != INDEX_NONE) M4_FAIL(TEXT("a warp at 7,%d"), Y);
        M4_PASS(TEXT("one warp, 7,1 -> REDS_HOUSE_1F (the debug warps at 7,2-7,5 are gone)"));
    });
    Own(TEXT("warp-002"), 120, [](FGameTestContext& Test) { M4_READY(); return EdgeScenery(Test, TEXT("PALLET_TOWN"), EYDir::Down); });
    Own(TEXT("warp-015"), 120, [](FGameTestContext& Test) { M4_READY(); return EdgeScenery(Test, TEXT("ROUTE_22"), EYDir::Up); });
    Own(TEXT("warp-016"), 120, [](FGameTestContext& Test) { M4_READY(); return EdgeScenery(Test, TEXT("CERULEAN_CITY"), EYDir::Right); });
    Own(TEXT("warp-017"), 120, [](FGameTestContext& Test) { M4_READY(); return EdgeScenery(Test, TEXT("CERULEAN_CITY"), EYDir::Down); });
    Own(TEXT("warp-003"), 120, [](FGameTestContext& Test) { static FWarpState St; M4_READY(); return BlockedWarp(Test, St, TEXT("ROUTE_22_GATE"), 4, 0, TEXT("You can pass here")); });
    Own(TEXT("warp-004"), 120, [](FGameTestContext& Test) { static FWarpState St; M4_READY(); return BlockedWarp(Test, St, TEXT("DIGLETTS_CAVE_ROUTE_2"), 4, 4, nullptr); });
    Own(TEXT("warp-005"), 120, [](FGameTestContext& Test)
    {
        // Cerulean Cave's door (4,11) is on the far side of the water (Surf, past the range): no way to it on foot,
        // and its warp, past the range, never takes Red anywhere.
        M4_READY();
        AfterPokedex(Test);
        PYRScripts::Refresh(Test.World);
        const FYMap* M = Data(Test)->Map(TEXT("CERULEAN_CITY"));
        const int32 Idx = M ? M->WarpAt(4, 11) : INDEX_NONE;
        if (Idx == INDEX_NONE) M4_FAIL(TEXT("no warp at 4,11"));
        TArray<QA::FStep> Path;
        if (QA::Plan(Test, QA::K(Test, TEXT("CERULEAN_CITY"), 19, 18), QA::K(Test, TEXT("CERULEAN_CITY"), 4, 12), Path)) M4_FAIL(TEXT("Red can walk to Cerulean Cave's door (%d steps) without Surf"), Path.Num());
        Place(Test, TEXT("CERULEAN_CITY"), 4, 12, EYDir::Up);
        if (Field(Test)->TakeWarp(TEXT("CERULEAN_CITY"), Idx)) M4_FAIL(TEXT("the warp to %s was taken"), *M->Warps[Idx].ToMap.ToString());
        M4_PASS(TEXT("Cerulean Cave is across the water (no path on foot) and its warp, past the range, is refused"));
    });
    struct FW { const TCHAR* Id; const TCHAR* Map; int32 X, Y; EYDir Dir; const TCHAR* To; int32 TX, TY; };
    static const FW Warps[] = {
        {TEXT("warp-006"), TEXT("ROUTE_2"), 3, 11, EYDir::Down, TEXT("VIRIDIAN_FOREST_NORTH_GATE"), 5, 1},
        {TEXT("warp-007"), TEXT("ROUTE_2"), 16, 35, EYDir::Down, TEXT("ROUTE_2_GATE"), 5, 1},
        {TEXT("warp-008"), TEXT("ROUTE_2"), 17, 35, EYDir::Down, TEXT("ROUTE_2_GATE"), 5, 1},
        {TEXT("warp-009"), TEXT("VIRIDIAN_FOREST"), 15, 47, EYDir::Down, TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), 5, 1},
        {TEXT("warp-010"), TEXT("VIRIDIAN_FOREST"), 16, 47, EYDir::Down, TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), 5, 1},
        {TEXT("warp-011"), TEXT("VIRIDIAN_FOREST"), 17, 47, EYDir::Down, TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), 5, 1},
        {TEXT("warp-012"), TEXT("VIRIDIAN_FOREST"), 18, 47, EYDir::Down, TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), 5, 1},
        {TEXT("warp-013"), TEXT("CERULEAN_CITY"), 27, 9, EYDir::Left, TEXT("CERULEAN_TRASHED_HOUSE"), 3, 1},
        {TEXT("warp-014"), TEXT("CERULEAN_CITY"), 9, 9, EYDir::Left, TEXT("CERULEAN_BADGE_HOUSE"), 2, 1},
    };
    for (const FW& W : Warps)
        Own(W.Id, 120, [W](FGameTestContext& Test) { static FWarpState St; M4_READY(); return TakeWarp(Test, St, W.Map, W.X, W.Y, W.Dir, W.To, W.TX, W.TY); });

    // ---- story
    Shot(TEXT("story-001"), TEXT("beat-intro"), StageShows(TEXT("oak")));
    Own(TEXT("story-002"), 90, [](FGameTestContext& Test)
    {
        static int32 Step = 0;
        static FString Named;
        M4_READY();
        UPYRMenus* Me = Menus(Test);
        if (Step == 0)
        {
            Me->CloseAll();
            PYRScreens::OpenNaming(Test.World, TEXT("YOUR NAME?"), TEXT("RED"), [](FString N) { Named = N; });
            Step = 1;
            return EGameTest::Running;
        }
        if (Step == 1)
        {
            if (TopId(Test) != TEXT("naming")) M4_FAIL(TEXT("no naming screen"));
            // R, E, D on the keyboard (E and D are letters here, not confirm and right), then Enter.
            Me->Type(TEXT("RED\n"));
            Step = 2;
            return EGameTest::Running;
        }
        if (TopId(Test) == TEXT("naming")) M4_FAIL(TEXT("Enter did not end the name: %s"), *Me->Top()->Describe());
        if (Named != TEXT("RED")) M4_FAIL(TEXT("typing R E D Enter named Red '%s'"), *Named);
        M4_PASS(TEXT("R E D Enter on the keyboard names him RED"));
    });
    // Yellow's order (data/maps/toggleable_objects.asm, BillsHouse_2.asm, CeruleanHideRocket): GUARD2 stands at 27,12
    // before the trashed house's door until Bill gives the S.S.TICKET; then GUARD1 stands aside at 28,12 and the way
    // through the house to the thief's yard is open.
    auto ThiefWay = [](FGameTestContext& Test, bool bTicket, int32& OutSteps)
    {
        AfterPokedex(Test);
        for (const TCHAR* F : {TEXT("EVENT_BEAT_BROCK"), TEXT("EVENT_BEAT_CERULEAN_RIVAL"), TEXT("EVENT_MET_BILL")}) Progress(Test)->Set(F);
        Progress(Test)->Set(TEXT("EVENT_GOT_SS_TICKET"), bTicket);
        Progress(Test)->Set(TEXT("EVENT_BEAT_CERULEAN_ROCKET_THIEF"), false);
        PYRScripts::Refresh(Test.World);
        // People who stand still block the way; walkers step aside.
        TSet<FIntPoint> People;
        for (const TWeakObjectPtr<APYRPerson>& PW : Field(Test)->People) if (PW.IsValid() && PW->IsShown() && !PW->bWalker) People.Add(PW->Tile);
        for (const FIntPoint Next : {FIntPoint(30, 7), FIntPoint(30, 9)})   // the thief's trigger tiles
        {
            TArray<QA::FStep> Path;
            if (QA::Plan(Test, QA::K(Test, TEXT("CERULEAN_CITY"), 19, 18), QA::K(Test, TEXT("CERULEAN_CITY"), Next.X, Next.Y), Path, People)) { OutSteps = Path.Num(); return true; }
        }
        return false;
    };
    Own(TEXT("story-003"), 90, [ThiefWay](FGameTestContext& Test)
    {
        M4_READY();
        int32 Steps = 0;
        if (ThiefWay(Test, false, Steps)) M4_FAIL(TEXT("before the S.S.TICKET the thief's yard is open (%d steps): Yellow's GUARD2 is missing"), Steps);
        if (!ThiefWay(Test, true, Steps)) M4_FAIL(TEXT("with the S.S.TICKET there is still no way to the thief"));
        M4_PASS(TEXT("GUARD2 holds the trashed house until the S.S.TICKET; then the thief is %d steps from the Pokemon Center"), Steps);
    });
    Own(TEXT("story-004"), 90, [ThiefWay](FGameTestContext& Test)
    {
        M4_READY();
        int32 Steps = 0;
        if (!ThiefWay(Test, true, Steps)) M4_FAIL(TEXT("with the S.S.TICKET there is no way to the thief"));
        if (!Shown(Test, TEXT("CERULEAN_CITY"), TEXT("CERULEANCITY_ROCKET"))) M4_FAIL(TEXT("no Rocket thief before he is beaten"));
        if (Shown(Test, TEXT("CERULEAN_CITY"), TEXT("CERULEANCITY_GUARD2")) || !Shown(Test, TEXT("CERULEAN_CITY"), TEXT("CERULEANCITY_GUARD1")))
            M4_FAIL(TEXT("with the S.S.TICKET Yellow has GUARD1 aside and GUARD2 gone"));
        Progress(Test)->Set(TEXT("EVENT_BEAT_CERULEAN_ROCKET_THIEF"));
        PYRScripts::Refresh(Test.World);
        if (Shown(Test, TEXT("CERULEAN_CITY"), TEXT("CERULEANCITY_ROCKET"))) M4_FAIL(TEXT("the Rocket is still there after he is beaten"));
        M4_PASS(TEXT("after the S.S.TICKET the thief's beat can happen (%d steps away); beaten, he is gone"), Steps);
    });

    // ---- text
    struct FT { const TCHAR* Id; const TCHAR* Label; const TCHAR* Want; };
    static const FT Texts[] = {
        {TEXT("text-002"), TEXT("AfterTrade1Text"), TEXT("Isn't my old\n{str:wInGameTradeReceiveMonName} great?")},
        {TEXT("text-004"), TEXT("BillsHousePrintBillSSTicketText.SSTicketReceivedText"), TEXT("<PLAYER> received\nan {str:wStringBuffer}!")},
        {TEXT("text-005"), TEXT("FoundItemText"), TEXT("<PLAYER> found\n{str:wStringBuffer}!")},
        {TEXT("text-006"), TEXT("GotMapText"), TEXT("<PLAYER> got a\n{str:wStringBuffer}!")},
        {TEXT("text-007"), TEXT("MtMoonB2FReceivedFossilText.Text"), TEXT("<PLAYER> got the\n{str:wStringBuffer}!")},
        {TEXT("text-008"), TEXT("OaksLabReceivedText"), TEXT("<PLAYER> received\na {str:wNameBuffer}!")},
        {TEXT("text-009"), TEXT("PokemartTellBuyPriceText"), TEXT("{str:wStringBuffer}?\nThat will be\n¥{num:hMoney}. OK?")},
        {TEXT("text-010"), TEXT("PokemartTellSellPriceText"), TEXT("I can pay you\n¥{num:hMoney} for that.")},
        {TEXT("text-011"), TEXT("Route1PrintYoungster1Text.GotPotionText"), TEXT("<PLAYER> got\n{str:wStringBuffer}!")},
        {TEXT("text-012"), TEXT("Route24CooltrainerM1Text.ReceivedNuggetText"), TEXT("<PLAYER> received\na {str:wStringBuffer}!")},
        {TEXT("text-013"), TEXT("TradedForText"), TEXT("<PLAYER> traded\n{str:wInGameTradeGiveMonName} for\n{str:wInGameTradeReceiveMonName}!")},
        {TEXT("text-014"), TEXT("WannaTrade1Text"), TEXT("I'm looking for\n{str:wInGameTradeGiveMonName}! Wanna\ftrade one for\n{str:wInGameTradeReceiveMonName}? ")},
        {TEXT("text-015"), TEXT("WrongMon1Text"), TEXT("What? That's not\n{str:wInGameTradeGiveMonName}!\fIf you get one,\ncome back here!")},
    };
    for (const FT& T : Texts) Own(T.Id, 60, [T](FGameTestContext& Test) { return TextIs(Test, T.Label, T.Want); });
    Own(TEXT("text-001"), 60, [](FGameTestContext& Test)
    {
        // The buy question with a 7-letter name and the dearest price keeps every line within 18 columns.
        M4_READY();
        const FString* Raw = Data(Test)->Text.Find(TEXT("PokemartTellBuyPriceText"));
        if (!Raw) M4_FAIL(TEXT("no PokemartTellBuyPriceText"));
        const FString Filled = Raw->Replace(TEXT("{str:wStringBuffer}"), TEXT("ANTIDOT")).Replace(TEXT("{num:hMoney}"), TEXT("999999"));
        TArray<FString> Lines;
        Filled.Replace(TEXT("\f"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), false);
        for (const FString& L : Lines) if (L.Len() > 18) M4_FAIL(TEXT("'%s' is %d columns"), *L, L.Len());
        M4_PASS(TEXT("%d lines, none over 18 columns: '%s'"), Lines.Num(), *Filled.ReplaceCharWithEscapedChar());
    });
    Own(TEXT("text-003"), 90, [](FGameTestContext& Test)
    {
        // The Bike Shop's menu as Yellow draws it: BICYCLE with its price, then CANCEL (BikeShopMenuText + the price).
        static int32 Step = 0;
        M4_READY();
        if (Step == 0)
        {
            AfterPokedex(Test);
            APYRPerson* Clerk = Person(Test, TEXT("BIKE_SHOP"), TEXT("BIKESHOP_CLERK"));
            if (!Clerk || !PlaceToTalk(Test, Clerk)) M4_FAIL(TEXT("no Bike Shop clerk"));
            Step = 1;
            return EGameTest::Running;
        }
        static double T0 = 0;
        if (Step == 1) { T0 = Test.Seconds; Step = 2; return EGameTest::Running; }
        if (Step == 2) { if (Test.Seconds - T0 < .6) return EGameTest::Running; PressE(Test); Step = 3; return EGameTest::Running; }
        const FString S = Menus(Test)->ScreenText();
        if (S.Contains(TEXT("BICYCLE")) && S.Contains(TEXT("CANCEL")))
        {
            if (!S.Contains(TEXT("CANCEL¥1000000"))) M4_FAIL(TEXT("the menu is not Yellow's: '%s'"), *S.ReplaceCharWithEscapedChar());
            M4_PASS(TEXT("BICYCLE / CANCEL¥1000000 as Yellow prints it"));
        }
        if (TopId(Test) == TEXT("text") && Test.Seconds - T0 > 1.0) { Menus(Test)->Press(EPYRKey::Confirm); T0 = Test.Seconds; }
        if (Test.Seconds > 30) M4_FAIL(TEXT("the bike menu never opened: '%s'"), *S.Left(120));
        return EGameTest::Running;
    });
    Own(TEXT("text-016"), 120, [](FGameTestContext& Test)
    {
        // The battle captions that were not Yellow's: Bide, paralysis, and a ball at a trainer's Pokemon.
        using namespace PYRTest;
        static PYRTest::FState St;
        PYR_READY();
        APYRBattleManager* Mgr = PYRTest::M(Test);
        if (St.Step == 0) { Fight(Test, {Mon(PYRData::Pikachu, 20, {PYRData::Bide, PYRData::ThunderWave})}, {Mon(PYRData::Rattata, 20)}, EPYRBattleKind::Trainer, FVector(0, 450, 0)); Mgr->CaptionLog.Reset(); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
        PYR_WAIT(1.2);
        APYRMonPawn* P = Mgr->GetPlayerMon();
        APYRMonPawn* F = Mgr->GetFoeMon();
        if (!P || !F) PYR_FAIL(TEXT("fighters missing"));
        static const TCHAR* Wants[] = {TEXT("Rattata's paralyzed! It may not attack!"), TEXT("The trainer blocked the BALL! Don't be a thief!"), TEXT("Pikachu is saving energy!")};
        if (St.I > 0)
        {
            const TCHAR* Want = Wants[St.I - 1];
            if (!CaptionSeen(Mgr, Want)) { if (Test.Seconds - St.T0 > 6) PYR_FAIL(TEXT("no '%s' (captions: %s)"), Want, *FString::Join(Mgr->CaptionLog, TEXT(" | "))); return EGameTest::Running; }
            St.Note += FString::Printf(TEXT("'%s' "), Want);
            if (St.I == 3) PYR_PASS(TEXT("Yellow's words: %s"), *St.Note);
        }
        Place(P, F->GetActorLocation() + FVector(0, -350, 0));
        Face(P, F);
        Mgr->CaptionLog.Reset();
        // Bide last: it holds the next moves while it stores energy.
        if (St.I == 0) Mgr->PressMove(1);
        else if (St.I == 1) { Mgr->ForcedBallDodgeRoll = .99f; Mgr->PressBall(); }
        else Mgr->PressMove(0);
        ++St.I;
        St.T0 = Test.Seconds - 1.2;
        return EGameTest::Running;
    });
    Own(TEXT("text-017"), 60, [](FGameTestContext& Test)
    {
        M4_READY();
        Party(Test, {{TEXT("pikachu"), 8}});
        Menus(Test)->CloseAll();
        PYRScreens::OpenStats(Test.World, 0);
        const FString S = DrawnWords(Test);
        if (!S.Contains(TEXT("ID№/")) || !S.Contains(TEXT("OT/")) || S.Contains(TEXT("IDNo"))) M4_FAIL(TEXT("the status page reads '%s'"), *S.ReplaceCharWithEscapedChar());
        M4_PASS(TEXT("the status page shows Yellow's ID№/ and OT/"));
    });
    Own(TEXT("text-018"), 60, [](FGameTestContext& Test)
    {
        M4_READY();
        Menus(Test)->CloseAll();
        PYRScreens::OpenOptions(Test.World);
        const FString S = DrawnWords(Test);
        for (const TCHAR* W : {TEXT("TEXT SPEED :"), TEXT("ANIMATION  :"), TEXT("BATTLESTYLE:"), TEXT("CANCEL")})
            if (!S.Contains(W)) M4_FAIL(TEXT("no '%s' on the options page: '%s'"), W, *S.ReplaceCharWithEscapedChar());
        if (S.Contains(TEXT("BATTLE ANIMATION")) || S.Contains(TEXT("BATTLE STYLE"))) M4_FAIL(TEXT("the old option words: '%s'"), *S.ReplaceCharWithEscapedChar());
        M4_PASS(TEXT("Yellow's option page: TEXT SPEED :, ANIMATION  :, BATTLESTYLE:"));
    });
    Own(TEXT("text-019"), 60, [](FGameTestContext& Test)
    {
        // The naming screen's test description never reaches the screen.
        M4_READY();
        Menus(Test)->CloseAll();
        PYRScreens::OpenNaming(Test.World, TEXT("YOUR NAME?"), TEXT("RED"), [](FString) {});
        const FString S = DrawnWords(Test);
        if (!S.Contains(TEXT("YOUR NAME?"))) M4_FAIL(TEXT("the naming screen draws '%s'"), *S.ReplaceCharWithEscapedChar());
        if (S.Contains(TEXT("cursor")) || S.Contains(TEXT("stage")) || S.Contains(TEXT("models"))) M4_FAIL(TEXT("debug words on screen: '%s'"), *S.ReplaceCharWithEscapedChar());
        M4_PASS(TEXT("the naming screen shows only Yellow's words"));
    });
    Own(TEXT("text-020"), 90, [](FGameTestContext& Test)
    {
        // needs-designer (Live Battle words): the swap wait the HUD's 'Swap in N s' counts down still runs after a swap.
        using namespace PYRTest;
        static PYRTest::FState St;
        PYR_READY();
        APYRBattleManager* Mgr = PYRTest::M(Test);
        if (St.Step == 0) { Fight(Test, {Mon(PYRData::Pikachu, 12), Mon(PYRData::Pidgey, 10)}, {Mon(PYRData::Rattata, 5)}); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
        PYR_WAIT(1.2);
        if (St.Step == 1) { Mgr->PressSwap(1); St.Step = 2; return EGameTest::Running; }
        const double Left = Mgr->SwapSecondsLeft();
        if (Left <= 0) PYR_FAIL(TEXT("no swap wait after a swap"));
        PYR_PASS(TEXT("after a swap the next is %.1f s away (the HUD's 'Swap in N s')"), Left);
    });
    Own(TEXT("text-021"), 90, [](FGameTestContext& Test)
    {
        // needs-designer (Live Battle words): a second swap before the wait is over says 'Not yet... N s!'.
        using namespace PYRTest;
        static PYRTest::FState St;
        PYR_READY();
        APYRBattleManager* Mgr = PYRTest::M(Test);
        if (St.Step == 0) { Fight(Test, {Mon(PYRData::Pikachu, 12), Mon(PYRData::Pidgey, 10)}, {Mon(PYRData::Rattata, 30)}); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
        PYR_WAIT(1.2);
        if (St.Step == 1) { Mgr->PressSwap(1); St.Step = 2; St.T0 = Test.Seconds - .2; return EGameTest::Running; }
        if (St.Step == 2) { Mgr->PressSwap(0); St.Step = 3; St.T0 = Test.Seconds - 1.2; return EGameTest::Running; }
        if (!Mgr->Shout.Contains(TEXT("Not yet"))) { if (Test.Seconds - St.T0 > 5) PYR_FAIL(TEXT("no 'Not yet' (shout '%s')"), *Mgr->Shout); return EGameTest::Running; }
        PYR_PASS(TEXT("Red: '%s'"), *Mgr->Shout.ReplaceCharWithEscapedChar());
    });
    Own(TEXT("text-022"), 90, [](FGameTestContext& Test)
    {
        // needs-designer (Live Battle words): the line Red shouts when his Pokemon falls in the Cerulean pool.
        using namespace PYRTest;
        static PYRTest::FState St;
        PYR_READY();
        APYRBattleManager* Mgr = PYRTest::M(Test);
        if (St.Step == 0) { FightIn(Test, EArena::Pool, {Mon(PYRData::Pikachu, 12)}, {Mon(PYRData::Staryu, 10)}, EPYRBattleKind::Gym); St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
        PYR_WAIT(1.5);
        APYRMonPawn* P = Mgr->GetPlayerMon();
        if (!P) PYR_FAIL(TEXT("no Pikachu"));
        // Walked off its platform toward the water.
        if (P->WaterFalls == 0) { P->Steer(FVector(-1, 0, 0)); if (Test.Seconds - St.T0 > 8) PYR_FAIL(TEXT("Pikachu never fell in the water")); return EGameTest::Running; }
        if (!Mgr->Shout.Contains(TEXT("Get out of the water!"))) PYR_FAIL(TEXT("fell in; Red shouts '%s'"), *Mgr->Shout);
        PYR_PASS(TEXT("Pikachu fell in the water; Red: '%s'"), *Mgr->Shout.ReplaceCharWithEscapedChar());
    });
    Own(TEXT("text-023"), 150, [](FGameTestContext& Test)
    {
        // A catch with a full party: Yellow's ItemUseBallText08, '<MON> was transferred to someone's PC!'.
        static int32 Step = 0;
        static double T0 = 0;
        M4_READY();
        UPYREncounters* En = UPYREncounters::Get(Test.World);
        APYRBattleManager* Mgr = APYRBattleManager::Get(Test.World);
        if (Step == 0)
        {
            AfterPokedex(Test);
            Party(Test, {{TEXT("pikachu"), 20}, {TEXT("pidgey"), 5}, {TEXT("pidgey"), 5}, {TEXT("pidgey"), 5}, {TEXT("pidgey"), 5}, {TEXT("pidgey"), 5}});
            State(Test)->SetItemCount(UPYRTrainerState::PokeBall, 5);
            Progress(Test)->Set(TEXT("EVENT_MET_BILL"), false);
            Place(Test, TEXT("ROUTE_1"), 9, 26, EYDir::Up);
            Quiet(true, true);
            En->BeginWild(TEXT("rattata"), 3);
            Step = 1;
            T0 = Test.Seconds;
            return EGameTest::Running;
        }
        if (Step == 1)
        {
            if (En->GetPhase() != EPYREncounterPhase::Fight || !Mgr || !Mgr->GetFoeMon() || Test.Seconds - T0 < 2.5) { if (Test.Seconds - T0 > 15) M4_FAIL(TEXT("no fight")); return EGameTest::Running; }
            Mgr->bFoePassive = true;
            Mgr->ForcedBallDodgeRoll = .99f;
            Mgr->ForcedCatchRoll = 0.f;
            Mgr->GetFoeMon()->StunnedUntil = Test.World->GetTimeSeconds() + 10;
            Mgr->PressBall();
            Step = 2;
            T0 = Test.Seconds;
            return EGameTest::Running;
        }
        if (SaidContains(Test, TEXT("transferred to")))
        {
            const FString Want = PYRSysText::Get(TEXT("ItemUseBallText08"), {{TEXT("wBoxMonNicks"), PYRScreens::SpeciesName(TEXT("rattata"))}});
            if (!Said(Test).Contains(Want.Replace(TEXT("\f"), TEXT("\n"))) && !Said(Test).Contains(Want)) M4_FAIL(TEXT("said '%s', Yellow '%s'"), *Said(Test).ReplaceCharWithEscapedChar(), *Want.ReplaceCharWithEscapedChar());
            M4_PASS(TEXT("'%s'"), *Want.ReplaceCharWithEscapedChar());
        }
        // Through the catch: text on, NO to a nickname.
        if (TopId(Test) == TEXT("choice")) { Menus(Test)->Top()->Cursor = 1; Menus(Test)->Press(EPYRKey::Confirm); }
        else if (TopId(Test) == TEXT("text")) Menus(Test)->Press(EPYRKey::Confirm);
        if (Test.Seconds - T0 > 40) M4_FAIL(TEXT("no transfer line (said '%s')"), *Said(Test).Left(160));
        return EGameTest::Running;
    });
    Own(TEXT("text-024"), 120, [](FGameTestContext& Test) { static FWarpState St; M4_READY(); return BlockedWarp(Test, St, TEXT("DIGLETTS_CAVE_ROUTE_2"), 4, 4, TEXT("")); });
    Own(TEXT("text-025"), 120, [](FGameTestContext& Test)
    {
        // The walking super nerd in Cerulean answers when Red faces him.
        static int32 Step = 0;
        static double T0 = 0;
        M4_READY();
        APYRPerson* P = Shown(Test, TEXT("CERULEAN_CITY"), TEXT("CERULEANCITY_SUPER_NERD1"));
        if (!P) M4_FAIL(TEXT("no super nerd"));
        if (Step == 0)
        {
            AfterPokedex(Test);
            Quiet(true, true);
            P->bFrozen = true;
            if (!PlaceToTalk(Test, P)) M4_FAIL(TEXT("no tile beside him"));
            Step = 1;
            T0 = Test.Seconds;
            return EGameTest::Running;
        }
        if (Step == 1) { if (Test.Seconds - T0 < .5) return EGameTest::Running; PressE(Test); Step = 2; T0 = Test.Seconds; return EGameTest::Running; }
        if (TopId(Test) == TEXT("text")) M4_PASS(TEXT("he says '%s'"), *FirstLineOf(Menus(Test)->ScreenText()));
        if (Test.Seconds - T0 > 3) M4_FAIL(TEXT("E in front of him opened nothing"));
        return EGameTest::Running;
    });

    // ---- the visual tour
    Shot(TEXT("visual-001"), TEXT("map-REDS_HOUSE_2F"), [](FGameTestContext& T, FString& W)
    {
        const FYMap* M = Data(T)->Map(TEXT("REDS_HOUSE_2F"));
        for (const FYHiddenEvent& H : M->HiddenEvents)
            if (H.Kind == TEXT("PrintRedSNESText"))
            {
                const FName Snes = Furn(T, M->Id, H.X, H.Y), Tv = Furn(T, M->Id, H.X, H.Y - 1);
                W = FString::Printf(TEXT("SNES tile %d,%d: %s; the tile north: %s"), H.X, H.Y, *Snes.ToString(), *Tv.ToString());
                return Snes == TEXT("snes") && Tv == TEXT("tv");
            }
        W = TEXT("no SNES in the map");
        return false;
    });
    Shot(TEXT("visual-002"), TEXT("map-BLUES_HOUSE"), MapCheck(TEXT("BLUES_HOUSE"), ChairsAndTables));
    Shot(TEXT("visual-003"), TEXT("map-OAKS_LAB"), Both(EveryLook(TEXT("OAKS_LAB"), TEXT('h'), TEXT("ball_stand")), [](FGameTestContext& T, FString& W)
    {
        for (int32 Y = 2; Y <= 4; ++Y) for (int32 X = 8; X <= 10; ++X) if (Furn(T, TEXT("OAKS_LAB"), X, Y) == TEXT("ball_stand")) { W += FString::Printf(TEXT("a Poke Ball at %d,%d; "), X, Y); return true; }
        W += TEXT("no Poke Ball near 9,3; ");
        return false;
    }));
    Shot(TEXT("visual-004"), TEXT("map-VIRIDIAN_POKECENTER"), MapCheck(TEXT("VIRIDIAN_POKECENTER"), Centre));
    Shot(TEXT("visual-005"), TEXT("map-VIRIDIAN_SCHOOL_HOUSE"), Both(EveryLook(TEXT("VIRIDIAN_SCHOOL_HOUSE"), TEXT('l'), TEXT("blackboard")), MapCheck(TEXT("VIRIDIAN_SCHOOL_HOUSE"), PeopleModels)));
    Shot(TEXT("visual-006"), TEXT("map-VIRIDIAN_NICKNAME_HOUSE"), MapCheck(TEXT("VIRIDIAN_NICKNAME_HOUSE"), ChairsAndTables));
    Shot(TEXT("visual-007"), TEXT("map-VIRIDIAN_GYM"), RedCheck());
    Shot(TEXT("visual-008"), TEXT("map-VIRIDIAN_GYM"), [](FGameTestContext& T, FString& W)
    {
        // Nothing squashed at the top of the screen: no screen or box is open over the gym.
        const FString S = Menus(T)->ScreenText();
        W = FString::Printf(TEXT("screens: '%s'"), *S.Left(80));
        return !Menus(T)->Top().IsValid();
    });
    Shot(TEXT("visual-009"), TEXT("map-ROUTE_22"), [](FGameTestContext& T, FString& W) { return NoVoidNear(T, TEXT("ROUTE_22"), 12, W); });
    Shot(TEXT("visual-010"), TEXT("map-ROUTE_22_GATE"), [](FGameTestContext& T, FString& W)
    {
        const FYMap* M = Data(T)->Map(TEXT("ROUTE_22_GATE"));
        int32 N = 0;
        for (const FYWarp& Wp : M->Warps)
            if (Wp.Y == 0)
            {
                ++N;
                if (Furn(T, M->Id, Wp.X, 0) != TEXT("doorway")) { W = FString::Printf(TEXT("no doorway in the north wall at %d,0"), Wp.X); return false; }
            }
        W = FString::Printf(TEXT("%d doorways in the north wall; "), N);
        // Yellow's League gate: four statues by a carpet up the middle, no counters.
        for (const FIntPoint S : {FIntPoint(3, 1), FIntPoint(6, 1), FIntPoint(3, 7), FIntPoint(6, 7)})
            if (Furn(T, M->Id, S.X, S.Y) != TEXT("statue")) { W += FString::Printf(TEXT("no statue at %d,%d (%s)"), S.X, S.Y, *Furn(T, M->Id, S.X, S.Y).ToString()); return false; }
        for (int32 Y = 1; Y < M->Height; ++Y)
            for (const int32 X : {4, 5})
                if (Furn(T, M->Id, X, Y) != TEXT("carpet")) { W += FString::Printf(TEXT("no carpet at %d,%d"), X, Y); return false; }
        if (CountFurn(T, M->Id, TEXT("counter"))) { W += FString::Printf(TEXT("%d counters"), CountFurn(T, M->Id, TEXT("counter"))); return false; }
        W += TEXT("four statues, the carpet, no counters");
        return N > 0;
    });
    Shot(TEXT("visual-011"), TEXT("map-ROUTE_2_TRADE_HOUSE"), MapCheck(TEXT("ROUTE_2_TRADE_HOUSE"), ChairsAndTables));
    Shot(TEXT("visual-012"), TEXT("map-ROUTE_2_TRADE_HOUSE"), MapCheck(TEXT("ROUTE_2_TRADE_HOUSE"), PeopleModels));
    Shot(TEXT("visual-013"), TEXT("map-VIRIDIAN_FOREST_SOUTH_GATE"), Both(MapCheck(TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), RoomOnly),
        Both(NoFurn(TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), {TEXT("tree"), TEXT("bush"), TEXT("tall_grass")}), EveryLook(TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), TEXT('h'), TEXT("plant")))));
    Shot(TEXT("visual-014"), TEXT("map-VIRIDIAN_FOREST"), RedCheck());
    Shot(TEXT("visual-015"), TEXT("map-VIRIDIAN_FOREST"), [](FGameTestContext& T, FString& W)
    {
        TArray<FString> Drawn;
        for (const FName& M : Data(T)->MapOrder) if (M != TEXT("VIRIDIAN_FOREST") && World(T)->IsMapDrawn(M)) Drawn.Add(M.ToString());
        if (AActor* Sc = World(T)->GetSceneryActor()) if (!Sc->IsHidden()) Drawn.Add(TEXT("the Kanto scenery"));
        W = Drawn.Num() ? TEXT("drawn past the forest: ") + FString::Join(Drawn, TEXT(", ")) : TEXT("only the forest and its tree border drawn");
        return Drawn.Num() == 0 && World(T)->GetSceneryActorFor(TEXT("VIRIDIAN_FOREST")) != nullptr;
    });
    Shot(TEXT("visual-016"), TEXT("map-VIRIDIAN_FOREST_NORTH_GATE"), Both(NoFurn(TEXT("VIRIDIAN_FOREST_NORTH_GATE"), {TEXT("tree"), TEXT("bush"), TEXT("tall_grass")}),
        EveryLook(TEXT("VIRIDIAN_FOREST_NORTH_GATE"), TEXT('h'), TEXT("plant"))));
    Shot(TEXT("visual-017"), TEXT("map-VIRIDIAN_FOREST_NORTH_GATE"), MapCheck(TEXT("VIRIDIAN_FOREST_NORTH_GATE"), RoomOnly));
    Shot(TEXT("visual-018"), TEXT("map-MUSEUM_2F"), [](FGameTestContext& T, FString& W) { return CameraClear(T, Red(T), 250.f, W) && InView(T, Red(T), TEXT("Red"), W); });
    Shot(TEXT("visual-019"), TEXT("map-PEWTER_GYM"), MapCheck(TEXT("PEWTER_GYM"), RoomOnly));
    Shot(TEXT("visual-020"), TEXT("map-PEWTER_NIDORAN_HOUSE"), MapCheck(TEXT("PEWTER_NIDORAN_HOUSE"), ChairsAndTables));
    Shot(TEXT("visual-021"), TEXT("map-PEWTER_NIDORAN_HOUSE"), MapCheck(TEXT("PEWTER_NIDORAN_HOUSE"), PeopleModels));
    Shot(TEXT("visual-022"), TEXT("map-PEWTER_SPEECH_HOUSE"), MapCheck(TEXT("PEWTER_SPEECH_HOUSE"), ChairsAndTables));
    Shot(TEXT("visual-023"), TEXT("map-PEWTER_POKECENTER"), MapCheck(TEXT("PEWTER_POKECENTER"), Centre));
    Shot(TEXT("visual-024"), TEXT("map-MT_MOON_POKECENTER"), Both(MapCheck(TEXT("MT_MOON_POKECENTER"), Centre), ClearAroundRed()));
    Shot(TEXT("visual-025"), TEXT("map-MT_MOON_B1F"), Both(MapCheck(TEXT("MT_MOON_B1F"), SolidDrawn), FurnAt(TEXT("MT_MOON_B1F"), 14, 15, TEXT("cave_wall"))));
    Shot(TEXT("visual-026"), TEXT("map-MT_MOON_B1F"), RedCheck());
    Shot(TEXT("visual-027"), TEXT("map-MT_MOON_B2F"), Both(MapCheck(TEXT("MT_MOON_B2F"), SolidDrawn), EveryLook(TEXT("MT_MOON_B2F"), TEXT('u'), TEXT("ladder"))));
    Shot(TEXT("visual-028"), TEXT("map-CERULEAN_CITY"), RedCheck());
    Shot(TEXT("visual-029"), TEXT("map-CERULEAN_CITY"), RedCheck());
    Shot(TEXT("visual-030"), TEXT("map-CERULEAN_TRASHED_HOUSE"), MapCheck(TEXT("CERULEAN_TRASHED_HOUSE"), ChairsAndTables));
    Shot(TEXT("visual-031"), TEXT("map-CERULEAN_MELANIES_HOUSE"), MapCheck(TEXT("CERULEAN_MELANIES_HOUSE"), ChairsAndTables));
    Shot(TEXT("visual-032"), TEXT("map-CERULEAN_POKECENTER"), MapCheck(TEXT("CERULEAN_POKECENTER"), Centre));
    Shot(TEXT("visual-033"), TEXT("map-CERULEAN_GYM"), MapCheck(TEXT("CERULEAN_GYM"), RoomOnly));
    Shot(TEXT("visual-034"), TEXT("map-BIKE_SHOP"), [](FGameTestContext& T, FString& W)
    {
        // Every bicycle tile carries its bike (a two-tile bike notes its second tile as the bike's half).
        const FYMap* M = Data(T)->Map(TEXT("BIKE_SHOP"));
        int32 N = 0;
        for (int32 Y = 0; M && Y < M->Height; ++Y)
            for (int32 X = 0; X < M->Width; ++X)
                if (M->Look(X, Y) == TEXT('l'))
                {
                    ++N;
                    const FName F = Furn(T, M->Id, X, Y);
                    if (F != TEXT("bike") && F != TEXT("bike_half")) { W = FString::Printf(TEXT("bicycle tile %d,%d drawn as %s"), X, Y, *F.ToString()); return false; }
                }
        W = FString::Printf(TEXT("%d bicycle tiles, each with its bike"), N);
        return N > 0;
    });
    Shot(TEXT("visual-035"), TEXT("map-ROUTE_24"), Both(MapCheck(TEXT("ROUTE_24"), LooksMatch), WaterCheck(TEXT("ROUTE_24"))));
    Shot(TEXT("visual-036"), TEXT("map-ROUTE_25"), Both(RedCheck(), [](FGameTestContext& T, FString& W)
    {
        for (int32 X = 0; X < 20; ++X) if (Furn(T, TEXT("ROUTE_25"), X, 10) != TEXT("fence")) { W += FString::Printf(TEXT("; 10th row at %d: %s"), X, *Furn(T, TEXT("ROUTE_25"), X, 10).ToString()); return false; }
        W += TEXT("; row 10 is Yellow's line of posts");
        return true;
    }));
    Shot(TEXT("visual-037"), TEXT("map-BILLS_HOUSE"), [](FGameTestContext& T, FString& W)
    {
        const int32 Shelves = CountFurn(T, TEXT("BILLS_HOUSE"), TEXT("bookshelf")), Pods = CountFurn(T, TEXT("BILLS_HOUSE"), TEXT("pod"));
        W = FString::Printf(TEXT("%d bookshelves, %d Cell Separator pods"), Shelves, Pods);
        return Shelves == 0 && Pods == 2;
    });
    Shot(TEXT("visual-038"), TEXT("beat-naming-player"), StageShows(TEXT("red")));
    Shot(TEXT("visual-039"), TEXT("beat-naming-rival"), StageShows(TEXT("rival")));
    Shot(TEXT("visual-040"), TEXT("beat-oak-intercept"), [](FGameTestContext& T, FString& W)
    {
        APYRPerson* Oak = Shown(T, TEXT("PALLET_TOWN"), TEXT("PALLETTOWN_OAK"));
        const bool bA = CameraClear(T, Oak, 150.f, W);
        const bool bB = InView(T, Oak, TEXT("Oak"), W);
        const bool bC = InView(T, Red(T), TEXT("Red"), W);
        if (bA && bB && bC) W += TEXT("Oak and Red both in the picture, the camera clear of Oak");
        return bA && bB && bC;
    });
    Shot(TEXT("visual-041"), TEXT("beat-lab-rival-battle"), [](FGameTestContext& T, FString& W)
    {
        const APYRBattleStage* S = UPYREncounters::Get(T.World)->GetCurrentStage();
        W = FString::Printf(TEXT("the fight's stage: %s"), S ? APYRBattleStage::Name(S->Kind) : TEXT("none"));
        return S && S->Kind == EPYRStageKind::Indoor && UPYREncounters::StageForMap(Data(T)->Map(TEXT("OAKS_LAB"))) == EPYRStageKind::Indoor;
    });
    Shot(TEXT("visual-042"), TEXT("beat-route22-rival"), MapCheck(TEXT("ROUTE_22"), LooksMatch));
    Shot(TEXT("visual-043"), TEXT("beat-brock"), MapCheck(TEXT("PEWTER_GYM"), RoomOnly));
    Shot(TEXT("visual-044"), TEXT("beat-brock-battle"), [](FGameTestContext& T, FString& W)
    {
        const AActor* Brock = UPYREncounters::Get(T.World)->GetTrainerVisual();
        W = Brock ? FString::Printf(TEXT("Brock stands across the stage (%s)"), *Brock->GetName()) : FString(TEXT("no trainer across the stage"));
        return Brock != nullptr && UPYREncounters::Get(T.World)->GetFight().TrainerModel == TEXT("brock");
    });
    Shot(TEXT("visual-045"), TEXT("beat-mt-moon-rockets"), [](FGameTestContext& T, FString& W)
    {
        const bool bJ = InView(T, Shown(T, TEXT("MT_MOON_B2F"), TEXT("MTMOONB2F_JESSIE")), TEXT("Jessie"), W);
        const bool bJa = InView(T, Shown(T, TEXT("MT_MOON_B2F"), TEXT("MTMOONB2F_JAMES")), TEXT("James"), W);
        const bool bR = InView(T, Red(T), TEXT("Red"), W);
        if (bJ && bJa && bR) W += TEXT("Jessie, James and Red in the picture");
        return bJ && bJa && bR;
    });
    Shot(TEXT("visual-046"), TEXT("beat-mt-moon-rockets"), Both(MapCheck(TEXT("MT_MOON_B2F"), LooksMatch), MapCheck(TEXT("MT_MOON_B2F"), SolidDrawn)));
    Shot(TEXT("visual-047"), TEXT("beat-cerulean-rival"), [](FGameTestContext& T, FString& W)
    {
        const APYRPerson* Gary = Shown(T, TEXT("CERULEAN_CITY"), TEXT("CERULEANCITY_RIVAL"));
        if (!Gary) { W = TEXT("no Gary"); return false; }
        const float D = FVector::Dist2D(Gary->GetActorLocation(), Red(T)->GetActorLocation());
        W = FString::Printf(TEXT("Gary stands %.0f cm from Red (tiles %d,%d and %d,%d)"), D, Gary->Tile.X, Gary->Tile.Y, Field(T)->KantoTile.X, Field(T)->KantoTile.Y);
        return D >= 80.f && Gary->Tile != Field(T)->KantoTile && NotBehindRed(TEXT("CERULEAN_CITY"), TEXT("CERULEANCITY_RIVAL"))(T, W) && WaterCheck(TEXT("CERULEAN_CITY"))(T, W);
    });
    Shot(TEXT("visual-048"), TEXT("beat-bill"), Both(MapCheck(TEXT("BILLS_HOUSE"), LooksMatch), NoFurn(TEXT("BILLS_HOUSE"), {TEXT("bookshelf")})));
    Shot(TEXT("visual-049"), TEXT("beat-ui-party"), FontOk());
    Shot(TEXT("visual-050"), TEXT("beat-ui-party"), [](FGameTestContext& T, FString& W)
    {
        UYellowImages* Images = UYellowImages::Get(T.World);
        TArray<FString> Got;
        for (const FPYRMon& Mon : State(T)->Party)
        {
            const TSharedPtr<FJsonObject>* O = nullptr;
            FString Icon;
            if (!Data(T)->SpeciesJson.IsValid()) break;
            for (const auto& Pair : Data(T)->SpeciesJson->Values)
                if (YKey::Species(FString(*Pair.Key)) == Mon.Species && Pair.Value->TryGetObject(O) && O) (*O)->TryGetStringField(TEXT("menu_icon"), Icon);
            if (Icon.IsEmpty() || !Images || !Images->Load(FString::Printf(TEXT("icons/%s.png"), *Icon))) { W = FString::Printf(TEXT("no party icon for %s"), *Mon.Species.ToString()); return false; }
            Got.Add(Mon.Species.ToString() + TEXT(":") + Icon);
        }
        W = TEXT("party icons: ") + FString::Join(Got, TEXT(", "));
        return Got.Num() > 0;
    });
    Shot(TEXT("visual-051"), TEXT("beat-ui-card"), [](FGameTestContext& T, FString& W)
    {
        UYellowImages* Images = UYellowImages::Get(T.World);
        TArray<FString> Missing;
        for (const TCHAR* F : {TEXT("trainercard/red.png"), TEXT("trainercard/face_0.png"), TEXT("trainercard/badge_0.png"), TEXT("trainercard/face_7.png"), TEXT("trainercard/badge_7.png")})
            if (!Images || !Images->Load(F)) Missing.Add(F);
        W = Missing.Num() ? TEXT("missing ") + FString::Join(Missing, TEXT(", ")) : FString(TEXT("Red's picture, the leaders' faces and the badges load"));
        return Missing.Num() == 0 && TopId(T) == TEXT("trainer-card");
    });
    Shot(TEXT("visual-052"), TEXT("map-REDS_HOUSE_1F"), [](FGameTestContext& T, FString& W)
    {
        const FYMap* M = Data(T)->Map(TEXT("REDS_HOUSE_1F"));
        for (const FYSign& S : M->Signs)
            if (S.Text.EndsWith(TEXT("_TV")))
            {
                W = FString::Printf(TEXT("TV sign %d,%d: %s"), S.X, S.Y, *Furn(T, M->Id, S.X, S.Y).ToString());
                return Furn(T, M->Id, S.X, S.Y) == TEXT("tv");
            }
        W = TEXT("no TV sign");
        return false;
    });
    Shot(TEXT("visual-053"), TEXT("map-VIRIDIAN_SCHOOL_HOUSE"), Both(MapCheck(TEXT("VIRIDIAN_SCHOOL_HOUSE"), PeopleModels), EveryLook(TEXT("VIRIDIAN_SCHOOL_HOUSE"), TEXT('l'), TEXT("blackboard"))));
    Shot(TEXT("visual-054"), TEXT("map-VIRIDIAN_NICKNAME_HOUSE"), MapCheck(TEXT("VIRIDIAN_NICKNAME_HOUSE"), PeopleModels));
    Shot(TEXT("visual-055"), TEXT("map-VIRIDIAN_GYM"), MapCheck(TEXT("VIRIDIAN_GYM"), SolidDrawn));
    Shot(TEXT("visual-056"), TEXT("map-VIRIDIAN_GYM"), MapCheck(TEXT("VIRIDIAN_GYM"), RoomOnly));
    Shot(TEXT("visual-057"), TEXT("map-VIRIDIAN_FOREST"), MapCheck(TEXT("VIRIDIAN_FOREST"), LooksMatch));
    Shot(TEXT("visual-058"), TEXT("map-MUSEUM_2F"), [](FGameTestContext& T, FString& W) { return CameraClear(T, Red(T), 250.f, W) && InView(T, Red(T), TEXT("Red"), W); });
    Shot(TEXT("visual-059"), TEXT("map-MT_MOON_B2F"), RedCheck());
    Shot(TEXT("visual-060"), TEXT("map-CERULEAN_CITY"), Both(RedCheck(), [](FGameTestContext& T, FString& W)
    {
        // Cerulean's Pokemon Center stands against Melanie's house: two buildings, the Center one of its own kind.
        const FYMap* M = Data(T)->Map(TEXT("CERULEAN_CITY"));
        const FYBuilding* Centre = nullptr;
        const FYBuilding* House = nullptr;
        for (const FYBuilding& B : World(T)->GetBuildings())
        {
            if (B.Map != M->Id) continue;
            if (B.Leads == TEXT("CERULEAN_POKECENTER")) Centre = &B;
            if (B.Leads == TEXT("CERULEAN_MELANIES_HOUSE")) House = &B;
        }
        if (!Centre || !House || Centre == House) { W += TEXT("the Pokemon Center and Melanie's house are not two buildings"); return false; }
        const FIntRect C(Centre->Rect.Min - M->Origin, Centre->Rect.Max - M->Origin);
        W += FString::Printf(TEXT("the Pokemon Center is its own building (%d,%d to %d,%d), kind %d"), C.Min.X, C.Min.Y, C.Max.X, C.Max.Y, int32(Centre->Kind));
        return Centre->Kind == FYBuilding::EKind::Centre && C.Width() == 4 && C.Min.X == 18;
    }));
    Shot(TEXT("visual-061"), TEXT("map-CERULEAN_BADGE_HOUSE"), MapCheck(TEXT("CERULEAN_BADGE_HOUSE"), ChairsAndTables));
    Shot(TEXT("visual-062"), TEXT("map-ROUTE_25"), RedCheck());
    Shot(TEXT("visual-063"), TEXT("beat-oaks-lab-pikachu"), MapCheck(TEXT("OAKS_LAB"), PeopleModels));
    Shot(TEXT("visual-064"), TEXT("beat-pokedex"), MapCheck(TEXT("OAKS_LAB"), PeopleModels));
    Shot(TEXT("visual-065"), TEXT("beat-route22-rival"), Seen(TEXT("ROUTE_22"), TEXT("ROUTE22_RIVAL1")));
    Shot(TEXT("visual-066"), TEXT("beat-route22-rival"), [](FGameTestContext& T, FString& W) { return NoVoidNear(T, TEXT("ROUTE_22"), 12, W); });
    Shot(TEXT("visual-067"), TEXT("beat-bill"), Seen(TEXT("BILLS_HOUSE"), TEXT("BILLSHOUSE_BILL_POKEMON")));
    Shot(TEXT("visual-068"), TEXT("beat-ui-bag"), [](FGameTestContext& T, FString& W)
    {
        // needs-designer: Yellow's list puts each quantity on the row under the item (home/list_menu.asm prints it at
        // SCREEN_WIDTH + 8, one row down); the bag keeps that layout.
        const FString S = DrawnWords(T);
        TArray<FString> Lines;
        S.ParseIntoArray(Lines, TEXT("\n"), true);
        for (int32 I = 1; I < Lines.Num(); ++I)
            if (Lines[I].TrimStart().StartsWith(TEXT("×")) && !Lines[I - 1].Contains(TEXT("×")))
            {
                W = FString::Printf(TEXT("'%s' then '%s', as the Game Boy"), *Lines[I - 1].TrimEnd(), *Lines[I].TrimEnd());
                return true;
            }
        W = FString::Printf(TEXT("no quantity under an item: '%s'"), *S.ReplaceCharWithEscapedChar().Left(200));
        return false;
    });
    // ---- m4q integration: the harness's second visual run
    Shot(TEXT("visual-069"), TEXT("map-ROUTE_22"), WaterCheck(TEXT("ROUTE_22")));
    Shot(TEXT("visual-071"), TEXT("map-BIKE_SHOP"), Both(ClearAroundRed(), [](FGameTestContext& T, FString& W)
    {
        // One bicycle per two tiles, as the Game Boy draws them: 10 bikes over the 18 bicycle tiles (two stand alone).
        const int32 Bikes = CountFurn(T, TEXT("BIKE_SHOP"), TEXT("bike")), Halves = CountFurn(T, TEXT("BIKE_SHOP"), TEXT("bike_half"));
        W += FString::Printf(TEXT("%d bikes (%d tiles under a bike's second half)"), Bikes, Halves);
        return Bikes == 10 && Halves == 8;
    }));
    Shot(TEXT("visual-072"), TEXT("map-ROUTE_24"), WaterCheck(TEXT("ROUTE_24")));
    Shot(TEXT("visual-073"), TEXT("beat-pokedex"), [](FGameTestContext& T, FString& W)
        { return NotBehindRed(TEXT("OAKS_LAB"), Shown(T, TEXT("OAKS_LAB"), TEXT("OAKSLAB_OAK2")) ? TEXT("OAKSLAB_OAK2") : TEXT("OAKSLAB_OAK1"))(T, W); });
    Shot(TEXT("visual-074"), TEXT("beat-brock"), NotBehindRed(TEXT("PEWTER_GYM"), TEXT("PEWTERGYM_BROCK")));
    Shot(TEXT("visual-075"), TEXT("beat-ui-start"), [](FGameTestContext& T, FString& W)
    {
        // South of Pallet Town Kanto is Route 21's sea, all 90 rows of it, not a meadow past a narrow strip.
        const FYMap* M = Data(T)->Map(TEXT("PALLET_TOWN"));
        int32 Water = 0, Deep = 0;
        for (int32 Y = M->Height; Y < M->Height + 90; ++Y)
            for (int32 X = 0; X < M->Width; ++X)
                if (World(T)->SceneryLookAt(M->Origin + FIntPoint(X, Y)) == TEXT('W')) { ++Water; if (Y >= M->Height + 40) ++Deep; }
        W = FString::Printf(TEXT("%d sea tiles south of Pallet Town, %d of them 40 or more rows out; "), Water, Deep);
        return Deep > 200 && QA::WaterShows(T, TEXT("PALLET_TOWN"), W);
    });
    return List;
}

EGameTest RunBug(FGameTestContext& Test)
{
    static const FBug* Bug = nullptr;
    if (!Bug)
    {
        FString Name;
        FParse::Value(FCommandLine::Get(), TEXT("PYRAutoTest="), Name);
        for (const FBug& B : Bugs()) if (FString(TEXT("QA_")) + FString(B.Id).Replace(TEXT("-"), TEXT("_")) == Name) Bug = &B;
        if (!Bug) M4_FAIL(TEXT("no QA bug test named %s"), *Name);
    }
    if (Bug->Shot) return QA::ShotCheck(Test, Bug->Shot, Bug->Check);
    return Bug->Run(Test);
}

struct FRegisterBugs
{
    FRegisterBugs()
    {
        for (const FBug& B : Bugs()) GameTest::Register(*(FString(TEXT("QA_")) + FString(B.Id).Replace(TEXT("-"), TEXT("_"))), &RunBug, B.Timeout);
    }
} GRegisterBugs;

// ------------------------------------------------------------------------------------------------ m4r: TourLayout_<MAP>
// One test per map the visual tour flagged (the planner's Q-062).
// Each stages the tour's shot of the map and checks, where the picture is taken:
// - the collision against Yellow's tiles, physically: a trace down every tile hits the map's collision exactly where
//   pokeyellow's collision makes the tile solid (walls, counters, water, trees, ledges), and nowhere else;
// - the 3D blocks against Yellow's looks: what is built on each tile is what the Game Boy draws there (LooksMatch,
//   SolidDrawn);
// - the map's own fix (a flat back wall, the PC at the counter's end, the shuttle, the pipe, the bridge planks, low
//   cave ridges, the gym's low maze, the forest path in view...).

// Collision follows pokeyellow's tiles on every tile of the map (traced through the built collision).
bool CollisionMatches(FGameTestContext& Test, FName Map, FString& Why)
{
    const FYMap* M = Data(Test)->Map(Map);
    AYellowWorld* W = World(Test);
    if (!M) { Why += TEXT("no map; "); return false; }
    TArray<FString> Bad;
    int32 Solid = 0;
    for (int32 Y = 0; Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X)
        {
            const bool bWant = YTile::IsSolid(M->Tile(X, Y));
            const bool bGot = W->ProbeBlocked(M->Origin + FIntPoint(X, Y));
            Solid += bWant ? 1 : 0;
            if (bWant != bGot) Bad.Add(FString::Printf(TEXT("%d,%d '%c' %s"), X, Y, M->Tile(X, Y), bGot ? TEXT("blocked") : TEXT("open")));
        }
    if (Bad.Num())
    {
        if (Bad.Num() > 8) { const int32 More = Bad.Num() - 8; Bad.SetNum(8); Bad.Add(FString::Printf(TEXT("%d more"), More)); }
        Why += TEXT("collision unlike Yellow's: ") + FString::Join(Bad, TEXT(", ")) + TEXT("; ");
        return false;
    }
    Why += FString::Printf(TEXT("collision on all %d tiles as Yellow's (%d solid); "), M->Width * M->Height, Solid);
    return true;
}

// A room's top row is one flat back wall: every solid top-row tile (not an exit, not a bookshelf's top) is wall.
bool FlatBackWall(FGameTestContext& Test, FName Map, FString& Why)
{
    const FYMap* M = Data(Test)->Map(Map);
    TArray<FString> Bad;
    for (int32 X = 0; X < M->Width; ++X)
    {
        if (!YTile::IsSolid(M->Tile(X, 0)) || M->WarpAt(X, 0) != INDEX_NONE) continue;
        if (M->Look(X, 0) == TEXT('t') && M->Look(X, 1) == TEXT('b')) continue;   // a bookshelf's top
        const FName F = Furn(Test, Map, X, 0);
        if (F != TEXT("wall") && F != TEXT("window") && F != TEXT("poster") && F != TEXT("blackboard") && F != TEXT("shuttle"))
            Bad.Add(FString::Printf(TEXT("%d,0 '%c' is %s"), X, M->Look(X, 0), *F.ToString()));
    }
    if (Bad.Num()) { Why += TEXT("the back wall steps: ") + FString::Join(Bad, TEXT(", ")) + TEXT("; "); return false; }
    Why += TEXT("the back wall is one flat face; ");
    return true;
}

// Inside a room nothing above the walls shows: the rays through the picture's top corners and top middle leave the
// room through a wall below its top (RoomWallTop), never through the black lid.
bool NoBlackAbove(FGameTestContext& Test, FName Map, FString& Why)
{
    const FYMap* M = Data(Test)->Map(Map);
    FVector E;
    FRotator R;
    float Fov = 90.f;
    if (!Eye(Test, E, R, Fov)) { Why += TEXT("no camera; "); return false; }
    const FVector Off = World(Test)->GetOffset();
    const FVector Lo = Off + FVector(M->Origin.X * 100.f, M->Origin.Y * 100.f, 0.f), Hi = Lo + FVector(M->Width * 100.f, M->Height * 100.f, 0.f);
    const float Top = 880.f;
    const float H = FMath::Tan(FMath::DegreesToRadians(Fov * .5f)), V = H * 9.f / 16.f;
    const FRotationMatrix Rm(R);
    for (const float Sx : {-1.f, 0.f, 1.f})
    {
        const FVector Dir = (Rm.GetUnitAxis(EAxis::X) + Rm.GetUnitAxis(EAxis::Y) * Sx * H + Rm.GetUnitAxis(EAxis::Z) * V).GetSafeNormal();
        // Where the ray leaves the room's box (walls round it, 880 cm high).
        float T = 1e9f;
        if (Dir.X > 1e-4f) T = FMath::Min(T, (Hi.X - E.X) / Dir.X); else if (Dir.X < -1e-4f) T = FMath::Min(T, (Lo.X - E.X) / Dir.X);
        if (Dir.Y > 1e-4f) T = FMath::Min(T, (Hi.Y - E.Y) / Dir.Y); else if (Dir.Y < -1e-4f) T = FMath::Min(T, (Lo.Y - E.Y) / Dir.Y);
        const float Z = E.Z + Dir.Z * T - Off.Z;
        if (Dir.Z > 0.f && Z > Top) { Why += FString::Printf(TEXT("the picture's top %s sees over the walls (at %.0f cm); "), Sx < 0 ? TEXT("left") : Sx > 0 ? TEXT("right") : TEXT("middle"), Z); return false; }
    }
    Why += TEXT("the picture's top meets the walls, not the dark above; ");
    return true;
}

// Two people a tile apart stand well apart across the picture (not one in front of the other, not touching).
FCheck Apart(const TCHAR* Map, const TCHAR* Id, float MinCm)
{
    return [Map, Id, MinCm](FGameTestContext& T, FString& W)
    {
        const APYRPerson* P = Shown(T, Map, Id);
        FVector E;
        FRotator R;
        float Fov;
        if (!P || !Eye(T, E, R, Fov)) { W += FString::Printf(TEXT("%s or the camera missing; "), Id); return false; }
        // Their offset along the picture's horizontal (the camera's right).
        const float Across = FMath::Abs(FVector::DotProduct(FRotationMatrix(R).GetUnitAxis(EAxis::Y), P->GetActorLocation() - Red(T)->GetActorLocation()));
        W += FString::Printf(TEXT("%s and Red %.0f cm apart across the view; "), Id, Across);
        return Across >= MinCm;
    };
}

FCheck All(std::initializer_list<FCheck> Checks)
{
    TArray<FCheck> List(Checks);
    return [List](FGameTestContext& T, FString& W) { bool bOk = true; for (const FCheck& C : List) bOk &= C(T, W); return bOk; };
}

FCheck Layout(const TCHAR* Map) { return Both(MapCheck(Map, CollisionMatches), MapCheck(Map, LooksMatch)); }

struct FLayout { const TCHAR* Map; const TCHAR* Shot; FCheck Check; };

TArray<FLayout>& Layouts()
{
    static TArray<FLayout> List;
    if (List.Num()) return List;
    auto Room = [](const TCHAR* Map) { return All({Layout(Map), MapCheck(Map, SolidDrawn), MapCheck(Map, FlatBackWall), MapCheck(Map, NoBlackAbove), MapCheck(Map, RoomOnly)}); };
    // visual-001: Red's room, one flat north wall; the PC and the desk in the row below against it.
    List.Add({TEXT("REDS_HOUSE_2F"), TEXT("map-REDS_HOUSE_2F"), All({Room(TEXT("REDS_HOUSE_2F")), FurnAt(TEXT("REDS_HOUSE_2F"), 0, 1, TEXT("pc")),
        FurnAt(TEXT("REDS_HOUSE_2F"), 1, 1, TEXT("table")), FurnAt(TEXT("REDS_HOUSE_2F"), 2, 1, TEXT("table")), FurnAt(TEXT("REDS_HOUSE_2F"), 7, 1, TEXT("stairs"))})});
    // visual-004, -032: the Centres: the PC at the east end of the counter (13,3); the Cable Club man stands in the
    // counter's gap (11,2), as in Yellow's object data.
    for (const TPair<const TCHAR*, const TCHAR*>& CS : {TPair<const TCHAR*, const TCHAR*>(TEXT("VIRIDIAN_POKECENTER"), TEXT("map-VIRIDIAN_POKECENTER")),
             TPair<const TCHAR*, const TCHAR*>(TEXT("CERULEAN_POKECENTER"), TEXT("map-CERULEAN_POKECENTER"))})
    {
        const TCHAR* C = CS.Key;
        const FString Id = FString(C).Replace(TEXT("_"), TEXT("")) + TEXT("_LINK_RECEPTIONIST");
        List.Add({C, CS.Value,
            All({Room(C), MapCheck(C, Centre), FurnAt(C, 13, 3, TEXT("pc")), [C, Id](FGameTestContext& T, FString& W)
            {
                const APYRPerson* P = Shown(T, C, *Id);
                const FYMap* M = Data(T)->Map(C);
                if (!P) { W += Id + TEXT(" missing; "); return false; }
                const FIntPoint L = P->Tile - M->Origin;
                const bool bGap = !YTile::IsSolid(M->Tile(L.X, L.Y)) && YTile::IsSolid(M->Tile(L.X - 1, L.Y)) && YTile::IsSolid(M->Tile(L.X + 1, L.Y));
                W += FString::Printf(TEXT("the receptionist on %d,%d%s; "), L.X, L.Y, bGap ? TEXT(", the counter's gap") : TEXT(""));
                return bGap && Furn(T, C, L.X, L.Y) == NAME_None;
            }})});
    }
    // visual-025, -027, -082: the caves, rock ridges of one height, the void and the light walkways as their own.
    List.Add({TEXT("MT_MOON_B1F"), TEXT("map-MT_MOON_B1F"), All({Layout(TEXT("MT_MOON_B1F")), MapCheck(TEXT("MT_MOON_B1F"), SolidDrawn),
        EveryLook(TEXT("MT_MOON_B1F"), TEXT('V'), TEXT("cave_wall")), EveryLook(TEXT("MT_MOON_B1F"), TEXT('u'), TEXT("ladder"))})});
    List.Add({TEXT("MT_MOON_B2F"), TEXT("map-MT_MOON_B2F"), All({Layout(TEXT("MT_MOON_B2F")), MapCheck(TEXT("MT_MOON_B2F"), SolidDrawn),
        EveryLook(TEXT("MT_MOON_B2F"), TEXT('K'), TEXT("cave_wall")), EveryLook(TEXT("MT_MOON_B2F"), TEXT('u'), TEXT("ladder"))})});
    List.Add({TEXT("DIGLETTS_CAVE_ROUTE_2"), TEXT("map-DIGLETTS_CAVE_ROUTE_2"), All({Layout(TEXT("DIGLETTS_CAVE_ROUTE_2")), MapCheck(TEXT("DIGLETTS_CAVE_ROUTE_2"), SolidDrawn),
        FurnAt(TEXT("DIGLETTS_CAVE_ROUTE_2"), 6, 4, TEXT("cave_wall")), FurnAt(TEXT("DIGLETTS_CAVE_ROUTE_2"), 4, 4, TEXT("ladder"))})});
    // visual-030, -079: the Trashed House, the man by the bookshelves (2,1) as in Yellow; the broken back wall (3,0) shows
    // the yard; stools Red stands on.
    List.Add({TEXT("CERULEAN_TRASHED_HOUSE"), TEXT("map-CERULEAN_TRASHED_HOUSE"), All({Room(TEXT("CERULEAN_TRASHED_HOUSE")),
        MapCheck(TEXT("CERULEAN_TRASHED_HOUSE"), ChairsAndTables), [](FGameTestContext& T, FString& W)
        {
            const FYMap* M = Data(T)->Map(TEXT("CERULEAN_TRASHED_HOUSE"));
            const APYRPerson* Guru = Shown(T, TEXT("CERULEAN_TRASHED_HOUSE"), TEXT("CERULEANTRASHEDHOUSE_FISHING_GURU"));
            const bool bYard = World(T)->OutdoorDoorways.Contains(M->Origin + FIntPoint(3, 0));
            const bool bGuru = Guru && Guru->Tile == M->Origin + FIntPoint(2, 1);
            W += FString::Printf(TEXT("the hole at 3,0 %s; the man %s; "), bYard ? TEXT("shows the yard") : TEXT("shows nothing"), bGuru ? TEXT("on 2,1") : TEXT("not on Yellow's 2,1"));
            return bYard && bGuru;
        }})});
    // visual-035, -036, -084: Route 24's Nugget Bridge and Route 25's bridge are planks over water; Route 25's row of
    // posts ends at column 37 where the water starts.
    List.Add({TEXT("ROUTE_24"), TEXT("map-ROUTE_24"), All({Layout(TEXT("ROUTE_24")), EveryLook(TEXT("ROUTE_24"), TEXT('H'), TEXT("bridge")), WaterCheck(TEXT("ROUTE_24")),
        FurnAt(TEXT("ROUTE_24"), 10, 17, TEXT("bridge"))})});
    List.Add({TEXT("ROUTE_25"), TEXT("map-ROUTE_25"), All({Layout(TEXT("ROUTE_25")), EveryLook(TEXT("ROUTE_25"), TEXT('H'), TEXT("bridge")),
        FurnAt(TEXT("ROUTE_25"), 37, 10, TEXT("fence")), [](FGameTestContext& T, FString& W)
        {
            const bool bEnd = Furn(T, TEXT("ROUTE_25"), 38, 10) != TEXT("fence") && Data(T)->Map(TEXT("ROUTE_25"))->Look(38, 10) == TEXT('W');
            W += bEnd ? TEXT("the posts end at 37,10 and the water starts; ") : TEXT("the posts run on past 37,10; ");
            return bEnd;
        }, MapCheck(TEXT("ROUTE_25"), [](FGameTestContext& T, FName Map, FString& W) { return NoVoidNear(T, Map, 6, W); })})});
    // visual-037: Bill's pods joined by the pipe along the back wall (no pillar between them); the PC on its desk.
    List.Add({TEXT("BILLS_HOUSE"), TEXT("map-BILLS_HOUSE"), All({Layout(TEXT("BILLS_HOUSE")), MapCheck(TEXT("BILLS_HOUSE"), NoBlackAbove), FurnAt(TEXT("BILLS_HOUSE"), 3, 1, TEXT("pipe")),
        FurnAt(TEXT("BILLS_HOUSE"), 4, 1, TEXT("pipe")), FurnAt(TEXT("BILLS_HOUSE"), 1, 1, TEXT("pod")), FurnAt(TEXT("BILLS_HOUSE"), 6, 1, TEXT("pod")),
        FurnAt(TEXT("BILLS_HOUSE"), 1, 4, TEXT("pc")), FurnAt(TEXT("BILLS_HOUSE"), 2, 4, TEXT("table"))})});
    // visual-041: Oak's lab (the fight's round stage is the Live Battle look, for the designer).
    List.Add({TEXT("OAKS_LAB"), TEXT("map-OAKS_LAB"), All({Room(TEXT("OAKS_LAB"))})});
    // visual-042, -070, -078: the routes' rock as raised ground with a low rim, Yellow's tiles and collision.
    List.Add({TEXT("ROUTE_22"), TEXT("beat-route22-rival"), All({Layout(TEXT("ROUTE_22")), WaterCheck(TEXT("ROUTE_22"))})});
    List.Add({TEXT("ROUTE_3"), TEXT("map-ROUTE_3"), All({Layout(TEXT("ROUTE_3")), RedCheck()})});
    List.Add({TEXT("ROUTE_4"), TEXT("map-ROUTE_4"), All({Layout(TEXT("ROUTE_4")), RedCheck(),
        MapCheck(TEXT("ROUTE_4"), [](FGameTestContext& T, FName Map, FString& W) { return NoVoidNear(T, Map, 6, W); })})});
    // visual-055: Viridian Gym's maze walls are low (the camera sees Red over the wall behind him).
    List.Add({TEXT("VIRIDIAN_GYM"), TEXT("map-VIRIDIAN_GYM"), All({Layout(TEXT("VIRIDIAN_GYM")), MapCheck(TEXT("VIRIDIAN_GYM"), SolidDrawn), RedCheck()})});
    // visual-057: Viridian Forest, the path west of Red in view (the trees of the row behind him, beside the camera's
    // line, are faded).
    List.Add({TEXT("VIRIDIAN_FOREST"), TEXT("map-VIRIDIAN_FOREST"), All({Layout(TEXT("VIRIDIAN_FOREST")), RedCheck(), [](FGameTestContext& T, FString& W)
        {
            const FIntPoint R = Field(T)->CurrentTile;
            const FYMap* M = Data(T)->Map(TEXT("VIRIDIAN_FOREST"));
            int32 Hidden = 0, Trees = 0;
            for (const int32 Dx : {-1, 1})
                if (M->Look(R.X + Dx, R.Y + 1) == TEXT('T')) { ++Trees; Hidden += World(T)->IsOccluderHiddenAt(M->Origin + FIntPoint(R.X + Dx, R.Y + 1)) ? 1 : 0; }
            W += FString::Printf(TEXT("%d of the %d trees beside the camera's line behind Red faded; "), Hidden, Trees);
            return Hidden == Trees;
        }})});
    // visual-058, -083: Museum 2F, the space shuttle on its railed platform; no black above the walls.
    List.Add({TEXT("MUSEUM_2F"), TEXT("map-MUSEUM_2F"), All({Room(TEXT("MUSEUM_2F")), FurnAt(TEXT("MUSEUM_2F"), 10, 1, TEXT("shuttle")), FurnAt(TEXT("MUSEUM_2F"), 11, 1, TEXT("shuttle")),
        FurnAt(TEXT("MUSEUM_2F"), 12, 1, TEXT("shuttle")), FurnAt(TEXT("MUSEUM_2F"), 11, 2, TEXT("shuttle_platform"))})});
    // visual-068, -080, -085: Pallet Town; Oak's lab, right beside Red in the tour's picture, is drawn (the camera's
    // lane once faded it as if it were in the way).
    List.Add({TEXT("PALLET_TOWN"), TEXT("map-PALLET_TOWN"), All({Layout(TEXT("PALLET_TOWN")), WaterCheck(TEXT("PALLET_TOWN")), RedCheck(), [](FGameTestContext& T, FString& W)
        {
            const FYMap* M = Data(T)->Map(TEXT("PALLET_TOWN"));
            AYellowWorld* Wd = World(T);
            int32 Hidden = 0, Lab = 0;
            for (int32 Y = 8; Y <= 11; ++Y)
                for (int32 X = 10; X <= 15; ++X)
                    if (M->Look(X, Y) == TEXT('R') || M->Look(X, Y) == TEXT('B') || M->Look(X, Y) == TEXT('n') || M->Look(X, Y) == TEXT('D'))
                    { ++Lab; Hidden += Wd->IsOccluderHiddenAt(M->Origin + FIntPoint(X, Y)) ? 1 : 0; }
            W += FString::Printf(TEXT("Oak's lab: %d tiles, %d hidden; "), Lab, Hidden);
            return Lab > 0 && Hidden == 0;
        }})});
    // visual-071: the Bike Shop's back wall is one strip behind the bikes (no wall block standing out between them).
    List.Add({TEXT("BIKE_SHOP"), TEXT("map-BIKE_SHOP"), All({Layout(TEXT("BIKE_SHOP")), MapCheck(TEXT("BIKE_SHOP"), SolidDrawn), MapCheck(TEXT("BIKE_SHOP"), NoBlackAbove),
        [](FGameTestContext& T, FString& W)
        {
            const bool bThin = World(T)->ThinBackRooms.Contains(FName(TEXT("BIKE_SHOP")));
            const int32 Bikes = CountFurn(T, TEXT("BIKE_SHOP"), TEXT("bike")), Halves = CountFurn(T, TEXT("BIKE_SHOP"), TEXT("bike_half"));
            W += FString::Printf(TEXT("back wall %s; %d bikes (%d second halves); "), bThin ? TEXT("a strip behind the bikes") : TEXT("full tiles"), Bikes, Halves);
            return bThin && Bikes == 10 && Halves == 8;
        }})});
    // visual-077, -086: the Marts' back wall: four glass display cases between the SALE shelves, the SALE signs over
    // them; island shelves low enough to see over.
    for (const TPair<const TCHAR*, const TCHAR*>& MS : {TPair<const TCHAR*, const TCHAR*>(TEXT("VIRIDIAN_MART"), TEXT("map-VIRIDIAN_MART")),
             TPair<const TCHAR*, const TCHAR*>(TEXT("PEWTER_MART"), TEXT("map-PEWTER_MART"))})
    {
        const TCHAR* Mart = MS.Key;
        List.Add({Mart, MS.Value, All({Layout(Mart), MapCheck(Mart, SolidDrawn), MapCheck(Mart, NoBlackAbove), MapCheck(Mart, RoomOnly), [Mart](FGameTestContext& T, FString& W)
            {
                int32 Cases = 0, Sale = 0, Signs = 0;
                for (int32 X = 0; X < 8; ++X)
                {
                    Cases += Furn(T, Mart, X, 1) == TEXT("mart_case") && Furn(T, Mart, X, 0) == TEXT("mart_case") ? 1 : 0;
                    Sale += Furn(T, Mart, X, 1) == TEXT("mart_shelf") && Furn(T, Mart, X, 0) == TEXT("poster") ? 1 : 0;
                    Signs += Furn(T, Mart, X, 0) == TEXT("poster") ? 1 : 0;
                }
                const bool bThin = World(T)->ThinBackRooms.Contains(FName(Mart));
                W += FString::Printf(TEXT("%d display cases, %d SALE shelves under %d signs, back wall %s; "), Cases, Sale, Signs, bThin ? TEXT("a strip") : TEXT("full tiles"));
                return Cases == 4 && Sale == 4 && bThin;
            }})});
    }
    // visual-089, -090: the gates' doorways are dark, as the Game Boy draws them (only a hole broken in a back wall
    // shows the outdoors); the Route 2 gate's walker keeps to row 4, the open floor between the counters.
    for (const TPair<const TCHAR*, const TCHAR*>& GS : {TPair<const TCHAR*, const TCHAR*>(TEXT("ROUTE_2_GATE"), TEXT("map-ROUTE_2_GATE")),
             TPair<const TCHAR*, const TCHAR*>(TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), TEXT("map-VIRIDIAN_FOREST_SOUTH_GATE"))})
    {
        const TCHAR* Gate = GS.Key;
        List.Add({Gate, GS.Value, All({Layout(Gate), MapCheck(Gate, SolidDrawn), MapCheck(Gate, NoBlackAbove), MapCheck(Gate, RoomOnly), [Gate](FGameTestContext& T, FString& W)
            {
                const FYMap* M = Data(T)->Map(Gate);
                int32 Doors = 0, Lit = 0;
                for (int32 X = 0; X < M->Width; ++X)
                    if (M->WarpAt(X, 0) != INDEX_NONE) { ++Doors; Lit += World(T)->OutdoorDoorways.Contains(M->Origin + FIntPoint(X, 0)) ? 1 : 0; }
                W += FString::Printf(TEXT("%d north doorway tiles, %d showing a backdrop; "), Doors, Lit);
                return Doors > 0 && Lit == 0;
            }})});
    }
    // visual-088: Viridian, its fence rows where Yellow has them (row 13 east of the road).
    List.Add({TEXT("VIRIDIAN_CITY"), TEXT("map-VIRIDIAN_CITY"), All({Layout(TEXT("VIRIDIAN_CITY")), WaterCheck(TEXT("VIRIDIAN_CITY")), FurnAt(TEXT("VIRIDIAN_CITY"), 25, 13, TEXT("fence"))})});
    // visual-023: Pewter's Centre, like the others: the PC at the counter's east end, the receptionist in the gap.
    List.Add({TEXT("PEWTER_POKECENTER"), TEXT("map-PEWTER_POKECENTER"), All({Layout(TEXT("PEWTER_POKECENTER")), MapCheck(TEXT("PEWTER_POKECENTER"), SolidDrawn),
        MapCheck(TEXT("PEWTER_POKECENTER"), FlatBackWall), MapCheck(TEXT("PEWTER_POKECENTER"), NoBlackAbove), MapCheck(TEXT("PEWTER_POKECENTER"), Centre),
        FurnAt(TEXT("PEWTER_POKECENTER"), 13, 3, TEXT("pc"))})});
    // visual-047: Cerulean, the bridge north of town is planks over the water.
    List.Add({TEXT("CERULEAN_CITY"), TEXT("map-CERULEAN_CITY"), All({Layout(TEXT("CERULEAN_CITY")), WaterCheck(TEXT("CERULEAN_CITY")), EveryLook(TEXT("CERULEAN_CITY"), TEXT('H'), TEXT("bridge")),
        FurnAt(TEXT("CERULEAN_CITY"), 20, 0, TEXT("bridge"))})});
    // visual-074, -081: Pewter Gym, Brock and Red well apart while they talk (the conversation camera looks across).
    List.Add({TEXT("PEWTER_GYM"), TEXT("beat-brock"), All({Layout(TEXT("PEWTER_GYM")), NotBehindRed(TEXT("PEWTER_GYM"), TEXT("PEWTERGYM_BROCK")),
        Apart(TEXT("PEWTER_GYM"), TEXT("PEWTERGYM_BROCK"), 75.f)})});
    return List;
}

EGameTest RunLayout(FGameTestContext& Test)
{
    static const FLayout* L = nullptr;
    if (!L)
    {
        FString Name;
        FParse::Value(FCommandLine::Get(), TEXT("PYRAutoTest="), Name);
        for (const FLayout& X : Layouts()) if (FString(TEXT("TourLayout_")) + X.Map == Name) L = &X;
        if (!L) M4_FAIL(TEXT("no tour layout test named %s"), *Name);
    }
    return QA::ShotCheck(Test, L->Shot, L->Check);
}

struct FRegisterLayouts
{
    FRegisterLayouts() { for (const FLayout& L : Layouts()) GameTest::Register(*(FString(TEXT("TourLayout_")) + L.Map), &RunLayout, 240); }
} GRegisterLayouts;
}
