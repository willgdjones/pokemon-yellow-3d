// m4v: the anime look's tests (README "Anime look (m4v)").
// - Shot_M4vLook_<frame> (rendered), one per frame of design/look-anime/frames.json: a new game, the frame staged as the
//   m4u shot its target was restyled from (the look areas of Tests/M4LookStaging.cpp, Brock's fight, the Route 1 wild fight), then a
//   fixed camera at the frame's pose from Content/Look/Kit/look-frames.json (or the design's frames.json when it gives
//   one; the two lists of frames must match), the frame's area grade (World/PYRAreaLook) settled, and the picture saved to -M4vShot=<png> (the
//   look check's shot m4v-look-<frame>). Logs M4V_LOOK_CAMERA with the pose it used.
// - M4vFrameRateBudget (rendered): EdgeSceneryFrameRate's six views, each measured for 3 s, best of up to three passes;
//   the total frame time must stay within the baseline's /0.9 (at least 90% of its frame rate). The baseline is
//   the game's own Content/Look/Kit/baseline.json. -M4vRecordBaseline=<json> writes the measurement as the baseline instead.
// - M4vCastUnchanged: Red, Pikachu, every person and every Pokemon (the wild ones, the trainers' teams) of Pallet Town,
//   Viridian Forest, Pewter Gym and Cerulean Gym use only the character and Pokemon assets listed in
//   Content/Look/Kit/cast-hashes.json, and where the project's Content folder can be found, those files still have their
//   recorded hash (sha1; the list records sha256 too).
// - M4vKitCoverage (rendered): every frame's view and every room of every town; each static, instanced or procedural mesh
//   drawn in the last frame must be painted with the anime kit (/Game/Look/Kit/), the sky (/Game/Look/M_Sky), a character
//   or Pokemon material, or a battle effect. Lists the offenders.
#include "GameTest.h"
#include "Tests/M4TestKit.h"

#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Encounters/PYRBattleCamera.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Menus/PYRMenus.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Partner/PYRPikachuFollower.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "ProceduralMeshComponent.h"
#include "RenderCore.h"
#include "RHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Stage/PYREncounters.h"
#include "UnrealClient.h"
#include "World/CoreSeams.h"
#include "World/PYRAreaLook.h"
#include "Yellow/YellowData.h"

bool PYRStageM4Look(UWorld* W, const FString& Area, double Seconds, int32& Step, double& At);

namespace PYRM4V
{
// ------------------------------------------------------------------------------------------------ files
TSharedPtr<FJsonObject> ReadJson(const FString& Path)
{
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Path)) return nullptr;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)) return nullptr;
    return Root;
}

// The folder holding the design package (design/look-anime/frames.json): the project itself in the editor, or a folder
// above a package. Empty when not found.
FString RepoRoot()
{
    FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    for (int32 I = 0; I < 8 && !Dir.IsEmpty(); ++I)
    {
        if (FPaths::FileExists(FPaths::Combine(Dir, TEXT("design/look-anime/frames.json")))) return Dir;
        const FString Up = FPaths::GetPath(Dir.EndsWith(TEXT("/")) ? Dir.LeftChop(1) : Dir);
        if (Up == Dir) break;
        Dir = Up;
    }
    return FString();
}

FString KitFile(const TCHAR* Name) { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Look/Kit"), Name)); }

FVector Vec(const TArray<TSharedPtr<FJsonValue>>& A) { return A.Num() >= 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : FVector::ZeroVector; }

struct FFrame
{
    FString Id, Area, Stage, Shot;
    FName Map;
    FIntPoint Tile = FIntPoint::ZeroValue;
    bool bCamera = false;
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    float Fov = 75.f;
    FString CameraFrom;
};

bool ParseCamera(const TSharedPtr<FJsonObject>& Cam, FFrame& F)
{
    const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
    if (!Cam.IsValid() || !Cam->TryGetArrayField(TEXT("location"), L) || !Cam->TryGetArrayField(TEXT("rotation"), R)) return false;
    F.Location = Vec(*L);
    const FVector Rot = Vec(*R);
    F.Rotation = FRotator(Rot.X, Rot.Y, Rot.Z);
    double Fov = 75;
    Cam->TryGetNumberField(TEXT("fov"), Fov);
    F.Fov = float(Fov);
    F.bCamera = true;
    return true;
}

// The frames, from the game's table, checked against the design's frames.json (design/look-anime/frames.json)
// when it is there: the same ids, and a camera from the design when it gives one.
bool LoadFrames(TMap<FString, FFrame>& Out, FString& Error, FString& Note)
{
    TSharedPtr<FJsonObject> Table = ReadJson(KitFile(TEXT("look-frames.json")));
    const TSharedPtr<FJsonObject>* Frames = nullptr;
    if (!Table.IsValid() || !Table->TryGetObjectField(TEXT("frames"), Frames)) { Error = TEXT("no frames in ") + KitFile(TEXT("look-frames.json")); return false; }
    for (const auto& Pair : (*Frames)->Values)
    {
        const TSharedPtr<FJsonObject> O = Pair.Value->AsObject();
        FFrame F;
        F.Id = FString(*Pair.Key);
        F.Area = O->GetStringField(TEXT("area"));
        F.Stage = O->GetStringField(TEXT("stage"));
        F.Shot = O->GetStringField(TEXT("shot"));
        F.Map = FName(*O->GetStringField(TEXT("map")));
        const TArray<TSharedPtr<FJsonValue>>* T = nullptr;
        if (O->TryGetArrayField(TEXT("tile"), T) && T->Num() == 2) F.Tile = FIntPoint(int32((*T)[0]->AsNumber()), int32((*T)[1]->AsNumber()));
        const TSharedPtr<FJsonObject>* Cam = nullptr;
        if (O->TryGetObjectField(TEXT("camera"), Cam) && ParseCamera(*Cam, F)) F.CameraFrom = TEXT("look-frames.json");
        Out.Add(F.Id, F);
    }
    const FString Root = RepoRoot();
    if (Root.IsEmpty()) { Note = TEXT("the design package is not reachable from here; the game's frame table alone"); return true; }
    for (const TCHAR* Rel : {TEXT("design/look-anime/frames.json")})
    {
        TSharedPtr<FJsonObject> Design = ReadJson(FPaths::Combine(Root, Rel));
        if (!Design.IsValid()) { Error = FString::Printf(TEXT("%s does not parse"), Rel); return false; }
        TArray<FString> Ids, Mine;
        for (const auto& Pair : Design->Values) Ids.Add(FString(*Pair.Key));
        Out.GetKeys(Mine);
        Ids.Sort();
        Mine.Sort();
        if (Ids != Mine)
        {
            Error = FString::Printf(TEXT("%s lists %s; the look tests' table lists %s"), Rel, *FString::Join(Ids, TEXT(",")), *FString::Join(Mine, TEXT(",")));
            return false;
        }
        for (const auto& Pair : Design->Values)
        {
            const TSharedPtr<FJsonObject>* Cam = nullptr;
            FFrame& F = Out[FString(*Pair.Key)];
            if (Pair.Value->AsObject()->TryGetObjectField(TEXT("camera"), Cam) && ParseCamera(*Cam, F)) F.CameraFrom = Rel;
        }
    }
    Note = FString::Printf(TEXT("%d frames, the same ids as the design's frames.json"), Out.Num());
    return true;
}

// ------------------------------------------------------------------------------------------------ staging
const TCHAR* const Story[] = {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL"),
    TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")};

struct FShotState
{
    int32 Step = 0, StageStep = 0;
    double T0 = 0, StageAt = 0, FightAt = -1, MoveAt = -100;
    bool bCaption = false, bAsked = false;
    FFrame Frame;
    FString Note, Path;
    TWeakObjectPtr<ACameraActor> Camera;
};

// Brock's fight from his Onix, Thundershock until Yellow's "It doesn't affect ONIX..." shows (the m4u brock-battle shot).
// True once the caption is up and the camera has settled.
bool StageBrock(FGameTestContext& Test, FShotState& St)
{
    UWorld* W = Test.World;
    UPYRField* F = UPYRField::Get(W);
    UPYRTrainerState* S = UPYRTrainerState::Get(W);
    UPYRProgress* P = UPYRProgress::Get(W);
    UPYRScripts* Scripts = UPYRScripts::Get(W);
    UPYREncounters* Enc = UPYREncounters::Get(W);
    APYRBattleManager* Mgr = APYRBattleManager::Get(W);
    if (St.StageStep == 0)
    {
        FPYRMonSpec Pika;
        Pika.Species = TEXT("pikachu");
        Pika.Level = 12;
        Pika.Moves = {PYRData::ThunderShock, PYRData::Growl, PYRData::ThunderWave};
        S->DebugSetParty({Pika});
        for (const TCHAR* Flag : Story) P->Set(Flag);
        PYRScripts::Refresh(W);
        F->PlaceRed(TEXT("PEWTER_GYM"), 4, 3, EYDir::Up);
        St.StageAt = Test.Seconds;
        St.StageStep = 1;
        return false;
    }
    if (St.StageStep == 1)
    {
        if (Test.Seconds - St.StageAt < 1.2) return false;
        FPYRFight Fight;
        if (!Scripts->TrainerFight(F->FindPerson(TEXT("PEWTER_GYM"), TEXT("PEWTERGYM_BROCK")), Fight)) return false;
        Fight.Foes.RemoveAll([](const FPYRMonSpec& M) { return M.Species != FName(TEXT("onix")); });
        Fight.bNoBlackout = true;
        Enc->Begin(Fight);
        St.StageAt = Test.Seconds;
        St.StageStep = 2;
        return false;
    }
    APYRMonPawn* Mine = Mgr ? Mgr->GetPlayerMon() : nullptr;
    APYRMonPawn* Foe = Mgr ? Mgr->GetFoeMon() : nullptr;
    if (!Mgr || Enc->GetPhase() != EPYREncounterPhase::Fight || !Mine || !Foe) return false;
    Mgr->bFoePassive = true;
    if (St.FightAt < 0) St.FightAt = Test.Seconds;
    FString Said;
    for (const FString& Line : Mgr->CaptionLog) if (Line.Contains(TEXT("doesn't affect"))) Said = Line;
    if (!Said.IsEmpty())
    {
        // The caption band held 0.6 s old (it shows for 2.2 s), as the m4u shot had it.
        Mgr->CaptionText = Said;
        Mgr->CaptionAt = W->GetTimeSeconds() - .6;
        St.bCaption = true;
        const APYRBattleCamera* Cam = Enc->GetCamera();
        return Cam && Cam->IsSettled() && Test.Seconds - St.FightAt > 3.0;
    }
    if (Test.Seconds - St.FightAt > 2.0 && Test.Seconds - St.MoveAt > 2.5)
    {
        const FVector To = Foe->GetActorLocation() - Mine->GetActorLocation();
        Mine->SetActorRotation(FRotator(0, To.Rotation().Yaw, 0));
        Mgr->CaptionLog.Reset();
        Mgr->PressMove(0);
        St.MoveAt = Test.Seconds;
    }
    return false;
}

// A Route 1 grass encounter, forced: Pikachu and a wild Pidgey holding its far platform (the m4u route1-battle-stage shot).
bool StageRoute1Battle(FGameTestContext& Test, FShotState& St)
{
    UWorld* W = Test.World;
    UPYRField* F = UPYRField::Get(W);
    UPYRTrainerState* S = UPYRTrainerState::Get(W);
    UPYRProgress* P = UPYRProgress::Get(W);
    UPYREncounters* Enc = UPYREncounters::Get(W);
    APYRBattleManager* Mgr = APYRBattleManager::Get(W);
    if (St.StageStep == 0)
    {
        FPYRMonSpec Pika;
        Pika.Species = TEXT("pikachu");
        Pika.Level = 6;
        S->DebugSetParty({Pika});
        S->SetItemCount(UPYRTrainerState::Potion, 3);
        S->SetItemCount(UPYRTrainerState::PokeBall, 5);
        for (const TCHAR* Flag : {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER")}) P->Set(Flag);
        PYRScripts::Refresh(W);
        F->PlaceRed(TEXT("ROUTE_1"), 9, 26, EYDir::Up);
        St.StageAt = Test.Seconds;
        St.StageStep = 1;
        return false;
    }
    if (St.StageStep == 1)
    {
        if (Test.Seconds - St.StageAt < 1.2) return false;
        if (!Enc->BeginWild(TEXT("pidgey"), 3)) return false;
        St.StageStep = 2;
        return false;
    }
    if (!Mgr || Enc->GetPhase() != EPYREncounterPhase::Fight) return false;
    Mgr->bFoePassive = true;
    if (St.FightAt < 0) St.FightAt = Test.Seconds;
    const APYRBattleCamera* Cam = Enc->GetCamera();
    return Cam && Cam->IsSettled() && Test.Seconds - St.FightAt > 2.5;
}

FString Str(const FVector& V) { return FString::Printf(TEXT("%.3f,%.3f,%.3f"), V.X, V.Y, V.Z); }
FString Str(const FRotator& R) { return FString::Printf(TEXT("%.4f,%.4f,%.4f"), R.Pitch, R.Yaw, R.Roll); }

// One look frame: stage it, hold the fixed camera, let the area's grade settle, save the shot.
EGameTest LookShot(FGameTestContext& Test, const TCHAR* Id)
{
    static FShotState St;
    M4_READY();
    UWorld* W = Test.World;
    APlayerController* PC = W->GetFirstPlayerController();
    if (St.Step == 0)
    {
        TMap<FString, FFrame> Frames;
        FString Error;
        if (!LoadFrames(Frames, Error, St.Note)) M4_FAIL(TEXT("%s"), *Error);
        if (!Frames.Contains(Id)) M4_FAIL(TEXT("no frame %s in the frame table"), Id);
        St.Frame = Frames[Id];
        if (!FParse::Value(FCommandLine::Get(), TEXT("M4vShot="), St.Path) || St.Path.IsEmpty()) M4_FAIL(TEXT("no -M4vShot=<png> on the command line"));
        M4::Quiet(true, true);
        St.Step = 1;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 1)
    {
        bool bStaged = false;
        if (St.Frame.Stage == TEXT("m4-look")) bStaged = PYRStageM4Look(W, St.Frame.Id, Test.Seconds, St.StageStep, St.StageAt);
        else if (St.Frame.Stage == TEXT("brock-battle")) bStaged = StageBrock(Test, St);
        else if (St.Frame.Stage == TEXT("route1-battle-stage")) bStaged = StageRoute1Battle(Test, St);
        else M4_FAIL(TEXT("frame %s: unknown stage %s"), Id, *St.Frame.Stage);
        if (!bStaged)
        {
            if (Test.Seconds - St.T0 > 60) M4_FAIL(TEXT("frame %s: the %s staging never finished (step %d)"), Id, *St.Frame.Stage, St.StageStep);
            return EGameTest::Running;
        }
        // The fixed camera: the battle camera frozen at the frame's pose, or a camera of the look's own on the field.
        UPYREncounters* Enc = UPYREncounters::Get(W);
        APYRBattleCamera* Battle = Enc && Enc->GetPhase() == EPYREncounterPhase::Fight ? Enc->GetCamera() : nullptr;
        if (Battle)
        {
            Battle->SetActorTickEnabled(false);
            if (St.Frame.bCamera)
            {
                Battle->SetActorLocationAndRotation(St.Frame.Location, St.Frame.Rotation);
                Battle->GetCameraComponent()->SetFieldOfView(St.Frame.Fov);
            }
            else
            {
                St.Frame.Location = Battle->GetActorLocation();
                St.Frame.Rotation = Battle->GetActorRotation();
                St.Frame.Fov = Battle->GetCameraComponent()->FieldOfView;
                St.Frame.CameraFrom = TEXT("the settled battle camera (recorded)");
            }
        }
        else
        {
            if (!St.Frame.bCamera) M4_FAIL(TEXT("frame %s has no camera"), Id);
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            ACameraActor* Cam = W->SpawnActor<ACameraActor>(St.Frame.Location, St.Frame.Rotation, Params);
            Cam->Tags.Add(TEXT("PYR.FollowRed"));   // hides what stands between it and Red, as Red's own camera does
            Cam->GetCameraComponent()->SetFieldOfView(St.Frame.Fov);
            Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
            PC->SetViewTarget(Cam);
            St.Camera = Cam;
        }
        UE_LOG(LogTemp, Display, TEXT("M4V_LOOK_CAMERA id=%s location=%s rotation=%s fov=%.4f from=%s"), Id, *Str(St.Frame.Location), *Str(St.Frame.Rotation),
            St.Frame.Fov, *St.Frame.CameraFrom);
        St.Step = 2;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 2)
    {
        // Keep the battle's staging alive (the caption held), let streaming and the grade settle.
        if (St.Frame.Stage == TEXT("brock-battle")) StageBrock(Test, St);
        if (St.Frame.Stage == TEXT("route1-battle-stage")) StageRoute1Battle(Test, St);
        if (Test.Seconds - St.T0 < 2.0) return EGameTest::Running;
        // The m4u look shots were taken 8 s into the run (-PYRShotDelay=8). The townsfolk wander on seeded steps from
        // the start and the people's idle clips turn them as they play, so a frame shot at that moment shows the people
        // where and how the m4u shot (and so the target) has them.
        if (St.Frame.Stage == TEXT("m4-look") && Test.Seconds < 8.0) return EGameTest::Running;
        // The frame's area grade (World/PYRAreaLook, Content/Look/Kit/areas.json) in place and settled.
        const UPYRAreaLook* Look = UPYRAreaLook::Get(W);
        if (!Look || !Look->IsLoaded()) M4_FAIL(TEXT("frame %s: no area look table (%s)"), Id, Look ? *Look->LoadError() : TEXT("no subsystem"));
        if (Look->GetArea() != FName(*St.Frame.Area) || !Look->IsSettled())
        {
            if (Test.Seconds - St.T0 > 12) M4_FAIL(TEXT("frame %s: the look is %s (settled %d), not the frame's area %s"), Id, *Look->GetArea().ToString(), Look->IsSettled(), *St.Frame.Area);
            return EGameTest::Running;
        }
        const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
        if (!View.Location.Equals(St.Frame.Location, 2.f) || !View.Rotation.Equals(St.Frame.Rotation, .2f))
            M4_FAIL(TEXT("frame %s: the view is at %s %s, not the frame's %s %s"), Id, *Str(View.Location), *Str(View.Rotation), *Str(St.Frame.Location), *Str(St.Frame.Rotation));
        IFileManager::Get().Delete(*St.Path, false, true, true);
        FScreenshotRequest::RequestScreenshot(St.Path, true, false);
        St.Step = 3;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Frame.Stage == TEXT("brock-battle")) StageBrock(Test, St);
    if (IFileManager::Get().FileSize(*St.Path) <= 0)
    {
        if (Test.Seconds - St.T0 > 20) M4_FAIL(TEXT("frame %s: no screenshot at %s"), Id, *St.Path);
        return EGameTest::Running;
    }
    M4_PASS(TEXT("frame %s (%s, staged as %s) shot from %s %s fov %.1f (%s): %s; %s"), Id, *St.Frame.Area, *St.Frame.Stage, *Str(St.Frame.Location),
        *Str(St.Frame.Rotation), St.Frame.Fov, *St.Frame.CameraFrom, *St.Path, *St.Note);
}
}

#define M4V_LOOK(Name, Id) GAME_TEST(Shot_M4vLook_##Name, 180) { return PYRM4V::LookShot(Test, TEXT(Id)); }
M4V_LOOK(pallet_town, "pallet-town")
M4V_LOOK(route_1, "route-1")
M4V_LOOK(viridian_city, "viridian-city")
M4V_LOOK(viridian_forest, "viridian-forest")
M4V_LOOK(pewter_city, "pewter-city")
M4V_LOOK(mt_moon, "mt-moon")
M4V_LOOK(cerulean_city, "cerulean-city")
M4V_LOOK(pokemon_centre, "pokemon-centre")
M4V_LOOK(mart, "mart")
M4V_LOOK(oaks_lab, "oaks-lab")
M4V_LOOK(characters, "characters")
M4V_LOOK(ui, "ui")
M4V_LOOK(cutscene_oak, "cutscene-oak")
M4V_LOOK(pewter_gym_battle, "pewter-gym-battle")
M4V_LOOK(brock_battle, "brock-battle")
M4V_LOOK(route1_battle_stage, "route1-battle-stage")

// ------------------------------------------------------------------------------------------------ frame rate
GAME_TEST(M4vFrameRateBudget, 280)
{
    // EdgeSceneryFrameRate's own six views (Tests/M4STests.cpp).
    struct FAt { const TCHAR* Map; int32 X, Y; float Yaw; const TCHAR* Name; };
    static const FAt Views[] = {{TEXT("PALLET_TOWN"), 10, 12, -90.f, TEXT("pallet")}, {TEXT("ROUTE_22"), 32, 2, -135.f, TEXT("route 22 north-west")},
        {TEXT("ROUTE_4"), 4, 8, 180.f, TEXT("route 4 west")}, {TEXT("ROUTE_4"), 46, 4, -90.f, TEXT("route 4 north")}, {TEXT("ROUTE_24"), 4, 18, 180.f, TEXT("route 24 west")},
        {TEXT("ROUTE_25"), 40, 2, -90.f, TEXT("route 25 north")}};
    constexpr int32 NumViews = UE_ARRAY_COUNT(Views);
    constexpr int32 Passes = 3;
    static M4::FState St;
    static double Sum[2];
    static int32 Frames = 0, Pass = 0;
    static double LastWall = 0;
    static double Best[NumViews], BestGpu[NumViews];
    static TMap<FString, double> Base;
    static double BaseTotal = 0;
    static FString BaseFrom;
    static bool bInit = false;
    M4_READY();
    APlayerController* PC = Test.World->GetFirstPlayerController();
    FString RecordPath;
    const bool bRecord = FParse::Value(FCommandLine::Get(), TEXT("M4vRecordBaseline="), RecordPath) && !RecordPath.IsEmpty();
    if (!bInit)
    {
        bInit = true;
        for (int32 I = 0; I < NumViews; ++I) Best[I] = BestGpu[I] = 1e9;
        if (!bRecord)
        {
            const FString Path = PYRM4V::KitFile(TEXT("baseline.json"));
            TSharedPtr<FJsonObject> J = PYRM4V::ReadJson(Path);
            const TSharedPtr<FJsonObject>* V = nullptr;
            if (!J.IsValid() || !J->TryGetObjectField(TEXT("best_frame_ms"), V)) M4_FAIL(TEXT("no baseline (best_frame_ms) in %s"), *Path);
            for (const auto& Pair : (*V)->Values) { Base.Add(FString(*Pair.Key), Pair.Value->AsNumber()); BaseTotal += Pair.Value->AsNumber(); }
            BaseFrom = Path;
        }
        else BaseFrom = TEXT("(recording)");
    }
    if (St.I >= NumViews)
    {
        // A pass done: within budget (or recording, or out of passes) ends the test.
        double Total = 0, Gpu = 0;
        for (int32 I = 0; I < NumViews; ++I) { Total += Best[I]; Gpu += BestGpu[I]; }
        ++Pass;
        const double Budget = BaseTotal / .9;
        if ((!bRecord && Total <= Budget) || Pass >= Passes)
        {
            FString Views_;
            for (int32 I = 0; I < NumViews; ++I) Views_ += FString::Printf(TEXT("%s %.2f ms (GPU %.2f); "), Views[I].Name, Best[I], BestGpu[I]);
            if (bRecord)
            {
                TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
                TSharedRef<FJsonObject> V = MakeShared<FJsonObject>(), G = MakeShared<FJsonObject>();
                for (int32 I = 0; I < NumViews; ++I) { V->SetNumberField(Views[I].Name, Best[I]); G->SetNumberField(Views[I].Name, BestGpu[I]); }
                O->SetObjectField(TEXT("best_frame_ms"), V);
                O->SetObjectField(TEXT("best_gpu_ms"), G);
                O->SetNumberField(TEXT("total_frame_ms"), Total);
                O->SetNumberField(TEXT("rate_fps_sum_of_frames"), 1000.0 * NumViews / Total);
                O->SetStringField(TEXT("method"), TEXT("M4vFrameRateBudget: EdgeSceneryFrameRate's six views, 3 s each, best of 3 passes"));
                FString Text;
                FJsonSerializer::Serialize(O, TJsonWriterFactory<>::Create(&Text));
                FFileHelper::SaveStringToFile(Text, *RecordPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
                M4_PASS(TEXT("recorded %s: total %.2f ms, best of %d passes (%s)"), *RecordPath, Total, Pass, *Views_);
            }
            const double Ratio = BaseTotal / FMath::Max(Total, .001);
            if (Total > Budget)
                M4_FAIL(TEXT("frame rate %.1f%% of the baseline (total frame %.2f ms against %.2f ms, budget %.2f ms) after %d passes: %sbaseline %s"),
                    Ratio * 100.0, Total, BaseTotal, Budget, Pass, *Views_, *BaseFrom);
            M4_PASS(TEXT("frame rate %.1f%% of the baseline (at least 90%%): total frame %.2f ms against %.2f ms (pass %d): %sbaseline %s"), Ratio * 100.0, Total,
                BaseTotal, Pass, *Views_, *BaseFrom);
        }
        St.I = 0;
        St.Step = 0;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    const auto& V = Views[St.I];
    if (St.Step == 0)
    {
        M4::AfterPokedex(Test);
        M4::Quiet(true, true);
        M4::Place(Test, V.Map, V.X, V.Y, YDir::FromYaw(V.Yaw));
        PC->SetControlRotation(FRotator(-10.f, V.Yaw, 0));
        Sum[0] = Sum[1] = 0;
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
    Sum[1] += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
    LastWall = Now;
    ++Frames;
    M4_WAIT(3.0);
    const double Frame = Sum[0] / Frames, Gpu = Sum[1] / Frames;
    UE_LOG(LogTemp, Display, TEXT("PYR_M4V_FRAME pass %d %s: frame %.2f ms, gpu %.2f ms (%d frames)"), Pass + 1, V.Name, Frame, Gpu, Frames);
    Best[St.I] = FMath::Min(Best[St.I], Frame);
    BestGpu[St.I] = FMath::Min(BestGpu[St.I], Gpu);
    ++St.I;
    St.Step = 0;
    St.T0 = Test.Seconds;
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ the cast
namespace PYRM4V
{
// The asset a material, mesh or texture comes from (/Game/Characters/red/SK_red), a dynamic instance's parent asset.
FString AssetPath(const UObject* O)
{
    while (const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(O)) O = Mid->Parent;
    return O ? O->GetOutermost()->GetName() : FString();
}

void CollectCast(AActor* A, TSet<FString>& Assets)
{
    if (!A) return;
    TArray<UMeshComponent*> Meshes;
    A->GetComponents<UMeshComponent>(Meshes);
    for (UMeshComponent* M : Meshes)
    {
        if (const USkeletalMeshComponent* Sk = Cast<USkeletalMeshComponent>(M)) { if (Sk->GetSkeletalMeshAsset()) Assets.Add(AssetPath(Sk->GetSkeletalMeshAsset())); }
        else if (const UStaticMeshComponent* St = Cast<UStaticMeshComponent>(M)) { if (St->GetStaticMesh()) Assets.Add(AssetPath(St->GetStaticMesh())); }
        for (int32 I = 0; I < M->GetNumMaterials(); ++I)
            if (UMaterialInterface* Mat = M->GetMaterial(I))
            {
                const UMaterialInterface* Asset = Mat;
                while (const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Asset)) Asset = Mid->Parent;
                for (const UMaterialInterface* Up = Asset; Up; )
                {
                    Assets.Add(AssetPath(Up));
                    const UMaterialInstance* Mi = Cast<UMaterialInstance>(Up);
                    Up = Mi ? Mi->Parent.Get() : nullptr;
                }
                TArray<UTexture*> Textures;
                // The running quality level: what Num falls back to, without the engine's ensure.
                Mat->GetUsedTextures(Textures, GetCurrentMaterialQualityLevelChecked());
                for (UTexture* T : Textures) if (T) Assets.Add(AssetPath(T));
            }
    }
}
}

GAME_TEST(M4vCastUnchanged, 240)
{
    struct FPlace { const TCHAR* Map; int32 X, Y; };
    static const FPlace Places[] = {{TEXT("PALLET_TOWN"), 10, 12}, {TEXT("VIRIDIAN_FOREST"), 18, 43}, {TEXT("PEWTER_GYM"), 4, 9}, {TEXT("CERULEAN_GYM"), 4, 12}};
    static M4::FState St;
    static TSet<FString> Seen;
    static TSet<FName> Species;
    static TArray<TWeakObjectPtr<AActor>> Spawned;
    M4_READY();
    UWorld* W = Test.World;
    UPYRField* F = UPYRField::Get(W);
    if (St.I < int32(UE_ARRAY_COUNT(Places)))
    {
        const FPlace& P = Places[St.I];
        if (St.Step == 0)
        {
            M4::AfterPokedex(Test);
            M4::Quiet(true, true);
            M4::Party(Test, {{TEXT("pikachu"), 12}});
            if (!M4::Place(Test, P.Map, P.X, P.Y, EYDir::Up)) M4_FAIL(TEXT("could not place Red in %s"), P.Map);
            M4_NEXT();
        }
        M4_WAIT(1.0);
        PYRM4V::CollectCast(APokemonYellowReimagineCharacter::Find(W), Seen);
        PYRM4V::CollectCast(APYRPikachuFollower::Find(W), Seen);
        UPYRScripts* Scripts = UPYRScripts::Get(W);
        int32 People = 0;
        for (const TWeakObjectPtr<APYRPerson>& Person : F->People)
        {
            if (!Person.IsValid() || Person->Map != FName(P.Map)) continue;
            PYRM4V::CollectCast(Person.Get(), Seen);
            ++People;
            FPYRFight Fight;
            if (Scripts && Scripts->TrainerFight(Person.Get(), Fight)) for (const FPYRMonSpec& M : Fight.Foes) Species.Add(M.Species);
        }
        if (const FYellowDataSet* D = M4::Data(Test))
            if (const FYWild* Wild = D->Wild.Find(FName(P.Map))) for (const FYSlot& Slot : Wild->Grass) Species.Add(Slot.Species);
        St.Note += FString::Printf(TEXT("%s: %d people; "), P.Map, People);
        ++St.I;
        St.Step = 0;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 0)
    {
        // Every Pokemon those places show (their wild ones and the trainers' teams), as the fights spawn them.
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        int32 N = 0;
        for (const FName& S : Species)
        {
            APYRMonPawn* Mon = W->SpawnActor<APYRMonPawn>(FVector(-80000.f + N * 300.f, -80000.f, -30000.f), FRotator::ZeroRotator, Params);
            Mon->InitVisualOnly(S);
            Spawned.Add(Mon);
            ++N;
        }
        M4_NEXT();
    }
    M4_WAIT(1.0);
    for (const TWeakObjectPtr<AActor>& A : Spawned) PYRM4V::CollectCast(A.Get(), Seen);
    // The recorded list of the cast's assets.
    TSharedPtr<FJsonObject> J = PYRM4V::ReadJson(PYRM4V::KitFile(TEXT("cast-hashes.json")));
    const TSharedPtr<FJsonObject>* Files = nullptr;
    if (!J.IsValid() || !J->TryGetObjectField(TEXT("files"), Files)) M4_FAIL(TEXT("no files in %s"), *PYRM4V::KitFile(TEXT("cast-hashes.json")));
    TArray<FString> Outside, Changed;
    int32 Cast = 0, Hashed = 0;
    const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
    TArray<FString> Sorted = Seen.Array();
    Sorted.Sort();
    for (const FString& Asset : Sorted)
    {
        // The engine's own content (and its plugins', such as the FBX importer's default material) is not the game's.
        if (Asset.StartsWith(TEXT("/Engine/")) || Asset.StartsWith(TEXT("/Script/")) || Asset.StartsWith(TEXT("/InterchangeAssets/"))) continue;
        const FString Rel = Asset.Replace(TEXT("/Game/"), TEXT("")) + TEXT(".uasset");
        const TSharedPtr<FJsonValue> Want = (*Files)->TryGetField(Rel);
        if (!Want.IsValid()) { Outside.Add(Asset); continue; }
        ++Cast;
        // Read through the lowest platform file: a package's pak layer refuses loose .uasset files outside its paks.
        const FString File = FPaths::Combine(Content, Rel);
        TArray<uint8> Bytes;
        IPlatformFile* Disk = &FPlatformFileManager::Get().GetPlatformFile();
        while (Disk->GetLowerLevel()) Disk = Disk->GetLowerLevel();
        if (TUniquePtr<IFileHandle> Handle{Disk->OpenRead(*File)})
        {
            Bytes.SetNumUninitialized(Handle->Size());
            if (!Handle->Read(Bytes.GetData(), Bytes.Num())) Bytes.Reset();
        }
        if (Bytes.Num() > 0)
        {
            if (!FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num()).ToString().Equals(Want->AsString(), ESearchCase::IgnoreCase)) Changed.Add(Rel);
            ++Hashed;
        }
    }
    for (const TWeakObjectPtr<AActor>& A : Spawned) if (A.IsValid()) A->Destroy();
    if (Outside.Num()) M4_FAIL(TEXT("%d assets on the cast are not the recorded character and Pokemon assets: %s"), Outside.Num(), *FString::Join(Outside, TEXT(", ")));
    if (Changed.Num()) M4_FAIL(TEXT("%d cast files changed since m4u: %s"), Changed.Num(), *FString::Join(Changed, TEXT(", ")));
    if (Cast == 0) M4_FAIL(TEXT("no cast assets found"));
    M4_PASS(TEXT("%s%d Pokemon species; %d cast assets (meshes, materials, textures), all recorded in cast-hashes.json; %d re-hashed from %s unchanged"),
        *St.Note, Species.Num(), Cast, Hashed, *Content);
}

// ------------------------------------------------------------------------------------------------ kit coverage
namespace PYRM4V
{
// Where a mesh's material may come from: the kit, the sky, a character or Pokemon, a battle effect.
bool AllowedMaterial(const UMaterialInterface* M, FString& Path)
{
    const UObject* O = M;
    while (const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(O)) O = Mid->Parent;
    Path = O ? O->GetPathName() : FString(TEXT("(none)"));
    return Path.StartsWith(TEXT("/Game/Look/Kit/")) || Path.StartsWith(TEXT("/Game/Look/M_Sky.")) || Path.StartsWith(TEXT("/Game/Characters/"))
        || Path.StartsWith(TEXT("/Game/Pokemon/")) || Path.StartsWith(TEXT("/Game/BattleFx/"));
}

bool IsCastOrEffect(const AActor* A)
{
    return A && (A->IsA<APokemonYellowReimagineCharacter>() || A->IsA<APYRPerson>() || A->IsA<APYRPikachuFollower>() || A->IsA<APYRMonPawn>()
        || A->ActorHasTag(TEXT("BattleFx")) || A->GetClass()->GetName().Contains(TEXT("Fx")));
}

// Every drawn mesh component's materials checked; offenders added as "<owner>/<component> <material> @ <where>".
int32 CheckDrawn(UWorld* W, const FString& Where, TMap<FString, FString>& Offenders, double Since)
{
    int32 Checked = 0;
    for (TObjectIterator<UMeshComponent> It; It; ++It)
    {
        UMeshComponent* C = *It;
        if (!C || C->GetWorld() != W || !C->IsRegistered() || !C->IsVisible() || C->bHiddenInGame) continue;
        if (!C->IsA<UStaticMeshComponent>() && !C->IsA<UProceduralMeshComponent>()) continue;
        AActor* Owner = C->GetOwner();
        if (!Owner || Owner->IsHidden() || IsCastOrEffect(Owner)) continue;
        if (const UInstancedStaticMeshComponent* Ism = Cast<UInstancedStaticMeshComponent>(C)) if (Ism->GetInstanceCount() == 0) continue;
        if (C->GetLastRenderTimeOnScreen() < Since) continue;   // not drawn in this view
        ++Checked;
        for (int32 I = 0; I < C->GetNumMaterials(); ++I)
        {
            FString Path;
            if (AllowedMaterial(C->GetMaterial(I), Path)) continue;
            const FString Key = FString::Printf(TEXT("%s/%s %s"), *Owner->GetName(), *C->GetName(), *Path);
            if (!Offenders.Contains(Key)) Offenders.Add(Key, Where);
        }
    }
    return Checked;
}
}

GAME_TEST(M4vKitCoverage, 900)
{
    // The 16 frames' places, then every room of every town on the way (and Mt. Moon's floors).
    static const TCHAR* const Rooms[] = {TEXT("REDS_HOUSE_1F"), TEXT("REDS_HOUSE_2F"), TEXT("BLUES_HOUSE"), TEXT("OAKS_LAB"), TEXT("VIRIDIAN_POKECENTER"),
        TEXT("VIRIDIAN_MART"), TEXT("VIRIDIAN_SCHOOL_HOUSE"), TEXT("VIRIDIAN_NICKNAME_HOUSE"), TEXT("VIRIDIAN_GYM"), TEXT("ROUTE_22_GATE"), TEXT("ROUTE_2_GATE"),
        TEXT("ROUTE_2_TRADE_HOUSE"), TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), TEXT("VIRIDIAN_FOREST_NORTH_GATE"), TEXT("MUSEUM_1F"), TEXT("MUSEUM_2F"), TEXT("PEWTER_GYM"),
        TEXT("PEWTER_NIDORAN_HOUSE"), TEXT("PEWTER_MART"), TEXT("PEWTER_SPEECH_HOUSE"), TEXT("PEWTER_POKECENTER"), TEXT("MT_MOON_POKECENTER"), TEXT("MT_MOON_1F"),
        TEXT("MT_MOON_B1F"), TEXT("MT_MOON_B2F"), TEXT("CERULEAN_TRASHED_HOUSE"), TEXT("CERULEAN_MELANIES_HOUSE"), TEXT("CERULEAN_POKECENTER"), TEXT("CERULEAN_GYM"),
        TEXT("BIKE_SHOP"), TEXT("CERULEAN_MART"), TEXT("CERULEAN_BADGE_HOUSE"), TEXT("BILLS_HOUSE")};
    struct FView { FName Map; FIntPoint Tile; float Yaw, Pitch; bool bFrameCam; FVector CamLoc; FRotator CamRot; FString Name; int32 Battle = 0; };
    static TArray<FView> Views;
    static M4::FState St;
    static TMap<FString, FString> Offenders;
    static int32 Checked = 0;
    static TWeakObjectPtr<ACameraActor> Cam;
    M4_READY();
    UWorld* W = Test.World;
    APlayerController* PC = W->GetFirstPlayerController();
    const FYellowDataSet* D = M4::Data(Test);
    if (Views.Num() == 0)
    {
        TMap<FString, PYRM4V::FFrame> Frames;
        FString Error, Note;
        if (!PYRM4V::LoadFrames(Frames, Error, Note)) M4_FAIL(TEXT("%s"), *Error);
        TArray<FView> Battles;
        for (const auto& Pair : Frames)
        {
            const PYRM4V::FFrame& F = Pair.Value;
            FView V{F.Map, F.Tile, -90.f, -22.f, F.bCamera, F.Location, F.Rotation, F.Id};
            if (F.Stage == TEXT("route1-battle-stage")) { V.Battle = 1; Battles.Insert(V, 0); continue; }
            if (F.Stage == TEXT("brock-battle")) { V.Battle = 2; if (!Battles.ContainsByPredicate([](const FView& B) { return B.Battle == 2; })) Battles.Add(V); continue; }
            Views.Add(V);
        }
        for (const TCHAR* Room : Rooms)
        {
            const FYMap* M = D ? D->Map(Room) : nullptr;
            if (!M) M4_FAIL(TEXT("no map %s"), Room);
            // Red near the room's middle on a walkable tile, looking north and then south across it.
            FIntPoint Best(-1, -1);
            for (int32 Y = 0; Y < M->Height; ++Y)
                for (int32 X = 0; X < M->Width; ++X)
                    if (!YTile::IsSolid(M->Tile(X, Y)) && (Best.X < 0 || FMath::Abs(X - M->Width / 2) + FMath::Abs(Y - M->Height / 2) < FMath::Abs(Best.X - M->Width / 2) + FMath::Abs(Best.Y - M->Height / 2)))
                        Best = FIntPoint(X, Y);
            Views.Add({FName(Room), Best, -90.f, -30.f, false, FVector::ZeroVector, FRotator::ZeroRotator, FString(Room) + TEXT(" north")});
            Views.Add({FName(Room), Best, 90.f, -30.f, false, FVector::ZeroVector, FRotator::ZeroRotator, FString(Room) + TEXT(" south")});
        }
        Views.Append(Battles);   // the fights last: the wild one is fled, Brock's ends the test
        M4::AfterPokedex(Test);
        M4::Party(Test, {{TEXT("pikachu"), 12}});
        M4::Quiet(true, true);
    }
    if (St.I >= Views.Num())
    {
        if (Offenders.Num())
        {
            TArray<FString> Lines;
            for (const auto& Pair : Offenders) Lines.Add(Pair.Key + TEXT(" @ ") + Pair.Value);
            Lines.Sort();
            for (const FString& L : Lines) UE_LOG(LogTemp, Display, TEXT("M4V_KIT_OFFENDER %s"), *L);
            M4_FAIL(TEXT("%d drawn surfaces are not painted with the kit (%d checked in %d views): %s"), Offenders.Num(), Checked, Views.Num(),
                *FString::Join(Lines, TEXT("; ")));
        }
        M4_PASS(TEXT("%d drawn mesh components in %d views (the 16 frames, %d rooms looked at both ways) all painted with the kit, the sky or the cast's own"),
            Checked, Views.Num(), int32(UE_ARRAY_COUNT(Rooms)));
    }
    const FView& V = Views[St.I];
    UPYREncounters* Enc = UPYREncounters::Get(W);
    if (St.Step == 0)
    {
        if (Cam.IsValid()) { Cam->Destroy(); Cam.Reset(); }
        if (Enc && Enc->IsBusy()) { if (APYRBattleManager* Mgr = APYRBattleManager::Get(W)) Mgr->PressRun(); if (Test.Seconds - St.T0 < 25) return EGameTest::Running; }
        if (!M4::Place(Test, V.Map, V.Tile.X, V.Tile.Y, YDir::FromYaw(V.Yaw))) M4_FAIL(TEXT("could not place Red at %s"), *V.Name);
        PC->SetViewTarget(APokemonYellowReimagineCharacter::Find(W));
        PC->SetControlRotation(FRotator(V.Pitch, V.Yaw, 0));
        if (V.Battle == 1) Enc->BeginWild(TEXT("pidgey"), 3);
        if (V.Battle == 2)
        {
            FPYRFight Fight;
            if (UPYRScripts::Get(W)->TrainerFight(UPYRField::Get(W)->FindPerson(TEXT("PEWTER_GYM"), TEXT("PEWTERGYM_BROCK")), Fight))
            {
                Fight.bNoBlackout = true;
                Enc->Begin(Fight);
            }
        }
        else if (V.bFrameCam)
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Cam = W->SpawnActor<ACameraActor>(V.CamLoc, V.CamRot, Params);
            Cam->Tags.Add(TEXT("PYR.FollowRed"));
            Cam->GetCameraComponent()->SetFieldOfView(75.f);
            PC->SetViewTarget(Cam.Get());
        }
        M4_NEXT();
    }
    if (V.Battle)
    {
        if (APYRBattleManager* Mgr = APYRBattleManager::Get(W)) Mgr->bFoePassive = true;
        const APYRBattleCamera* BC = Enc ? Enc->GetCamera() : nullptr;
        if (!(Enc->GetPhase() == EPYREncounterPhase::Fight && BC && BC->IsSettled())) { if (Test.Seconds - St.T0 > 30) M4_FAIL(TEXT("%s: the fight never settled"), *V.Name); return EGameTest::Running; }
    }
    M4_WAIT(V.Battle ? 2.0 : 1.2);
    const double Since = W->GetTimeSeconds() - .25;
    Checked += PYRM4V::CheckDrawn(W, V.Name, Offenders, Since);
    if (V.Battle && Enc->IsBusy()) if (APYRBattleManager* Mgr = APYRBattleManager::Get(W)) Mgr->PressRun();
    ++St.I;
    St.Step = 0;
    St.T0 = Test.Seconds;
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ the designer's shots
#include "Tests/M4VShots.h"
#include "Tests/M4UTests.h"

namespace PYRM4VShots
{
bool Tick(UObject* Ctx, const FString& Name, int32& Step, double& Clock)
{
    if (Name == TEXT("m4v-battle-stage")) return PYRM4UShots::Tick(Ctx, TEXT("m4u-phone-battle"), Step, Clock);
    FString Frame;
    if (Name == TEXT("m4v-pallet-town")) Frame = TEXT("pallet-town");
    else if (Name == TEXT("m4v-viridian-forest")) Frame = TEXT("viridian-forest");
    else if (Name == TEXT("m4v-mt-moon")) Frame = TEXT("mt-moon");
    else return false;
    static int32 Stage = 0;
    static double StageAt = 0;
    static bool bReady = false;
    static PYRM4V::FFrame F;
    UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
    if (!W || bReady) return true;
    GameTest::HoldShot(1.0);
    if (Step == 0)
    {
        TMap<FString, PYRM4V::FFrame> Frames;
        FString Error, Note;
        if (!PYRM4V::LoadFrames(Frames, Error, Note) || !Frames.Contains(Frame) || !Frames[Frame].bCamera)
        {
            UE_LOG(LogTemp, Error, TEXT("PYR_SHOT_FAILED:%s no camera for frame %s %s"), *Name, *Frame, *Error);
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return true;
        }
        F = Frames[Frame];
        M4::Quiet(true, true);
        Step = 1;
        return true;
    }
    if (Step == 1)
    {
        if (!PYRStageM4Look(W, Frame, Clock, Stage, StageAt)) return true;
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ACameraActor* Cam = W->SpawnActor<ACameraActor>(F.Location, F.Rotation, Params);
        Cam->Tags.Add(TEXT("PYR.FollowRed"));
        Cam->GetCameraComponent()->SetFieldOfView(F.Fov);
        Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
        if (APlayerController* PC = W->GetFirstPlayerController()) PC->SetViewTarget(Cam);
        Step = 2;
        return true;
    }
    const UPYRAreaLook* Look = UPYRAreaLook::Get(W);
    if (Clock < 8.0 || !Look || !Look->IsSettled()) return true;
    bReady = true;
    UE_LOG(LogTemp, Display, TEXT("M4V_SHOT %s staged as frame %s, area %s"), *Name, *Frame, *Look->GetArea().ToString());
    return true;
}
}
