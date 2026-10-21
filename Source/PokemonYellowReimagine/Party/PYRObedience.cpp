#include "PYRObedience.h"

namespace PYRObedience
{
EPYRObey Roll(int32 Level, int32 ObedienceLevel, TFunctionRef<int32()> Rand, bool bCanUseOtherMove)
{
    const int32 C = FMath::Clamp(ObedienceLevel, 0, 255), D = FMath::Clamp(Level, 1, 255);
    if (C >= D) return EPYRObey::Obeys;
    const int32 B = FMath::Min(255, C + D);
    auto Below = [&Rand](int32 Limit) { int32 R; do { R = Rand() & 255; } while (R >= Limit); return R; };
    if (Below(B) < C) return EPYRObey::Obeys;
    if (Below(B) < C && bCanUseOtherMove) return EPYRObey::OtherMove;
    const int32 Span = D - C;
    const int32 R = Rand() & 255;
    if (R < Span) return EPYRObey::Naps;
    if (R - Span < Span) return EPYRObey::HurtsItself;
    switch (Rand() & 3)
    {
    case 0: return EPYRObey::Loafs;
    case 1: return EPYRObey::WontObey;
    case 2: return EPYRObey::TurnsAway;
    default: return EPYRObey::IgnoresOrders;
    }
}

EPYRObey Roll(int32 Level, int32 ObedienceLevel, bool bCanUseOtherMove)
{
    return Roll(Level, ObedienceLevel, [] { return FMath::RandRange(0, 255); }, bCanUseOtherMove);
}

FString Line(EPYRObey Result, const FString& Name)
{
    // source: Yellow (data/text/text_2.asm: _LoafingAroundText, _BeganToNapText, _WontObeyText, _TurnedAwayText,
    // _IgnoredOrdersText).
    switch (Result)
    {
    case EPYRObey::Naps: return FString::Printf(TEXT("%s began to nap!"), *Name);
    case EPYRObey::HurtsItself:
    case EPYRObey::WontObey: return FString::Printf(TEXT("%s won't obey!"), *Name);
    case EPYRObey::Loafs: return FString::Printf(TEXT("%s is loafing around!"), *Name);
    case EPYRObey::TurnsAway: return FString::Printf(TEXT("%s turned away!"), *Name);
    case EPYRObey::IgnoresOrders: return FString::Printf(TEXT("%s ignored orders!"), *Name);
    default: return FString();
    }
}

float NapSeconds(int32 Turns) { return 1.2f * FMath::Clamp(Turns, 1, 7); }
}
