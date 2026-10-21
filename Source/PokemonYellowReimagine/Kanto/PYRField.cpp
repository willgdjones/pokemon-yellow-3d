#include "Kanto/PYRField.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRSysText.h"
#include "Music/GbMusicPlayer.h"
#include "Partner/PYRPikachuFollower.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"

UPYRField* UPYRField::Get(const UObject* Ctx)
{
    UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
    return W ? W->GetSubsystem<UPYRField>() : nullptr;
}

APokemonYellowReimagineCharacter* UPYRField::Red() const
{
    UWorld* W = GetWorld();
    APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
    if (!PC) return nullptr;
    if (APokemonYellowReimagineCharacter* R = Cast<APokemonYellowReimagineCharacter>(PC->GetPawn())) return R;
    // In battle the controller drives a Pokemon; Red is still the first character of his class.
    return APokemonYellowReimagineCharacter::Find(W);
}

const FYMap* UPYRField::GetMap() const
{
    const AYellowWorld* W = World.Get();
    return W && W->GetData() ? W->GetData()->Map(CurrentMap) : nullptr;
}

void UPYRField::Start(AYellowWorld* InWorld)
{
    World = InWorld;
    bStarted = true;
    SpawnObjects();
    PYRScripts::Start(GetWorld());
}

void UPYRField::SpawnObjects()
{
    AYellowWorld* W = World.Get();
    const FYellowDataSet* Data = W ? W->GetData() : nullptr;
    if (!Data) return;
    const UPYRProgress* Progress = UPYRProgress::Get(GetWorld());
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (const FName& Id : Data->MapOrder)
    {
        const FYMap* M = Data->Map(Id);
        if (!M) continue;
        for (const FYNpc& N : M->Npcs)
        {
            APYRPerson* P = GetWorld()->SpawnActor<APYRPerson>(W->KantoCenter(M->Origin + FIntPoint(N.X, N.Y)), FRotator::ZeroRotator, Params);
            P->Init(W, *M, N);
            People.Add(P);
        }
        for (const FYSign& S : M->Signs)
        {
            APYRSignpost* Sign = GetWorld()->SpawnActor<APYRSignpost>(W->KantoCenter(M->Origin + FIntPoint(S.X, S.Y)), FRotator::ZeroRotator, Params);
            Sign->Init(W, *M, S);
            Signs.Add(Sign);
        }
        for (const FYItemBall& B : M->Items)
        {
            if (Progress && Progress->IsTaken(UPYRProgress::SpotKey(M->Id, B.X, B.Y))) continue;
            APYRItemBall* Ball = GetWorld()->SpawnActor<APYRItemBall>(W->KantoCenter(M->Origin + FIntPoint(B.X, B.Y)), FRotator::ZeroRotator, Params);
            Ball->Init(W, *M, B);
            Balls.Add(Ball);
        }
    }
    UE_LOG(LogYellow, Display, TEXT("YELLOW_OBJECTS people=%d signs=%d balls=%d"), People.Num(), Signs.Num(), Balls.Num());
}

APYRPerson* UPYRField::FindPerson(FName Map, const FString& Id) const
{
    for (const TWeakObjectPtr<APYRPerson>& P : People) if (P.IsValid() && P->Map == Map && P->Id == Id) return P.Get();
    return nullptr;
}

APYRPerson* UPYRField::FindPersonBySprite(FName Map, const FString& SpritePart) const
{
    for (const TWeakObjectPtr<APYRPerson>& P : People)
        if (P.IsValid() && P->Map == Map && P->Sprite.ToString().Contains(SpritePart)) return P.Get();
    return nullptr;
}

bool UPYRField::IsOccupied(FIntPoint Kanto, const AActor* Ignore) const
{
    for (const TWeakObjectPtr<APYRPerson>& P : People)
        if (P.IsValid() && P.Get() != Ignore && P->IsShown() && P->Tile == Kanto) return true;
    const AYellowWorld* W = World.Get();
    if (APokemonYellowReimagineCharacter* R = Red())
        if (R != Ignore && W && W->ToKanto(R->GetActorLocation()) == Kanto) return true;
    for (const TWeakObjectPtr<APYRItemBall>& B : Balls) if (B.IsValid() && B->Tile == Kanto) return true;
    return false;
}

bool UPYRField::CanStep(FIntPoint Kanto, const AActor* Who) const
{
    const AYellowWorld* W = World.Get();
    if (!W || !W->MapAtKanto(Kanto) || W->IsSolidAt(Kanto)) return false;
    if (IsOccupied(Kanto, Who)) return false;
    // Pikachu's tile too.
    if (const APYRPikachuFollower* Pika = APYRPikachuFollower::Find(GetWorld()))
        if (Pika != Who && !Pika->IsHidden() && W->ToKanto(Pika->GetActorLocation()) == Kanto) return false;
    const FYMap* M = W->MapAtKanto(Kanto);
    return M->WarpAt(Kanto.X - M->Origin.X, Kanto.Y - M->Origin.Y) == INDEX_NONE;
}

FIntPoint UPYRField::FacingTile() const
{
    return KantoTile + YDir::Delta(Facing);
}

// ------------------------------------------------------------------------------------------------ locks and fade
void UPYRField::Lock(FName Why)
{
    Locks.Add(Why);
    if (APokemonYellowReimagineCharacter* R = Red()) R->LockInput(true);
}

void UPYRField::Unlock(FName Why)
{
    Locks.Remove(Why);
    if (Locks.Num() == 0) if (APokemonYellowReimagineCharacter* R = Red()) R->LockInput(false);
}

void UPYRField::Fade(float Alpha, float Seconds, const FLinearColor& Colour)
{
    FadeTarget = FMath::Clamp(Alpha, 0.f, 1.f);
    FadeColour = Colour;
    FadeRate = Seconds > 0.f ? 1.f / Seconds : 1000.f;
}

void UPYRField::ApplyFade()
{
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (PC && PC->PlayerCameraManager) PC->PlayerCameraManager->SetManualCameraFade(FadeAlpha, FadeColour, false);
}

// ------------------------------------------------------------------------------------------------ placing and warps
bool UPYRField::PlaceRed(FName MapId, int32 X, int32 Y, EYDir Face)
{
    AYellowWorld* W = World.Get();
    APokemonYellowReimagineCharacter* R = Red();
    const FYMap* M = W && W->GetData() ? W->GetData()->Map(MapId) : nullptr;
    if (!W || !R || !M) return false;
    const FVector At = W->TileCenter(MapId, X, Y);
    R->PlaceAt(At, YDir::Yaw(Face));
    Facing = Face;
    // No warp fires on the tile he arrives on until he leaves it.
    bArmed = false;
    ArrivalTile = M->Origin + FIntPoint(X, Y);
    Trail.Reset();
    UpdateTile();
    if (APYRPikachuFollower* Pika = APYRPikachuFollower::Find(GetWorld())) Pika->SnapBehindRed();
    return true;
}

bool UPYRField::TakeWarp(FName MapId, int32 Warp, bool bFade)
{
    AYellowWorld* W = World.Get();
    FName To;
    int32 ToWarp = 0;
    if (!W || !W->WarpDestination(MapId, Warp, LastOutdoor, To, ToWarp))
    {
        UE_LOG(LogYellow, Warning, TEXT("Warp %d of %s leads nowhere (last outdoor %s)"), Warp, *MapId.ToString(), *LastOutdoor.ToString());
        return false;
    }
    const FYMap* From = W->GetData()->Map(MapId);
    const FYMap* Dest = W->GetData()->Map(To);
    UGbMusicPlayer::PlaySfx(this, Dest && !Dest->bOutdoor && From && From->bOutdoor ? TEXT("go_inside") : TEXT("go_outside"));
    WarpMap = To;
    WarpIndex = ToWarp;
    if (!bFade) { Arrive(To, ToWarp); return true; }
    WarpPhase = 1;
    WarpClock = 0;
    Lock(TEXT("warp"));
    Fade(1.f, .15f);
    return true;
}

void UPYRField::Arrive(FName MapId, int32 Warp)
{
    AYellowWorld* W = World.Get();
    const FYMap* M = W ? W->GetData()->Map(MapId) : nullptr;
    if (!M || !M->Warps.IsValidIndex(Warp)) return;
    const FYWarp& Dest = M->Warps[Warp];
    PlaceRed(MapId, Dest.X, Dest.Y, Facing);
    ++WarpsTaken;
    // Arriving on a door tile (data/tilesets/door_tile_ids.asm: house and shop doors outside, the back doors of gates
    // and houses inside) the Game Boy walks Red one tile down, off the door. Someone standing there stops the step.
    bAutoStep = false;
    if (ShouldStepOffDoor(*M, Warp))
    {
        Facing = EYDir::Down;
        if (APokemonYellowReimagineCharacter* R = Red())
        {
            R->PlaceAt(W->TileCenter(MapId, Dest.X, Dest.Y), YDir::Yaw(EYDir::Down));
            R->ScriptWalkTo(W->TileCenter(MapId, Dest.X, Dest.Y + 1));
            bAutoStep = true;
        }
    }
    OnWarped.Broadcast();
}

FString UPYRField::BlockedWarpText(FName MapId, int32 WarpIdx) const
{
    const AYellowWorld* W = World.Get();
    const FYellowDataSet* D = W ? W->GetData() : nullptr;
    const FYMap* M = D ? D->Map(MapId) : nullptr;
    if (!M || !M->Warps.IsValidIndex(WarpIdx)) return FString();
    FName To;
    int32 ToWarp = 0;
    if (W->WarpDestination(MapId, WarpIdx, LastOutdoor, To, ToWarp)) return FString();
    const FName Dest = M->Warps[WarpIdx].ToMap;
    if (Dest == TEXT("ROUTE_23"))
    {
        // Route 23's guards, from the south: the first badge Red lacks stops him (scripts/Route23.asm).
        static const TPair<EPYRBadge, const TCHAR*> Guards[] = {{EPYRBadge::Cascade, TEXT("CASCADEBADGE")}, {EPYRBadge::Thunder, TEXT("THUNDERBADGE")},
            {EPYRBadge::Rainbow, TEXT("RAINBOWBADGE")}, {EPYRBadge::Soul, TEXT("SOULBADGE")}, {EPYRBadge::Marsh, TEXT("MARSHBADGE")},
            {EPYRBadge::Volcano, TEXT("VOLCANOBADGE")}, {EPYRBadge::Earth, TEXT("EARTHBADGE")}};
        const UPYRTrainerState* S = UPYRTrainerState::Get(GetWorld());
        for (const auto& G : Guards)
        {
            if (S && S->HasBadge(G.Key)) continue;
            const FYItem* Badge = D->ItemAny(G.Value);
            return D->TextFor(TEXT("_Route23YouDontHaveTheBadgeYetText")).Replace(TEXT("{str:wNameBuffer}"), Badge ? *Badge->Name : G.Value);
        }
    }
    // Cerulean Cave: the man Yellow stands in front of its door says why (only the League champion may go in).
    if (Dest == TEXT("CERULEAN_CAVE_1F")) return D->TextFor(TEXT("CeruleanCitySuperNerd3Text"));
    // Diglett's Cave: Yellow has no line here. Its Route 2 house is behind Cut trees, out of reach this early in the
    // story, so Red never gets here on foot; if he is put here he steps back off the ladder without a word.
    return FString();
}

bool UPYRField::IsOutOfRange(FName MapId, int32 WarpIdx) const
{
    const AYellowWorld* W = World.Get();
    const FYellowDataSet* D = W ? W->GetData() : nullptr;
    const FYMap* M = D ? D->Map(MapId) : nullptr;
    if (!M || !M->Warps.IsValidIndex(WarpIdx)) return false;
    FName To;
    int32 ToWarp = 0;
    return !W->WarpDestination(MapId, WarpIdx, LastOutdoor, To, ToWarp);
}

void UPYRField::BlockWarp(FName MapId, int32 WarpIdx)
{
    UPYRMenus* Menus = UPYRMenus::Get(GetWorld());
    APokemonYellowReimagineCharacter* R = Red();
    AYellowWorld* W = World.Get();
    if (!Menus || !R || !W || !IsOutOfRange(MapId, WarpIdx)) return;
    const FString Text = BlockedWarpText(MapId, WarpIdx);
    ++WarpsBlocked;
    UE_LOG(LogYellow, Display, TEXT("YELLOW_WARP_BLOCKED %s warp %d"), *MapId.ToString(), WarpIdx);
    // Back the way he came (the tile before this one), as the guards walk the player back.
    FIntPoint Back = KantoTile - YDir::Delta(Facing);
    if (Trail.Num() && FMath::Abs(Trail.Last().X - KantoTile.X) + FMath::Abs(Trail.Last().Y - KantoTile.Y) == 1) Back = Trail.Last();
    R->StopScriptWalk();
    TWeakObjectPtr<UPYRField> Self = this;
    auto StepBack = [Self, Back]()
    {
        if (!Self.IsValid()) return;
        APokemonYellowReimagineCharacter* R2 = Self->Red();
        AYellowWorld* W2 = Self->World.Get();
        if (R2 && W2 && !W2->IsSolidAt(Back)) R2->ScriptWalkTo(W2->KantoCenter(Back) + FVector(0, 0, R2->GetActorLocation().Z));
    };
    if (Text.IsEmpty()) StepBack();
    else Menus->Say(Text, StepBack);
}

bool UPYRField::ShouldStepOffDoor(const FYMap& M, int32 Warp) const
{
    if (!M.Warps.IsValidIndex(Warp) || !M.Warps[Warp].bDoor) return false;
    const FYWarp& Dest = M.Warps[Warp];
    if (!M.Contains(Dest.X, Dest.Y + 1) || YTile::IsSolid(M.Tile(Dest.X, Dest.Y + 1)) || M.WarpAt(Dest.X, Dest.Y + 1) != INDEX_NONE) return false;
    const FIntPoint Below = M.Origin + FIntPoint(Dest.X, Dest.Y + 1);
    for (const TWeakObjectPtr<APYRPerson>& P : People) if (P.IsValid() && P->IsShown() && P->Tile == Below) return false;
    return true;
}

void UPYRField::TickWarp(float Dt)
{
    if (WarpPhase == 0) return;
    WarpClock += Dt;
    if (WarpPhase == 1 && (FadeAlpha >= .999f || WarpClock > .4))
    {
        Arrive(WarpMap, WarpIndex);
        WarpPhase = 2;
        WarpClock = 0;
        Fade(0.f, .15f);
    }
    else if (WarpPhase == 2)
    {
        APokemonYellowReimagineCharacter* R = Red();
        const bool bWalking = bAutoStep && R && R->IsScriptWalking() && WarpClock < 1.0;
        if (FadeAlpha <= .001f && !bWalking)
        {
            WarpPhase = 0;
            bAutoStep = false;
            Unlock(TEXT("warp"));
        }
    }
}

// ------------------------------------------------------------------------------------------------ tick
void UPYRField::UpdateTile()
{
    AYellowWorld* W = World.Get();
    APokemonYellowReimagineCharacter* R = Red();
    if (!W || !R || R->IsHidden()) return;
    const FVector At = R->GetActorLocation();
    const FIntPoint K = W->ToKanto(At);
    const FYMap* M = W->MapAtKanto(K);
    if (!R->GetVelocity().IsNearlyZero(20.f)) Facing = YDir::FromYaw(R->GetActorRotation().Yaw);
    else Facing = YDir::FromYaw(R->GetActorRotation().Yaw);
    if (!M || (K == KantoTile && M->Id == CurrentMap)) return;
    const FIntPoint Old = KantoTile;
    KantoTile = K;
    CurrentTile = K - M->Origin;
    // The trail is Red's steps (Pikachu walks it); a placement or warp is no step.
    if (FMath::Abs(K.X - Old.X) + FMath::Abs(K.Y - Old.Y) <= 2) { if (Trail.Num() == 0 || Trail.Last() != Old) Trail.Add(Old); }
    else Trail.Reset();
    if (Trail.Num() > 16) Trail.RemoveAt(0);
    if (K != ArrivalTile) bArmed = true;
    ++TileChanges;
    if (M->Id != CurrentMap)
    {
        const FName From = CurrentMap;
        CurrentMap = M->Id;
        if (M->bOutdoor) LastOutdoor = M->Id;
        ++MapChanges;
        UE_LOG(LogYellow, Display, TEXT("YELLOW_MAP %s -> %s"), *From.ToString(), *CurrentMap.ToString());
        // Each map's own tune, crossfaded (a script that wants another song sets it after this).
        if (!M->Music.IsEmpty() && !IsLockedBy(TEXT("battle")))
            if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(GetWorld())) Music->Play(FName(*M->Music), .5f);
        OnMapChanged.Broadcast(From, CurrentMap);
    }
    if (UPYRProgress* P = UPYRProgress::Get(GetWorld())) if (P->RepelSteps > 0 && --P->RepelSteps == 0)
        if (UPYRMenus* Menus = UPYRMenus::Get(GetWorld())) Menus->Say(TEXT("REPEL's effect wore off."));
    OnTileChanged.Broadcast(CurrentMap, CurrentTile);
    // A warp tile fires the moment Red's tile becomes it.
    const int32 Warp = M->WarpAt(CurrentTile.X, CurrentTile.Y);
    if (Warp != INDEX_NONE && bArmed && WarpPhase == 0 && !IsLockedBy(TEXT("script")) && !TakeWarp(CurrentMap, Warp)) BlockWarp(CurrentMap, Warp);
}

void UPYRField::Tick(float DeltaTime)
{
    // The fade runs whatever else happens.
    if (FadeAlpha != FadeTarget)
    {
        FadeAlpha = FMath::FInterpConstantTo(FadeAlpha, FadeTarget, DeltaTime, FadeRate);
        ApplyFade();
    }
    if (!bStarted) return;
    if (UPYRProgress* P = UPYRProgress::Get(GetWorld())) if (!GetWorld()->IsPaused()) P->PlaySeconds += DeltaTime;
    TickWarp(DeltaTime);
    UpdateTile();
    // Standing on a warp at a room's or cave's edge and pushing out of it takes it (the Game Boy's exit mats).
    APokemonYellowReimagineCharacter* R = Red();
    AYellowWorld* W = World.Get();
    const FYMap* M = GetMap();
    if (R && W && M && WarpPhase == 0 && !IsLocked())
    {
        const int32 Warp = M->WarpAt(CurrentTile.X, CurrentTile.Y);
        const FVector Input = R->GetLastMovementInputVector();
        const EYDir Push = YDir::FromVector(Input);
        const bool bBlocked = Push != EYDir::None && W->IsSolidAt(KantoTile + YDir::Delta(Push)) && R->GetVelocity().Size2D() < 80.f;
        const bool bOffMap = Push != EYDir::None && !M->Contains(CurrentTile.X + YDir::Delta(Push).X, CurrentTile.Y + YDir::Delta(Push).Y);
        if (Warp != INDEX_NONE && (bBlocked || bOffMap)) EdgePush += DeltaTime;
        else EdgePush = 0.f;
        if (EdgePush > .15f) { EdgePush = 0.f; if (!TakeWarp(CurrentMap, Warp)) BlockWarp(CurrentMap, Warp); }
    }
    // Red's input follows the locks.
    if (R) R->LockInput(IsLocked());
}

// ------------------------------------------------------------------------------------------------ interaction
void UPYRField::GiveItem(FName Item, int32 Count, const FString& Verb, TFunction<void()> OnDone, const FString& TextId)
{
    UPYRTrainerState* State = UPYRTrainerState::Get(GetWorld());
    UPYRMenus* Menus = UPYRMenus::Get(GetWorld());
    const FYellowDataSet* Data = World.IsValid() ? World->GetData() : nullptr;
    const FYItem* Info = Data ? Data->Item(Item) : nullptr;
    const FString Name = Info ? Info->Name : Item.ToString().ToUpper();
    if (!State || !Menus) { if (OnDone) OnDone(); return; }
    if (!State->AddItem(Item, Count))
    {
        Menus->Say(FString::Printf(TEXT("<PLAYER> found\n%s!\fNo more room for\nitems!"), *Name), OnDone);
        return;
    }
    if (UGbMusicPlayer* Music = UGbMusicPlayer::Get(GetWorld())) Music->Jingle(Info && Info->bKey ? TEXT("get_key_item") : TEXT("get_item_1"));
    if (!TextId.IsEmpty() && Data && Data->HasText(TextId))
    {
        Menus->Say(PYRSysText::FillIn(Data->TextFor(TextId), {{TEXT("wStringBuffer"), Name}, {TEXT("wNameBuffer"), Name}}), OnDone);
        return;
    }
    Menus->Say(FString::Printf(TEXT("<PLAYER> %s\n%s!"), *Verb, *Name), OnDone);
}

bool UPYRField::HiddenAt(FIntPoint Kanto, FName& OutMap, int32& OutIndex) const
{
    const AYellowWorld* W = World.Get();
    const FYMap* M = W ? W->MapAtKanto(Kanto) : nullptr;
    const UPYRProgress* P = UPYRProgress::Get(GetWorld());
    if (!M) return false;
    for (int32 I = 0; I < M->Hidden.Num(); ++I)
    {
        const FYItemBall& H = M->Hidden[I];
        if (M->Origin + FIntPoint(H.X, H.Y) != Kanto) continue;
        if (P && P->IsTaken(TEXT("hidden:") + UPYRProgress::SpotKey(M->Id, H.X, H.Y))) continue;
        OutMap = M->Id;
        OutIndex = I;
        return true;
    }
    return false;
}

bool UPYRField::TryHidden(FIntPoint Kanto)
{
    FName MapId;
    int32 Index = 0;
    if (!HiddenAt(Kanto, MapId, Index)) return false;
    const FYMap* M = World->GetData()->Map(MapId);
    const FYItemBall& H = M->Hidden[Index];
    if (UPYRProgress* P = UPYRProgress::Get(GetWorld())) P->Take(TEXT("hidden:") + UPYRProgress::SpotKey(MapId, H.X, H.Y));
    GiveItem(H.Item, 1, TEXT("found"));
    return true;
}

bool UPYRField::Interact()
{
    APokemonYellowReimagineCharacter* R = Red();
    AYellowWorld* W = World.Get();
    UPYRMenus* Menus = UPYRMenus::Get(GetWorld());
    if (!R || !W || !Menus || Menus->AnyOpen() || IsLocked()) return false;
    Facing = YDir::FromYaw(R->GetActorRotation().Yaw);
    const FVector Here = R->GetActorLocation();
    const FVector Fwd = R->GetActorForwardVector().GetSafeNormal2D();
    const FIntPoint Ahead = FacingTile();
    const bool bCounter = W->TileAt(Ahead) == TEXT('=');
    auto Facing2D = [&](const FVector& At, float Reach)
    {
        const FVector To = (At - Here) * FVector(1, 1, 0);
        const float D = To.Size();
        if (D > Reach) return -1.f;
        if (D > 20.f && FVector::DotProduct(To / D, Fwd) < .55f) return -1.f;
        return D;
    };
    // People: within 1.2 m in front of Red, or across the counter he faces.
    APYRPerson* Best = nullptr;
    float BestD = 1e9f;
    for (const TWeakObjectPtr<APYRPerson>& P : People)
    {
        if (!P.IsValid() || !P->IsShown()) continue;
        float D = Facing2D(P->GetActorLocation(), 125.f);
        if (D < 0.f && bCounter && P->Tile == Ahead + YDir::Delta(Facing)) D = 200.f;
        if (D < 0.f && P->Tile == Ahead) D = 150.f;   // a walker mid-step on the tile Red faces still answers (the Game Boy's grid)
        if (D >= 0.f && D < BestD) { Best = P.Get(); BestD = D; }
    }
    if (Best)
    {
        if (!Best->Model.bProp && !Best->IsMoving()) Best->FaceToward(Here);
        UE_LOG(LogYellow, Display, TEXT("YELLOW_TALK %s:%s (%s)"), *Best->Map.ToString(), *Best->Id, *Best->Sprite.ToString());
        {
            TArray<FString> Lines;
            for (const FString& Id : Best->Texts) Lines.Add(W->GetData()->TextFor(Id));
            Menus->SetSpeaker(Best->SpeakerName(), Lines);
        }
        if (!PYRScripts::Talk(GetWorld(), Best))
        {
            const FString Text = W->GetData()->TextFor(Best->TextId);
            Menus->Say(Text.IsEmpty() ? FString(TEXT("...")) : Text);
        }
        return true;
    }
    for (const TWeakObjectPtr<APYRItemBall>& B : Balls)
    {
        if (!B.IsValid() || Facing2D(B->GetActorLocation(), 125.f) < 0.f) continue;
        APYRItemBall* Ball = B.Get();
        if (UPYRProgress* P = UPYRProgress::Get(GetWorld())) P->Take(Ball->Key());
        const FName Item = Ball->Item;
        Ball->Destroy();
        GiveItem(Item, 1, TEXT("found"));
        return true;
    }
    for (const TWeakObjectPtr<APYRSignpost>& S : Signs)
    {
        if (!S.IsValid() || Facing2D(S->GetActorLocation(), 130.f) < 0.f) continue;
        if (PYRScripts::ReadSign(GetWorld(), S.Get())) return true;
        const FString Text = W->GetData()->TextFor(S->TextId);
        Menus->Say(Text.IsEmpty() ? FString(TEXT("...")) : Text);
        return true;
    }
    if (TryHidden(KantoTile) || TryHidden(Ahead)) return true;
    return PYRScripts::UseTile(GetWorld(), Ahead);
}
