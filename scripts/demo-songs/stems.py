import mido, sys
m = mido.MidiFile(sys.argv[1])
for i, tr in enumerate(m.tracks[1:], 1):
    name = tr[0].name if tr[0].type == 'track_name' else f'Track {i}'
    out = mido.MidiFile(type=1, ticks_per_beat=m.ticks_per_beat)
    out.tracks.append(m.tracks[0]); out.tracks.append(tr)
    out.save(f'stems/{i:02d} {name}.mid'); print(name)
