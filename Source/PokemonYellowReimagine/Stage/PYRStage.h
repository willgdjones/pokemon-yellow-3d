// M4: the classic 3D battle stages, built once far below the world (Z -30000) at level start: two raised oval
// platforms (the foe's far and higher, Red's near) on a painted backdrop. The fight itself is m3's real-time battle
// in a ring laid over the stage (walls, and for wild battles a worn gap at the edge on Red's side).
// Variants: Grass (routes), Forest (Viridian Forest), Cave (Mt. Moon), Indoor (the lab and houses), PewterGym (rock)
// and CeruleanGym (m3's pool: water tagged PYR.Water and floating platforms tagged PYR.Platform, so the swimmers dive).
// Red stands at the stage's west edge; the foe's platform is east of the centre; the camera looks east.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/PYRKit.h"
#include "PYRStage.generated.h"

UENUM()
enum class EPYRStageKind : uint8 { Grass, Forest, Cave, Indoor, PewterGym, CeruleanGym, Count UMETA(Hidden) };

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRBattleStage : public AActor
{
    GENERATED_BODY()
public:
    APYRBattleStage();
    static constexpr float StageZ = -30000.f;
    static FVector CentreFor(EPYRStageKind Kind);
    static const TCHAR* Name(EPYRStageKind Kind);

    void Build(EPYRStageKind InKind);
    EPYRStageKind Kind = EPYRStageKind::Grass;
    FVector Centre = FVector::ZeroVector;
    float Radius = 900.f;          // the ring over the stage
    // Where Red stands (the west edge), and the tops of the two platforms.
    FVector RedSpot() const;
    FVector NearPlatform() const;
    FVector FarPlatform() const;
    float FarTopZ = 0.f, NearTopZ = 0.f;
    // Cave: the height fliers stay under (0: open sky).
    float HoverLimit = 0.f;
    bool IsGym() const { return Kind == EPYRStageKind::PewterGym || Kind == EPYRStageKind::CeruleanGym; }
    // The lab and the two gyms are indoors: their stage stands in a closed hall (m4s). HallHalf: the hall's half size
    // in X and Y and its ceiling's height over the stage floor (zero for the outdoor stages).
    static bool IsIndoor(EPYRStageKind Kind);
    FVector HallHalf = FVector::ZeroVector;
    int32 Pieces = 0;

private:
    // m4v: every piece is one of the anime kit's surfaces (World/PYRKit), tinted to Colour.
    UStaticMeshComponent* Piece(const TCHAR* Shape, const FVector& At, const FVector& Size, EPYRKit Kit, const FLinearColor& Colour, bool bCollide,
        const FRotator& Rot = FRotator::ZeroRotator, float TilingCm = 0.f);
    void BuildPlatforms(const FLinearColor& Top, const FLinearColor& Rim);
    void BuildPool();
    void BuildHall(float HalfX, float HalfY, float Height, const FLinearColor& WallColour, EPYRKit WallKit, const FLinearColor& CeilingColour,
        float TilingCm = 0.f);
    FRandomStream Rng{0x57a9e};
};
