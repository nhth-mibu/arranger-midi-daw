"""Writes 'Northern Fire', a slow rock ballad demo song, as a General MIDI file (type 1)."""
import sys, math, random
import mido

random.seed(11)
PPQ = 480
E8 = PPQ // 2
S = PPQ // 4
BAR = PPQ * 4
BPM = 72

def n(name):
    names = {'C':0,'C#':1,'Db':1,'D':2,'D#':3,'Eb':3,'E':4,'F':5,'F#':6,'Gb':6,'G':7,'G#':8,'Ab':8,'A':9,'A#':10,'Bb':10,'B':11}
    return 12 * (int(name[-1]) + 1) + names[name[:-1]]

class Track:
    def __init__(self, name, ch, program, vol=100, pan=64, rev=40, cho=0):
        self.name, self.ch, self.ev = name, ch, []
        for c, v in ((0, 0), (7, vol), (10, pan), (91, rev), (93, cho), (11, 127), (101, 0), (100, 0), (6, 2), (38, 0)):
            self.cc(0, c, v)
        self.ev.append((0, 2, mido.Message('program_change', channel=ch, program=program)))
    def note(self, t, length, pitch, vel):
        self.ev.append((t, 3, mido.Message('note_on', channel=self.ch, note=pitch, velocity=max(1, min(127, int(vel))))))
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

SECTIONS = [('Intro', 4), ('Verse', 8), ('Chorus', 8), ('Interlude', 2), ('Verse 2', 8),
            ('Chorus 2', 8), ('Solo', 8), ('Chorus 3', 8), ('Outro', 4)]
starts, b = {}, 0
for name, bars in SECTIONS:
    starts[name] = (b, b + bars); b += bars
TOTAL = b

VERSE = ['Em', 'C', 'G', 'D', 'Em', 'C', 'Am', 'D']
CHORUS = ['C', 'D', 'G', 'Em', 'C', 'D', 'Em', 'Em']
SOLO = ['Em', 'C', 'G', 'D', 'Em', 'C', 'D', 'D']
CHORDS = {}
for name, (a, z) in starts.items():
    seq = CHORUS if name.startswith('Chorus') else SOLO if name == 'Solo' else VERSE
    for i in range(a, z):
        CHORDS[i] = seq[(i - a) % len(seq)]
CHORDS[TOTAL - 1] = 'Em'

# Guitar voicings (low to high) and roots
VOICE = {'Em': ['E2','B2','E3','G3','B3','E4'], 'C': ['C3','E3','G3','C4','E4'], 'G': ['G2','B2','D3','G3','B3','G4'],
         'D': ['D3','A3','D4','F#4'], 'Am': ['A2','E3','A3','C4','E4']}
ROOT = {'Em': n('E2'), 'C': n('C2'), 'G': n('G2'), 'D': n('D2'), 'Am': n('A2')}
FIFTH = {k: v + 7 for k, v in ROOT.items()}

def sec_of(bar):
    for name, _ in SECTIONS:
        if starts[name][0] <= bar < starts[name][1]: return name

drums  = Track('Drums', 9, 16, vol=108, rev=45)               # GS Power kit (GM players use Standard)
bass   = Track('Bass', 0, 34, vol=110, pan=64, rev=10)          # Electric Bass (pick)
clean  = Track('Clean Guitar', 1, 27, vol=92, pan=44, rev=70, cho=60)   # Electric Guitar (clean)
rhyL   = Track('Rhythm Guitar L', 2, 30, vol=84, pan=20, rev=35)        # Distortion Guitar
rhyR   = Track('Rhythm Guitar R', 3, 29, vol=80, pan=108, rev=35)       # Overdriven Guitar
lead   = Track('Lead Guitar', 4, 30, vol=100, pan=64, rev=75, cho=20)   # Distortion Guitar
strings= Track('Strings', 5, 49, vol=72, pan=74, rev=90)                # Slow Strings
organ  = Track('Organ', 6, 18, vol=58, pan=84, rev=60)                  # Rock Organ

KICK, SNARE, STICK, CHH, OHH, RIDE, CRASH, CRASH2 = 36, 38, 37, 42, 46, 51, 49, 57
TOMS = [50, 48, 47, 45, 43, 41]

for bar in range(TOTAL):
    s = sec_of(bar); t0 = bar * BAR; ch = CHORDS[bar]
    a, z = starts[s]; rel = bar - a; last = bar == z - 1
    chorus = s.startswith('Chorus') or s == 'Solo'
    verse = s.startswith('Verse')
    nxt = CHORDS.get(bar + 1, 'Em')

    # ---------- drums ----------
    if rel == 0 and (chorus or s in ('Verse 2', 'Interlude', 'Outro')):
        drums.note(t0, PPQ, CRASH, 115); drums.note(t0, PPQ, KICK, 118)
    if s == 'Intro' and rel == 3:
        for i, tom in enumerate([45, 45, 43, 43, 41, 41]):
            drums.note(t0 + 2 * PPQ + i * (PPQ // 3), S, tom, 80 + i * 6)
    if verse or chorus or s == 'Interlude':
        soft = s == 'Verse'
        fill = last and s != 'Interlude'
        for beat in range(4 if not fill else 2):
            tb = t0 + beat * PPQ
            if beat == 0 or (beat == 2):
                drums.note(tb, S, KICK, 112 if not soft else 96)
            if beat in (1, 3):
                drums.note(tb, S, STICK if soft else SNARE, 80 if soft else 118)
            if beat == 2 and not soft:
                drums.note(tb + E8 + S, S, KICK, 92)
            for e in range(2):
                inst = (OHH if e == 0 else CHH) if s == 'Solo' else (RIDE if chorus else CHH)
                drums.note(tb + e * E8, S, inst, (78 if e == 0 else 60) + random.randint(-6, 6))
        if fill:
            # Tom fill across beats 3-4 in 16ths
            for i in range(8):
                drums.note(t0 + 2 * PPQ + i * S, S, TOMS[min(5, i * 6 // 8)], 90 + i * 4)
                if i % 4 == 0: drums.note(t0 + 2 * PPQ + i * S, S, KICK, 100)
    if s == 'Outro':
        if rel < 3:
            drums.note(t0, S, KICK, 90); drums.note(t0 + 2 * PPQ, S, KICK, 85)
            drums.note(t0 + PPQ, S, STICK, 70); drums.note(t0 + 3 * PPQ, S, STICK, 70)
        else:
            drums.note(t0, BAR, CRASH, 110); drums.note(t0, BAR, CRASH2, 100); drums.note(t0, S, KICK, 118)

    # ---------- bass: a driving 8th-note line in the loud parts, long notes in the soft parts ----------
    r = ROOT[ch]
    if chorus or s == 'Interlude':
        line = [r] * 6 + [r + 12, FIFTH[ch]]
        # walk to the next chord on the last 8th
        if ROOT[nxt] != r:
            step = 1 if ROOT[nxt] > r else -1
            line[7] = ROOT[nxt] - step * 2
        for i, p in enumerate(line):
            bass.note(t0 + i * E8, E8 - 30, p, 112 if i % 2 == 0 else 98)
    elif s == 'Verse 2':
        for i, p in enumerate([r, r, r + 12, r, FIFTH[ch], r, r + 12, FIFTH[ch]]):
            bass.note(t0 + i * E8, E8 - 40, p, 100 if i % 2 == 0 else 84)
    elif s == 'Verse':
        bass.note(t0, PPQ * 2 + E8, r, 98); bass.note(t0 + 2 * PPQ + E8, E8, r, 84); bass.note(t0 + 3 * PPQ, PPQ - 20, FIFTH[ch], 90)
    elif s == 'Outro' and rel == 3:
        bass.note(t0, BAR, ROOT['Em'], 110)

    # ---------- clean guitar arpeggio ----------
    if s in ('Intro', 'Verse', 'Verse 2', 'Interlude', 'Outro') and not (s == 'Outro' and rel == 3):
        v = [n(x) for x in VOICE[ch]]
        pat = [0, 2, 3, 4, len(v) - 1, 4, 3, 2]
        for i, k in enumerate(pat):
            k = min(k, len(v) - 1)
            clean.note(t0 + i * E8, E8 * 3, v[k], (74 if i % 4 == 0 else 62) + random.randint(-4, 4))
    if s == 'Outro' and rel == 3:
        for i, p in enumerate(VOICE['Em']): clean.note(t0 + i * 40, BAR * 2, n(p), 70)

    # ---------- distorted rhythm guitars: power chords, doubled left and right ----------
    if chorus or s == 'Interlude' or s == 'Verse 2' and rel >= 4 or s == 'Outro' and rel == 3:
        pc = [r + 12, r + 19, r + 24]
        if s == 'Verse 2' or (s == 'Outro'):
            hits = [(0, BAR - 30, 100)]
        else:
            hits = [(0, PPQ * 2 - 30, 110)] + [(2 * PPQ + i * E8, E8 - 40, 96 if i % 2 else 104) for i in range(4)]
        for st, ln, vel in hits:
            for p in pc:
                rhyL.note(t0 + st, ln, p, vel)
                rhyR.note(t0 + st + 12, ln, p, vel - 6)

    # ---------- strings and organ under the loud parts ----------
    if chorus or s == 'Outro' or s == 'Verse 2':
        v = [n(x) + 12 for x in VOICE[ch]][-3:]
        for p in v: strings.note(t0, BAR - 20, p, 70 if s != 'Verse 2' else 55)
    if chorus:
        for p in [n(x) for x in VOICE[ch]][-3:]: organ.note(t0, BAR - 20, p, 70)

# ---------- lead guitar: melodies with bends and vibrato ----------
def phrase(bar, notes, vel=100, unit=E8):
    """notes: (start, length, pitch, bend) in `unit`s. bend: 0 none, 1 whole-tone bend up into the note,
    2 half-tone bend. Long notes get vibrato."""
    t0 = bar * BAR
    for st, ln, nm, bnd in notes:
        t, L, p = t0 + st * unit, ln * unit, n(nm)
        lead.bend(t, 0)
        if bnd:
            semis = 2 if bnd == 1 else 1
            p -= semis
            target = 8191 if semis == 2 else 4096
            steps = 10
            for i in range(1, steps + 1):
                lead.bend(t + int(E8 * 0.8 * i / steps), target * (math.sin(i / steps * math.pi / 2)))
            base = target
        else:
            base = 0
        lead.note(t, int(L * 0.96), p, vel + (8 if st % 4 == 0 else 0))
        if L >= 3 * E8:
            # vibrato: starts after a beat, grows deeper
            k, tt = 0, t + PPQ
            while tt < t + L - 30:
                depth = min(900, 200 + k * 25)
                val = base - depth * (0.5 + 0.5 * math.sin(k * 0.9)) if bnd else base + depth * 0.6 * math.sin(k * 0.9)
                lead.bend(tt, val)
                k += 1; tt += 22
            lead.bend(t + L - 10, base)

VERSE_MEL = [
    [(1,1,'B4',0),(2,1,'B4',0),(3,1,'A4',0),(4,2,'G4',0),(6,2,'A4',0),(8,3,'B4',0),(11,1,'A4',0),(12,4,'G4',0)],
    [(1,1,'D5',0),(2,1,'D5',0),(3,1,'B4',0),(4,2,'A4',0),(6,2,'G4',0),(8,6,'A4',1)],
    [(1,1,'B4',0),(2,1,'B4',0),(3,1,'D5',0),(4,2,'E5',0),(6,2,'D5',0),(8,3,'B4',0),(11,1,'A4',0),(12,4,'G4',0)],
    [(1,1,'A4',0),(2,1,'B4',0),(3,2,'C5',0),(5,1,'B4',0),(6,2,'A4',0),(8,7,'F#4',2)],
]
CHORUS_MEL = [
    [(0,2,'E5',0),(2,2,'E5',0),(4,2,'D5',0),(6,2,'E5',0),(8,3,'F#5',0),(11,1,'E5',0),(12,4,'D5',0),
     (16,2,'D5',0),(18,2,'B4',0),(20,2,'D5',0),(22,2,'G5',1),(24,8,'E5',1)],
    [(0,2,'E5',0),(2,2,'E5',0),(4,2,'D5',0),(6,2,'E5',0),(8,3,'F#5',0),(11,1,'G5',0),(12,4,'A5',1),
     (16,4,'G5',0),(20,2,'F#5',0),(22,2,'D5',0),(24,8,'E5',1)],
]
for name in ('Verse', 'Verse 2'):
    a, _ = starts[name]
    for k, ph in enumerate(VERSE_MEL):
        phrase(a + 2 * k, ph, vel=78 if name == 'Verse' else 86)
for name in ('Chorus', 'Chorus 2', 'Chorus 3'):
    a, _ = starts[name]
    phrase(a, CHORUS_MEL[0], vel=100)
    phrase(a + 4, CHORUS_MEL[1], vel=104)
    if name == 'Chorus 3':  # last chorus: lead doubled an octave up by the rhythm guitar R? keep it simple: higher ending
        phrase(a + 7, [(4, 4, 'B5', 1)], vel=110)

# ---------- vocal: the melody with lyrics (FF 05 lyric events), silent by default ----------
# Volume 0 so the demo stays instrumental: raise the fader to hear the melody as "Voice Oohs".
# The lyrics show in the Score and on keyboards that read MIDI lyrics (such as the PSR-S970).
vocal = Track('Vocal', 7, 53, vol=0, pan=64, rev=60)
LYRICS = {
    'Verse': ["Cold wind is call-ing through the night", "Out on the road a-lone",
              "Ev-ery light a-cross the ci-ty", "Burns for the ones gone home"],
    'Verse 2': ["I hear your voice a-cross the years", "Ech-o-ing in the dark",
                "Hold me now, the storm is com-ing", "Two hearts, one guid-ing spark"],
    'Chorus': ["North-ern fire, take me high-er, light the sky to-night",
               "North-ern fire, nev-er dy-ing, bring me the light"],
}
LYRICS['Chorus 2'] = LYRICS['Chorus 3'] = LYRICS['Chorus']

def syllables(line):
    out = []
    for word in line.replace(',', ',').split():
        parts = word.split('-')
        for i, p in enumerate(parts):
            out.append(p if i == len(parts) - 1 else p + '-')
    return out

def sing(bar, notes, line, vel=90):
    syl = syllables(line)
    assert len(syl) == len(notes), (line, len(syl), len(notes))
    for (st, ln, nm, _), word in zip(notes, syl):
        t = bar * BAR + st * E8
        vocal.note(t, int(ln * E8 * 0.95), n(nm), vel)
        # MIDI lyric convention: a space after the last syllable of a word
        vocal.ev.append((t, 2, mido.MetaMessage('lyrics', text=word[:-1] if word.endswith('-') else word + ' ')))

for name in ('Verse', 'Verse 2'):
    a, _ = starts[name]
    for k, ph in enumerate(VERSE_MEL):
        sing(a + 2 * k, ph, LYRICS[name][k])
for name in ('Chorus', 'Chorus 2', 'Chorus 3'):
    a, _ = starts[name]
    sing(a, CHORUS_MEL[0], LYRICS[name][0])
    sing(a + 4, CHORUS_MEL[1], LYRICS[name][1])

# Interlude and intro: short lead hooks
phrase(starts['Intro'][0] + 2, [(4,2,'B4',0),(6,2,'D5',0),(8,8,'E5',1)], vel=88)
phrase(starts['Interlude'][0], [(0,2,'G5',0),(2,2,'F#5',0),(4,2,'E5',0),(6,2,'D5',0),(8,6,'E5',1),(14,2,'B4',0)], vel=100)

# Guitar solo, 8 bars, screaming bends up to E6
SOLO_NOTES = [
    # bar 1-2 (Em, C): slow, singing
    (0,4,'B5',1),(4,2,'A5',0),(6,2,'G5',0),(8,8,'E5',0),
    (16,3,'G5',0),(19,1,'A5',0),(20,4,'B5',1),(24,8,'G5',2),
    # bar 3-4 (G, D)
    (32,2,'D6',1),(34,2,'B5',0),(36,2,'A5',0),(38,2,'G5',0),(40,4,'A5',1),(44,4,'F#5',0),
    (48,2,'D5',0),(50,2,'E5',0),(52,2,'F#5',0),(54,2,'A5',0),(56,8,'A5',1),
]
phrase(starts['Solo'][0], SOLO_NOTES, vel=106)
# bars 5-8: faster 16th runs on E minor pentatonic, then a long high scream
pent = [n(x) for x in ('E5','G5','A5','B5','D6','E6')]
t = (starts['Solo'][0] + 4) * BAR
run = [5,4,3,4,3,2,3,2,1,2,1,0, 1,2,3,4, 3,4,5,4,3,4,3,2, 1,2,3,2,1,0,1,2]
for i, k in enumerate(run):
    lead.bend(t + i * S, 0)
    lead.note(t + i * S, S - 15, pent[k], 96 + (10 if i % 4 == 0 else 0))
phrase(starts['Solo'][0] + 6, [(0,2,'D6',0),(2,6,'E6',1),(8,4,'D6',0),(12,4,'B5',2)], vel=112)

# Outro: one last held, bent note over the final chord
phrase(starts['Outro'][0] + 1, [(0,2,'D5',0),(2,2,'E5',0),(4,4,'G5',0),(8,16,'E5',1)], vel=96)

# Fade the clean guitar into the intro, swell strings into each chorus
a = starts['Intro'][0] * BAR
for i in range(17): clean.cc(a + i * BAR // 8, 11, 60 + 67 * i / 16)

mid = mido.MidiFile(type=1, ticks_per_beat=PPQ)
meta = mido.MidiTrack()
meta.append(mido.MetaMessage('track_name', name='Northern Fire', time=0))
meta.append(mido.MetaMessage('copyright', text='Arranger MIDI DAW demo song. Free to use.', time=0))
meta.append(mido.MetaMessage('set_tempo', tempo=mido.bpm2tempo(BPM), time=0))
meta.append(mido.MetaMessage('time_signature', numerator=4, denominator=4, time=0))
meta.append(mido.MetaMessage('key_signature', key='Em', time=0))
meta.append(mido.Message('sysex', data=[0x7E, 0x7F, 0x09, 0x01], time=0))
last = 0
for name, _ in SECTIONS:
    t = starts[name][0] * BAR
    meta.append(mido.MetaMessage('marker', text=name, time=t - last)); last = t
meta.append(mido.MetaMessage('end_of_track', time=TOTAL * BAR - last + BAR))
mid.tracks.append(meta)
for trk in (drums, bass, clean, rhyL, rhyR, lead, vocal, strings, organ):
    mid.tracks.append(trk.midi())
mid.save(sys.argv[1])
print('bars', TOTAL, 'seconds', round(TOTAL * 4 * 60 / BPM, 1))
