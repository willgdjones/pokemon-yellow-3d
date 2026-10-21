#include "Yellow/YellowImages.h"

#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "Yellow/YellowData.h"

UYellowImages* UYellowImages::Get(const UObject* Ctx)
{
    const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    return GI ? GI->GetSubsystem<UYellowImages>() : nullptr;
}

UTexture2D* UYellowImages::LoadAbsolute(const FString& Path, UObject* Outer)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return nullptr;
    IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    const EImageFormat Format = Module.DetectImageFormat(Bytes.GetData(), Bytes.Num());
    TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(Format == EImageFormat::Invalid ? EImageFormat::PNG : Format);
    if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num())) return nullptr;
    TArray<uint8> Raw;
    if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw)) return nullptr;
    const int32 W = Wrapper->GetWidth(), H = Wrapper->GetHeight();
    UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, NAME_None, Raw);
    if (!Tex) return nullptr;
    // Pixel art: nearest filtering, no mips, no sRGB surprises.
    Tex->Filter = TF_Nearest;
    Tex->LODGroup = TEXTUREGROUP_Pixels2D;
    Tex->NeverStream = true;
    Tex->SRGB = true;
    Tex->AddressX = TA_Clamp;
    Tex->AddressY = TA_Clamp;
    Tex->UpdateResource();
    return Tex;
}

UTexture2D* UYellowImages::Load(const FString& Relative)
{
    if (TObjectPtr<UTexture2D>* Found = Cache.Find(Relative)) return *Found;
    if (Missing.Contains(Relative)) return nullptr;
    const FString Path = FPaths::Combine(UYellowData::DefaultRoot(), Relative);
    UTexture2D* Tex = LoadAbsolute(Path, this);
    if (!Tex)
    {
        Missing.Add(Relative);
        UE_LOG(LogYellow, Error, TEXT("YellowData missing: %s"), *Path);
        return nullptr;
    }
    Cache.Add(Relative, Tex);
    return Tex;
}
