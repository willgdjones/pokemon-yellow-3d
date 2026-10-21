#include "Hud/PYRHud.h"

#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Partner/PYRPikachuFollower.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Rendering/DrawElements.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Stage/PYREncounters.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Hud/PYRPhoneLayout.h"
#include "Widgets/SLeafWidget.h"
#include "Yellow/YellowData.h"
#include "Yellow/YellowImages.h"

// The minimap's own widget: the picture's window clipped to the Game Boy screen, everyone else's sprite in it, and
// Red's sprite in the middle.
class SPYRMinimap : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SPYRMinimap) {}
    SLATE_END_ARGS()
    void Construct(const FArguments&) {}

    // A sprite to draw: its atlas row and frame, and its centre in Game Boy screen pixels.
    struct FDraw { int32 Row = 0; int32 Frame = 0; FVector2D Centre = FVector2D::ZeroVector; };

    UTexture2D* Tex = nullptr;
    UTexture2D* Sprite = nullptr;   // red_minimap.png (Red, without the people atlas)
    UTexture2D* People = nullptr;   // people_<PAL>.png
    int32 PeopleRows = 1;
    int32 RedRow = INDEX_NONE;
    TArray<FDraw> Others;
    FVector2D Window = FVector2D::ZeroVector;   // picture tiles
    float PxPerTile = 16.f;
    int32 Frame = 0;

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(UPYRHud::ScreenW, UPYRHud::ScreenH) * UPYRHud::Scale + FVector2D(12, 12);
    }

    // One 16x16 frame (Col, Row) of a sheet of Cols x Rows frames, centred on Centre (px in the inner geometry).
    static void DrawFrame(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Inner, UTexture2D* Sheet, int32 Cols, int32 Rows, int32 Col, int32 Row,
        const FVector2D& Centre)
    {
        const FVector2D SpriteSize(16.f * UPYRHud::Scale, 16.f * UPYRHud::Scale);
        FSlateBrush Brush;
        Brush.SetResourceObject(Sheet);
        Brush.ImageSize = FVector2D(Sheet->GetSizeX(), Sheet->GetSizeY());
        Brush.SetUVRegion(FBox2f(FVector2f(Col / float(Cols), Row / float(Rows)), FVector2f((Col + 1) / float(Cols), (Row + 1) / float(Rows))));
        FSlateDrawElement::MakeBox(Out, Layer, Inner.ToPaintGeometry(SpriteSize, FSlateLayoutTransform(Centre - SpriteSize * .5f)), &Brush, ESlateDrawEffect::None,
            FLinearColor::White);
    }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer,
        const FWidgetStyle& Style, bool bParentEnabled) const override
    {
        const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        const float S = UPYRHud::Scale;
        const FVector2D Screen(UPYRHud::ScreenW * S, UPYRHud::ScreenH * S);
        // The Game Boy's dark frame, then the screen.
        FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(Screen + FVector2D(12, 12), FSlateLayoutTransform()), White, ESlateDrawEffect::None,
            FLinearColor(.08f, .08f, .12f));
        const FGeometry Inner = Geo.MakeChild(Screen, FSlateLayoutTransform(FVector2D(6, 6)));
        FSlateDrawElement::MakeBox(Out, Layer + 1, Inner.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(.9f, .95f, .85f));
        Out.PushClip(FSlateClippingZone(Inner));
        if (Tex)
        {
            FSlateBrush Brush;
            Brush.SetResourceObject(Tex);
            Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
            Brush.DrawAs = ESlateBrushDrawType::Image;
            const float K = S * 16.f / FMath::Max(1.f, PxPerTile);   // screen px per picture px
            const FVector2D Size(Tex->GetSizeX() * K, Tex->GetSizeY() * K);
            const FVector2D Offset = -Window * PxPerTile * K;
            FSlateDrawElement::MakeBox(Out, Layer + 2, Inner.ToPaintGeometry(Size, FSlateLayoutTransform(Offset)), &Brush, ESlateDrawEffect::None, FLinearColor::White);
        }
        // Everyone else in the window, over the tiles as the Game Boy draws them.
        if (People)
            for (const FDraw& D : Others) DrawFrame(Out, Layer + 3, Inner, People, 8, PeopleRows, D.Frame, D.Row, D.Centre * S);
        Out.PopClip();
        // Red in the middle of the screen.
        const FVector2D Centre = Screen * .5f;
        if (People && RedRow != INDEX_NONE)
            DrawFrame(Out, Layer + 4, Inner, People, 8, PeopleRows, Frame, RedRow, Centre);
        else if (Sprite)
        {
            const int32 Cols = FMath::Max(1, Sprite->GetSizeX() / 16), Rows = FMath::Max(1, Sprite->GetSizeY() / 16);
            DrawFrame(Out, Layer + 4, Inner, Sprite, Cols, Rows, Frame % Cols, FMath::Min(Frame / Cols, Rows - 1), Centre);
        }
        else
        {
            const FVector2D SpriteSize(16.f * S, 16.f * S);
            FSlateDrawElement::MakeBox(Out, Layer + 4, Inner.ToPaintGeometry(SpriteSize * .7f, FSlateLayoutTransform(Centre - SpriteSize * .35f)), White,
                ESlateDrawEffect::None, FLinearColor(.85f, .15f, .12f));
        }
        return Layer + 5;
    }
};

UPYRHud* UPYRHud::Get(const UObject* Ctx)
{
    UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
    return W ? W->GetSubsystem<UPYRHud>() : nullptr;
}

void UPYRHud::LoadOutdoorInfo()
{
    bOutdoorLoaded = true;
    FString Text;
    const FString Path = FPaths::Combine(UYellowData::DefaultRoot(), TEXT("minimap/kanto_outdoor.json"));
    if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogYellow, Error, TEXT("YellowData missing: %s"), *Path); return; }
    TSharedPtr<FJsonObject> Json;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) return;
    double V = 16;
    if (Json->TryGetNumberField(TEXT("px_per_tile"), V)) PxPerTile = float(V);
    const TSharedPtr<FJsonObject>* O = nullptr;
    if (Json->TryGetObjectField(TEXT("origin_px"), O) && O)
    {
        double X = 0, Y = 0;
        (*O)->TryGetNumberField(TEXT("x"), X);
        (*O)->TryGetNumberField(TEXT("y"), Y);
        OriginPx = FVector2D(X, Y);
    }
}

void UPYRHud::LoadRoomRing()
{
    RoomRing = 0;
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(UYellowData::DefaultRoot(), TEXT("minimap/ringed.json")))) return;
    TSharedPtr<FJsonObject> Json;
    double Ring = 0;
    if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid() && Json->TryGetNumberField(TEXT("ring"), Ring)) RoomRing = FMath::Max(0, int32(Ring));
}

void UPYRHud::LoadAtlas()
{
    FString Text;
    const FString Path = FPaths::Combine(UYellowData::DefaultRoot(), TEXT("sprites/people.json"));
    if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogYellow, Error, TEXT("YellowData missing: %s"), *Path); return; }
    TSharedPtr<FJsonObject> Json;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) return;
    const TSharedPtr<FJsonObject>* Rows = nullptr;
    if (!Json->TryGetObjectField(TEXT("rows"), Rows) || !Rows) return;
    for (const auto& Pair : (*Rows)->Values)
    {
        double Row = 0;
        if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Row)) AtlasRows.Add(FName(*Pair.Key), int32(Row));
    }
}

void UPYRHud::Start()
{
    if (bStarted) return;
    bStarted = true;
    Widget = SNew(SPYRMinimap);
    Canvas = SNew(SConstraintCanvas);
    Place();
    Root = Canvas;
    if (UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
    {
        VC->AddViewportWidgetContent(Canvas.ToSharedRef(), 40);
        bAdded = true;
    }
    if (UYellowImages* Images = UYellowImages::Get(this)) Sprite = Images->Load(TEXT("sprites/red_minimap.png"));
    LoadAtlas();
    if (Widget)
    {
        Widget->Sprite = Sprite;
        Widget->PeopleRows = FMath::Max(1, AtlasRows.Num());
        Widget->RedRow = AtlasRow(TEXT("SPRITE_RED"));
    }
}

void UPYRHud::Deinitialize()
{
    if (bAdded && Root.IsValid())
        if (UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr) VC->RemoveViewportWidgetContent(Root.ToSharedRef());
    Super::Deinitialize();
}

TSharedPtr<SWidget> UPYRHud::GetWidget() const { return Widget; }

void UPYRHud::Place()
{
    if (!Canvas.IsValid() || !Widget.IsValid()) return;
    // Desktop: the bottom-right corner, 20 px in. Phone: the top right of the safe band, below the page's top bar and
    // above its button cluster.
    const bool bPhone = PYRPhoneLayout::IsPhone();
    const FVector2f View = PYRPhoneLayout::ViewportSize();
    const FPYRSafeArea S = PYRPhoneLayout::Safe();
    const FString Key = bPhone && View.X > 0 ? FString::Printf(TEXT("Phone %.0fx%.0f %.1f"), View.X, View.Y, S.Top) : FString(TEXT("Desktop"));
    if (Key == PlaceKey) return;
    PlaceKey = Key;
    Canvas->ClearChildren();
    if (Key == TEXT("Desktop"))
    {
        Anchor = FVector2D(1.f, 1.f);
        Canvas->AddSlot().Anchors(FAnchors(1.f, 1.f)).Alignment(FVector2D(1.f, 1.f)).AutoSize(true).Offset(FMargin(-20.f, -20.f, 0.f, 0.f))[Widget.ToSharedRef()];
        return;
    }
    Anchor = FVector2D((View.X - FPYRSafeArea::Margin) / View.X, S.Top / View.Y);
    Canvas->AddSlot().Anchors(FAnchors(Anchor.X, Anchor.Y)).Alignment(FVector2D(1.f, 0.f)).AutoSize(true).Offset(FMargin(0.f, 0.f, 0.f, 0.f))[Widget.ToSharedRef()];
}

void UPYRHud::Tick(float DeltaTime)
{
    // m4u: the phone layout's log line and the in-game picture of the page's controls (tests and shots).
    PYRPhoneLayout::TickGuide(GetWorld());
    if (!bStarted) return;
    Place();
    const UPYRField* Field = UPYRField::Get(this);
    const AYellowWorld* W = Field ? Field->GetYellowWorld() : nullptr;
    const FYMap* M = Field ? Field->GetMap() : nullptr;
    APokemonYellowReimagineCharacter* Red = APokemonYellowReimagineCharacter::Find(this);
    const UPYRMenus* Menus = UPYRMenus::Get(this);
    const UPYREncounters* Enc = UPYREncounters::Get(this);
    // Hidden in battle (from the flash to the return) and while a menu is up.
    // Hidden in Oak's opening too (the intro screen is open under its text).
    const bool bShow = W && M && Red && !(Enc && Enc->IsBusy()) && !(Menus && (Menus->AnyMenu() || Menus->IsOpen(TEXT("intro"))));
    bShown = bShow;
    if (Root.IsValid()) Root->SetVisibility(bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
    if (!bShow) return;
    // The picture: all of outdoor Kanto stitched (with what lies past the edges), or the room ringed by its border block
    // (minimap/ringed/, as the Game Boy draws past a room's walls), else the room's own.
    const bool bOutdoor = M->bOutdoor;
    if (RoomRing < 0) LoadRoomRing();
    const FString Ringed = FString::Printf(TEXT("minimap/ringed/%s.png"), *M->Id.ToString());
    const bool bRinged = !bOutdoor && RoomRing > 0 && FPaths::FileExists(FPaths::Combine(UYellowData::DefaultRoot(), Ringed));
    const FString Want = bOutdoor ? FString(TEXT("minimap/kanto_outdoor.png")) : bRinged ? Ringed : FString::Printf(TEXT("minimap/%s.png"), *M->Id.ToString());
    PictureRing = bRinged ? RoomRing : 0;
    if (Want != Image)
    {
        Image = Want;
        if (UYellowImages* Images = UYellowImages::Get(this)) Texture = Images->Load(Image);
        if (bOutdoor && !bOutdoorLoaded) LoadOutdoorInfo();
    }
    // The people atlas in the map's palette (the Game Boy Color's object palettes follow the map's).
    const FString WantPeople = M->Palette.IsEmpty() ? FString() : FString::Printf(TEXT("sprites/people_%s.png"), *M->Palette);
    if (WantPeople != PeopleImage)
    {
        PeopleImage = WantPeople;
        UYellowImages* Images = UYellowImages::Get(this);
        People = Images && !PeopleImage.IsEmpty() && AtlasRows.Num() ? Images->Load(PeopleImage) : nullptr;
    }
    // Red's continuous position in the picture's tiles; the window keeps him in the middle.
    const FVector Local = (Red->GetActorLocation() - W->GetOffset()) / YTile::Size;
    FVector2D Pos(Local.X, Local.Y);
    if (bOutdoor) Pos += OriginPx / FMath::Max(1.f, PxPerTile);
    else Pos += FVector2D(PictureRing - M->Origin.X, PictureRing - M->Origin.Y);
    Window = Pos - FVector2D(5.f, 4.5f);
    // His sprite: facing (down, up, left, right) and the step frame while he moves.
    const bool bMoving = Red->GetVelocity().Size2D() > 30.f;
    StepClock = bMoving ? StepClock + DeltaTime : 0.f;
    const int32 Facing = Field->Facing == EYDir::Up ? 1 : Field->Facing == EYDir::Left ? 2 : Field->Facing == EYDir::Right ? 3 : 0;
    Frame = Facing * 2 + (bMoving && FMath::Frac(StepClock * 3.75f) > .5f ? 1 : 0);
    GatherMarkers(W, M, DeltaTime);
    if (Widget)
    {
        Widget->Tex = Texture;
        Widget->Window = Window;
        Widget->PxPerTile = bOutdoor ? PxPerTile : 16.f;
        Widget->Frame = Frame;
        Widget->People = People;
        Widget->Others.Reset();
        for (const FPYRMinimapMarker& Mk : Markers)
            if (!Mk.bRed && Mk.Row != INDEX_NONE) Widget->Others.Add({Mk.Row, Mk.Frame, (Mk.Tile - Window) * 16.f});
    }
}

void UPYRHud::GatherMarkers(const AYellowWorld* W, const FYMap* M, float DeltaTime)
{
    Markers.Reset();
    const UPYRField* Field = UPYRField::Get(this);
    const FYellowDataSet* Data = W ? W->GetData() : nullptr;
    if (!Field || !Data) return;
    const bool bOutdoor = M->bOutdoor;
    // Picture tiles of a world point (the same mapping as Red's).
    auto ToPicture = [&](const FVector& At)
    {
        const FVector Local = (At - W->GetOffset()) / YTile::Size;
        FVector2D Pos(Local.X, Local.Y);
        if (bOutdoor) Pos += OriginPx / FMath::Max(1.f, PxPerTile);
        else Pos += FVector2D(PictureRing - M->Origin.X, PictureRing - M->Origin.Y);
        return Pos;
    };
    // In the window, with a tile's margin so sprites slide in and out at the edges.
    auto InWindow = [&](const FVector2D& Pos)
    {
        return Pos.X > Window.X - 1.f && Pos.Y > Window.Y - 1.f && Pos.X < Window.X + ScreenW / 16.f + 1.f && Pos.Y < Window.Y + ScreenH / 16.f + 1.f;
    };
    // Outdoors every outdoor map shares the picture (connections are seamless); indoors only the room's own people.
    auto SameSpace = [&](FName Map)
    {
        if (Map == M->Id) return true;
        const FYMap* Other = bOutdoor ? Data->Map(Map) : nullptr;
        return Other && Other->bOutdoor;
    };
    auto FacingIndex = [](EYDir D) { return D == EYDir::Up ? 1 : D == EYDir::Left ? 2 : D == EYDir::Right ? 3 : 0; };
    // The step frame runs while the actor walks, at the Game Boy's walking cadence. A walk of tile-by-tile steps
    // (people, Pikachu) pauses for a moment on each tile, so it counts as walking until it has stood for 0.2 s.
    const double Time = GetWorld()->GetRealTimeSeconds();
    auto Step = [&](AActor* A)
    {
        const FVector Now = A->GetActorLocation();
        const FVector* Was = LastAt.Find(A);
        if (Was && FVector::Dist2D(*Was, Now) > FMath::Max(.05f, 30.f * DeltaTime)) LastMoved.Add(A, Time);   // faster than 30 cm/s
        LastAt.Add(A, Now);
        const double* Moved = LastMoved.Find(A);
        const bool bMoving = Moved && Time - *Moved < .2;
        float& Clock = Clocks.FindOrAdd(A);
        Clock = bMoving ? Clock + DeltaTime : 0.f;
        return bMoving && FMath::Frac(Clock * 3.75f) > .5f ? 1 : 0;
    };
    auto Add = [&](AActor* A, FName SpriteId, EYDir Facing, bool bStill)
    {
        const FVector2D Pos = ToPicture(A->GetActorLocation());
        const int32 StepFrame = Step(A);
        if (!InWindow(Pos)) return;
        FPYRMinimapMarker Mk;
        Mk.Actor = A;
        Mk.Sprite = SpriteId;
        Mk.Row = AtlasRow(SpriteId);
        Mk.Frame = bStill ? 0 : FacingIndex(Facing) * 2 + StepFrame;
        Mk.Tile = Pos;
        Mk.Kanto = W->ToKanto(A->GetActorLocation());
        Markers.Add(Mk);
    };
    for (const TWeakObjectPtr<APYRPerson>& P : Field->People)
    {
        if (!P.IsValid() || !P->IsShown() || P->IsHidden() || !SameSpace(P->Map)) continue;
        Add(P.Get(), P->Sprite, P->Facing, false);
    }
    for (const TWeakObjectPtr<APYRItemBall>& B : Field->Balls)
        if (B.IsValid() && !B->IsHidden() && SameSpace(B->Map)) Add(B.Get(), TEXT("SPRITE_POKE_BALL"), EYDir::Down, true);
    // Pikachu once it is out (not before Red has it, nor while a battle hides it).
    if (APYRPikachuFollower* Pika = APYRPikachuFollower::Find(this))
        if (!Pika->IsHidden() && Pika->GetMode() != EPYRPikachuMode::Waiting)
            if (const FYMap* PikaMap = W->MapAtKanto(W->ToKanto(Pika->GetActorLocation())))
                if (SameSpace(PikaMap->Id)) Add(Pika, TEXT("SPRITE_PIKACHU"), YDir::FromYaw(Pika->GetActorRotation().Yaw), false);
    // Red last (the widget draws him in the middle).
    if (APokemonYellowReimagineCharacter* Red = APokemonYellowReimagineCharacter::Find(this))
    {
        FPYRMinimapMarker Mk;
        Mk.Actor = Red;
        Mk.Sprite = TEXT("SPRITE_RED");
        Mk.Row = AtlasRow(Mk.Sprite);
        Mk.Frame = Frame;
        Mk.Tile = Window + FVector2D(5.f, 4.5f);
        Mk.Kanto = W->ToKanto(Red->GetActorLocation());
        Mk.bRed = true;
        Markers.Add(Mk);
    }
}

