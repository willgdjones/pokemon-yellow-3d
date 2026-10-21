"""Render the Game Boy Yellow tunes, jingles and menu sounds into YellowMusic/.

    python Tools/gbmusic/render.py [--only id,id] [--pokeyellow PATH]

Songs are sequenced by gbengine (a frame-exact model of pokeyellow's audio engine) and
played by gbsynth with fuller instruments. Each WAV holds the intro once, one loop body
and a reverb tail; music.json gives the loop points in samples.
"""
import argparse
import json
import math
import os
import re
import sys
import time
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pyenv  # noqa: E402

pyenv.ensure_numpy()

import numpy as np  # noqa: E402

from gbasm import Program  # noqa: E402
from gbengine import Engine, loop_points, FRAME_HZ, CHAN8  # noqa: E402
import gbsynth  # noqa: E402
from gbsynth import SR, frame_to_sample  # noqa: E402
import gbchip  # noqa: E402

OUT = os.path.join(pyenv.GAME, 'YellowMusic')
TAIL_SECONDS = 2.0
SONG_LUFS = -14.0
SFX_MOMENTARY_LUFS = -16.0

# Looping songs, by pokeyellow constant.
SONGS = [
    'MUSIC_PALLET_TOWN', 'MUSIC_OAKS_LAB', 'MUSIC_ROUTES1', 'MUSIC_ROUTES2', 'MUSIC_ROUTES3',
    'MUSIC_CITIES1', 'MUSIC_CITIES2', 'MUSIC_POKECENTER', 'MUSIC_GYM', 'MUSIC_DUNGEON1',
    'MUSIC_DUNGEON2', 'MUSIC_DUNGEON3', 'MUSIC_MUSEUM_GUY', 'MUSIC_MEET_PROF_OAK', 'MUSIC_MEET_RIVAL',
    'MUSIC_MEET_MALE_TRAINER', 'MUSIC_MEET_FEMALE_TRAINER', 'MUSIC_MEET_EVIL_TRAINER',
    'MUSIC_MEET_JESSIE_JAMES', 'MUSIC_WILD_BATTLE', 'MUSIC_TRAINER_BATTLE', 'MUSIC_GYM_LEADER_BATTLE',
    'MUSIC_DEFEATED_WILD_MON', 'MUSIC_DEFEATED_TRAINER', 'MUSIC_DEFEATED_GYM_LEADER',
    'MUSIC_SAFARI_ZONE', 'MUSIC_TITLE_SCREEN',
]
# Jingles play once: (id, constant).
JINGLES = [
    ('pkmn_healed', 'MUSIC_PKMN_HEALED'), ('get_item_1', 'SFX_GET_ITEM_1'),
    ('get_item_2', 'SFX_GET_ITEM_2'), ('get_key_item', 'SFX_GET_KEY_ITEM'),
    ('level_up', 'SFX_LEVEL_UP'), ('caught_mon', 'SFX_CAUGHT_MON'),
    ('dex_evaluation', 'SFX_POKEDEX_RATING'),
]
# The evolution scene plays MUSIC_SAFARI_ZONE (engine/movie/evolution.asm); `evolution` is that
# file played once.
ALIASES = {'evolution': 'safari_zone'}
# Menu and field sound effects, rendered on the Game Boy chip: (name, constant).
SFX = [
    ('press_ab', 'SFX_PRESS_AB'), ('start_menu', 'SFX_START_MENU'), ('save', 'SFX_SAVE'),
    ('purchase', 'SFX_PURCHASE'), ('heal_machine', 'SFX_HEALING_MACHINE'), ('ball_poof', 'SFX_BALL_POOF'),
    ('go_inside', 'SFX_GO_INSIDE'), ('go_outside', 'SFX_GO_OUTSIDE'), ('ledge', 'SFX_LEDGE'),
]

# Maps from Pallet Town to Cerulean (the yellowdata scope; pokeyellow map constants).
SCOPE_MAPS = [
    'PALLET_TOWN', 'REDS_HOUSE_1F', 'REDS_HOUSE_2F', 'BLUES_HOUSE', 'OAKS_LAB',
    'ROUTE_1', 'VIRIDIAN_CITY', 'VIRIDIAN_POKECENTER', 'VIRIDIAN_MART', 'VIRIDIAN_SCHOOL_HOUSE',
    'VIRIDIAN_NICKNAME_HOUSE', 'VIRIDIAN_GYM',
    'ROUTE_22', 'ROUTE_22_GATE', 'ROUTE_2', 'ROUTE_2_GATE', 'ROUTE_2_TRADE_HOUSE', 'DIGLETTS_CAVE_ROUTE_2',
    'VIRIDIAN_FOREST_SOUTH_GATE', 'VIRIDIAN_FOREST', 'VIRIDIAN_FOREST_NORTH_GATE',
    'PEWTER_CITY', 'MUSEUM_1F', 'MUSEUM_2F', 'PEWTER_GYM', 'PEWTER_NIDORAN_HOUSE', 'PEWTER_MART',
    'PEWTER_SPEECH_HOUSE', 'PEWTER_POKECENTER',
    'ROUTE_3', 'MT_MOON_POKECENTER', 'MT_MOON_1F', 'MT_MOON_B1F', 'MT_MOON_B2F', 'ROUTE_4',
    'CERULEAN_CITY', 'CERULEAN_TRASHED_HOUSE', 'CERULEAN_MELANIES_HOUSE', 'CERULEAN_POKECENTER',
    'CERULEAN_GYM', 'BIKE_SHOP', 'CERULEAN_MART', 'CERULEAN_BADGE_HOUSE',
    'ROUTE_24', 'ROUTE_25', 'BILLS_HOUSE',
]
# Scripted plays in this stretch (pokeyellow scripts/ and engine/).
EVENT_USES = {
    'meet_prof_oak': ['PalletTown.asm: Oak stops Red at the edge of the grass'],
    'meet_rival': ['OaksLab.asm, Route22.asm, CeruleanCity.asm: the rival appears'],
    'meet_jessie_james': ['MtMoonB2F.asm: Jessie and James'],
    'museum_guy': ['PewterCity.asm (engine/overworld/auto_movement.asm): the boys who lead Red to the museum and gym'],
    'meet_male_trainer': ['a male trainer spots Red (engine/overworld/trainer_sight.asm, trainer class table)'],
    'meet_female_trainer': ['a female trainer spots Red'],
    'meet_evil_trainer': ['a Rocket or rough trainer spots Red'],
    'wild_battle': ['engine/battle/core.asm: wild battles'],
    'trainer_battle': ['engine/battle: trainer battles'],
    'gym_leader_battle': ['engine/battle: gym leaders and the rival'],
    'defeated_wild_mon': ['victory over a wild Pokemon'],
    'defeated_trainer': ['victory over a trainer'],
    'defeated_gym_leader': ['victory over a gym leader'],
    'safari_zone': ['engine/movie/evolution.asm: the evolution scene'],
    'title_screen': ['engine/movie/title.asm: the title screen'],
    'pkmn_healed': ['engine/overworld/healing_machine.asm: Nurse Joy heals the party'],
    'get_item_1': ['text sound: an item is received'],
    'get_item_2': ['text sound: a special item is received'],
    'get_key_item': ['text sound: a key item is received'],
    'level_up': ['a Pokemon grows a level'],
    'caught_mon': ['a wild Pokemon is caught'],
    'dex_evaluation': ['audio/pokedex_rating_sfx.asm: Oak rates the Pokedex'],
}


def constants(root):
    """pokeyellow constant -> header label (constants/music_constants.asm)."""
    out = {}
    with open(os.path.join(root, 'constants', 'music_constants.asm'), encoding='utf-8') as fh:
        for line in fh:
            m = re.match(r'\s*music_const\s+([A-Z0-9_]+)\s*,\s*([A-Za-z0-9_]+)', line)
            if m:
                out[m.group(1)] = m.group(2)
    return out


def song_id(const):
    return re.sub(r'^(MUSIC|SFX)_', '', const).lower()


def map_music(root):
    """map constant -> music id, from data/maps/songs.asm in map_constants order."""
    with open(os.path.join(root, 'constants', 'map_constants.asm'), encoding='utf-8') as fh:
        maps = re.findall(r'^\s*map_const\s+([A-Z0-9_]+)', fh.read(), re.M)
    with open(os.path.join(root, 'data', 'maps', 'songs.asm'), encoding='utf-8') as fh:
        songs = re.findall(r'^\s*db\s+(MUSIC_[A-Z0-9_]+)\s*,', fh.read(), re.M)
    return {m: song_id(s) for m, s in zip(maps, songs)}


# ---- timelines ---------------------------------------------------------------

def timeline(prog, header):
    """Run the engine and find where the song loops.

    Returns a dict with the engine and either kind='loop' (F1: first frame of the loop body,
    F2 = F1 + one period, F3 = F1 + two periods, all on the same event of the channel whose
    intro is longest) or kind='once' (end: the frame the last channel stops).
    """
    frames = 20000
    while True:
        eng = Engine(prog)
        eng.load(header)
        ran = eng.run(frames)
        if not any(c.active for c in eng.chans.values()):
            end = max(e['t'] for c in eng.chans.values() for e in c.events if e['kind'] == 'end')
            return {'kind': 'once', 'engine': eng, 'end': end}
        lp = loop_points(eng)
        loops = {k: v for k, v in lp.items() if v}
        if len(loops) == sum(1 for c in eng.chans.values() if c.active):
            periods = [v['period_frames'] for v in loops.values()]
            period = 1
            for p in periods:
                period = period * p // math.gcd(period, p)
            if period > 4 * max(periods):
                eng.warnings.append('channel periods %s do not share a small multiple; using the longest' % periods)
                period = max(periods)
            cstar = max(loops, key=lambda k: (loops[k]['intro_frame'], -k))
            info = loops[cstar]
            per_events = info['period_events'] * (period // info['period_frames'])
            e0 = info['intro_event']
            evs = eng.chans[cstar].events
            if e0 + 2 * per_events < len(evs):
                f1, f2, f3 = evs[e0]['t'], evs[e0 + per_events]['t'], evs[e0 + 2 * per_events]['t']
                for idx, ch in eng.chans.items():
                    if not ch.active:
                        late = [e for e in ch.events if e['kind'] in ('note', 'drum') and e['t'] >= f1]
                        if late:
                            eng.warnings.append('channel %d ends after the loop start' % (idx + 1))
                return {'kind': 'loop', 'engine': eng, 'chan': cstar, 'event': e0, 'events': per_events,
                        'period': period, 'F1': f1, 'F2': f2, 'F3': f3}
        if ran < frames:
            raise RuntimeError('%s: engine stopped early' % header)
        frames *= 2
        if frames > 400000:
            raise RuntimeError('%s: no loop found' % header)


def render_music(prog, header, inst, ir, keep=None):
    """Render a song or music-type jingle. Returns (file samples float, loop_start, loop_end, info)."""
    tl = timeline(prog, header)
    eng = tl['engine']
    if tl['kind'] == 'loop':
        limit = tl['F3']
    else:
        limit = tl['end']
    stems, total = gbsynth.render_stems(eng, limit, inst, TAIL_SECONDS, keep=keep)
    full = gbsynth.mix(stems, total, ir)
    return tl, stems, full


def cut(tl, x):
    """Intro from pass 1, loop body from pass 2 (which carries pass 1's reverb), then the tail."""
    if tl['kind'] == 'once':
        return x, 0, 0
    s1, s2, s3 = (frame_to_sample(tl[k]) for k in ('F1', 'F2', 'F3'))
    y = np.concatenate([x[:s1], x[s2:s3], x[s3:s3 + int(TAIL_SECONDS * SR)]])
    return y, s1, s1 + (s3 - s2)


def finish(tl, full, target=SONG_LUFS):
    """Loudness-normalise on the playable region (limiting the continuous render), then cut."""
    gain = 1.0
    for _ in range(4):
        y = gbsynth.limit(full * gain)
        f, ls, le = cut(tl, y)
        region = f[:le] if le else f
        lufs = gbsynth.integrated_lufs(region)
        if abs(lufs - target) < 0.05:
            break
        gain *= 10 ** ((target - lufs) / 20.0)
    return f, ls, le, lufs


def trim_tail(pcm, keep_until):
    """Drop the silent end of the reverb tail (below 2 LSB), never before keep_until."""
    loud = np.nonzero(np.abs(pcm.astype(np.int32)) >= 2)[0]
    end = max(keep_until, int(loud[-1]) + 1 if len(loud) else keep_until)
    end = min(len(pcm), end + int(0.05 * SR))
    return pcm[:end]


def write_wav(path, pcm):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def render_sfx(prog, header, ir):
    eng = Engine(prog)
    eng.load(header)
    eng.run(4000)
    end = max(e['t'] for c in eng.chans.values() for e in c.events if e['kind'] == 'end')
    total = frame_to_sample(end) + int(TAIL_SECONDS * SR)
    dry = gbchip.render_sfx(eng, total)
    # The chip sounds stay dry, as on the Game Boy; only the DC of the pulse duty is removed.
    x = gbsynth.fft_filter(dry, 25, None, 0.5)
    gain = 10 ** ((SFX_MOMENTARY_LUFS - gbsynth.max_momentary_lufs(x)) / 20.0)
    x = gbsynth.limit(x * gain)
    return eng, x, gbsynth.max_momentary_lufs(x)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', help='comma-separated ids to render (others keep their music.json entry)')
    ap.add_argument('--pokeyellow', help='path to a pokeyellow checkout')
    ap.add_argument('--out', default=OUT)
    args = ap.parse_args()

    root, commit = pyenv.pokeyellow(args.pokeyellow)
    t0 = time.time()
    prog = Program(root)
    consts = constants(root)
    inst = gbsynth.Instruments()
    ir = gbsynth.reverb_ir()
    os.makedirs(args.out, exist_ok=True)
    only = set(args.only.split(',')) if args.only else None
    music_path = os.path.join(args.out, 'music.json')
    music = {}
    if only and os.path.exists(music_path):
        with open(music_path, encoding='utf-8') as fh:
            music = json.load(fh)
    manifest_path = os.path.join(pyenv.HERE, 'manifest.json')
    manifest = {'songs': {}}
    if only and os.path.exists(manifest_path):
        with open(manifest_path, encoding='utf-8') as fh:
            manifest = json.load(fh)
    manifest['source'] = {'repo': pyenv.REPO_URL, 'commit': commit}
    manifest['format'] = {'rate': SR, 'channels': 1, 'bits': 16, 'song_lufs': SONG_LUFS,
                          'sfx_max_momentary_lufs': SFX_MOMENTARY_LUFS, 'tail_seconds': TAIL_SECONDS}
    maps = map_music(root)
    used_by = {}
    for m in SCOPE_MAPS:
        used_by.setdefault(maps[m], []).append(m)

    jobs = [(song_id(c), c, 'song') for c in SONGS] + [(i, c, 'jingle') for i, c in JINGLES] + \
           [('sfx_' + i, c, 'sfx') for i, c in SFX]
    for sid, const, kind in jobs:
        if only and sid not in only:
            continue
        header = consts[const]
        t = time.time()
        if kind == 'sfx':
            eng, x, lufs = render_sfx(prog, header, ir)
            pcm = trim_tail(gbsynth.to_pcm16(x), frame_to_sample(max(
                e['t'] for c in eng.chans.values() for e in c.events if e['kind'] == 'end')))
            ls = le = 0
            tl = {'kind': 'once', 'engine': eng}
        else:
            tl, stems, full = render_music(prog, header, inst, ir)
            f, ls, le, lufs = finish(tl, full)
            pcm = gbsynth.to_pcm16(f)
            pcm = trim_tail(pcm, le if le else frame_to_sample(tl['end']))
            eng = tl['engine']
        fname = sid + '.wav'
        write_wav(os.path.join(args.out, fname), pcm)
        jingle = kind != 'song'
        music[sid] = {'file': fname, 'loop_start': int(ls) if not jingle else 0,
                      'loop_end': int(le) if not jingle else 0, 'jingle': jingle}
        entry = {'constant': const, 'header': header, 'kind': kind, 'channels': sorted(i + 1 for i in eng.chans),
                 'seconds': round(len(pcm) / SR, 3), 'lufs': round(lufs, 2),
                 'peak_dbfs': round(20 * np.log10(max(1, int(np.abs(pcm.astype(np.int32)).max())) / 32768.0), 2),
                 'maps': used_by.get(sid, []), 'events': EVENT_USES.get(sid, []), 'warnings': eng.warnings}
        if tl['kind'] == 'loop':
            entry.update({'intro_frames': tl['F1'], 'loop_frames': tl['F2'] - tl['F1'],
                          'intro_seconds': round(ls / SR, 3), 'loop_seconds': round((le - ls) / SR, 3),
                          'loop_channel': tl['chan'] + 1})
        manifest['songs'][sid] = entry
        print('%-22s %-6s %7.2fs  %6.2f LUFS  loop %s-%s  (%.1fs)%s' % (
            sid, kind, len(pcm) / SR, lufs, ls, le, time.time() - t,
            ('  ! ' + '; '.join(eng.warnings)) if eng.warnings else ''))
        sys.stdout.flush()
    for alias, target in ALIASES.items():
        if target in music:
            music[alias] = {'file': music[target]['file'], 'loop_start': 0, 'loop_end': 0, 'jingle': True}
            manifest['songs'][alias] = {'alias_of': target, 'events': ['engine/movie/evolution.asm: '
                                        'the evolution scene plays MUSIC_SAFARI_ZONE; this id plays it once']}
    with open(music_path, 'w', encoding='utf-8', newline='\n') as fh:
        json.dump(dict(sorted(music.items())), fh, indent=2)
        fh.write('\n')
    with open(manifest_path, 'w', encoding='utf-8', newline='\n') as fh:
        manifest['songs'] = dict(sorted(manifest['songs'].items()))
        json.dump(manifest, fh, indent=2)
        fh.write('\n')
    size = sum(os.path.getsize(os.path.join(args.out, f)) for f in os.listdir(args.out))
    print('wrote %d entries, %.1f MB, in %.0fs' % (len(music), size / 1e6, time.time() - t0))


if __name__ == '__main__':
    main()
