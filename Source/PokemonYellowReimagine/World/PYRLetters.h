// m4u: lettering on Kanto's signs, built from little ink blocks in the Game Boy's own 5x7 pixel font, so a signpost,
// a house plate or the POKé / MART plate over a door reads from the follow camera (the blind judge called them blank).
// The blocks are instances of a cube, the same flat material as the rest of Kanto, so they cook like everything else.
#pragma once

#include "CoreMinimal.h"

class UInstancedStaticMeshComponent;

namespace PYRLetters
{
// The short label a sign's text shows on its board: its first line in capitals (the town name, TRAINER TIPS, ROUTE 2,
// BIKE SHOP, POKéMON MART), else its first words, in at most two lines of at most MaxLine characters.
TArray<FString> SignLabel(const FString& Text, int32 MaxLine = 9);

// The pixel size (cm) that fits Lines in a Width x Height area, at most MaxPx.
float FitPixel(const TArray<FString>& Lines, float Width, float Height, float MaxPx);

// Lines centred on Center, laid out along Right with Up as up, each pixel a Px cube Depth deep, out along Out (the
// face's normal; the blocks stand Depth proud of the face). Returns the number of blocks added.
int32 Draw(UInstancedStaticMeshComponent* Ism, const FVector& Center, const FVector& Right, const FVector& Up, const FVector& Out,
    const TArray<FString>& Lines, float Px, float Depth);
// The same, handing each block (its centre, its size and rotation) to Emit (a building's part of an occluder).
int32 Draw(TFunctionRef<void(const FVector& At, const FVector& Size, const FRotator& Rot)> Emit, const FVector& Center, const FVector& Right,
    const FVector& Up, const FVector& Out, const TArray<FString>& Lines, float Px, float Depth);

// Whether the font has a glyph for C (letters, digits, é and . , ! ? ' - : & / and space).
bool HasGlyph(TCHAR C);
}
