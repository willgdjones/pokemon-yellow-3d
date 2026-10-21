"""Check YellowMusic against the output contract and against the pokeyellow sources.

    python Tools/gbmusic/verify.py

1. Every id in music.json has its file, and every file is 32000 Hz mono 16-bit PCM.
2. Loop points lie inside the file and on a note boundary: loop_start is the first frame of
   the loop body and loop_end is the same note one full period later (re-derived here from
   the asm by the engine model).
3. For pallet_town, routes1 and wild_battle, the first 16 channel-2 notes parsed from the asm
   are printed next to the pitches and lengths detected in the rendered channel-2 part
   (the whistle lead, dry), and must match. The song is then re-rendered and compared with
   the committed WAV, so the part that was measured is the one in the file.
4. Loudness of every file, and the total size of YellowMusic (< 200 MB).
Exits nonzero on any failure.
"""
import json
import os
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pyenv  # noqa: E402

pyenv.ensure_numpy()

import numpy as np  # noqa: E402

from gbasm import Program, PITCH_NAMES, frequency_register, pulse_hz  # noqa: E402
from gbengine import CHAN2, FRAME_HZ  # noqa: E402
import gbsynth  # noqa: E402
from gbsynth import SR, frame_to_sample  # noqa: E402
import render  # noqa: E402

PITCH_SONGS = ['pallet_town', 'routes1', 'wild_battle']
FIRST_NOTES = 16
MAX_MB = 200.0

failures = []


def fail(msg):
    failures.append(msg)
    print('  FAIL ' + msg)


def read_wav(path):
    with wave.open(path, 'rb') as w:
        fmt = (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getcomptype())
        data = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
    return fmt, data


def envelope_ms(x):
    """RMS over 2 ms, one value per millisecond."""
    hop = SR // 1000
    n = len(x) // hop
    sq = (x[:n * hop] ** 2).reshape(n, hop).mean(axis=1)
    sq = np.convolve(sq, np.ones(2) / 2, mode='same')
    return np.sqrt(sq)


def local_max(env, radius):
    pad = np.concatenate([np.zeros(radius), env, np.zeros(radius)])
    out = env.copy()
    for k in range(-radius, radius + 1, 2):
        out = np.maximum(out, pad[radius + k:radius + k + len(env)])
    return out


def detect_notes(stem, count):
    """Split a monophonic part into notes at its near-silent dips; return (start s, end s, Hz)."""
    env = envelope_ms(stem)
    lm = local_max(env, 40)
    on = env > np.maximum(0.12 * lm, 1e-5)
    edges = np.diff(np.concatenate([[0], on.astype(np.int8), [0]]))
    starts, ends = np.nonzero(edges == 1)[0], np.nonzero(edges == -1)[0]
    segs = [(s, e) for s, e in zip(starts, ends) if e - s >= 8]
    notes = []
    for i, (s, e) in enumerate(segs[:count]):
        t0 = (s - 1.0) / 1000.0
        if i + 1 < len(segs) and segs[i + 1][0] - e <= 25:
            t1 = (segs[i + 1][0] - 1.0) / 1000.0
        else:
            t1 = (e + 1.0) / 1000.0
        a = int((s + 0.2 * (e - s)) * SR / 1000)
        b = int((s + 0.8 * (e - s)) * SR / 1000)
        seg = stem[a:b] * np.hanning(b - a)
        size = 1 << 17
        spec = np.abs(np.fft.rfft(seg, size))
        freqs = np.fft.rfftfreq(size, 1.0 / SR)
        band = (freqs > 60) & (freqs < 4000)
        k = int(np.argmax(np.where(band, spec, 0)))
        if 0 < k < len(spec) - 1:
            y0, y1, y2 = np.log(spec[k - 1:k + 2] + 1e-12)
            k = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
        notes.append((t0, t1, k * SR / size))
    return notes


def nearest_note(hz):
    best = None
    for octave in range(1, 9):
        for p in range(12):
            f = pulse_hz(frequency_register(p, octave))
            d = abs(np.log2(hz / f))
            if best is None or d < best[0]:
                best = (d, p, octave)
    return best[1], best[2], 1200 * best[0]


def check_files(music, folder):
    print('== 1. files and formats')
    total = 0
    for sid, m in sorted(music.items()):
        path = os.path.join(folder, m['file'])
        if not os.path.exists(path):
            fail('%s: missing %s' % (sid, m['file']))
            continue
        fmt, data = read_wav(path)
        if fmt != (1, 2, SR, 'NONE'):
            fail('%s: format %s, want mono 16-bit %d Hz PCM' % (sid, fmt, SR))
        if m['jingle'] and (m['loop_start'] or m['loop_end']):
            fail('%s: a jingle with loop points' % sid)
        if not (0 <= m['loop_start'] <= m['loop_end'] <= len(data)):
            fail('%s: loop %d-%d outside %d samples' % (sid, m['loop_start'], m['loop_end'], len(data)))
        if not m['jingle'] and m['loop_end'] <= m['loop_start']:
            fail('%s: empty loop' % sid)
        print('  ok   %-20s %-22s %8.2fs  mono 16-bit %d Hz  loop %d-%d  jingle=%s' % (
            sid, m['file'], len(data) / SR, fmt[2], m['loop_start'], m['loop_end'], m['jingle']))
    for f in os.listdir(folder):
        total += os.path.getsize(os.path.join(folder, f))
    return total


def check_loops(music, prog, consts):
    print('== 2. loop points on note boundaries (re-derived from the asm)')
    ids = {render.song_id(c): c for c in render.SONGS}
    for sid, m in sorted(music.items()):
        if m['jingle'] or sid not in ids:
            continue
        tl = render.timeline(prog, consts[ids[sid]])
        s1, s2, s3 = (frame_to_sample(tl[k]) for k in ('F1', 'F2', 'F3'))
        want = (s1, s1 + s3 - s2)
        ch = tl['engine'].chans[tl['chan']]
        ev0 = ch.events[tl['event']]
        ev1 = ch.events[tl['event'] + tl['events']]
        desc = lambda ev: ('%s%d' % (PITCH_NAMES[ev['pitch']], ev['octave'])) if ev['kind'] == 'note' else ev['kind']
        same = ev0.get('src') == ev1.get('src') and ev0['kind'] == ev1['kind']
        ok = (m['loop_start'], m['loop_end']) == want and same
        print('  %s %-20s loop %8d-%-8d  ch%d %s (%s) at frame %d -> same note at frame %d, one period (%d frames) on' % (
            'ok  ' if ok else 'FAIL', sid, m['loop_start'], m['loop_end'], tl['chan'] + 1, desc(ev0),
            ev0.get('src', ''), tl['F1'], tl['F2'], tl['F2'] - tl['F1']))
        if not ok:
            fail('%s: loop %s, engine says %s' % (sid, (m['loop_start'], m['loop_end']), want))


def check_pitches(music, prog, consts, folder):
    print('== 3. channel 2: asm notes vs notes detected in the render')
    ids = {render.song_id(c): c for c in render.SONGS}
    inst = gbsynth.Instruments()
    ir = gbsynth.reverb_ir()
    for sid in PITCH_SONGS:
        header = consts[ids[sid]]
        tl, stems, full = render.render_music(prog, header, inst, ir)
        eng = tl['engine']
        asm = [e for e in eng.chans[CHAN2].events if e['kind'] == 'note'][:FIRST_NOTES]
        det = detect_notes(stems[CHAN2], FIRST_NOTES)
        print('  -- %s (%s, channel 2)' % (sid, header))
        print('     #  asm source                   asm note  len  frames | detected    Hz      cents  len  frames  match')
        for i, ev in enumerate(asm):
            frames_per_unit = ev['speed'] * ev['tempo'] / 256.0
            name = '%s%d' % (PITCH_NAMES[ev['pitch']], ev['octave'])
            if i < len(det):
                t0, t1, hz = det[i]
                p, o, cents = nearest_note(hz)
                dname = '%s%d' % (PITCH_NAMES[p], o)
                dframes = (t1 - t0) * FRAME_HZ
                dunits = dframes / frames_per_unit
                ok = dname == name and int(round(dunits)) == ev['units'] and abs(t0 - frame_to_sample(ev['t']) / SR) < 0.02
                print('    %2d  %-28s %-8s %4d  %6d | %-8s %8.2f %+6.1f %4d  %6.1f  %s' % (
                    i + 1, ev['src'], name, ev['units'], ev['len'], dname, hz, cents, int(round(dunits)), dframes,
                    'yes' if ok else 'NO'))
                if not ok:
                    fail('%s note %d: asm %s x%d, detected %s x%.2f at %.3fs' % (sid, i + 1, name, ev['units'], dname, dunits, t0))
            else:
                fail('%s: only %d notes detected' % (sid, len(det)))
        # The measured part is the one in the committed file: re-render and compare.
        f, ls, le, _ = render.finish(tl, full)
        pcm = render.trim_tail(gbsynth.to_pcm16(f), le)
        _, data = read_wav(os.path.join(folder, music[sid]['file']))
        n = min(len(pcm), len(data))
        diff = int(np.abs(pcm[:n].astype(np.int32) - data[:n].astype(np.int32)).max()) if n else -1
        same_len = len(pcm) == len(data)
        print('     re-render vs committed %s: %d samples, max difference %d LSB%s' % (
            music[sid]['file'], n, diff, '' if same_len else ' (length %d vs %d)' % (len(pcm), len(data))))
        if diff > 2 or not same_len:
            fail('%s: the committed file differs from a re-render (max %d LSB)' % (sid, diff))


def check_loudness(music, folder):
    print('== 4. loudness')
    for sid, m in sorted(music.items()):
        _, data = read_wav(os.path.join(folder, m['file']))
        x = data.astype(np.float64) / 32767.0
        if m['file'].startswith('sfx_'):
            l = gbsynth.max_momentary_lufs(x)
            target, what = render.SFX_MOMENTARY_LUFS, 'max momentary'
        else:
            region = x[:m['loop_end']] if m['loop_end'] else x
            l = gbsynth.integrated_lufs(region)
            target, what = render.SONG_LUFS, 'integrated'
        peak = 20 * np.log10(max(1, np.abs(data.astype(np.int32)).max()) / 32768.0)
        ok = abs(l - target) <= 0.5 and peak <= -0.9
        print('  %s %-20s %6.2f LUFS %-13s peak %6.2f dBFS' % ('ok  ' if ok else 'FAIL', sid, l, what, peak))
        if not ok:
            fail('%s: %.2f LUFS (%s), peak %.2f dBFS' % (sid, l, what, peak))


def main():
    folder = render.OUT
    with open(os.path.join(folder, 'music.json'), encoding='utf-8') as fh:
        music = json.load(fh)
    root, commit = pyenv.pokeyellow()
    print('pokeyellow %s at %s' % (commit, root))
    prog = Program(root)
    consts = render.constants(root)
    total = check_files(music, folder)
    check_loops(music, prog, consts)
    check_pitches(music, prog, consts, folder)
    check_loudness(music, folder)
    print('== 5. size')
    mb = total / 1e6
    print('  %s YellowMusic is %.1f MB (limit %.0f MB)' % ('ok  ' if mb < MAX_MB else 'FAIL', mb, MAX_MB))
    if mb >= MAX_MB:
        fail('YellowMusic is %.1f MB' % mb)
    print()
    if failures:
        print('%d FAILURES' % len(failures))
        sys.exit(1)
    print('ALL CHECKS PASSED (%d ids)' % len(music))


if __name__ == '__main__':
    main()
