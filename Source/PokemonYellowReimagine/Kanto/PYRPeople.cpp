#include "Kanto/PYRPeople.h"

#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/YellowWorld.h"
#include "Misc/PackageName.h"
#include "World/PYRLetters.h"
#include "World/PYRKit.h"
#include "World/PYRPaint.h"

using namespace PYR;

// ------------------------------------------------------------------------------------------------ sprite table
namespace PYRSprites
{
namespace
{
struct FRow { const TCHAR* Sprite; const TCHAR* Model; float Height; bool bPokemon; bool bProp; };
// The Game Boy sprites of Pallet Town to Cerulean (pokeyellow constants/sprite_constants.asm) and the model each one
// wears. People without a model of their own wear the nearest one (listed in the m4 core report).
const FRow Rows[] = {
    {TEXT("SPRITE_RED"), TEXT("red"), 145, false, false},
    {TEXT("SPRITE_BLUE"), TEXT("gary"), 150, false, false},
    {TEXT("SPRITE_OAK"), TEXT("professor_oak"), 172, false, false},
    {TEXT("SPRITE_MOM"), TEXT("mom"), 162, false, false},
    {TEXT("SPRITE_DAISY"), TEXT("daisy"), 160, false, false},
    {TEXT("SPRITE_YOUNGSTER"), TEXT("youngster"), 135, false, false},
    {TEXT("SPRITE_GAMEBOY_KID"), TEXT("gameboy_kid"), 130, false, false},
    {TEXT("SPRITE_LITTLE_BOY"), TEXT("little_boy"), 120, false, false},
    {TEXT("SPRITE_COOLTRAINER_M"), TEXT("jr_trainer"), 160, false, false},
    {TEXT("SPRITE_COOLTRAINER_F"), TEXT("lass"), 150, false, false},
    {TEXT("SPRITE_BRUNETTE_GIRL"), TEXT("brunette_girl"), 150, false, false},
    {TEXT("SPRITE_GIRL"), TEXT("old_man_granddaughter"), 140, false, false},
    {TEXT("SPRITE_LITTLE_GIRL"), TEXT("little_girl"), 115, false, false},
    {TEXT("SPRITE_SUPER_NERD"), TEXT("super_nerd"), 170, false, false},
    {TEXT("SPRITE_SCIENTIST"), TEXT("scientist"), 170, false, false},
    {TEXT("SPRITE_HIKER"), TEXT("hiker"), 175, false, false},
    {TEXT("SPRITE_ROCKET"), TEXT("rocket_grunt"), 175, false, false},
    {TEXT("SPRITE_GIOVANNI"), TEXT("giovanni"), 180, false, false},
    {TEXT("SPRITE_JESSIE"), TEXT("jessie"), 170, false, false},
    {TEXT("SPRITE_JAMES"), TEXT("james"), 175, false, false},
    {TEXT("SPRITE_NURSE"), TEXT("nurse_joy"), 165, false, false},
    {TEXT("SPRITE_LINK_RECEPTIONIST"), TEXT("museum_clerk"), 165, false, false},   // the cached link_receptionist faces sideways: a desk clerk stands in
    {TEXT("SPRITE_CLERK"), TEXT("mart_clerk"), 170, false, false},
    {TEXT("SPRITE_BIKE_SHOP_CLERK"), TEXT("bike_shop_clerk"), 170, false, false},
    {TEXT("SPRITE_GENTLEMAN"), TEXT("gentleman"), 170, false, false},
    {TEXT("SPRITE_BALDING_GUY"), TEXT("balding_guy"), 168, false, false},
    {TEXT("SPRITE_GAMBLER"), TEXT("gambler"), 172, false, false},
    {TEXT("SPRITE_ROCKER"), TEXT("rocker"), 172, false, false},
    {TEXT("SPRITE_MIDDLE_AGED_MAN"), TEXT("middle_aged_man"), 170, false, false},
    {TEXT("SPRITE_MIDDLE_AGED_WOMAN"), TEXT("granny"), 158, false, false},
    {TEXT("SPRITE_GRANNY"), TEXT("granny"), 150, false, false},
    {TEXT("SPRITE_GRAMPS"), TEXT("viridian_old_man"), 160, false, false},
    {TEXT("SPRITE_GAMBLER_ASLEEP"), TEXT("viridian_old_man"), 160, false, false},
    {TEXT("SPRITE_FISHER"), TEXT("fisher"), 172, false, false},
    {TEXT("SPRITE_FISHING_GURU"), TEXT("fishing_guru"), 168, false, false},
    {TEXT("SPRITE_SWIMMER"), TEXT("swimmer"), 165, false, false},
    {TEXT("SPRITE_GYM_GUIDE"), TEXT("gym_guide"), 172, false, false},
    {TEXT("SPRITE_GUARD"), TEXT("guard"), 176, false, false},
    {TEXT("SPRITE_OFFICER_JENNY"), TEXT("officer_jenny"), 166, false, false},
    {TEXT("SPRITE_BRUNO"), TEXT("hiker"), 185, false, false},
    // Pokemon sprites.
    {TEXT("SPRITE_BIRD"), TEXT("pidgey"), 60, true, false},
    {TEXT("SPRITE_MONSTER"), TEXT("clefairy"), 110, true, false},
    {TEXT("SPRITE_CLEFAIRY"), TEXT("clefairy"), 80, true, false},
    {TEXT("SPRITE_CHANSEY"), TEXT("chansey"), 110, true, false},
    {TEXT("SPRITE_JIGGLYPUFF"), TEXT("jigglypuff"), 60, true, false},
    {TEXT("SPRITE_PIKACHU"), TEXT("pikachu"), 45, true, false},
    {TEXT("SPRITE_BULBASAUR"), TEXT("bulbasaur"), 60, true, false},
    {TEXT("SPRITE_ODDISH"), TEXT("oddish"), 50, true, false},
    {TEXT("SPRITE_SANDSHREW"), TEXT("sandshrew"), 60, true, false},
    {TEXT("SPRITE_SLOWBRO"), TEXT("slowpoke"), 90, true, false},
    {TEXT("SPRITE_SEEL"), TEXT("slowpoke"), 90, true, false},
    // Things on tables and floors.
    {TEXT("SPRITE_POKE_BALL"), TEXT("item_ball"), 30, false, true},
    {TEXT("SPRITE_POKEDEX"), TEXT(""), 18, false, true},
    {TEXT("SPRITE_CLIPBOARD"), TEXT(""), 10, false, true},
    {TEXT("SPRITE_FOSSIL"), TEXT("dome_fossil"), 35, false, true},
    {TEXT("SPRITE_OLD_AMBER"), TEXT(""), 25, false, true},
    {TEXT("SPRITE_PAPER"), TEXT(""), 5, false, true},
    {TEXT("SPRITE_BOOK_MAP_DEX"), TEXT(""), 10, false, true},
};

// People with a model of their own in one map (the leaders, Bill, the old man).
struct FMapRow { const TCHAR* Map; const TCHAR* Sprite; const TCHAR* Model; };
const FMapRow MapRows[] = {
    {TEXT("PEWTER_GYM"), TEXT("SPRITE_SUPER_NERD"), TEXT("brock")},
    {TEXT("PEWTER_GYM"), TEXT("SPRITE_BROCK"), TEXT("brock")},
    {TEXT("CERULEAN_GYM"), TEXT("SPRITE_BRUNETTE_GIRL"), TEXT("misty")},
    {TEXT("CERULEAN_GYM"), TEXT("SPRITE_MISTY"), TEXT("misty")},
    {TEXT("BILLS_HOUSE"), TEXT("SPRITE_SUPER_NERD"), TEXT("bill")},
    {TEXT("VIRIDIAN_CITY"), TEXT("SPRITE_GAMBLER_ASLEEP"), TEXT("viridian_old_man")},
    {TEXT("VIRIDIAN_CITY"), TEXT("SPRITE_GAMBLER"), TEXT("viridian_old_man")},
    {TEXT("VIRIDIAN_CITY"), TEXT("SPRITE_GIRL"), TEXT("old_man_granddaughter")},
    {TEXT("PEWTER_CITY"), TEXT("SPRITE_YOUNGSTER"), TEXT("pewter_boy")},
    {TEXT("MUSEUM_1F"), TEXT("SPRITE_SCIENTIST"), TEXT("museum_clerk")},
    {TEXT("MT_MOON_POKECENTER"), TEXT("SPRITE_MIDDLE_AGED_MAN"), TEXT("magikarp_salesman")},
    // The house Pokemon: Speary the Spearow and the Pewter boy's Nidoran.
    {TEXT("VIRIDIAN_NICKNAME_HOUSE"), TEXT("SPRITE_BIRD"), TEXT("spearow")},
    {TEXT("PEWTER_NIDORAN_HOUSE"), TEXT("SPRITE_MONSTER"), TEXT("nidoran_m")},
};
}

FPYRSpriteModel For(FName Sprite, FName Map)
{
    FPYRSpriteModel Out;
    const FString S = Sprite.ToString().ToUpper();
    for (const FRow& R : Rows)
        if (S == R.Sprite)
        {
            Out.Model = R.Model;
            Out.HeightCm = R.Height;
            Out.bPokemon = R.bPokemon;
            Out.bProp = R.bProp;
            break;
        }
    if (Out.Model.IsEmpty() && !Out.bProp) { Out.Model = TEXT("youngster"); Out.HeightCm = 150.f; }  // An unknown sprite: a townsperson.
    const FString M = Map.ToString();
    for (const FMapRow& R : MapRows) if (M == R.Map && S == R.Sprite) Out.Model = R.Model;
    Out.bLying = S == TEXT("SPRITE_GAMBLER_ASLEEP");
    return Out;
}

void Known(TArray<FName>& Out)
{
    for (const FRow& R : Rows) Out.Add(FName(R.Sprite));
}
}

namespace
{
// Load SK_ (preferred) or SM_ for a model folder; fit under Parent at HeightCm, facing the actor's forward (+X).
USceneComponent* FitModel(AActor* Owner, USceneComponent* Parent, const FString& Folder, const FString& Id, float HeightCm, bool bPokemon,
    USkeletalMeshComponent*& OutRig)
{
    OutRig = nullptr;
    if (Id.IsEmpty()) return nullptr;
    const FString Sk = FString::Printf(TEXT("/Game/%s/%s/SK_%s"), *Folder, *Id, *Id);
    if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(LoadOptional(USkeletalMesh::StaticClass(), Sk)))
    {
        USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(Owner);
        C->SetSkeletalMesh(Mesh);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetupAttachment(Parent);
        C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
        const FBox Box = Mesh->GetBounds().GetBox();
        const float Scale = HeightCm / FMath::Max(1.f, float(Box.GetSize().Z));
        C->SetRelativeScale3D(FVector(Scale));
        C->SetRelativeLocation(FVector(0, 0, -Box.Min.Z * Scale));   // Imported rigs face +X.
        C->RegisterComponent();
        Owner->AddInstanceComponent(C);
        OutRig = C;
        return C;
    }
    const FString Sm = FString::Printf(TEXT("/Game/%s/%s/SM_%s"), *Folder, *Id, *Id);
    if (UStaticMesh* Mesh = Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), Sm)))
        return FitStaticBody(Owner, Parent, Mesh, HeightCm, 0.f, bPokemon ? 0.f : 90.f);
    return nullptr;
}
}

USceneComponent* PYRSprites::SpawnModel(AActor* Owner, USceneComponent* Parent, const FString& Model, float HeightCm, bool bPokemon)
{
    USkeletalMeshComponent* Rig = nullptr;
    USceneComponent* Body = FitModel(Owner, Parent, bPokemon ? TEXT("Pokemon") : TEXT("Characters"), Model, HeightCm, bPokemon, Rig);
    if (Rig)
    {
        // Standing still: the idle clip, else a slowed walk (Pokemon rigs carry only their walk).
        const FString Folder = bPokemon ? TEXT("Pokemon") : TEXT("Characters");
        for (const TCHAR* Clip : {TEXT("standing"), TEXT("idle"), TEXT("walk"), TEXT("walking")})
            if (UAnimSequence* A = Cast<UAnimSequence>(LoadOptional(UAnimSequence::StaticClass(), FString::Printf(TEXT("/Game/%s/%s/A_%s_%s"), *Folder, *Model, *Model, Clip))))
            {
                Rig->SetAnimationMode(EAnimationMode::AnimationSingleNode);
                Rig->PlayAnimation(A, true);
                if (FCString::Strstr(Clip, TEXT("walk"))) Rig->SetPlayRate(.25f);
                break;
            }
        Rig->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
    }
    return Body;
}

// ------------------------------------------------------------------------------------------------ person
APYRPerson::APYRPerson()
{
    PrimaryActorTick.bCanEverTick = true;
    Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
    Capsule->InitCapsuleSize(32.f, 85.f);
    Capsule->SetCollisionProfileName(TEXT("Pawn"));
    Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);   // the follow camera tilts over people, never into them
    Capsule->SetCanEverAffectNavigation(false);
    RootComponent = Capsule;
    Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
    Visual->SetupAttachment(Capsule);
    Visual->SetRelativeLocation(FVector(0, 0, -85.f));
    Tags.Add(TEXT("PYRPerson"));
}

void APYRPerson::Init(AYellowWorld* InWorld, const FYMap& InMap, const FYNpc& Npc)
{
    World = InWorld;
    Map = InMap.Id;
    Id = Npc.Id;
    Index = Npc.Index;
    Sprite = Npc.Sprite;
    Model = PYRSprites::For(Sprite, Map);
    Home = Tile = InMap.Origin + FIntPoint(Npc.X, Npc.Y);
    Facing = HomeFacing = Npc.Facing;
    bWalker = Npc.bWalk;
    WalkAxis = Npc.WalkAxis;
    TextId = Npc.Text;
    Texts = Npc.Texts;
    Texts.AddUnique(Npc.Text);
    Script = Npc.Script;
    Trainer = Npc.Trainer;
    // Yellow draws bug catchers with the youngster sprite; the class has its own model.
    if (Trainer.bValid && Trainer.Class.ToString().Contains(TEXT("BUG_CATCHER"))) Model.Model = TEXT("bug_catcher");
    GiveItem = Npc.Item;
    Raw = Npc.Raw;
    Rng.Initialize(int32(GetTypeHash(Map) ^ (Index * 7919)));
    NextWander = Rng.FRandRange(1.0, 3.0);
    Tags.Add(FName(*FString::Printf(TEXT("Person.%s.%s"), *Map.ToString(), *Id)));
    if (Trainer.bValid) Tags.Add(TEXT("Trainer"));
    Teleport(Tile, Facing);
    BuildBody();
}

bool APYRPerson::IsBeaten() const
{
    const UPYRProgress* P = UPYRProgress::Get(this);
    return P && P->IsBeaten(TrainerKey());
}

FString APYRPerson::TrainerKey() const
{
    return UPYRProgress::TrainerKey(Map, Id);
}

FString APYRPerson::ClassName() const
{
    if (!Trainer.bValid) return FString();
    const AYellowWorld* W = World.Get();
    const FYTrainerClass* C = W && W->GetData() ? W->GetData()->TrainerClass(Trainer.Class) : nullptr;
    return C ? C->Name : Trainer.Class.ToString().Replace(TEXT("OPP_"), TEXT("")).Replace(TEXT("_"), TEXT(" "));
}

FString APYRPerson::SpeakerName() const
{
    // A trainer by his Game Boy class (BUG CATCHER, and the leaders' own names, BROCK and MISTY); townsfolk by what they
    // are; things (a Poke Ball, the Pokedex, a fossil) and nameless Pokemon say nothing with a name.
    const FString Class = ClassName();
    if (!Class.IsEmpty()) return Class;
    // The map's own people first (the Game Boy reuses a sprite for them: the Viridian old man is a gambler's).
    static const TMap<FString, FString> ByModel = {{TEXT("viridian_old_man"), TEXT("OLD MAN")}, {TEXT("old_man_granddaughter"), TEXT("GIRL")},
        {TEXT("brock"), TEXT("BROCK")}, {TEXT("misty"), TEXT("MISTY")}, {TEXT("bill"), TEXT("BILL")}};
    if (const FString* Own = ByModel.Find(Model.Model)) return *Own;
    static const TMap<FString, FString> Names = {
        {TEXT("OAK"), TEXT("PROF.OAK")}, {TEXT("BLUE"), TEXT("<RIVAL>")}, {TEXT("MOM"), TEXT("MOM")}, {TEXT("DAISY"), TEXT("DAISY")},
        {TEXT("NURSE"), TEXT("NURSE")}, {TEXT("CLERK"), TEXT("CLERK")}, {TEXT("BIKE_SHOP_CLERK"), TEXT("CLERK")},
        {TEXT("LINK_RECEPTIONIST"), TEXT("RECEPTIONIST")}, {TEXT("GYM_GUIDE"), TEXT("GYM GUIDE")}, {TEXT("OFFICER_JENNY"), TEXT("OFFICER JENNY")},
        {TEXT("GRAMPS"), TEXT("OLD MAN")}, {TEXT("GENTLEMAN"), TEXT("GENTLEMAN")}, {TEXT("MIDDLE_AGED_MAN"), TEXT("MAN")},
        {TEXT("MIDDLE_AGED_WOMAN"), TEXT("WOMAN")}, {TEXT("BALDING_GUY"), TEXT("MAN")}, {TEXT("GIRL"), TEXT("GIRL")},
        {TEXT("BRUNETTE_GIRL"), TEXT("GIRL")}, {TEXT("LITTLE_GIRL"), TEXT("LITTLE GIRL")}, {TEXT("LITTLE_BOY"), TEXT("BOY")},
        {TEXT("GAMEBOY_KID"), TEXT("KID")}, {TEXT("YOUNGSTER"), TEXT("YOUNGSTER")}, {TEXT("COOLTRAINER_M"), TEXT("COOLTRAINER")},
        {TEXT("COOLTRAINER_F"), TEXT("COOLTRAINER")}, {TEXT("SUPER_NERD"), TEXT("SUPER NERD")}, {TEXT("SCIENTIST"), TEXT("SCIENTIST")},
        {TEXT("HIKER"), TEXT("HIKER")}, {TEXT("GAMBLER"), TEXT("GAMBLER")}, {TEXT("GAMBLER_ASLEEP"), TEXT("GAMBLER")},
        {TEXT("ROCKET"), TEXT("ROCKET")}, {TEXT("FISHER"), TEXT("FISHERMAN")}, {TEXT("FISHING_GURU"), TEXT("FISHING GURU")},
        {TEXT("ROCKER"), TEXT("ROCKER")}, {TEXT("SWIMMER"), TEXT("SWIMMER")}, {TEXT("GUARD"), TEXT("GUARD")},
        {TEXT("JESSIE"), TEXT("JESSIE")}, {TEXT("JAMES"), TEXT("JAMES")}, {TEXT("GIOVANNI"), TEXT("GIOVANNI")},
        {TEXT("CHANSEY"), TEXT("CHANSEY")}, {TEXT("JIGGLYPUFF"), TEXT("JIGGLYPUFF")}, {TEXT("ODDISH"), TEXT("ODDISH")},
        {TEXT("SANDSHREW"), TEXT("SANDSHREW")}, {TEXT("BULBASAUR"), TEXT("BULBASAUR")}};
    const FString Key = Sprite.ToString().Replace(TEXT("SPRITE_"), TEXT(""));
    const FString* Found = Names.Find(Key);
    return Found ? *Found : FString();
}

void APYRPerson::BuildBody()
{
    Rig = nullptr;
    if (Model.bProp)
    {
        // Things on tables: the Poke Ball model, or a small painted object.
        USkeletalMeshComponent* Unused = nullptr;
        const bool bBall = Model.Model == TEXT("item_ball");
        const float TableZ = World.IsValid() && World->LookAt(Tile) == TEXT('t') ? 75.f : 0.f;
        USceneComponent* Body = FitModel(this, Visual, TEXT("Props"), Model.Model, Model.HeightCm, true, Unused);
        if (Body) Body->SetRelativeLocation(Body->GetRelativeLocation() + FVector(0, 0, TableZ));
        else
        {
            // m4v: a small painted object (the anime kit's plain paint, tinted).
            auto Paint = [this](UStaticMeshComponent* C, const FLinearColor& Colour) { PYRKit::Apply(C, EPYRKit::Paint, Colour); };
            if (bBall)
            {
                Paint(Shape(this, Visual, EShape::Sphere, FVector(0, 0, TableZ + 15), FVector(30), PYRColor::Red), PYRColor::Red);
                Paint(Shape(this, Visual, EShape::Sphere, FVector(0, 0, TableZ + 12), FVector(30.5f, 30.5f, 14), PYRColor::White), PYRColor::White);
            }
            else if (Id == TEXT("BLUESHOUSE_TOWN_MAP"))
            {
                // Daisy's Town Map: a rolled map on the table.
                Paint(Shape(this, Visual, EShape::Cylinder, FVector(0, 0, TableZ + 8), FVector(16, 16, 50), PYRColor::Cream, false, FRotator(0, 0, 90)), PYRColor::Cream);
                Paint(Shape(this, Visual, EShape::Cylinder, FVector(0, 0, TableZ + 8), FVector(17, 17, 6), PYRColor::Red, false, FRotator(0, 0, 90)), PYRColor::Red);
            }
            else if (Sprite == TEXT("SPRITE_POKEDEX"))
            {
                // A Pokedex: a closed red device with its little blue lens.
                Paint(Shape(this, Visual, EShape::Cube, FVector(0, 0, TableZ + 4), FVector(26, 20, 8), PYRColor::Red), PYRColor::Red);
                Paint(Shape(this, Visual, EShape::Sphere, FVector(-6, -4, TableZ + 9), FVector(7, 7, 3), PYRColor::Blue), PYRColor::Blue);
            }
            else if (Sprite == TEXT("SPRITE_PAPER") || Sprite == TEXT("SPRITE_CLIPBOARD") || Sprite == TEXT("SPRITE_BOOK_MAP_DEX"))
                Paint(Shape(this, Visual, EShape::Cube, FVector(0, 0, TableZ + 2), FVector(24, 32, 3), PYRColor::Cream), PYRColor::Cream);
            else if (Sprite == TEXT("SPRITE_OLD_AMBER"))
                Paint(Shape(this, Visual, EShape::Sphere, FVector(0, 0, TableZ + 12), FVector(22, 18, 24), PYRColor::Hex(0xf0a030)), PYRColor::Hex(0xf0a030));
            else Paint(Shape(this, Visual, EShape::Cube, FVector(0, 0, TableZ + Model.HeightCm * .5f), FVector(30, 22, Model.HeightCm), PYRColor::Red), PYRColor::Red);
        }
        Capsule->SetCapsuleSize(40.f, 60.f);
        return;
    }
    USkeletalMeshComponent* RigRaw = nullptr;
    USceneComponent* Body = FitModel(this, Visual, Model.bPokemon ? TEXT("Pokemon") : TEXT("Characters"), Model.Model, Model.HeightCm, Model.bPokemon, RigRaw);
    Rig = RigRaw;
    if (!Body)
    {
        // Primitive person in palette colours.
        const float H = Model.HeightCm;
        Shape(this, Visual, EShape::Cylinder, FVector(0, 0, H * .3f), FVector(34, 34, H * .6f), PYRColor::Blue);
        Shape(this, Visual, EShape::Sphere, FVector(0, 0, H * .78f), FVector(34, 34, 36), PYRColor::Skin);
    }
    if (Rig)
    {
        for (const TPair<const TCHAR*, const TCHAR*>& C : {TPair<const TCHAR*, const TCHAR*>(TEXT("idle"), TEXT("standing")),
                 TPair<const TCHAR*, const TCHAR*>(TEXT("walk"), TEXT("walking")), TPair<const TCHAR*, const TCHAR*>(TEXT("run"), TEXT("run"))})
            for (const TCHAR* Asset : {C.Key, C.Value})
                if (UAnimSequence* A = Cast<UAnimSequence>(LoadOptional(UAnimSequence::StaticClass(), FString::Printf(TEXT("/Game/Characters/%s/A_%s_%s"), *Model.Model, *Model.Model, Asset))))
                {
                    A->bForceRootLock = false;
                    Clips.Add(FName(C.Key), A);
                }
        Rig->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        PlayClip(TEXT("idle"));
    }
    if (Model.bLying) Visual->SetRelativeRotation(FRotator(0, 0, 90));
}

void APYRPerson::PlayClip(FName Want)
{
    if (!Rig || Clips.Num() == 0 || Want == Clip) return;
    TObjectPtr<UAnimSequence>* A = Clips.Find(Want);
    if (!A) A = Clips.Find(TEXT("idle"));
    if (!A) return;
    Clip = Want;
    Rig->PlayAnimation(*A, true);
}

void APYRPerson::Teleport(FIntPoint Kanto, EYDir Face)
{
    Tile = Kanto;
    bStepping = false;
    const FVector At = World.IsValid() ? World->KantoCenter(Kanto) : FVector::ZeroVector;
    SetActorLocationAndRotation(At + FVector(0, 0, Capsule->GetUnscaledCapsuleHalfHeight() + 1.f), FRotator(0, YDir::Yaw(Face), 0), false, nullptr, ETeleportType::TeleportPhysics);
    Facing = Face;
    // m4u: someone on a water tile (Cerulean Gym's swimmers) is in the pool to the waist, not standing on its surface
    // (the blind judge's floating swimmer); the painted water hides the rest.
    if (Visual) Visual->SetRelativeLocation(FVector(0, 0, World.IsValid() && World->LookAt(Kanto) == TEXT('W') ? -185.f : -85.f));
}

void APYRPerson::Face(EYDir Dir)
{
    if (Dir == EYDir::None) return;
    Facing = Dir;
    SetActorRotation(FRotator(0, YDir::Yaw(Dir), 0));
}

void APYRPerson::FaceToward(const FVector& Point)
{
    Face(YDir::FromVector(Point - GetActorLocation()));
}

bool APYRPerson::Step(EYDir Dir, bool bForce)
{
    if (bStepping || Dir == EYDir::None) return false;
    Face(Dir);
    const FIntPoint To = Tile + YDir::Delta(Dir);
    UPYRField* Field = UPYRField::Get(this);
    const AYellowWorld* W = World.Get();
    if (!W || W->IsSolidAt(To)) return false;
    if (!bForce && Field && Field->IsOccupied(To, this)) return false;
    StepFrom = GetActorLocation();
    StepTo = W->KantoCenter(To) + FVector(0, 0, Capsule->GetUnscaledCapsuleHalfHeight() + 1.f);
    StepT = 0.f;
    Tile = To;
    bStepping = true;
    PlayClip(TEXT("walk"));
    return true;
}

void APYRPerson::Walk(const TArray<EYDir>& InSteps, TFunction<void()> OnDone)
{
    Steps = InSteps;
    WalkDone = MoveTemp(OnDone);
    bFrozen = true;
    if (Steps.Num() == 0 && WalkDone) { TFunction<void()> Done = MoveTemp(WalkDone); WalkDone = nullptr; Done(); }
}

void APYRPerson::SetCulled(bool bCull)
{
    if (Visual) Visual->SetVisibility(!bCull, true);
}

void APYRPerson::SetShown(bool bShow)
{
    bShown = bShow;
    SetActorHiddenInGame(!bShow);
    SetActorEnableCollision(bShow);
}

void APYRPerson::Exclaim(float Seconds)
{
    ExclaimUntil = GetWorld()->GetTimeSeconds() + Seconds;
    if (!Bang)
    {
        Bang = Shape(this, Capsule, EShape::Cube, FVector(0, 0, Model.HeightCm * .5f + 55.f), FVector(10, 10, 34), PYRColor::Red);
        Shape(this, Bang, EShape::Sphere, FVector(0, 0, -.9f), FVector(.9f, .9f, .3f), PYRColor::Red);
    }
    Bang->SetVisibility(true, true);
}

bool APYRPerson::IsExclaiming() const
{
    return GetWorld() && GetWorld()->GetTimeSeconds() < ExclaimUntil;
}

void APYRPerson::Wander()
{
    // Game Boy walkers take one step in a random direction now and then, staying near home.
    static const EYDir Dirs[] = {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right};
    // up_down walkers pace a column, left_right walkers a row.
    const int32 Pick = WalkAxis == 1 ? Rng.RandRange(0, 1) : WalkAxis == 2 ? Rng.RandRange(2, 3) : Rng.RandRange(0, 3);
    const EYDir D = Dirs[Pick];
    const FIntPoint To = Tile + YDir::Delta(D);
    if (FMath::Abs(To.X - Home.X) > 2 || FMath::Abs(To.Y - Home.Y) > 2) { Face(D); return; }
    UPYRField* Field = UPYRField::Get(this);
    if (Field && !Field->CanStep(To, this)) { Face(D); return; }
    // Walkers stay in their own map.
    if (World.IsValid()) { const FYMap* M = World->MapAtKanto(To); if (!M || M->Id != Map) { Face(D); return; } }
    Step(D);
}

void APYRPerson::Tick(float Dt)
{
    Super::Tick(Dt);
    if (Bang && GetWorld()->GetTimeSeconds() >= ExclaimUntil) Bang->SetVisibility(false, true);
    if (bStepping)
    {
        StepT += Dt * WalkSpeed / 100.f;
        const float A = FMath::Clamp(StepT, 0.f, 1.f);
        SetActorLocation(FMath::Lerp(StepFrom, StepTo, A));
        if (A >= 1.f) { bStepping = false; if (Steps.Num() == 0) PlayClip(TEXT("idle")); }
        return;
    }
    if (Steps.Num() > 0)
    {
        const EYDir D = Steps[0];
        if (Step(D, true)) Steps.RemoveAt(0);
        else if (World.IsValid() && World->IsSolidAt(Tile + YDir::Delta(D))) Steps.RemoveAt(0);  // A wall: skip that step.
        if (Steps.Num() == 0 && !bStepping && WalkDone) { TFunction<void()> Done = MoveTemp(WalkDone); WalkDone = nullptr; Done(); }
        return;
    }
    if (WalkDone) { TFunction<void()> Done = MoveTemp(WalkDone); WalkDone = nullptr; Done(); }
    if (!bShown || bFrozen) return;
    UPYRField* Field = UPYRField::Get(this);
    if (Field && Field->IsLocked()) return;
    if (bWalker)
    {
        NextWander -= Dt;
        if (NextWander <= 0.f) { NextWander = Rng.FRandRange(1.2f, 3.2f); Wander(); }
    }
}

// ------------------------------------------------------------------------------------------------ sign
APYRSignpost::APYRSignpost()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Tags.Add(TEXT("PYRSign"));
}

void APYRSignpost::Init(AYellowWorld* InWorld, const FYMap& InMap, const FYSign& Sign)
{
    Map = InMap.Id;
    Tile = InMap.Origin + FIntPoint(Sign.X, Sign.Y);
    TextId = Sign.Text;
    const TCHAR Look = InMap.Look(Sign.X, Sign.Y);
    bOnWall = Look == TEXT('B') || Look == TEXT('n') || Look == TEXT('D') || Look == TEXT('R') || Look == TEXT('b') || Look == TEXT('P');
    SetActorLocation(InWorld->KantoCenter(Tile));
    // m4u: what the sign says, lettered on its face (the blind judge read the boards as blank): the place's name or the
    // sign's heading (PEWTER CITY, ROUTE 2, TRAINER TIPS). A house plate names no owner, whom the player may rename.
    FString Said = InWorld->GetData() ? InWorld->GetData()->TextFor(Sign.Text) : FString();
    if (Said.Contains(TEXT("<"))) { const int32 At = Said.Find(TEXT("'s ")); Said = At != INDEX_NONE ? Said.Mid(At + 3).ToUpper() : FString(); }
    Label = PYRLetters::SignLabel(Said);
    auto Letter = [this](const FVector& Center, float Width, float Height, float MaxPx)
    {
        if (Label.Num() == 0) return;
        UInstancedStaticMeshComponent* Ink = Instanced(this, EShape::Cube, PYRColor::Ink, false, false, false);
        PYRKit::Apply(Ink, EPYRKit::Ink, PYRColor::Ink);
        PYRLetters::Draw(Ink, GetActorLocation() + Center, FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), Label,
            PYRLetters::FitPixel(Label, Width, Height, MaxPx), 0.3f);   // m4v: thin plates, so the outline pass leaves the strokes crisp
    };
    if (bOnWall)
    {
        // A plate on the wall's front (house signs, the gym plate, shelves and machines indoors).
        PYRKit::Apply(Shape(this, nullptr, EShape::Cube, FVector(0, 52, 150), FVector(70, 4, 40), PYRColor::Hex(0xf2e2c4)), EPYRKit::Sign, PYRColor::Hex(0xf2e2c4));
        Letter(FVector(0, 54, 150), 62.f, 32.f, 2.4f);
        return;
    }
    if (UStaticMesh* Mesh = Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), TEXT("/Game/Props/signpost/SM_signpost"))))
    {
        UStaticMeshComponent* Body = FitStaticBody(this, nullptr, Mesh, 110.f, 0.f, 90.f);
        // A pale notice pinned flat to the front of the board (the board itself is hand-shaped), lettered.
        const FBox B = Mesh->GetBoundingBox().TransformBy(Body->GetRelativeTransform());
        const float BoardZ = FMath::Lerp(B.Min.Z, B.Max.Z, .74f), Front = B.Max.Y;
        const float W = FMath::Max(40.f, B.GetSize().X * .78f), H = FMath::Max(24.f, B.GetSize().Z * .3f);
        PYRKit::Apply(Shape(this, nullptr, EShape::Cube, FVector(B.GetCenter().X, Front + 1.f, BoardZ), FVector(W, 2.f, H), PYRColor::Hex(0xfaf0d2)),
            EPYRKit::Paint, PYRColor::Hex(0xfaf0d2));
        Letter(FVector(B.GetCenter().X, Front + 2.f, BoardZ), W - 8.f, H - 7.f, 2.6f);
        return;
    }
    PYRKit::Apply(Shape(this, nullptr, EShape::Cube, FVector(0, 0, 45), FVector(10, 10, 90), PYRColor::Umber), EPYRKit::TreeTrunk, PYRColor::Umber);
    PYRKit::Apply(Shape(this, nullptr, EShape::Cube, FVector(0, 4, 90), FVector(76, 8, 48), PYRColor::Hex(0xe8c888)), EPYRKit::Sign, PYRColor::Hex(0xe8c888));
    Letter(FVector(0, 8, 90), 68.f, 40.f, 2.6f);
}

// ------------------------------------------------------------------------------------------------ item ball
APYRItemBall::APYRItemBall()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Block = CreateDefaultSubobject<UBoxComponent>(TEXT("Block"));
    Block->SetupAttachment(RootComponent);
    Block->SetBoxExtent(FVector(40, 40, 60));
    Block->SetRelativeLocation(FVector(0, 0, 60));
    Block->SetCollisionProfileName(TEXT("BlockAll"));
    Tags.Add(TEXT("PYRItemBall"));
}

FString APYRItemBall::Key() const
{
    return UPYRProgress::SpotKey(Map, Local.X, Local.Y);
}

void APYRItemBall::Init(AYellowWorld* InWorld, const FYMap& InMap, const FYItemBall& Ball)
{
    Map = InMap.Id;
    Local = FIntPoint(Ball.X, Ball.Y);
    Tile = InMap.Origin + Local;
    Item = Ball.Item;
    SetActorLocation(InWorld->KantoCenter(Tile));
    if (UStaticMesh* Mesh = Cast<UStaticMesh>(LoadOptional(UStaticMesh::StaticClass(), TEXT("/Game/Props/item_ball/SM_item_ball"))))
    {
        FitStaticBody(this, nullptr, Mesh, 34.f, 0.f, 0.f);
        return;
    }
    PYRKit::Apply(Shape(this, nullptr, EShape::Sphere, FVector(0, 0, 17), FVector(34), PYRColor::Red), EPYRKit::Paint, PYRColor::Red);
    PYRKit::Apply(Shape(this, nullptr, EShape::Sphere, FVector(0, 0, 13), FVector(34.5f, 34.5f, 16), PYRColor::White), EPYRKit::Paint, PYRColor::White);
    PYRKit::Apply(Shape(this, nullptr, EShape::Cylinder, FVector(16, 0, 17), FVector(10, 10, 3), PYRColor::White, false, FRotator(90, 0, 0)), EPYRKit::Paint, PYRColor::White);
}
