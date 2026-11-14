"""Makes two demo projects from a MIDI file: one with MIDI tracks (from the app's import),
one with audio tracks that play rendered stems. Usage: build_projects.py <midi project.arranger> <title> <bpm> <out folder>"""
import json, sys, os, subprocess, copy, math
src, title, bpm, out = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
d = json.load(open(src))
s = d['song']
s['title'] = title
s['soundFont'] = ''
s['loop']['enabled'] = False
s['metronome']['enabled'] = False
GM = {0:'Acoustic Grand Piano',18:'Rock Organ',27:'Electric Guitar (clean)',29:'Overdriven Guitar',30:'Distortion Guitar',
      34:'Electric Bass (pick)',38:'Synth Bass 1',39:'Synth Bass 2',49:'String Ensemble 2',80:'Lead 1 (square)',
      81:'Lead 2 (sawtooth)',89:'Pad 2 (warm)',90:'Pad 3 (polysynth)',98:'FX 3 (crystal)',102:'FX 7 (echoes)'}
KITS = {0:'Standard Kit',16:'Power Kit',25:'Electronic Kit',26:'TR-808 Kit'}
for t in s['tracks']:
    snd = t['sound']
    snd['name'] = (KITS if t['output']['channel'] == 10 else GM).get(snd['program'], snd['name'])
    snd['bankMsb'] = -1
json.dump(d, open(os.path.join(out, f'{title} (MIDI).arranger'), 'w'), indent=2)

a = copy.deepcopy(d); sa = a['song']
stems = {f[3:-4]: f for f in os.listdir(os.path.join(out, 'Stems')) if f.endswith('.ogg')}
# Tracks with no stem (such as a silent vocal guide) are left out of the audio project.
sa['tracks'] = [t for t in sa['tracks'] if t['name'] in stems]
next_id = sa['nextId']
for t in sa['tracks']:
    f = stems[t['name']]
    dur = float(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', os.path.join(out, 'Stems', f)]))
    t['output'] = {'kind': 'audio', 'port': '', 'channel': 1}
    t['sound'] = {'name': '', 'bankMsb': -1, 'bankLsb': -1, 'program': 0}
    t['mixer'].update({'volume': 100, 'pan': 64, 'reverb': 0, 'chorus': 0})
    t['clips'] = []
    t['audio'] = [{'id': next_id, 'name': t['name'], 'file': 'Stems/' + f, 'start': 0,
                   'length': int(math.ceil(dur * bpm / 60 * 960)), 'offset': 0.0, 'gainDb': 0.0,
                   'fadeIn': 0, 'fadeOut': 0, 'bpm': bpm}]
    next_id += 1
sa['nextId'] = next_id
json.dump(a, open(os.path.join(out, f'{title} (Audio).arranger'), 'w'), indent=2)
