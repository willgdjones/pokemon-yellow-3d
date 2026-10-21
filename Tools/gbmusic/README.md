# gbmusic: the Game Boy Yellow tunes, remade

This folder renders the Pokemon Yellow music, jingles and menu sounds from the
[pret/pokeyellow](https://github.com/pret/pokeyellow) disassembly into `../../YellowMusic/`. The notes, lengths,
tempo, loops, vibrato and pitch slides are the Game Boy's. Only the instruments are new.

## Re-render
```
python Tools/gbmusic/render.py            # everything (about 2.5 min)
python Tools/gbmusic/render.py --only routes1,sfx_ledge
python Tools/gbmusic/verify.py            # the checks; exits nonzero on failure
```
- **Python.** Any Python 3 runs the scripts. They need numpy, and scipy is not used. If the Python has no numpy,
  the scripts re-launch themselves under one that does: `PYR_PYTHON`, otherwise Blender's bundled Python
  (in its default install folder). The engine's Python has no numpy, but it works through that re-launch.
- **Source.** The scripts look for pokeyellow in `.cache/pokeyellow/` in the project folder. If it isn't there, they
  clone it and check out the pinned commit (`e89ead154b9968aa50eed9328ff2b38b6c194382`, in `pyenv.py`). The clone is
  never committed.
- **Deterministic.** Rendering is deterministic: fixed seeds and no dither. `verify.py` re-renders three songs and
  requires a bit-exact match with the committed files.

## Files
| File | What it does |
|---|---|
| `gbasm.py` | Parses `audio/music/*.asm`, `audio/sfx/*.asm` and `audio/headers/*.asm`: the macros, labels, the pitch table (`audio/notes.asm`) and frequency registers. |
| `gbengine.py` | A frame-exact model of `audio/engine_1.asm` at 59.7275 Hz. It covers `tempo`, `note_type`/`drum_speed`, `octave`, `note`, `rest`, `drum_note`, `sound_call`/`sound_loop`/`sound_ret` and the fractional note-delay counter. It also models `vibrato` (delay, extent and rate on the frequency low byte), `pitch_slide` (including the engine's borrow and step bugs), `toggle_perfect_pitch`, `execute_music` and the sfx notes and `pitch_sweep`. It finds the loop by exact engine-state recurrence per channel, and the song's loop is the LCM of the channel loops. |
| `gbsynth.py` | The instruments, drum kit, reverb, BS.1770 loudness and peak limiter. |
| `gbchip.py` | A band-limited Game Boy APU (pulse duty, envelope, channel-1 sweep, noise LFSR) for the UI sounds. |
| `render.py` | The song list, the loop cut, normalisation, WAVs, `music.json` and `manifest.json`. |
| `verify.py` | The checks: formats, loop points against the engine, channel-2 pitches and lengths detected in the render, loudness and size. |
| `manifest.json` | For each id: the pokeyellow constant and header, source commit, maps and events that use it, intro and loop lengths, LUFS, peak and any warnings. |

## Instruments
- **Channel 1 (pulse):** a musette accordion (two reeds at plus and minus 5.5 cents, bellows tremolo).
- **Channel 2 (pulse):** a tin-whistle lead (breath and chiff) with a soft double 7 cents sharp.
- **Channel 3 (wave):** a plucked upright bass at the wave channel's real pitch, plus a sine sub layer an octave
  down (at the fundamental for notes under 98 Hz). Its level follows the 100/50/25% setting.
- **Channel 4 (noise):** an acoustic kit.
  - Each drum id maps to a piece, with the instrument's Game Boy volume as its velocity. The names come from
    pokecrystal's drum table for the same noise instruments.
  - Ids 1-4 and 17 are snares; 18-19 brushed snares; 8, 12, 15 and 16 closed hats; 13 an open hat; 9, 10 and 11
    high, mid and low toms; 14 the kick; 5 a crash; 6-7 rim clicks.
- **Loudness:** each note follows the Game Boy volume envelope. Lead and accordion decays keep a sustain floor
  (45% and 35%), so held notes still sound held.
- **Mix:** a light shared reverb (RT60 1.7 s low, 0.8 s high; small sends; the bass stays nearly dry). Each file is
  normalised to -14 LUFS integrated, with peaks limited to -1 dBFS.
- **UI sounds** (`sfx_*`) are the Game Boy's own chip sounds, rendered dry and band-limited and normalised to a
  maximum momentary loudness of -16 LUFS.

## Output contract
- **Audio.** `YellowMusic/<id>.wav` is 16-bit PCM, mono, 32000 Hz. `YellowMusic/music.json` is
  `{id:{file, loop_start, loop_end, jingle}}`, with loop points in samples.
- **Ids.** An id is the pokeyellow `MUSIC_*` (or `SFX_*`) constant, lowercased without the prefix. Menu and field
  sounds are `sfx_<name>`.
- **Looping songs.** A file holds the intro once, then one loop body, then the reverb tail (up to 2 s, cut once
  silent). Play `[0, loop_end)`, then repeat `[loop_start, loop_end)`. The body is taken from the second pass,
  so it starts with the reverb of its own end and the wrap is seamless. `loop_start` is where the whole engine
  state first repeats. That can be partway through the song: for example, Pallet Town's channel 1 plays its first
  pass at volume 12 and later passes at 11.
- **Jingles.** A jingle has `loop_start = loop_end = 0`: play it once. `evolution` shares `safari_zone.wav`,
  because the evolution scene plays `MUSIC_SAFARI_ZONE` (`engine/movie/evolution.asm`).
- **The one assumption.** `PkmnHealed` channel 1 never sets an octave, so the Game Boy uses whatever the previous
  song left. The render assumes octave 4 (the Pokecenter theme's), and it is logged as a warning in `manifest.json`.
