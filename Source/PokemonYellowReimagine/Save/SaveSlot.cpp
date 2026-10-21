#include "Save/SaveSlot.h"

#include "GameTest.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSaveSlot, Log, All);

namespace
{
const TCHAR* Magic = TEXT("PYR_SAVE");

bool ReadBounded(const FString& Path, FString& Text)
{
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path));
    if (!Reader || Reader->TotalSize() <= 0 || Reader->TotalSize() > FSaveSlot::MaxBytes) return false;
    TArray<uint8> Bytes;
    Bytes.SetNumUninitialized(int32(Reader->TotalSize()));
    Reader->Serialize(Bytes.GetData(), Bytes.Num());
    if (Reader->IsError() || !Reader->Close()) return false;
    for (uint8 C : Bytes) if (C == 0) return false;  // Text only.
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
    Text = FString(Converted.Length(), Converted.Get());
    return true;
}

bool AtomicWrite(const FString& Path, const FString& Text)
{
    const FString Temp = Path + TEXT(".tmp");
    FTCHARToUTF8 Bytes(*Text);
#if PLATFORM_WINDOWS
    HANDLE File = CreateFileW(*Temp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File == INVALID_HANDLE_VALUE) return false;
    DWORD Written = 0;
    bool Ok = WriteFile(File, Bytes.Get(), Bytes.Length(), &Written, nullptr) && Written == DWORD(Bytes.Length());
    if (Ok) Ok = FlushFileBuffers(File) != 0;
    if (!CloseHandle(File)) Ok = false;
    // Never delete the destination first: same-directory rename with replace.
    if (Ok) Ok = MoveFileExW(*Temp, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!Ok) DeleteFileW(*Temp);
    return Ok;
#else
    TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*Temp));
    if (!Writer) return false;
    Writer->Serialize(const_cast<ANSICHAR*>(Bytes.Get()), Bytes.Length());
    Writer->Flush();
    const bool Ok = !Writer->IsError() && Writer->Close() && IFileManager::Get().Move(*Path, *Temp, true, true);
    if (!Ok) IFileManager::Get().Delete(*Temp, false, true, true);
    return Ok;
#endif
}

void KeepRejected(const FString& Path)
{
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size >= 0 && Size <= FSaveSlot::MaxBytes)
        IFileManager::Get().Copy(*(Path + TEXT(".invalid")), *Path, true, true);
}
}

bool FSaveSlot::IsValidName(const FString& Slot)
{
    if (Slot.IsEmpty() || Slot.Len() > 64) return false;
    for (TCHAR C : Slot)
        if (!FChar::IsAlnum(C) && C != TEXT('-') && C != TEXT('_')) return false;
    return true;
}

FString FSaveSlot::Directory()
{
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames"));
    const int32 TestSlot = GameTest::Slot();
    return TestSlot > 0 ? Root / FString::Printf(TEXT("slot-%d"), TestSlot) : Root;
}

FString FSaveSlot::PathFor(const FString& Slot) { return Directory() / (Slot + TEXT(".json")); }

bool FSaveSlot::Exists(const FString& Slot) { return IsValidName(Slot) && IFileManager::Get().FileExists(*PathFor(Slot)); }

bool FSaveSlot::Delete(const FString& Slot)
{
    return IsValidName(Slot) && IFileManager::Get().Delete(*PathFor(Slot), false, true, true);
}

const TCHAR* FSaveSlot::ResultName(ESaveSlotResult Result)
{
    switch (Result)
    {
    case ESaveSlotResult::Loaded: return TEXT("Loaded");
    case ESaveSlotResult::Missing: return TEXT("Missing");
    case ESaveSlotResult::Outdated: return TEXT("Outdated");
    default: return TEXT("Invalid");
    }
}

bool FSaveSlot::SaveStruct(const FString& Slot, const UScriptStruct* Type, const void* Data, int32 Version)
{
    if (!IsValidName(Slot) || !Type || !Data) return false;
    TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    if (!FJsonObjectConverter::UStructToJsonObject(Type, Data, Payload, 0, 0)) return false;
    // Fixed header order: magic, version, type, data.
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("magic"), Magic);
    Root->SetNumberField(TEXT("version"), Version);
    Root->SetStringField(TEXT("type"), Type->GetPathName());
    Root->SetObjectField(TEXT("data"), Payload);
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    if (!FJsonSerializer::Serialize(Root, Writer)) return false;
    if (FTCHARToUTF8(*Text).Length() > MaxBytes) return false;
    IFileManager::Get().MakeDirectory(*Directory(), true);
    const bool Ok = AtomicWrite(PathFor(Slot), Text);
    UE_LOG(LogSaveSlot, Display, TEXT("SAVE_SLOT_WRITE:%s:%s"), *Slot, Ok ? TEXT("PASS") : TEXT("FAIL"));
    return Ok;
}

ESaveSlotResult FSaveSlot::LoadStruct(const FString& Slot, const UScriptStruct* Type, void* Out, int32 Version, int32* FoundVersion)
{
    if (!IsValidName(Slot) || !Type || !Out) return ESaveSlotResult::Invalid;
    const FString Path = PathFor(Slot);
    if (!IFileManager::Get().FileExists(*Path)) return ESaveSlotResult::Missing;
    FString Text;
    TSharedPtr<FJsonObject> Root;
    const bool Parsed = ReadBounded(Path, Text) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid();
    FString FoundMagic, FoundType;
    int32 Found = -1;
    const TSharedPtr<FJsonObject>* Payload = nullptr;
    if (!Parsed || !Root->TryGetStringField(TEXT("magic"), FoundMagic) || FoundMagic != Magic
        || !Root->TryGetNumberField(TEXT("version"), Found) || !Root->TryGetStringField(TEXT("type"), FoundType)
        || FoundType != Type->GetPathName() || !Root->TryGetObjectField(TEXT("data"), Payload) || !Payload)
    {
        KeepRejected(Path);
        UE_LOG(LogSaveSlot, Warning, TEXT("SAVE_SLOT_INVALID:%s"), *Slot);
        return ESaveSlotResult::Invalid;
    }
    if (FoundVersion) *FoundVersion = Found;
    if (Found != Version) return ESaveSlotResult::Outdated;
    // Decode into a scratch copy so a half-applied load never reaches the caller.
    void* Scratch = FMemory::Malloc(FMath::Max(1, Type->GetStructureSize()), Type->GetMinAlignment());
    Type->InitializeStruct(Scratch);
    Type->CopyScriptStruct(Scratch, Out);
    const bool Ok = FJsonObjectConverter::JsonObjectToUStruct((*Payload).ToSharedRef(), Type, Scratch, 0, 0);
    if (Ok) Type->CopyScriptStruct(Out, Scratch);
    Type->DestroyStruct(Scratch);
    FMemory::Free(Scratch);
    if (!Ok)
    {
        KeepRejected(Path);
        UE_LOG(LogSaveSlot, Warning, TEXT("SAVE_SLOT_INVALID:%s (fields)"), *Slot);
        return ESaveSlotResult::Invalid;
    }
    return ESaveSlotResult::Loaded;
}
