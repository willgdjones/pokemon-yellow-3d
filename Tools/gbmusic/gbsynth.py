"""Play the engine's events with fuller instruments (numpy only, deterministic).

Channel 1 (pulse)  -> a warm musette accordion.
Channel 2 (pulse)  -> a tin whistle / flute lead with a soft detuned double.
Channel 3 (wave)   -> a plucked upright bass with a sine sub layer.
Channel 4 (noise)  -> an acoustic kit: kick, snares, hats, toms, crash and rim, per drum id.

Pitch comes from the Game Boy frequency register frame by frame, so vibrato and pitch
slides are the engine's own. Notes start on the engine's frames. The Game Boy volume
envelope shapes each note's loudness, with a sustain floor so a held note still sounds
like a held note on a real instrument.
"""
import numpy as np

from gbasm import pulse_hz, wave_hz
from gbengine import FRAME_HZ, CHAN1, CHAN2, CHAN3, CHAN4, CHAN5, CHAN6, CHAN7, CHAN8

SR = 32000
NYQ = SR / 2.0


def frame_to_sample(frame):
    return int(round(frame * SR / FRAME_HZ))


# ---- helpers -----------------------------------------------------------------

def fft_filter(x, lo=None, hi=None, width=0.25):
    """Zero-phase band filter with raised-cosine skirts (width in octaves)."""
    n = len(x)
    size = 1 << int(np.ceil(np.log2(max(n, 2))))
    X = np.fft.rfft(x, size)
    f = np.fft.rfftfreq(size, 1.0 / SR)
    g = np.ones_like(f)
    with np.errstate(divide='ignore'):
        oc = np.log2(np.maximum(f, 1e-6))
    if lo:
        t = np.clip((oc - (np.log2(lo) - width)) / width, 0, 1)
        g *= 0.5 - 0.5 * np.cos(np.pi * t)
    if hi:
        t = np.clip(((np.log2(hi) + width) - oc) / width, 0, 1)
        g *= 0.5 - 0.5 * np.cos(np.pi * t)
    return np.fft.irfft(X * g, size)[:n]


def gb_envelope(env, n):
    """The Game Boy volume envelope (NRx2) over n samples, as 0..1, steps at 64 Hz smoothed."""
    v0 = env >> 4
    period = env & 7
    t = np.arange(n) / SR
    if period == 0:
        return np.full(n, v0 / 15.0)
    steps = t * 64.0 / period
    v = v0 + steps if env & 8 else v0 - steps
    return np.clip(v, 0, 15) / 15.0


def adsr(n, attack, release, floor_end=None):
    """Linear attack from 0 and a release that reaches 0 exactly at sample n."""
    e = np.ones(n)
    a = max(1, min(int(attack * SR), n // 3))
    r = max(1, min(int(release * SR), n // 3))
    e[:a] = np.linspace(0, 1, a, endpoint=False)
    e[n - r:] *= np.linspace(1, 0, r)
    return e


def additive(freq, harmonics, phase0=0.0):
    """Sum of sine harmonics following a per-sample frequency curve. harmonics: [(k, amp)]."""
    phase = 2 * np.pi * np.cumsum(freq) / SR + phase0
    out = np.zeros(len(freq))
    fmax = float(np.max(freq)) if len(freq) else 0.0
    for k, amp in harmonics:
        if k * fmax >= NYQ * 0.92:
            break
        out += amp * np.sin(k * phase)
    return out


def smooth(x, width):
    """Moving average (same length) that rounds the Game Boy's square-wave vibrato."""
    w = int(width)
    if w <= 1 or len(x) < 2:
        return x
    pad = np.concatenate([np.full(w, x[0]), x, np.full(w, x[-1])])
    y = np.convolve(pad, np.ones(w) / w, mode='same')
    return y[w:w + len(x)]


# ---- notes from events ---------------------------------------------------------

class Note:
    __slots__ = ('ch', 'start', 'end', 'frames', 'regs', 'env', 'wave', 'master', 'ev', 'ring')


def channel_notes(ch, limit_frame):
    """Turn a channel's events into notes with per-frame frequency registers."""
    evs = ch.events
    notes = []
    changes = sorted(ch.regs.items())
    ci = 0
    for i, ev in enumerate(evs):
        if ev['kind'] != 'note':
            continue
        nxt = next((e for e in evs[i + 1:] if e['kind'] in ('note', 'rest', 'end', 'drum')), None)
        ring = False
        if nxt is None:
            end = limit_frame
        else:
            end = nxt['t']
            if nxt['kind'] == 'end' and not nxt['cut']:
                ring = True  # a sound effect channel leaves its last note decaying
        end = min(end, limit_frame)
        if end <= ev['t']:
            continue
        n = Note()
        n.ch, n.ev, n.env, n.wave, n.master, n.ring = ch.idx, ev, ev['env'], ev['wave'], ev['master'], ring
        n.start, n.end = ev['t'], end
        regs = np.full(end - ev['t'], ev['reg'], dtype=np.int64)
        while ci < len(changes) and changes[ci][0] < ev['t']:
            ci += 1
        cj = ci
        while cj < len(changes) and changes[cj][0] < end:
            f, r = changes[cj]
            regs[f - ev['t']:] = r
            cj += 1
        n.regs = regs
        notes.append(n)
    return notes


def note_freq_samples(note, hz_fn, n_samples):
    """Per-sample frequency for a note from its per-frame registers, lightly smoothed."""
    hz = np.array([hz_fn(int(r)) for r in note.regs])
    idx = (np.arange(n_samples) * FRAME_HZ / SR).astype(np.int64)
    idx = np.minimum(idx, len(hz) - 1)
    f = hz[idx]
    if len(np.unique(note.regs)) > 1:
        f = smooth(f, 0.018 * SR)
    return f


# ---- instruments -------------------------------------------------------------

def musette_harmonics():
    hs = []
    for k in range(1, 40):
        a = 1.0 / k ** 1.05
        if k % 2 == 1:
            a *= 1.15  # reeds lean odd
        a *= 1.0 + 0.8 * np.exp(-((np.log2(k) - 2.4) ** 2) / 0.6)  # a body formant near the 5th harmonic
        hs.append((k, a))
    return hs


def whistle_harmonics():
    return [(1, 1.0), (2, 0.30), (3, 0.16), (4, 0.055), (5, 0.03), (6, 0.012)]


class Instruments:
    def __init__(self, seed=1996):
        rng = np.random.default_rng(seed)
        self.musette_h = musette_harmonics()
        self.whistle_h = whistle_harmonics()
        # One long band-passed breath buffer, sliced per note (deterministic).
        self.breath = fft_filter(rng.standard_normal(SR * 8), 900, 7000, 0.5)
        self.breath /= np.max(np.abs(self.breath)) + 1e-9
        self.kit = make_kit(rng)
        # The finger thump at the start of each bass note: one fixed burst.
        self.thump = fft_filter(rng.standard_normal(400), 60, 900, 0.5) * np.exp(-np.arange(400) / (0.004 * SR))

    def loudness_shape(self, env, n, floor):
        e = gb_envelope(env, n)
        peak = float(e.max()) if n else 0.0
        if peak <= 0:
            return None
        if env & 8 or (env & 7) == 0:
            return e  # swells and flat notes as written
        return peak * (floor + (1 - floor) * e / peak)

    def accordion(self, note, n, hz_fn):
        f = note_freq_samples(note, hz_fn, n)
        shape = self.loudness_shape(note.env, n, 0.35)
        if shape is None:
            return None
        cents = 2 ** (5.5 / 1200.0)
        tone = (additive(f / cents, self.musette_h, 0.3) + additive(f * cents, self.musette_h, 1.7)) * 0.5
        t = np.arange(n) / SR
        bellows = 1.0 + 0.035 * np.sin(2 * np.pi * 4.6 * t + note.start)
        return 0.30 * tone * shape * bellows * adsr(n, 0.022, 0.035)

    def whistle(self, note, n, hz_fn, double=True):
        f = note_freq_samples(note, hz_fn, n)
        shape = self.loudness_shape(note.env, n, 0.45)
        if shape is None:
            return None
        body = additive(f, self.whistle_h)
        if double:
            det = 2 ** (7.0 / 1200.0)
            body = body + 0.32 * additive(f * det, self.whistle_h, 2.1)
        off = (note.start * 997) % (len(self.breath) - n - 1) if n < len(self.breath) - 1 else 0
        br = self.breath[off:off + n] if n < len(self.breath) - 1 else np.resize(self.breath, n)
        t = np.arange(n) / SR
        chiff = np.exp(-t / 0.018)
        noise = br * (0.035 + 0.16 * chiff)
        return 0.34 * (body + noise) * shape * adsr(n, 0.012, 0.018)

    def bass(self, note, n, hz_fn):
        f = note_freq_samples(note, hz_fn, n)
        level = {0x20: 1.0, 0x40: 0.5, 0x60: 0.25}.get(note.env & 0x60, 0.0)
        if level == 0.0:
            return None
        t = np.arange(n) / SR
        phase = 2 * np.pi * np.cumsum(f) / SR
        pluck = np.zeros(n)
        fmax = float(f.max())
        for k in range(1, 14):
            if k * fmax >= NYQ * 0.9:
                break
            pluck += (1.0 / k ** 1.25) * np.exp(-t * (1.2 + 1.6 * k)) * np.sin(k * phase)
        body = pluck * (0.30 + 0.70 * np.exp(-t / 0.9))
        sub_ratio = 0.5 if fmax > 98.0 else 1.0
        sub = 0.42 * np.sin(phase * sub_ratio) * (0.55 + 0.45 * np.exp(-t / 1.2))
        m = min(n, len(self.thump))
        body[:m] += 0.25 * self.thump[:m]
        return 0.34 * level * (body + sub) * adsr(n, 0.004, 0.03)


def make_kit(rng):
    def t_of(sec):
        return np.arange(int(sec * SR)) / SR

    def noise(sec, lo=None, hi=None):
        return fft_filter(rng.standard_normal(int(sec * SR)), lo, hi, 0.4)

    def norm(x):
        return x / (np.max(np.abs(x)) + 1e-9)

    metal_f = np.array([205.3, 304.4, 369.6, 522.7, 540.0, 800.0]) * 1.7

    def metal(sec):
        t = t_of(sec)
        return sum(np.sign(np.sin(2 * np.pi * fr * t + i)) for i, fr in enumerate(metal_f))

    kit = {}
    t = t_of(0.45)
    fk = 46 + 80 * np.exp(-t / 0.03)
    kick = np.sin(2 * np.pi * np.cumsum(fk) / SR) * np.exp(-t / 0.2)
    kick[:120] += 0.3 * noise(0.45, 1500, 6000)[:120] * np.exp(-np.arange(120) / 25)
    kit['kick'] = norm(kick)

    t = t_of(0.35)
    tone = 0.55 * np.sin(2 * np.pi * 188 * t) * np.exp(-t / 0.055) + 0.3 * np.sin(2 * np.pi * 332 * t) * np.exp(-t / 0.035)
    snare = tone + 0.9 * noise(0.35, 1300, 9000) * np.exp(-t / 0.11)
    kit['snare'] = norm(snare)
    t = t_of(0.25)
    kit['brush'] = norm(noise(0.25, 1800, 8000) * np.exp(-t / 0.07) + 0.2 * np.sin(2 * np.pi * 200 * t) * np.exp(-t / 0.03))

    t = t_of(0.12)
    kit['hat_closed'] = norm(fft_filter(metal(0.12) * 0.25 + noise(0.12), 6500, None, 0.3) * np.exp(-t / 0.028))
    t = t_of(0.7)
    kit['hat_open'] = norm(fft_filter(metal(0.7) * 0.25 + noise(0.7), 5500, None, 0.3) * np.exp(-t / 0.22))
    t = t_of(1.8)
    env = np.minimum(t / 0.004, 1) * np.exp(-t / 0.75)
    kit['crash'] = norm(fft_filter(metal(1.8) * 0.4 + noise(1.8), 3000, None, 0.5) * env)

    for name, f0 in (('tom_high', 205.0), ('tom_mid', 150.0), ('tom_low', 108.0)):
        t = t_of(0.6)
        ft = f0 * (1 + 0.35 * np.exp(-t / 0.04))
        tom = np.sin(2 * np.pi * np.cumsum(ft) / SR) * np.exp(-t / 0.28)
        tom += 0.25 * noise(0.6, 300, 3000) * np.exp(-t / 0.03)
        kit[name] = norm(tom)

    t = t_of(0.12)
    rim = np.sin(2 * np.pi * 1720 * t) * np.exp(-t / 0.009) + 0.6 * np.sin(2 * np.pi * 470 * t) * np.exp(-t / 0.018)
    rim += 0.4 * noise(0.12, 1500, 7000) * np.exp(-t / 0.006)
    kit['rim'] = norm(rim)
    return kit


# Drum ids (1-19) -> kit piece and velocity. Names follow pokecrystal's drumkits.asm for the same
# noise instruments (audio/sfx/noise_instrument*.asm); velocity is the instrument's GB volume.
DRUM_MAP = {
    1: ('snare', 12), 2: ('snare', 11), 3: ('snare', 10), 4: ('snare', 8),
    5: ('crash', 8),
    6: ('rim', 5), 7: ('rim', 6),
    8: ('hat_closed', 8),
    9: ('tom_high', 8), 10: ('tom_mid', 8), 11: ('tom_low', 8),
    12: ('hat_closed', 10), 13: ('hat_open', 10),
    14: ('kick', 10),
    15: ('hat_closed', 10), 16: ('hat_closed', 9),
    17: ('snare', 9), 18: ('brush', 7), 19: ('brush', 6),
}

ROLE_FOR = {CHAN1: 'accordion', CHAN2: 'whistle', CHAN3: 'bass', CHAN4: 'drums',
            CHAN5: 'accordion', CHAN6: 'whistle', CHAN7: 'bass', CHAN8: 'drums'}
REVERB_SEND = {'accordion': 0.26, 'whistle': 0.30, 'bass': 0.06, 'drums': 0.16}


def render_stems(engine, limit_frame, inst, tail_seconds=2.0, keep=None):
    """Render each channel dry. Returns {chan: array}, total samples."""
    total = frame_to_sample(limit_frame) + int(tail_seconds * SR)
    stems = {}
    for idx, ch in engine.chans.items():
        if keep is not None and idx not in keep:
            continue
        role = ROLE_FOR[idx]
        out = np.zeros(total + SR)
        if role == 'drums':
            for ev in ch.events:
                if ev['kind'] != 'drum' or ev['t'] >= limit_frame or ev['id'] not in DRUM_MAP:
                    continue
                piece, vel = DRUM_MAP[ev['id']]
                s = frame_to_sample(ev['t'])
                smp = inst.kit[piece] * (vel / 15.0)
                e = min(len(out), s + len(smp))
                out[s:e] += smp[:e - s]
        else:
            hz_fn = wave_hz if role == 'bass' else pulse_hz
            for note in channel_notes(ch, limit_frame):
                s0 = frame_to_sample(note.start)
                s1 = frame_to_sample(note.end)
                if note.ring:
                    s1 = s0 + max(s1 - s0, ring_samples(note.env))
                n = s1 - s0
                if n < 16:
                    continue
                if role == 'accordion':
                    y = inst.accordion(note, n, hz_fn)
                elif role == 'whistle':
                    y = inst.whistle(note, n, hz_fn)
                else:
                    y = inst.bass(note, n, hz_fn)
                if y is None:
                    continue
                y = y * master_gain(note.master)
                e = min(len(out), s1)
                out[s0:e] += y[:e - s0]
        stems[idx] = out[:total]
    return stems, total


def ring_samples(env):
    """How long a note left decaying by a finished sound effect lasts (its envelope to zero)."""
    v0, period = env >> 4, env & 7
    if period == 0 or env & 8:
        return int(0.4 * SR)
    return int(min(v0 * period / 64.0, 1.5) * SR)


def master_gain(nr50):
    return (((nr50 >> 4) & 7) + 1) / 8.0


# ---- reverb, loudness, limiting ----------------------------------------------

def reverb_ir(seed=64, seconds=2.2):
    rng = np.random.default_rng(seed)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    low = fft_filter(rng.standard_normal(n), None, 1800, 0.6) * np.exp(-t * 6.91 / 1.7)
    high = fft_filter(rng.standard_normal(n), 1800, None, 0.6) * np.exp(-t * 6.91 / 0.8)
    ir = low + 0.6 * high
    ir[:int(0.012 * SR)] = 0  # pre-delay
    for ms, g in ((13, 0.5), (19, -0.35), (29, 0.3), (37, -0.22), (47, 0.18)):
        ir[int(ms / 1000.0 * SR)] += g * 4.0
    ir = fft_filter(ir, 160, 9000, 0.5)
    return ir / np.sqrt(np.sum(ir ** 2))


def convolve(x, ir):
    n = len(x) + len(ir) - 1
    size = 1 << int(np.ceil(np.log2(n)))
    y = np.fft.irfft(np.fft.rfft(x, size) * np.fft.rfft(ir, size), size)
    return y[:len(x)]


def mix(stems, total, ir):
    dry = np.zeros(total)
    send = np.zeros(total)
    for idx, s in stems.items():
        dry += s
        send += s * REVERB_SEND[ROLE_FOR[idx]]
    return dry + 0.55 * convolve(send, ir)


def _biquad_response(b, a, w):
    z = np.exp(-1j * w)
    return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)


def k_weight(x):
    """ITU-R BS.1770 K-weighting at 32 kHz (the two biquads, applied in the frequency domain)."""
    G, Q, fc = 3.999843853973347, 0.7071752369554196, 1681.974450955533
    A = 10 ** (G / 40.0)
    w0 = 2 * np.pi * fc / SR
    al = np.sin(w0) / (2 * Q)
    c = np.cos(w0)
    b1 = [A * ((A + 1) + (A - 1) * c + 2 * np.sqrt(A) * al), -2 * A * ((A - 1) + (A + 1) * c),
          A * ((A + 1) + (A - 1) * c - 2 * np.sqrt(A) * al)]
    a1 = [(A + 1) - (A - 1) * c + 2 * np.sqrt(A) * al, 2 * ((A - 1) - (A + 1) * c),
          (A + 1) - (A - 1) * c - 2 * np.sqrt(A) * al]
    Q2, fc2 = 0.5003270373238773, 38.13547087602444
    w2 = 2 * np.pi * fc2 / SR
    al2 = np.sin(w2) / (2 * Q2)
    c2 = np.cos(w2)
    b2 = [(1 + c2) / 2, -(1 + c2), (1 + c2) / 2]
    a2 = [1 + al2, -2 * c2, 1 - al2]
    n = len(x) + SR
    size = 1 << int(np.ceil(np.log2(n)))
    w = 2 * np.pi * np.fft.rfftfreq(size)
    H = _biquad_response(b1, a1, w) * _biquad_response(b2, a2, w)
    return np.fft.irfft(np.fft.rfft(x, size) * H, size)[:len(x)]


def integrated_lufs(x):
    y = k_weight(x)
    block, hop = int(0.4 * SR), int(0.1 * SR)
    if len(y) < block:
        y = np.concatenate([y, np.zeros(block - len(y))])
    c = np.concatenate([[0.0], np.cumsum(y * y)])
    starts = np.arange(0, len(y) - block + 1, hop)
    z = (c[starts + block] - c[starts]) / block
    with np.errstate(divide='ignore'):
        l = -0.691 + 10 * np.log10(z)
    z = z[l > -70]
    if not len(z):
        return -100.0
    rel = -0.691 + 10 * np.log10(np.mean(z)) - 10
    with np.errstate(divide='ignore'):
        l2 = -0.691 + 10 * np.log10(z)
    z = z[l2 > rel]
    return float(-0.691 + 10 * np.log10(np.mean(z)))


def max_momentary_lufs(x):
    y = k_weight(x)
    block = int(0.4 * SR)
    y = np.concatenate([y, np.zeros(block)])
    c = np.concatenate([[0.0], np.cumsum(y * y)])
    starts = np.arange(0, len(y) - block + 1, int(0.01 * SR))
    z = (c[starts + block] - c[starts]) / block
    return float(-0.691 + 10 * np.log10(max(z.max(), 1e-12)))


def limit(x, ceiling=10 ** (-1.0 / 20)):
    """Transparent-ish look-around peak limiter: block minimum gain, interpolated."""
    a = np.abs(x)
    if a.max() <= ceiling:
        return x
    g = np.minimum(1.0, ceiling / np.maximum(a, 1e-12))
    blk = int(0.005 * SR)
    nb = int(np.ceil(len(x) / blk))
    gp = np.concatenate([g, np.ones(nb * blk - len(g))]).reshape(nb, blk).min(axis=1)
    spread = 4  # +-20 ms
    padded = np.concatenate([np.ones(spread), gp, np.ones(spread)])
    gm = np.min(np.stack([padded[i:i + nb] for i in range(2 * spread + 1)]), axis=0)
    centers = np.arange(nb) * blk + blk / 2.0
    gs = np.interp(np.arange(len(x)), centers, gm)
    return np.clip(x * gs, -ceiling, ceiling)


def normalise(x, target, measure):
    """Gain to the loudness target on the measured region, then limit peaks to -1 dBFS."""
    for _ in range(3):
        l = measure(x)
        x = x * 10 ** ((target - l) / 20.0)
        x = limit(x)
    return x, measure(x)


def to_pcm16(x):
    return np.clip(np.round(x * 32767.0), -32768, 32767).astype('<i2')
