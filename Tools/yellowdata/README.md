# yellowdata: Game Boy Pokemon Yellow, Pallet Town to Cerulean, as data

`extract.py` reads the [pret/pokeyellow](https://github.com/pret/pokeyellow) disassembly (`.asm`, `.blk`, `.bst`,
`.png`; the ROM is never built) and writes `YellowData/` in the project folder. The Unreal game builds the
world, people, menus and tables from those files at runtime, so they are the source of truth for "tile exact".
`verify.py` checks the output against the contract and against the pokeyellow sources.

Standard library Python 3 only (the build machines have no Pillow); the engine's bundled Python works:

```
set PY="C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
%PY% extract.py      # clones pokeyellow into .cache/pokeyellow if missing
%PY% verify.py       # exit 0 = every check passed; prints Pallet Town's tiles
```

Options: `--pokeyellow <clone>` and `--out <dir>` (extract), `--data <dir>` and `--pokeyellow <clone>` (verify).
The clone lives under `.cache/` (ignored, never committed); `manifest.json` records its commit.

## Files

| File | What it is |
| --- | --- |
| `extract.py` | the extractor; `IN_SCOPE` lists the maps |
| `pret.py` | reads constants, map headers/objects/blocks, tilesets, CGB palettes |
| `textlib.py` | decodes text (charmap, `text_far`, `text_ram`, conditionals) and follows text scripts |
| `events_pikachu.py` | `events.json` (from the hand table) and `pikachu.json` + faces |
| `pngio.py` | PNG read/write with zlib |
| `verify.py` | the checks |
| `tilesheet.py` | helper used to write `looks.json`: labelled tile sheets of every in-scope tileset |
| `looks.json` | hand table: look class per 8x8 tile id, per tileset graphics |
| `text_handlers.json` | hand table: texts printed by the engine's shared text scripts (nurse, mart, PC, item balls, trade) |
| `item_use.json` | hand table: field/battle use per item-use handler (`engine/items/item_effects.asm`) |
| `events.json` | hand table: the scripted gifts and events, each with the script lines it came from |

Each hand table has a `_comment`/`_source` naming the pokeyellow files it was read from.

## Coordinates

- **1 tile = one 16x16-pixel walking square = 1 m.** A block (`.blk` entry) is 2x2 tiles. Map `width`/`height` are
  in tiles; x grows east, y grows south (Game Boy order). `x,y` of warps, signs, people and items are tiles.
- **World frame:** Pallet Town's origin is 0,0. Every outdoor map is placed by a breadth-first search over the map
  connections (`data/maps/headers/*.asm`), and each offset is checked both ways. Interiors, gates, Viridian Forest
  and the Mt. Moon floors are packed in rows from y = 1000, 10 tiles apart, rows at most 200 tiles wide.
- **Connections:** `connections.<dir> = {map, offset}`; `offset` is in tiles along the shared edge: for north/south
  it is the other map's origin x minus this map's origin x, for east/west the difference in y.

## Tile legend (`tiles`)

Collision uses the bottom-left 8x8 tile of each square, exactly as the engine does (`hlcoord 8, 9`).

| Char | Meaning | Source |
| --- | --- | --- |
| `.` | walkable | tile in the tileset's `data/tilesets/collision_tile_ids.asm` list |
| `g` | tall grass (walkable, wild encounters) | the tileset header's grass tile |
| `c` | cave floor (walkable; the whole floor has encounters) | walkable square in a CAVERN map |
| `#` | solid | not in the collision list |
| `w` | water (surfable) | tile $14, or shore tiles $48/$32, in a water tileset (`IsNextTileShoreOrWater`) |
| `v` `<` `>` | ledge, hopped south / west / east | `data/tilesets/ledge_tiles.asm`: ledge tile with the standing tile behind it |
| `d` | warp | a `warp_event` position |
| `=` | counter (talk across it) | the tileset header's counter tiles |
| `x` | Cut tree | `engine/overworld/cut.asm` ($3D overworld, $50 gym) |

`blocked_edges: [{x, y, dir}]` (extra) lists steps the engine forbids between two walkable squares of different
height (`data/tilesets/pair_collision_tile_ids.asm`: Mt. Moon's raised floors, the forest).

## Look legend (`looks`)

One code per tile, chosen from the four 8x8 tiles through `looks.json`: walkable squares take the first ground-type
class by priority `D S P u m f t G c y s p o`, solid squares prefer object classes over ground. Ledges are always `L`.

| Code | Look | Code | Look |
| --- | --- | --- | --- |
| `p` | path, town ground | `B` | building wall |
| `s` | short grass | `R` | roof |
| `f` | flowers | `D` | door, doorway, cave mouth, gate exit |
| `G` | tall grass | `n` | window |
| `T` | tree (and the small Cut tree) | `S` | sign (incl. Poke Mart / Pokemon Center signs) |
| `F` | fence | `C` | cave wall |
| `W` | water | `c` | cave floor |
| `y` | sand / dirt road (Route 22) | `u` | ladder or stairs |
| `r` | rock or cliff | `o` | indoor floor |
| `L` | ledge | `m` | mat, exit mat, gym spinner/stop tile |
| `k` | counter | `b` | bookshelf, shelf or machine |
| `t` | table and other furniture (chairs, beds, benches) | `P` | PC |
| `h` | plant or pot | `l` | statue, poster, exhibit, decoration |
| `K` | cave: raised light walkway (solid; m4r) | `V` | cave: the black void (solid; m4r) |
| `H` | bridge planks over water (Nugget Bridge; m4r) | `Z` | a Cut tree in the scenery past the edges (m4r) |
| `?` | other (none in scope) | | |

## Output contract

All JSON is UTF-8, pretty-printed with sorted keys. Fields beyond the contract are marked *extra*.

- `manifest.json`: `{version: 1, source: {repo, commit}, maps: [MAP_ID...]}`.
- `maps/<MAP_ID>.json`: `id`, `name` (town-map name for towns and routes, the pokeyellow label spaced out for
  interiors), `width`, `height`, `outdoor`, `cave`, `origin {x,y}`, `tileset`, `music` (`MUSIC_*` lowercased without
  the prefix, from `data/maps/songs.asm`), `palette` (the `PAL_*` constant chosen by `SetPal_Overworld`: towns their
  own, routes `PAL_ROUTE`, caves `PAL_CAVE`, buildings their outdoor map's), `encounter_rate` (grass/cave rate or 0),
  `connections`, `tiles`, `looks`, `warps`, `signs`, `npcs`, `items`, `hidden`; *extra*: `label`, `border_block`,
  `blocked_edges`, `hidden_events`, `looks_unknown`, `source`.
  - `warps: [{x, y, to_map, to_warp}]`, `to_warp` 1-based as in pokeyellow. `LAST_MAP` exits are resolved to the
    outdoor map they lead to and marked `last_map: true`; warps out of scope keep `to_map` and add `out_of_scope: true`.
  - `signs: [{x, y, text, texts}]`.
  - `npcs: [{id, x, y, sprite, movement, facing, text, trainer?}]` from `data/maps/objects/*.asm`. `id` is the object
    constant (the object order), `sprite` the `SPRITE_*` constant, `movement` `stay`/`walk`, `facing` the object's
    direction or walk range lowercased (`down`, `left`, `none`, `any_dir`, `up_down`, `left_right`). `text` is the
    map's `TEXT_*` id; *extra* `texts` lists every text that person's script can show, in source order (the first is
    `text`), `text_handler` the script label, `script` the shared engine script (nurse, mart, PC...), `toggle` and
    `hidden_at_start` for objects the story shows or hides (`data/maps/toggleable_objects.asm`).
    `trainer: {class, party, sight, event, battle_text, end_text, after_text}`: class without `OPP_`, `party` is the
    1-based party number in `trainers.json`, `sight` the trainer header's range in tiles.
  - `items: [...]` item balls, same fields as npcs plus `item`. `npcs` + `items` = the map's `object_event`s.
  - `hidden: [{x, y, item}]` hidden items; *extra* `hidden_events: [{x, y, kind, arg}]` (PCs, bookshelves, posters...).
- `text.json`: `{text_id: string}`. Ids are `TEXT_*` constants (a map's text pointers) and pokeyellow labels; local
  labels are `Parent.local`. Game Boy caps are kept; `line`/`cont`/`next` are `\n`, `para`/`page` are `\f`;
  `<PLAYER>` and `<RIVAL>` stay as tokens, `#` becomes `POKé`, `text_ram wX` becomes `{str:wX}`, `text_decimal`/`text_bcd wX`
  become `{num:wX}`. A Pokedex entry ends with the period the engine prints.
- `species.json`, keyed by species constant (151): `dex` (number), `name`, `types`, `base {hp, attack, defense,
  speed, special}`, `catch_rate`, `base_exp`, `growth`, `learnset [[level, MOVE]]`, `evolutions [{method: level|item|trade,
  param, to}]`, `tm` (`TM01`..`HM05`), and **`dex_entry: {category, height_m, weight_kg, text}`** (the contract's
  second `dex` object; a JSON object can't hold both). Heights and weights are converted from the game's feet/inches
  and pounds, which are kept as `height_ft_in` and `weight_lb`. *Extra*: `internal_id`, `level1_moves`.
- `moves.json` `{MOVE: {id, name, type, power, accuracy, pp, effect}}`; `types.json` `{types, chart: {attacker: {defender:
  multiplier}}}` (unlisted pairs are 1.0); `items.json` `{ITEM: {id, name, price, key, field_use, battle_use, tm?|hm?, move?}}`.
- `marts.json` `{MAP_ID: [ITEM...]}` Yellow stock (`data/items/marts.asm`).
- `wild.json` `{MAP_ID: {grass_rate, water_rate, grass: [[level, SPECIES] x10], water, slot_chances_256}}`.
- `trainers.json` `{CLASS: {name, base_money, parties: [[{level, species, moves?}]]}}`; `parties[party-1]` is the
  object's party. `base_money` is the prize per level of the last Pokemon (pokeyellow stores 1500 in BCD for 15);
  `moves {slot: MOVE}` are Yellow's special trainer moves (`data/trainers/special_moves.asm`).
- `trades.json`, `events.json`, `pikachu.json`, `exp.json` (`rates.<growth>[level-1]`, clamped at 0 for level 1).
- `minimap/<MAP_ID>.png`: the real tiles, 16 px per tile, in the map's CGB palette (`CGBBasePalettes`).
  `minimap/kanto_outdoor.png` stitches every outdoor map at its origin; `kanto_outdoor.json` `{px_per_tile: 16,
  origin_px: {x, y}}` gives world 0,0 in pixels (plus `size_px`, `maps`). Since m4r it also draws what the Game Boy
  shows past the edges, `ring` (6) tiles deep: the out-of-range neighbours' own tiles (`scenery`: Route 21, 23, 5, 9)
  and every outdoor map's border block. `minimap/ringed/<MAP>.png` is each room (and Viridian Forest) with its border
  block round it, `ring` tiles deep (`minimap/ringed.json`). The plain `minimap/<MAP>.png` stay the map alone (the QA
  harness compares shots with them).
- `sprites/red_minimap.png`: 32x64, columns = standing / walking frame, rows = facing down, up, left, right (right is
  left mirrored), coloured through the overworld OBP0 over `PAL_ROUTE`, transparent background.
- `sprites/people_<PAL>.png` and `people.json` (m4r, the minimap's people): every overworld sprite in
  `data/sprites/sprites.asm`, one 16-px row each (`rows {SPRITE_*: row}`), 8 frames per row in red_minimap's order
  (down, down step, up, up step, left, left step, right, right step; sheets with no walking frames repeat the standing
  one, still sprites repeat their one picture). One atlas per map palette in scope: on the Game Boy Color the object
  palettes follow the map's palette (`InitCGBPalettes`), through the overworld OBP0.
- `townmap/townmap.png` (160x144, the Game Boy screen) and `townmap.json` `{MAP_ID: {x, y}}` in 8-px town-map tiles
  from `data/maps/town_map_entries.asm`; interiors use their indoor group's entry.
- `pikachu/<mood>.png` (40x40, `PAL_PIKACHU_PORTRAIT`) named in `pikachu.json`.

## Notes on Yellow
- The task's `CERULEAN_TRADE_HOUSE` is Red/Blue's constant; in Yellow that house is `CERULEAN_MELANIES_HOUSE`
  (Melanie and her Bulbasaur).
- Kept as the game does it: Viridian Forest's leaving sign shows Trainer Tips 1 (`scripts/ViridianForest.asm` loads the
  wrong pointer), and Oak's Lab uses the DOJO tileset.
