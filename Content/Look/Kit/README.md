# The anime material kit (m4v)

Every painted surface of Kanto, the rooms, the battle stages and Oak's opening is one of the kit's surface families,
so a new area (m7, m8) gets the 1997-anime look by naming surfaces, and its light by adding a row to `areas.json`.

## Surfaces
`/Game/Look/Kit/MI_Kit_<Surface>`, each a material instance of `M_AnimeKit` with its painted texture
`/Game/Look/Kit/Textures/T_Kit_<Surface>` (512 px, seamless, mipmapped; the paint in RGB and its ink lines in alpha).

| Surface | What it is | Used for |
|---|---|---|
| WallWood | white clapboard, ink seams, a shadow under each board | houses, Centres, Marts, the lab; public rooms' walls |
| WallPlaster | cream plaster, soft brush | home interiors |
| WallStone | cut stone blocks | Pewter's houses, gates, museum plinths, gym mazes |
| WallBrick | sandstone brick, ink mortar | the museum and gyms outside, chimneys |
| FloorWood | honey planks | wooden floors, bridges, stairs, trims and skirting |
| FloorTile | big two-tone tiles, grout | Centre (pink), Mart (blue), gates, the pool walkway, Oak's battle stage |
| FloorStone | cracked sandstone slabs | gym floors, Brock's arena |
| RoofRed / RoofBlue / RoofGrey / RoofBrown | barrel tiles in courses, inked lips | roofs by family, tinted per building |
| PathDirt | sandy dirt, inked pebbles | paths, the ring's worn gap |
| PathPaving | cobbles | (paved squares) |
| GrassShort | painted blade tufts | lawns, ledge tops, plateaus, meadows, battle ground |
| GrassTall | upright blades, sways (M_AnimeKitSway) | tall grass tufts |
| Flowers | petal dabs | flower beds, vases |
| RockGrey | faceted rock, ink cracks | outdoor rock, statues, boulders, Brock's hall, far mountains |
| RockCave | blue-grey rock | Mt. Moon's walls and stalagmites, the cave stage |
| CaveFloor | blue-grey flagstones, gravel | Mt. Moon's floor and walkways |
| Water | painted depths, caustic net | ponds, the sea, canals, the pool |
| Sand | soft ripples | beaches, the battle platforms' tops, the ring's stones |
| TreeCanopy | leaf clumps, inked undersides | tree crowns, bushes, forest masses |
| TreeTrunk | bark | trunks, posts, beams |
| Fence | white painted posts | Yellow's bollard fences |
| Ledge | earth strata | ledge faces, battle platform rims |
| Sign | signboard planks | sign boards and plates |
| Counter | upright wood panels | counters, shelves, tables, doors |
| Carpet | woven fabric | door mats, carpets, beds |
| Paint | plain painted colour | props, books, plates, clouds (anything coloured with no material of its own) |
| Glass | window glass with a shine | windows, screens, glass cases |
| Metal | painted machine panels | PCs, machines, rails |
| Ink | the outline ink itself | window and door rims, letters, the dark past a doorway |

The generated props (`/Game/Props/<id>`) are drawn through the kit too: `/Game/Look/Kit/Props/MI_KitProp_<id>`, their own
pictures on their own UVs with the kit's flat tones (`PYRKit::ApplyProp`, called by `PYR::FitStaticBody` for props only).
Characters and Pokemon keep their own materials.

## M_AnimeKit's parameters
- `Base` (texture): the painted surface (RGB) and its ink (alpha).
- `Tiling` (cm): the texture repeats every Tiling cm, projected in world space from the three axes (any box, cylinder,
  sphere or roof tiles without UVs); `BlendSharpness` how hard the axes switch (12 on roofs, so courses never double).
- `InkStrength` (0..1): how dark the ink lines draw, in `InkColor` (#1f1f2e, the outlines' colour).
- `CelSteps` (0 off): flattens the paint's own light and dark into that many flat tones. The two-tone light and shade
  itself is M_ToonPost's step (`Threshold`), the same for the characters, so surfaces and cast always agree.
- `Tint` (default white): multiplies the paint. `PYRKit::Get(Ctx, Surface, Colour)` sets it to Colour / `PaintAverage`
  (the texture's average linear colour, written by the authoring script), so a surface takes the place's palette colour
  and keeps its painted detail; `PYRKit::AsPainted` keeps the texture's own colours.
- `UseMeshUV`, `InkFromAlpha`: the props' variant (own UVs, no ink channel).
- `Detail` (0..1): how much of the paint's own light and dark is kept (the ground at about half reads calm at distance);
  `RimInk`: ink where a rounded surface turns edge-on; `ShadeAmount`/`ShadeDir`/`ShadeColor`: a painted two-tone shade
  side (posts, trunks). `InkStrength` above 1 keeps thin painted lines visible in the mips.
- `M_AnimeKitSway` (tall grass) and `M_AnimeKitFoliage` (masked, scalloped crowns; available, not used by default).

## Picking surfaces in a new area
```cpp
#include "World/PYRKit.h"
PYRKit::Apply(Component, EPYRKit::WallBrick, PYRColor::Hex(0xd9a86a));    // a sandstone wall tinted to the area's colour
PYRKit::Apply(Grass, EPYRKit::GrassShort, PYRKit::AsPainted);             // the painted grass as it is
```
In the world builder (`Kanto/YellowWorldLook.cpp`) every piece goes through `FKit::Get(Shape, Surface, Colour)` or
`FKit::Box(Center, Size, Surface, Colour)`; the battle stages (`Stage/PYRStage.cpp`) through `Piece(..., Surface, Colour)`.
`M4vKitCoverage` fails for any drawn surface that isn't the kit's, the sky's or the cast's.

## The light and colour per area: `areas.json`
`World/PYRAreaLook` reads it at runtime (staged with the package). Each area layers over its `base` and `defaults` (the
m4u look):
- `post`: M_ToonPost's parameters. The m4u ones (`Threshold`, `LitTone`, `ShadeTone`, `ShadeTint`, `OutlineWidth`,
  `DepthEdge`, `NormalEdge`, `FxKeep`, `Ink`, `Haze`, `HazeStart`, `HazeEnd`, `HazeAmount`: the haze is the area's fog) and
  the m4v grade: `GradeTint`, `ShadowTint`, `SkyTint`, `Saturation`, `Contrast`, `Warmth`, `InkWidth`, `InkStrength`,
  `DappleAmount`/`DappleScale` (leaf shade on the ground), `GlowAmount`/`GlowRadius` (a pool of light round Red, darker
  beyond; round the stage's centre in a fight), `RayAmount`/`RayColor`/`RaySource` (painted light shafts),
  `Vignette`, `Grain`. Colours are `[r, g, b]` linear or `"#rrggbb"`. All in M_ToonPost's one pass.
- `sun`: `rotation` [pitch, yaw, roll], `color`, `intensity`; `sky_light`, `sky_light_color`; `sky`: M_Sky's `top`
  and `horizon`, and the painted sky: `painted` (0..1 over the gradient; at 0.5 or more the dome's cloud pieces hide),
  `painted_tint`, `painted_offset` (lowers it toward the horizon), `painted_scale` and `texture` (another texture painted
  round the dome: the forest's leaf canopy); `volume`: `vignette`, `bloom`.
- More post values a new area can use: `ToneColor`/`ToneAmount` (the light's colour on lit surfaces), `GroundInk`
  (ink where lawns meet paths), `GrassLit`/`GrassInk` (tall grass two-tone and outlined), `DappleCover`, `RayEdge`.
- `maps`: which area each Game Boy map is, first match wins (`ROUTE_*`, `*_POKECENTER`, `*` for the rest);
  `stages`: which area each battle stage is (`grass`, `forest`, `cave`, `indoor`, `pewter-gym`, `cerulean-gym`).
A new place adds a row to `areas` and to `maps`; `AYellowWorld::SetFocus` switches the look when Red's map changes
(blending outdoors, snapping through a door) and a fight switches to its stage's area.

## Files
- `kit.json`: every surface's texture, tiling, ink strength, cel steps, blend sharpness, average colour and source;
  the props. Written by `Tools/look/paint_kit.py --kit-json`.
- `look-frames.json`: the 16 look frames' staging and fixed cameras (the look tests).
- `baseline.json`, `cast-hashes.json`, `hud-m4u.json`: the m4u records the m4v tests compare against.

## Rebuilding the kit
1. `python Tools/look/paint_kit.py --out build/kit/textures --kit-json Content/Look/Kit/kit.json`
   (standard library only; deterministic).
2. Import `build/kit/textures/T_Kit_*.png` into `/Game/Look/Kit/Textures` in the editor (the list is in
   `build/kit/textures/imports.json`).
3. `UnrealEditor-Cmd <uproject> -run=pythonscript -script=Source/PokemonYellowReimagine/World/Editor/author_kit.py`
   then `.../author_look.py` (M_ToonPost's grade). Both print a READY line.
