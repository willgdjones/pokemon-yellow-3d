// M4: the Game Boy screens, opened by the START menu, the people (Mart clerks, PCs, Nurse Joy) and the fights.
// Every screen follows the Game Boy's layout and flow; see each function.
#pragma once

#include "CoreMinimal.h"

class UObject;

namespace PYRScreens
{
    // START: POKéDEX (once received), POKéMON (once Red has one), ITEM, <PLAYER>, SAVE, OPTION, EXIT.
    POKEMONYELLOWREIMAGINE_API void OpenStart(const UObject* Ctx);
    // The start menu's entries as shown, in order (tests).
    POKEMONYELLOWREIMAGINE_API TArray<FString> StartEntries(const UObject* Ctx);

    // POKéMON: six rows (icon, name, level, HP); pick one for STATS, SWITCH or CANCEL.
    POKEMONYELLOWREIMAGINE_API void OpenParty(const UObject* Ctx);
    // Choose a party Pokemon (items, TMs, trades): OnPick(index), or -1 on Back. Title says what for.
    POKEMONYELLOWREIMAGINE_API void ChooseMon(const UObject* Ctx, const FString& Title, TFunction<void(int32)> OnPick);
    // The two-page summary.
    POKEMONYELLOWREIMAGINE_API void OpenStats(const UObject* Ctx, int32 PartyIndex);
    POKEMONYELLOWREIMAGINE_API void OpenPokedex(const UObject* Ctx);
    POKEMONYELLOWREIMAGINE_API void OpenDexEntry(const UObject* Ctx, FName Species, TFunction<void()> OnDone = nullptr);
    // ITEM: the 20-slot bag with USE, TOSS and SELECT.
    POKEMONYELLOWREIMAGINE_API void OpenBag(const UObject* Ctx);
    // Use an item outside battle (the bag's USE): Potions and status heals on a Pokemon, Escape Rope, Repel, Moon Stone,
    // TMs and HMs, the Town Map, a Poke Ball ("This isn't the time to use that!").
    POKEMONYELLOWREIMAGINE_API void UseItem(const UObject* Ctx, FName Item, TFunction<void()> OnDone = nullptr);
    // <PLAYER>: the trainer card with the badge case.
    POKEMONYELLOWREIMAGINE_API void OpenTrainerCard(const UObject* Ctx);
    // The trainer card's TIME line, hours:minutes of play as the Game Boy prints it ("TIME/2:14").
    POKEMONYELLOWREIMAGINE_API FString CardTime(const UObject* Ctx);
    POKEMONYELLOWREIMAGINE_API void OpenSave(const UObject* Ctx);
    POKEMONYELLOWREIMAGINE_API void OpenOptions(const UObject* Ctx);
    // A Mart clerk: BUY, SELL, QUIT with the map's stock (marts.json) at items.json's prices.
    POKEMONYELLOWREIMAGINE_API void OpenMart(const UObject* Ctx, FName MartMap, TFunction<void()> OnDone = nullptr);
    // A Pokemon Centre PC: SOMEONE's/BILL's PC, <PLAYER>'s PC, PROF.OAK's PC, LOG OFF.
    POKEMONYELLOWREIMAGINE_API void OpenPc(const UObject* Ctx, TFunction<void()> OnDone = nullptr);
    // The Game Boy letter grid (7 letters at most). OnDone gets the name (Default when left empty).
    // The Game Boy's naming grid (and the keyboard); bBeside: a panel beside the one being named (Oak's opening).
    POKEMONYELLOWREIMAGINE_API void OpenNaming(const UObject* Ctx, const FString& Prompt, const FString& Default, TFunction<void(FString)> OnDone, bool bBeside = false);
    // "Do you want to give a nickname to X?" then the grid. Storage true: the Pokemon is the last in the PC.
    POKEMONYELLOWREIMAGINE_API void OfferNickname(const UObject* Ctx, int32 PartyIndex, bool bInStorage, TFunction<void()> OnDone);
    // Learning a move with four already known: "Delete an older move to make room for X?" and the forget list.
    POKEMONYELLOWREIMAGINE_API void LearnMove(const UObject* Ctx, int32 PartyIndex, FName Move, TFunction<void(bool)> OnDone);
    // Every move waiting in FPYRMon::Pending, one after another.
    POKEMONYELLOWREIMAGINE_API void LearnPending(const UObject* Ctx, TFunction<void()> OnDone);
    // The evolution scene at At: "What? X is evolving!", the two models flickering, Back cancels, "Congratulations!".
    POKEMONYELLOWREIMAGINE_API void Evolve(const UObject* Ctx, int32 PartyIndex, FName Into, const FVector& At, float Yaw, TFunction<void(bool)> OnDone);
    // Every party Pokemon waiting to evolve (after a battle).
    POKEMONYELLOWREIMAGINE_API void EvolvePending(const UObject* Ctx, const FVector& At, float Yaw, TFunction<void()> OnDone);
    // The Town Map with Red's blinking cursor.
    POKEMONYELLOWREIMAGINE_API void OpenTownMap(const UObject* Ctx);
    // Pikachu's face for its mood.
    POKEMONYELLOWREIMAGINE_API void ShowPikachu(const UObject* Ctx, TFunction<void()> OnDone = nullptr);
    // New Game: Oak's intro, naming Red and the rival.
    POKEMONYELLOWREIMAGINE_API void Intro(const UObject* Ctx, TFunction<void()> OnDone);
    // The title: CONTINUE / NEW GAME / OPTION (CONTINUE only with a save).
    POKEMONYELLOWREIMAGINE_API void Title(const UObject* Ctx, bool bHasSave, TFunction<void(bool)> OnPick);
    // The Game Boy name of an item ("POKé BALL").
    POKEMONYELLOWREIMAGINE_API FString ItemName(const UObject* Ctx, FName Item);
    POKEMONYELLOWREIMAGINE_API FString MoveName(FName Move);
    POKEMONYELLOWREIMAGINE_API FString SpeciesName(FName Species);
}
