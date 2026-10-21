// m4t: the one way a Pokemon learns a move: level-ups in and out of battle, a move learned on evolving, TMs and HMs
// from the bag. PYRScreens::LearnMove and LearnPending forward here; nothing else writes over a move slot.
//
// With a free slot the move goes straight in ("PIKACHU learned THUNDER WAVE!"). With four, Yellow's prompt:
//   "PIKACHU is trying to learn THUNDER WAVE!" / "But, PIKACHU can't learn more than 4 moves!" / "Delete an older move to
//   make room for THUNDER WAVE?" YES/NO.
//   YES: "Which move should be forgotten?", the four moves with their type and cooldown class, the new one beside them
//   for comparison. Picking one asks "Forget GROWL?" YES/NO (the cursor starts on NO). Only that YES replaces the slot:
//   "1, 2 and... Poof! PIKACHU forgot GROWL!" / "And... PIKACHU learned THUNDER WAVE!".
//   NO, or Back from the list: "Abandon learning THUNDER WAVE?" YES: "PIKACHU did not learn THUNDER WAVE!", the four
//   moves untouched. NO: back to the question.
//
// Input isolation: the prompt answers only menu keys (W/S or the arrows, the D-pad, Enter or E or the pad's A to
// confirm, Esc or Backspace or the pad's B to go back) and clicks or taps on its own buttons. The move keys, Space,
// Q, Tab, R, F, X and the stream's battle buttons never answer it. For 0.3 s after it opens it ignores everything, and a
// key already held when it opened is ignored until it is let go. In a battle the fight is paused under it (PauseLearn).
#pragma once

#include "CoreMinimal.h"

class SWidget;
class UObject;

namespace PYRMoveLearning
{
    POKEMONYELLOWREIMAGINE_API void RequestLearn(const UObject* Ctx, int32 PartyIndex, FName Move, TFunction<void(bool)> OnDone);
    // Every move waiting in FPYRMon::Pending, one after another.
    POKEMONYELLOWREIMAGINE_API void LearnPending(const UObject* Ctx, TFunction<void()> OnDone);

    // Seconds after opening during which the prompt ignores input.
    constexpr double InputDelay = .3;

    enum class EStage : uint8 { None, Ask, List, Confirm, Abandon };
    // The prompt on top of the screen stack, as a player sees it (tests, the bot, shots).
    struct FView
    {
        EStage Stage = EStage::None;
        int32 Cursor = 0;            // YES 0 / NO 1 in the questions; the move row in the list.
        int32 ListCursor = 0;        // The move picked in the list (the one "Forget X?" asks about).
        int32 PartyIndex = INDEX_NONE;
        FName Move;
        FString Text;                // The box's words as shown.
        TArray<FString> Rows;        // The list's rows (stage List and Confirm).
        bool bTakesInput = false;    // The 0.3 s guard is over.
    };
    POKEMONYELLOWREIMAGINE_API bool View(const UObject* Ctx, FView& Out);
    // The prompt's clickable buttons in the current stage, in order (the list's rows then BACK; YES then NO), for a
    // click or a tap through the widget's own mouse handler.
    POKEMONYELLOWREIMAGINE_API TArray<TSharedPtr<SWidget>> Buttons(const UObject* Ctx);
    // One player press towards keeping the old four (NO, then YES to abandon, then the text): the battle bot, the QA
    // story and the older tests. False when no learn prompt (or its text) is up.
    POKEMONYELLOWREIMAGINE_API bool StepKeep(const UObject* Ctx);
}
