#include "World/PYRQuery.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace PYRWorld
{
void FindAllTagged(const UWorld* World, FName Tag, TArray<AActor*>& Out)
{
    Out.Reset();
    if (!World) return;
    for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
        if (IsValid(*It) && It->ActorHasTag(Tag)) Out.Add(*It);
}

bool Ground(const UWorld* World, const FVector& XY, float RefZ, FHitResult& Hit, float Up, float Down)
{
    if (!World) return false;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Params(TEXT("PYRGround"), false);
    // Object-type traces also hit overlap-only volumes, so skip anything a walking body would pass through.
    for (int32 Try = 0; Try < 8; ++Try)
    {
        if (!World->LineTraceSingleByObjectType(Hit, FVector(XY.X, XY.Y, RefZ + Up), FVector(XY.X, XY.Y, RefZ - Down), Objects, Params)) return false;
        const UPrimitiveComponent* Comp = Hit.GetComponent();
        if (!Comp || Comp->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block) return true;
        Params.AddIgnoredComponent(Comp);
    }
    return false;
}

void Paint(UPrimitiveComponent* Comp, const FLinearColor& Colour)
{
    if (!Comp) return;
    static TWeakObjectPtr<UMaterialInterface> Base;
    if (!Base.IsValid()) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Encounters/M_Paint.M_Paint"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    UMaterialInterface* Material = Base.IsValid() ? Base.Get() : Comp->GetMaterial(0);
    if (!Material) return;
    UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(Material, Comp);
    Mat->SetVectorParameterValue(TEXT("Color"), Colour);
    for (int32 I = 0; I < FMath::Max(1, Comp->GetNumMaterials()); ++I) Comp->SetMaterial(I, Mat);
}
}
