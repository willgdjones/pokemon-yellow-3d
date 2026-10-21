// The Game Boy's shared system lines (Oak's intro, evolution, forgetting a move, saving, the PCs, items, the Mart,
// the Centre, the Pokedex ratings), copied from pret/pokeyellow into PYRSysText.cpp by
// a one-off generator script. Map-specific lines come from YellowData's text.json.
#pragma once

#include "CoreMinimal.h"

namespace PYRSysText
{
    POKEMONYELLOWREIMAGINE_API const TMap<FString, FString>& Table();
    // A line by its pokeyellow label without the leading underscore ("IsEvolvingText"), with its RAM fills
    // ({str:wStringBuffer}, {num:hMoney}) replaced from Fills (by the name after the colon). Empty when unknown.
    POKEMONYELLOWREIMAGINE_API FString Get(const FString& Label, const TMap<FString, FString>& Fills = TMap<FString, FString>());
    // Replace {str:X} and {num:X} fills in any Yellow string.
    POKEMONYELLOWREIMAGINE_API FString FillIn(const FString& Text, const TMap<FString, FString>& Fills);
}
