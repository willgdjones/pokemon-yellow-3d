#include "Kanto/PYRStoryScripts.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Menus/PYRSysText.h"
#include "Music/GbMusicPlayer.h"
#include "Partner/PYRBond.h"
#include "Partner/PYRPikachuFollower.h"
#include "Party/PYRSpecies.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Stage/PYREncounters.h"
#include "World/CoreSeams.h"

namespace
{
UPYRProgress* Prog(const UObject* C) { return UPYRProgress::Get(C); }
UPYRTrainerState* St(const UObject* C) { return UPYRTrainerState::Get(C); }
UPYRField* Fld(const UObject* C) { return UPYRField::Get(C); }
UPYRMenus* Mn(const UObject* C) { return UPYRMenus::Get(C); }
bool Has(const UObject* C, const TCHAR* Flag) { const UPYRProgress* P = Prog(C); return P && P->Has(FName(Flag)); }
void SetFlag(const UObject* C, const TCHAR* Flag, bool bOn = true) { if (UPYRProgress* P = Prog(C)) P->Set(FName(Flag), bOn); }

APYRPerson* Person(const UObject* C, const TCHAR* Map, const TCHAR* Id)
{
    const UPYRField* F = Fld(C);
    return F ? F->FindPerson(FName(Map), Id) : nullptr;
}

void Show(const UObject* C, const TCHAR* Map, const TCHAR* Id, bool bShow)
{
    if (APYRPerson* P = Person(C, Map, Id)) if (P->IsShown() != bShow) P->SetShown(bShow);
}

FString ItemName(const UObject* C, const TCHAR* Key) { return PYRScreens::ItemName(C, YKey::Item(Key)); }

// A branchy script written with callbacks: Red is locked from Begin until Done.
void Flow(UPYRScripts* S, const FString& Name, TFunction<void(TFunction<void()>)> Body)
{
    TSharedRef<FPYRSeq> Q = S->Seq(Name);
    Q->Then(MoveTemp(Body));
    Q->Run();
}

int32 WarpTo(const FYMap* From, const TCHAR* To)
{
    if (!From) return INDEX_NONE;
    for (int32 I = 0; I < From->Warps.Num(); ++I) if (From->Warps[I].ToMap == FName(To)) return I;
    return INDEX_NONE;
}

const FYellowDataSet* Data(const UObject* C) { return UYellowData::Data(C); }

// The fight an event names ({class, trainer_no}).
bool EventFight(const UPYRScripts* S, const FString& EventId, FPYRFight& Out)
{
    const TSharedPtr<FJsonObject> E = S->Event(EventId);
    const TSharedPtr<FJsonObject>* B = nullptr;
    const FYellowDataSet* D = Data(S);
    if (!E.IsValid() || !D || !E->TryGetObjectField(TEXT("battle"), B) || !B) return false;
    FYTrainerRef Ref;
    Ref.bValid = true;
    Ref.Class = FName(*(*B)->GetStringField(TEXT("class")));
    Ref.Party = int32((*B)->GetNumberField(TEXT("trainer_no")));
    const TArray<FYSlot> Party = D->TrainerParty(Ref);
    if (Party.Num() == 0) return false;
    PYRStory::FillFight(S, Ref.Class, Party, Out);
    // The person the event is about stands across the stage.
    FString Npc, MapId;
    if (E->TryGetStringField(TEXT("npc"), Npc) && E->TryGetStringField(TEXT("map"), MapId))
        if (const UPYRField* F = UPYRField::Get(S))
            if (const APYRPerson* P = F->FindPerson(FName(*MapId), Npc)) { Out.TrainerModel = P->Model.Model; Out.TrainerHeight = P->Model.HeightCm; }
    return true;
}
}

namespace PYRStory
{
// ------------------------------------------------------------------------------------------------ who is shown
void Refresh(UPYRScripts* S)
{
    UPYRField* F = Fld(S);
    if (!F) return;
    for (const TWeakObjectPtr<APYRPerson>& PW : F->People)
    {
        APYRPerson* P = PW.Get();
        if (!P) continue;
        const FString Id = P->Id;
        // A beaten trainer stays; a script-only person (Oak on the road, the rival on Route 22...) waits hidden.
        bool bShow = !(P->Raw.IsValid() && P->Raw->HasField(TEXT("hidden_at_start")) && P->Raw->GetBoolField(TEXT("hidden_at_start")));
        if (Id == TEXT("PALLETTOWN_OAK") || Id == TEXT("OAKSLAB_OAK2") || Id == TEXT("ROUTE22_RIVAL1") || Id == TEXT("CERULEANCITY_RIVAL")
            || Id == TEXT("MTMOONB2F_JESSIE") || Id == TEXT("MTMOONB2F_JAMES")) bShow = false;
        else if (Id == TEXT("OAKSLAB_OAK1")) bShow = Has(S, TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"));
        else if (Id == TEXT("OAKSLAB_RIVAL")) bShow = !Has(S, TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"));
        else if (Id == TEXT("OAKSLAB_EEVEE_POKE_BALL")) bShow = !Has(S, TEXT("EVENT_GOT_STARTER"));
        else if (Id.StartsWith(TEXT("OAKSLAB_POKEDEX"))) bShow = !Has(S, TEXT("EVENT_GOT_POKEDEX"));
        else if (Id == TEXT("VIRIDIANCITY_OLD_MAN_SLEEPY")) bShow = !Has(S, TEXT("EVENT_GOT_POKEDEX"));
        else if (Id == TEXT("VIRIDIANCITY_OLD_MAN2")) bShow = Has(S, TEXT("EVENT_GOT_POKEDEX")) && !Has(S, TEXT("EVENT_COMPLETED_CATCH_TRAINING"));
        else if (Id == TEXT("VIRIDIANCITY_OLD_MAN")) bShow = Has(S, TEXT("EVENT_SPAWNED_OLD_MAN_1"));
        else if (Id == TEXT("BLUESHOUSE_TOWN_MAP")) bShow = !Has(S, TEXT("EVENT_GOT_TOWN_MAP"));
        else if (Id == TEXT("PEWTERCITY_YOUNGSTER")) bShow = !Has(S, TEXT("EVENT_BEAT_BROCK"));
        else if (Id == TEXT("CERULEANCITY_ROCKET")) bShow = !Has(S, TEXT("EVENT_BEAT_CERULEAN_ROCKET_THIEF"));
        // The officer at the robbed house's door (27,12) steps aside to 28,12 once Bill gives the S.S.TICKET or the
        // thief is beaten (scripts/BillsHouse_2.asm, CeruleanHideRocket): until then the house is shut.
        else if (Id == TEXT("CERULEANCITY_GUARD1")) bShow = Has(S, TEXT("EVENT_GOT_SS_TICKET")) || Has(S, TEXT("EVENT_BEAT_CERULEAN_ROCKET_THIEF"));
        else if (Id == TEXT("CERULEANCITY_GUARD2")) bShow = !Has(S, TEXT("EVENT_GOT_SS_TICKET")) && !Has(S, TEXT("EVENT_BEAT_CERULEAN_ROCKET_THIEF"));
        else if (Id == TEXT("MTMOONB2F_DOME_FOSSIL") || Id == TEXT("MTMOONB2F_HELIX_FOSSIL"))
            bShow = !Has(S, TEXT("EVENT_GOT_DOME_FOSSIL")) && !Has(S, TEXT("EVENT_GOT_HELIX_FOSSIL"));
        else if (Id == TEXT("BILLSHOUSE_BILL_POKEMON")) bShow = !Has(S, TEXT("EVENT_BILL_SAID_USE_CELL_SEPARATOR"));
        else if (Id == TEXT("BILLSHOUSE_BILL1")) bShow = Has(S, TEXT("EVENT_USED_CELL_SEPARATOR_ON_BILL")) && !Has(S, TEXT("EVENT_LEFT_BILLS_HOUSE_AFTER_HELPING"));
        else if (Id == TEXT("BILLSHOUSE_BILL2")) bShow = Has(S, TEXT("EVENT_LEFT_BILLS_HOUSE_AFTER_HELPING"));
        else if (Id == TEXT("ROUTE24_COOLTRAINER_M1")) bShow = !Has(S, TEXT("EVENT_LEFT_BILLS_HOUSE_AFTER_HELPING"));
        if (P->IsShown() != bShow) P->SetShown(bShow);
    }
}

// ------------------------------------------------------------------------------------------------ fights
void FillFight(const UPYRScripts* S, FName Class, const TArray<FYSlot>& Party, FPYRFight& Out)
{
    const FYellowDataSet* D = Data(S);
    const FYTrainerClass* C = D ? D->TrainerClass(Class) : nullptr;
    const FString Cls = Class.ToString().ToUpper();
    const bool bLeader = Cls == TEXT("BROCK") || Cls == TEXT("MISTY") || Cls == TEXT("LT_SURGE") || Cls == TEXT("ERIKA") || Cls == TEXT("KOGA")
        || Cls == TEXT("SABRINA") || Cls == TEXT("BLAINE") || Cls == TEXT("GIOVANNI");
    Out.Kind = bLeader ? EPYRBattleKind::Gym : EPYRBattleKind::Trainer;
    Out.TrainerName = C ? C->Name : Cls.Replace(TEXT("_"), TEXT(" "));
    Out.TrainerId = FName(*Out.TrainerName);
    Out.Foes.Reset();
    for (const FYSlot& Slot : Party)
    {
        FPYRMonSpec Spec;
        Spec.Species = Slot.Species;
        Spec.Level = Slot.Level;
        if (Slot.Moves.Num() > 0)
        {
            // Yellow's special moves replace those slots of the default moveset.
            TArray<FName> Moves = PYRData::DefaultMoves(Slot.Species, Slot.Level);
            for (const auto& Pair : Slot.Moves)
            {
                const int32 I = Pair.Key - 1;
                if (I < 0 || I > 3) continue;
                while (Moves.Num() <= I) Moves.Add(Pair.Value);
                Moves[I] = Pair.Value;
            }
            Spec.Moves = Moves;
        }
        Out.Foes.Add(Spec);
    }
    // Yellow's prize: the class's base money times the last Pokemon's level.
    Out.Prize = (C ? C->BaseMoney : 10) * (Party.Num() ? Party.Last().Level : 1);
    Out.Music = bLeader ? FName(TEXT("gym_leader_battle")) : FName(TEXT("trainer_battle"));
}

void AfterTrainer(UPYRScripts* S, APYRPerson* T)
{
    if (!T) return;
    const FString Id = T->Id;
    if (Id == TEXT("PEWTERGYM_BROCK")) RunEvent(S, TEXT("pewter_gym_brock_tm34_after"), T);
    else if (Id == TEXT("CERULEANGYM_MISTY")) RunEvent(S, TEXT("cerulean_gym_misty_tm11_after"), T);
}

// ------------------------------------------------------------------------------------------------ services
namespace
{
void Nurse(UPYRScripts* S, APYRPerson* P)
{
    TWeakObjectPtr<UPYRScripts> W = S;
    const FName Centre = P->Map;
    Flow(S, TEXT("pokecenter"), [W, Centre](TFunction<void()> Done)
    {
        UPYRMenus* M = Mn(W.Get());
        M->Say(W->Line(TEXT("PokemonCenterWelcomeText")), [W, M, Centre, Done]()
        {
            M->Ask(W->Line(TEXT("ShallWeHealYourPokemonText")), [W, M, Centre, Done](bool bYes)
            {
                if (!bYes) { M->Say(W->Line(TEXT("PokemonCenterFarewellText")), Done); return; }
                M->Say(W->Line(TEXT("NeedYourPokemonText")), [W, M, Centre, Done]()
                {
                    if (UPYRTrainerState* T = St(W.Get())) { T->HealParty(); T->SetLastCentre(Centre); }
                    UGbMusicPlayer::PlaySfx(W.Get(), TEXT("heal_machine"));
                    if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(W.Get())) Music->Jingle(TEXT("pkmn_healed"));
                    UE_LOG(LogYellow, Display, TEXT("YELLOW_HEALED at %s"), *Centre.ToString());
                    M->Say(W->Line(TEXT("PokemonFightingFitText")), [W, M, Done]() { M->Say(W->Line(TEXT("PokemonCenterFarewellText")), Done); });
                });
            });
        });
    });
}

// scripts/BikeShop.asm BikeShopClerkText: the welcome, then the menu (BikeShopMenuText, with BikeShopMenuPrice
// printed at 8,3: after CANCEL) and "Do you like it?"; BICYCLE is more than Red can afford. The BIKE VOUCHER comes
// from Vermilion, past the range.
void BikeClerk(UPYRScripts* S)
{
    TWeakObjectPtr<UPYRScripts> W = S;
    Flow(S, TEXT("bike-shop"), [W](TFunction<void()> Done)
    {
        UPYRMenus* M = Mn(W.Get());
        if (Has(W.Get(), TEXT("EVENT_GOT_BICYCLE"))) { M->Say(W->Line(TEXT("BikeShopClerkHowDoYouLikeYourBicycleText")), Done); return; }
        M->Say(W->Line(TEXT("BikeShopClerkWelcomeText")), [W, M, Done]()
        {
            TArray<FString> Rows;
            W->Line(TEXT("BikeShopMenuText")).ParseIntoArray(Rows, TEXT("\n"));
            if (Rows.Num() < 2) Rows = {TEXT("BICYCLE"), TEXT("CANCEL")};
            M->Choose(W->Line(TEXT("BikeShopClerkDoYouLikeItText")), {Rows[0], Rows[1] + W->Line(TEXT("BikeShopMenuPrice"))}, [W, M, Done](int32 Pick)
            {
                auto Again = [W, M, Done]() { M->Say(W->Line(TEXT("BikeShopComeAgainText")), Done); };
                if (Pick == 0) M->Say(W->Line(TEXT("BikeShopCantAffordText")), Again);
                else Again();
            });
        });
    });
}

void Clerk(UPYRScripts* S, APYRPerson* P)
{
    TWeakObjectPtr<UPYRScripts> W = S;
    const FName Map = P->Map;
    Flow(S, TEXT("mart"), [W, Map](TFunction<void()> Done)
    {
        Mn(W.Get())->Say(W->Line(TEXT("PokemartGreetingText")), [W, Map, Done]() { PYRScreens::OpenMart(W.Get(), Map, Done); });
    });
}
}

// ------------------------------------------------------------------------------------------------ talking
bool Talk(UPYRScripts* S, APYRPerson* P)
{
    const FString Id = P->Id;
    const FYellowDataSet* D = Data(S);
    TWeakObjectPtr<UPYRScripts> W = S;
    // ---- Mom heals the party (Yellow: "You should take a rest.").
    if (Id == TEXT("REDSHOUSE1F_MOM"))
    {
        const UPYRTrainerState* T = St(S);
        if (!T || T->Party.Num() == 0) { S->Seq(TEXT("mom"))->Say(TEXT("RedsHouse1FPrintMomText.WakeUpText")).Run(); return true; }
        S->Seq(TEXT("mom-heal")).Get()
            .Say(TEXT("RedsHouse1FMomYouShouldRestText"))
            .Do([W]() { if (UPYRTrainerState* T2 = St(W.Get())) T2->HealParty(); })
            .Jingle(TEXT("pkmn_healed"))
            .Say(TEXT("RedsHouse1FMomLookingGreatText"))
            .Run();
        return true;
    }
    // ---- Oak in the lab.
    if (Id == TEXT("OAKSLAB_OAK1"))
    {
        if (S->Check(S->Event(TEXT("oaks_lab_deliver_parcel_pokedex")))) return RunEvent(S, TEXT("oaks_lab_deliver_parcel_pokedex"), P);
        if (S->Check(S->Event(TEXT("oaks_lab_poke_balls")))) return RunEvent(S, TEXT("oaks_lab_poke_balls"), P);
        const TCHAR* Line = Has(S, TEXT("EVENT_GOT_POKEBALLS_FROM_OAK")) ? TEXT("OaksLabOak1Text.ComeSeeMeSometimesText")
            : Has(S, TEXT("EVENT_GOT_POKEDEX")) ? TEXT("OaksLabOak1Text.PokemonAroundTheWorldText")
            : Has(S, TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB")) ? TEXT("OaksLabOak1Text.YourPokemonCanFightText")
            : Has(S, TEXT("EVENT_GOT_STARTER")) ? TEXT("OaksLabOak1Text.YouShouldTalkToIt") : TEXT("OaksLabOak1Text.GoAheadItsYours");
        S->Seq(TEXT("oak"))->Say(Line).Run();
        return true;
    }
    if (Id == TEXT("OAKSLAB_RIVAL"))
    {
        S->Seq(TEXT("lab-rival"))->Say(Has(S, TEXT("EVENT_GOT_STARTER")) ? TEXT("OaksLabRivalText.MyPokemonLooksStrongerText") : TEXT("OaksLabRivalText.GrampsIsntAroundText")).Run();
        return true;
    }
    // ---- The Viridian Mart sells nothing until Oak has his parcel.
    if (Id == TEXT("VIRIDIANMART_CLERK"))
    {
        if (!Has(S, TEXT("EVENT_OAK_GOT_PARCEL"))) { S->Seq(TEXT("mart-parcel-wait"))->Say(TEXT("ViridianMartClerkSayHiToOakText")).Run(); return true; }
        Clerk(S, P);
        return true;
    }
    if (P->Script == TEXT("script_pokecenter_nurse") || P->Sprite == TEXT("SPRITE_NURSE")) { Nurse(S, P); return true; }
    if (Id == TEXT("BIKESHOP_CLERK")) { BikeClerk(S); return true; }
    if (P->Sprite == TEXT("SPRITE_CLERK") && D && D->Marts.Contains(P->Map)) { Clerk(S, P); return true; }
    // ---- Daisy's Town Map.
    if (Id == TEXT("BLUESHOUSE_DAISY1"))
    {
        if (S->Check(S->Event(TEXT("daisy_town_map"))))
        {
            const TSharedPtr<FJsonObject> E = S->Event(TEXT("daisy_town_map"));
            S->Seq(TEXT("daisy-map")).Get()
                .Say(TEXT("BluesHouseDaisyOfferMapText"))
                .Give(YKey::Item(TEXT("TOWN_MAP")), 1, TEXT("got a"), TEXT("GotMapText"))
                .Do([W, E]() { W->ApplySets(E); })
                .Say(TEXT("BluesHouseDaisyUseMapText"))
                .Run();
        }
        else S->Seq(TEXT("daisy"))->Say(Has(S, TEXT("EVENT_GOT_TOWN_MAP")) ? TEXT("BluesHouseDaisyUseMapText") : TEXT("BluesHouseDaisyRivalAtLabText")).Run();
        return true;
    }
    // ---- Viridian: the old man and his granddaughter.
    if (Id == TEXT("VIRIDIANCITY_GIRL"))
    {
        S->Seq(TEXT("granddaughter"))->Say(Has(S, TEXT("EVENT_GOT_POKEDEX")) ? TEXT("ViridianCityPrintGirlText.WhenIGoShopText") : TEXT("ViridianCityPrintGirlText.HasntHadHisCoffeeYetText")).Run();
        return true;
    }
    if (Id == TEXT("VIRIDIANCITY_OLD_MAN_SLEEPY")) return RunEvent(S, TEXT("viridian_old_man_sleepy_blocks_north"), P);
    if (Id == TEXT("VIRIDIANCITY_OLD_MAN2")) return RunEvent(S, TEXT("viridian_old_man_catch_demo_first"), P);
    if (Id == TEXT("VIRIDIANCITY_OLD_MAN")) return RunEvent(S, TEXT("viridian_old_man_catch_demo_repeat"), P);
    if (Id == TEXT("PEWTERCITY_YOUNGSTER")) return RunEvent(S, TEXT("pewter_youngster_leads_to_gym"), P);
    if (Id == TEXT("ROUTE22GATE_GUARD"))
    {
        const bool bBadge = St(S) && St(S)->HasBadge(EPYRBadge::Boulder);
        S->Seq(TEXT("gate-guard"))->Say(bBadge ? TEXT("Route22GateGuardGoRightAheadText") : TEXT("Route22GateGuardNoBoulderbadgeText")).Run();
        return true;
    }
    if (Id == TEXT("ROUTE2TRADEHOUSE_GAMEBOY_KID")) return RunEvent(S, TEXT("trade_route2"), P);
    if (Id == TEXT("MUSEUM1F_SCIENTIST1"))
    {
        S->Seq(TEXT("museum-desk"))->Say(Has(S, TEXT("EVENT_BOUGHT_MUSEUM_TICKET")) ? TEXT("Museum1FPrintScientist1Text.TakePlentyOfTimeText") : TEXT("Museum1FPrintScientist1Text.GoToOtherSideText")).Run();
        return true;
    }
    if (Id == TEXT("MTMOONB2F_DOME_FOSSIL") || Id == TEXT("MTMOONB2F_HELIX_FOSSIL"))
        return RunEvent(S, Id == TEXT("MTMOONB2F_DOME_FOSSIL") ? TEXT("mt_moon_dome_fossil") : TEXT("mt_moon_helix_fossil"), P);
    // ---- Gym leaders, the Rocket thief and the Nugget Bridge man: beaten, their last lines.
    if (Id == TEXT("PEWTERGYM_BROCK") && Has(S, TEXT("EVENT_BEAT_BROCK"))) { S->Seq(TEXT("brock-after"))->Say(TEXT("PewterGymBrockText.PostBattleAdviceText")).Run(); return true; }
    if (Id == TEXT("CERULEANGYM_MISTY") && Has(S, TEXT("EVENT_BEAT_MISTY"))) { S->Seq(TEXT("misty-after"))->Say(TEXT("CeruleanGymMistyText.TM11ExplanationText")).Run(); return true; }
    if (Id == TEXT("PEWTERGYM_BROCK")) return RunEvent(S, TEXT("pewter_gym_brock_tm34"), P);
    if (Id == TEXT("CERULEANGYM_MISTY")) return RunEvent(S, TEXT("cerulean_gym_misty_tm11"), P);
    if (Id == TEXT("PEWTERGYM_GYM_GUIDE"))
    {
        S->Seq(TEXT("gym-guide"))->Say(Has(S, TEXT("EVENT_BEAT_BROCK")) ? TEXT("PewterGymGuidePostBattleText") : TEXT("PewterGymGuidePreAdviceText")).Run();
        return true;
    }
    if (Id == TEXT("BILLSHOUSE_BILL_POKEMON")) return RunEvent(S, TEXT("bills_house_bill_asks_help"), P);
    if (Id == TEXT("BILLSHOUSE_BILL1")) return RunEvent(S, TEXT("bills_house_ss_ticket"), P);
    if (Id == TEXT("MTMOONPOKECENTER_MAGIKARP_SALESMAN")) return RunEvent(S, TEXT("mt_moon_magikarp_salesman"), P);
    if (Id == TEXT("CERULEANMELANIESHOUSE_MELANIE")) return RunEvent(S, TEXT("cerulean_bulbasaur_gift"), P);
    if (Id == TEXT("ROUTE24_COOLTRAINER_M4")) return RunEvent(S, TEXT("route24_charmander_gift"), P);
    if (Id == TEXT("ROUTE1_YOUNGSTER1")) return RunEvent(S, TEXT("route1_potion_sample"), P);
    if (Id == TEXT("VIRIDIANCITY_FISHER")) return RunEvent(S, TEXT("viridian_fisher_tm42"), P);
    if (Id == TEXT("MUSEUM1F_SCIENTIST2")) return RunEvent(S, TEXT("pewter_old_amber"), P);
    // ---- Trainers: a battle until beaten, then the after-battle line.
    if (P->IsTrainer())
    {
        if (P->IsBeaten() || (!P->Trainer.Event.IsNone() && Prog(S) && Prog(S)->Has(P->Trainer.Event)))
        {
            if (!P->Trainer.AfterText.IsEmpty()) { S->Seq(TEXT("trainer-after"))->Say(P->Trainer.AfterText).Run(); return true; }
            return false;
        }
        S->Challenge(P, false);
        return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------ events
bool RunEvent(UPYRScripts* S, const FString& Id, APYRPerson* Talker)
{
    TWeakObjectPtr<UPYRScripts> W = S;
    const TSharedPtr<FJsonObject> E = S->Event(Id);
    UPYRField* F = Fld(S);
    auto Apply = [W, E]() { if (W.IsValid()) W->ApplySets(E); };

    if (Id == TEXT("route1_potion_sample"))
    {
        if (!S->Check(E)) { S->Seq(Id)->Say(TEXT("Route1PrintYoungster1Text.AlsoGotPokeballsText")).Run(); return true; }
        S->Seq(Id).Get().Say(TEXT("Route1PrintYoungster1Text.MartSampleText")).Do(Apply).Give(YKey::Item(TEXT("POTION")), 1, TEXT("got"), TEXT("Route1PrintYoungster1Text.GotPotionText")).Run();
        return true;
    }
    if (Id == TEXT("viridian_fisher_tm42"))
    {
        if (!S->Check(E)) { S->Seq(Id)->Say(TEXT("ViridianCityPrintFisherText.TM42ExplanationText")).Run(); return true; }
        S->Seq(Id).Get().Say(TEXT("ViridianCityPrintFisherText.YouCanHaveThisText")).Do(Apply).Give(YKey::Item(TEXT("TM_DREAM_EATER")), 1, TEXT("received"), TEXT("ViridianCityPrintFisherText.ReceivedTM42Text")).Run();
        return true;
    }
    if (Id == TEXT("oaks_lab_deliver_parcel_pokedex"))
    {
        S->Seq(Id).Get()
            .Say(TEXT("OaksLabOak1Text.DeliverParcelText"))
            .Do([W]() { if (UPYRTrainerState* T = St(W.Get())) T->RemoveItem(UPYRTrainerState::OaksParcel, 1); })
            .Say(TEXT("_OaksLabOak1ParcelThanksText"))
            .Music(TEXT("meet_rival"))
            .Say(TEXT("OaksLabRivalGrampsText"))
            .Say(TEXT("OaksLabRivalMyPokemonHasGrownStrongerText"))
            .Say(TEXT("OaksLabOakIHaveARequestText"))
            .Say(TEXT("OaksLabOakMyInventionPokedexText"))
            .Jingle(TEXT("get_key_item"))
            .Say(TEXT("OaksLabOakGotPokedexText"))
            .Do(Apply)
            .Say(TEXT("OaksLabOakThatWasMyDreamText"))
            .Say(TEXT("OaksLabRivalLeaveItAllToMeText"))
            .Music(TEXT("oaks_lab"))
            .Run();
        return true;
    }
    if (Id == TEXT("oaks_lab_poke_balls"))
    {
        S->Seq(Id).Get()
            .Do(Apply)
            .Jingle(TEXT("get_item_1"))
            .Do([W]() { if (UPYRTrainerState* T = St(W.Get())) T->AddItem(UPYRTrainerState::PokeBall, 5); })
            .Say(TEXT("OaksLabOak1Text.GivePokeballsText"))
            .Say(TEXT("_OaksLabGivePokeballsExplanationText"))
            .Run();
        return true;
    }
    if (Id == TEXT("viridian_old_man_sleepy_blocks_north"))
    {
        // Lying across the road until the Pokedex: his line and a step back down.
        S->Seq(Id).Get().Say(TEXT("ViridianCityPrintOldManSleepyText.PrivatePropertyText")).WalkRed({EYDir::Down}).Run();
        return true;
    }
    if (Id == TEXT("viridian_old_man_catch_demo_first") || Id == TEXT("viridian_old_man_catch_demo_repeat"))
    {
        const bool bFirst = Id == TEXT("viridian_old_man_catch_demo_first");
        if (!S->Check(E)) return false;
        auto Demo = [W, E](FPYRSeq& Q)
        {
            Q.Fight([](FPYRFight& Fi)
            {
                Fi.Kind = EPYRBattleKind::Demo;
                FPYRMonSpec R;
                R.Species = TEXT("rattata");
                R.Level = 5;
                Fi.Foes = {R};
                Fi.bNoBlackout = true;
                Fi.Stage = EPYRStageKind::Grass;
                Fi.bStageSet = true;
                Fi.TrainerName = TEXT("OLD MAN");
            }, [W, E](EPYRBattleOutcome) { if (W.IsValid()) W->ApplySets(E); });
        };
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        if (bFirst)
        {
            Q->Say(TEXT("ViridianCityOldMan2Text.HadMyCoffeeNowText"));
            Demo(Q.Get());
            Q->Say(TEXT("ViridianCityOldMan2Text.LosingMyTouchText"));
            Q->Do([W]() { if (APYRPerson* O = Person(W.Get(), TEXT("VIRIDIAN_CITY"), TEXT("VIRIDIANCITY_OLD_MAN2"))) O->SetShown(false); });
            Q->Run();
            return true;
        }
        Q->Then([W, Demo](TFunction<void()> Next)
        {
            Mn(W.Get())->Ask(W->Line(TEXT("ViridianCityPrintOldManText.WantMeToShowYouAgainText")), [W, Demo, Next](bool bYes)
            {
                if (!bYes) { Mn(W.Get())->Say(W->Line(TEXT("ViridianCityPrintOldManText.NotGoodEnoughForYouText")), Next); return; }
                TSharedRef<FPYRSeq> Q2 = W->Seq(TEXT("old-man-demo-again"));
                Q2->bLock = false;
                Q2->Say(TEXT("ViridianCityPrintOldManText.WatchCloselyText"));
                Demo(Q2.Get());
                Q2->OnEnd = Next;
                Q2->Run();
            });
        });
        Q->Run();
        return true;
    }
    if (Id == TEXT("route22_rival_battle_1"))
    {
        FPYRFight Fi;
        if (!EventFight(S, Id, Fi)) return false;
        APYRPerson* Rival = Person(S, TEXT("ROUTE_22"), TEXT("ROUTE22_RIVAL1"));
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Music(TEXT("meet_rival"));
        if (Rival && F)
        {
            // He runs in from the west and stops next to Red.
            Q->Do([W, Rival]()
            {
                UPYRField* F2 = Fld(W.Get());
                Rival->Teleport(F2->KantoTile + FIntPoint(-4, 0), EYDir::Right);
                Rival->SetShown(true);
            });
            Q->WalkPerson(Rival, {EYDir::Right, EYDir::Right, EYDir::Right});
            Q->FaceRed(EYDir::Left);
        }
        Fi.WinText = S->Line(TEXT("Route22Rival1DefeatedText"));
        Q->Say(TEXT("Route22RivalBeforeBattleText1"));
        Q->Fight([Fi](FPYRFight& Out) { Out = Fi; }, [W, E](EPYRBattleOutcome Out)
        {
            if (Out == EPYRBattleOutcome::Won && W.IsValid()) W->ApplySets(E);
        });
        Q->Say(TEXT("Route22RivalAfterBattleText1"));
        if (Rival) Q->WalkPerson(Rival, {EYDir::Left, EYDir::Left, EYDir::Left, EYDir::Left});
        Q->Do([Rival]() { if (Rival) Rival->SetShown(false); });
        Q->Run();
        return true;
    }
    if (Id == TEXT("route22_gate_blocked"))
    {
        S->Seq(Id).Get().Say(TEXT("Route22GateGuardNoBoulderbadgeText")).WalkRed({EYDir::Down}).Run();
        return true;
    }
    if (Id == TEXT("route22_gate_pass"))
    {
        if (Has(S, TEXT("route22_gate_passed_this_visit"))) return false;
        SetFlag(S, TEXT("route22_gate_passed_this_visit"));
        S->Seq(Id)->Say(TEXT("Route22GateGuardGoRightAheadText")).Run();
        return true;
    }
    if (Id == TEXT("pewter_museum_ticket"))
    {
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Then([W, E](TFunction<void()> Next)
        {
            UPYRMenus* M = Mn(W.Get());
            M->Ask(W->Line(TEXT("Museum1FPrintScientist1Text.WouldYouLikeToComeInText")), [W, E, M, Next](bool bYes)
            {
                UPYRTrainerState* T = St(W.Get());
                if (bYes && T && T->Money >= 50)
                {
                    T->AddMoney(-50);
                    W->ApplySets(E);
                    M->Say(W->Line(TEXT("Museum1FPrintScientist1Text.ThankYouText")), Next);
                    return;
                }
                const FString No = bYes ? W->Line(TEXT("Museum1FPrintScientist1Text.DontHaveEnoughMoneyText")) + TEXT("\f") : FString();
                M->Say(No + W->Line(TEXT("Museum1FPrintScientist1Text.ComeAgainText")), [W, Next]()
                {
                    TSharedRef<FPYRSeq> Back = W->Seq(TEXT("museum-push"));
                    Back->bLock = false;
                    Back->WalkRed({EYDir::Down});
                    Back->OnEnd = Next;
                    Back->Run();
                });
            });
        });
        Q->Run();
        return true;
    }
    if (Id == TEXT("pewter_old_amber"))
    {
        if (!S->Check(E)) { S->Seq(Id)->Say(TEXT("Museum1FPrintScientist2Text.GetTheOldAmberCheckText")).Run(); return true; }
        S->Seq(Id).Get().Say(TEXT("Museum1FPrintScientist2Text.TakeThisToAPokemonLabText")).Do(Apply).Give(YKey::Item(TEXT("OLD_AMBER")), 1, TEXT("received"), TEXT("Museum1FPrintScientist2Text.ReceivedOldAmberText")).Run();
        return true;
    }
    if (Id == TEXT("pewter_youngster_leads_to_gym"))
    {
        if (Has(S, TEXT("EVENT_BEAT_BROCK"))) return false;
        // "Follow me!": he walks Red to the gym's door.
        const AYellowWorld* Wd = F ? F->GetYellowWorld() : nullptr;
        const FYBuilding* Gym = Wd ? Wd->BuildingWithDoorTo(TEXT("PEWTER_GYM")) : nullptr;
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Say(TEXT("PewterCityYoungsterText.YoureATrainerFollowMeText"));
        if (Gym && Gym->Doors.Num())
        {
            const FIntPoint Door = Gym->Doors[0];
            Q->Fade(1.f, .3f);
            Q->Do([W, Door]()
            {
                UPYRField* F2 = Fld(W.Get());
                const FYMap* Town = F2 && F2->GetYellowWorld() ? F2->GetYellowWorld()->MapAtKanto(Door + FIntPoint(0, 1)) : nullptr;
                if (Town) F2->PlaceRed(Town->Id, Door.X - Town->Origin.X, Door.Y + 1 - Town->Origin.Y, EYDir::Up);
                if (APYRPerson* Boy = Person(W.Get(), TEXT("PEWTER_CITY"), TEXT("PEWTERCITY_YOUNGSTER"))) Boy->Teleport(Door + FIntPoint(1, 1), EYDir::Left);
            });
            Q->Fade(0.f, .3f);
        }
        Q->Say(TEXT("PewterCityYoungsterGoTakeOnBrockText"));
        Q->Do([W]() { if (APYRPerson* Boy = Person(W.Get(), TEXT("PEWTER_CITY"), TEXT("PEWTERCITY_YOUNGSTER"))) Boy->Teleport(Boy->Home, Boy->HomeFacing); });
        Q->Run();
        return true;
    }
    if (Id == TEXT("pewter_gym_brock_tm34") || Id == TEXT("cerulean_gym_misty_tm11"))
    {
        const bool bBrock = Id.StartsWith(TEXT("pewter"));
        FPYRFight Fi;
        if (!EventFight(S, Id, Fi)) return false;
        Fi.Kind = EPYRBattleKind::Gym;
        Fi.Stage = bBrock ? EPYRStageKind::PewterGym : EPYRStageKind::CeruleanGym;
        Fi.bStageSet = true;
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Say(bBrock ? TEXT("PewterGymBrockText.PreBattleText") : TEXT("CeruleanGymMistyText.PreBattleText"));
        Q->Fight([Fi](FPYRFight& Out) { Out = Fi; }, [W, E, bBrock](EPYRBattleOutcome Out)
        {
            if (Out != EPYRBattleOutcome::Won || !W.IsValid()) return;
            W->ApplySets(E);
            if (bBrock)
            {
                // Beating Brock hides the Route 22 rival for good.
                SetFlag(W.Get(), TEXT("EVENT_1ST_ROUTE22_RIVAL_BATTLE"), false);
                SetFlag(W.Get(), TEXT("EVENT_ROUTE22_RIVAL_WANTS_BATTLE"), false);
            }
        });
        Q->Then([W, bBrock](TFunction<void()> Next)
        {
            if (!Has(W.Get(), bBrock ? TEXT("EVENT_BEAT_BROCK") : TEXT("EVENT_BEAT_MISTY"))) { Next(); return; }
            UPYRMenus* M = Mn(W.Get());
            if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(W.Get())) Music->Jingle(TEXT("get_key_item"));
            M->Say(W->Line(bBrock ? TEXT("PewterGymBrockReceivedBoulderBadgeText") : TEXT("CeruleanGymMistyReceivedCascadeBadgeText")), [W, bBrock, Next]()
            {
                Fld(W.Get())->GiveItem(YKey::Item(bBrock ? TEXT("TM_BIDE") : TEXT("TM_BUBBLEBEAM")), 1, TEXT("received"), [W, bBrock, Next]()
                {
                    // Misty explains TM11 after it (scripts/CeruleanGym.asm); Brock's TM34 line already does.
                    if (bBrock || !W.IsValid()) { Next(); return; }
                    Mn(W.Get())->Say(W->Line(TEXT("CeruleanGymMistyText.TM11ExplanationText")), Next);
                }, bBrock ? TEXT("PewterGymReceivedTM34Text") : TEXT("CeruleanGymMistyReceivedTM11Text"));
            });
        });
        Q->Run();
        return true;
    }
    if (Id == TEXT("mt_moon_b2f_super_nerd_battle"))
    {
        FPYRFight Fi;
        if (!EventFight(S, Id, Fi)) return false;
        Fi.WinText = S->Line(TEXT("MtMoonB2FSuperNerdOkIllShareText"));
        S->Seq(Id).Get()
            .Music(TEXT("meet_male_trainer"))
            .Say(TEXT("MtMoonB2FSuperNerdTheyreBothMineText"))
            .Fight([Fi](FPYRFight& Out) { Out = Fi; }, [W, E](EPYRBattleOutcome Out) { if (Out == EPYRBattleOutcome::Won && W.IsValid()) W->ApplySets(E); })
            .Say(TEXT("MtMoonB2fSuperNerdEachTakeOneText"))
            .Run();
        return true;
    }
    if (Id == TEXT("mt_moon_dome_fossil") || Id == TEXT("mt_moon_helix_fossil"))
    {
        if (!S->Check(E)) return false;
        const bool bDome = Id.Contains(TEXT("dome"));
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Then([W, E, bDome](TFunction<void()> Next)
        {
            UPYRMenus* M = Mn(W.Get());
            const FString Ask = W->Line(bDome ? TEXT("MtMoonB2FDomeFossilText.YouWantText") : TEXT("MtMoonB2FHelixFossilText.YouWantText"));
            M->Ask(Ask, [W, E, bDome, Next](bool bYes)
            {
                if (!bYes) { Next(); return; }
                W->ApplySets(E);
                Fld(W.Get())->GiveItem(YKey::Item(bDome ? TEXT("DOME_FOSSIL") : TEXT("HELIX_FOSSIL")), 1, TEXT("got the"), Next, TEXT("MtMoonB2FReceivedFossilText.Text"));
            });
        });
        Q->Run();
        return true;
    }
    if (Id == TEXT("mt_moon_jessie_james"))
    {
        FPYRFight Fi;
        if (!EventFight(S, Id, Fi)) return false;
        Fi.TrainerName = TEXT("JESSIE & JAMES");
        Fi.TrainerModel = TEXT("jessie,james");
        Fi.TrainerHeight = 172.f;
        Fi.WinText = S->Line(TEXT("MtMoonB2FJessieJamesEndBattleText"));
        APYRPerson* Jessie = Person(S, TEXT("MT_MOON_B2F"), TEXT("MTMOONB2F_JESSIE"));
        APYRPerson* James = Person(S, TEXT("MT_MOON_B2F"), TEXT("MTMOONB2F_JAMES"));
        S->Seq(Id).Get()
            .Music(TEXT("meet_jessie_james"))
            .Do([Jessie, James]() { if (Jessie) Jessie->SetShown(true); if (James) James->SetShown(true); })
            .WalkPerson(Jessie, {EYDir::Left})
            .Do([Jessie]() { if (Jessie) Jessie->Face(EYDir::Down); })
            .WalkPerson(James, {EYDir::Left, EYDir::Left, EYDir::Left, EYDir::Left, EYDir::Left})
            .Do([James]() { if (James) James->Face(EYDir::Left); })
            .Say(TEXT("MtMoonB2FText12"))
            .Say(TEXT("MtMoonB2FText13"))
            .Fight([Fi](FPYRFight& Out) { Out = Fi; }, [W, E](EPYRBattleOutcome Out) { if (Out == EPYRBattleOutcome::Won && W.IsValid()) W->ApplySets(E); })
            .Say(TEXT("MtMoonB2FText14"))
            .Do([Jessie, James]() { if (Jessie) Jessie->SetShown(false); if (James) James->SetShown(false); })
            .Run();
        return true;
    }
    if (Id == TEXT("mt_moon_magikarp_salesman"))
    {
        if (Has(S, TEXT("EVENT_BOUGHT_MAGIKARP"))) { S->Seq(Id)->Say(TEXT("MagikarpSalesman.NoRefundsText")).Run(); return true; }
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Then([W, E](TFunction<void()> Next)
        {
            UPYRMenus* M = Mn(W.Get());
            M->Ask(W->Line(TEXT("MagikarpSalesman.IGotADealText")), [W, E, M, Next](bool bYes)
            {
                UPYRTrainerState* T = St(W.Get());
                if (!bYes) { M->Say(W->Line(TEXT("MagikarpSalesman.NoText")), Next); return; }
                if (!T || T->Money < 500) { M->Say(W->Line(TEXT("MagikarpSalesman.NoMoneyText")), Next); return; }
                T->AddMoney(-500);
                W->ApplySets(E);
                TSharedRef<FPYRSeq> Q2 = W->Seq(TEXT("magikarp-bought"));
                Q2->bLock = false;
                Q2->GiveMon(TEXT("magikarp"), 5);
                Q2->OnEnd = Next;
                Q2->Run();
            });
        });
        Q->Run();
        return true;
    }
    if (Id == TEXT("cerulean_rival_battle"))
    {
        FPYRFight Fi;
        if (!EventFight(S, Id, Fi)) return false;
        Fi.WinText = S->Line(TEXT("CeruleanCityRivalDefeatedText"));
        APYRPerson* Rival = Person(S, TEXT("CERULEAN_CITY"), TEXT("CERULEANCITY_RIVAL"));
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Music(TEXT("meet_rival"));
        if (Rival)
        {
            Q->Do([W, Rival]() { UPYRField* F2 = Fld(W.Get()); Rival->Teleport(F2->KantoTile + FIntPoint(0, -3), EYDir::Down); Rival->SetShown(true); });
            Q->WalkPerson(Rival, {EYDir::Down, EYDir::Down});
            Q->FaceRed(EYDir::Up);
        }
        Q->Say(TEXT("CeruleanCityRivalText.PreBattleText"));
        Q->Fight([Fi](FPYRFight& Out) { Out = Fi; }, [W, E](EPYRBattleOutcome Out) { if (Out == EPYRBattleOutcome::Won && W.IsValid()) W->ApplySets(E); });
        Q->Say(TEXT("CeruleanCityRivalIWentToBillsText"));
        if (Rival) Q->WalkPerson(Rival, {EYDir::Down, EYDir::Down, EYDir::Down, EYDir::Down});
        Q->Do([Rival]() { if (Rival) Rival->SetShown(false); });
        Q->Run();
        return true;
    }
    if (Id == TEXT("cerulean_rocket_thief_battle"))
    {
        APYRPerson* Rocket = Person(S, TEXT("CERULEAN_CITY"), TEXT("CERULEANCITY_ROCKET"));
        FPYRFight Fi;
        if (!EventFight(S, Id, Fi)) return false;
        Fi.WinText = S->Line(TEXT("CeruleanCityRocketText.IGiveUpText"));
        S->Seq(Id).Get()
            .Music(TEXT("meet_evil_trainer"))
            .Say(TEXT("CeruleanCityRocketText.Text"))
            .Fight([Fi](FPYRFight& Out) { Out = Fi; }, [W, E](EPYRBattleOutcome Out) { if (Out == EPYRBattleOutcome::Won && W.IsValid()) W->ApplySets(E); })
            .Then([W](TFunction<void()> Next)
            {
                if (!Has(W.Get(), TEXT("EVENT_BEAT_CERULEAN_ROCKET_THIEF"))) { Next(); return; }
                Mn(W.Get())->Say(W->Line(TEXT("CeruleanCityRocketText.IllReturnTheTMText")), [W, Next]()
                {
                    Fld(W.Get())->GiveItem(YKey::Item(TEXT("TM_DIG")), 1, TEXT("recovered"), Next, TEXT("CeruleanCityRocketText.ReceivedTM28Text"));
                });
            })
            .Do([Rocket]() { if (Rocket) Rocket->SetShown(false); })
            .Run();
        return true;
    }
    if (Id == TEXT("cerulean_bulbasaur_gift"))
    {
        if (Has(S, TEXT("EVENT_GOT_BULBASAUR_IN_CERULEAN"))) { S->Seq(Id)->Say(TEXT("CeruleanHouse1Text_1cfd9")).Run(); return true; }
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Say(TEXT("CeruleanHouse1Text_1cfc8"));
        // Yellow: only when Pikachu is happy enough with Red (147 or more).
        if (S->Check(E))
            Q->Then([W, E](TFunction<void()> Next)
            {
                UPYRMenus* M = Mn(W.Get());
                M->Ask(W->Line(TEXT("CeruleanHouse1Text_1cfce")), [W, E, M, Next](bool bYes)
                {
                    if (!bYes) { M->Say(W->Line(TEXT("CeruleanHouse1Text_1cfdf")), Next); return; }
                    W->ApplySets(E);
                    TSharedRef<FPYRSeq> Q2 = W->Seq(TEXT("bulbasaur-given"));
                    Q2->bLock = false;
                    Q2->GiveMon(TEXT("bulbasaur"), 10);
                    Q2->Say(TEXT("CeruleanHouse1Text_1cfd3"));
                    Q2->OnEnd = Next;
                    Q2->Run();
                });
            });
        Q->Run();
        return true;
    }
    if (Id == TEXT("route24_nugget_bridge_prize"))
    {
        FPYRFight Fi;
        if (!EventFight(S, Id, Fi)) return false;
        Fi.WinText = S->Line(TEXT("Route24CooltrainerM1Text.DefeatedText"));
        S->Seq(Id).Get()
            .Say(TEXT("Route24CooltrainerM1Text.YouBeatOurContestText"))
            .Give(YKey::Item(TEXT("NUGGET")), 1, TEXT("received"), TEXT("Route24CooltrainerM1Text.ReceivedNuggetText"))
            .Do([W]() { SetFlag(W.Get(), TEXT("EVENT_GOT_NUGGET")); })
            .Music(TEXT("meet_evil_trainer"))
            .Say(TEXT("Route24CooltrainerM1Text.JoinTeamRocketText"))
            .Fight([Fi](FPYRFight& Out) { Out = Fi; }, [W](EPYRBattleOutcome Out) { if (Out == EPYRBattleOutcome::Won) SetFlag(W.Get(), TEXT("EVENT_BEAT_ROUTE24_ROCKET")); })
            .Say(TEXT("Route24CooltrainerM1Text.YouCouldBecomeATopLeaderText"))
            .Run();
        return true;
    }
    if (Id == TEXT("route24_charmander_gift"))
    {
        if (!S->Check(E)) { S->Seq(Id)->Say(TEXT("Route24Text_515ee")).Run(); return true; }
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Then([W, E](TFunction<void()> Next)
        {
            UPYRMenus* M = Mn(W.Get());
            M->Ask(W->Line(TEXT("Route24Text_515de")), [W, E, M, Next](bool bYes)
            {
                if (!bYes) { M->Say(W->Line(TEXT("Route24Text_515e9")), Next); return; }
                W->ApplySets(E);
                TSharedRef<FPYRSeq> Q2 = W->Seq(TEXT("charmander-given"));
                Q2->bLock = false;
                Q2->GiveMon(TEXT("charmander"), 10);
                Q2->Say(TEXT("Route24Text_515e3"));
                Q2->OnEnd = Next;
                Q2->Run();
            });
        });
        Q->Run();
        return true;
    }
    if (Id == TEXT("bills_house_bill_asks_help"))
    {
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Then([W, E](TFunction<void()> Next)
        {
            UPYRMenus* M = Mn(W.Get());
            auto Help = [W, E, M, Next]()
            {
                M->Say(W->Line(TEXT("BillsHousePrintBillPokemonText.UseSeparationSystemText")), [W, E, Next]()
                {
                    W->ApplySets(E);
                    if (APYRPerson* Bill = Person(W.Get(), TEXT("BILLS_HOUSE"), TEXT("BILLSHOUSE_BILL_POKEMON"))) Bill->SetShown(false);
                    Next();
                });
            };
            M->Ask(W->Line(TEXT("BillsHousePrintBillPokemonText.ImNotAPokemonText")), [W, M, Help](bool bYes)
            {
                if (bYes) Help();
                else M->Say(W->Line(TEXT("BillsHousePrintBillPokemonText.NoYouGottaHelpText")), Help);
            });
        });
        Q->Run();
        return true;
    }
    if (Id == TEXT("bills_house_cell_separator_pc"))
    {
        S->Seq(Id).Get()
            .Say(TEXT("BillsHouseInitiatedText"))
            .Do(Apply)
            .Wait(.6f)
            .Do([W]() { PYRStory::Refresh(W.Get()); })
            .Run();
        return true;
    }
    if (Id == TEXT("bills_house_ss_ticket"))
    {
        if (!S->Check(E)) { S->Seq(Id)->Say(TEXT("BillsHousePrintBillSSTicketText.WhyDontYouGoInsteadOfMeText")).Run(); return true; }
        S->Seq(Id).Get()
            .Say(TEXT("BillsHousePrintBillSSTicketText.ThankYouText"))
            .Do(Apply)
            .Give(YKey::Item(TEXT("S_S_TICKET")), 1, TEXT("received an"), TEXT("BillsHousePrintBillSSTicketText.SSTicketReceivedText"))
            .Do([W]() { PYRStory::Refresh(W.Get()); })
            .Say(TEXT("BillsHousePrintBillSSTicketText.WhyDontYouGoInsteadOfMeText"))
            .Run();
        return true;
    }
    if (Id == TEXT("trade_route2"))
    {
        // In-game trade from trades.json: the wanted Pokemon for theirs, with its nickname and another trainer's ID.
        const FYellowDataSet* D = Data(S);
        TSharedPtr<FJsonObject> Trade;
        if (D) for (const TSharedPtr<FJsonValue>& V : D->Trades) if (V->Type == EJson::Object && V->AsObject()->GetStringField(TEXT("map")) == TEXT("ROUTE_2_TRADE_HOUSE")) Trade = V->AsObject();
        if (!Trade.IsValid()) return false;
        const FName Give = YKey::Species(Trade->GetStringField(TEXT("give"))), Get = YKey::Species(Trade->GetStringField(TEXT("get")));
        const FString Nick = Trade->GetStringField(TEXT("nickname"));
        if (Has(S, TEXT("trade_route2_done")))
        {
            S->Seq(Id)->Say(PYRSysText::FillIn(S->Line(TEXT("AfterTrade1Text")), {{TEXT("wInGameTradeReceiveMonName"), PYRScreens::SpeciesName(Get)}})).Run();
            return true;
        }
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Then([W, Give, Get, Nick](TFunction<void()> Next)
        {
            UPYRMenus* M = Mn(W.Get());
            M->Ask(PYRSysText::FillIn(W->Line(TEXT("WannaTrade1Text")), {{TEXT("wInGameTradeGiveMonName"), PYRScreens::SpeciesName(Give)}, {TEXT("wInGameTradeReceiveMonName"), PYRScreens::SpeciesName(Get)}}), [W, Give, Get, Nick, M, Next](bool bYes)
            {
                if (!bYes) { M->Say(W->Line(TEXT("NoTrade1Text")), Next); return; }
                PYRScreens::ChooseMon(W.Get(), TEXT("Trade which\nPOKéMON?"), [W, Give, Get, Nick, M, Next](int32 I)
                {
                    UPYRTrainerState* T = St(W.Get());
                    if (!T || !T->Party.IsValidIndex(I)) { M->Say(W->Line(TEXT("NoTrade1Text")), Next); return; }
                    if (T->Party[I].Species != Give)
                    {
                        M->Say(PYRSysText::FillIn(W->Line(TEXT("WrongMon1Text")), {{TEXT("wInGameTradeGiveMonName"), PYRScreens::SpeciesName(Give)}}), Next);
                        return;
                    }
                    FPYRMon New = UPYRTrainerState::MakeMon(Get, T->Party[I].Level);
                    New.Nickname = Nick;
                    New.OriginalTrainer = TEXT("TRAINER");
                    New.OtName = TEXT("TRAINER");
                    New.OtId = 12345;
                    T->Party[I] = New;
                    T->MarkCaught(Get);
                    SetFlag(W.Get(), TEXT("trade_route2_done"));
                    M->Say(W->Line(TEXT("ConnectCableText")) + TEXT("\f") + PYRSysText::FillIn(W->Line(TEXT("TradedForText")), {{TEXT("wInGameTradeGiveMonName"), PYRScreens::SpeciesName(Give)}, {TEXT("wInGameTradeReceiveMonName"), PYRScreens::SpeciesName(Get)}}) + TEXT("\f") + W->Line(TEXT("Thanks1Text")), Next);
                });
            });
        });
        Q->Run();
        return true;
    }
    if (Id == TEXT("oak_stops_red"))
    {
        // Yellow's opening: Oak runs up, catches the Pikachu that leaps out, and leads Red back to the lab.
        const AYellowWorld* Wd = F ? F->GetYellowWorld() : nullptr;
        const FYMap* Pallet = Wd ? Wd->GetData()->Map(TEXT("PALLET_TOWN")) : nullptr;
        const int32 LabWarp = WarpTo(Pallet, TEXT("OAKS_LAB"));
        APYRPerson* Oak = Person(S, TEXT("PALLET_TOWN"), TEXT("PALLETTOWN_OAK"));
        TSharedRef<FPYRSeq> Q = S->Seq(Id);
        Q->Music(TEXT("meet_prof_oak"));
        Q->Say(TEXT("PalletTownOakText.HeyWaitDontGoOutText"));
        if (Oak)
        {
            Q->Do([W, Oak]() { UPYRField* F2 = Fld(W.Get()); Oak->Teleport(F2->KantoTile + FIntPoint(0, 3), EYDir::Up); Oak->SetShown(true); });
            Q->WalkPerson(Oak, {EYDir::Up, EYDir::Up});
            Q->FaceRed(EYDir::Down);
        }
        Q->Say(TEXT("PalletTownOakText.WhewText"));
        Q->Say(TEXT("PalletTownOakText.ThatWasCloseText"));
        Q->Fade(1.f, .3f);
        Q->Do([W, Oak, LabWarp]()
        {
            SetFlag(W.Get(), TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"));
            if (Oak) Oak->SetShown(false);
            UPYRField* F2 = Fld(W.Get());
            if (F2 && LabWarp != INDEX_NONE) F2->TakeWarp(TEXT("PALLET_TOWN"), LabWarp, false);
            if (F2) F2->PlaceRed(TEXT("OAKS_LAB"), 5, 4, EYDir::Up);
            PYRStory::Refresh(W.Get());
        });
        Q->Fade(0.f, .3f);
        Q->Music(TEXT("oaks_lab"));
        for (const TCHAR* L : {TEXT("OaksLabRivalFedUpWithWaitingText.Text"), TEXT("OaksLabOakChooseMonText.Text"), TEXT("OaksLabRivalWhatAboutMeText.Text"),
                 TEXT("OaksLabOakBePatientText.Text")})
            Q->Say(L);
        Q->Do([W]() { SetFlag(W.Get(), TEXT("EVENT_OAK_ASKED_TO_CHOOSE_MON")); });
        for (const TCHAR* L : {TEXT("OaksLabRivalTakesText1"), TEXT("OaksLabRivalTakesText2"), TEXT("OaksLabRivalTakesText3"), TEXT("OaksLabRivalTakesText4"), TEXT("OaksLabRivalTakesText5")})
            Q->Say(L);
        Q->Do([W]() { if (APYRPerson* Ball = Person(W.Get(), TEXT("OAKS_LAB"), TEXT("OAKSLAB_EEVEE_POKE_BALL"))) Ball->SetShown(false); });
        Q->Say(TEXT("OaksLabOakGivesText"));
        Q->Jingle(TEXT("get_key_item"));
        Q->Do([W]()
        {
            if (UPYRTrainerState* T = St(W.Get())) T->GiveStarter();
            SetFlag(W.Get(), TEXT("EVENT_GOT_STARTER"));
            if (APYRPikachuFollower* Pika = APYRPikachuFollower::Find(W.Get())) Pika->SnapBehindRed();
        });
        Q->Say(PYRSysText::FillIn(S->Line(TEXT("OaksLabReceivedText")), {{TEXT("wNameBuffer"), PYRScreens::SpeciesName(TEXT("pikachu"))}}));
        Q->Run();
        return true;
    }
    if (Id == TEXT("oaks_lab_rival_battle"))
    {
        FPYRFight Fi;
        if (!EventFight(S, Id, Fi)) return false;
        Fi.Kind = EPYRBattleKind::Lab;
        Fi.bNoBlackout = true;
        Fi.Stage = EPYRStageKind::Indoor;
        Fi.bStageSet = true;
        Fi.WinText = S->Line(TEXT("OaksLabRivalIPickedTheWrongPokemonText"));
        Fi.LossText = S->Line(TEXT("OaksLabRivalAmIGreatOrWhatText"));
        S->Seq(Id).Get()
            .Music(TEXT("meet_rival"))
            .Say(TEXT("OaksLabRivalIllTakeYouOnText.Text"))
            .Fight([Fi](FPYRFight& Out) { Out = Fi; }, [W, E](EPYRBattleOutcome)
            {
                if (!W.IsValid()) return;
                W->ApplySets(E);
                if (UPYRTrainerState* T = St(W.Get())) T->HealParty();
            })
            .Say(TEXT("OaksLabRivalSmellYouLaterText.Text"))
            .Do([W]() { PYRStory::Refresh(W.Get()); })
            .Say(TEXT("OaksLabPikachuDislikesPokeballsText1.Text"))
            .Do([W]() { if (APYRPikachuFollower* Pika = APYRPikachuFollower::Find(W.Get())) Pika->PlayMood(TEXT("refuse-ball")); })
            .Say(TEXT("OaksLabPikachuDislikesPokeballsText2.Text"))
            .Run();
        return true;
    }
    if (Id == TEXT("viridian_mart_oaks_parcel"))
    {
        if (!S->Check(E)) return false;
        S->Seq(Id).Get()
            .Say(TEXT("ViridianMartClerkYouCameFromPalletTownText"))
            .Do(Apply)
            .GiveQuiet(UPYRTrainerState::OaksParcel, 1)
            .Say(TEXT("ViridianMartClerkParcelQuestText"))   // ends "<PLAYER> got OAK's PARCEL!"
            .Run();
        return true;
    }
    // Anything else: an event for this person whose conditions hold, told with its first line.
    return false;
}

// ------------------------------------------------------------------------------------------------ tiles and maps
bool OnTile(UPYRScripts* S, FName Map, FIntPoint Local)
{
    if (PYRTestFlags::bNoBlocks) return false;
    // Oak stops Red at Route 1's grass (Pallet Town's top row) until he has a Pokemon of his own.
    if (Map == TEXT("PALLET_TOWN") && Local.Y <= 1 && !Has(S, TEXT("EVENT_FOLLOWED_OAK_INTO_LAB")))
    {
        const UPYRTrainerState* T = St(S);
        if (!T || T->Party.Num() == 0) return RunEvent(S, TEXT("oak_stops_red"), nullptr);
    }
    // The lab battle: Gary challenges Red on his way out (Yellow: at row 6).
    if (Map == TEXT("OAKS_LAB") && Local.Y == 6 && Has(S, TEXT("EVENT_GOT_STARTER")) && !Has(S, TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB")))
        return RunEvent(S, TEXT("oaks_lab_rival_battle"), nullptr);
    return false;
}

void OnMap(UPYRScripts* S, FName From, FName To)
{
    // The Viridian Mart's clerk calls Red over on the first visit.
    if (To == TEXT("VIRIDIAN_MART"))
    {
        if (Has(S, TEXT("EVENT_COMPLETED_CATCH_TRAINING")) && !Has(S, TEXT("EVENT_SPAWNED_OLD_MAN_1"))) SetFlag(S, TEXT("EVENT_SPAWNED_OLD_MAN_1"));
        if (!PYRTestFlags::bNoBlocks && S->Check(S->Event(TEXT("viridian_mart_oaks_parcel")))) RunEvent(S, TEXT("viridian_mart_oaks_parcel"), nullptr);
    }
    // Pewter City's own script resets the museum ticket each visit.
    if (To == TEXT("PEWTER_CITY")) SetFlag(S, TEXT("EVENT_BOUGHT_MUSEUM_TICKET"), false);
    if (To == TEXT("ROUTE_22")) SetFlag(S, TEXT("route22_gate_passed_this_visit"), false);
    if (From == TEXT("BILLS_HOUSE") && Has(S, TEXT("EVENT_GOT_SS_TICKET"))) SetFlag(S, TEXT("EVENT_LEFT_BILLS_HOUSE_AFTER_HELPING"));
    Refresh(S);
}

bool UseTile(UPYRScripts* S, FIntPoint Kanto)
{
    UPYRField* F = Fld(S);
    const AYellowWorld* Wd = F ? F->GetYellowWorld() : nullptr;
    const FYMap* M = Wd ? Wd->MapAtKanto(Kanto) : nullptr;
    if (!M) return false;
    const FIntPoint L = Kanto - M->Origin;
    for (const FYHiddenEvent& H : M->HiddenEvents)
    {
        if (H.X != L.X || H.Y != L.Y) continue;
        // Most want Red facing a certain way (SPRITE_FACING_UP: from below).
        if (H.Arg.Contains(TEXT("FACING_UP")) && F->Facing != EYDir::Up) continue;
        const FString K = H.Kind;
        if (K == TEXT("OpenPokemonCenterPC")) { Flow(S, TEXT("pc"), [S](TFunction<void()> Done) { PYRScreens::OpenPc(S, Done); }); return true; }
        if (K == TEXT("OpenRedsPC"))
        {
            // Red's own PC at home: the item storage.
            Flow(S, TEXT("reds-pc"), [S](TFunction<void()> Done) { PYRScreens::OpenPc(S, Done); });
            return true;
        }
        if (K == TEXT("BillsHousePC"))
        {
            if (S->Check(S->Event(TEXT("bills_house_cell_separator_pc")))) return RunEvent(S, TEXT("bills_house_cell_separator_pc"), nullptr);
            S->Seq(TEXT("bills-pc"))->Say(TEXT("BillsHouseMonitorText")).Run();
            return true;
        }
        const TCHAR* Line = K == TEXT("PrintBookcaseText") ? TEXT("BookcaseText") : K == TEXT("PrintRedSNESText") ? TEXT("RedBedroomSNESText")
            : K == TEXT("DisplayOakLabEmailText") ? TEXT("OakLabEmailText") : K == TEXT("DisplayOakLabLeftPoster") ? TEXT("Push START to\nopen the MENU!")
            : K == TEXT("DisplayOakLabRightPoster") ? TEXT("The SAVE option is\non the MENU\nscreen.") : K == TEXT("PrintBenchGuyText") ? TEXT("MtMoonPokecenterBenchGuyText") : nullptr;
        if (Line) { S->Seq(K)->Say(Line).Run(); return true; }
    }
    return false;
}
}
