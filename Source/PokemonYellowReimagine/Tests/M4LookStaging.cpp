// The look areas' staged scenes (pallet-town ... cutscene-oak), shared by the m4v look tests and shots. No gameplay camera overrides.
#include "GameTest.h"
#include "Battle/PYRMonPawn.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "World/PYRPaint.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/PYRStoryScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Partner/PYRPikachuFollower.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Tests/M4TestKit.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"

// The scene of one look area (pallet-town ... cutscene-oak), a step a tick: Red, Pikachu and the people placed as
// in the reference look shots, the player's ordinary camera looking north. True once it is staged (Step 3). m4v: the
// look tests (Tests/M4VLookTests.cpp) stage their frames with this, then hold a fixed camera.
bool PYRStageM4Look(UWorld* W, const FString& Area, double Seconds, int32& Step, double& At)
{
    struct FPose { const TCHAR* Area; const TCHAR* Map; float X, Y; };
    // Final tiles, measured from the reference look shots.
    static const FPose Poses[] = {
        {TEXT("pallet-town"), TEXT("PALLET_TOWN"), 10.5f,12.5f},
        {TEXT("route-1"), TEXT("ROUTE_1"),11.5f,28.5f},
        {TEXT("viridian-city"), TEXT("VIRIDIAN_CITY"),20.5f,25.5f},
        {TEXT("viridian-forest"), TEXT("VIRIDIAN_FOREST"),18.5f,43.5f},
        {TEXT("pewter-city"), TEXT("PEWTER_CITY"),19.5f,10.5f},
        {TEXT("mt-moon"), TEXT("MT_MOON_1F"),15.5f,25.5f},
        {TEXT("cerulean-city"), TEXT("CERULEAN_CITY"),7.5f,19.5f},
        {TEXT("pokemon-centre"), TEXT("VIRIDIAN_POKECENTER"),6.5f,5.5f},
        {TEXT("mart"), TEXT("VIRIDIAN_MART"),4.5f,5.5f},
        {TEXT("oaks-lab"), TEXT("OAKS_LAB"),5.5f,4.5f},
        {TEXT("characters"), TEXT("PALLET_TOWN"),9.5f,13.5f},
        {TEXT("ui"), TEXT("PALLET_TOWN"),9.5f,12.5f},
        {TEXT("ui-dialogue"), TEXT("PALLET_TOWN"),9.5f,12.5f},
        {TEXT("cutscene-oak"), TEXT("PALLET_TOWN"),10.5f,2.1f}
    };
    const FPose* Pose = nullptr;
    for (const FPose& P : Poses) if (Area == P.Area) Pose = &P;
    if (!Pose) return false;
    auto F = UPYRField::Get(W);
    auto Red = APokemonYellowReimagineCharacter::Find(W);
    auto S = UPYRTrainerState::Get(W);
    auto Progress = UPYRProgress::Get(W);
    auto Menus = UPYRMenus::Get(W);
    auto Scripts = UPYRScripts::Get(W);
    auto World = F ? F->GetYellowWorld() : nullptr;
    if (!World || !World->GetData() || !Red || !S || !Progress || !Menus || !Scripts) { GameTest::HoldShot(1); return false; }
    const FYMap* Map = World->GetData()->Map(Pose->Map);
    if (!Map) { UE_LOG(LogTemp, Error, TEXT("LOOK missing map %s"), Pose->Map); FPlatformMisc::RequestExitWithStatus(false,1); return false; }
    if (Step == 0)
    {
        M4::Quiet(true,true);
        Scripts->bSight = false;
        const bool Oak = Area == TEXT("cutscene-oak");
        if (!Oak)
        {
            FPYRMonSpec P; P.Species=TEXT("pikachu"); P.Level=5; S->DebugSetParty({P});
            for (const TCHAR* Flag : {TEXT("EVENT_GOT_STARTER"),TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"),TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"),TEXT("EVENT_OAK_GOT_PARCEL"),TEXT("EVENT_GOT_POKEDEX")}) Progress->Set(Flag);
            PYRScripts::Refresh(W);
        }
        const bool Menu = Area.StartsWith(TEXT("ui"));
        const bool West = Area==TEXT("viridian-city") || Area==TEXT("cerulean-city");
        const EYDir Facing = Oak ? EYDir::Up : Menu ? EYDir::Down : West ? EYDir::Left : EYDir::Right;
        F->PlaceRed(Pose->Map, FMath::FloorToInt(Pose->X), FMath::FloorToInt(Pose->Y), Facing);
        const FVector Ground = World->TileCenter(Pose->Map,0,0) + FVector((Pose->X-.5f)*100,(Pose->Y-.5f)*100,0);
        const float Yaw = Oak ? -90.f : Menu ? 90.f : West ? 180.f : 0.f;
        Red->PlaceAt(Ground, Yaw);
        // Web rotates its visual child toward the review camera without changing its logical walk facing.
        if (!Oak && !Menu) Red->Visual->SetRelativeRotation(FRotator(0,102.63f-Yaw,0));
        // A player's ordinary mouse yaw looking north; length/FOV/collision/room confine remain production values.
        // Oak approaches from the south: the ordinary north-side player view keeps both actors visible.
        W->GetFirstPlayerController()->SetControlRotation(FRotator(-22,Oak ? 90.f : -90.f,0));
        if (auto Pika=APYRPikachuFollower::Find(W))
        {
            Pika->SnapBehindRed();
            if (Area==TEXT("characters") || Oak) { Pika->BeginScript(); Pika->SetActorHiddenInGame(true); Pika->SetActorTickEnabled(false); }
        }
        if (Area==TEXT("characters"))
        {
            Red->SetActorHiddenInGame(true);
            for (auto Person:F->People) if (Person.IsValid()) Person->SetShown(false);
            // Match the reference lineup's canopy removal; the ordinary player camera remains untouched.
            if (auto MapActor=World->MapActor(Pose->Map)) for (auto C:MapActor->Looks)
            {
                FLinearColor Color;
                if (C && C->GetStaticMesh() && C->GetStaticMesh()->GetName()==TEXT("Sphere") && UPYRPaint::ColorOf(C,Color)
                    && Color.G>Color.R*1.2f && Color.G>Color.B*1.2f) C->SetVisibility(false);
            }
            const TCHAR* Sprites[]={TEXT("SPRITE_RED"),TEXT("SPRITE_OAK"),TEXT("SPRITE_NURSE"),TEXT("SPRITE_BLUE"),TEXT("SPRITE_MOM"),TEXT("SPRITE_CLERK"),TEXT("SPRITE_BRUNETTE_GIRL")};
            for (int32 i=0;i<7;++i)
            {
                FYNpc N; N.Id=FString::Printf(TEXT("LOOK-%d"),i); N.Sprite=Sprites[i]; N.X=6+i; N.Y=13; N.Facing=EYDir::Down;
                FYMap CastMap=*Map; if(i==6) CastMap.Id=TEXT("CERULEAN_GYM");
                auto Person=W->SpawnActor<APYRPerson>(); Person->Init(World,CastMap,N); Person->bFrozen=true;
                // The reference lineup's visual handles have no colliders. In particular Gary overlaps the hidden
                // player pivot; keeping his capsule would collapse the normal spring arm to zero.
                Person->SetActorEnableCollision(false);
                Person->SetActorLocation(World->TileCenter(Pose->Map,0,0)+FVector((6.25f+i*1.08f-.5f)*100,1300,Person->Capsule->GetUnscaledCapsuleHalfHeight()+1.f));
                Person->SetActorRotation(FRotator(0,83.1245f,0));
                // FName display casing can bypass M4's case-sensitive map-specific sprite lookup.
                // This named cast fixture selects the existing Misty asset explicitly, without changing gameplay.
                if (i==6 && Person->Rig)
                {
                    auto Mesh=Cast<USkeletalMesh>(PYR::LoadOptional(USkeletalMesh::StaticClass(),TEXT("/Game/Characters/misty/SK_misty")));
                    if (Mesh)
                    {
                        Person->Rig->SetSkeletalMesh(Mesh);
                        const FBox Bounds=Mesh->GetBounds().GetBox();
                        const float Scale=Person->Model.HeightCm/FMath::Max(1.f,float(Bounds.GetSize().Z));
                        Person->Rig->SetRelativeScale3D(FVector(Scale));
                        Person->Rig->SetRelativeLocation(FVector(0,0,-Bounds.Min.Z*Scale));
                        if (auto Idle=Cast<UAnimSequence>(PYR::LoadOptional(UAnimSequence::StaticClass(),TEXT("/Game/Characters/misty/A_misty_idle")))) Person->Rig->PlayAnimation(Idle,true);
                    }
                }
            }
        }
        // M4's own field-sized visual-only Pokemon; a shot fixture, not a new roaming encounter mechanic.
        const TCHAR* Wild = Area==TEXT("route-1") ? TEXT("pidgey") : Area==TEXT("viridian-forest") ? TEXT("caterpie") : Area==TEXT("mt-moon") ? TEXT("zubat") : nullptr;
        if (Wild)
        {
            const FIntPoint Tile = Area==TEXT("route-1") ? FIntPoint(13,25) : Area==TEXT("viridian-forest") ? FIntPoint(20,41) : FIntPoint(17,23);
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto Mon=W->SpawnActor<APYRMonPawn>(World->TileCenter(Pose->Map,Tile.X,Tile.Y)+FVector(0,0,80),FRotator(0,90,0),Params);
            Mon->InitVisualOnly(Wild);
        }
        if (Oak) PYRStory::RunEvent(Scripts,TEXT("oak_stops_red"),nullptr);
        At=Seconds; Step=1; GameTest::HoldShot(5);
    }
    if (Step==1 && Seconds-At>1.5)
    {
        if (Area==TEXT("ui")) PYRScreens::OpenStart(W);
        if (Area==TEXT("ui-dialogue")) Menus->Say(Scripts->Line(TEXT("PalletTownOakText.HeyWaitDontGoOutText")));
        if (Area==TEXT("cutscene-oak")) { Menus->Press(EPYRKey::Confirm); Menus->Press(EPYRKey::Confirm); }
        Step=2;
    }
    if (Step==2 && Seconds-At>4.5)
    {
        for (TActorIterator<APostProcessVolume> It(W); It; ++It)
            for (const auto& Blend : It->Settings.WeightedBlendables.Array)
                if (auto Material = Cast<UMaterialInterface>(Blend.Object))
                {
                    UE_LOG(LogTemp, Display, TEXT("LOOK_MATERIAL %s weight=%f"), *Material->GetPathName(), Blend.Weight);
                    for (const TCHAR* Key : {TEXT("Threshold"),TEXT("LitTone"),TEXT("ShadeTone"),TEXT("OutlineWidth"),TEXT("DepthEdge"),TEXT("NormalEdge"),TEXT("FxKeep"),TEXT("HazeStart"),TEXT("HazeEnd"),TEXT("HazeAmount")})
                    {
                        float Value=0;
                        if(Material->GetScalarParameterValue(FMaterialParameterInfo(Key),Value)) UE_LOG(LogTemp, Display, TEXT("LOOK_SCALAR %s=%f"),Key,Value);
                    }
                    for (const TCHAR* Key : {TEXT("ShadeTint"),TEXT("Ink"),TEXT("Haze")})
                    {
                        FLinearColor Value;
                        if(Material->GetVectorParameterValue(FMaterialParameterInfo(Key),Value)) UE_LOG(LogTemp, Display, TEXT("LOOK_VECTOR %s=%s"),Key,*Value.ToString());
                    }
                }
        UE_LOG(LogTemp, Display, TEXT("LOOK_REFERENCE %s map=%s tile=%d,%d red=%s camera=%s rotation=%s fov=%f arm=%f UI=%s"),
            *Area,*F->CurrentMap.ToString(),F->CurrentTile.X,F->CurrentTile.Y,*Red->GetActorLocation().ToString(),
            *Red->Camera->GetComponentLocation().ToString(),*Red->Camera->GetComponentRotation().ToString(),Red->Camera->FieldOfView,Red->Boom->TargetArmLength,*Menus->ScreenText());
        Step=3;
    }
    return Step >= 3;
}
