#include "World/PYRLetters.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "World/PYRPaint.h"

namespace
{
// 5x7 glyphs, one string of 0/1 per row, top row first.
struct FGlyph { TCHAR C; const char* Rows[7]; };
const FGlyph Glyphs[] = {
    {TEXT('A'), {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {TEXT('B'), {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
    {TEXT('C'), {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}},
    {TEXT('D'), {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
    {TEXT('E'), {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
    {TEXT('F'), {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
    {TEXT('G'), {"01110", "10001", "10000", "10111", "10001", "10001", "01111"}},
    {TEXT('H'), {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {TEXT('I'), {"01110", "00100", "00100", "00100", "00100", "00100", "01110"}},
    {TEXT('J'), {"00111", "00010", "00010", "00010", "00010", "10010", "01100"}},
    {TEXT('K'), {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
    {TEXT('L'), {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
    {TEXT('M'), {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
    {TEXT('N'), {"10001", "10001", "11001", "10101", "10011", "10001", "10001"}},
    {TEXT('O'), {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {TEXT('P'), {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
    {TEXT('Q'), {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}},
    {TEXT('R'), {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
    {TEXT('S'), {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
    {TEXT('T'), {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
    {TEXT('U'), {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {TEXT('V'), {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
    {TEXT('W'), {"10001", "10001", "10001", "10101", "10101", "10101", "01010"}},
    {TEXT('X'), {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
    {TEXT('Y'), {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}},
    {TEXT('Z'), {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
    {TEXT('0'), {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
    {TEXT('1'), {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {TEXT('2'), {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
    {TEXT('3'), {"11111", "00010", "00100", "00010", "00001", "10001", "01110"}},
    {TEXT('4'), {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
    {TEXT('5'), {"11111", "10000", "11110", "00001", "00001", "10001", "01110"}},
    {TEXT('6'), {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}},
    {TEXT('7'), {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
    {TEXT('8'), {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
    {TEXT('9'), {"01110", "10001", "10001", "01111", "00001", "00010", "01100"}},
    {TEXT('\x00e9'), {"00010", "00100", "01110", "10001", "11111", "10000", "01110"}},   // é (POKéMON)
    {TEXT('.'), {"00000", "00000", "00000", "00000", "00000", "01100", "01100"}},
    {TEXT(','), {"00000", "00000", "00000", "00000", "01100", "00100", "01000"}},
    {TEXT('!'), {"00100", "00100", "00100", "00100", "00100", "00000", "00100"}},
    {TEXT('?'), {"01110", "10001", "00001", "00010", "00100", "00000", "00100"}},
    {TEXT('\''), {"00100", "00100", "01000", "00000", "00000", "00000", "00000"}},
    {TEXT('-'), {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
    {TEXT(':'), {"00000", "01100", "01100", "00000", "01100", "01100", "00000"}},
    {TEXT('&'), {"01100", "10010", "10100", "01000", "10101", "10010", "01101"}},
    {TEXT('/'), {"00001", "00010", "00010", "00100", "01000", "01000", "10000"}},
    {TEXT(' '), {"00000", "00000", "00000", "00000", "00000", "00000", "00000"}},
};

const FGlyph* Find(TCHAR C)
{
    if (C >= TEXT('a') && C <= TEXT('z')) C = C - TEXT('a') + TEXT('A');
    for (const FGlyph& G : Glyphs) if (G.C == C) return &G;
    return nullptr;
}

bool HasLower(const FString& S)
{
    for (TCHAR C : S) if (C >= TEXT('a') && C <= TEXT('z')) return true;   // é alone (POKéMON) doesn't count
    return false;
}

// Words into at most two lines of at most MaxLine characters (a longer word stays whole on its own line).
TArray<FString> Wrap(const FString& Text, int32 MaxLine)
{
    TArray<FString> Words;
    Text.ParseIntoArrayWS(Words);
    TArray<FString> Lines;
    for (const FString& W : Words)
    {
        if (Lines.Num() > 0 && Lines.Last().Len() + 1 + W.Len() <= MaxLine) { Lines.Last() += TEXT(" ") + W; continue; }
        if (Lines.Num() == 2) break;
        Lines.Add(W);
    }
    return Lines;
}
}

namespace PYRLetters
{
bool HasGlyph(TCHAR C) { return Find(C) != nullptr; }

TArray<FString> SignLabel(const FString& Text, int32 MaxLine)
{
    // Pages (\f) and lines (\n): the first line in capitals names the place (PEWTER CITY, ROUTE 2, TRAINER TIPS); a
    // shop sign puts its name last (Grass and caves / handled easily! / BIKE SHOP).
    TArray<FString> Lines;
    Text.Replace(TEXT("\f"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), true);
    FString Pick;
    for (const FString& L : Lines)
    {
        const FString T = L.TrimStartAndEnd();
        bool bLetters = false;
        for (TCHAR C : T) bLetters |= FChar::IsAlpha(C);
        if (bLetters && !HasLower(T)) { Pick = T; break; }
    }
    if (Pick.IsEmpty() && Lines.Num() > 0) Pick = Lines[0].TrimStartAndEnd().ToUpper();
    FString Clean;
    for (TCHAR C : Pick) if (HasGlyph(C)) Clean.AppendChar(C >= TEXT('a') && C <= TEXT('z') ? C - TEXT('a') + TEXT('A') : C);
    return Wrap(Clean, MaxLine);
}

// A line is 7 glyph rows and a 2-row gap (m4v: with a 1-row gap the outline pass ran a sign's lines together).
static constexpr int32 LineRows = 9;

float FitPixel(const TArray<FString>& Lines, float Width, float Height, float MaxPx)
{
    int32 Cols = 0;
    for (const FString& L : Lines) Cols = FMath::Max(Cols, L.Len() * 6 - 1);
    const int32 Rows = Lines.Num() * LineRows - 2;
    if (Cols <= 0 || Rows <= 0) return MaxPx;
    return FMath::Min3(MaxPx, Width / Cols, Height / Rows);
}

int32 Draw(UInstancedStaticMeshComponent* Ism, const FVector& Center, const FVector& Right, const FVector& Up, const FVector& Out,
    const TArray<FString>& Lines, float Px, float Depth)
{
    if (!Ism) return 0;
    return Draw([Ism](const FVector& At, const FVector& Size, const FRotator& Rot) { PYR::Instance(Ism, At, Size, Rot); }, Center, Right, Up, Out,
        Lines, Px, Depth);
}

int32 Draw(TFunctionRef<void(const FVector& At, const FVector& Size, const FRotator& Rot)> Emit, const FVector& Center, const FVector& Right,
    const FVector& Up, const FVector& Out, const TArray<FString>& Lines, float Px, float Depth)
{
    if (Lines.Num() == 0) return 0;
    const FRotator Rot = FRotationMatrix::MakeFromXZ(Right, Up).Rotator();
    const float TotalH = (Lines.Num() * LineRows - 2) * Px;
    int32 Added = 0;
    for (int32 Li = 0; Li < Lines.Num(); ++Li)
    {
        const FString& Line = Lines[Li];
        const float LineW = (Line.Len() * 6 - 1) * Px;
        const float TopY = TotalH * .5f - Li * LineRows * Px;   // the line's top edge, up from the centre
        for (int32 Ci = 0; Ci < Line.Len(); ++Ci)
        {
            const FGlyph* G = Find(Line[Ci]);
            if (!G) continue;
            const float Left = -LineW * .5f + Ci * 6 * Px;
            for (int32 R = 0; R < 7; ++R)
            {
                // Each row's runs of set pixels as one block (fewer instances, no seams inside a stroke).
                const char* Row = G->Rows[R];
                for (int32 X = 0; X < 5; )
                {
                    if (Row[X] != '1') { ++X; continue; }
                    int32 Run = 1;
                    while (X + Run < 5 && Row[X + Run] == '1') ++Run;
                    const float Cx = Left + (X + Run * .5f) * Px, Cy = TopY - (R + .5f) * Px;
                    const FVector At = Center + Right * Cx + Up * Cy + Out * (Depth * .5f);
                    Emit(At, FVector(Run * Px, Depth, Px), Rot);
                    ++Added;
                    X += Run;
                }
            }
        }
    }
    return Added;
}
}
