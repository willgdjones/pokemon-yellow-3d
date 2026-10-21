#include "Menus/PYRSysText.h"

namespace PYRSysText
{
FString FillIn(const FString& Text, const TMap<FString, FString>& Fills)
{
    FString Out;
    Out.Reserve(Text.Len());
    int32 I = 0;
    while (I < Text.Len())
    {
        if (Text[I] == TEXT('{'))
        {
            const int32 End = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
            const int32 Colon = End != INDEX_NONE ? Text.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I) : INDEX_NONE;
            if (End != INDEX_NONE && Colon != INDEX_NONE && Colon < End)
            {
                const FString Name = Text.Mid(Colon + 1, End - Colon - 1);
                const FString* Value = Fills.Find(Name);
                Out += Value ? *Value : FString();
                I = End + 1;
                continue;
            }
        }
        Out.AppendChar(Text[I++]);
    }
    return Out;
}

FString Get(const FString& Label, const TMap<FString, FString>& Fills)
{
    const FString* Found = Table().Find(Label);
    if (!Found) return FString();
    return FillIn(*Found, Fills);
}
}
