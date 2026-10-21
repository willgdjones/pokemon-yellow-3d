// Pokemon data for the live battles, taken from Game Boy Pokemon Yellow (pret/pokeyellow: data/pokemon/base_stats,
// data/pokemon/evos_moves.asm, data/moves/moves.asm, data/trainers/special_moves.asm): species (base stats, types,
// growth rate, catch rate, experience yield), level-up learnsets, evolutions, moves (power, accuracy, type, effect)
// and the Gen 1 type chart, stat formula, damage formula, stat stages and catch formula. Plain C++ tables (no assets)
// so tests, the HUD and the fights all read the same numbers.
#pragma once

#include "CoreMinimal.h"
#include "PYRSpecies.generated.h"

// The fifteen Gen 1 types.
enum class EPYRType : uint8
{
    None, Normal, Fire, Water, Electric, Grass, Ice, Fighting, Poison, Ground, Flying, Psychic, Bug, Rock, Ghost, Dragon,
    Count
};

// How a Pokemon moves and dodges in the ring. Swimmers (m3) swim at 6 m/s and dive in tagged water, and flop on land.
enum class EPYRBody : uint8 { Runner, Flier, Crawler, Giant, Swimmer };

// Cooldown class: quick (about 1 s), tactical (3-8 s) or big (12-20 s).
enum class EPYRMoveTier : uint8 { Quick, Tactical, Big };

// Gen 1 experience groups.
enum class EPYRGrowth : uint8 { MediumFast, MediumSlow, Fast, Slow };

// Stats that stages apply to (Gen 1: -6..+6, accuracy and evasion use the same table).
enum class EPYRStat : uint8 { Attack, Defence, Speed, Special, Accuracy, Evasion, Count };

// Non-volatile status kept on the party Pokemon (Gen 1: paralysis, poison, burn and sleep last until healed; a
// sleeping Pokemon wakes on its own in battle).
UENUM(BlueprintType)
enum class EPYRStatus : uint8 { None, Paralysis, Poison, Sleep, Burn };

// How a move reaches its target (see APYRAttack).
enum class EPYRMoveShape : uint8
{
    Projectile,  // A bolt, needle, thread or psychic wave flying from the user.
    Dash,        // The user lunges and strikes what it touches.
    Dive,        // Flier melee: a shadow grows at the landing point, then the user swoops onto it.
    Cone,        // A cone in front of the user (Gust, Sand-Attack).
    Circle,      // A blow falling at a point (Rock Throw): a ground shadow first.
    Sweep,       // A 180-degree arc around the user at a height band (Onix's Tackle, a tail sweep).
    Shockwave,   // An expanding ground ring from the user (Bide's release).
    Ring,        // An expanding ring pulse that hits whatever it passes (Screech, Thunder Wave).
    Debuff,      // An instant effect on the foe in range (Growl, Tail Whip, Leer).
    Self,        // A stat change on the user (Harden, Defense Curl, Double Team).
    Bide,        // Glows orange for 3 s storing damage, then releases double as a shockwave ring.
    Burrow,      // Dig: the user goes underground (can't be hit), a mound runs to the target, then it erupts there.
};

enum class EPYREffect : uint8
{
    None,
    FoeStat,     // Lowers the target's Stat by Stages (Growl, Tail Whip, Leer, Screech, Sand-Attack, String Shot);
                 // on a damaging move it is a side effect with EffectChance (Bubble, Bubblebeam, Acid).
    SelfStat,    // Raises the user's Stat by Stages (Harden, Defense Curl, Double Team, Withdraw, Growth, Minimize).
    Paralyse,    // EffectChance to paralyse (Thunder Wave 1, Thundershock 0.1, Stun Spore, Glare).
    Poison,      // EffectChance to poison (Poison Sting 0.2, Smog 0.4, Poison Powder and Poison Gas 1).
    Confuse,     // EffectChance to confuse (Confusion 0.1; Supersonic and Confuse Ray 1).
    Flinch,      // EffectChance to make the target flinch (Bite, Hyper Fang, Low Kick, Headbutt).
    Bide,
    Sleep,       // Puts the target to sleep (Sing, Hypnosis, Sleep Powder, Spore).
    Burn,        // EffectChance to burn (Ember 0.1).
    Drain,       // Heals the user half the damage dealt (Absorb, Leech Life).
    Heal,        // Heals the user half its max HP (Recover).
    Trap,        // Squeezes the target: it can't move or act while bound, and takes a second squeeze (Wrap, Bind, Clamp).
    LeechSeed,   // Seeds the target: 1/16 of its max HP flows to the user every 2 s (not Grass types).
    Disable,     // Locks one of the target's moves for a while.
    FocusEnergy, // Yellow's Focus Energy (with Gen 1's bug: it quarters the critical-hit rate).
    Rage,        // For a while after, every hit taken raises the user's Attack.
    Mirror,      // Mirror Move: uses the foe's last move.
    Splash,      // Nothing happens.
    Explode,     // Self-Destruct: the user faints; Gen 1 halves the target's Defense for the hit.
    PayDay,      // Scatters coins: 2 x level in money after a battle that isn't lost.
    Whirlwind,   // Blows a wild foe away (the fight ends); fails in a trainer battle.
};

struct FPYRMoveInfo
{
    FName Id;
    FText Name;
    EPYRType Type = EPYRType::Normal;
    EPYRMoveTier Tier = EPYRMoveTier::Quick;
    EPYRMoveShape Shape = EPYRMoveShape::Projectile;
    float Power = 0;          // Yellow power; 0 for status moves.
    float Accuracy = 100;     // Yellow accuracy in percent; 0 means it never misses (self moves, Bide).
    int32 MinHits = 1, MaxHits = 1;  // Double Kick 2, Fury Attack 2-5.
    bool bHighCrit = false;   // Karate Chop, Slash.
    float FixedDamage = 0;    // Sonic Boom: always 20.
    float Cooldown = 1;       // Seconds.
    float Range = 1200;       // cm: projectile travel, dash length, cone length, sweep radius, ring radius...
    float Radius = 60;        // cm: projectile or circle radius; cone half-angle in degrees for cones.
    float TellSeconds = .6f;  // Enemy use: warning before the hit window (0.4-1.2 s).
    float HitSeconds = .25f;  // Length of the hit window.
    EPYREffect Effect = EPYREffect::None;
    float EffectChance = 1;
    EPYRStat Stat = EPYRStat::Attack;
    int32 Stages = 0;
    float HeightMin = -1e6f, HeightMax = 1e6f;  // Band above the user's floor that the hit covers.
    int32 Pp = 0;             // m4: Yellow's PP (moves.json), for the summary screen.
    FString GbName;           // m4: Game Boy caps ("THUNDERSHOCK").
    // m1 name for the cooldown class, kept for the loadout panel.
    EPYRMoveTier TierOf() const { return Tier; }
};

struct FPYRLearn { int32 Level; FName Move; };

// m4: how a species evolves (species.json evolutions).
enum class EPYREvolveKind : uint8 { Level, Item, Trade };
struct FPYREvolution
{
    EPYREvolveKind Kind = EPYREvolveKind::Level;
    int32 Level = 0;
    FName Item;          // YKey::Item ("moonstone", "thunderstone").
    FName Into;
};

struct FPYRSpeciesInfo
{
    FName Id;                 // Lower case with '-', e.g. "nidoran-f".
    FText Name;
    int32 Dex = 0;
    EPYRBody Body = EPYRBody::Runner;
    EPYRType Type1 = EPYRType::Normal, Type2 = EPYRType::None;
    int32 BaseHp = 50, BaseAttack = 50, BaseDefence = 50, BaseSpeed = 50, BaseSpecial = 50;
    EPYRGrowth Growth = EPYRGrowth::MediumFast;
    int32 BaseExp = 60;
    int32 CatchRate = 255;
    float HeightM = .5f;      // Pokedex height.
    FLinearColor Colour = FLinearColor::White, Accent = FLinearColor::Black;
    FText Habitat;
    TArray<FPYRLearn> Learnset;
    int32 EvolveLevel = 0;
    FName EvolvesInto;
    // m4 (species.json): every evolution, the TMs (1-50) and HMs (51-55) it can learn, and its Pokedex entry.
    TArray<FPYREvolution> Evolutions;
    TArray<int32> Tms;
    FText Category;           // "MOUSE"
    float WeightKg = 0.f;
    FText DexText;
    FString GbName;           // Game Boy caps ("PIKACHU").

    // ---- Derived sizes and speeds
    // Outside battle: 1.5x the Pokedex height below 1 m (at least 45 cm); true size from 1 m up.
    float FieldHeightCm() const;
    // In battle: at least 1.2 m for everything but giants, which stay true size.
    float BattleHeightCm() const;
    float MoveSpeed() const;  // cm/s: runner 700, flier 550, crawler 350, giant 220, swimmer 600 in water.
    float LandSpeed() const;  // cm/s on dry ground: swimmers flop at 150, the rest as MoveSpeed.
    float TurnRate() const;   // deg/s: giants turn slowly.
    // Battle-size capsule (crawlers stay low so wide sweeps pass over them).
    float CapsuleRadius() const;
    float CapsuleHalfHeight() const;
    float HoverHeight() const;  // Fliers: capsule centre above the floor in battle (150 cm).
};

// A Pokemon to put in a fight: Species at Level with Moves (empty: Yellow's last four learned by that level).
USTRUCT(BlueprintType)
struct FPYRMonSpec
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Species;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Level = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> Moves;
};

namespace PYRSpecies
{
    POKEMONYELLOWREIMAGINE_API const FPYRSpeciesInfo* Find(FName Id);
    POKEMONYELLOWREIMAGINE_API const FPYRMoveInfo* FindMove(FName Id);
    // The moves a Spec fights with: its own, a Yellow trainer's special moveset (Brock's Geodude and Onix), or the
    // last four the species learned by that level.
    POKEMONYELLOWREIMAGINE_API TArray<FName> MovesFor(const FPYRMonSpec& Spec);
    // Mesh paths: /Game/Pokemon/<id>/SK_<id> and SM_<id> with '-' as '_'.
    POKEMONYELLOWREIMAGINE_API FString AssetId(FName Species);
}

namespace PYRData
{
    POKEMONYELLOWREIMAGINE_API const FPYRSpeciesInfo* Species(FName Id);
    POKEMONYELLOWREIMAGINE_API const FPYRMoveInfo* Move(FName Id);
    POKEMONYELLOWREIMAGINE_API const TArray<FName>& AllSpecies();
    POKEMONYELLOWREIMAGINE_API const TArray<FName>& AllMoves();
    // Gen 1 chart: attack type against one defending type (0, .5, 1 or 2), and against a species' two types.
    POKEMONYELLOWREIMAGINE_API float TypeFactor(EPYRType Attack, EPYRType Defend);
    POKEMONYELLOWREIMAGINE_API float Effectiveness(EPYRType Attack, const FPYRSpeciesInfo& Defender);
    POKEMONYELLOWREIMAGINE_API FText TypeName(EPYRType Type);
    POKEMONYELLOWREIMAGINE_API FLinearColor TypeColour(EPYRType Type);
    POKEMONYELLOWREIMAGINE_API FText BodyName(EPYRBody Body);
    POKEMONYELLOWREIMAGINE_API FText TierName(EPYRMoveTier Tier);
    POKEMONYELLOWREIMAGINE_API FText StatName(EPYRStat Stat);
    POKEMONYELLOWREIMAGINE_API FLinearColor StatColour(EPYRStat Stat);
    // Gen 1 stats with the fixed trainer DVs (Attack 9, the rest 8) and no stat experience.
    POKEMONYELLOWREIMAGINE_API float MaxHp(const FPYRSpeciesInfo& S, int32 Level);
    POKEMONYELLOWREIMAGINE_API float StatAt(const FPYRSpeciesInfo& S, EPYRStat Stat, int32 Level);
    // Gen 1 stage multipliers: 25/100 at -6 .. 400/100 at +6.
    POKEMONYELLOWREIMAGINE_API float StageMultiplier(int32 Stage);
    // Total experience to reach Level in the species' growth group.
    POKEMONYELLOWREIMAGINE_API int32 XpForLevel(EPYRGrowth Growth, int32 Level);
    POKEMONYELLOWREIMAGINE_API int32 XpForLevel(FName Species, int32 Level);
    // Gen 1: base yield x level / 7, x1.5 for trainers, split between the Pokemon that fought.
    POKEMONYELLOWREIMAGINE_API int32 XpYield(FName Foe, int32 FoeLevel, bool bTrainer, int32 Participants);
    // Moves the species learns in (FromLevel, ToLevel].
    POKEMONYELLOWREIMAGINE_API TArray<FName> MovesLearned(FName Species, int32 FromLevel, int32 ToLevel);
    // The last four moves learned by Level (Yellow's default moveset).
    POKEMONYELLOWREIMAGINE_API TArray<FName> DefaultMoves(FName Species, int32 Level);
    // The species this one evolves into at Level (NAME_None when it doesn't).
    POKEMONYELLOWREIMAGINE_API FName EvolutionAt(FName Species, int32 Level);
    // Gen 1 damage: floor(floor(floor(2L/5+2) * Power * A / D) / 50) + 2, then STAB x1.5 and each type factor.
    // Random is the 217..255 roll as a fraction (1 for the top roll). 0 when Power or Factor is 0.
    POKEMONYELLOWREIMAGINE_API float Damage(int32 Level, float Attack, float Defence, float Power, float Stab, float Factor, float Random = 1.f);
    // Gen 1 Poke Ball catch chance for a foe with Hp of MaxHp and a status (sleep +25, paralysis/poison/burn +12 of 256).
    POKEMONYELLOWREIMAGINE_API float CatchChance(int32 CatchRate, float Hp, float MaxHp, EPYRStatus Status);
    // The highest level whose Yellow learnset this table carries for the species (25 for most; see PYRSpecies.cpp).
    POKEMONYELLOWREIMAGINE_API int32 LearnsetCap(FName Species);
    // The species added in m3 (every Pokemon met from Pewter to Cerulean in Yellow that m2 did not have).
    POKEMONYELLOWREIMAGINE_API const TArray<FName>& M3Species();
    // m4: the tables as Yellow's JSON extends them (Yellow/YellowTables.cpp): add or change a species or move.
    POKEMONYELLOWREIMAGINE_API FPYRSpeciesInfo& MutableSpecies(FName Id);
    POKEMONYELLOWREIMAGINE_API FPYRMoveInfo& MutableMove(FName Id);
    // Yellow's experience table for a growth group (index = level, at least 101 entries).
    POKEMONYELLOWREIMAGINE_API void SetXpTable(EPYRGrowth Growth, const TArray<int32>& ByLevel);
    // Every known species in Pokedex order.
    POKEMONYELLOWREIMAGINE_API const TArray<FName>& DexOrder();
    // The species an item (a Moon Stone) turns this one into, or NAME_None.
    POKEMONYELLOWREIMAGINE_API FName EvolutionByItem(FName Species, FName ItemKey);
    // True when the species can learn TM/HM number N (HMs are 51-55).
    POKEMONYELLOWREIMAGINE_API bool CanLearnTm(FName Species, int32 TmNumber);

    // Ids used in code.
    extern POKEMONYELLOWREIMAGINE_API const FName Pikachu, Eevee, Pidgey, Pidgeotto, Rattata, Spearow, NidoranF, NidoranM, Mankey,
        Caterpie, Metapod, Butterfree, Weedle, Kakuna, Geodude, Onix, Diglett, Sandshrew;
    extern POKEMONYELLOWREIMAGINE_API const FName Zubat, Clefairy, Paras, Parasect, Jigglypuff, Ekans, Koffing, Meowth, Staryu, Starmie,
        Goldeen, Horsea, Shellder, Oddish, Bellsprout, Venonat, Magikarp, Charmander, Bulbasaur, Slowpoke, Machop, Magnemite, Voltorb,
        Grimer, Drowzee;
    extern POKEMONYELLOWREIMAGINE_API const FName ThunderShock, Growl, TailWhip, ThunderWave, QuickAttack, DoubleTeam, Tackle,
        SandAttack, Gust, Scratch, PoisonSting, Peck, Bite, Leer, StringShot, Harden, DefenseCurl, Screech, Bide, DoubleKick,
        Confusion, HyperFang, FuryAttack, HornAttack, LowKick, KarateChop, PoisonPowder, RockThrow;
    extern POKEMONYELLOWREIMAGINE_API const FName Absorb, Acid, Bind, Bubble, BubbleBeam, Clamp, ConfuseRay, Dig, Disable, DoubleSlap,
        Ember, FocusEnergy, FurySwipes, Glare, Growth, Headbutt, Hypnosis, LeechLife, LeechSeed, Minimize, MirrorMove, PayDay, PoisonGas,
        Pound, Rage, Recover, SelfDestruct, Sing, Slam, Slash, SleepPowder, Smog, Smokescreen, SonicBoom, Splash, Spore, StunSpore,
        Supersonic, Swift, VineWhip, WaterGun, Whirlwind, Withdraw, Wrap;
}
