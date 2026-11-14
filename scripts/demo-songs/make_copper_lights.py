"""Writes 'Copper Lights', an EDM demo song, as a General MIDI file (type 1)."""
import sys, random
import mido

random.seed(7)
PPQ = 480
S = PPQ // 4          # a 16th note
BAR = PPQ * 4
BPM = 126

def n(name):
    names = {'C':0,'C#':1,'Db':1,'D':2,'D#':3,'Eb':3,'E':4,'F':5,'F#':6,'Gb':6,'G':7,'G#':8,'Ab':8,'A':9,'A#':10,'Bb':10,'B':11}
    p, o = name[:-1], int(name[-1])
    return 12 * (o + 1) + names[p]

# Sections: (name, bars)
SECTIONS = [('Intro', 8), ('Build', 8), ('Drop', 16), ('Break', 8), ('Build 2', 8), ('Drop 2', 16), ('Outro', 8)]
starts, b = {}, 0
for name, bars in SECTIONS:
    starts[name] = (b, b + bars); b += bars
TOTAL = b

# Am - F - C - G, one bar each (roots and triads)
PROG = [('A', [n('A3'), n('C4'), n('E4')]), ('F', [n('F3'), n('A3'), n('C4')]),
        ('C', [n('G3'), n('C4'), n('E4')]), ('G', [n('G3'), n('B3'), n('D4')])]
BASS_ROOT = {'A': n('A1'), 'F': n('F1'), 'C': n('C2'), 'G': n('G1')}

class Track:
    def __init__(self, name, ch, program, vol=100, pan=64, rev=40, cho=0, bank=0):
        self.name, self.ch, self.ev = name, ch, []
        self.cc(0, 0, bank); self.cc(0, 7, vol); self.cc(0, 10, pan); self.cc(0, 91, rev); self.cc(0, 93, cho); self.cc(0, 11, 127)
        self.ev.append((0, 2, mido.Message('program_change', channel=ch, program=program)))
    def note(self, t, length, pitch, vel):
        vel = max(1, min(127, int(vel)))
        self.ev.append((t, 3, mido.Message('note_on', channel=self.ch, note=pitch, velocity=vel)))
        self.ev.append((t + length, 1, mido.Message('note_off', channel=self.ch, note=pitch, velocity=0)))
    def cc(self, t, c, v):
        self.ev.append((t, 2, mido.Message('control_change', channel=self.ch, control=c, value=max(0, min(127, int(v))))))
    def bend(self, t, v):
        self.ev.append((t, 2, mido.Message('pitchwheel', channel=self.ch, pitch=max(-8192, min(8191, int(v))))))
    def midi(self):
        tr = mido.MidiTrack(); tr.append(mido.MetaMessage('track_name', name=self.name, time=0))
        last = 0
        for t, _, m in sorted(self.ev, key=lambda e: (e[0], e[1])):
            tr.append(m.copy(time=t - last)); last = t
        tr.append(mido.MetaMessage('end_of_track', time=0))
        return tr

def in_(sec, bar):
    a, z = starts[sec]; return a <= bar < z
def sec_of(bar):
    for name, _ in SECTIONS:
        if in_(name, bar): return name
def chord(bar): return PROG[bar % 4]

drums = Track('Drums', 9, 25, vol=110, rev=25)              # GS Electronic kit (GM players use Standard)
bass  = Track('Synth Bass', 0, 38, vol=105, rev=10)          # Synth Bass 1
sub   = Track('Sub Bass', 1, 39, vol=80, rev=0)              # Synth Bass 2
lead  = Track('Saw Lead', 2, 81, vol=92, pan=70, rev=60, cho=40)   # Lead 2 (sawtooth)
chords= Track('Supersaw Chords', 3, 90, vol=88, pan=64, rev=70, cho=70)  # Pad 3 (polysynth)
pad   = Track('Warm Pad', 4, 89, vol=78, rev=90, cho=50)    # Pad 2 (warm)
arp   = Track('Pluck Arp', 5, 80, vol=72, pan=40, rev=70)   # Lead 1 (square)
piano = Track('Piano', 6, 0, vol=86, pan=80, rev=80)        # Acoustic Grand
fx    = Track('Riser FX', 7, 102, vol=80, rev=100)          # FX 7 (echoes)
bells = Track('Bells', 8, 98, vol=70, pan=90, rev=90)       # FX 3 (crystal)

KICK, CLAP, SNARE, CHH, OHH, RIDE, CRASH, TOMH = 36, 39, 38, 42, 46, 51, 49, 50

for bar in range(TOTAL):
    s = sec_of(bar); t0 = bar * BAR
    rel = bar - starts[s][0]; slen = starts[s][1] - starts[s][0]
    root, triad = chord(bar)
    drop = s in ('Drop', 'Drop 2')
    build = s in ('Build', 'Build 2')

    # ---------- drums ----------
    if rel == 0 and s in ('Drop', 'Drop 2', 'Break', 'Outro', 'Build'):
        drums.note(t0, S * 4, CRASH, 112)
    if drop or (s == 'Outro' and rel < 6) or (s == 'Build' and rel < 4):
        for beat in range(4):
            drums.note(t0 + beat * PPQ, S, KICK, 120 if beat == 0 else 112)
    if drop or s == 'Outro' and rel < 6:
        for beat in (1, 3): drums.note(t0 + beat * PPQ, S, CLAP, 105)
    if drop or s in ('Intro', 'Outro') or (build and rel < 4):
        for i in range(16):
            if i % 4 == 2: drums.note(t0 + i * S, S, OHH, 90 if drop else 70)
            elif drop and i % 2 == 1: drums.note(t0 + i * S, S, CHH, 55 + random.randint(0, 15))
            elif s == 'Intro' and rel >= 4 and i % 2 == 0: drums.note(t0 + i * S, S, CHH, 50)
    if drop and rel % 8 == 7:
        for i, d in enumerate((TOMH, TOMH, SNARE, SNARE)):
            drums.note(t0 + (12 + i) * S, S, d, 90 + i * 8)
    if build:
        # Snare roll: quarters, then 8ths, then 16ths, louder as it goes
        step = PPQ if rel < 4 else (PPQ // 2 if rel < 6 else S if rel < 7 else S // 2)
        if rel >= 2:
            k = 0
            while k * step < BAR:
                prog = (rel * BAR + k * step) / (slen * BAR)
                drums.note(t0 + k * step, min(step, S), SNARE, 50 + 70 * prog)
                k += 1
    if s == 'Break' and rel >= 4:
        for beat in range(4): drums.note(t0 + beat * PPQ + PPQ // 2, S, CHH, 45)

    # ---------- bass ----------
    rb = BASS_ROOT[root]
    if drop:
        # Off-beat pumping bass with an octave pop
        for beat in range(4):
            bass.note(t0 + beat * PPQ + PPQ // 2, PPQ // 2 - 20, rb + 12, 108)
            bass.note(t0 + beat * PPQ + 3 * S, S - 10, rb + 24 if beat == 3 else rb + 12, 80)
        sub.note(t0, BAR - 20, rb, 100)
    elif build and rel >= 4:
        for i in range(8): bass.note(t0 + i * PPQ // 2, PPQ // 2 - 30, rb + 12, 70 + rel * 4)
    elif s == 'Outro' and rel < 6:
        for beat in range(4): bass.note(t0 + beat * PPQ + PPQ // 2, PPQ // 2 - 20, rb + 12, 95)
    elif s == 'Break' and rel >= 4:
        sub.note(t0, BAR - 20, rb, 80)

    # ---------- chords: supersaw stabs in drops, pumping ----------
    if drop:
        rhythm = [0, 3, 6, 10, 12, 14]  # in 16ths
        for i in rhythm:
            ln = 2 * S if i in (6, 12) else S + S // 2
            for p in triad + [triad[0] + 12]:
                chords.note(t0 + i * S, ln, p + 12, 92)
    if build and rel >= 4:
        for i in range(0, 16, 2):
            for p in triad: chords.note(t0 + i * S, S, p + 12, 50 + rel * 6)

    # ---------- pad with side-chain style pumping (CC11) ----------
    if s in ('Intro', 'Build', 'Break', 'Build 2', 'Outro') or drop:
        for p in triad: pad.note(t0, BAR - 10, p, 70 if not drop else 60)
        pad.note(t0, BAR - 10, rb + 24, 60)
    pump = drop or (s == 'Outro' and rel < 6)
    for beat in range(4):
        tb = t0 + beat * PPQ
        for k, v in enumerate((25, 60, 90, 110, 120, 127) if pump else (127,)):
            pad.cc(tb + k * (PPQ // 6), 11, v)
            chords.cc(tb + k * (PPQ // 6), 11, max(v, 70) if pump else 127)

    # ---------- arp ----------
    if s in ('Intro', 'Build', 'Drop', 'Build 2', 'Drop 2') or (s == 'Outro' and rel < 4):
        pat = [0, 1, 2, 1, 3, 2, 1, 2] * 2
        notes = triad + [triad[0] + 12]
        base = 62 if s == 'Intro' else 75 if build else 68
        for i, k in enumerate(pat):
            vel = base + (12 if i % 4 == 0 else 0)
            if s == 'Intro': vel = vel * (0.5 + 0.5 * rel / 8)
            arp.note(t0 + i * S, S - 15, notes[k] + 12, vel)

    # ---------- bells in intro/outro ----------
    if s in ('Intro', 'Outro') and rel % 2 == 0:
        bells.note(t0, PPQ * 2, triad[2] + 24, 70)

# ---------- lead hook (4-bar phrase, in 16ths) ----------
HOOK_A = [(0,2,'E5'),(2,2,'E5'),(4,1,'D5'),(6,2,'C5'),(8,2,'A4'),(11,3,'C5'),(14,2,'D5'),
          (16,2,'C5'),(18,2,'C5'),(20,2,'A4'),(22,2,'C5'),(24,4,'F5'),(28,4,'E5'),
          (32,2,'E5'),(34,2,'G5'),(36,2,'E5'),(38,2,'D5'),(40,2,'C5'),(43,3,'D5'),(46,2,'E5'),
          (48,3,'D5'),(51,3,'B4'),(54,2,'G4'),(56,6,'B4'),(62,2,'D5')]
HOOK_B = HOOK_A[:-5] + [(48,2,'G5'),(50,2,'A5'),(52,2,'G5'),(54,2,'E5'),(56,8,'D5')]

def play_phrase(trk, bar, hook, vel, transpose=0, legato=0.9):
    for st, ln, nm in hook:
        trk.note(bar * BAR + st * S, int(ln * S * legato), n(nm) + transpose, vel + (8 if st % 8 == 0 else 0))

for name in ('Drop', 'Drop 2'):
    a, z = starts[name]
    for k, bar in enumerate(range(a, z, 4)):
        hook = HOOK_A if k % 2 == 0 else HOOK_B
        play_phrase(lead, bar, hook, 98)
        if name == 'Drop 2':               # double an octave up in the last drop
            play_phrase(lead, bar, hook, 70, 12)

# Break: piano plays the hook softly, with left-hand chords
a, z = starts['Break']
for k, bar in enumerate(range(a, z, 4)):
    play_phrase(piano, bar, HOOK_A if k == 0 else HOOK_B, 70, legato=1.0)
for bar in range(a, z):
    root, triad = chord(bar)
    for i, p in enumerate(triad): piano.note(bar * BAR + i * 30, BAR - 40, p - 12, 55)

# Build 2: lead teases the first bar of the hook, filtered by volume
a, z = starts['Build 2']
for bar in range(a, a + 4):
    for st, ln, nm in HOOK_A[:7]:
        lead.note(bar * BAR + st * S, int(ln * S * 0.8), n(nm), 50 + (bar - a) * 8)

# ---------- risers: pitch-bend sweep up across each build, white-noise sweep ----------
for name in ('Build', 'Build 2'):
    a, z = starts[name]
    t_start, t_end = (a + 4) * BAR, z * BAR
    fx.cc(t_start, 101, 0); fx.cc(t_start, 100, 0); fx.cc(t_start, 6, 12)  # bend range 12 semitones
    fx.bend(t_start, -8192)
    fx.note(t_start, t_end - t_start, n('A4'), 100)
    fx.note(t_start, t_end - t_start, n('E5'), 90)
    steps = 64
    for i in range(steps + 1):
        t = t_start + (t_end - t_start) * i // steps
        fx.bend(t, -8192 + 16383 * i / steps)
        fx.cc(t, 11, 40 + 87 * i / steps)
    fx.bend(t_end + 10, 0)
    fx.cc(t_end + 10, 11, 127)

# Fade out in the last 4 bars
for trk in (pad, bells, arp, bass, chords, drums, piano):
    a = (TOTAL - 4) * BAR
    for i in range(33):
        trk.cc(a + i * BAR // 8, 7, {'Drums':110}.get(trk.name, 90) * (1 - i / 32))

mid = mido.MidiFile(type=1, ticks_per_beat=PPQ)
meta = mido.MidiTrack()
meta.append(mido.MetaMessage('track_name', name='Copper Lights', time=0))
meta.append(mido.MetaMessage('copyright', text='Arranger MIDI DAW demo song. Free to use.', time=0))
meta.append(mido.MetaMessage('set_tempo', tempo=mido.bpm2tempo(BPM), time=0))
meta.append(mido.MetaMessage('time_signature', numerator=4, denominator=4, time=0))
meta.append(mido.MetaMessage('key_signature', key='Am', time=0))
meta.append(mido.Message('sysex', data=[0x7E, 0x7F, 0x09, 0x01], time=0))  # GM System On
last = 0
for name, bars in SECTIONS:
    t = starts[name][0] * BAR
    meta.append(mido.MetaMessage('marker', text=name, time=t - last)); last = t
meta.append(mido.MetaMessage('end_of_track', time=TOTAL * BAR - last + PPQ))
mid.tracks.append(meta)
for trk in (drums, bass, sub, chords, pad, arp, lead, piano, bells, fx):
    mid.tracks.append(trk.midi())
mid.save(sys.argv[1])
print('bars', TOTAL, 'seconds', round(TOTAL * 4 * 60 / BPM, 1))
