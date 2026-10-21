#include "Tests/M4TShots.h"

#include "Battle/BattleTestKit.h"
#include "Battle/PYRAttack.h"
#include "Battle/PYRBattleFx.h"
#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameTest.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Menus/PYRScreens.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRMoveLearning.h"
#include "Partner/PYRPikachuFollower.h"
#include "Party/PYRSpecies.h"
#include "Party/PYRTrainerState.h"
#include "Stage/PYREncounters.h"
#include "World/CoreSeams.h"
#include "Yellow/YellowData.h"

namespace
{
const TCHAR* const Story[] = {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL"),
    TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")};

// The walkable tile nearest a map's middle (no wall, no warp).
FIntPoint OpenMiddle(const FYMap* M)
{
    FIntPoint Best(M->Width / 2, M->Height / 2);
    int32 BestD = MAX_int32;
    for (int32 Y = 1; Y < M->Height - 1; ++Y)
        for (int32 X = 1; X < M->Width - 1; ++X)
        {
            if (YTile::IsSolid(M->Tile(X, Y)) || M->WarpAt(X, Y) != INDEX_NONE) continue;
            const int32 D = FMath::Square(X - M->Width / 2) + FMath::Square(Y - M->Height / 2);
            if (D < BestD) { BestD = D; Best = FIntPoint(X, Y); }
        }
    return Best;
}

APYRAttack* FoeAttack(UWorld* World)
{
    for (TActorIterator<APYRAttack> It(World); It; ++It)
        if (It->IsLive() && It->User.IsValid() && !It->User->bPlayerSide) return *It;
    return nullptr;
}

// m4u: the learn shot stages something it can't show: say why and end the run without a picture (the runner then fails
// it for want of PYR_SHOT_SAVED).
bool LearnShotFailed(const FString& Why)
{
    UE_LOG(LogTemp, Error, TEXT("PYR_SHOT_FAILED:m4t-learn-move %s"), *Why);
    FPlatformMisc::RequestExitWithStatus(false, 1);
    return true;
}
}

namespace PYRM4TShots
{
bool Tick(UObject* Ctx, const FString& Name, int32& Step, double& Clock)
{
    if (Name != TEXT("m4t-learn-move") && Name != TEXT("m4t-switch-paused") && Name != TEXT("m4t-run-wild") && Name != TEXT("m4t-run-trainer")) return false;
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    UPYRField* Field = UPYRField::Get(Ctx);
    UPYRTrainerState* State = UPYRTrainerState::Get(Ctx);
    UPYRProgress* Progress = UPYRProgress::Get(Ctx);
    UPYREncounters* Enc = UPYREncounters::Get(Ctx);
    UPYRMenus* Menus = UPYRMenus::Get(Ctx);
    APYRBattleManager* Mgr = APYRBattleManager::Get(Ctx);
    const FYellowDataSet* Data = UYellowData::Data(Ctx);
    if (!World || !Field || !State || !Progress || !Enc || !Menus || !Mgr || !Data) return true;
    const bool bFight = Enc->GetPhase() == EPYREncounterPhase::Fight && Mgr->IsBattleActive();
    // m4u: a failed check is on its way out: no picture of it.
    if (Step == 99) { GameTest::HoldShot(5.0); return true; }

    if (Step == 0)
    {
        Step = 1;
        Clock = 0;
        for (const TCHAR* F : Story) Progress->Set(F);
        PYRTestFlags::bNoEncounters = true;   // Nothing but the staged fight.
        PYRScripts::Refresh(Ctx);
        State->SetItemCount(UPYRTrainerState::Potion, 3);
        State->SetItemCount(UPYRTrainerState::PokeBall, 5);
        if (Name == TEXT("m4t-learn-move"))
        {
            FPYRMonSpec Pika;
            Pika.Species = PYRData::Pikachu;
            Pika.Level = 7;
            Pika.Moves = {PYRData::ThunderShock, PYRData::Growl, PYRData::TailWhip, PYRData::QuickAttack};
            State->DebugSetParty({Pika});
            State->Party[0].Xp = PYRData::XpForLevel(PYRData::Pikachu, 8) - 1;
            Field->PlaceRed(TEXT("ROUTE_1"), 9, 26, EYDir::Up);
        }
        else if (Name == TEXT("m4t-switch-paused"))
        {
            FPYRMonSpec A, B, C;
            A.Species = PYRData::Pikachu; A.Level = 8;
            B.Species = PYRData::Rattata; B.Level = 5;
            C.Species = PYRData::Caterpie; C.Level = 4;
            State->DebugSetParty({A, B, C});
            State->Party[2].Hp = 0;   // A fainted one shows as unavailable.
            Field->PlaceRed(TEXT("ROUTE_1"), 9, 26, EYDir::Up);
        }
        else if (Name == TEXT("m4t-run-wild"))
        {
            FPYRMonSpec Pika;
            Pika.Species = PYRData::Pikachu;
            Pika.Level = 9;
            State->DebugSetParty({Pika});
            Field->PlaceRed(TEXT("ROUTE_1"), 9, 26, EYDir::Up);
        }
        else
        {
            FPYRMonSpec Pika;
            Pika.Species = PYRData::Pikachu;
            Pika.Level = 12;
            Pika.Moves = {PYRData::ThunderShock, PYRData::Growl, PYRData::ThunderWave};
            State->DebugSetParty({Pika});
            if (const FYMap* M = Data->Map(TEXT("VIRIDIAN_FOREST")))
            {
                const FIntPoint At = OpenMiddle(M);
                Field->PlaceRed(M->Id, At.X, At.Y, EYDir::Up);
            }
        }
        GameTest::HoldShot(10.0);
        return true;
    }
    if (Step == 1)
    {
        GameTest::HoldShot(6.0);
        if (Clock < 1.2) return true;
        Step = 2;
        Clock = 0;
        // m4u: a fixed seed, so the wild fight starts the same way every run.
        FMath::RandInit(4242);
        FMath::SRandInit(4242);
        if (Name == TEXT("m4t-learn-move") || Name == TEXT("m4t-run-wild")) Enc->BeginWild(PYRData::Pidgey, 3);
        else if (Name == TEXT("m4t-switch-paused")) Enc->BeginWild(PYRData::Pidgey, 4);
        else
        {
            // Viridian Forest's Bug Catcher, as his trainer data gives him.
            UPYRScripts* Scripts = UPYRScripts::Get(Ctx);
            FPYRFight Fight;
            if (Scripts && Scripts->TrainerFight(Field->FindPerson(TEXT("VIRIDIAN_FOREST"), TEXT("VIRIDIANFOREST_YOUNGSTER2")), Fight))
            {
                Fight.bNoBlackout = true;
                Enc->Begin(Fight);
            }
        }
        return true;
    }
    if (Step == 2)
    {
        GameTest::HoldShot(4.0);
        // m4u: the learn shot's Pidgey never acts (nothing the AI does decides the shot).
        if (bFight && Name == TEXT("m4t-learn-move")) Mgr->bFoePassive = true;
        if (!bFight || Clock < (Name == TEXT("m4t-switch-paused") ? 3.6 : 2.4)) return true;
        Mgr->bFoePassive = true;
        if (Name == TEXT("m4t-learn-move"))
        {
            // The Pidgey falls to one hit with its dice pinned: Lv 8 and THUNDER WAVE's prompt over the paused fight. A hit
            // that can't land this tick (the fight between states) is tried again on the next.
            if (!PYRTest::KnockOut(Mgr))
            {
                if (Clock > 20.0) { Step = 99; return LearnShotFailed(TEXT("the staged knock-out never landed")); }
                return true;
            }
            UE_LOG(LogTemp, Display, TEXT("M4T_LEARN_SHOT knock-out landed after %.2f s of the fight"), Clock);
        }
        Step = 3;
        Clock = 0;
        if (Name == TEXT("m4t-learn-move")) {}
        else if (Name == TEXT("m4t-switch-paused"))
        {
            APYRMonPawn* P = Mgr->GetPlayerMon();
            APYRMonPawn* F = Mgr->GetFoeMon();
            if (P && F)
            {
                F->Hp = F->MaxHp = 999.f;
                const FVector Mid = Mgr->GetCenter();
                // Across the ring from side to side (the camera looks along +X), so the wind flies clear of the menu.
                PYRTest::Place(P, Mid + FVector(-150, -420, 0));
                PYRTest::Place(F, Mid + FVector(-150, 420, 0));
                PYRTest::Face(F, P);
                PYRTest::Face(P, F);
                Mgr->DebugForceFoeMove(PYRData::Gust);
            }
        }
        else
        {
            Mgr->bHoldEnd = true;   // The picture is of the moment after RUN.
            Mgr->PressRun();
        }
        return true;
    }
    if (Step == 5)
    {
        GameTest::HoldShot(2.0);
        if (Clock < .2) return true;
        Mgr->OpenWheel(false);   // The fight holds, the Gust frozen in the air.
        Step = 4;
        Clock = 0;
        GameTest::HoldShot(1.2);
        return true;
    }
    if (Step == 3)
    {
        if (Name == TEXT("m4t-learn-move"))
        {
            GameTest::HoldShot(2.0);
            // m4u: wait for the prompt by polling (generously), then check it before the picture.
            PYRMoveLearning::FView V;
            const bool bUp = PYRMoveLearning::View(Ctx, V);
            if (!bUp || !V.bTakesInput || Clock < .6)
            {
                if (Clock > 30.0) { Step = 99; return LearnShotFailed(TEXT("no learn prompt 30 s after the knock-out")); }
                return true;
            }
            const FName New = PYRData::ThunderWave;
            const TArray<FName> Four = {PYRData::ThunderShock, PYRData::Growl, PYRData::TailWhip, PYRData::QuickAttack};
            if (V.Stage != PYRMoveLearning::EStage::Ask || V.Move != New) { Step = 99; return LearnShotFailed(FString::Printf(TEXT("the prompt is not asking about THUNDER WAVE (%s)"), *V.Text)); }
            // An attack key does nothing to it (m4t's rule).
            if (APlayerController* PC = World->GetFirstPlayerController())
            {
                PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One, IE_Pressed, 1.f));
                PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One, IE_Released, 0.f));
            }
            PYRMoveLearning::FView After;
            if (!PYRMoveLearning::View(Ctx, After) || After.Stage != PYRMoveLearning::EStage::Ask || After.Cursor != V.Cursor || State->Party[0].Carried != Four)
                { Step = 99; return LearnShotFailed(TEXT("the attack key 1 changed the prompt or the moves")); }
            // YES: the list of the four with THUNDER WAVE beside them; then the cursor down to GROWL.
            Menus->Press(EPYRKey::Confirm);
            PYRMoveLearning::FView List;
            if (!PYRMoveLearning::View(Ctx, List) || List.Stage != PYRMoveLearning::EStage::List || List.Rows.Num() != 4) { Step = 99; return LearnShotFailed(TEXT("YES did not open the list of four")); }
            const FString Rows = FString::Join(List.Rows, TEXT("|"));
            for (const FName& M : Four) if (!Rows.Contains(PYRScreens::MoveName(M))) { Step = 99; return LearnShotFailed(FString::Printf(TEXT("%s is missing from the list (%s)"), *PYRScreens::MoveName(M), *Rows)); }
            // The NEW MOVE row under the four is the prompt's own move.
            if (List.Move != New) { Step = 99; return LearnShotFailed(FString::Printf(TEXT("the list's new move is %s, not THUNDER WAVE"), *List.Move.ToString())); }
            Menus->Press(EPYRKey::Down);
            UE_LOG(LogTemp, Display, TEXT("M4T_LEARN_SHOT prompt checked: '%s'; key 1 ignored; the list shows %s and %s; cursor on GROWL"),
                *V.Text.Replace(TEXT("\n"), TEXT(" / ")), *FString::JoinBy(Four, TEXT(", "), [](FName N) { return PYRScreens::MoveName(N); }), *PYRScreens::MoveName(New));
            Step = 4;
            Clock = 0;
            GameTest::HoldShot(1.0);
            return true;
        }
        if (Name == TEXT("m4t-switch-paused"))
        {
            GameTest::HoldShot(2.0);
            // Pidgey's Gust has left its wings: a moment later, with the swirl halfway across, the switch menu opens.
            for (TActorIterator<APYRMoveFx> It(World); It; ++It)
                if (It->Kind == EPYRFx::WindSwirl && It->MoveId == PYRData::Gust)
                {
                    Step = 5;
                    Clock = 0;
                    return true;
                }
            if (!FoeAttack(World) && Clock > 1.5)
            {
                Step = 2;   // Missed it: again.
                Clock = 3.6;
            }
            return true;
        }
        // The run shots: the caption and the lit RUN button stay fresh until the picture.
        Mgr->CaptionAt = Mgr->Now() - .5;
        Mgr->RunPressedAt = Mgr->Now() - .3;
        if (Clock < .2) GameTest::HoldShot(1.0);
        return true;
    }
    return true;
}
}
