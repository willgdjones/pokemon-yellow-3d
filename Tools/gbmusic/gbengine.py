"""A frame-exact model of the pokeyellow sound engine (audio/engine_1.asm).

It runs the parsed commands once per Game Boy frame (59.7275 Hz), the way
Audio1_UpdateMusic does, and records what each channel plays: note-ons with their
frequency register and envelope, rests, drum hits and the per-frame frequency
register (vibrato and pitch slides). The synthesiser then plays those events with
new instruments, so timing and pitch are the Game Boy's.
"""
from gbasm import frequency_register

FRAME_HZ = 4194304.0 / 70224.0  # Game Boy VBlank rate, 59.7275 Hz

STALE_OCTAVE = 4

CHAN1, CHAN2, CHAN3, CHAN4, CHAN5, CHAN6, CHAN7, CHAN8 = range(8)


class Chan:
    def __init__(self, idx, pc):
        self.idx = idx
        self.pc = pc
        self.active = True
        self.ret = None
        self.in_call = False
        self.loop_ctr = 1
        self.delay = 1
        self.frac = 0
        self.speed = 1
        self.octave = 0
        self.volume = 0
        self.wave = 0
        self.duty = 0
        self.vib_delay = 0
        self.vib_delay_reload = 0
        self.vib_extent = 0
        self.vib_rate = 0
        self.vib_up_next = True  # BIT_VIBRATO_DIRECTION clear
        self.freq_lo = 0
        self.perfect_pitch = False
        self.slide_on = False
        self.slide_dec = False
        self.slide_len_mod = 0
        self.slide_target = 0
        self.slide_cur = 0
        self.slide_step = 0
        self.slide_step_frac = 0
        self.slide_cur_frac = 0
        self.execute_music = False
        self.noise_or_sfx = idx >= CHAN4
        self.reg = 0          # current frequency register (11 bits)
        self.events = []
        self.regs = {}        # frame -> register, whenever it changes without a trigger
        self.state_log = []   # (event index, state key, frame)


class Engine:
    def __init__(self, program):
        self.p = program
        self.music_tempo = 0x100
        self.sfx_tempo = 0x100
        self.master = 0x77
        self.frame = 0
        self.chans = {}
        self.warnings = []

    # ---- setup -------------------------------------------------------------
    def load(self, header):
        """Start a song or sound effect by its header label (Music_X or SFX_X)."""
        for num, label in self.p.headers[header]:
            idx = num - 1
            self.chans[idx] = Chan(idx, self.p.labels[label])

    def key(self, ch):
        return (ch.pc, ch.loop_ctr, ch.in_call, ch.ret, ch.octave, ch.speed, ch.volume, ch.wave,
                ch.duty, ch.vib_delay_reload, ch.vib_extent, ch.vib_rate >> 4, ch.perfect_pitch,
                ch.execute_music, self.music_tempo if ch.idx < CHAN5 else self.sfx_tempo)

    # ---- running -----------------------------------------------------------
    def run(self, max_frames):
        while self.frame < max_frames and any(c.active for c in self.chans.values()):
            for idx in sorted(self.chans):
                ch = self.chans[idx]
                if ch.active:
                    self.update(ch)
            self.frame += 1
        return self.frame

    def update(self, ch):
        if ch.delay == 1:
            self.play_next_note(ch)
            return
        ch.delay = (ch.delay - 1) & 0xFF
        if not ch.execute_music and ch.noise_or_sfx:
            return
        if ch.slide_on:
            self.apply_slide(ch)
            return
        if ch.vib_delay:
            ch.vib_delay -= 1
            return
        if not ch.vib_extent:
            return
        if ch.vib_rate & 0xF:
            ch.vib_rate -= 1
            return
        r = ch.vib_rate >> 4
        ch.vib_rate = (r << 4) | r
        if ch.vib_up_next:
            ch.vib_up_next = False
            lo = min(ch.freq_lo + (ch.vib_extent >> 4), 0xFF)
        else:
            ch.vib_up_next = True
            lo = max(ch.freq_lo - (ch.vib_extent & 0xF), 0)
        ch.reg = (ch.reg & 0x700) | lo
        ch.regs[self.frame] = ch.reg

    def next_cmd(self, ch):
        cmd = self.p.cmds[ch.pc]
        ch.pc += 1
        return cmd

    def play_next_note(self, ch):
        ch.vib_delay = ch.vib_delay_reload
        ch.slide_on = False
        ch.slide_dec = False
        while True:
            if ch.idx < CHAN5:
                ch.state_log.append((len(ch.events), self.key(ch), self.frame))
            cmd = self.next_cmd(ch)
            op, a = cmd.op, cmd.args
            if op == 'sound_ret':
                if ch.in_call:
                    ch.in_call = False
                    ch.pc = ch.ret
                    continue
                ch.active = False
                ch.events.append({'t': self.frame, 'kind': 'end',
                                  'cut': ch.idx < CHAN4 or ch.idx == CHAN7})
                return
            if op == 'sound_call':
                ch.ret = ch.pc
                ch.in_call = True
                ch.pc = cmd.target
                continue
            if op == 'sound_loop':
                count = a[0]
                if count == 0:
                    ch.pc = cmd.target
                    continue
                if ch.loop_ctr == count:
                    ch.loop_ctr = 1
                    continue
                ch.loop_ctr += 1
                ch.pc = cmd.target
                continue
            if op in ('note_type', 'drum_speed'):
                ch.speed = a[0] & 0xF
                if ch.idx == CHAN4 or op == 'drum_speed':
                    continue
                vol, fade = a[1], a[2]
                d = ((vol & 0xF) << 4) | ((8 | -fade) if fade < 0 else fade)
                if ch.idx in (CHAN3, CHAN7):
                    ch.wave = d & 0xF
                    ch.volume = (d & 0x30) << 1
                else:
                    ch.volume = d
                continue
            if op == 'toggle_perfect_pitch':
                ch.perfect_pitch = not ch.perfect_pitch
                continue
            if op == 'vibrato':
                delay, depth, rate = a
                ch.vib_delay = delay
                ch.vib_delay_reload = delay
                ch.vib_extent = (((depth >> 1) + (depth & 1)) << 4) | (depth >> 1)
                ch.vib_rate = (rate << 4) | rate
                continue
            if op == 'pitch_slide':
                length, octave, pitch = a
                ch.slide_len_mod = (length - 1) & 0xFF
                ch.slide_target = frequency_register(pitch, octave)
                ch.slide_on = True
                note = self.next_cmd(ch)
                self.note_length(ch, note)
                return
            if op == 'duty_cycle':
                ch.duty = a[0]
                continue
            if op == 'duty_cycle_pattern':
                ch.duty = a[0]
                continue
            if op == 'tempo':
                if ch.idx < CHAN5:
                    self.music_tempo = a[0] & 0xFFFF
                    for c in self.chans.values():
                        if c.idx < CHAN5:
                            c.frac = 0
                else:
                    self.sfx_tempo = a[0] & 0xFFFF
                    for c in self.chans.values():
                        if c.idx >= CHAN5:
                            c.frac = 0
                continue
            if op in ('stereo_panning', 'unknownmusic0xef'):
                continue
            if op == 'volume':
                self.master = (a[0] << 4) | a[1]
                continue
            if op == 'execute_music':
                ch.execute_music = True
                continue
            if op == 'octave':
                ch.octave = a[0]
                continue
            if op in ('square_note', 'noise_note') and ch.idx >= CHAN4 and not ch.execute_music:
                self.sfx_note(ch, cmd)
                return
            if op == 'pitch_sweep':
                if ch.idx >= CHAN5 and not ch.execute_music:
                    length, change = a
                    val = ((length & 0xF) << 4) | ((8 | -change) if change < 0 else change)
                    ch.events.append({'t': self.frame, 'kind': 'sweep', 'val': val})
                    continue
                raise ValueError('pitch_sweep on a music channel at %s:%d' % (cmd.file, cmd.line))
            if op in ('note', 'rest', 'drum_note'):
                self.note_length(ch, cmd)
                return
            raise ValueError('unhandled %s at %s:%d' % (op, cmd.file, cmd.line))

    def length_frames(self, ch, units):
        prod = (units * ch.speed) & 0xFF
        # Sound-effect channels always run at $100: Audio1_SetSfxTempo overrides any tempo
        # command unless a cry or battle effect is playing.
        tempo = 0x100 if ch.idx >= CHAN5 else self.music_tempo
        total = (ch.frac + prod * tempo) & 0xFFFF
        ch.frac = total & 0xFF
        ch.delay = total >> 8
        return prod

    def note_length(self, ch, cmd):
        op, a = cmd.op, cmd.args
        if op == 'drum_note':
            inst, units = a
            if ch.idx == CHAN4:
                ch.events.append({'t': self.frame, 'kind': 'drum', 'id': inst})
        elif op == 'note':
            units = a[1]
        elif op == 'rest':
            units = a[0]
        else:
            raise ValueError('expected a note after pitch_slide at %s:%d' % (cmd.file, cmd.line))
        self.length_frames(ch, units)
        if not ch.execute_music and ch.noise_or_sfx:
            if op == 'drum_note' and ch.idx == CHAN4:
                ch.events[-1]['len'] = ch.delay
            return
        if op == 'rest':
            ch.events.append({'t': self.frame, 'kind': 'rest', 'len': ch.delay, 'units': units,
                              'speed': ch.speed, 'src': '%s:%d' % (cmd.file, cmd.line)})
            return
        pitch = a[0]
        if not ch.octave:
            # The song never set an octave (PkmnHealed channel 1), so the Game Boy uses whatever
            # the interrupted song left in wChannelOctaves. Assume octave 4, the Pokecenter's.
            ch.octave = STALE_OCTAVE
            self.warnings.append('%s:%d: no octave set, assuming %d' % (cmd.file, cmd.line, STALE_OCTAVE))
        reg = frequency_register(pitch, ch.octave)
        if ch.slide_on:
            self.init_slide(ch, reg)
        if ch.perfect_pitch:
            reg = (reg & 0xFF00) | ((reg + 1) & 0xFF)
        ch.freq_lo = reg & 0xFF
        ch.reg = reg & 0x7FF
        ch.events.append({'t': self.frame, 'kind': 'note', 'reg': ch.reg, 'env': ch.volume,
                          'wave': ch.wave, 'duty': ch.duty, 'len': ch.delay, 'units': units,
                          'speed': ch.speed, 'pitch': pitch, 'octave': ch.octave,
                          'tempo': self.music_tempo if ch.idx < CHAN5 else 0x100,
                          'master': self.master, 'src': '%s:%d' % (cmd.file, cmd.line)})

    def init_slide(self, ch, reg):
        d, e = reg >> 8, reg & 0xFF
        ch.slide_cur = reg
        mod = ch.delay - ch.slide_len_mod
        if mod < 0:
            mod = 1
        mod &= 0xFF
        ch.slide_len_mod = mod
        thi, tlo = ch.slide_target >> 8, ch.slide_target & 0xFF
        lo = e - tlo
        borrow = 1 if lo < 0 else 0
        lo &= 0xFF
        hi = d - borrow - thi
        if hi >= 0:
            diff = (hi << 8) | lo
            ch.slide_dec = True
        else:
            lo = tlo - e
            borrow = 1 if lo < 0 else 0
            lo &= 0xFF
            hi = (thi - ((d - borrow) & 0xFF)) & 0xFF  # the engine's borrow bug
            diff = (hi << 8) | lo
            ch.slide_dec = False
        q, r = divmod(diff, mod) if mod else (0, 0)
        ch.slide_step = (q + 1) & 0xFF
        ch.slide_step_frac = r & 0xFF
        ch.slide_cur_frac = r & 0xFF

    def apply_slide(self, ch):
        cur = ch.slide_cur
        if not ch.slide_dec:
            new = cur + ch.slide_step
            s = ch.slide_cur_frac + ch.slide_step_frac
            ch.slide_cur_frac = s & 0xFF
            new = (new + (s >> 8)) & 0xFFFF
            reached = new > ch.slide_target
        else:
            new = cur - ch.slide_step
            s = ch.slide_step_frac * 2
            ch.slide_step_frac = s & 0xFF
            new = (new - (s >> 8)) & 0xFFFF
            reached = new < ch.slide_target
        if reached:
            ch.slide_on = False
            ch.slide_dec = False
            return
        ch.slide_cur = new
        ch.reg = new & 0x7FF
        ch.regs[self.frame] = ch.reg

    def sfx_note(self, ch, cmd):
        op, a = cmd.op, cmd.args
        length, vol, fade = a[0], a[1], a[2]
        env = ((vol & 0xF) << 4) | ((8 | -fade) if fade < 0 else fade)
        prod = ((length + 1) * ch.speed) & 0xFF
        total = (ch.frac + prod * 0x100) & 0xFFFF
        ch.frac = total & 0xFF
        ch.delay = total >> 8
        ev = {'t': self.frame, 'kind': 'sfxnote', 'env': env, 'duty': ch.duty, 'len': ch.delay}
        if op == 'noise_note':
            ev['poly'] = a[3]
        else:
            ev['reg'] = a[3] & 0x7FF
        ch.events.append(ev)


def loop_points(engine):
    """Per channel, the first event whose full engine state recurs: (intro frame, period frames,
    events per period). Returns None for channels that end."""
    out = {}
    for idx, ch in engine.chans.items():
        seen = {}
        found = None
        for ev_i, key, frame in ch.state_log:
            # Only states at event boundaries count; the same key twice in one command run
            # (e.g. two jumps before a note) is skipped by keying on the event index too.
            if key in seen and seen[key][0] != ev_i:
                found = (seen[key], (ev_i, frame))
                break
            seen.setdefault(key, (ev_i, frame))
        if found is None:
            out[idx] = None
            continue
        (e0, f0), (e1, f1) = found
        out[idx] = {'intro_event': e0, 'intro_frame': f0, 'period_events': e1 - e0,
                    'period_frames': f1 - f0}
    return out
