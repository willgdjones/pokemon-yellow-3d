#include "Stage/PYREncounters.h"
#include "Menus/PYRSysText.h"

#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Encounters/PYRBattleCamera.h"
#include "Encounters/PYRRing.h"
#include "Engine/World.h"
#include "GameTest.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Music/GbMusicPlayer.h"
#include "Partner/PYRPikachuFollower.h"
#include "World/PYRPaint.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "World/CoreSeams.h"

UPYREncounters* UPYREncounters::Get(const UObject* Ctx)
{
    UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
    return W ? W->GetSubsystem<UPYREncounters>() : nullptr;
}

void UPYREncounters::Start()
{
    if (bStarted) return;
    bStarted = true;
    Rng.Initialize(GameTest::Automated() ? 0x5eed : int32(FPlatformTime::Cycles()));
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (int32 K = 0; K < int32(EPYRStageKind::Count); ++K)
    {
        APYRBattleStage* S = GetWorld()->SpawnActor<APYRBattleStage>(APYRBattleStage::CentreFor(EPYRStageKind(K)), FRotator::ZeroRotator, Params);
        S->Build(EPYRStageKind(K));
        Stages.Add(S);
    }
    if (UPYRField* Field = UPYRField::Get(this)) Field->OnTileChanged.AddUObject(this, &UPYREncounters::OnTile);
    if (APYRBattleManager* BM = APYRBattleManager::Get(this)) BM->OnBattleEnded.AddUObject(this, &UPYREncounters::OnBattleEnded);
}

APYRBattleStage* UPYREncounters::GetStage(EPYRStageKind Kind) const
{
    for (const TObjectPtr<APYRBattleStage>& S : Stages) if (S && S->Kind == Kind) return S;
    return nullptr;
}

EPYRStageKind UPYREncounters::StageForMap(const FYMap* Map)
{
    if (!Map) return EPYRStageKind::Grass;
    const FString Id = Map->Id.ToString();
    if (Id == TEXT("PEWTER_GYM")) return EPYRStageKind::PewterGym;
    if (Id == TEXT("CERULEAN_GYM")) return EPYRStageKind::CeruleanGym;
    if (Map->bCave || Id.StartsWith(TEXT("MT_MOON_")) && !Id.Contains(TEXT("POKECENTER")) || Id.Contains(TEXT("CAVE"))) return EPYRStageKind::Cave;
    if (Id == TEXT("VIRIDIAN_FOREST") || Map->Tileset.ToUpper() == TEXT("FOREST")) return EPYRStageKind::Forest;
    if (!Map->bOutdoor) return EPYRStageKind::Indoor;
    return EPYRStageKind::Grass;
}

// ------------------------------------------------------------------------------------------------ rolls
int32 UPYREncounters::SlotFor(int32 Roll)
{
    // source: pokeyellow data/wild/probabilities.asm: 51, 51, 39, 25, 25, 25, 13, 13, 11, 3 (of 256).
    static const int32 Cumulative[] = {51, 102, 141, 166, 191, 216, 229, 242, 253, 256};
    for (int32 I = 0; I < 10; ++I) if (Roll < Cumulative[I]) return I;
    return 9;
}

int32 UPYREncounters::RateFor(const FYellowDataSet& Data, const FYMap& Map)
{
    if (Map.EncounterRate > 0) return Map.EncounterRate;
    const FYWild* W = Data.Wild.Find(Map.Id);
    return W ? W->GrassRate : 0;
}

bool UPYREncounters::RollStep(const FYellowDataSet& Data, const FYMap& Map, TCHAR Tile, FRandomStream& Rng, FYSlot& Out)
{
    const bool bGrass = Tile == TEXT('g') || (Tile == TEXT('c') && Map.bCave);
    if (!bGrass) return false;
    const FYWild* W = Data.Wild.Find(Map.Id);
    if (!W || W->Grass.Num() == 0) return false;
    if (Rng.RandRange(0, 255) >= RateFor(Data, Map)) return false;
    const int32 Slot = FMath::Min(SlotFor(Rng.RandRange(0, 255)), W->Grass.Num() - 1);
    Out = W->Grass[Slot];
    return true;
}

bool UPYREncounters::RepelBlocks(const FYSlot& Slot) const
{
    const UPYRProgress* P = UPYRProgress::Get(this);
    const UPYRTrainerState* S = UPYRTrainerState::Get(this);
    if (!P || !S || P->RepelSteps <= 0) return false;
    const int32 Lead = S->FirstAble();
    return Lead != INDEX_NONE && Slot.Level < S->Party[Lead].Level;
}

void UPYREncounters::OnTile(FName MapId, FIntPoint Local)
{
    UPYRField* Field = UPYRField::Get(this);
    const UPYRTrainerState* State = UPYRTrainerState::Get(this);
    const AYellowWorld* W = Field ? Field->GetYellowWorld() : nullptr;
    const FYMap* M = W && W->GetData() ? W->GetData()->Map(MapId) : nullptr;
    if (!M || IsBusy() || Field->IsWarping() || Field->IsLocked() || PYRTestFlags::bNoEncounters) return;
    if (!State || State->FirstAble() == INDEX_NONE) return;
    const TCHAR T = M->Tile(Local.X, Local.Y);
    ++StepsChecked;
    FYSlot Slot;
    bool bMeet = RollStep(*W->GetData(), *M, T, Rng, Slot);
    if (bForceEncounter && (T == TEXT('g') || T == TEXT('c')))
    {
        const FYWild* Table = W->GetData()->Wild.Find(MapId);
        if (Table && Table->Grass.Num())
        {
            bMeet = true;
            Slot = Table->Grass[ForceSlot >= 0 ? FMath::Min(ForceSlot, Table->Grass.Num() - 1) : SlotFor(Rng.RandRange(0, 255))];
        }
    }
    if (!bMeet || RepelBlocks(Slot)) return;
    bForceEncounter = false;
    LastWild = Slot;
    BeginWild(Slot.Species, Slot.Level);
}

// ------------------------------------------------------------------------------------------------ fights
bool UPYREncounters::BeginWild(FName Species, int32 Level)
{
    FPYRFight F;
    F.Kind = EPYRBattleKind::Wild;
    FPYRMonSpec Spec;
    Spec.Species = Species;
    Spec.Level = Level;
    F.Foes.Add(Spec);
    return Begin(F);
}

bool UPYREncounters::Begin(const FPYRFight& InFight)
{
    APYRBattleManager* BM = APYRBattleManager::Get(this);
    UPYRField* Field = UPYRField::Get(this);
    if (IsBusy() || !BM || BM->IsBattleActive() || !Field || InFight.Foes.Num() == 0) return false;
    Fight = InFight;
    if (!Fight.bStageSet) Fight.Stage = StageForMap(Field->GetMap());
    ReturnMap = Field->CurrentMap;
    ReturnTile = Field->CurrentTile;
    ReturnFacing = Field->Facing;
    ++Encounters;
    Phase = EPYREncounterPhase::Flash;
    Clock = 0;
    FlashStep = -1;
    if (const UPYRTrainerState* State = UPYRTrainerState::Get(this)) { PartyBefore = State->Party.Num(); StorageBefore = State->Storage.Num(); }
    Field->Lock(TEXT("battle"));
    if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(this))
    {
        SongBefore = Music->GetSong();
        FName Theme = Fight.Music;
        if (Theme.IsNone())
            Theme = Fight.Kind == EPYRBattleKind::Wild || Fight.Kind == EPYRBattleKind::Demo ? FName(TEXT("wild_battle"))
                : Fight.Kind == EPYRBattleKind::Gym ? FName(TEXT("gym_leader_battle")) : FName(TEXT("trainer_battle"));
        Music->Play(Theme, .1f);
    }
    UE_LOG(LogYellow, Display, TEXT("YELLOW_ENCOUNTER kind=%d foe=%s L%d stage=%s from %s (%d,%d)"), int32(Fight.Kind), *Fight.Foes[0].Species.ToString(),
        Fight.Foes[0].Level, APYRBattleStage::Name(Fight.Stage), *ReturnMap.ToString(), ReturnTile.X, ReturnTile.Y);
    return true;
}

void UPYREncounters::StartFight()
{
    APYRBattleManager* BM = APYRBattleManager::Get(this);
    APYRBattleStage* Stage = GetStage(Fight.Stage);
    APokemonYellowReimagineCharacter* Red = UPYRField::Get(this) ? UPYRField::Get(this)->Red() : nullptr;
    if (!BM || !Stage || !Red) { Phase = EPYREncounterPhase::Idle; if (UPYRField* F = UPYRField::Get(this)) F->Unlock(TEXT("battle")); return; }
    CurrentStage = Stage;
    Red->PlaceAt(Stage->RedSpot(), 0.f);
    // The trainer across the stage from Red, facing him (two side by side for Jessie and James).
    if (!Fight.TrainerModel.IsEmpty() && Fight.Kind != EPYRBattleKind::Wild)
    {
        FActorSpawnParameters P;
        P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector At = Stage->Kind == EPYRStageKind::CeruleanGym ? Stage->Centre + FVector(1700.f, 0, 0) : Stage->Centre + FVector(Stage->Radius - 120.f, 0, 0);
        AActor* T = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FRotator(0, 180, 0), At), P);
        if (T)
        {
            USceneComponent* Root = NewObject<USceneComponent>(T, TEXT("Root"));
            T->SetRootComponent(Root);
            Root->RegisterComponent();
            T->SetActorLocationAndRotation(At, FRotator(0, 180, 0));
            TArray<FString> Models;
            Fight.TrainerModel.ParseIntoArray(Models, TEXT(","));
            for (int32 I = 0; I < Models.Num(); ++I)
            {
                const float Side = Models.Num() > 1 ? (I - (Models.Num() - 1) * .5f) * 120.f : 0.f;
                USceneComponent* Slot = PYR::Group(T, Root, FVector(0, Side, 0));
                PYRSprites::SpawnModel(T, Slot, Models[I].TrimStartAndEnd(), Fight.TrainerHeight, false);
            }
            T->Tags.Add(TEXT("StageTrainer"));
            TrainerVisual = T;
        }
    }
    // The ring over the stage: closed for trainers, a worn gap on Red's side for wild Pokemon.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const bool bGap = Fight.Kind == EPYRBattleKind::Wild;
    APYRRing* NewRing = GetWorld()->SpawnActor<APYRRing>(Stage->Centre, FRotator::ZeroRotator, Params);
    NewRing->Setup(Stage->Centre, Stage->Radius, bGap, Stage->Centre + FVector(-Stage->Radius, 0, 0));
    NewRing->Rise(.6f);
    Ring = NewRing;
    GapTime = 0.f;
    FPYRBattleSetup Setup;
    Setup.Kind = Fight.Kind;
    Setup.Foes = Fight.Foes;
    Setup.TrainerId = Fight.TrainerId.IsNone() && !Fight.TrainerName.IsEmpty() ? FName(*Fight.TrainerName) : Fight.TrainerId;
    Setup.TrainerTitle = Fight.TrainerName;
    Setup.Prize = Fight.Prize;
    Setup.Center = Stage->Centre;
    Setup.Radius = Stage->Radius;
    Setup.bNoBlackout = Fight.bNoBlackout;
    Setup.MaxHoverHeightCm = Stage->HoverLimit;
    BM->StartBattle(Setup);
    // The high camera over the stage, looking from Red's side.
    APYRBattleCamera* Cam = GetWorld()->SpawnActor<APYRBattleCamera>(Stage->Centre, FRotator::ZeroRotator, Params);
    Cam->Frame(Stage->Centre, Stage->Radius, 0.f, Stage->IsGym());
    Cam->SnapToFinal();
    Camera = Cam;
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        PC->bAutoManageActiveCameraTarget = false;
        PC->SetViewTarget(Cam);
    }
    Phase = EPYREncounterPhase::Fight;
}

void UPYREncounters::OnBattleEnded(EPYRBattleOutcome Outcome)
{
    if (Phase != EPYREncounterPhase::Fight) return;
    if (AActor* T = TrainerVisual.Get()) T->Destroy();
    TrainerVisual = nullptr;
    LastOutcome = Outcome;
    Phase = EPYREncounterPhase::After;
    if (APYRRing* R = Ring.Get()) R->FadeOut();
    UGbMusicPlayer* Music = UGbMusicPlayer::Get(this);
    UPYRMenus* Menus = UPYRMenus::Get(this);
    UPYRTrainerState* State = UPYRTrainerState::Get(this);
    AfterQueue.Reset();
    TWeakObjectPtr<UPYREncounters> Self = this;
    if (Outcome == EPYRBattleOutcome::Lost)
    {
        if (!Fight.bNoBlackout) { Blackout(); return; }
        if (State) State->HealParty();
        if (!Fight.LossText.IsEmpty()) AfterQueue.Add([Self, Menus, Text = Fight.LossText](TFunction<void()> Next) { if (Menus) Menus->Say(Text, Next); else Next(); });
    }
    if (Outcome == EPYRBattleOutcome::Won && Music)
        Music->Play(Fight.Kind == EPYRBattleKind::Gym ? TEXT("defeated_gym_leader") : Fight.Kind == EPYRBattleKind::Wild ? TEXT("defeated_wild_mon") : TEXT("defeated_trainer"), .2f);
    if (Outcome == EPYRBattleOutcome::Won && Fight.Kind != EPYRBattleKind::Wild && Fight.Kind != EPYRBattleKind::Demo)
    {
        if (!Fight.WinText.IsEmpty()) AfterQueue.Add([Menus, Text = Fight.WinText](TFunction<void()> Next) { if (Menus) Menus->Say(Text, Next); else Next(); });
        if (Fight.Prize > 0)
            AfterQueue.Add([Menus, Prize = Fight.Prize](TFunction<void()> Next)
            {
                if (Menus) Menus->Say(FString::Printf(TEXT("<PLAYER> got %s\nfor winning!"), *PYRGb::Money(Prize)), Next); else Next();
            });
    }
    if (Outcome == EPYRBattleOutcome::Caught && State)
    {
        // The new Pokemon: the last in the party, or the last stored when the party was full.
        AfterQueue.Add([Self, Menus, State](TFunction<void()> Next)
        {
            if (!Self.IsValid() || !Menus) { Next(); return; }
            const bool bParty = State->Party.Num() > Self->PartyBefore;
            const FName Species = Self->Fight.Foes[0].Species;
            if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(Self.Get())) Music->Jingle(TEXT("caught_mon"));
            Menus->Say(FString::Printf(TEXT("All right!\n%s was\fcaught!"), *PYRScreens::SpeciesName(Species)), [Self, Menus, State, bParty, Species, Next]()
            {
                PYRScreens::OfferNickname(Self.Get(), bParty ? State->Party.Num() - 1 : State->Storage.Num() - 1, !bParty, [Self, Menus, bParty, Species, Next]()
                {
                    // Yellow's ItemUseBallText07/08: BILL's PC once Red has met him, someone's before.
                    const UPYRProgress* Prog = UPYRProgress::Get(Self.Get());
                    if (!bParty && Menus)
                        Menus->Say(PYRSysText::Get(Prog && Prog->Has(TEXT("EVENT_MET_BILL")) ? TEXT("ItemUseBallText07") : TEXT("ItemUseBallText08"),
                            {{TEXT("wBoxMonNicks"), PYRScreens::SpeciesName(Species)}}), Next);
                    else Next();
                });
            });
        });
    }
    // A fifth move (the Game Boy forget screen), then the evolution scene, both on the stage.
    AfterQueue.Add([Self](TFunction<void()> Next) { PYRScreens::LearnPending(Self.Get(), Next); });
    AfterQueue.Add([Self](TFunction<void()> Next)
    {
        const APYRBattleStage* Stage = Self.IsValid() ? Self->CurrentStage.Get() : nullptr;
        PYRScreens::EvolvePending(Self.Get(), Stage ? Stage->Centre : FVector::ZeroVector, 180.f, Next);
    });
    Clock = 0;
    NextAfter();
}

void UPYREncounters::NextAfter()
{
    if (AfterQueue.Num() == 0) { ReturnToField(); return; }
    TFunction<void(TFunction<void()>)> Step = AfterQueue[0];
    AfterQueue.RemoveAt(0);
    TWeakObjectPtr<UPYREncounters> Self = this;
    Step([Self]() { if (Self.IsValid()) Self->NextAfter(); });
}

void UPYREncounters::ReturnToField()
{
    Phase = EPYREncounterPhase::Return;
    Clock = 0;
    if (UPYRField* Field = UPYRField::Get(this)) Field->Fade(1.f, .25f, FLinearColor::White);
}

void UPYREncounters::Blackout()
{
    Phase = EPYREncounterPhase::Blackout;
    ++Blackouts;
    TWeakObjectPtr<UPYREncounters> Self = this;
    if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(this)) Music->Stop(.5f);
    UPYRMenus* Menus = UPYRMenus::Get(this);
    auto Wake = [Self]()
    {
        if (!Self.IsValid()) return;
        UPYRField* Field = UPYRField::Get(Self.Get());
        UPYRTrainerState* State = UPYRTrainerState::Get(Self.Get());
        if (State) { State->HealParty(); State->HalveMoney(); }
        // Wake at the last Pokemon Centre used (in front of its nurse), else at home.
        FName WakeMap = State && !State->LastCentre.IsNone() ? State->LastCentre : FName(TEXT("REDS_HOUSE_1F"));
        const AYellowWorld* W = Field ? Field->GetYellowWorld() : nullptr;
        const FYMap* M = W && W->GetData() ? W->GetData()->Map(WakeMap) : nullptr;
        if (!M && W && W->GetData()) { WakeMap = TEXT("PALLET_TOWN"); M = W->GetData()->Map(WakeMap); }
        FIntPoint At(M ? M->Width / 2 : 0, M ? M->Height - 2 : 0);
        if (Field)
            for (const TWeakObjectPtr<APYRPerson>& P : Field->People)
                if (P.IsValid() && P->Map == WakeMap && (P->Sprite.ToString().Contains(TEXT("NURSE")) || P->Sprite.ToString().Contains(TEXT("MOM"))) && M)
                    At = P->Tile - M->Origin + FIntPoint(0, P->Sprite.ToString().Contains(TEXT("NURSE")) ? 2 : 1);
        Self->ReturnMap = WakeMap;
        Self->ReturnTile = At;
        Self->ReturnFacing = EYDir::Up;
        if (APYRPikachuFollower* Pika = APYRPikachuFollower::Find(Self.Get())) Pika->Worried(6.f);
        Self->ReturnToField();
    };
    const FString Line = TEXT("<PLAYER> is out of\nuseable POKéMON!\f<PLAYER> blacked\nout!");
    if (Menus) Menus->Say(Line, Wake); else Wake();
}

void UPYREncounters::Finish()
{
    UPYRField* Field = UPYRField::Get(this);
    if (APYRBattleCamera* Cam = Camera.Get()) { Cam->Release(); Cam->Destroy(); }
    Camera = nullptr;
    if (APYRRing* R = Ring.Get()) R->Destroy();
    Ring = nullptr;
    if (Field)
    {
        Field->PlaceRed(ReturnMap, ReturnTile.X, ReturnTile.Y, ReturnFacing);
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
        {
            PC->bAutoManageActiveCameraTarget = true;
            if (APawn* R = Field->Red()) { if (PC->GetPawn() != R) PC->Possess(R); PC->SetViewTarget(R); }
        }
        // The place's own song again.
        if (const FYMap* M = Field->GetMap()) if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(this)) Music->Play(FName(*M->Music), .3f);
        Field->Fade(0.f, .3f, FLinearColor::White);
        Field->Unlock(TEXT("battle"));
    }
    ++Returns;
    Phase = EPYREncounterPhase::Idle;
    UE_LOG(LogYellow, Display, TEXT("YELLOW_ENCOUNTER_END outcome=%d back at %s (%d,%d)"), int32(LastOutcome), *ReturnMap.ToString(), ReturnTile.X, ReturnTile.Y);
    TFunction<void(EPYRBattleOutcome)> Done = MoveTemp(Fight.OnDone);
    Fight.OnDone = nullptr;
    if (Done) Done(LastOutcome);
}

void UPYREncounters::Tick(float DeltaTime)
{
    if (Phase == EPYREncounterPhase::Idle) return;
    Clock += DeltaTime;
    UPYRField* Field = UPYRField::Get(this);
    switch (Phase)
    {
    case EPYREncounterPhase::Flash:
    {
        // Three white flashes (0.16 s each), then the wipe to black (0.35 s), then the stage.
        const int32 Step = FMath::FloorToInt(Clock / .08);
        if (Field && Step < 6 && Step != FlashStep)
        {
            FlashStep = Step;
            const bool bOn = Step % 2 == 0;
            Field->Fade(bOn ? 1.f : 0.f, .05f, FLinearColor::White);
            if (bOn) ++Flashes;
        }
        else if (Field && Step >= 6 && FlashStep < 6)
        {
            FlashStep = 6;
            Field->Fade(1.f, .35f, FLinearColor::Black);
        }
        if (Clock >= .86)
        {
            StartFight();
            if (Field) Field->Fade(0.f, .3f, FLinearColor::Black);
        }
        break;
    }
    case EPYREncounterPhase::Fight:
    {
        APYRBattleManager* BM = APYRBattleManager::Get(this);
        if (!BM) break;
        TArray<FVector> Subjects;
        if (APYRMonPawn* P = BM->GetPlayerMon()) Subjects.Add(P->GetActorLocation());
        if (APYRMonPawn* F = BM->GetFoeMon()) Subjects.Add(F->GetActorLocation());
        if (ACharacter* R = BM->GetRed()) Subjects.Add(R->GetActorLocation());
        if (APYRBattleCamera* Cam = Camera.Get()) Cam->SetSubjects(Subjects);
        // The worn gap: 3 s standing in it flees a wild battle (m4t: its timer holds while the battle is paused).
        APYRRing* R = Ring.Get();
        APYRMonPawn* P = BM->GetPlayerMon();
        if (R && R->HasGap() && P && BM->IsBattleActive() && !BM->IsEnding() && !BM->IsBattlePaused())
        {
            if (R->IsInGap(P->GetActorLocation())) GapTime += DeltaTime;
            else GapTime = FMath::Max(0.f, GapTime - DeltaTime * 2.f);
            R->SetFill(GapTime / 3.f);
            if (GapTime >= 3.f)
            {
                GapTime = 0.f;
                if (UPYRMenus* Menus = UPYRMenus::Get(this)) Menus->TextLog.Add(TEXT("Got away safely!"));
                BM->Caption(TEXT("Got away safely!"));
                BM->RequestFlee();
            }
        }
        break;
    }
    case EPYREncounterPhase::Return:
        if (Clock > .3 || (Field && Field->GetFade() >= .99f)) Finish();
        break;
    default: break;
    }
}
