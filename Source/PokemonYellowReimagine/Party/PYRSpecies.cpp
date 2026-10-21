#include "PYRSpecies.h"

#define LOCTEXT_NAMESPACE "PYRSpecies"

namespace PYRData
{
const FName Pikachu(TEXT("pikachu")), Eevee(TEXT("eevee")), Pidgey(TEXT("pidgey")), Pidgeotto(TEXT("pidgeotto")), Rattata(TEXT("rattata")),
    Spearow(TEXT("spearow")), NidoranF(TEXT("nidoran-f")), NidoranM(TEXT("nidoran-m")), Mankey(TEXT("mankey")), Caterpie(TEXT("caterpie")),
    Metapod(TEXT("metapod")), Butterfree(TEXT("butterfree")), Weedle(TEXT("weedle")), Kakuna(TEXT("kakuna")), Geodude(TEXT("geodude")),
    Onix(TEXT("onix")), Diglett(TEXT("diglett")), Sandshrew(TEXT("sandshrew"));
const FName Zubat(TEXT("zubat")), Clefairy(TEXT("clefairy")), Paras(TEXT("paras")), Parasect(TEXT("parasect")), Jigglypuff(TEXT("jigglypuff")),
    Ekans(TEXT("ekans")), Koffing(TEXT("koffing")), Meowth(TEXT("meowth")), Staryu(TEXT("staryu")), Starmie(TEXT("starmie")),
    Goldeen(TEXT("goldeen")), Horsea(TEXT("horsea")), Shellder(TEXT("shellder")), Oddish(TEXT("oddish")), Bellsprout(TEXT("bellsprout")),
    Venonat(TEXT("venonat")), Magikarp(TEXT("magikarp")), Charmander(TEXT("charmander")), Bulbasaur(TEXT("bulbasaur")),
    Slowpoke(TEXT("slowpoke")), Machop(TEXT("machop")), Magnemite(TEXT("magnemite")), Voltorb(TEXT("voltorb")), Grimer(TEXT("grimer")),
    Drowzee(TEXT("drowzee"));
const FName ThunderShock(TEXT("thundershock")), Growl(TEXT("growl")), TailWhip(TEXT("tail-whip")), ThunderWave(TEXT("thunder-wave")),
    QuickAttack(TEXT("quick-attack")), DoubleTeam(TEXT("double-team")), Tackle(TEXT("tackle")), SandAttack(TEXT("sand-attack")),
    Gust(TEXT("gust")), Scratch(TEXT("scratch")), PoisonSting(TEXT("poison-sting")), Peck(TEXT("peck")), Bite(TEXT("bite")),
    Leer(TEXT("leer")), StringShot(TEXT("string-shot")), Harden(TEXT("harden")), DefenseCurl(TEXT("defense-curl")),
    Screech(TEXT("screech")), Bide(TEXT("bide")), DoubleKick(TEXT("double-kick")), Confusion(TEXT("confusion")),
    HyperFang(TEXT("hyper-fang")), FuryAttack(TEXT("fury-attack")), HornAttack(TEXT("horn-attack")), LowKick(TEXT("low-kick")),
    KarateChop(TEXT("karate-chop")), PoisonPowder(TEXT("poison-powder")), RockThrow(TEXT("rock-throw"));
const FName Absorb(TEXT("absorb")), Acid(TEXT("acid")), Bind(TEXT("bind")), Bubble(TEXT("bubble")), BubbleBeam(TEXT("bubblebeam")),
    Clamp(TEXT("clamp")), ConfuseRay(TEXT("confuse-ray")), Dig(TEXT("dig")), Disable(TEXT("disable")), DoubleSlap(TEXT("doubleslap")),
    Ember(TEXT("ember")), FocusEnergy(TEXT("focus-energy")), FurySwipes(TEXT("fury-swipes")), Glare(TEXT("glare")), Growth(TEXT("growth")),
    Headbutt(TEXT("headbutt")), Hypnosis(TEXT("hypnosis")), LeechLife(TEXT("leech-life")), LeechSeed(TEXT("leech-seed")),
    Minimize(TEXT("minimize")), MirrorMove(TEXT("mirror-move")), PayDay(TEXT("pay-day")), PoisonGas(TEXT("poison-gas")), Pound(TEXT("pound")),
    Rage(TEXT("rage")), Recover(TEXT("recover")), SelfDestruct(TEXT("selfdestruct")), Sing(TEXT("sing")), Slam(TEXT("slam")),
    Slash(TEXT("slash")), SleepPowder(TEXT("sleep-powder")), Smog(TEXT("smog")), Smokescreen(TEXT("smokescreen")),
    SonicBoom(TEXT("sonicboom")), Splash(TEXT("splash")), Spore(TEXT("spore")), StunSpore(TEXT("stun-spore")),
    Supersonic(TEXT("supersonic")), Swift(TEXT("swift")), VineWhip(TEXT("vine-whip")), WaterGun(TEXT("water-gun")),
    Whirlwind(TEXT("whirlwind")), Withdraw(TEXT("withdraw")), Wrap(TEXT("wrap"));

namespace
{
FLinearColor Hex(const TCHAR* Text) { return FLinearColor(FColor::FromHex(Text)); }
using T = EPYRType;

// How far up each species' Yellow learnset this table goes: level 25 covers every Pokemon Red meets or raises from
// Pallet to Cerulean; Paras and Parasect go to 31 (the Route 4 Lass's level), Staryu to 27 (Recover) and Grimer to
// 30 (Poison Gas).
int32 CapFor(FName Id)
{
    if (Id == Paras || Id == Parasect) return 31;
    if (Id == Staryu) return 27;
    if (Id == Grimer) return 30;
    return 25;
}

// source: Yellow (pret/pokeyellow data/pokemon/base_stats/*.asm and data/pokemon/evos_moves.asm; the rows below are
// generated from them by a one-off script). Base stats
// (HP/Attack/Defence/Speed/Special), types, growth group, base experience, catch rate, the level-1 moves and the
// level-up learnset up to CapFor(), and the evolution level. Pokedex heights are Bulbapedia's metric Gen 1 heights.
// The body family, colours and habitat line are this game's.
TMap<FName, FPYRSpeciesInfo> BuildSpecies()
{
    TMap<FName, FPYRSpeciesInfo> Out;
    auto Add = [&Out](FName Id, FText Name, int32 Dex, EPYRBody Body, T T1, T T2, int32 Hp, int32 Atk, int32 Def, int32 Spd, int32 Spc,
        EPYRGrowth Group, int32 Exp, int32 Catch, float HeightM, const TCHAR* Colour, const TCHAR* Accent, FText Habitat,
        TArray<FPYRLearn> Learnset, int32 EvolveLevel = 0, FName Into = NAME_None)
    {
        FPYRSpeciesInfo S;
        S.Id = Id; S.Name = Name; S.Dex = Dex; S.Body = Body; S.Type1 = T1; S.Type2 = T2;
        S.BaseHp = Hp; S.BaseAttack = Atk; S.BaseDefence = Def; S.BaseSpeed = Spd; S.BaseSpecial = Spc;
        S.Growth = Group; S.BaseExp = Exp; S.CatchRate = Catch; S.HeightM = HeightM;
        S.Colour = Hex(Colour); S.Accent = Hex(Accent); S.Habitat = Habitat;
        S.Learnset = MoveTemp(Learnset); S.EvolveLevel = EvolveLevel; S.EvolvesInto = Into;
        Out.Add(Id, MoveTemp(S));
    };
    const EPYRBody R = EPYRBody::Runner, F = EPYRBody::Flier, C = EPYRBody::Crawler, G = EPYRBody::Giant, S = EPYRBody::Swimmer;
    const EPYRGrowth MF = EPYRGrowth::MediumFast, MS = EPYRGrowth::MediumSlow, FA = EPYRGrowth::Fast, SL = EPYRGrowth::Slow;
    Add(Pikachu, LOCTEXT("Pikachu", "Pikachu"), 25, R, T::Electric, T::None, 35, 55, 30, 90, 50, MF, 82, 190, 0.4f, TEXT("f7d23e"), TEXT("e3342f"),
        LOCTEXT("PikachuHome", "Oak's lab, Pallet Town. Walks beside Red."),
        {{1, ThunderShock}, {1, Growl}, {6, TailWhip}, {8, ThunderWave}, {11, QuickAttack}, {15, DoubleTeam}, {20, Slam}});
    Add(Eevee, LOCTEXT("Eevee", "Eevee"), 133, R, T::Normal, T::None, 55, 55, 50, 55, 65, MF, 92, 45, 0.3f, TEXT("a8703c"), TEXT("f0e0c0"),
        LOCTEXT("EeveeHome", "Oak's lab. Gary's partner."),
        {{1, Tackle}, {1, TailWhip}, {8, SandAttack}, {16, Growl}, {23, QuickAttack}});
    Add(Pidgey, LOCTEXT("Pidgey", "Pidgey"), 16, F, T::Normal, T::Flying, 40, 45, 40, 56, 35, MS, 55, 255, 0.3f, TEXT("9a6b43"), TEXT("efe0c0"),
        LOCTEXT("PidgeyHome", "Route 1, Route 2, Viridian Forest, Route 24 and Route 25."),
        {{1, Gust}, {5, SandAttack}, {12, QuickAttack}, {19, Whirlwind}}, 18, FName(TEXT("pidgeotto")));
    Add(Pidgeotto, LOCTEXT("Pidgeotto", "Pidgeotto"), 17, F, T::Normal, T::Flying, 63, 60, 55, 71, 50, MS, 113, 120, 1.1f, TEXT("a0703e"), TEXT("e8d8b0"),
        LOCTEXT("PidgeottoHome", "Viridian Forest (rare), Route 24 and Route 25."),
        {{1, Gust}, {1, SandAttack}, {5, SandAttack}, {12, QuickAttack}, {21, Whirlwind}}, 36, FName(TEXT("pidgeot")));
    Add(Rattata, LOCTEXT("Rattata", "Rattata"), 19, R, T::Normal, T::None, 30, 56, 35, 72, 25, MF, 57, 255, 0.3f, TEXT("8e5fa8"), TEXT("efe8d0"),
        LOCTEXT("RattataHome", "Route 1, Route 2, Route 22, Route 3 and Route 4."),
        {{1, Tackle}, {1, TailWhip}, {7, QuickAttack}, {14, HyperFang}, {23, FocusEnergy}}, 20, FName(TEXT("raticate")));
    Add(Spearow, LOCTEXT("Spearow", "Spearow"), 21, F, T::Normal, T::Flying, 40, 60, 30, 70, 31, MF, 58, 255, 0.3f, TEXT("9c5d34"), TEXT("d9b8a0"),
        LOCTEXT("SpearowHome", "Route 22, Route 3 and Route 4."),
        {{1, Peck}, {1, Growl}, {9, Leer}, {15, FuryAttack}, {22, MirrorMove}}, 20, FName(TEXT("fearow")));
    Add(NidoranF, LOCTEXT("NidoranF", "Nidoran F"), 29, R, T::Poison, T::None, 55, 47, 52, 41, 40, MS, 59, 235, 0.4f, TEXT("9fb6e0"), TEXT("3a6fb0"),
        LOCTEXT("NidoranFHome", "Route 22 and Route 2."),
        {{1, Growl}, {1, Tackle}, {8, Scratch}, {12, DoubleKick}, {17, PoisonSting}, {23, TailWhip}}, 16, FName(TEXT("nidorina")));
    Add(NidoranM, LOCTEXT("NidoranM", "Nidoran M"), 32, R, T::Poison, T::None, 46, 57, 40, 50, 40, MS, 60, 235, 0.5f, TEXT("c78fc7"), TEXT("7a3b88"),
        LOCTEXT("NidoranMHome", "Route 22 and Route 2."),
        {{1, Leer}, {1, Tackle}, {8, HornAttack}, {12, DoubleKick}, {17, PoisonSting}, {23, FocusEnergy}}, 16, FName(TEXT("nidorino")));
    Add(Mankey, LOCTEXT("Mankey", "Mankey"), 56, R, T::Fighting, T::None, 40, 80, 35, 70, 35, MF, 74, 190, 0.5f, TEXT("e8d8b8"), TEXT("8a6040"),
        LOCTEXT("MankeyHome", "Route 22, Route 3 and Route 4."),
        {{1, Scratch}, {1, Leer}, {9, LowKick}, {15, KarateChop}, {21, FurySwipes}}, 28, FName(TEXT("primeape")));
    Add(Caterpie, LOCTEXT("Caterpie", "Caterpie"), 10, C, T::Bug, T::None, 45, 30, 35, 45, 20, MF, 53, 255, 0.3f, TEXT("7fbf3e"), TEXT("f0d64a"),
        LOCTEXT("CaterpieHome", "Viridian Forest, clinging to the trunks."),
        {{1, Tackle}, {1, StringShot}}, 7, FName(TEXT("metapod")));
    Add(Metapod, LOCTEXT("Metapod", "Metapod"), 11, C, T::Bug, T::None, 50, 20, 55, 30, 25, MF, 72, 120, 0.7f, TEXT("6fb03a"), TEXT("4a7a2a"),
        LOCTEXT("MetapodHome", "Viridian Forest."),
        {{1, Harden}, {7, Harden}}, 10, FName(TEXT("butterfree")));
    Add(Butterfree, LOCTEXT("Butterfree", "Butterfree"), 12, F, T::Bug, T::Flying, 60, 45, 50, 70, 80, MF, 160, 45, 1.1f, TEXT("7a78c0"), TEXT("f2f2f2"),
        LOCTEXT("ButterfreeHome", "Raised from a Caterpie."),
        {{1, Confusion}, {10, Confusion}, {13, PoisonPowder}, {14, StunSpore}, {15, SleepPowder}, {18, Supersonic}, {23, Whirlwind}});
    Add(Weedle, LOCTEXT("Weedle", "Weedle"), 13, C, T::Bug, T::Poison, 40, 35, 30, 50, 20, MF, 52, 255, 0.3f, TEXT("d09a48"), TEXT("e8e0c0"),
        LOCTEXT("WeedleHome", "Bug Catchers' partners (no wild Weedle in Yellow's forest)."),
        {{1, PoisonSting}, {1, StringShot}}, 7, FName(TEXT("kakuna")));
    Add(Kakuna, LOCTEXT("Kakuna", "Kakuna"), 14, C, T::Bug, T::Poison, 45, 25, 50, 35, 25, MF, 71, 120, 0.6f, TEXT("e0c040"), TEXT("8a7a30"),
        LOCTEXT("KakunaHome", "Bug Catchers' partners."),
        {{1, Harden}}, 10, FName(TEXT("beedrill")));
    Add(Geodude, LOCTEXT("Geodude", "Geodude"), 74, C, T::Rock, T::Ground, 40, 80, 100, 20, 30, MS, 86, 255, 0.4f, TEXT("9a9488"), TEXT("6a645a"),
        LOCTEXT("GeodudeHome", "Mt. Moon, and Pewter Gym with Brock."),
        {{1, Tackle}, {11, DefenseCurl}, {16, RockThrow}, {21, SelfDestruct}}, 25, FName(TEXT("graveler")));
    Add(Onix, LOCTEXT("Onix", "Onix"), 95, G, T::Rock, T::Ground, 35, 45, 160, 70, 30, MF, 108, 45, 8.8f, TEXT("8c8a86"), TEXT("5a4a3a"),
        LOCTEXT("OnixHome", "Pewter Gym, with Brock."),
        {{1, Tackle}, {1, Screech}, {15, Bind}, {19, RockThrow}, {25, Rage}});
    Add(Diglett, LOCTEXT("Diglett", "Diglett"), 50, C, T::Ground, T::None, 10, 55, 25, 95, 45, MF, 81, 255, 0.2f, TEXT("9a6a44"), TEXT("e38b9a"),
        LOCTEXT("DiglettHome", "Pewter Gym, with the Jr. Trainer."),
        {{1, Scratch}, {15, Growl}, {19, Dig}, {24, SandAttack}}, 26, FName(TEXT("dugtrio")));
    Add(Sandshrew, LOCTEXT("Sandshrew", "Sandshrew"), 27, R, T::Ground, T::None, 50, 75, 85, 40, 30, MF, 93, 255, 0.6f, TEXT("e0c060"), TEXT("8a6a30"),
        LOCTEXT("SandshrewHome", "Route 3, Route 4 and Mt. Moon."),
        {{1, Scratch}, {10, SandAttack}, {17, Slash}, {24, PoisonSting}}, 22, FName(TEXT("sandslash")));
    Add(Zubat, LOCTEXT("Zubat", "Zubat"), 41, F, T::Poison, T::Flying, 40, 45, 35, 55, 40, MF, 54, 255, 0.8f, TEXT("5a7fc8"), TEXT("a070c8"),
        LOCTEXT("ZubatHome", "Mt. Moon, fluttering in the dark."),
        {{1, LeechLife}, {10, Supersonic}, {15, Bite}, {21, ConfuseRay}}, 22, FName(TEXT("golbat")));
    Add(Clefairy, LOCTEXT("Clefairy", "Clefairy"), 35, R, T::Normal, T::None, 70, 45, 48, 35, 60, FA, 68, 150, 0.6f, TEXT("f4b8c8"), TEXT("8a5a50"),
        LOCTEXT("ClefairyHome", "Mt. Moon, deep in the caverns."),
        {{1, Pound}, {1, Growl}, {13, Sing}, {18, DoubleSlap}, {24, Minimize}});
    Add(Paras, LOCTEXT("Paras", "Paras"), 46, C, T::Bug, T::Grass, 35, 70, 55, 25, 55, MF, 70, 190, 0.3f, TEXT("e8783a"), TEXT("e04030"),
        LOCTEXT("ParasHome", "Mt. Moon B1F and B2F."),
        {{1, Scratch}, {13, StunSpore}, {20, LeechLife}, {27, Spore}}, 24, FName(TEXT("parasect")));
    Add(Parasect, LOCTEXT("Parasect", "Parasect"), 47, C, T::Bug, T::Grass, 60, 95, 80, 30, 80, MF, 128, 75, 1.0f, TEXT("e05a30"), TEXT("f0a050"),
        LOCTEXT("ParasectHome", "A Lass's partner on Route 4."),
        {{1, Scratch}, {1, StunSpore}, {1, LeechLife}, {13, StunSpore}, {20, LeechLife}, {30, Spore}});
    Add(Jigglypuff, LOCTEXT("Jigglypuff", "Jigglypuff"), 39, R, T::Normal, T::None, 115, 45, 20, 20, 25, FA, 76, 170, 0.5f, TEXT("f8c0d0"), TEXT("70c8d8"),
        LOCTEXT("JigglypuffHome", "A Lass's partner on Route 3."),
        {{1, Sing}, {9, Pound}, {14, Disable}, {19, DefenseCurl}, {24, DoubleSlap}});
    Add(Ekans, LOCTEXT("Ekans", "Ekans"), 23, C, T::Poison, T::None, 35, 60, 44, 55, 40, MF, 62, 255, 2.0f, TEXT("9a60b0"), TEXT("e8d060"),
        LOCTEXT("EkansHome", "Youngsters' and Team Rocket's partners."),
        {{1, Wrap}, {1, Leer}, {10, PoisonSting}, {17, Bite}, {24, Glare}}, 22, FName(TEXT("arbok")));
    Add(Koffing, LOCTEXT("Koffing", "Koffing"), 109, F, T::Poison, T::None, 40, 65, 95, 35, 60, MF, 114, 190, 0.6f, TEXT("8a70a8"), TEXT("e8e0b0"),
        LOCTEXT("KoffingHome", "Team Rocket's partner at Mt. Moon."),
        {{1, Tackle}, {1, Smog}}, 35, FName(TEXT("weezing")));
    Add(Meowth, LOCTEXT("Meowth", "Meowth"), 52, R, T::Normal, T::None, 40, 45, 35, 90, 40, MF, 69, 255, 0.4f, TEXT("f0e0b0"), TEXT("d8a040"),
        LOCTEXT("MeowthHome", "Team Rocket's partner at Mt. Moon."),
        {{1, Scratch}, {1, Growl}, {12, Bite}, {17, PayDay}, {24, Screech}}, 28, FName(TEXT("persian")));
    Add(Staryu, LOCTEXT("Staryu", "Staryu"), 120, S, T::Water, T::None, 30, 45, 55, 85, 70, SL, 106, 225, 0.8f, TEXT("c89a50"), TEXT("e0303a"),
        LOCTEXT("StaryuHome", "Cerulean Gym, with Misty."),
        {{1, Tackle}, {17, WaterGun}, {22, Harden}, {27, Recover}});
    Add(Starmie, LOCTEXT("Starmie", "Starmie"), 121, S, T::Water, T::Psychic, 60, 75, 85, 115, 100, SL, 207, 60, 1.1f, TEXT("8a6ab8"), TEXT("e0303a"),
        LOCTEXT("StarmieHome", "Cerulean Gym, Misty's ace."),
        {{1, Tackle}, {1, WaterGun}, {1, Harden}});
    Add(Goldeen, LOCTEXT("Goldeen", "Goldeen"), 118, S, T::Water, T::None, 45, 67, 60, 63, 50, MF, 111, 225, 0.6f, TEXT("f4f0ec"), TEXT("f08040"),
        LOCTEXT("GoldeenHome", "Cerulean Gym, with the Jr. Trainer."),
        {{1, Peck}, {1, TailWhip}, {19, Supersonic}, {24, HornAttack}}, 33, FName(TEXT("seaking")));
    Add(Horsea, LOCTEXT("Horsea", "Horsea"), 116, S, T::Water, T::None, 30, 40, 70, 60, 70, MF, 83, 225, 0.4f, TEXT("70b8e8"), TEXT("f0e0a0"),
        LOCTEXT("HorseaHome", "Cerulean Gym, with the Swimmer."),
        {{1, Bubble}, {19, Smokescreen}, {24, Leer}}, 32, FName(TEXT("seadra")));
    Add(Shellder, LOCTEXT("Shellder", "Shellder"), 90, S, T::Water, T::None, 30, 65, 100, 40, 45, SL, 97, 190, 0.3f, TEXT("8a6ab8"), TEXT("f08aa0"),
        LOCTEXT("ShellderHome", "Cerulean Gym, with the Swimmer."),
        {{1, Tackle}, {1, Withdraw}, {18, Supersonic}, {23, Clamp}});
    Add(Oddish, LOCTEXT("Oddish", "Oddish"), 43, C, T::Grass, T::Poison, 45, 50, 55, 30, 75, MS, 78, 255, 0.5f, TEXT("506ab8"), TEXT("4a9a3a"),
        LOCTEXT("OddishHome", "Route 24 and Route 25."),
        {{1, Absorb}, {15, PoisonPowder}, {17, StunSpore}, {19, SleepPowder}, {24, Acid}}, 21, FName(TEXT("gloom")));
    Add(Bellsprout, LOCTEXT("Bellsprout", "Bellsprout"), 69, R, T::Grass, T::Poison, 50, 75, 35, 40, 70, MS, 84, 255, 0.7f, TEXT("e8d050"), TEXT("5ab04a"),
        LOCTEXT("BellsproutHome", "Route 24 and Route 25."),
        {{1, VineWhip}, {1, Growth}, {13, Wrap}, {15, PoisonPowder}, {18, SleepPowder}, {21, StunSpore}}, 21, FName(TEXT("weepinbell")));
    Add(Venonat, LOCTEXT("Venonat", "Venonat"), 48, C, T::Bug, T::Poison, 60, 55, 50, 45, 40, MF, 75, 190, 1.0f, TEXT("8a60a8"), TEXT("e04040"),
        LOCTEXT("VenonatHome", "Route 24 and Route 25."),
        {{1, Tackle}, {1, Disable}, {11, Supersonic}, {19, Confusion}, {22, PoisonPowder}}, 31, FName(TEXT("venomoth")));
    Add(Magikarp, LOCTEXT("Magikarp", "Magikarp"), 129, S, T::Water, T::None, 20, 10, 55, 80, 20, SL, 20, 255, 0.9f, TEXT("e8503a"), TEXT("f0e0b0"),
        LOCTEXT("MagikarpHome", "Sold for 500 at the Route 4 Pokemon Centre."),
        {{1, Splash}, {15, Tackle}}, 20, FName(TEXT("gyarados")));
    Add(Charmander, LOCTEXT("Charmander", "Charmander"), 4, R, T::Fire, T::None, 39, 52, 43, 65, 50, MS, 65, 45, 0.6f, TEXT("f0903a"), TEXT("f8e080"),
        LOCTEXT("CharmanderHome", "Given away on Route 24 (Nugget Bridge)."),
        {{1, Scratch}, {1, Growl}, {9, Ember}, {15, Leer}, {22, Rage}}, 16, FName(TEXT("charmeleon")));
    Add(Bulbasaur, LOCTEXT("Bulbasaur", "Bulbasaur"), 1, R, T::Grass, T::Poison, 45, 49, 49, 45, 65, MS, 64, 45, 0.7f, TEXT("70c0a0"), TEXT("3a8a50"),
        LOCTEXT("BulbasaurHome", "Given away in Cerulean City to a Trainer whose Pikachu is happy."),
        {{1, Tackle}, {1, Growl}, {7, LeechSeed}, {13, VineWhip}, {20, PoisonPowder}}, 16, FName(TEXT("ivysaur")));
    Add(Slowpoke, LOCTEXT("Slowpoke", "Slowpoke"), 79, R, T::Water, T::Psychic, 90, 65, 65, 15, 40, MF, 99, 190, 1.2f, TEXT("f0a0b0"), TEXT("f0e0c0"),
        LOCTEXT("SlowpokeHome", "A Youngster's partner on Route 25."),
        {{1, Confusion}, {18, Disable}, {22, Headbutt}}, 37, FName(TEXT("slowbro")));
    Add(Machop, LOCTEXT("Machop", "Machop"), 66, R, T::Fighting, T::None, 70, 80, 50, 35, 35, MS, 88, 180, 0.8f, TEXT("a0b0c0"), TEXT("8a8aa0"),
        LOCTEXT("MachopHome", "Hikers' partners on Route 25, and the Cerulean burglar's."),
        {{1, KarateChop}, {20, LowKick}, {25, Leer}}, 28, FName(TEXT("machoke")));
    Add(Magnemite, LOCTEXT("Magnemite", "Magnemite"), 81, F, T::Electric, T::None, 25, 35, 70, 45, 95, MF, 89, 190, 0.3f, TEXT("c0c8d0"), TEXT("e04040"),
        LOCTEXT("MagnemiteHome", "A Super Nerd's partner in Mt. Moon."),
        {{1, Tackle}, {21, SonicBoom}, {25, ThunderShock}}, 30, FName(TEXT("magneton")));
    Add(Voltorb, LOCTEXT("Voltorb", "Voltorb"), 100, C, T::Electric, T::None, 40, 30, 50, 100, 55, MF, 103, 190, 0.5f, TEXT("e03a3a"), TEXT("f4f4f4"),
        LOCTEXT("VoltorbHome", "Super Nerds' partners in Mt. Moon."),
        {{1, Tackle}, {1, Screech}, {17, SonicBoom}, {22, SelfDestruct}}, 30, FName(TEXT("electrode")));
    Add(Grimer, LOCTEXT("Grimer", "Grimer"), 88, C, T::Poison, T::None, 80, 80, 50, 25, 40, MF, 90, 190, 0.9f, TEXT("9a70b8"), TEXT("5a3a70"),
        LOCTEXT("GrimerHome", "A Super Nerd's partner on Mt. Moon B2F."),
        {{1, Pound}, {1, Disable}, {30, PoisonGas}}, 38, FName(TEXT("muk")));
    Add(Drowzee, LOCTEXT("Drowzee", "Drowzee"), 96, R, T::Psychic, T::None, 60, 48, 45, 42, 90, MF, 102, 190, 1.0f, TEXT("e8c050"), TEXT("8a5a30"),
        LOCTEXT("DrowzeeHome", "The Cerulean burglar's partner."),
        {{1, Pound}, {1, Hypnosis}, {12, Disable}, {17, Confusion}, {24, Headbutt}}, 26, FName(TEXT("hypno")));
    return Out;
}

// source: Yellow (pret/pokeyellow data/moves/moves.asm). Power, accuracy, type and effect are Gen 1's (Gust, Bite,
// Sand-Attack and Karate Chop are Normal there; side-effect chances are Gen 1's: 10% for Thundershock, Ember, Bite,
// Confusion and Hyper Fang, 20% for Poison Sting, 30% for Low Kick and Headbutt, 33% for Bubble, Bubblebeam and Acid,
// 40% for Smog). Swift never misses. The cooldown class, shape, reach and timings are the live-battle layer.
TMap<FName, FPYRMoveInfo> BuildMoves()
{
    TMap<FName, FPYRMoveInfo> Out;
    auto M = [&Out](FName Id, FText Name, T Type, EPYRMoveTier Tier, EPYRMoveShape Shape, float Power, float Accuracy, float Cooldown) -> FPYRMoveInfo&
    {
        FPYRMoveInfo& I = Out.Add(Id);
        I.Id = Id; I.Name = Name; I.Type = Type; I.Tier = Tier; I.Shape = Shape; I.Power = Power; I.Accuracy = Accuracy; I.Cooldown = Cooldown;
        return I;
    };
    using S = EPYRMoveShape;
    using Q = EPYRMoveTier;
    using E = EPYREffect;
    auto Stat = [](FPYRMoveInfo& I, EPYREffect Ef, EPYRStat St, int32 Stages, float Chance = 1.f) { I.Effect = Ef; I.Stat = St; I.Stages = Stages; I.EffectChance = Chance; };
    auto Fx = [](FPYRMoveInfo& I, EPYREffect Ef, float Chance = 1.f) { I.Effect = Ef; I.EffectChance = Chance; };
    // Quick (about 1 s).
    {   FPYRMoveInfo& I = M(ThunderShock, LOCTEXT("ThunderShock", "Thundershock"), T::Electric, Q::Quick, S::Projectile, 40, 100, 1.f);
        I.Range = 1400; I.Radius = 55; I.TellSeconds = .5f; I.HitSeconds = .6f; Fx(I, E::Paralyse, .1f); }
    {   FPYRMoveInfo& I = M(Tackle, LOCTEXT("Tackle", "Tackle"), T::Normal, Q::Quick, S::Dash, 35, 95, 1.f);
        I.Range = 380; I.Radius = 55; I.TellSeconds = .6f; I.HitSeconds = .3f; }
    {   FPYRMoveInfo& I = M(Gust, LOCTEXT("Gust", "Gust"), T::Normal, Q::Quick, S::Cone, 40, 100, 1.1f);
        I.Range = 700; I.Radius = 32; I.TellSeconds = .7f; I.HitSeconds = .25f; }
    {   FPYRMoveInfo& I = M(Scratch, LOCTEXT("Scratch", "Scratch"), T::Normal, Q::Quick, S::Dash, 40, 100, 1.f);
        I.Range = 330; I.Radius = 55; I.TellSeconds = .5f; I.HitSeconds = .25f; }
    {   FPYRMoveInfo& I = M(Pound, LOCTEXT("Pound", "Pound"), T::Normal, Q::Quick, S::Dash, 40, 100, 1.f);
        I.Range = 330; I.Radius = 55; I.TellSeconds = .5f; I.HitSeconds = .25f; }
    {   FPYRMoveInfo& I = M(QuickAttack, LOCTEXT("QuickAttack", "Quick Attack"), T::Normal, Q::Quick, S::Dash, 40, 100, 1.2f);
        I.Range = 750; I.Radius = 70; I.TellSeconds = .4f; I.HitSeconds = .22f; }
    {   FPYRMoveInfo& I = M(PoisonSting, LOCTEXT("PoisonSting", "Poison Sting"), T::Poison, Q::Quick, S::Projectile, 15, 100, 1.f);
        I.Range = 1100; I.Radius = 40; I.TellSeconds = .5f; I.HitSeconds = .7f; Fx(I, E::Poison, .2f); }
    {   FPYRMoveInfo& I = M(Peck, LOCTEXT("Peck", "Peck"), T::Flying, Q::Quick, S::Dash, 35, 100, 1.f);
        I.Range = 350; I.Radius = 50; I.TellSeconds = .5f; I.HitSeconds = .25f; }
    {   FPYRMoveInfo& I = M(Bite, LOCTEXT("Bite", "Bite"), T::Normal, Q::Quick, S::Dash, 60, 100, 1.3f);
        I.Range = 340; I.Radius = 55; I.TellSeconds = .6f; I.HitSeconds = .25f; Fx(I, E::Flinch, .1f); }
    {   FPYRMoveInfo& I = M(FuryAttack, LOCTEXT("FuryAttack", "Fury Attack"), T::Normal, Q::Quick, S::Dash, 15, 85, 1.3f);
        I.Range = 340; I.Radius = 50; I.TellSeconds = .5f; I.HitSeconds = .3f; I.MinHits = 2; I.MaxHits = 5; }
    {   FPYRMoveInfo& I = M(DoubleSlap, LOCTEXT("DoubleSlap", "Doubleslap"), T::Normal, Q::Quick, S::Dash, 15, 85, 1.3f);
        I.Range = 340; I.Radius = 55; I.TellSeconds = .5f; I.HitSeconds = .3f; I.MinHits = 2; I.MaxHits = 5; }
    {   FPYRMoveInfo& I = M(FurySwipes, LOCTEXT("FurySwipes", "Fury Swipes"), T::Normal, Q::Quick, S::Dash, 18, 80, 1.3f);
        I.Range = 340; I.Radius = 55; I.TellSeconds = .5f; I.HitSeconds = .3f; I.MinHits = 2; I.MaxHits = 5; }
    {   FPYRMoveInfo& I = M(VineWhip, LOCTEXT("VineWhip", "Vine Whip"), T::Grass, Q::Quick, S::Cone, 35, 100, 1.f);
        I.Range = 520; I.Radius = 16; I.TellSeconds = .5f; I.HitSeconds = .25f; }
    {   FPYRMoveInfo& I = M(WaterGun, LOCTEXT("WaterGun", "Water Gun"), T::Water, Q::Quick, S::Projectile, 40, 100, 1.f);
        I.Range = 1300; I.Radius = 60; I.TellSeconds = .5f; I.HitSeconds = .55f; }
    {   FPYRMoveInfo& I = M(Bubble, LOCTEXT("Bubble", "Bubble"), T::Water, Q::Quick, S::Projectile, 20, 100, 1.f);
        I.Range = 1000; I.Radius = 75; I.TellSeconds = .5f; I.HitSeconds = .9f; Stat(I, E::FoeStat, EPYRStat::Speed, -1, .33f); }
    {   FPYRMoveInfo& I = M(Ember, LOCTEXT("Ember", "Ember"), T::Fire, Q::Quick, S::Projectile, 40, 100, 1.f);
        I.Range = 1200; I.Radius = 55; I.TellSeconds = .5f; I.HitSeconds = .6f; Fx(I, E::Burn, .1f); }
    {   FPYRMoveInfo& I = M(Absorb, LOCTEXT("Absorb", "Absorb"), T::Grass, Q::Quick, S::Projectile, 20, 100, 1.f);
        I.Range = 1100; I.Radius = 60; I.TellSeconds = .5f; I.HitSeconds = .6f; Fx(I, E::Drain); }
    {   FPYRMoveInfo& I = M(LeechLife, LOCTEXT("LeechLife", "Leech Life"), T::Bug, Q::Quick, S::Dash, 20, 100, 1.f);
        I.Range = 340; I.Radius = 55; I.TellSeconds = .5f; I.HitSeconds = .25f; Fx(I, E::Drain); }
    {   FPYRMoveInfo& I = M(Splash, LOCTEXT("Splash", "Splash"), T::Normal, Q::Quick, S::Self, 0, 0, 1.f);
        I.TellSeconds = .4f; I.HitSeconds = .1f; Fx(I, E::Splash); }
    // Tactical (3-8 s).
    {   FPYRMoveInfo& I = M(Growl, LOCTEXT("Growl", "Growl"), T::Normal, Q::Tactical, S::Debuff, 0, 100, 4.f);
        I.Range = 1000; I.TellSeconds = .6f; I.HitSeconds = .1f; Stat(I, E::FoeStat, EPYRStat::Attack, -1); }
    {   FPYRMoveInfo& I = M(TailWhip, LOCTEXT("TailWhip", "Tail Whip"), T::Normal, Q::Tactical, S::Debuff, 0, 100, 4.f);
        I.Range = 1000; I.TellSeconds = .6f; I.HitSeconds = .1f; Stat(I, E::FoeStat, EPYRStat::Defence, -1); }
    {   FPYRMoveInfo& I = M(Leer, LOCTEXT("Leer", "Leer"), T::Normal, Q::Tactical, S::Debuff, 0, 100, 4.f);
        I.Range = 1000; I.TellSeconds = .6f; I.HitSeconds = .1f; Stat(I, E::FoeStat, EPYRStat::Defence, -1); }
    {   FPYRMoveInfo& I = M(SandAttack, LOCTEXT("SandAttack", "Sand-Attack"), T::Normal, Q::Tactical, S::Cone, 0, 100, 5.f);
        I.Range = 600; I.Radius = 32; I.TellSeconds = .6f; I.HitSeconds = .25f; Stat(I, E::FoeStat, EPYRStat::Accuracy, -1); }
    {   FPYRMoveInfo& I = M(Smokescreen, LOCTEXT("Smokescreen", "Smokescreen"), T::Normal, Q::Tactical, S::Cone, 0, 100, 5.f);
        I.Range = 650; I.Radius = 35; I.TellSeconds = .6f; I.HitSeconds = .3f; Stat(I, E::FoeStat, EPYRStat::Accuracy, -1); }
    {   FPYRMoveInfo& I = M(StringShot, LOCTEXT("StringShot", "String Shot"), T::Bug, Q::Tactical, S::Projectile, 0, 95, 5.f);
        I.Range = 1000; I.Radius = 60; I.TellSeconds = .6f; I.HitSeconds = .8f; Stat(I, E::FoeStat, EPYRStat::Speed, -1); }
    {   FPYRMoveInfo& I = M(Harden, LOCTEXT("Harden", "Harden"), T::Normal, Q::Tactical, S::Self, 0, 0, 5.f);
        I.TellSeconds = .5f; I.HitSeconds = .1f; Stat(I, E::SelfStat, EPYRStat::Defence, 1); }
    {   FPYRMoveInfo& I = M(DefenseCurl, LOCTEXT("DefenseCurl", "Defense Curl"), T::Normal, Q::Tactical, S::Self, 0, 0, 5.f);
        I.TellSeconds = .5f; I.HitSeconds = .1f; Stat(I, E::SelfStat, EPYRStat::Defence, 1); }
    {   FPYRMoveInfo& I = M(Withdraw, LOCTEXT("Withdraw", "Withdraw"), T::Water, Q::Tactical, S::Self, 0, 0, 5.f);
        I.TellSeconds = .5f; I.HitSeconds = .1f; Stat(I, E::SelfStat, EPYRStat::Defence, 1); }
    {   FPYRMoveInfo& I = M(Growth, LOCTEXT("Growth", "Growth"), T::Normal, Q::Tactical, S::Self, 0, 0, 5.f);
        I.TellSeconds = .5f; I.HitSeconds = .1f; Stat(I, E::SelfStat, EPYRStat::Special, 1); }
    {   FPYRMoveInfo& I = M(DoubleTeam, LOCTEXT("DoubleTeam", "Double Team"), T::Normal, Q::Tactical, S::Self, 0, 0, 6.f);
        I.TellSeconds = .5f; I.HitSeconds = .1f; Stat(I, E::SelfStat, EPYRStat::Evasion, 1); }
    {   FPYRMoveInfo& I = M(Minimize, LOCTEXT("Minimize", "Minimize"), T::Normal, Q::Tactical, S::Self, 0, 0, 6.f);
        I.TellSeconds = .5f; I.HitSeconds = .1f; Stat(I, E::SelfStat, EPYRStat::Evasion, 1); }
    {   FPYRMoveInfo& I = M(FocusEnergy, LOCTEXT("FocusEnergy", "Focus Energy"), T::Normal, Q::Tactical, S::Self, 0, 0, 8.f);
        I.TellSeconds = .5f; I.HitSeconds = .1f; Fx(I, E::FocusEnergy); }
    {   FPYRMoveInfo& I = M(MirrorMove, LOCTEXT("MirrorMove", "Mirror Move"), T::Flying, Q::Tactical, S::Self, 0, 0, 6.f);
        I.TellSeconds = .5f; I.HitSeconds = .1f; Fx(I, E::Mirror); }
    {   FPYRMoveInfo& I = M(Screech, LOCTEXT("Screech", "Screech"), T::Normal, Q::Tactical, S::Ring, 0, 85, 8.f);
        I.Range = 1500; I.Radius = 90; I.TellSeconds = .8f; I.HitSeconds = .9f; Stat(I, E::FoeStat, EPYRStat::Defence, -2); }
    {   FPYRMoveInfo& I = M(HornAttack, LOCTEXT("HornAttack", "Horn Attack"), T::Normal, Q::Tactical, S::Dash, 65, 100, 3.f);
        I.Range = 400; I.Radius = 55; I.TellSeconds = .6f; I.HitSeconds = .3f; }
    {   FPYRMoveInfo& I = M(LowKick, LOCTEXT("LowKick", "Low Kick"), T::Fighting, Q::Tactical, S::Dash, 50, 90, 3.f);
        I.Range = 350; I.Radius = 55; I.TellSeconds = .6f; I.HitSeconds = .3f; Fx(I, E::Flinch, .3f); }
    {   FPYRMoveInfo& I = M(KarateChop, LOCTEXT("KarateChop", "Karate Chop"), T::Normal, Q::Tactical, S::Dash, 50, 100, 3.f);
        I.Range = 350; I.Radius = 55; I.TellSeconds = .6f; I.HitSeconds = .3f; I.bHighCrit = true; }
    {   FPYRMoveInfo& I = M(Slash, LOCTEXT("Slash", "Slash"), T::Normal, Q::Tactical, S::Dash, 70, 100, 3.f);
        I.Range = 360; I.Radius = 55; I.TellSeconds = .6f; I.HitSeconds = .3f; I.bHighCrit = true; }
    {   FPYRMoveInfo& I = M(Headbutt, LOCTEXT("Headbutt", "Headbutt"), T::Normal, Q::Tactical, S::Dash, 70, 100, 4.f);
        I.Range = 400; I.Radius = 60; I.TellSeconds = .7f; I.HitSeconds = .3f; Fx(I, E::Flinch, .3f); }
    {   FPYRMoveInfo& I = M(Rage, LOCTEXT("Rage", "Rage"), T::Normal, Q::Tactical, S::Dash, 20, 100, 3.f);
        I.Range = 360; I.Radius = 55; I.TellSeconds = .6f; I.HitSeconds = .3f; Fx(I, E::Rage); }
    {   FPYRMoveInfo& I = M(Wrap, LOCTEXT("Wrap", "Wrap"), T::Normal, Q::Tactical, S::Dash, 15, 85, 5.f);
        I.Range = 380; I.Radius = 60; I.TellSeconds = .6f; I.HitSeconds = .3f; Fx(I, E::Trap); }
    {   FPYRMoveInfo& I = M(Bind, LOCTEXT("Bind", "Bind"), T::Normal, Q::Tactical, S::Dash, 15, 75, 5.f);
        I.Range = 400; I.Radius = 60; I.TellSeconds = .7f; I.HitSeconds = .3f; Fx(I, E::Trap); }
    {   FPYRMoveInfo& I = M(Clamp, LOCTEXT("Clamp", "Clamp"), T::Water, Q::Tactical, S::Dash, 35, 75, 5.f);
        I.Range = 320; I.Radius = 60; I.TellSeconds = .6f; I.HitSeconds = .3f; Fx(I, E::Trap); }
    {   FPYRMoveInfo& I = M(Acid, LOCTEXT("Acid", "Acid"), T::Poison, Q::Tactical, S::Projectile, 40, 100, 3.f);
        I.Range = 1100; I.Radius = 60; I.TellSeconds = .5f; I.HitSeconds = .6f; Stat(I, E::FoeStat, EPYRStat::Defence, -1, .33f); }
    {   FPYRMoveInfo& I = M(Smog, LOCTEXT("Smog", "Smog"), T::Poison, Q::Tactical, S::Cone, 20, 70, 3.f);
        I.Range = 650; I.Radius = 35; I.TellSeconds = .7f; I.HitSeconds = .4f; Fx(I, E::Poison, .4f); }
    {   FPYRMoveInfo& I = M(PoisonGas, LOCTEXT("PoisonGas", "Poison Gas"), T::Poison, Q::Tactical, S::Cone, 0, 55, 5.f);
        I.Range = 650; I.Radius = 35; I.TellSeconds = .7f; I.HitSeconds = .4f; Fx(I, E::Poison); }
    {   FPYRMoveInfo& I = M(PoisonPowder, LOCTEXT("PoisonPowder", "Poison Powder"), T::Poison, Q::Tactical, S::Circle, 0, 75, 6.f);
        I.Range = 1100; I.Radius = 160; I.TellSeconds = .8f; I.HitSeconds = .3f; Fx(I, E::Poison); }
    {   FPYRMoveInfo& I = M(StunSpore, LOCTEXT("StunSpore", "Stun Spore"), T::Grass, Q::Tactical, S::Circle, 0, 75, 6.f);
        I.Range = 1100; I.Radius = 160; I.TellSeconds = .8f; I.HitSeconds = .3f; Fx(I, E::Paralyse); }
    {   FPYRMoveInfo& I = M(SleepPowder, LOCTEXT("SleepPowder", "Sleep Powder"), T::Grass, Q::Tactical, S::Circle, 0, 75, 8.f);
        I.Range = 1100; I.Radius = 160; I.TellSeconds = .8f; I.HitSeconds = .3f; Fx(I, E::Sleep); }
    {   FPYRMoveInfo& I = M(Glare, LOCTEXT("Glare", "Glare"), T::Normal, Q::Tactical, S::Debuff, 0, 75, 8.f);
        I.Range = 900; I.TellSeconds = .7f; I.HitSeconds = .1f; Fx(I, E::Paralyse); }
    {   FPYRMoveInfo& I = M(Disable, LOCTEXT("Disable", "Disable"), T::Normal, Q::Tactical, S::Debuff, 0, 55, 8.f);
        I.Range = 900; I.TellSeconds = .6f; I.HitSeconds = .1f; Fx(I, E::Disable); }
    {   FPYRMoveInfo& I = M(Supersonic, LOCTEXT("Supersonic", "Supersonic"), T::Normal, Q::Tactical, S::Projectile, 0, 55, 5.f);
        I.Range = 1200; I.Radius = 80; I.TellSeconds = .6f; I.HitSeconds = .7f; Fx(I, E::Confuse); }
    {   FPYRMoveInfo& I = M(ConfuseRay, LOCTEXT("ConfuseRay", "Confuse Ray"), T::Ghost, Q::Tactical, S::Projectile, 0, 100, 6.f);
        I.Range = 1200; I.Radius = 70; I.TellSeconds = .7f; I.HitSeconds = .8f; Fx(I, E::Confuse); }
    {   FPYRMoveInfo& I = M(Sing, LOCTEXT("Sing", "Sing"), T::Normal, Q::Tactical, S::Ring, 0, 55, 8.f);
        I.Range = 900; I.Radius = 90; I.TellSeconds = .8f; I.HitSeconds = .8f; Fx(I, E::Sleep); }
    {   FPYRMoveInfo& I = M(Hypnosis, LOCTEXT("Hypnosis", "Hypnosis"), T::Psychic, Q::Tactical, S::Projectile, 0, 60, 8.f);
        I.Range = 1100; I.Radius = 80; I.TellSeconds = .7f; I.HitSeconds = .8f; Fx(I, E::Sleep); }
    {   FPYRMoveInfo& I = M(LeechSeed, LOCTEXT("LeechSeed", "Leech Seed"), T::Grass, Q::Tactical, S::Projectile, 0, 90, 8.f);
        I.Range = 1000; I.Radius = 60; I.TellSeconds = .6f; I.HitSeconds = .7f; Fx(I, E::LeechSeed); }
    {   FPYRMoveInfo& I = M(SonicBoom, LOCTEXT("SonicBoom", "Sonicboom"), T::Normal, Q::Tactical, S::Projectile, 1, 90, 4.f);
        I.Range = 1300; I.Radius = 70; I.TellSeconds = .6f; I.HitSeconds = .5f; I.FixedDamage = 20; }
    {   FPYRMoveInfo& I = M(PayDay, LOCTEXT("PayDay", "Pay Day"), T::Normal, Q::Tactical, S::Projectile, 40, 100, 3.f);
        I.Range = 1100; I.Radius = 60; I.TellSeconds = .5f; I.HitSeconds = .6f; Fx(I, E::PayDay); }
    {   FPYRMoveInfo& I = M(Swift, LOCTEXT("Swift", "Swift"), T::Normal, Q::Tactical, S::Projectile, 60, 0, 4.f);
        I.Range = 1400; I.Radius = 90; I.TellSeconds = .6f; I.HitSeconds = .6f; }
    {   FPYRMoveInfo& I = M(BubbleBeam, LOCTEXT("BubbleBeam", "Bubblebeam"), T::Water, Q::Tactical, S::Projectile, 65, 100, 6.f);
        I.Range = 1400; I.Radius = 80; I.TellSeconds = .7f; I.HitSeconds = .7f; Stat(I, E::FoeStat, EPYRStat::Speed, -1, .33f); }
    {   FPYRMoveInfo& I = M(Whirlwind, LOCTEXT("Whirlwind", "Whirlwind"), T::Normal, Q::Tactical, S::Cone, 0, 85, 8.f);
        I.Range = 800; I.Radius = 30; I.TellSeconds = .7f; I.HitSeconds = .3f; Fx(I, E::Whirlwind); }
    // Big (12-20 s).
    {   FPYRMoveInfo& I = M(ThunderWave, LOCTEXT("ThunderWave", "Thunder Wave"), T::Electric, Q::Big, S::Ring, 0, 100, 15.f);
        I.Range = 1100; I.Radius = 80; I.TellSeconds = .7f; I.HitSeconds = .5f; Fx(I, E::Paralyse); }
    {   FPYRMoveInfo& I = M(Bide, LOCTEXT("Bide", "Bide"), T::Normal, Q::Big, S::Bide, 0, 0, 18.f);
        I.Range = 1100; I.Radius = 110; I.TellSeconds = .8f; I.HitSeconds = .8f; I.Effect = E::Bide; }
    {   FPYRMoveInfo& I = M(DoubleKick, LOCTEXT("DoubleKick", "Double Kick"), T::Fighting, Q::Big, S::Dash, 30, 100, 12.f);
        I.Range = 380; I.Radius = 55; I.TellSeconds = .7f; I.HitSeconds = .35f; I.MinHits = I.MaxHits = 2; }
    {   FPYRMoveInfo& I = M(Confusion, LOCTEXT("Confusion", "Confusion"), T::Psychic, Q::Big, S::Projectile, 50, 100, 12.f);
        I.Range = 1300; I.Radius = 75; I.TellSeconds = .7f; I.HitSeconds = .7f; Fx(I, E::Confuse, .1f); }
    {   FPYRMoveInfo& I = M(HyperFang, LOCTEXT("HyperFang", "Hyper Fang"), T::Normal, Q::Big, S::Dash, 80, 90, 12.f);
        I.Range = 420; I.Radius = 55; I.TellSeconds = .8f; I.HitSeconds = .3f; Fx(I, E::Flinch, .1f); }
    {   FPYRMoveInfo& I = M(RockThrow, LOCTEXT("RockThrow", "Rock Throw"), T::Rock, Q::Big, S::Circle, 50, 65, 12.f);
        I.Range = 2400; I.Radius = 170; I.TellSeconds = 1.f; I.HitSeconds = .15f; }
    {   FPYRMoveInfo& I = M(Slam, LOCTEXT("Slam", "Slam"), T::Normal, Q::Big, S::Dash, 80, 75, 12.f);
        I.Range = 450; I.Radius = 65; I.TellSeconds = .8f; I.HitSeconds = .35f; }
    {   FPYRMoveInfo& I = M(Spore, LOCTEXT("Spore", "Spore"), T::Grass, Q::Big, S::Circle, 0, 100, 14.f);
        I.Range = 1100; I.Radius = 150; I.TellSeconds = .9f; I.HitSeconds = .3f; Fx(I, E::Sleep); }
    {   FPYRMoveInfo& I = M(Recover, LOCTEXT("Recover", "Recover"), T::Normal, Q::Big, S::Self, 0, 0, 15.f);
        I.TellSeconds = .6f; I.HitSeconds = .1f; Fx(I, E::Heal); }
    {   FPYRMoveInfo& I = M(Dig, LOCTEXT("Dig", "Dig"), T::Ground, Q::Big, S::Burrow, 100, 100, 14.f);
        I.Range = 1600; I.Radius = 150; I.TellSeconds = 1.f; I.HitSeconds = .3f; }
    {   FPYRMoveInfo& I = M(SelfDestruct, LOCTEXT("SelfDestruct", "Selfdestruct"), T::Normal, Q::Big, S::Ring, 130, 100, 20.f);
        I.Range = 550; I.Radius = 120; I.TellSeconds = 1.2f; I.HitSeconds = .4f; Fx(I, E::Explode); }
    return Out;
}

TMap<FName, FPYRSpeciesInfo>& SpeciesTableMut() { static TMap<FName, FPYRSpeciesInfo> Table = BuildSpecies(); return Table; }
TMap<FName, FPYRMoveInfo>& MoveTableMut() { static TMap<FName, FPYRMoveInfo> Table = BuildMoves(); return Table; }
const TMap<FName, FPYRSpeciesInfo>& SpeciesTable() { return SpeciesTableMut(); }
const TMap<FName, FPYRMoveInfo>& MoveTable() { return MoveTableMut(); }
// m4: Yellow's experience tables (exp.json), index = level; empty until imported (the formulas below stand in).
TArray<int32> XpTables[4];
TArray<FName> DexList;

// Gen 1 chart (source: Yellow): attacker row, only the entries that are not x1. Includes Gen 1's quirks
// (Bug and Poison hit each other for x2, Ghost does nothing to Psychic, Ice is neutral on Fire).
float Chart[(int32)EPYRType::Count][(int32)EPYRType::Count];
bool bChart = false;
void BuildChart()
{
    for (auto& Row : Chart) for (float& V : Row) V = 1.f;
    auto S = [](T A, T D, float V) { Chart[(int32)A][(int32)D] = V; };
    S(T::Normal, T::Rock, .5f); S(T::Normal, T::Ghost, 0);
    S(T::Fire, T::Fire, .5f); S(T::Fire, T::Water, .5f); S(T::Fire, T::Grass, 2); S(T::Fire, T::Ice, 2); S(T::Fire, T::Bug, 2); S(T::Fire, T::Rock, .5f); S(T::Fire, T::Dragon, .5f);
    S(T::Water, T::Fire, 2); S(T::Water, T::Water, .5f); S(T::Water, T::Grass, .5f); S(T::Water, T::Ground, 2); S(T::Water, T::Rock, 2); S(T::Water, T::Dragon, .5f);
    S(T::Electric, T::Water, 2); S(T::Electric, T::Electric, .5f); S(T::Electric, T::Grass, .5f); S(T::Electric, T::Ground, 0); S(T::Electric, T::Flying, 2); S(T::Electric, T::Dragon, .5f);
    S(T::Grass, T::Fire, .5f); S(T::Grass, T::Water, 2); S(T::Grass, T::Grass, .5f); S(T::Grass, T::Poison, .5f); S(T::Grass, T::Ground, 2); S(T::Grass, T::Flying, .5f); S(T::Grass, T::Bug, .5f); S(T::Grass, T::Rock, 2); S(T::Grass, T::Dragon, .5f);
    S(T::Ice, T::Water, .5f); S(T::Ice, T::Grass, 2); S(T::Ice, T::Ice, .5f); S(T::Ice, T::Ground, 2); S(T::Ice, T::Flying, 2); S(T::Ice, T::Dragon, 2);
    S(T::Fighting, T::Normal, 2); S(T::Fighting, T::Ice, 2); S(T::Fighting, T::Poison, .5f); S(T::Fighting, T::Flying, .5f); S(T::Fighting, T::Psychic, .5f); S(T::Fighting, T::Bug, .5f); S(T::Fighting, T::Rock, 2); S(T::Fighting, T::Ghost, 0);
    S(T::Poison, T::Grass, 2); S(T::Poison, T::Poison, .5f); S(T::Poison, T::Ground, .5f); S(T::Poison, T::Bug, 2); S(T::Poison, T::Rock, .5f); S(T::Poison, T::Ghost, .5f);
    S(T::Ground, T::Fire, 2); S(T::Ground, T::Electric, 2); S(T::Ground, T::Grass, .5f); S(T::Ground, T::Poison, 2); S(T::Ground, T::Flying, 0); S(T::Ground, T::Bug, .5f); S(T::Ground, T::Rock, 2);
    S(T::Flying, T::Electric, .5f); S(T::Flying, T::Grass, 2); S(T::Flying, T::Fighting, 2); S(T::Flying, T::Bug, 2); S(T::Flying, T::Rock, .5f);
    S(T::Psychic, T::Fighting, 2); S(T::Psychic, T::Poison, 2); S(T::Psychic, T::Psychic, .5f);
    S(T::Bug, T::Fire, .5f); S(T::Bug, T::Grass, 2); S(T::Bug, T::Fighting, .5f); S(T::Bug, T::Poison, 2); S(T::Bug, T::Flying, .5f); S(T::Bug, T::Psychic, 2); S(T::Bug, T::Ghost, .5f);
    S(T::Rock, T::Fire, 2); S(T::Rock, T::Ice, 2); S(T::Rock, T::Fighting, .5f); S(T::Rock, T::Ground, .5f); S(T::Rock, T::Flying, 2); S(T::Rock, T::Bug, 2);
    S(T::Ghost, T::Normal, 0); S(T::Ghost, T::Psychic, 0); S(T::Ghost, T::Ghost, 2);
    S(T::Dragon, T::Dragon, 2);
    bChart = true;
}

// Gen 1 special types use Special for attack and defence.
bool IsSpecialType(T Type)
{
    return Type == T::Fire || Type == T::Water || Type == T::Grass || Type == T::Electric || Type == T::Ice || Type == T::Psychic || Type == T::Dragon;
}
}

const FPYRSpeciesInfo* Species(FName Id) { return Id.IsNone() ? nullptr : SpeciesTable().Find(Id); }
const FPYRMoveInfo* Move(FName Id) { return Id.IsNone() ? nullptr : MoveTable().Find(Id); }

const TArray<FName>& AllSpecies()
{
    static TArray<FName> Order = [] {
        TArray<FName> Out = {Pikachu, Eevee, Pidgey, Pidgeotto, Rattata, Spearow, NidoranF, NidoranM, Mankey, Caterpie, Metapod,
            Butterfree, Weedle, Kakuna, Geodude, Onix, Diglett, Sandshrew};
        Out.Append(M3Species());
        return Out;
    }();
    return Order;
}

const TArray<FName>& M3Species()
{
    // Yellow, Pewter to Cerulean: the wild Pokemon of Route 3, Mt. Moon, Route 4, Route 24 and Route 25, every trainer's
    // team on those routes, in Mt. Moon, on Nugget Bridge and in Cerulean (Gym, Gary, the burglar), and the gifts
    // (Route 24 Charmander, Cerulean Bulbasaur, the Route 4 salesman's Magikarp). In Pokedex order.
    static TArray<FName> Order = {Bulbasaur, Charmander, Ekans, Clefairy, Jigglypuff, Zubat, Oddish, Paras, Parasect, Venonat, Meowth,
        Machop, Bellsprout, Slowpoke, Magnemite, Grimer, Shellder, Drowzee, Voltorb, Koffing, Horsea, Goldeen, Staryu, Starmie, Magikarp};
    return Order;
}

int32 LearnsetCap(FName SpeciesId) { return CapFor(SpeciesId); }

FPYRSpeciesInfo& MutableSpecies(FName Id)
{
    FPYRSpeciesInfo* Found = SpeciesTableMut().Find(Id);
    if (!Found)
    {
        Found = &SpeciesTableMut().Add(Id);
        Found->Id = Id;
        Found->Name = FText::FromName(Id);
    }
    DexList.Reset();
    return *Found;
}

FPYRMoveInfo& MutableMove(FName Id)
{
    FPYRMoveInfo* Found = MoveTableMut().Find(Id);
    if (!Found)
    {
        Found = &MoveTableMut().Add(Id);
        Found->Id = Id;
        Found->Name = FText::FromName(Id);
    }
    return *Found;
}

void SetXpTable(EPYRGrowth Group, const TArray<int32>& ByLevel)
{
    const int32 I = int32(Group);
    if (I >= 0 && I < 4 && ByLevel.Num() >= 100) XpTables[I] = ByLevel;
}

const TArray<FName>& DexOrder()
{
    if (DexList.Num() == 0)
    {
        for (const auto& Pair : SpeciesTable()) DexList.Add(Pair.Key);
        DexList.Sort([](const FName& A, const FName& B)
        {
            const int32 DA = SpeciesTable()[A].Dex, DB = SpeciesTable()[B].Dex;
            return DA != DB ? DA < DB : A.LexicalLess(B);
        });
    }
    return DexList;
}

FName EvolutionByItem(FName SpeciesId, FName ItemKey)
{
    if (const FPYRSpeciesInfo* S = Species(SpeciesId))
        for (const FPYREvolution& E : S->Evolutions)
            if (E.Kind == EPYREvolveKind::Item && E.Item == ItemKey && Species(E.Into)) return E.Into;
    return NAME_None;
}

bool CanLearnTm(FName SpeciesId, int32 TmNumber)
{
    const FPYRSpeciesInfo* S = Species(SpeciesId);
    return S && S->Tms.Contains(TmNumber);
}

const TArray<FName>& AllMoves()
{
    static TArray<FName> Order = [] { TArray<FName> Out; MoveTable().GetKeys(Out); return Out; }();
    return Order;
}

float TypeFactor(EPYRType Attack, EPYRType Defend)
{
    if (Attack == EPYRType::None || Defend == EPYRType::None) return 1.f;
    if (!bChart) BuildChart();
    return Chart[(int32)Attack][(int32)Defend];
}

float Effectiveness(EPYRType Attack, const FPYRSpeciesInfo& Defender)
{
    return TypeFactor(Attack, Defender.Type1) * (Defender.Type2 == Defender.Type1 ? 1.f : TypeFactor(Attack, Defender.Type2));
}

FText TypeName(EPYRType Type)
{
    static const TCHAR* Names[] = {TEXT("-"), TEXT("Normal"), TEXT("Fire"), TEXT("Water"), TEXT("Electric"), TEXT("Grass"), TEXT("Ice"),
        TEXT("Fighting"), TEXT("Poison"), TEXT("Ground"), TEXT("Flying"), TEXT("Psychic"), TEXT("Bug"), TEXT("Rock"), TEXT("Ghost"), TEXT("Dragon")};
    return FText::FromString(Names[FMath::Clamp((int32)Type, 0, 15)]);
}

FLinearColor TypeColour(EPYRType Type)
{
    switch (Type)
    {
    case EPYRType::Electric: return Hex(TEXT("ffd733"));
    case EPYRType::Normal: return Hex(TEXT("d8d0b8"));
    case EPYRType::Flying: return Hex(TEXT("9fc4f0"));
    case EPYRType::Bug: return Hex(TEXT("a8c83a"));
    case EPYRType::Poison: return Hex(TEXT("a860c8"));
    case EPYRType::Rock: return Hex(TEXT("c0a060"));
    case EPYRType::Ground: return Hex(TEXT("e0c068"));
    case EPYRType::Water: return Hex(TEXT("6ec6f0"));
    case EPYRType::Fire: return Hex(TEXT("f08030"));
    case EPYRType::Grass: return Hex(TEXT("5cb85c"));
    case EPYRType::Fighting: return Hex(TEXT("d05038"));
    case EPYRType::Psychic: return Hex(TEXT("f85888"));
    default: return Hex(TEXT("fff4d6"));
    }
}

FText BodyName(EPYRBody Body)
{
    switch (Body)
    {
    case EPYRBody::Runner: return LOCTEXT("Runner", "Runner");
    case EPYRBody::Flier: return LOCTEXT("Flier", "Flier");
    case EPYRBody::Crawler: return LOCTEXT("Crawler", "Crawler");
    case EPYRBody::Swimmer: return LOCTEXT("Swimmer", "Swimmer");
    default: return LOCTEXT("Giant", "Giant");
    }
}

FText TierName(EPYRMoveTier Tier)
{
    switch (Tier)
    {
    case EPYRMoveTier::Quick: return LOCTEXT("Quick", "Quick");
    case EPYRMoveTier::Tactical: return LOCTEXT("Tactical", "Tactical");
    default: return LOCTEXT("Big", "Big");
    }
}

FText StatName(EPYRStat Stat)
{
    switch (Stat)
    {
    case EPYRStat::Attack: return LOCTEXT("StatAttack", "ATTACK");
    case EPYRStat::Defence: return LOCTEXT("StatDefense", "DEFENSE");
    case EPYRStat::Speed: return LOCTEXT("StatSpeed", "SPEED");
    case EPYRStat::Special: return LOCTEXT("StatSpecial", "SPECIAL");
    case EPYRStat::Accuracy: return LOCTEXT("StatAccuracy", "accuracy");
    default: return LOCTEXT("StatEvasion", "evade");
    }
}

FLinearColor StatColour(EPYRStat Stat)
{
    switch (Stat)
    {
    case EPYRStat::Attack: return Hex(TEXT("e3342f"));
    case EPYRStat::Defence: return Hex(TEXT("3a7bd5"));
    case EPYRStat::Speed: return Hex(TEXT("5cb85c"));
    case EPYRStat::Special: return Hex(TEXT("c060e0"));
    case EPYRStat::Accuracy: return Hex(TEXT("e0a030"));
    default: return Hex(TEXT("6ec6f0"));
    }
}

float MaxHp(const FPYRSpeciesInfo& S, int32 Level)
{
    // Gen 1: floor((Base + DV) * 2 * L / 100) + L + 10, HP DV 8.
    return FMath::FloorToFloat((S.BaseHp + 8) * 2.f * Level / 100.f) + Level + 10;
}

float StatAt(const FPYRSpeciesInfo& S, EPYRStat Stat, int32 Level)
{
    int32 Base = S.BaseSpecial, Dv = 8;
    switch (Stat)
    {
    case EPYRStat::Attack: Base = S.BaseAttack; Dv = 9; break;
    case EPYRStat::Defence: Base = S.BaseDefence; break;
    case EPYRStat::Speed: Base = S.BaseSpeed; break;
    default: break;
    }
    return FMath::FloorToFloat((Base + Dv) * 2.f * Level / 100.f) + 5;
}

float StageMultiplier(int32 Stage)
{
    static const float Table[] = {.25f, .28f, .33f, .40f, .50f, .66f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 3.5f, 4.f};
    return Table[FMath::Clamp(Stage, -6, 6) + 6];
}

int32 XpForLevel(EPYRGrowth Group, int32 Level)
{
    if (Level <= 1) return 0;
    // m4: Yellow's own table from exp.json when it is loaded.
    const TArray<int32>& Table = XpTables[FMath::Clamp(int32(Group), 0, 3)];
    if (Table.IsValidIndex(Level)) return Table[Level];
    const double N = Level;
    switch (Group)
    {
    case EPYRGrowth::MediumSlow: return FMath::Max(0, int32(FMath::FloorToDouble(1.2 * N * N * N - 15 * N * N + 100 * N - 140)));
    case EPYRGrowth::Fast: return int32(FMath::FloorToDouble(4 * N * N * N / 5));
    case EPYRGrowth::Slow: return int32(FMath::FloorToDouble(5 * N * N * N / 4));
    default: return int32(N * N * N);
    }
}

int32 XpForLevel(FName SpeciesId, int32 Level)
{
    const FPYRSpeciesInfo* S = Species(SpeciesId);
    return XpForLevel(S ? S->Growth : EPYRGrowth::MediumFast, Level);
}

int32 XpYield(FName Foe, int32 FoeLevel, bool bTrainer, int32 Participants)
{
    const FPYRSpeciesInfo* S = Species(Foe);
    const int32 Base = S ? S->BaseExp : 50;
    int32 Total = Base * FoeLevel / 7;
    if (bTrainer) Total = Total * 3 / 2;
    return FMath::Max(1, Total / FMath::Max(1, Participants));
}

TArray<FName> MovesLearned(FName SpeciesId, int32 FromLevel, int32 ToLevel)
{
    TArray<FName> Out;
    if (const FPYRSpeciesInfo* S = Species(SpeciesId))
        for (const FPYRLearn& L : S->Learnset)
            if (L.Level > FromLevel && L.Level <= ToLevel) Out.AddUnique(L.Move);
    return Out;
}

TArray<FName> DefaultMoves(FName SpeciesId, int32 Level)
{
    // source: Yellow (engine/pokemon/evos_moves.asm, WriteMonMoves). A fresh wild or trainer Pokemon knows its own
    // species' level-1 moves, then each learnset move up to its level in order: a move it already knows is skipped,
    // and a fifth pushes out the oldest. (A Pokemon Red raises and evolves keeps its moves in FPYRMon::Carried.)
    TArray<FName> Known;
    if (const FPYRSpeciesInfo* S = Species(SpeciesId))
        for (const FPYRLearn& L : S->Learnset)
        {
            if (L.Level > Level || Known.Contains(L.Move)) continue;
            if (Known.Num() >= 4) Known.RemoveAt(0);
            Known.Add(L.Move);
        }
    return Known;
}

FName EvolutionAt(FName SpeciesId, int32 Level)
{
    const FPYRSpeciesInfo* S = Species(SpeciesId);
    return S && S->EvolveLevel > 0 && Level >= S->EvolveLevel && Species(S->EvolvesInto) ? S->EvolvesInto : NAME_None;
}

float Damage(int32 Level, float Attack, float Defence, float Power, float Stab, float Factor, float Random)
{
    if (Power <= 0 || Factor <= 0) return 0;
    const float A = FMath::Clamp(FMath::FloorToFloat(Attack), 1.f, 999.f), D = FMath::Clamp(FMath::FloorToFloat(Defence), 1.f, 999.f);
    float Base = FMath::FloorToFloat(FMath::FloorToFloat(FMath::FloorToFloat(2.f * Level / 5.f + 2.f) * Power * A / D) / 50.f) + 2.f;
    Base = FMath::FloorToFloat(Base * Stab);
    Base = FMath::FloorToFloat(Base * Factor);
    if (Random < 1.f && Base > 1.f) Base = FMath::FloorToFloat(Base * Random);
    return FMath::Max(1.f, Base);
}

float CatchChance(int32 CatchRate, float Hp, float MaxHp, EPYRStatus Status)
{
    // Gen 1 Poke Ball: caught when R1 < S; else fails when R1 - S > rate; else caught when R2 <= F.
    const int32 S = Status == EPYRStatus::None ? 0 : Status == EPYRStatus::Sleep ? 25 : 12;
    const int32 Max = FMath::Max(1, FMath::RoundToInt(MaxHp));
    const int32 Cur = FMath::Max(0, FMath::RoundToInt(Hp));
    const int32 F = FMath::Min(255, (Max * 255 / 12) / FMath::Max(1, Cur / 4));
    const float PassRate = FMath::Min(256 - S, CatchRate + 1) / 256.f;
    return FMath::Clamp(S / 256.f + PassRate * (F + 1) / 256.f, 0.f, 1.f);
}
}

using namespace PYRData;

float FPYRSpeciesInfo::FieldHeightCm() const
{
    if (Body == EPYRBody::Giant || HeightM >= 1.f) return HeightM * 100.f;
    return FMath::Max(45.f, HeightM * 150.f);
}

float FPYRSpeciesInfo::BattleHeightCm() const
{
    if (Body == EPYRBody::Giant) return HeightM * 100.f;
    return FMath::Max(120.f, FieldHeightCm());
}

float FPYRSpeciesInfo::MoveSpeed() const
{
    switch (Body)
    {
    case EPYRBody::Runner: return 700.f;
    case EPYRBody::Flier: return 550.f;
    case EPYRBody::Crawler: return 350.f;
    case EPYRBody::Swimmer: return 600.f;
    default: return 220.f;
    }
}

float FPYRSpeciesInfo::LandSpeed() const { return Body == EPYRBody::Swimmer ? 150.f : MoveSpeed(); }

float FPYRSpeciesInfo::TurnRate() const { return Body == EPYRBody::Giant ? 45.f : Body == EPYRBody::Crawler ? 540.f : Body == EPYRBody::Swimmer ? 720.f : 900.f; }

float FPYRSpeciesInfo::CapsuleRadius() const
{
    if (Body == EPYRBody::Giant) return 170.f;
    if (Body == EPYRBody::Crawler) return 34.f;
    return BattleHeightCm() * .3f;
}

float FPYRSpeciesInfo::CapsuleHalfHeight() const
{
    if (Body == EPYRBody::Giant) return 260.f;
    const float H = BattleHeightCm();
    // Crawlers keep their top under 70 cm, so a wide sweep at hip height passes over them.
    if (Body == EPYRBody::Crawler) return 34.f;
    return FMath::Max(CapsuleRadius(), H * .5f);
}

float FPYRSpeciesInfo::HoverHeight() const { return Body == EPYRBody::Flier ? 150.f : 0.f; }

namespace PYRSpecies
{
const FPYRSpeciesInfo* Find(FName Id) { return PYRData::Species(Id); }
const FPYRMoveInfo* FindMove(FName Id) { return PYRData::Move(Id); }

TArray<FName> MovesFor(const FPYRMonSpec& Spec)
{
    if (Spec.Moves.Num() > 0)
    {
        TArray<FName> Out;
        for (const FName& M : Spec.Moves) if (PYRData::Move(M) && Out.Num() < 4) Out.AddUnique(M);
        if (Out.Num() > 0) return Out;
    }
    // source: Yellow (pret/pokeyellow data/trainers/special_moves.asm). Gym leaders' Pokemon get moves they don't learn
    // by that level: Brock's Onix 12 has Bind in slot 3 and Bide (TM34) in slot 4; Misty's Starmie 21 has Bubblebeam
    // (TM11) in slot 4. Brock's Geodude 10 has no special move, so it knows only Tackle.
    if (Spec.Species == Onix && Spec.Level == 12) return {Tackle, Screech, Bind, Bide};
    if (Spec.Species == Starmie && Spec.Level == 21) return {Tackle, WaterGun, Harden, BubbleBeam};
    return DefaultMoves(Spec.Species, Spec.Level);
}

FString AssetId(FName Species)
{
    const FPYRSpeciesInfo* S = PYRData::Species(Species);
    return (S ? S->Id : Species).ToString().ToLower().Replace(TEXT("-"), TEXT("_"));
}
}

#undef LOCTEXT_NAMESPACE
