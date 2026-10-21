"""Band-limited Game Boy APU rendering for the menu and field sound effects.

The UI sounds (square_note / noise_note / pitch_sweep) are the Game Boy's own chip
sounds, so they are rendered as the APU would play them: pulse duty, 64 Hz volume
envelope, 128 Hz frequency sweep on channel 1 and the 15/7-bit noise LFSR. It runs at
8x the output rate and is filtered down to 32 kHz, so there is no aliasing.
"""
import numpy as np

from gbengine import CHAN5, CHAN6, CHAN7, CHAN8
from gbsynth import SR, frame_to_sample

OS = 8
OSR = SR * OS
DUTY = [0.125, 0.25, 0.5, 0.75]


def _lfsr(width7):
    n = 127 if width7 else 32767
    reg = 0x7FFF
    out = np.empty(n, dtype=np.float64)
    for i in range(n):
        bit = (reg ^ (reg >> 1)) & 1
        reg = (reg >> 1) | (bit << 14)
        if width7:
            reg = (reg & ~0x40) | (bit << 6)
        out[i] = 1.0 if (reg & 1) == 0 else -1.0
    return out


LFSR15 = _lfsr(False)
LFSR7 = _lfsr(True)


def _envelope(env, n):
    v0, period = env >> 4, env & 7
    if period == 0:
        return np.full(n, float(v0))
    steps = np.floor(np.arange(n) / OSR * 64.0 / period)
    v = v0 + steps if env & 8 else v0 - steps
    return np.clip(v, 0, 15)


def _decimate(x):
    taps = 511
    fc = 0.45 * SR / OSR
    k = np.arange(taps) - (taps - 1) / 2
    h = 2 * fc * np.sinc(2 * fc * k) * np.blackman(taps)
    h /= h.sum()
    size = 1 << int(np.ceil(np.log2(len(x) + taps)))
    y = np.fft.irfft(np.fft.rfft(x, size) * np.fft.rfft(h, size), size)
    y = y[(taps - 1) // 2:(taps - 1) // 2 + len(x)]
    return y[::OS]


def render_sfx(engine, total_samples):
    """Mix the sound effect channels (5 pulse, 6 pulse, 8 noise) as the APU plays them."""
    out = np.zeros(total_samples * OS)
    for idx, ch in engine.chans.items():
        sweep = 0x08  # AUD1SWEEP_DOWN: InitSFXVariables turns the sweep off
        notes = [e for e in ch.events if e['kind'] in ('sfxnote', 'sweep', 'end')]
        for i, ev in enumerate(notes):
            if ev['kind'] == 'sweep':
                sweep = ev['val']
                continue
            if ev['kind'] != 'sfxnote':
                continue
            nxt = next((e for e in notes[i + 1:] if e['kind'] == 'sfxnote'), None)
            s0 = frame_to_sample(ev['t']) * OS
            s1 = frame_to_sample(nxt['t']) * OS if nxt else len(out)
            s1 = min(s1, len(out))
            n = s1 - s0
            if n <= 0:
                continue
            vol = _envelope(ev['env'], n) / 15.0
            if idx == CHAN8:
                poly = ev['poly']
                shift, width7, div = poly >> 4, poly & 8, poly & 7
                clock = 524288.0 / (div if div else 0.5) / (2 ** (shift + 1))
                seq = LFSR7 if width7 else LFSR15
                pos = (np.arange(n) * clock / OSR).astype(np.int64) % len(seq)
                wave = seq[pos]
            elif idx in (CHAN5, CHAN6):
                reg = np.full(n, float(ev['reg']))
                alive = np.ones(n)
                period, neg, sh = (sweep >> 4) & 7, sweep & 8, sweep & 7
                if idx == CHAN5 and period and sh:
                    step = int(OSR * period / 128.0)
                    f = ev['reg']
                    k = step
                    while k < n:
                        f = f - (f >> sh) if neg else f + (f >> sh)
                        if f > 2047:
                            alive[k:] = 0
                            break
                        reg[k:] = f
                        k += step
                hz = 131072.0 / (2048.0 - np.minimum(reg, 2047))
                phase = np.cumsum(hz / OSR)
                duty = DUTY[ev['duty'] & 3]
                wave = np.where((phase % 1.0) < duty, 1.0, -1.0) * alive
            else:
                continue
            out[s0:s1] += 0.25 * vol * wave
    return _decimate(out)
