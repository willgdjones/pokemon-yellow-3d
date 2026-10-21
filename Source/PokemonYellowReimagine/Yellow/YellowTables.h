// M4: Yellow's tables join the battle's own (Party/PYRSpecies): species.json adds the species this game had no row
// for and gives every species Yellow's full learnset, evolutions (level, item, trade), TM list and Pokedex entry;
// moves.json adds moves with Yellow's type, power, accuracy and PP (the live-battle shape and cooldown are chosen from
// the power and effect); exp.json replaces the growth formulas with Yellow's tables. Existing rows keep their
// live-battle layer (body family, colours, move shapes).
#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FYellowDataSet;
struct FYPikachuTable;
enum class EPYRType : uint8;

namespace YellowTables
{
    POKEMONYELLOWREIMAGINE_API void Import(const FYellowDataSet& Data);
    POKEMONYELLOWREIMAGINE_API int32 ImportSpecies(const TSharedPtr<FJsonObject>& SpeciesJson);
    POKEMONYELLOWREIMAGINE_API int32 ImportMoves(const TSharedPtr<FJsonObject>& MovesJson);
    POKEMONYELLOWREIMAGINE_API int32 ImportExp(const TSharedPtr<FJsonObject>& ExpJson);
    POKEMONYELLOWREIMAGINE_API void ParsePikachu(const TSharedPtr<FJsonObject>& Json, FYPikachuTable& Out);
    // "ELECTRIC", "Electric", "PSYCHIC_TYPE" -> the type.
    POKEMONYELLOWREIMAGINE_API EPYRType TypeFrom(const FString& Name);
    // "PIKACHU" -> "Pikachu" (keeps symbols such as the Nidoran signs).
    POKEMONYELLOWREIMAGINE_API FString TitleCase(const FString& GbName);
}
