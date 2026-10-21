#include "Tests/QAKit.h"

#include "Battle/BattleTestKit.h"
#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRMoveLearning.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "InputCoreTypes.h"
#include "Stage/PYREncounters.h"

namespace QA
{
TSharedRef<FJsonObject> Rec(const TCHAR* Kind)
{
    TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("kind"), Kind);
    return O;
}

void Log(const TSharedRef<FJsonObject>& Record)
{
    FString Out;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Record, Writer);
    UE_LOG(LogYellow, Display, TEXT("QA_REC %s"), *Out);
}

TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& In)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FString& S : In) Out.Add(MakeShared<FJsonValueString>(S));
    return Out;
}

TArray<TSharedPtr<FJsonValue>> Point(FIntPoint P)
{
    return {MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y)};
}

bool Strict() { return FParse::Param(FCommandLine::Get(), TEXT("QAStrict")); }

FString ShotDir()
{
    FString Dir;
    if (!FParse::Value(FCommandLine::Get(), TEXT("QAShotDir="), Dir)) Dir = FPaths::ProjectSavedDir() / TEXT("QAShots");
    return Dir;
}

bool Idle(FGameTestContext& Test)
{
    const UPYRMenus* Me = M4::Menus(Test);
    const UPYRScripts* Sc = UPYRScripts::Get(Test.World);
    const UPYREncounters* En = UPYREncounters::Get(Test.World);
    const UPYRField* F = M4::Field(Test);
    return (!Me || !Me->AnyOpen()) && (!Sc || !Sc->IsRunning()) && (!En || !En->IsBusy()) && (!F || (!F->IsWarping() && !F->IsLocked()));
}

void WinFight(FGameTestContext& Test)
{
    APYRBattleManager* BM = APYRBattleManager::Get(Test.World);
    if (!BM || !BM->IsBattleActive() || BM->IsEnding()) return;
    BM->bNoRandom = true;
    APYRMonPawn* Foe = BM->GetFoeMon();
    APYRMonPawn* Mine = BM->GetPlayerMon();
    if (Foe && Mine && !Foe->bFainted && Foe->Hp > 0) PYRTest::KnockOut(BM);
}

bool Drive(FGameTestContext& Test, bool bYes)
{
    WinFight(Test);
    UPYRMenus* Me = M4::Menus(Test);
    if (Me && M4::TopId(Test) == TEXT("naming"))
    {
        // Keep the offered name: Enter, as a player at the keyboard ends a name (ED).
        Me->HandleKey(EKeys::Enter);
        return true;
    }
    // m4t: a fifth move's prompt (in a fight or after one): keep the old four, as the automated runs always have.
    if (PYRMoveLearning::StepKeep(Test.World)) return true;
    const bool bOpen = M4::Advance(Test, bYes);
    return bOpen || !Idle(Test);
}

// ------------------------------------------------------------------------------------------------ paths
FIntPoint K(FGameTestContext& Test, FName Map, int32 X, int32 Y)
{
    const FYellowDataSet* D = M4::Data(Test);
    const FYMap* M = D ? D->Map(Map) : nullptr;
    return M ? M->Origin + FIntPoint(X, Y) : FIntPoint(X, Y);
}

FString TileText(FGameTestContext& Test, FIntPoint Kanto)
{
    const AYellowWorld* W = M4::World(Test);
    const FYMap* M = W ? W->MapAtKanto(Kanto) : nullptr;
    return M ? FString::Printf(TEXT("%s %d,%d"), *M->Id.ToString(), Kanto.X - M->Origin.X, Kanto.Y - M->Origin.Y) : FString::Printf(TEXT("off-map %d,%d"), Kanto.X, Kanto.Y);
}

FString PersonAt(FGameTestContext& Test, FIntPoint Kanto)
{
    if (const UPYRField* F = M4::Field(Test))
    {
        for (const TWeakObjectPtr<APYRPerson>& P : F->People)
            if (P.IsValid() && P->IsShown() && P->Tile == Kanto) return P->Id;
        for (const TWeakObjectPtr<APYRItemBall>& B : F->Balls)
            if (B.IsValid() && B->Tile == Kanto) return B->Key();
    }
    return FString();
}

TSet<FIntPoint> PeopleTiles(FGameTestContext& Test)
{
    TSet<FIntPoint> Out;
    if (const UPYRField* F = M4::Field(Test))
    {
        for (const TWeakObjectPtr<APYRPerson>& P : F->People)
            if (P.IsValid() && P->IsShown()) Out.Add(P->Tile);
        // Item balls stand on their tile like a person until taken (they block, as on the Game Boy).
        for (const TWeakObjectPtr<APYRItemBall>& B : F->Balls)
            if (B.IsValid() && !B->IsHidden()) Out.Add(B->Tile);
    }
    return Out;
}

namespace
{
struct FNode { FIntPoint Tile; int32 Cost; };
struct FVia { FIntPoint From; FIntPoint WarpTile; bool bWarp = false; bool bHop = false; };

// Where stepping onto a warp tile lands (the Game Boy's step off an outdoor door included); false when it leads nowhere.
bool Landing(const AYellowWorld* W, const UPYRField* F, const FYMap* M, int32 WarpIndex, FIntPoint& Out, FName& OutMap)
{
    FName To;
    int32 ToWarp = 0;
    if (!W->WarpDestination(M->Id, WarpIndex, F ? F->LastOutdoor : NAME_None, To, ToWarp)) return false;
    const FYMap* Dest = W->GetData()->Map(To);
    if (!Dest || !Dest->Warps.IsValidIndex(ToWarp)) return false;
    const FYWarp& Wp = Dest->Warps[ToWarp];
    FIntPoint L(Wp.X, Wp.Y);
    if (Dest->bOutdoor && Dest->Contains(L.X, L.Y + 1) && !YTile::IsSolid(Dest->Tile(L.X, L.Y + 1)) && Dest->WarpAt(L.X, L.Y + 1) == INDEX_NONE) L.Y += 1;
    Out = Dest->Origin + L;
    OutMap = To;
    return true;
}
}

bool Plan(FGameTestContext& Test, FIntPoint From, FIntPoint To, TArray<FStep>& Out, const TSet<FIntPoint>& Avoid, const TSet<FName>* AllowedMaps)
{
    Out.Reset();
    const AYellowWorld* W = M4::World(Test);
    const UPYRField* F = M4::Field(Test);
    if (!W) return false;
    if (From == To) return true;
    TMap<FIntPoint, int32> Best;
    TMap<FIntPoint, FVia> Came;
    TArray<FNode> Open;
    auto Less = [](const FNode& A, const FNode& B) { return A.Cost < B.Cost; };
    Open.HeapPush({From, 0}, Less);
    Best.Add(From, 0);
    bool bFound = false;
    int32 Expanded = 0;
    while (Open.Num() && Expanded < 400000)
    {
        FNode Cur;
        Open.HeapPop(Cur, Less);
        if (Cur.Cost > Best.FindRef(Cur.Tile)) continue;
        if (Cur.Tile == To) { bFound = true; break; }
        ++Expanded;
        for (const EYDir Dir : {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right})
        {
            const FIntPoint N = Cur.Tile + YDir::Delta(Dir);
            const FYMap* NM = W->MapAtKanto(N);
            if (!NM || W->IsEdgeBlocked(Cur.Tile, Dir)) continue;
            const TCHAR T = W->TileAt(N);
            FIntPoint Next = N;
            FVia Via{Cur.Tile, FIntPoint::ZeroValue, false, false};
            int32 Cost = 1;
            if (YTile::IsLedge(T))
            {
                if (YTile::LedgeDir(T) != Dir) continue;
                const FIntPoint L = N + YDir::Delta(Dir);
                if (!W->MapAtKanto(L) || YTile::IsSolid(W->TileAt(L)) || Avoid.Contains(L)) continue;
                Next = L;
                Via.bHop = true;
                Cost = 2;
            }
            else if (YTile::IsSolid(T) || (Avoid.Contains(N) && N != To)) continue;
            else if (N != To)
            {
                const int32 Warp = NM->WarpAt(N.X - NM->Origin.X, N.Y - NM->Origin.Y);
                if (Warp != INDEX_NONE)
                {
                    FIntPoint Land;
                    FName LandMap;
                    if (Landing(W, F, NM, Warp, Land, LandMap))
                    {
                        if (AllowedMaps && !AllowedMaps->Contains(LandMap)) continue;
                        Next = Land;
                        Via.bWarp = true;
                        Via.WarpTile = N;
                        Cost = 25;
                    }
                }
            }
            const int32 NewCost = Cur.Cost + Cost;
            const int32* Old = Best.Find(Next);
            if (Old && *Old <= NewCost) continue;
            Best.Add(Next, NewCost);
            Came.Add(Next, Via);
            Open.HeapPush({Next, NewCost}, Less);
        }
    }
    if (!bFound) return false;
    TArray<FStep> Rev;
    for (FIntPoint At = To; At != From;)
    {
        const FVia& V = Came[At];
        FStep S;
        S.Tile = At;
        S.bHop = V.bHop;
        Rev.Add(S);
        if (V.bWarp) { FStep WS; WS.Tile = V.WarpTile; WS.bWarp = true; Rev.Add(WS); }
        At = V.From;
    }
    for (int32 I = Rev.Num() - 1; I >= 0; --I) Out.Add(Rev[I]);
    return true;
}

bool FWalker::Replan(FGameTestContext& Test, bool bAvoidPeople)
{
    const UPYRField* F = M4::Field(Test);
    if (!F) return false;
    TSet<FIntPoint> Avoid;
    if (bAvoidPeople) Avoid = PeopleTiles(Test);
    Avoid.Remove(F->KantoTile);
    At = 0;
    ++Replans;
    if (!Plan(Test, F->KantoTile, Goal, Path, Avoid, bUseAllowed ? &Allowed : nullptr))
    {
        // People in the way on every route: try once more through them (they wander off).
        if (!bAvoidPeople || !Plan(Test, F->KantoTile, Goal, Path, TSet<FIntPoint>(), bUseAllowed ? &Allowed : nullptr))
        {
            Why = FString::Printf(TEXT("no path from %s to %s"), *TileText(Test, F->KantoTile), *TileText(Test, Goal));
            return false;
        }
    }
    return true;
}

bool FWalker::Start(FGameTestContext& Test, FIntPoint InGoal)
{
    Goal = InGoal;
    Replans = 0;
    StuckCount = 0;
    LastProgress = Test.Seconds;
    const UPYRField* F = M4::Field(Test);
    LastTile = F ? F->KantoTile : FIntPoint::ZeroValue;
    Warps = F ? F->WarpsTaken : 0;
    return Replan(Test, true);
}

FWalker::EState FWalker::Tick(FGameTestContext& Test)
{
    UPYRField* F = M4::Field(Test);
    APokemonYellowReimagineCharacter* R = M4::Red(Test);
    const AYellowWorld* W = M4::World(Test);
    if (!F || !R || !W) return EState::Stuck;
    if (F->IsWarping() || F->IsLocked() || R->IsScriptWalking())
    {
        M4::StopWalking(Test);
        LastProgress = Test.Seconds;
        return EState::Walking;
    }
    const FIntPoint Here = F->KantoTile;
    if (Here == Goal && FVector::Dist2D(R->GetActorLocation(), W->KantoCenter(Goal)) < 35.f) { M4::StopWalking(Test); return EState::Arrived; }
    if (F->WarpsTaken != Warps)
    {
        Warps = F->WarpsTaken;
        LastTile = Here;
        LastProgress = Test.Seconds;
        if (!Replan(Test, true)) return EState::NoPath;
    }
    if (Here != LastTile)
    {
        LastTile = Here;
        LastProgress = Test.Seconds;
        // Skip ahead to where Red is on the path (a hop or a slide can pass a tile).
        for (int32 I = Path.Num() - 1; I >= At; --I) if (Path[I].Tile == Here) { At = I + (Path[I].bWarp ? 0 : 1); break; }
    }
    while (Path.IsValidIndex(At) && !Path[At].bWarp && Path[At].Tile == Here && FVector::Dist2D(R->GetActorLocation(), W->KantoCenter(Here)) < 30.f) ++At;
    if (!Path.IsValidIndex(At))
    {
        if (Here == Goal) { M4::WalkTo(Test, W->KantoCenter(Goal), 20.f); return EState::Walking; }
        if (!Replan(Test, true)) return EState::NoPath;
        if (!Path.IsValidIndex(At)) return EState::Walking;
    }
    if (Test.Seconds - LastProgress > 2.5)
    {
        ++StuckCount;
        LastProgress = Test.Seconds;
        if (StuckCount > 8)
        {
            const FString Next = Path.IsValidIndex(At) ? PersonAt(Test, Path[At].Tile) : FString();
            Why = FString::Printf(TEXT("stuck at %s on the way to %s%s"), *TileText(Test, Here), *TileText(Test, Goal),
                Next.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s stands on the next tile, %s)"), *Next, *TileText(Test, Path[At].Tile)));
            M4::StopWalking(Test);
            return EState::Stuck;
        }
        if (!Replan(Test, true)) return EState::NoPath;
    }
    const FStep& Next = Path[At];
    FVector Target = W->KantoCenter(Next.Tile);
    // Pushing on into a warp at a room's edge (an exit mat): keep going the same way past the tile's centre.
    if (Next.bWarp && Here == Next.Tile && At > 0)
    {
        const FIntPoint Dir = Next.Tile - Path[At - 1].Tile;
        Target += FVector(Dir.X * 100.f, Dir.Y * 100.f, 0.f);
    }
    R->TestInput = (Target - R->GetActorLocation()) * FVector(1, 1, 0);
    R->TestInput = R->TestInput.GetSafeNormal() * FMath::Clamp(float(R->TestInput.Size()) / 60.f, .35f, 1.f);
    return EState::Walking;
}

const TCHAR* LookKind(TCHAR L)
{
    switch (L)
    {
    case TEXT('W'): return TEXT("water");
    case TEXT('T'): return TEXT("trees");
    case TEXT('F'): return TEXT("fence");
    case TEXT('C'): return TEXT("cave wall");
    case TEXT('c'): return TEXT("cave floor");
    case TEXT('B'): case TEXT('R'): case TEXT('D'): case TEXT('n'): return TEXT("building");
    case TEXT('L'): return TEXT("ledge");
    case TEXT('S'): return TEXT("sign");
    case TEXT('G'): return TEXT("tall grass");
    case TEXT('x'): return TEXT("rock");
    default: return TEXT("open ground");
    }
}

TMap<FIntPoint, uint8> DrawnOffMap(FGameTestContext& Test, int32 Ring)
{
    TMap<FIntPoint, uint8> Drawn;
    const AYellowWorld* W = M4::World(Test);
    const FYellowDataSet* D = M4::Data(Test);
    if (!W || !D) return Drawn;
    TArray<const AYellowMapActor*> Actors;
    for (const FName& Id : D->MapOrder) Actors.Add(W->MapActor(Id));
    Actors.Add(W->GetSceneryActor());
    // The outdoor maps' box and the ring round it: the only tiles counted.
    FIntRect Area(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max(), TNumericLimits<int32>::Lowest(), TNumericLimits<int32>::Lowest());
    for (const FName& Id : D->MapOrder)
        if (const FYMap* M = D->Map(Id); M && M->bOutdoor)
        {
            Area.Min = Area.Min.ComponentMin(M->Origin - FIntPoint(Ring, Ring));
            Area.Max = Area.Max.ComponentMax(M->Origin + FIntPoint(M->Width + Ring, M->Height + Ring));
        }
    for (const AYellowMapActor* A : Actors)
    {
        if (!A) continue;
        for (const TObjectPtr<UInstancedStaticMeshComponent>& C : A->Looks)
        {
            if (!C || !C->GetStaticMesh() || !C->IsVisible()) continue;
            const FString Mesh = C->GetStaticMesh()->GetName();
            const uint8 Bit = Mesh == TEXT("Sphere") || Mesh == TEXT("Cylinder") || Mesh == TEXT("Cone") ? 1 : Mesh == TEXT("Plane") ? 2 : 4;
            for (int32 I = 0; I < C->GetInstanceCount(); ++I)
            {
                FTransform T;
                if (!C->GetInstanceTransform(I, T, true)) continue;
                const FIntPoint Kt = W->ToKanto(T.GetLocation());
                // Ground is merged into big planes (100 cm at scale 1), and rock past the edges into big blocks (m4r):
                // every tile under one counts. Anything else counts on the tile it stands on.
                const bool bSpread = Bit == 2 || (Mesh == TEXT("Cube") && FMath::Max(T.GetScale3D().X, T.GetScale3D().Y) > 1.5f);
                if (!bSpread) { if (!W->MapAtKanto(Kt)) Drawn.FindOrAdd(Kt) |= Bit; continue; }
                const FVector Half = T.GetScale3D() * 50.f;
                const FIntPoint Lo = W->ToKanto(T.GetLocation() - Half + FVector(1, 1, 0)).ComponentMax(Area.Min);
                const FIntPoint Hi = W->ToKanto(T.GetLocation() + Half - FVector(1, 1, 0)).ComponentMin(Area.Max);
                for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
                    for (int32 X = Lo.X; X <= Hi.X; ++X)
                        if (!W->MapAtKanto(FIntPoint(X, Y))) Drawn.FindOrAdd(FIntPoint(X, Y)) |= Bit;
            }
        }
    }
    return Drawn;
}

void Face(FGameTestContext& Test, EYDir Dir)
{
    UPYRField* F = M4::Field(Test);
    const FYMap* M = F ? F->GetMap() : nullptr;
    if (M) F->PlaceRed(M->Id, F->CurrentTile.X, F->CurrentTile.Y, Dir);
}

bool TalkSpot(FGameTestContext& Test, const APYRPerson* P, FIntPoint& OutKanto, EYDir& OutFace)
{
    const AYellowWorld* W = M4::World(Test);
    const FYellowDataSet* D = M4::Data(Test);
    const FYMap* M = P && D ? D->Map(P->Map) : nullptr;
    if (!W || !M) return false;
    auto Open = [W, M](FIntPoint Kt)
    {
        const FIntPoint L = Kt - M->Origin;
        return M->Contains(L.X, L.Y) && !YTile::IsSolid(M->Tile(L.X, L.Y)) && M->WarpAt(L.X, L.Y) == INDEX_NONE;
    };
    for (int32 Reach = 2; Reach >= 1; --Reach)
        for (const EYDir Dir : {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right})
        {
            const FIntPoint From = P->Tile - YDir::Delta(Dir) * Reach;
            if (!Open(From)) continue;
            if (Reach == 2 && W->TileAt(P->Tile - YDir::Delta(Dir)) != TEXT('=')) continue;
            if (Reach == 1 && W->IsEdgeBlocked(From, Dir)) continue;
            OutKanto = From;
            OutFace = Dir;
            return true;
        }
    return false;
}

// Yellow's water drawn and not covered: every flat piece (a plane, 100 cm at scale 1) of every map and scenery actor
// over each water tile of Map. The water's own surface sits 14 cm under the banks; anything flat higher up hides it
// (the scenery canvas's far meadow once lay at -6 cm and hid every pond).
bool WaterShows(FGameTestContext& Test, FName Map, FString& Why, int32* Count)
{
    const FYellowDataSet* D = M4::Data(Test);
    const AYellowWorld* W = M4::World(Test);
    const FYMap* M = D ? D->Map(Map) : nullptr;
    if (!M || !W) { Why += TEXT("no map; "); return false; }
    TArray<const AYellowMapActor*> Actors;
    for (const FName& Id : D->MapOrder) { Actors.Add(W->MapActor(Id)); Actors.Add(W->GetSceneryActorFor(Id)); }
    Actors.Add(W->GetSceneryActor());
    struct FFlat { FBox2D Box; float Z; FString Owner; };
    TArray<FFlat> Flats;
    for (const AYellowMapActor* A : Actors)
    {
        if (!A) continue;   // hidden or not: only the map Red is in is drawn, and every outdoor map shows once he is out
        TInlineComponentArray<UInstancedStaticMeshComponent*> Comps(A);   // all of them: the meadow is not in Looks
        for (const UInstancedStaticMeshComponent* C : Comps)
        {
            if (!C || !C->GetStaticMesh() || C->GetStaticMesh()->GetName() != TEXT("Plane")) continue;
            for (int32 I = 0; I < C->GetInstanceCount(); ++I)
            {
                FTransform T;
                if (!C->GetInstanceTransform(I, T, true)) continue;
                const FVector2D Half(T.GetScale3D().X * 50.f, T.GetScale3D().Y * 50.f);
                const FVector2D At(T.GetLocation());
                Flats.Add({FBox2D(At - Half, At + Half), float(T.GetLocation().Z), A->MapId.ToString()});
            }
        }
    }
    int32 N = 0;
    for (int32 Y = 0; Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X)
        {
            if (M->Look(X, Y) != TEXT('W')) continue;
            ++N;
            const FVector P = W->TileCenter(Map, X, Y);
            bool bSurface = false;
            for (const FFlat& F : Flats)
            {
                if (!F.Box.IsInside(FVector2D(P))) continue;
                if (FMath::IsNearlyEqual(F.Z, float(P.Z) - 14.f, 1.f)) { bSurface = true; continue; }
                if (F.Z > P.Z - 13.f)
                {
                    Why += FString::Printf(TEXT("%s %d,%d: the water is covered by a flat piece of %s %.0f cm above it; "), *Map.ToString(), X, Y, *F.Owner, F.Z - (P.Z - 14.f));
                    return false;
                }
            }
            if (!bSurface) { Why += FString::Printf(TEXT("%s %d,%d: no water surface drawn; "), *Map.ToString(), X, Y); return false; }
        }
    if (Count) *Count = N;
    Why += FString::Printf(TEXT("%d water tiles on %s, each showing; "), N, *Map.ToString());
    return true;
}
}
