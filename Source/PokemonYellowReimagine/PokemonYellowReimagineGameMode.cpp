#include "PokemonYellowReimagineGameMode.h"

#include "Battle/PYRBattleManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameTest.h"
#include "GameFramework/PlayerController.h"
#include "Hud/PYRHud.h"
#include "Kanto/PYRField.h"
#include "Library/FollowCameraComponent.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menu/PYRPlayerController.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Music/GbMusicPlayer.h"
#include "Partner/PYRPikachuFollower.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Save/PYRSave.h"
#include "Stage/PYREncounters.h"
#include "Tests/M4TShots.h"
#include "Tests/M4UTests.h"
#include "Tests/M4VShots.h"
#include "World/CoreSeams.h"
#include "World/PYRPaint.h"
#include "Yellow/YellowData.h"

APokemonYellowReimagineGameMode::APokemonYellowReimagineGameMode()
{
    DefaultPawnClass = APokemonYellowReimagineCharacter::StaticClass();
    PlayerControllerClass = APYRPlayerController::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
}

void APokemonYellowReimagineGameMode::BeginPlay()
{
    Super::BeginPlay();
    UWorld* W = GetWorld();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    UYellowData* Data = UYellowData::Get(this);
    TSharedPtr<FYellowDataSet> Set = Data ? Data->Set : nullptr;
    GYellowPrices = Set.Get();
    // The whole Game Boy stretch, built here in this one map: no second level, no streaming, no loading screen.
    World = W->SpawnActor<AYellowWorld>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    World->Build(Set, FVector::ZeroVector, true);
    if (!Set.IsValid() || !Set->IsLoaded())
    {
        // Without YellowData: a flat meadow so Red stands somewhere (the log says what is missing).
        AStaticMeshActor* Floor = W->SpawnActor<AStaticMeshActor>(FVector(0, 0, -50), FRotator::ZeroRotator, Params);
        Floor->SetMobility(EComponentMobility::Movable);
        Floor->GetStaticMeshComponent()->SetStaticMesh(PYR::Mesh(PYR::EShape::Cube));
        Floor->GetStaticMeshComponent()->SetMaterial(0, UPYRPaint::Get(this, PYRColor::Grass));
        Floor->SetActorScale3D(FVector(40, 40, 1));
        Floor->Tags.Add(TEXT("FallbackMeadow"));
    }
    Pikachu = W->SpawnActor<APYRPikachuFollower>(FVector(0, 0, 30), FRotator::ZeroRotator, Params);
    Pikachu->SetActorHiddenInGame(true);
    if (UPYRField* Field = UPYRField::Get(this)) Field->Start(World);
    if (UPYREncounters* Enc = UPYREncounters::Get(this)) Enc->Start();
    if (UPYRHud* Hud = UPYRHud::Get(this)) Hud->Start();

    ShotName = GameTest::ShotName();
    if (GameTest::Automated()) StartNewGame();
    else StartTitle();
    GameTest::Start(W);
}

void APokemonYellowReimagineGameMode::StartTitle()
{
    // A player at the keyboard: the title over Red's room.
    StartNewGame();
    UPYRSave* Save = UPYRSave::Get(this);
    UPYRField* Field = UPYRField::Get(this);
    if (Field) Field->Lock(TEXT("title"));
    TWeakObjectPtr<APokemonYellowReimagineGameMode> Self = this;
    PYRScreens::Title(this, Save && Save->CanContinue(), [Self](bool bContinue)
    {
        if (!Self.IsValid()) return;
        UPYRField* Field = UPYRField::Get(Self.Get());
        UPYRSave* Save = UPYRSave::Get(Self.Get());
        FPYRSaveGame Loaded;
        if (bContinue && Save && Save->Load(Loaded) == 0 && Save->Apply(Loaded))
        {
            if (Field) Field->Unlock(TEXT("title"));
            PYRScripts::Refresh(Self.Get());
            return;
        }
        PYRScreens::Intro(Self.Get(), [Self]()
        {
            if (!Self.IsValid()) return;
            if (UPYRField* F = UPYRField::Get(Self.Get())) F->Unlock(TEXT("title"));
            PYRScripts::Refresh(Self.Get());
        });
    });
}

void APokemonYellowReimagineGameMode::StartNewGame()
{
    if (UPYRSave* Save = UPYRSave::Get(this)) Save->ResetToNewGame();
    UPYRField* Field = UPYRField::Get(this);
    if (Field && World && World->GetData() && World->GetData()->Map(TEXT("REDS_HOUSE_2F")))
        Field->PlaceRed(TEXT("REDS_HOUSE_2F"), StartX, StartY, EYDir::Up);
    else if (APokemonYellowReimagineCharacter* Red = APokemonYellowReimagineCharacter::Find(this))
        Red->PlaceAt(FVector(50, 50, 0), 0.f);
    PYRScripts::Refresh(this);
    if (Field)
        if (const FYMap* M = Field->GetMap())
            if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(this)) Music->Play(FName(*M->Music), 0.f);
}

void APokemonYellowReimagineGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!ShotName.IsEmpty()) TickShot(DeltaSeconds);
}

// ------------------------------------------------------------------------------------------------ the m4 shots
void APokemonYellowReimagineGameMode::TickShot(float DeltaSeconds)
{
    UPYRField* Field = UPYRField::Get(this);
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    UPYRProgress* Progress = UPYRProgress::Get(this);
    UPYRMenus* Menus = UPYRMenus::Get(this);
    APokemonYellowReimagineCharacter* Red = APokemonYellowReimagineCharacter::Find(this);
    if (!Field || !State || !Progress || !Menus || !Red || !World || !World->GetData()) return;
    ShotClock += DeltaSeconds;
    // m4t's battle-control shots (Tests/M4TShots).
    if (PYRM4TShots::Tick(this, ShotName, ShotStep, ShotClock)) return;
    // m4u's phone-layout shots (Tests/M4UTests).
    if (PYRM4UShots::Tick(this, ShotName, ShotStep, ShotClock)) return;
    // m4v's designer shots of the anime look (Tests/M4VShots).
    if (PYRM4VShots::Tick(this, ShotName, ShotStep, ShotClock)) return;
    auto Party = [State](std::initializer_list<TPair<const TCHAR*, int32>> Mons)
    {
        TArray<FPYRMonSpec> Specs;
        for (const TPair<const TCHAR*, int32>& M : Mons) { FPYRMonSpec S; S.Species = M.Key; S.Level = M.Value; Specs.Add(S); }
        State->DebugSetParty(Specs);
    };
    if (ShotStep == 0)
    {
        ShotStep = 1;
        // m4s: the views past the route edges (Kanto's far scenery) and Brock's closed hall, as named shots.
        if (ShotName == TEXT("edge-route22-league")) ShotName = TEXT("at~ROUTE_22~17~4~-105~-5");
        else if (ShotName == TEXT("edge-route25-sea")) ShotName = TEXT("at~ROUTE_25~40~2~-50~-5");
        else if (ShotName == TEXT("edge-route4-mtmoon")) ShotName = TEXT("at~ROUTE_4~6~8~-165~-5");
        else if (ShotName == TEXT("edge-route24-west")) ShotName = TEXT("at~ROUTE_24~4~18~-165~-5");
        // Inside the gym, the walking camera at its highest pitch (20 degrees up) looking north at the ceiling over Brock.
        else if (ShotName == TEXT("pewter-gym-roof")) ShotName = TEXT("at~PEWTER_GYM~4~9~-90~20");
        else if (ShotName == TEXT("pewter-gym-battle")) ShotName = TEXT("brock-battle");
        if (ShotName == TEXT("pallet-town-3d"))
        {
            // A new game after naming: Red south of Oak's lab facing north, Pikachu beside him, the minimap showing.
            Party({{TEXT("pikachu"), 5}});
            Progress->Set(TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"));
            Progress->Set(TEXT("EVENT_GOT_STARTER"));
            PYRScripts::Refresh(this);
            Field->PlaceRed(TEXT("PALLET_TOWN"), 12, 14, EYDir::Up);
            // High and far enough to see over the lab to both houses.
            if (Red->FollowCamera) Red->FollowCamera->FollowArmLength = 1500.f;
            if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->SetControlRotation(FRotator(-50, -100, 0));
            GameTest::HoldShot(2.5);
        }
        else if (ShotName == TEXT("start-menu"))
        {
            // Viridian City with the Pokedex, Pikachu and one caught Pidgey; Enter opens START, the cursor on POKéMON.
            Party({{TEXT("pikachu"), 9}, {TEXT("pidgey"), 4}});
            for (const TCHAR* F : {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL"),
                     TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")})
                Progress->Set(F);
            State->MarkSeen(TEXT("rattata"));
            PYRScripts::Refresh(this);
            Field->PlaceRed(TEXT("VIRIDIAN_CITY"), 23, 27, EYDir::Up);
            if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->SetControlRotation(FRotator(-20, -90, 0));
            GameTest::HoldShot(3.0);
        }
        else if (ShotName == TEXT("viridian-mart-buy"))
        {
            // Inside the Viridian Mart after the parcel went to Oak: the BUY list, the money, the clerk at the counter.
            Party({{TEXT("pikachu"), 8}});
            for (const TCHAR* F : {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL"),
                     TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")})
                Progress->Set(F);
            PYRScripts::Refresh(this);
            Field->PlaceRed(TEXT("VIRIDIAN_MART"), 2, 5, EYDir::Left);
            if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->SetControlRotation(FRotator(-34, 238, 0));   // north-west: the clerk left of the BUY box
            GameTest::HoldShot(3.0);
        }
        else if (ShotName == TEXT("route1-battle-stage"))
        {
            // A Route 1 grass encounter, forced: the flash, then Pikachu and a wild Pidgey on the grass stage.
            Party({{TEXT("pikachu"), 6}});
            State->SetItemCount(UPYRTrainerState::Potion, 3);
            State->SetItemCount(UPYRTrainerState::PokeBall, 5);
            for (const TCHAR* F : {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER")}) Progress->Set(F);
            PYRScripts::Refresh(this);
            Field->PlaceRed(TEXT("ROUTE_1"), 9, 26, EYDir::Up);
            GameTest::HoldShot(4.5);
        }
        else if (ShotName == TEXT("opening-oak") || ShotName == TEXT("naming-typed") || ShotName == TEXT("opening-rival"))
        {
            // New Game: Oak's 3D stage ("Hello there!"), or the naming panel beside Red after typing a name.
            PYRScreens::Intro(this, nullptr);
            GameTest::HoldShot(ShotName == TEXT("opening-oak") ? 4.5 : 7.0);
        }
        else if (ShotName == TEXT("route1-north-edge"))
        {
            // Red on Route 1's northernmost walkable tile facing north: Viridian City's houses and trees past the seam.
            Party({{TEXT("pikachu"), 6}});
            for (const TCHAR* F : {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB")}) Progress->Set(F);
            PYRScripts::Refresh(this);
            const FYMap* R1 = World->GetData()->Map(TEXT("ROUTE_1"));
            int32 X = 10;
            for (int32 I = 0; R1 && I < R1->Width; ++I)
                if (!YTile::IsSolid(R1->Tile(I, 0)) && R1->Tile(I, 0) != TEXT('g') && FMath::Abs(I - R1->Width / 2) < FMath::Abs(X - R1->Width / 2)) X = I;
            Field->PlaceRed(TEXT("ROUTE_1"), X, 0, EYDir::Up);
            if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->SetControlRotation(FRotator(-16, -90, 0));
            GameTest::HoldShot(3.0);
        }
        else if (ShotName == TEXT("brock-battle"))
        {
            // Pewter Gym: Red in front of Brock with Pikachu (Thundershock first), ready for the gym fight.
            FPYRMonSpec Pika;
            Pika.Species = TEXT("pikachu");
            Pika.Level = 12;
            Pika.Moves = {PYRData::ThunderShock, PYRData::Growl, PYRData::ThunderWave};
            State->DebugSetParty({Pika});
            for (const TCHAR* F : {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL"),
                     TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")})
                Progress->Set(F);
            PYRScripts::Refresh(this);
            Field->PlaceRed(TEXT("PEWTER_GYM"), 4, 3, EYDir::Up);
            GameTest::HoldShot(6.0);
        }
        else if (ShotName == TEXT("minimap-people"))
        {
            // Viridian City: Red out of the Pokemon Centre's door, then two tiles east (step 1) towards the youngster
            // by the Centre and the Mart; the minimap draws him, Pikachu and Red as their Game Boy sprites.
            Party({{TEXT("pikachu"), 9}});
            for (const TCHAR* F : {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL"),
                     TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")})
                Progress->Set(F);
            PYRTestFlags::bNoEncounters = true;
            PYRScripts::Refresh(this);
            if (APYRPerson* Boy = Field->FindPerson(TEXT("VIRIDIAN_CITY"), TEXT("VIRIDIANCITY_YOUNGSTER2"))) Boy->bFrozen = true;   // on his own tile, 30,25
            Field->PlaceRed(TEXT("VIRIDIAN_CITY"), 23, 26, EYDir::Down);
            if (Pikachu) Pikachu->SnapBehindRed();
            if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->SetControlRotation(FRotator(-28, -30, 0));   // north-east
            GameTest::HoldShot(4.5);
        }
        else if (ShotName == TEXT("minimap-people-route25"))
        {
            // Route 25 among its trainers, Pikachu behind Red: the minimap draws each as their own Game Boy sprite.
            Party({{TEXT("pikachu"), 14}});
            for (const TCHAR* F : {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL"),
                     TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")})
                Progress->Set(F);
            PYRTestFlags::bNoEncounters = true;   // no trainer walks up while the picture is taken
            PYRScripts::Refresh(this);
            Field->PlaceRed(TEXT("ROUTE_25"), 20, 6, EYDir::Up);
            if (Pikachu) Pikachu->SnapBehindRed();
            GameTest::HoldShot(3.0);
        }
        else if (ShotName.StartsWith(TEXT("at~")) || ShotName.StartsWith(TEXT("tour-")))
        {
            // tour-room-fixed: Red's room as the visual tour framed it (visual-001, the flat north wall).
            if (ShotName == TEXT("tour-room-fixed")) ShotName = TEXT("tour-REDS_HOUSE_2F-4-4");
            // A look at any tile for checking a map against the Game Boy's: at~MAP~X~Y~Yaw~Pitch[~Arm], or the QA visual
            // tour's own view of a map, tour-MAP-X-Y (Red facing north, the ordinary follow camera looking north). After
            // the Pokedex, no wild Pokemon or trainers; Red faces the camera's way.
            TArray<FString> P;
            if (ShotName.StartsWith(TEXT("tour-")))
            {
                ShotName.ParseIntoArray(P, TEXT("-"));
                P.SetNum(4);
                P.Add(TEXT("-90"));
                P.Add(TEXT("-22"));
            }
            else ShotName.ParseIntoArray(P, TEXT("~"));
            if (P.Num() >= 6)
            {
                Party({{TEXT("pikachu"), 16}});
                for (const TCHAR* F : {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL"),
                         TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")})
                    Progress->Set(F);
                PYRTestFlags::bNoEncounters = true;
                PYRTestFlags::bNoBlocks = true;
                PYRScripts::Refresh(this);
                const float Yaw = FCString::Atof(*P[4]);
                Field->PlaceRed(FName(*P[1]), FCString::Atoi(*P[2]), FCString::Atoi(*P[3]), YDir::FromYaw(Yaw));
                if (Pikachu) Pikachu->SnapBehindRed();
                if (P.Num() >= 7 && Red->FollowCamera) Red->FollowCamera->FollowArmLength = FCString::Atof(*P[6]);
                if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->SetControlRotation(FRotator(FCString::Atof(*P[5]), Yaw, 0));
                GameTest::HoldShot(3.0);
            }
            else ShotName.Reset();
        }
        else if (ShotName == TEXT("cerulean-east"))
        {
            // Cerulean's east edge, looking out along the road to Route 9: Route 9's own scenery past the seam.
            Party({{TEXT("pikachu"), 16}});
            for (const TCHAR* F : {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL"),
                     TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX"), TEXT("EVENT_BEAT_BROCK")})
                Progress->Set(F);
            PYRTestFlags::bNoEncounters = true;
            PYRScripts::Refresh(this);
            Field->PlaceRed(TEXT("CERULEAN_CITY"), 37, 17, EYDir::Right);
            if (Pikachu) Pikachu->SnapBehindRed();
            if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->SetControlRotation(FRotator(-24, 0, 0));
            GameTest::HoldShot(3.0);
        }
        else ShotName.Reset();
        return;
    }
    if (ShotStep == 1 && ShotClock > 1.2)
    {
        ShotStep = 2;
        if (ShotName == TEXT("pallet-town-3d"))
        {
            // Pikachu beside Red, facing the lab with him.
            if (Pikachu)
            {
                Pikachu->BeginScript();
                Pikachu->SetActorLocationAndRotation(World->TileCenter(TEXT("PALLET_TOWN"), 13, 14) + FVector(-20, -20, 23), FRotator(0, -90, 0));
            }
        }
        else if (ShotName == TEXT("start-menu"))
        {
            PYRScreens::OpenStart(this);
            Menus->Press(EPYRKey::Down);   // POKéDEX, then the cursor down to POKéMON
        }
        else if (ShotName == TEXT("viridian-mart-buy"))
        {
            PYRScreens::OpenMart(this, TEXT("VIRIDIAN_MART"));
            Menus->Press(EPYRKey::Confirm); // BUY
        }
        else if (ShotName == TEXT("route1-battle-stage"))
        {
            if (UPYREncounters* Enc = UPYREncounters::Get(this)) Enc->BeginWild(TEXT("pidgey"), 3);
        }
        else if (ShotName == TEXT("naming-typed") || ShotName == TEXT("opening-rival")) Menus->SkipTalk();   // Start: on to "First, what is your name?"
        else if (ShotName == TEXT("minimap-people"))
        {
            // The youngster wanders (WALK, ANY_DIR): two of his steps west bring him fully into the Game Boy's window.
            Red->ScriptWalkTo(World->TileCenter(TEXT("VIRIDIAN_CITY"), 25, 26));
            if (APYRPerson* Boy = Field->FindPerson(TEXT("VIRIDIAN_CITY"), TEXT("VIRIDIANCITY_YOUNGSTER2"))) Boy->Walk({EYDir::Left, EYDir::Left});
            ShotClock = 0;
        }
        else if (ShotName == TEXT("brock-battle"))
        {
            // Brock's fight as his trainer data gives it, from his Onix (the Thundershock picture needs the rock snake).
            UPYRScripts* Scripts = UPYRScripts::Get(this);
            UPYREncounters* Enc = UPYREncounters::Get(this);
            FPYRFight Fight;
            if (Scripts && Enc && Scripts->TrainerFight(Field->FindPerson(TEXT("PEWTER_GYM"), TEXT("PEWTERGYM_BROCK")), Fight))
            {
                Fight.Foes.RemoveAll([](const FPYRMonSpec& M) { return M.Species != FName(TEXT("onix")); });
                Fight.bNoBlackout = true;
                Enc->Begin(Fight);
            }
            ShotClock = 0;
        }
    }
    // The rival's turn: RED from the list, then on to "...Erm, what is his name again?".
    if (ShotStep == 2 && ShotName == TEXT("opening-rival"))
    {
        const TSharedPtr<FPYRScreen> Top = Menus->Top();
        if (Top.IsValid() && Top->Id == TEXT("choice") && !bShotTyped) { Top->Cursor = 1; Menus->Press(EPYRKey::Confirm); bShotTyped = true; }
        else if (Top.IsValid() && Top->Id == TEXT("text") && bShotTyped) Menus->SkipTalk();
    }
    // The naming shot: NEW NAME, then a name typed on the keyboard.
    if (ShotStep == 2 && ShotName == TEXT("naming-typed") && !bShotTyped)
    {
        const TSharedPtr<FPYRScreen> Top = Menus->Top();
        if (Top.IsValid() && Top->Id == TEXT("choice")) { Top->Cursor = 0; Menus->Press(EPYRKey::Confirm); }
        else if (Top.IsValid() && Top->Id == TEXT("naming")) { Menus->Type(TEXT("SATOSHI")); bShotTyped = true; }
    }
    // Brock's Onix holds still across from Pikachu; Thundershock (slot 1) until Yellow's "It doesn't affect ONIX..." shows.
    if (ShotStep == 2 && ShotName == TEXT("brock-battle"))
    {
        APYRBattleManager* Mgr = APYRBattleManager::Get(this);
        UPYREncounters* Enc = UPYREncounters::Get(this);
        APYRMonPawn* Mine = Mgr ? Mgr->GetPlayerMon() : nullptr;
        APYRMonPawn* Foe = Mgr ? Mgr->GetFoeMon() : nullptr;
        if (!Mgr || !Enc || Enc->GetPhase() != EPYREncounterPhase::Fight || !Mine || !Foe) { GameTest::HoldShot(2.0); return; }
        Mgr->bFoePassive = true;
        FString Said;
        for (const FString& Line : Mgr->CaptionLog) if (Line.Contains(TEXT("doesn't affect"))) Said = Line;
        // The picture with the caption band up: it stays 0.6 s old (the band holds 2.2 s) until the shot is taken.
        if (!Said.IsEmpty())
        {
            Mgr->CaptionText = Said;
            Mgr->CaptionAt = GetWorld()->GetTimeSeconds() - .6;
            if (!bShotTyped) { bShotTyped = true; GameTest::HoldShot(0.6); }
            return;
        }
        GameTest::HoldShot(0.4);
        if (ShotClock > 2.0 && ShotClock - ShotMoveAt > 2.5)
        {
            const FVector To = Foe->GetActorLocation() - Mine->GetActorLocation();
            Mine->SetActorRotation(FRotator(0, To.Rotation().Yaw, 0));
            Mgr->CaptionLog.Reset();
            Mgr->PressMove(0);
            ShotMoveAt = ShotClock;
        }
    }
    // The wild Pidgey holds its far platform for the picture (a live foe flies off it within a second or two).
    if (ShotStep == 2 && ShotName == TEXT("route1-battle-stage"))
        if (APYRBattleManager* Mgr = APYRBattleManager::Get(this)) Mgr->bFoePassive = true;
}

// The starter's own check: the game boots, a player exists and stands on the ground.
GAME_TEST(Boot, 30)
{
    APlayerController* PC = Test.World->GetFirstPlayerController();
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (Test.Seconds < 1.0) return EGameTest::Running;
    if (!Pawn) { Test.Detail = TEXT("no player pawn"); return EGameTest::Failed; }
    Test.Detail = FString::Printf(TEXT("pawn at %s"), *Pawn->GetActorLocation().ToCompactString());
    return Pawn->GetActorLocation().Z > -100 ? EGameTest::Passed : EGameTest::Failed;
}
