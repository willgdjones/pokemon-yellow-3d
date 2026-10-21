#include "Yellow/YellowData.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Party/PYRSpecies.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Yellow/YellowTables.h"

DEFINE_LOG_CATEGORY(LogYellow);

// ------------------------------------------------------------------------------------------------ directions
namespace YDir
{
FIntPoint Delta(EYDir D)
{
    switch (D)
    {
    case EYDir::Down: return FIntPoint(0, 1);
    case EYDir::Up: return FIntPoint(0, -1);
    case EYDir::Left: return FIntPoint(-1, 0);
    case EYDir::Right: return FIntPoint(1, 0);
    default: return FIntPoint(0, 0);
    }
}

float Yaw(EYDir D)
{
    switch (D)
    {
    case EYDir::Down: return 90.f;
    case EYDir::Up: return -90.f;
    case EYDir::Left: return 180.f;
    default: return 0.f;
    }
}

EYDir FromYaw(float InYaw)
{
    const float Y = FRotator::NormalizeAxis(InYaw);
    if (Y >= -45.f && Y < 45.f) return EYDir::Right;
    if (Y >= 45.f && Y < 135.f) return EYDir::Down;
    if (Y >= -135.f && Y < -45.f) return EYDir::Up;
    return EYDir::Left;
}

EYDir FromVector(const FVector& V)
{
    if (V.SizeSquared2D() < KINDA_SMALL_NUMBER) return EYDir::None;
    return FMath::Abs(V.X) >= FMath::Abs(V.Y) ? (V.X > 0 ? EYDir::Right : EYDir::Left) : (V.Y > 0 ? EYDir::Down : EYDir::Up);
}

EYDir FromName(const FString& Name)
{
    const FString N = Name.ToLower();
    if (N.Contains(TEXT("up")) || N.Contains(TEXT("north"))) return EYDir::Up;
    if (N.Contains(TEXT("left")) || N.Contains(TEXT("west"))) return EYDir::Left;
    if (N.Contains(TEXT("right")) || N.Contains(TEXT("east"))) return EYDir::Right;
    if (N.Contains(TEXT("down")) || N.Contains(TEXT("south"))) return EYDir::Down;
    return EYDir::Down;
}

EYDir Opposite(EYDir D)
{
    switch (D)
    {
    case EYDir::Down: return EYDir::Up;
    case EYDir::Up: return EYDir::Down;
    case EYDir::Left: return EYDir::Right;
    case EYDir::Right: return EYDir::Left;
    default: return EYDir::None;
    }
}

const TCHAR* Name(EYDir D)
{
    switch (D)
    {
    case EYDir::Down: return TEXT("down");
    case EYDir::Up: return TEXT("up");
    case EYDir::Left: return TEXT("left");
    case EYDir::Right: return TEXT("right");
    default: return TEXT("none");
    }
}
}

// ------------------------------------------------------------------------------------------------ keys
namespace YKey
{
FString Alnum(const FString& Any)
{
    FString Out;
    Out.Reserve(Any.Len());
    for (TCHAR C : Any)
    {
        if (C == 0x00E9 || C == 0x00C9) { Out.AppendChar(TEXT('e')); continue; }   // é, É (POKé)
        if (C == 0x2640) { Out.AppendChar(TEXT('f')); continue; }                  // ♀
        if (C == 0x2642) { Out.AppendChar(TEXT('m')); continue; }                  // ♂
        if (FChar::IsAlnum(C) && C < 128) Out.AppendChar(FChar::ToLower(C));
    }
    return Out;
}

namespace
{
FString Kebab(const FString& Any)
{
    FString S = Any;
    S.ReplaceInline(TEXT("♀"), TEXT("_f"));
    S.ReplaceInline(TEXT("♂"), TEXT("_m"));
    S.ReplaceInline(TEXT("é"), TEXT("e"));
    S.ReplaceInline(TEXT("É"), TEXT("e"));
    FString Out;
    bool bDash = false;
    for (TCHAR C : S)
    {
        if (FChar::IsAlnum(C) && C < 128) { if (bDash && Out.Len() > 0) Out.AppendChar(TEXT('-')); bDash = false; Out.AppendChar(FChar::ToLower(C)); }
        else if (C != TEXT('\'')) bDash = true;
    }
    return Out;
}

// Alnum -> known id, built from a table of ids once.
const TMap<FString, FName>& KnownSpecies()
{
    static TMap<FString, FName> Map = [] {
        TMap<FString, FName> M;
        for (const FName& Id : PYRData::AllSpecies()) M.Add(Alnum(Id.ToString()), Id);
        return M;
    }();
    return Map;
}

const TMap<FString, FName>& KnownMoves()
{
    static TMap<FString, FName> Map = [] {
        TMap<FString, FName> M;
        for (const FName& Id : PYRData::AllMoves()) M.Add(Alnum(Id.ToString()), Id);
        return M;
    }();
    return Map;
}
}

FName Item(const FString& Any)
{
    return Any.IsEmpty() ? NAME_None : FName(*Alnum(Any));
}

FName Species(const FString& Any)
{
    if (Any.IsEmpty()) return NAME_None;
    if (const FName* Known = KnownSpecies().Find(Alnum(Any))) return *Known;
    return FName(*Kebab(Any));
}

FName Move(const FString& Any)
{
    if (Any.IsEmpty() || Alnum(Any) == TEXT("nomove")) return NAME_None;
    if (const FName* Known = KnownMoves().Find(Alnum(Any))) return *Known;
    return FName(*Kebab(Any));
}
}

// ------------------------------------------------------------------------------------------------ JSON helpers
namespace YJson
{
FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Default)
{
    if (!O.IsValid()) return Default;
    const TSharedPtr<FJsonValue> V = O->TryGetField(Key);
    if (!V.IsValid() || V->IsNull()) return Default;
    if (V->Type == EJson::String) return V->AsString();
    if (V->Type == EJson::Number) return FString::FromInt(int32(V->AsNumber()));
    if (V->Type == EJson::Boolean) return V->AsBool() ? TEXT("true") : TEXT("false");
    return Default;
}

int32 Int(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, int32 Default)
{
    if (!O.IsValid()) return Default;
    const TSharedPtr<FJsonValue> V = O->TryGetField(Key);
    if (!V.IsValid() || V->IsNull()) return Default;
    if (V->Type == EJson::Number) return int32(V->AsNumber());
    if (V->Type == EJson::String) { const FString S = V->AsString(); return S.IsNumeric() ? FCString::Atoi(*S) : Default; }
    if (V->Type == EJson::Boolean) return V->AsBool() ? 1 : 0;
    return Default;
}

bool Bool(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool Default)
{
    if (!O.IsValid()) return Default;
    const TSharedPtr<FJsonValue> V = O->TryGetField(Key);
    if (!V.IsValid() || V->IsNull()) return Default;
    if (V->Type == EJson::Boolean) return V->AsBool();
    if (V->Type == EJson::Number) return V->AsNumber() != 0;
    if (V->Type == EJson::String) return V->AsString().ToBool();
    return Default;
}

const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
{
    const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
    if (O.IsValid() && O->TryGetArrayField(Key, Out)) return Out;
    return nullptr;
}

TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
{
    const TSharedPtr<FJsonObject>* Out = nullptr;
    if (O.IsValid() && O->TryGetObjectField(Key, Out) && Out) return *Out;
    return nullptr;
}

TSharedPtr<FJsonObject> AsObj(const TSharedPtr<FJsonValue>& V)
{
    return V.IsValid() && V->Type == EJson::Object ? V->AsObject() : nullptr;
}

FString AsStr(const TSharedPtr<FJsonValue>& V)
{
    if (!V.IsValid()) return FString();
    if (V->Type == EJson::String) return V->AsString();
    if (V->Type == EJson::Number) return FString::FromInt(int32(V->AsNumber()));
    return FString();
}

int32 AsInt(const TSharedPtr<FJsonValue>& V, int32 Default)
{
    if (!V.IsValid()) return Default;
    if (V->Type == EJson::Number) return int32(V->AsNumber());
    if (V->Type == EJson::String && V->AsString().IsNumeric()) return FCString::Atoi(*V->AsString());
    return Default;
}

// [level, species] or {level, species}.
bool Slot(const TSharedPtr<FJsonValue>& V, FYSlot& Out)
{
    if (!V.IsValid()) return false;
    if (V->Type == EJson::Array)
    {
        const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
        if (A.Num() < 2) return false;
        // Either order: a number and a name.
        if (A[0]->Type == EJson::Number) { Out.Level = AsInt(A[0], 0); Out.Species = YKey::Species(AsStr(A[1])); }
        else { Out.Species = YKey::Species(AsStr(A[0])); Out.Level = AsInt(A[1], 0); }
        return Out.Level > 0 && !Out.Species.IsNone();
    }
    if (const TSharedPtr<FJsonObject> O = AsObj(V))
    {
        Out.Level = Int(O, TEXT("level"), 0);
        Out.Species = YKey::Species(Str(O, TEXT("species"), Str(O, TEXT("mon"), FString())));
        // Special moves (Brock's Onix: {"3": "BIND", "4": "BIDE"}).
        if (const TSharedPtr<FJsonObject> M = Obj(O, TEXT("moves")))
            for (const auto& Pair : M->Values)
                if (FString(*Pair.Key).IsNumeric()) Out.Moves.Add(FCString::Atoi(*Pair.Key), YKey::Move(AsStr(Pair.Value)));
        return Out.Level > 0 && !Out.Species.IsNone();
    }
    return false;
}
}

// ------------------------------------------------------------------------------------------------ map
TCHAR FYMap::Tile(int32 X, int32 Y) const
{
    if (!Contains(X, Y) || !Tiles.IsValidIndex(Y) || X >= Tiles[Y].Len()) return TEXT('#');
    return Tiles[Y][X];
}

TCHAR FYMap::Look(int32 X, int32 Y) const
{
    if (!Contains(X, Y) || !Looks.IsValidIndex(Y) || X >= Looks[Y].Len()) return TEXT(' ');
    return Looks[Y][X];
}

int32 FYMap::WarpAt(int32 X, int32 Y) const
{
    for (int32 I = 0; I < Warps.Num(); ++I) if (Warps[I].X == X && Warps[I].Y == Y) return I;
    return INDEX_NONE;
}

const FYNpc* FYMap::FindNpc(const FString& NpcId) const
{
    for (const FYNpc& N : Npcs) if (N.Id == NpcId) return &N;
    return nullptr;
}

bool FYellowDataSet::ParseMap(const TSharedPtr<FJsonObject>& J, FYMap& M, FString& Why)
{
    using namespace YJson;
    if (!J.IsValid()) { Why = TEXT("not a JSON object"); return false; }
    M.Id = FName(*Str(J, TEXT("id"), FString()));
    M.Name = Str(J, TEXT("name"), M.Id.ToString());
    M.Width = Int(J, TEXT("width"), 0);
    M.Height = Int(J, TEXT("height"), 0);
    M.bOutdoor = Bool(J, TEXT("outdoor"), false);
    M.bCave = Bool(J, TEXT("cave"), false);
    if (const TSharedPtr<FJsonObject> O = Obj(J, TEXT("origin"))) M.Origin = FIntPoint(Int(O, TEXT("x"), 0), Int(O, TEXT("y"), 0));
    M.Tileset = Str(J, TEXT("tileset"), FString());
    M.Music = Str(J, TEXT("music"), FString()).ToLower();
    M.Palette = Str(J, TEXT("palette"), FString());
    M.EncounterRate = FMath::Clamp(Int(J, TEXT("encounter_rate"), 0), 0, 255);
    if (const TSharedPtr<FJsonObject> C = Obj(J, TEXT("connections")))
    {
        for (const auto& Pair : C->Values)
        {
            const TSharedPtr<FJsonObject> E = AsObj(Pair.Value);
            if (!E.IsValid()) continue;
            FYConnection& Conn = M.Conn[int32(YDir::FromName(FString(*Pair.Key)))];
            Conn.bValid = true;
            Conn.Map = FName(*Str(E, TEXT("map"), FString()));
            Conn.Offset = Int(E, TEXT("offset"), 0);
            Conn.bOutOfScope = Bool(E, TEXT("out_of_scope"), false);
        }
    }
    auto Rows = [&](const TCHAR* Key, TArray<FString>& Out)
    {
        if (const TArray<TSharedPtr<FJsonValue>>* A = Arr(J, Key)) for (const TSharedPtr<FJsonValue>& V : *A) Out.Add(AsStr(V));
    };
    Rows(TEXT("tiles"), M.Tiles);
    Rows(TEXT("looks"), M.Looks);
    if (M.Id.IsNone()) { Why = TEXT("no id"); return false; }
    if (M.Width <= 0 || M.Height <= 0) { Why = TEXT("no size"); return false; }
    if (M.Tiles.Num() != M.Height) { Why = FString::Printf(TEXT("%d tile rows for height %d"), M.Tiles.Num(), M.Height); return false; }
    for (int32 Y = 0; Y < M.Tiles.Num(); ++Y)
        if (M.Tiles[Y].Len() != M.Width) { Why = FString::Printf(TEXT("tile row %d is %d wide, not %d"), Y, M.Tiles[Y].Len(), M.Width); return false; }
    if (const TArray<TSharedPtr<FJsonValue>>* A = Arr(J, TEXT("warps")))
        for (const TSharedPtr<FJsonValue>& V : *A)
            if (const TSharedPtr<FJsonObject> O = AsObj(V))
            {
                FYWarp W;
                W.X = Int(O, TEXT("x"), 0); W.Y = Int(O, TEXT("y"), 0);
                W.ToMap = FName(*Str(O, TEXT("to_map"), FString()));
                W.ToWarp = Int(O, TEXT("to_warp"), 0);
                W.bOutOfScope = Bool(O, TEXT("out_of_scope"), false);
                W.bLastMap = Bool(O, TEXT("last_map"), false);
                W.bDoor = Bool(O, TEXT("door"), false);
                M.Warps.Add(W);
            }
    if (const TArray<TSharedPtr<FJsonValue>>* A = Arr(J, TEXT("signs")))
        for (const TSharedPtr<FJsonValue>& V : *A)
            if (const TSharedPtr<FJsonObject> O = AsObj(V))
                M.Signs.Add(FYSign{Int(O, TEXT("x"), 0), Int(O, TEXT("y"), 0), Str(O, TEXT("text"), FString())});
    if (const TArray<TSharedPtr<FJsonValue>>* A = Arr(J, TEXT("npcs")))
        for (const TSharedPtr<FJsonValue>& V : *A)
            if (const TSharedPtr<FJsonObject> O = AsObj(V))
            {
                FYNpc N;
                N.Index = M.Npcs.Num();
                N.Id = Str(O, TEXT("id"), FString::FromInt(N.Index + 1));
                N.X = Int(O, TEXT("x"), 0); N.Y = Int(O, TEXT("y"), 0);
                N.Sprite = FName(*Str(O, TEXT("sprite"), FString()));
                N.bWalk = Str(O, TEXT("movement"), TEXT("stay")).ToLower().Contains(TEXT("walk"));
                const FString Face = Str(O, TEXT("facing"), TEXT("down")).ToLower();
                // Walkers keep to their range (up_down, left_right, any_dir); people with no facing face down.
                N.WalkAxis = Face.Contains(TEXT("up_down")) ? 1 : Face.Contains(TEXT("left_right")) ? 2 : 0;
                N.Facing = N.WalkAxis == 2 ? EYDir::Left : (Face == TEXT("none") || Face.Contains(TEXT("any")) || N.WalkAxis == 1) ? EYDir::Down : YDir::FromName(Face);
                N.Text = Str(O, TEXT("text"), FString());
                if (const TArray<TSharedPtr<FJsonValue>>* Texts = Arr(O, TEXT("texts"))) for (const TSharedPtr<FJsonValue>& T : *Texts) N.Texts.Add(AsStr(T));
                N.bHiddenAtStart = Bool(O, TEXT("hidden_at_start"), false);
                N.Toggle = Str(O, TEXT("toggle"), FString());
                N.Script = Str(O, TEXT("script"), FString());
                if (const TSharedPtr<FJsonObject> T = Obj(O, TEXT("trainer")))
                {
                    N.Trainer.bValid = true;
                    N.Trainer.Class = FName(*Str(T, TEXT("class"), FString()));
                    N.Trainer.Party = Int(T, TEXT("party"), 0);
                    N.Trainer.Sight = Int(T, TEXT("sight"), 0);
                    N.Trainer.BattleText = Str(T, TEXT("battle_text"), FString());
                    N.Trainer.EndText = Str(T, TEXT("end_text"), FString());
                    N.Trainer.AfterText = Str(T, TEXT("after_text"), FString());
                    N.Trainer.Event = FName(*Str(T, TEXT("event"), FString()));
                }
                const FString Item = Str(O, TEXT("item"), FString());
                if (!Item.IsEmpty()) N.Item = YKey::Item(Item);
                N.Raw = O;
                M.Npcs.Add(N);
            }
    auto Balls = [&](const TCHAR* Key, TArray<FYItemBall>& Out)
    {
        if (const TArray<TSharedPtr<FJsonValue>>* A = Arr(J, Key))
            for (const TSharedPtr<FJsonValue>& V : *A)
                if (const TSharedPtr<FJsonObject> O = AsObj(V))
                {
                    FYItemBall B;
                    B.X = Int(O, TEXT("x"), 0); B.Y = Int(O, TEXT("y"), 0);
                    B.RawItem = Str(O, TEXT("item"), FString());
                    B.Item = YKey::Item(B.RawItem);
                    B.Id = Str(O, TEXT("id"), FString());
                    Out.Add(B);
                }
    };
    Balls(TEXT("items"), M.Items);
    Balls(TEXT("hidden"), M.Hidden);
    if (const TArray<TSharedPtr<FJsonValue>>* A = Arr(J, TEXT("blocked_edges")))
        for (const TSharedPtr<FJsonValue>& V : *A)
            if (const TSharedPtr<FJsonObject> O = AsObj(V))
                M.BlockedEdges.Add(FYEdge{Int(O, TEXT("x"), 0), Int(O, TEXT("y"), 0), YDir::FromName(Str(O, TEXT("dir"), TEXT("down")))});
    if (const TArray<TSharedPtr<FJsonValue>>* A = Arr(J, TEXT("hidden_events")))
        for (const TSharedPtr<FJsonValue>& V : *A)
            if (const TSharedPtr<FJsonObject> O = AsObj(V))
                M.HiddenEvents.Add(FYHiddenEvent{Int(O, TEXT("x"), 0), Int(O, TEXT("y"), 0), Str(O, TEXT("kind"), FString()), Str(O, TEXT("arg"), FString())});
    return true;
}

// ------------------------------------------------------------------------------------------------ loading
void FYellowDataSet::Missing(const FString& Relative, const FString& Why)
{
    const FString Path = Root.IsEmpty() ? FString::Printf(TEXT("%s:%s"), *SourceLabel, *Relative) : FPaths::Combine(Root, Relative);
    const FString Line = Why.IsEmpty() ? FString::Printf(TEXT("YellowData missing: %s"), *Path)
                                       : FString::Printf(TEXT("YellowData missing: %s (%s)"), *Path, *Why);
    UE_LOG(LogYellow, Error, TEXT("%s"), *Line);
    Errors.Add(Line);
}

TSharedPtr<FJsonValue> FYellowDataSet::ReadValue(const FString& Relative, bool bRequired)
{
    FString Contents;
    if (!Read || !Read(Relative, Contents)) { if (bRequired) Missing(Relative); return nullptr; }
    TSharedPtr<FJsonValue> Value;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
    if (!FJsonSerializer::Deserialize(Reader, Value) || !Value.IsValid()) { Missing(Relative, TEXT("not valid JSON")); return nullptr; }
    return Value;
}

TSharedPtr<FJsonObject> FYellowDataSet::ReadObject(const FString& Relative, bool bRequired)
{
    const TSharedPtr<FJsonValue> V = ReadValue(Relative, bRequired);
    if (!V.IsValid()) return nullptr;
    if (V->Type != EJson::Object) { Missing(Relative, TEXT("not a JSON object")); return nullptr; }
    return V->AsObject();
}

bool FYellowDataSet::LoadFromDisk(const FString& InRoot)
{
    Root = InRoot;
    return Load([InRoot](const FString& Relative, FString& Out)
    {
        const FString Path = FPaths::Combine(InRoot, Relative);
        return FPaths::FileExists(Path) && FFileHelper::LoadFileToString(Out, *Path);
    }, InRoot);
}

bool FYellowDataSet::LoadFromFiles(const TMap<FString, FString>& Files)
{
    Root.Reset();
    TMap<FString, FString> Copy = Files;
    return Load([Copy](const FString& Relative, FString& Out)
    {
        const FString* Found = Copy.Find(Relative);
        if (!Found) return false;
        Out = *Found;
        return true;
    }, TEXT("fixture"));
}

bool FYellowDataSet::Load(FReader Reader, const FString& Label)
{
    Read = MoveTemp(Reader);
    SourceLabel = Label;
    Errors.Reset();
    MapOrder.Reset(); Maps.Reset(); Text.Reset(); Items.Reset(); ItemOrder.Reset(); Marts.Reset(); Wild.Reset();
    Trainers.Reset(); Trades.Reset(); Events.Reset(); Scenery.Reset(); Kanto.Reset(); TownMapRows.Reset(); Landmarks.Reset();
    using namespace YJson;
    const TSharedPtr<FJsonObject> Manifest = ReadObject(TEXT("manifest.json"), true);
    if (!Manifest.IsValid()) return false;
    if (const TSharedPtr<FJsonObject> Src = Obj(Manifest, TEXT("source"))) SourceCommit = Str(Src, TEXT("commit"), FString());
    if (const TArray<TSharedPtr<FJsonValue>>* List = Arr(Manifest, TEXT("maps")))
        for (const TSharedPtr<FJsonValue>& V : *List)
        {
            const FString Id = AsStr(V);
            if (Id.IsEmpty()) continue;
            const FString Rel = FString::Printf(TEXT("maps/%s.json"), *Id);
            FYMap Map;
            FString Why;
            if (!ParseMap(ReadObject(Rel, true), Map, Why)) { if (!Why.IsEmpty() && Why != TEXT("not a JSON object")) Missing(Rel, Why); continue; }
            MapOrder.Add(Map.Id);
            Maps.Add(Map.Id, MoveTemp(Map));
        }
    if (MapOrder.Num() == 0) Missing(TEXT("maps/"), TEXT("no map loaded"));
    ParseTables();
    // pokeyellow numbers warps and a class's parties from 1; an extractor may have made them 0-based. A 0 anywhere
    // means 0-based unless some index also equals its list's length.
    {
        bool bAnyZero = false, bAnyAtCount = false;
        for (const auto& Pair : Maps)
            for (const FYWarp& W : Pair.Value.Warps)
            {
                if (W.ToWarp == 0) bAnyZero = true;
                if (const FYMap* Dest = Maps.Find(W.ToMap)) if (W.ToWarp == Dest->Warps.Num()) bAnyAtCount = true;
            }
        bWarpsOneBased = !bAnyZero || bAnyAtCount;
        bAnyZero = bAnyAtCount = false;
        for (const auto& Pair : Maps)
            for (const FYNpc& N : Pair.Value.Npcs)
            {
                if (!N.Trainer.bValid) continue;
                if (N.Trainer.Party == 0) bAnyZero = true;
                if (const FYTrainerClass* C = TrainerClass(N.Trainer.Class)) if (N.Trainer.Party == C->Parties.Num()) bAnyAtCount = true;
            }
        bPartiesOneBased = !bAnyZero || bAnyAtCount;
    }
    UE_LOG(LogYellow, Display, TEXT("YellowData %s: %d maps, %d texts, %d items, %d wild tables, %d trainer classes, %d events, %d errors (commit %s)"),
        *Label, MapOrder.Num(), Text.Num(), Items.Num(), Wild.Num(), Trainers.Num(), Events.Num(), Errors.Num(), *SourceCommit);
    return Errors.Num() == 0;
}

void FYellowDataSet::ParseTables()
{
    using namespace YJson;
    // text.json: {id: string}
    if (const TSharedPtr<FJsonObject> T = ReadObject(TEXT("text.json"), true))
        for (const auto& Pair : T->Values) Text.Add(FString(*Pair.Key), AsStr(Pair.Value));

    // items.json: a list of items, or {id: item}.
    if (const TSharedPtr<FJsonValue> V = ReadValue(TEXT("items.json"), true))
    {
        auto AddItem = [this](const FString& KeyHint, const TSharedPtr<FJsonObject>& O)
        {
            if (!O.IsValid()) return;
            FYItem I;
            // The dict key is the constant (POKE_BALL, TM_BIDE); a list entry names itself (a numeric id is no name).
            const FString IdField = Str(O, TEXT("id"), FString());
            I.RawId = !KeyHint.IsEmpty() ? KeyHint : (!IdField.IsEmpty() && !IdField.IsNumeric() ? IdField : Str(O, TEXT("name"), FString()));
            I.Id = YKey::Item(I.RawId);
            if (I.Id.IsNone()) return;
            I.Name = Str(O, TEXT("name"), I.RawId);
            I.Price = Int(O, TEXT("price"), 0);
            I.bKey = Bool(O, TEXT("key"), false);
            I.bFieldUse = Bool(O, TEXT("field_use"), false);
            I.bBattleUse = Bool(O, TEXT("battle_use"), false);
            I.Tm = Int(O, TEXT("tm"), 0);
            I.Hm = Int(O, TEXT("hm"), 0);
            const FString Id = I.Id.ToString();
            if (I.Tm == 0 && Id.StartsWith(TEXT("tm")) && Id.Mid(2).IsNumeric()) I.Tm = FCString::Atoi(*Id.Mid(2));
            if (I.Hm == 0 && Id.StartsWith(TEXT("hm")) && Id.Mid(2).IsNumeric()) I.Hm = FCString::Atoi(*Id.Mid(2));
            I.Move = YKey::Move(Str(O, TEXT("move"), FString()));
            if (!Items.Contains(I.Id)) ItemOrder.Add(I.Id);
            Items.Add(I.Id, I);
        };
        if (V->Type == EJson::Array) for (const TSharedPtr<FJsonValue>& E : V->AsArray()) AddItem(FString(), AsObj(E));
        else if (V->Type == EJson::Object) for (const auto& Pair : V->AsObject()->Values) AddItem(FString(*Pair.Key), AsObj(Pair.Value));
    }

    // marts.json: {MAP_ID: [item ids]}
    if (const TSharedPtr<FJsonObject> M = ReadObject(TEXT("marts.json"), true))
        for (const auto& Pair : M->Values)
        {
            TArray<FName>& Stock = Marts.Add(FName(*Pair.Key));
            if (Pair.Value->Type == EJson::Array)
                for (const TSharedPtr<FJsonValue>& E : Pair.Value->AsArray())
                {
                    const FString S = E->Type == EJson::Object ? Str(E->AsObject(), TEXT("item"), Str(E->AsObject(), TEXT("id"), FString())) : AsStr(E);
                    if (!S.IsEmpty()) Stock.Add(YKey::Item(S));
                }
        }

    // wild.json: {MAP_ID: {grass_rate, water_rate, grass: [[level, species] x10]}}
    if (const TSharedPtr<FJsonObject> W = ReadObject(TEXT("wild.json"), true))
        for (const auto& Pair : W->Values)
        {
            const TSharedPtr<FJsonObject> O = AsObj(Pair.Value);
            if (!O.IsValid()) continue;
            FYWild Table;
            Table.GrassRate = Int(O, TEXT("grass_rate"), 0);
            Table.WaterRate = Int(O, TEXT("water_rate"), 0);
            if (const TArray<TSharedPtr<FJsonValue>>* A = Arr(O, TEXT("grass")))
                for (const TSharedPtr<FJsonValue>& E : *A) { FYSlot S; if (Slot(E, S)) Table.Grass.Add(S); }
            Wild.Add(FName(*Pair.Key), Table);
        }

    // trainers.json: {class: {name, base_money, parties: [[ [level, species] ...] ]}}
    if (const TSharedPtr<FJsonObject> T = ReadObject(TEXT("trainers.json"), true))
        for (const auto& Pair : T->Values)
        {
            const TSharedPtr<FJsonObject> O = AsObj(Pair.Value);
            if (!O.IsValid()) continue;
            FYTrainerClass C;
            C.Class = FName(*Pair.Key);
            C.Name = Str(O, TEXT("name"), FString(*Pair.Key));
            C.BaseMoney = Int(O, TEXT("base_money"), 0);
            C.PartyBase = Int(O, TEXT("party_index_base"), 1);
            if (const TArray<TSharedPtr<FJsonValue>>* Parties = Arr(O, TEXT("parties")))
                for (const TSharedPtr<FJsonValue>& P : *Parties)
                {
                    TArray<FYSlot>& Party = C.Parties.AddDefaulted_GetRef();
                    const TArray<TSharedPtr<FJsonValue>>* Mons = nullptr;
                    if (P->Type == EJson::Array) Mons = &P->AsArray();
                    else if (const TSharedPtr<FJsonObject> PO = AsObj(P)) Mons = Arr(PO, TEXT("mons")) ? Arr(PO, TEXT("mons")) : Arr(PO, TEXT("party"));
                    if (Mons) for (const TSharedPtr<FJsonValue>& E : *Mons) { FYSlot S; if (Slot(E, S)) Party.Add(S); }
                }
            Trainers.Add(C.Class, C);
        }

    // trades.json and events.json are kept as JSON: the scripts read the fields they need.
    if (const TSharedPtr<FJsonValue> V = ReadValue(TEXT("trades.json"), true))
    {
        if (V->Type == EJson::Array) Trades = V->AsArray();
        else if (V->Type == EJson::Object)
        {
            const TArray<TSharedPtr<FJsonValue>>* A = Arr(V->AsObject(), TEXT("trades"));
            if (A) Trades = *A;
            else for (const auto& Pair : V->AsObject()->Values) Trades.Add(Pair.Value);
        }
    }
    if (const TSharedPtr<FJsonValue> V = ReadValue(TEXT("events.json"), true))
    {
        auto AddEvent = [this](const FString& Key, const TSharedPtr<FJsonValue>& E)
        {
            if (TSharedPtr<FJsonObject> O = AsObj(E))
            {
                if (!Key.IsEmpty() && !O->HasField(TEXT("id"))) O->SetStringField(TEXT("id"), Key);
                Events.Add(O);
            }
        };
        if (V->Type == EJson::Array) for (const TSharedPtr<FJsonValue>& E : V->AsArray()) AddEvent(FString(), E);
        else if (V->Type == EJson::Object)
        {
            if (const TArray<TSharedPtr<FJsonValue>>* A = Arr(V->AsObject(), TEXT("events"))) for (const TSharedPtr<FJsonValue>& E : *A) AddEvent(FString(), E);
            else for (const auto& Pair : V->AsObject()->Values) AddEvent(FString(*Pair.Key), Pair.Value);
        }
    }

    // pikachu.json: happiness start, deltas and moods.
    if (const TSharedPtr<FJsonObject> P = ReadObject(TEXT("pikachu.json"), true))
        YellowTables::ParsePikachu(P, Pikachu);

    SpeciesJson = ReadObject(TEXT("species.json"), true);
    if (!SpeciesJson.IsValid())
        if (const TSharedPtr<FJsonValue> V = ReadValue(TEXT("species.json"), false))
            if (V->Type == EJson::Array)
            {
                // A list of species: key them by name.
                SpeciesJson = MakeShared<FJsonObject>();
                for (const TSharedPtr<FJsonValue>& E : V->AsArray())
                    if (const TSharedPtr<FJsonObject> O = AsObj(E)) SpeciesJson->SetObjectField(Str(O, TEXT("name"), Str(O, TEXT("id"), FString())), O);
            }
    MovesJson = ReadObject(TEXT("moves.json"), false);
    if (!MovesJson.IsValid())
    {
        if (const TSharedPtr<FJsonValue> V = ReadValue(TEXT("moves.json"), true))
            if (V->Type == EJson::Array)
            {
                MovesJson = MakeShared<FJsonObject>();
                for (const TSharedPtr<FJsonValue>& E : V->AsArray())
                    if (const TSharedPtr<FJsonObject> O = AsObj(E)) MovesJson->SetObjectField(Str(O, TEXT("id"), Str(O, TEXT("name"), FString())), O);
            }
    }
    // scenery.json (optional): what lies past the maps built.
    if (const TSharedPtr<FJsonObject> Sc = ReadObject(TEXT("scenery.json"), false))
    {
        if (const TSharedPtr<FJsonObject> B = Obj(Sc, TEXT("borders")))
            for (const auto& Pair : B->Values)
                if (FYMap* M = Maps.Find(FName(*Pair.Key)))
                    if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Array)
                        for (const TSharedPtr<FJsonValue>& Row : Pair.Value->AsArray()) M->BorderLooks.Add(AsStr(Row));
        if (const TArray<TSharedPtr<FJsonValue>>* List = Arr(Sc, TEXT("maps")))
            for (const TSharedPtr<FJsonValue>& V : *List)
            {
                const TSharedPtr<FJsonObject> O = AsObj(V);
                if (!O.IsValid()) continue;
                FYMap M;
                M.Id = FName(*Str(O, TEXT("id"), FString()));
                M.Name = M.Id.ToString();
                M.Width = Int(O, TEXT("width"), 0);
                M.Height = Int(O, TEXT("height"), 0);
                M.bOutdoor = true;
                M.Tileset = Str(O, TEXT("tileset"), FString());
                if (const TSharedPtr<FJsonObject> Or = Obj(O, TEXT("origin"))) M.Origin = FIntPoint(Int(Or, TEXT("x"), 0), Int(Or, TEXT("y"), 0));
                if (const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("looks"))) for (const TSharedPtr<FJsonValue>& R : *Rows) M.Looks.Add(AsStr(R));
                if (M.Width <= 0 || M.Height <= 0 || M.Looks.Num() != M.Height) continue;
                for (int32 Y = 0; Y < M.Height; ++Y) M.Tiles.Add(FString::ChrN(M.Width, TEXT('#')));
                if (const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("border"))) for (const TSharedPtr<FJsonValue>& R : *Rows) M.BorderLooks.Add(AsStr(R));
                Scenery.Add(MoveTemp(M));
            }
    }
    // kanto.json (optional, m4s): the rest of Kanto for the far view.
    if (const TSharedPtr<FJsonObject> K = ReadObject(TEXT("kanto.json"), false))
    {
        if (const TArray<TSharedPtr<FJsonValue>>* List = Arr(K, TEXT("maps")))
            for (const TSharedPtr<FJsonValue>& V : *List)
            {
                const TSharedPtr<FJsonObject> O = AsObj(V);
                if (!O.IsValid()) continue;
                FYMap M;
                M.Id = FName(*Str(O, TEXT("id"), FString()));
                M.Name = M.Id.ToString();
                M.Width = Int(O, TEXT("width"), 0);
                M.Height = Int(O, TEXT("height"), 0);
                M.bOutdoor = true;
                M.Tileset = Str(O, TEXT("tileset"), FString());
                if (const TSharedPtr<FJsonObject> Or = Obj(O, TEXT("origin"))) M.Origin = FIntPoint(Int(Or, TEXT("x"), 0), Int(Or, TEXT("y"), 0));
                if (const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("looks"))) for (const TSharedPtr<FJsonValue>& R : *Rows) M.Looks.Add(AsStr(R));
                if (M.Width <= 0 || M.Height <= 0 || M.Looks.Num() != M.Height) continue;
                for (int32 Y = 0; Y < M.Height; ++Y) M.Tiles.Add(FString::ChrN(M.Width, TEXT('#')));
                if (const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("border"))) for (const TSharedPtr<FJsonValue>& R : *Rows) M.BorderLooks.Add(AsStr(R));
                Kanto.Add(MoveTemp(M));
            }
        if (const TSharedPtr<FJsonObject> T = Obj(K, TEXT("townmap")))
        {
            if (const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(T, TEXT("rows"))) for (const TSharedPtr<FJsonValue>& R : *Rows) TownMapRows.Add(AsStr(R));
            double Ax = 0, Ay = 0, Bx = 22, By = 22;
            if (const TSharedPtr<FJsonObject> C = Obj(T, TEXT("square0_centre"))) { C->TryGetNumberField(TEXT("x"), Ax); C->TryGetNumberField(TEXT("y"), Ay); }
            if (const TSharedPtr<FJsonObject> S = Obj(T, TEXT("square_tiles"))) { S->TryGetNumberField(TEXT("x"), Bx); S->TryGetNumberField(TEXT("y"), By); }
            TownMapSquare0 = FVector2D(Ax, Ay);
            TownMapSquareTiles = FVector2D(Bx, By);
        }
        if (const TSharedPtr<FJsonObject> L = Obj(K, TEXT("landmarks")))
            for (const auto& Pair : L->Values)
                if (const TSharedPtr<FJsonObject> O = AsObj(Pair.Value)) Landmarks.Add(FName(*Pair.Key), FIntPoint(Int(O, TEXT("x"), 0), Int(O, TEXT("y"), 0)));
    }
    TypesJson = ReadObject(TEXT("types.json"), true);
    ExpJson = ReadObject(TEXT("exp.json"), true);
}

// ------------------------------------------------------------------------------------------------ queries
FString FYellowDataSet::TextFor(const FString& Id) const
{
    if (const FString* Found = Text.Find(Id)) return *Found;
    if (!Id.IsEmpty()) UE_LOG(LogYellow, Warning, TEXT("YellowData text missing: %s"), *Id);
    return FString();
}

int32 FYellowDataSet::WarpIndex(const FYMap& To, int32 ToWarp) const
{
    const int32 Index = bWarpsOneBased ? ToWarp - 1 : ToWarp;
    return To.Warps.IsValidIndex(Index) ? Index : (To.Warps.IsValidIndex(ToWarp) ? ToWarp : INDEX_NONE);
}

const FYTrainerClass* FYellowDataSet::TrainerClass(FName Class) const
{
    if (const FYTrainerClass* Found = Trainers.Find(Class)) return Found;
    const FString Want = YKey::Alnum(Class.ToString());
    for (const auto& Pair : Trainers)
    {
        const FString Key = YKey::Alnum(Pair.Key.ToString());
        if (Key == Want || Key == TEXT("opp") + Want || TEXT("opp") + Key == Want) return &Pair.Value;
    }
    return nullptr;
}

TArray<FYSlot> FYellowDataSet::TrainerParty(const FYTrainerRef& Ref) const
{
    const FYTrainerClass* C = Ref.bValid ? TrainerClass(Ref.Class) : nullptr;
    if (!C || C->Parties.Num() == 0) return {};
    const int32 Index = Ref.Party - C->PartyBase;
    return C->Parties.IsValidIndex(Index) ? C->Parties[Index] : TArray<FYSlot>();
}

void FYellowDataSet::OutdoorMaps(TArray<FName>& Out) const
{
    for (const FName& Id : MapOrder) if (const FYMap* M = Maps.Find(Id)) if (M->bOutdoor) Out.Add(Id);
}

// ------------------------------------------------------------------------------------------------ Pikachu table
int32 FYPikachuTable::Delta(const FString& Event, int32 Happiness, int32 Default) const
{
    const FDelta* D = Deltas.Find(Event);
    if (!D) return Default;
    return Happiness < 100 ? D->Low : Happiness < 200 ? D->Mid : D->High;
}

int32 FYPikachuTable::MoodTarget(const FString& Event) const
{
    const FDelta* D = Deltas.Find(Event);
    return D ? D->MoodTarget : -1;
}

FString FYPikachuTable::FaceFor(int32 Happiness, int32 Mood) const
{
    for (const FFaceRow& Row : FaceTable)
    {
        if (Row.HappinessAtMost < Happiness) continue;
        for (const TPair<int32, FString>& M : Row.ByMood) if (M.Key >= Mood) return M.Value;
        return Row.ByMood.Num() ? Row.ByMood.Last().Value : FString();
    }
    return FaceTable.Num() && FaceTable.Last().ByMood.Num() ? FaceTable.Last().ByMood.Last().Value : FString();
}

// ------------------------------------------------------------------------------------------------ subsystem
UYellowData* UYellowData::Get(const UObject* Ctx)
{
    const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    return GI ? GI->GetSubsystem<UYellowData>() : nullptr;
}

const FYellowDataSet* UYellowData::Data(const UObject* Ctx)
{
    UYellowData* Self = Get(Ctx);
    return Self && Self->Set.IsValid() ? Self->Set.Get() : nullptr;
}

FString UYellowData::DefaultRoot()
{
    FString Dir;
    if (FParse::Value(FCommandLine::Get(), TEXT("YellowDataDir="), Dir) && !Dir.IsEmpty()) return Dir;
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("YellowData")));
}

FString UYellowData::MusicRoot()
{
    FString Dir;
    if (FParse::Value(FCommandLine::Get(), TEXT("YellowMusicDir="), Dir) && !Dir.IsEmpty()) return Dir;
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("YellowMusic")));
}

void UYellowData::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Reload();
}

void UYellowData::Reload()
{
    Set = MakeShared<FYellowDataSet>();
    Set->LoadFromDisk(DefaultRoot());
    // The tables join this game's species, moves and experience (new species, Yellow's learnsets, TMs, dex text).
    YellowTables::Import(*Set);
}
