"""Parse the pokeyellow audio sources (audio/music, audio/sfx, audio/headers).

Every macro line becomes a Cmd. All files are flattened into one program so that
labels (global `Name:`/`Name::` and local `.name:`, scoped to the last global) map
to an index, and sound_call / sound_loop jump by index the way the ROM jumps by
address.
"""
import os
import re

PITCHES = {'C_': 0, 'C#': 1, 'D_': 2, 'D#': 3, 'E_': 4, 'F_': 5,
           'F#': 6, 'G_': 7, 'G#': 8, 'A_': 9, 'A#': 10, 'B_': 11}
PITCH_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

# audio/notes.asm: the frequency register for each pitch at the top octave.
NOTE_TABLE = [0xF82C, 0xF89D, 0xF907, 0xF96B, 0xF9CA, 0xFA23,
              0xFA77, 0xFAC7, 0xFB12, 0xFB58, 0xFB9B, 0xFBDA]

COMMANDS = {
    'note', 'rest', 'octave', 'note_type', 'drum_speed', 'drum_note', 'tempo', 'volume',
    'duty_cycle', 'duty_cycle_pattern', 'vibrato', 'pitch_slide', 'toggle_perfect_pitch',
    'sound_call', 'sound_loop', 'sound_ret', 'execute_music', 'stereo_panning',
    'square_note', 'noise_note', 'pitch_sweep', 'unknownmusic0xef', 'drum_note_short',
}


class Cmd:
    __slots__ = ('op', 'args', 'file', 'line', 'target')

    def __init__(self, op, args, file, line):
        self.op, self.args, self.file, self.line = op, args, file, line
        self.target = None

    def __repr__(self):
        return '%s %s' % (self.op, ', '.join(str(a) for a in self.args))


def _number(tok):
    tok = tok.strip()
    if tok in PITCHES:
        return PITCHES[tok]
    neg = tok.startswith('-')
    if neg:
        tok = tok[1:].strip()
    if tok.startswith('$'):
        v = int(tok[1:], 16)
    elif tok.startswith('%'):
        v = int(tok[1:], 2)
    else:
        v = int(tok, 10)
    return -v if neg else v


class Program:
    """All audio asm files flattened, with label -> index."""

    def __init__(self, root):
        self.root = root
        self.cmds = []
        self.labels = {}
        self.headers = {}  # header label -> [(channel number 1-8, label)]
        files = []
        for sub in ('audio/music', 'audio/sfx'):
            d = os.path.join(root, sub)
            files += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith('.asm')]
        for path in files:
            self._parse(path)
        for cmd in self.cmds:
            if cmd.op in ('sound_call', 'sound_loop'):
                name = cmd.args[-1]
                if name not in self.labels:
                    raise ValueError('%s:%d unknown label %s' % (cmd.file, cmd.line, name))
                cmd.target = self.labels[name]
        hd = os.path.join(root, 'audio/headers')
        for f in sorted(os.listdir(hd)):
            self._parse_headers(os.path.join(hd, f))

    def _parse(self, path):
        rel = os.path.relpath(path, self.root).replace('\\', '/')
        scope = ''
        with open(path, encoding='utf-8') as fh:
            for n, raw in enumerate(fh, 1):
                line = raw.split(';', 1)[0].rstrip()
                if not line.strip():
                    continue
                # Labels start in column 0; rgbds allows a local label without its colon.
                m = re.match(r'^(\.?[A-Za-z_][A-Za-z0-9_.]*)(::?|$)\s*(.*)$', line.strip()) if not line[0].isspace() else None
                if m:
                    name = m.group(1)
                    if name.startswith('.'):
                        full = scope + name
                    else:
                        scope = name
                        full = name
                    self.labels[full] = len(self.cmds)
                    line = m.group(3)
                    if not line.strip():
                        continue
                parts = line.strip().split(None, 1)
                op = parts[0]
                if op not in COMMANDS:
                    raise ValueError('%s:%d unknown command %r' % (rel, n, op))
                args = [a.strip() for a in parts[1].split(',')] if len(parts) > 1 else []
                if op in ('sound_call', 'sound_loop'):
                    lab = args[-1]
                    if lab.startswith('.'):
                        lab = scope + lab
                    vals = [_number(a) for a in args[:-1]] + [lab]
                else:
                    vals = [_number(a) for a in args]
                self.cmds.append(Cmd(op, vals, rel, n))

    def _parse_headers(self, path):
        cur = None
        with open(path, encoding='utf-8') as fh:
            for raw in fh:
                line = raw.split(';', 1)[0].strip()
                m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)::?$', line)
                if m:
                    cur = m.group(1)
                    self.headers.setdefault(cur, [])
                    continue
                m = re.match(r'^channel\s+(\d+)\s*,\s*([A-Za-z0-9_.]+)$', line)
                if m and cur:
                    self.headers[cur].append((int(m.group(1)), m.group(2)))


def frequency_register(pitch, octave):
    """Audio1_CalculateFrequency: 11-bit frequency register for a note."""
    v = NOTE_TABLE[pitch]
    if v & 0x8000:
        v -= 0x10000
    v >>= (octave - 1)  # arithmetic shift, octave 1..8
    return (v + 0x800) & 0xFFFF


def pulse_hz(reg):
    return 131072.0 / (2048 - (reg & 0x7FF))


def wave_hz(reg):
    return 65536.0 / (2048 - (reg & 0x7FF))


def note_name(pitch, octave):
    return '%s%d' % (PITCH_NAMES[pitch], octave)
