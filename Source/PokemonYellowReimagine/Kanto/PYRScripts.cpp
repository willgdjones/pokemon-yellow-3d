#include "Kanto/PYRScripts.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRStoryScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Menus/PYRSysText.h"
#include "Music/GbMusicPlayer.h"
#include "Partner/PYRBond.h"
#include "Party/PYRSpecies.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Stage/PYREncounters.h"
#include "World/CoreSeams.h"

// ------------------------------------------------------------------------------------------------ the step runner
FPYRSeq& FPYRSeq::Do(TFunction<void()> F)
{
    return Then([F](TFunction<void()> Next) { if (F) F(); Next(); });
}

FPYRSeq& FPYRSeq::Say(const FString& LabelOrText)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Then([O, LabelOrText](TFunction<void()> Next)
    {
        UPYRMenus* M = O.IsValid() ? UPYRMenus::Get(O.Get()) : nullptr;
        const FString Text = O.IsValid() ? O->Line(LabelOrText) : LabelOrText;
        if (!M || Text.IsEmpty()) { Next(); return; }
        // m4u: a line from someone's own text label (ViridianCityOldMan2Text.HadMyCoffeeNowText is the old man's) carries
        // his name in the text box, however the scene began (a talk, or Red walking onto its trigger).
        FString Label = LabelOrText, Unused;
        LabelOrText.Split(TEXT("."), &Label, &Unused);
        if (const UPYRField* F = UPYRField::Get(O.Get()))
            for (const TWeakObjectPtr<APYRPerson>& P : F->People)
                if (P.IsValid() && P->IsShown() && (P->Texts.Contains(Label) || P->Texts.Contains(LabelOrText)))
                {
                    M->SetSpeaker(P->SpeakerName(), {Text});
                    break;
                }
        M->Say(Text, Next);
    });
}

FPYRSeq& FPYRSeq::Ask(const FString& LabelOrText, TFunction<void(bool)> OnAnswer)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Then([O, LabelOrText, OnAnswer](TFunction<void()> Next)
    {
        UPYRMenus* M = O.IsValid() ? UPYRMenus::Get(O.Get()) : nullptr;
        if (!M) { if (OnAnswer) OnAnswer(false); Next(); return; }
        M->Ask(O->Line(LabelOrText), [OnAnswer, Next](bool bYes) { if (OnAnswer) OnAnswer(bYes); Next(); });
    });
}

FPYRSeq& FPYRSeq::Wait(float Seconds)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Then([O, Seconds](TFunction<void()> Next)
    {
        if (!O.IsValid()) { Next(); return; }
        const double Until = O->GetWorld()->GetRealTimeSeconds() + Seconds;
        TWeakObjectPtr<UPYRScripts> O2 = O;
        O->Waiters.Add({[O2, Until]() { return !O2.IsValid() || O2->GetWorld()->GetRealTimeSeconds() >= Until; }, Next});
    });
}

FPYRSeq& FPYRSeq::WaitFor(TFunction<bool()> Cond)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Then([O, Cond](TFunction<void()> Next)
    {
        if (!O.IsValid() || !Cond || Cond()) { Next(); return; }
        O->Waiters.Add({Cond, Next});
    });
}

FPYRSeq& FPYRSeq::WalkPerson(APYRPerson* Person, const TArray<EYDir>& InSteps)
{
    TWeakObjectPtr<APYRPerson> P = Person;
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Then([P, O, InSteps](TFunction<void()> Next)
    {
        if (!P.IsValid() || InSteps.Num() == 0) { Next(); return; }
        P->SetShown(true);
        bool* bDone = new bool(false);
        P->Walk(InSteps, [bDone]() { *bDone = true; });
        if (O.IsValid()) O->Waiters.Add({[bDone, P]() { return *bDone || !P.IsValid(); }, [bDone, Next]() { delete bDone; Next(); }});
        else { delete bDone; Next(); }
    });
}

FPYRSeq& FPYRSeq::WalkRed(const TArray<EYDir>& InSteps)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Then([O, InSteps](TFunction<void()> Next)
    {
        UPYRField* Field = O.IsValid() ? UPYRField::Get(O.Get()) : nullptr;
        APokemonYellowReimagineCharacter* Red = Field ? Field->Red() : nullptr;
        const AYellowWorld* W = Field ? Field->GetYellowWorld() : nullptr;
        if (!Red || !W || InSteps.Num() == 0) { Next(); return; }
        TArray<FVector> Points;
        FIntPoint T = W->ToKanto(Red->GetActorLocation());
        for (EYDir D : InSteps) { T += YDir::Delta(D); Points.Add(W->KantoCenter(T) + FVector(0, 0, Red->GetActorLocation().Z)); }
        Red->ScriptWalkPath(Points);
        TWeakObjectPtr<APokemonYellowReimagineCharacter> R = Red;
        const double Start = O->GetWorld()->GetRealTimeSeconds();
        TWeakObjectPtr<UPYRScripts> O2 = O;
        O->Waiters.Add({[R, O2, Start, N = InSteps.Num()]() { return !R.IsValid() || !R->IsScriptWalking() || O2->GetWorld()->GetRealTimeSeconds() - Start > N * .6 + 1.0; },
            [R, Next]() { if (R.IsValid()) R->StopScriptWalk(); Next(); }});
    });
}

FPYRSeq& FPYRSeq::FaceRed(EYDir Dir)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Do([O, Dir]()
    {
        if (UPYRField* F = O.IsValid() ? UPYRField::Get(O.Get()) : nullptr)
            if (APokemonYellowReimagineCharacter* R = F->Red()) { R->SetActorRotation(FRotator(0, YDir::Yaw(Dir), 0)); F->Facing = Dir; }
    });
}

FPYRSeq& FPYRSeq::Fade(float Alpha, float Seconds)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    Do([O, Alpha, Seconds]() { if (UPYRField* F = O.IsValid() ? UPYRField::Get(O.Get()) : nullptr) F->Fade(Alpha, Seconds); });
    return Wait(Seconds + .02f);
}

FPYRSeq& FPYRSeq::Fight(TFunction<void(FPYRFight&)> Setup, TFunction<void(EPYRBattleOutcome)> After)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Then([O, Setup, After](TFunction<void()> Next)
    {
        UPYREncounters* Enc = O.IsValid() ? UPYREncounters::Get(O.Get()) : nullptr;
        UPYRField* Field = O.IsValid() ? UPYRField::Get(O.Get()) : nullptr;
        if (!Enc) { if (After) After(EPYRBattleOutcome::Won); Next(); return; }
        FPYRFight F;
        if (Setup) Setup(F);
        F.OnDone = [After, Next, Field](EPYRBattleOutcome Out)
        {
            if (Field) Field->Lock(TEXT("script"));
            if (After) After(Out);
            Next();
        };
        // The fight owns Red for a while; the script's lock comes back after it.
        if (Field) Field->Unlock(TEXT("script"));
        if (!Enc->Begin(F)) { if (Field) Field->Lock(TEXT("script")); if (After) After(EPYRBattleOutcome::Fled); Next(); }
    });
}

FPYRSeq& FPYRSeq::Jingle(FName Id)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Do([O, Id]() { if (UGbMusicPlayer* M = O.IsValid() ? UGbMusicPlayer::Get(O.Get()) : nullptr) M->Jingle(Id); });
}

FPYRSeq& FPYRSeq::Music(FName Id)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Do([O, Id]() { if (UGbMusicPlayer* M = O.IsValid() ? UGbMusicPlayer::Get(O.Get()) : nullptr) M->Play(Id, .3f); });
}

FPYRSeq& FPYRSeq::Give(FName Item, int32 Count, const FString& Verb, const FString& TextId)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Then([O, Item, Count, Verb, TextId](TFunction<void()> Next)
    {
        UPYRField* F = O.IsValid() ? UPYRField::Get(O.Get()) : nullptr;
        if (!F) { Next(); return; }
        F->GiveItem(Item, Count, Verb, Next, TextId);
    });
}

FPYRSeq& FPYRSeq::GiveQuiet(FName Item, int32 Count)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Do([O, Item, Count]()
    {
        if (UPYRTrainerState* S = O.IsValid() ? UPYRTrainerState::Get(O.Get()) : nullptr) S->AddItem(Item, Count);
        if (UGbMusicPlayer* M = O.IsValid() ? UGbMusicPlayer::Get(O.Get()) : nullptr) M->Jingle(TEXT("get_key_item"));
    });
}

FPYRSeq& FPYRSeq::GiveMon(FName Species, int32 Level)
{
    TWeakObjectPtr<UPYRScripts> O = Owner;
    return Then([O, Species, Level](TFunction<void()> Next)
    {
        UPYRTrainerState* S = O.IsValid() ? UPYRTrainerState::Get(O.Get()) : nullptr;
        UPYRMenus* M = O.IsValid() ? UPYRMenus::Get(O.Get()) : nullptr;
        if (!S || !M) { Next(); return; }
        FPYRMon Mon = UPYRTrainerState::MakeMon(Species, Level);
        S->MarkCaught(Species);
        const bool bParty = S->AddMonValue(Mon);
        if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(O.Get())) Music->Jingle(TEXT("get_key_item"));
        const int32 Index = bParty ? S->Party.Num() - 1 : S->Storage.Num() - 1;
        TWeakObjectPtr<UPYRScripts> O2 = O;
        M->Say(PYRSysText::Get(TEXT("GotMonText"), {{TEXT("wNameBuffer"), PYRScreens::SpeciesName(Species)}}), [O2, Index, bParty, Species, Next]()
        {
            PYRScreens::OfferNickname(O2.Get(), Index, !bParty, [O2, bParty, Species, Next]()
            {
                if (!bParty && O2.IsValid())
                    if (UPYRMenus* M2 = UPYRMenus::Get(O2.Get()))
                    {
                        M2->Say(PYRSysText::Get(TEXT("ItemUseBallText08"), {{TEXT("wBoxMonNicks"), PYRScreens::SpeciesName(Species)}}), Next);
                        return;
                    }
                Next();
            });
        });
    });
}

void FPYRSeq::Run()
{
    if (UPYRScripts* O = Owner.Get()) O->Began(AsShared());
    bRunning = true;
    At = 0;
    Next();
}

void FPYRSeq::Next()
{
    if (!bRunning) return;
    if (At >= Steps.Num())
    {
        bRunning = false;
        TFunction<void()> End = OnEnd;
        if (UPYRScripts* O = Owner.Get()) O->Ended(AsShared());
        if (End) End();
        return;
    }
    FStep Step = Steps[At++];
    TWeakPtr<FPYRSeq> Weak = AsShared();
    Step([Weak]() { if (TSharedPtr<FPYRSeq> S = Weak.Pin()) S->Next(); });
}

// ------------------------------------------------------------------------------------------------ subsystem
UPYRScripts* UPYRScripts::Get(const UObject* Ctx)
{
    UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
    return W ? W->GetSubsystem<UPYRScripts>() : nullptr;
}

TSharedRef<FPYRSeq> UPYRScripts::Seq(const FString& Name)
{
    TSharedRef<FPYRSeq> S = MakeShared<FPYRSeq>();
    S->Owner = this;
    S->Name = Name;
    return S;
}

void UPYRScripts::Began(TSharedRef<FPYRSeq> S)
{
    Ran.Add(S->Name);
    UE_LOG(LogYellow, Display, TEXT("YELLOW_SCRIPT %s"), *S->Name);
    if (S->bLock) if (UPYRField* F = UPYRField::Get(this)) F->Lock(TEXT("script"));
    // A sequence started inside another (a gift's GiveMon): keep the outer one alive, since its steps continue
    // through weak pointers and it ends (and unlocks Red) only after the inner one hands back.
    if (Running.IsValid() && Running != S) Outer.Push(Running);
    Running = S;
}

void UPYRScripts::Ended(TSharedRef<FPYRSeq> S)
{
    Outer.Remove(S);
    if (Running == S)
    {
        Running.Reset();
        while (Outer.Num() && !Running.IsValid())
        {
            TSharedPtr<FPYRSeq> O = Outer.Pop();
            if (O->bRunning) Running = O;
        }
    }
    if (S->bLock) if (UPYRField* F = UPYRField::Get(this)) F->Unlock(TEXT("script"));
    Refresh();
}

void UPYRScripts::Start()
{
    if (bStarted) return;
    bStarted = true;
    if (UPYRField* F = UPYRField::Get(this))
    {
        F->OnTileChanged.AddUObject(this, &UPYRScripts::OnTile);
        F->OnMapChanged.AddUObject(this, &UPYRScripts::OnMap);
    }
    if (const FYellowDataSet* D = UYellowData::Data(this))
        for (const TSharedPtr<FJsonObject>& E : D->Events)
        {
            FString Npc;
            if (E.IsValid() && E->HasField(TEXT("battle")) && E->TryGetStringField(TEXT("npc"), Npc)) EventNpcs.Add(Npc);
        }
    Refresh();
}

FString UPYRScripts::Line(const FString& LabelOrText) const
{
    if (const FYellowDataSet* D = UYellowData::Data(this)) if (const FString* T = D->Text.Find(LabelOrText)) return *T;
    const FString Sys = PYRSysText::Get(LabelOrText);
    return Sys.IsEmpty() ? LabelOrText : Sys;
}

TSharedPtr<FJsonObject> UPYRScripts::Event(const FString& Id) const
{
    if (const FYellowDataSet* D = UYellowData::Data(this))
        for (const TSharedPtr<FJsonObject>& E : D->Events) if (E.IsValid() && E->GetStringField(TEXT("id")) == Id) return E;
    return nullptr;
}

bool UPYRScripts::Check(const TSharedPtr<FJsonObject>& E) const
{
    if (!E.IsValid()) return false;
    const UPYRProgress* P = UPYRProgress::Get(this);
    const UPYRTrainerState* S = UPYRTrainerState::Get(this);
    const UPYRField* F = UPYRField::Get(this);
    if (!P || !S) return false;
    const TArray<TSharedPtr<FJsonValue>>* Req = nullptr;
    if (E->TryGetArrayField(TEXT("requires"), Req))
        for (const TSharedPtr<FJsonValue>& V : *Req)
        {
            if (V->Type == EJson::String) { if (!P->Has(FName(*V->AsString()))) return false; continue; }
            const TSharedPtr<FJsonObject> O = V->Type == EJson::Object ? V->AsObject() : nullptr;
            if (!O.IsValid()) continue;
            FString Str;
            double Num = 0;
            if (O->TryGetStringField(TEXT("item_in_bag"), Str) && !S->HasItem(YKey::Item(Str))) return false;
            if (O->TryGetStringField(TEXT("item_not_in_bag"), Str) && S->HasItem(YKey::Item(Str))) return false;
            if (O->TryGetNumberField(TEXT("pokedex_owned_less_than"), Num) && S->Caught.Num() >= int32(Num)) return false;
            if (O->TryGetNumberField(TEXT("money_at_least"), Num) && S->Money < int32(Num)) return false;
            if (O->TryGetNumberField(TEXT("pikachu_happiness_at_least"), Num) && S->BondPoints < int32(Num)) return false;
            if (O->TryGetStringField(TEXT("badge"), Str) && !S->HasBadge(FName(*Str.Replace(TEXT("BADGE"), TEXT("")).ToLower().Left(1).ToUpper() + Str.Replace(TEXT("BADGE"), TEXT("")).ToLower().Mid(1)))) return false;
            if (O->TryGetStringField(TEXT("badge_not"), Str) && S->HasBadge(FName(*Str.Replace(TEXT("BADGE"), TEXT("")).ToLower().Left(1).ToUpper() + Str.Replace(TEXT("BADGE"), TEXT("")).ToLower().Mid(1)))) return false;
            if (O->TryGetStringField(TEXT("player_facing"), Str) && F && YDir::FromName(Str) != F->Facing) return false;
        }
    const TArray<TSharedPtr<FJsonValue>>* Not = nullptr;
    if (E->TryGetArrayField(TEXT("requires_not"), Not))
        for (const TSharedPtr<FJsonValue>& V : *Not) if (V->Type == EJson::String && P->Has(FName(*V->AsString()))) return false;
    return true;
}

void UPYRScripts::ApplySets(const TSharedPtr<FJsonObject>& E)
{
    UPYRProgress* P = UPYRProgress::Get(this);
    UPYRTrainerState* S = UPYRTrainerState::Get(this);
    if (!E.IsValid() || !P) return;
    const TArray<TSharedPtr<FJsonValue>>* Sets = nullptr;
    if (E->TryGetArrayField(TEXT("sets"), Sets))
        for (const TSharedPtr<FJsonValue>& V : *Sets)
        {
            if (V->Type == EJson::String) P->Set(FName(*V->AsString()));
            else if (V->Type == EJson::Object && S)
            {
                FString Badge;
                if (V->AsObject()->TryGetStringField(TEXT("badge"), Badge))
                {
                    const FString B = Badge.Replace(TEXT("BADGE"), TEXT("")).ToLower();
                    S->AddBadge(FName(*(B.Left(1).ToUpper() + B.Mid(1))));
                }
            }
        }
    const TArray<TSharedPtr<FJsonValue>>* Resets = nullptr;
    if (E->TryGetArrayField(TEXT("resets"), Resets))
        for (const TSharedPtr<FJsonValue>& V : *Resets) if (V->Type == EJson::String) P->Set(FName(*V->AsString()), false);
}

void UPYRScripts::Refresh()
{
    PYRStory::Refresh(this);
}

// ------------------------------------------------------------------------------------------------ trainers
bool UPYRScripts::TrainerFight(APYRPerson* T, FPYRFight& Out) const
{
    const FYellowDataSet* D = UYellowData::Data(this);
    if (!T || !D || !T->IsTrainer()) return false;
    const TArray<FYSlot> Party = D->TrainerParty(T->Trainer);
    Out.TrainerModel = T->Model.Model;
    Out.TrainerHeight = T->Model.HeightCm;
    if (Party.Num() == 0) { UE_LOG(LogYellow, Warning, TEXT("No party for %s %d"), *T->Trainer.Class.ToString(), T->Trainer.Party); return false; }
    PYRStory::FillFight(this, T->Trainer.Class, Party, Out);
    Out.TrainerId = FName(*T->TrainerKey());
    Out.WinText = Line(T->Trainer.EndText);
    Out.Stage = UPYREncounters::StageForMap(D->Map(T->Map));
    Out.bStageSet = true;
    return true;
}

void UPYRScripts::Challenge(APYRPerson* T, bool bWalk)
{
    if (!T || IsRunning()) return;
    UPYRField* Field = UPYRField::Get(this);
    const AYellowWorld* W = Field ? Field->GetYellowWorld() : nullptr;
    if (!Field || !W) return;
    FPYRFight Fight;
    if (!TrainerFight(T, Fight)) return;
    ++Spotted;
    LastSpotted = T->TrainerKey();
    TSharedRef<FPYRSeq> S = Seq(FString::Printf(TEXT("trainer %s"), *T->TrainerKey()));
    TWeakObjectPtr<APYRPerson> P = T;
    const FString Cls = T->Trainer.Class.ToString();
    const FName Meet = Cls.Contains(TEXT("ROCKET")) ? FName(TEXT("meet_evil_trainer")) : Cls.Contains(TEXT("RIVAL")) ? FName(TEXT("meet_rival"))
        : (Cls.Contains(TEXT("LASS")) || Cls.EndsWith(TEXT("_F")) || Cls.Contains(TEXT("BEAUTY")) || Cls.Contains(TEXT("MISTY"))) ? FName(TEXT("meet_female_trainer"))
        : FName(TEXT("meet_male_trainer"));
    S->Do([P]() { if (P.IsValid()) { P->bFrozen = true; P->Exclaim(1.0f); } });
    S->Do([this]() { UGbMusicPlayer::PlaySfx(this, TEXT("trainer_spot")); });
    S->Music(Meet);
    S->Wait(.8f);
    if (bWalk)
    {
        // Walk up tile by tile until next to Red.
        const FIntPoint RedTile = Field->KantoTile;
        const FIntPoint D = RedTile - T->Tile;
        const int32 Steps = FMath::Max(0, FMath::Abs(D.X) + FMath::Abs(D.Y) - 1);
        TArray<EYDir> Path;
        for (int32 I = 0; I < Steps; ++I) Path.Add(T->Facing);
        S->WalkPerson(T, Path);
    }
    const EYDir Toward = YDir::Opposite(T->Facing);
    S->FaceRed(Toward);
    S->Say(T->Trainer.BattleText);
    S->Fight([Fight](FPYRFight& F) { F = Fight; }, [this, P](EPYRBattleOutcome Out)
    {
        if (!P.IsValid()) return;
        P->bFrozen = false;
        if (Out == EPYRBattleOutcome::Won)
        {
            UPYRProgress* Prog = UPYRProgress::Get(this);
            if (Prog)
            {
                Prog->Beat(P->TrainerKey());
                if (!P->Trainer.Event.IsNone()) Prog->Set(P->Trainer.Event);
            }
            PYRStory::AfterTrainer(this, P.Get());
        }
    });
    S->Run();
}

void UPYRScripts::CheckSight()
{
    UPYRField* Field = UPYRField::Get(this);
    const UPYRMenus* Menus = UPYRMenus::Get(this);
    const UPYREncounters* Enc = UPYREncounters::Get(this);
    const AYellowWorld* W = Field ? Field->GetYellowWorld() : nullptr;
    if (!bSight || !Field || !W || IsRunning() || Field->IsLocked() || Field->IsWarping() || PYRTestFlags::bNoEncounters) return;
    if ((Menus && Menus->AnyOpen()) || (Enc && Enc->IsBusy())) return;
    const UPYRTrainerState* State = UPYRTrainerState::Get(this);
    if (!State || State->FirstAble() == INDEX_NONE) return;
    const FIntPoint Red = Field->KantoTile;
    for (const TWeakObjectPtr<APYRPerson>& PW : Field->People)
    {
        APYRPerson* P = PW.Get();
        if (!P || !P->IsTrainer() || !P->IsShown() || P->IsBeaten() || P->Trainer.Sight <= 0 || P->Map != Field->CurrentMap || P->IsMoving()) continue;
        if (EventNpcs.Contains(P->Id)) continue;
        if (!P->Trainer.Event.IsNone()) if (const UPYRProgress* Prog = UPYRProgress::Get(this)) if (Prog->Has(P->Trainer.Event)) continue;
        const FIntPoint D = YDir::Delta(P->Facing);
        FIntPoint T = P->Tile;
        for (int32 I = 1; I <= P->Trainer.Sight; ++I)
        {
            if (W->IsEdgeBlocked(T, P->Facing)) break;
            T += D;
            if (T == Red) { Challenge(P, true); return; }
            if (W->IsSolidAt(T)) break;
            bool bBlocked = false;
            for (const TWeakObjectPtr<APYRPerson>& Q : Field->People) if (Q.IsValid() && Q.Get() != P && Q->IsShown() && Q->Tile == T) bBlocked = true;
            if (bBlocked) break;
        }
    }
}

// ------------------------------------------------------------------------------------------------ hooks
void UPYRScripts::OnTile(FName Map, FIntPoint Local)
{
    if (IsRunning()) return;
    if (PYRStory::OnTile(this, Map, Local)) return;
    RunEventAt(Map, Local);
}

void UPYRScripts::OnMap(FName From, FName To)
{
    PYRStory::OnMap(this, From, To);
}

bool UPYRScripts::RunEventAt(FName Map, FIntPoint Local)
{
    const FYellowDataSet* D = UYellowData::Data(this);
    if (!D) return false;
    for (const TSharedPtr<FJsonObject>& E : D->Events)
    {
        if (!E.IsValid() || E->GetStringField(TEXT("map")) != Map.ToString()) continue;
        const TArray<TSharedPtr<FJsonValue>>* Coords = nullptr;
        if (!E->TryGetArrayField(TEXT("trigger_coords"), Coords)) continue;
        bool bHere = false;
        for (const TSharedPtr<FJsonValue>& C : *Coords)
        {
            const TSharedPtr<FJsonObject> O = C->AsObject();
            if (O.IsValid() && int32(O->GetNumberField(TEXT("x"))) == Local.X && int32(O->GetNumberField(TEXT("y"))) == Local.Y) bHere = true;
        }
        if (!bHere || !Check(E)) continue;
        if (RunEvent(E->GetStringField(TEXT("id")), nullptr)) return true;
    }
    return false;
}

bool UPYRScripts::RunEvent(const FString& Id, APYRPerson* Talker)
{
    return PYRStory::RunEvent(this, Id, Talker);
}

bool UPYRScripts::Talk(APYRPerson* Person)
{
    if (!Person || IsRunning()) return IsRunning();
    return PYRStory::Talk(this, Person);
}

bool UPYRScripts::ReadSign(APYRSignpost* Sign)
{
    return false;
}

bool UPYRScripts::UseTile(FIntPoint Kanto)
{
    return PYRStory::UseTile(this, Kanto);
}

void UPYRScripts::Tick(float DeltaTime)
{
    if (!bStarted) return;
    // Waiting steps (walks, pauses, fights handing back).
    for (int32 I = 0; I < Waiters.Num(); )
    {
        if (Waiters[I].Cond && !Waiters[I].Cond()) { ++I; continue; }
        TFunction<void()> Then = MoveTemp(Waiters[I].Then);
        Waiters.RemoveAt(I);
        if (Then) Then();
    }
    CheckSight();
}

// ------------------------------------------------------------------------------------------------ free functions
namespace PYRScripts
{
void Start(UWorld* World) { if (UPYRScripts* S = UPYRScripts::Get(World)) S->Start(); }
void Refresh(const UObject* Ctx) { if (UPYRScripts* S = UPYRScripts::Get(Ctx)) S->Refresh(); }
bool Talk(UWorld* World, APYRPerson* Person) { UPYRScripts* S = UPYRScripts::Get(World); return S && S->Talk(Person); }
bool ReadSign(UWorld* World, APYRSignpost* Sign) { UPYRScripts* S = UPYRScripts::Get(World); return S && S->ReadSign(Sign); }
bool UseTile(UWorld* World, FIntPoint Kanto) { UPYRScripts* S = UPYRScripts::Get(World); return S && S->UseTile(Kanto); }
}
