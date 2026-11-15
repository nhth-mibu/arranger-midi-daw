"""
Arranger MIDI DAW: Anticipatory Music Transformer helper.

    python arranger_generate.py run <request.json> <answer.json>
    python arranger_generate.py prefetch [small|medium]

The app writes a request and reads the answer (see src/core/include/arranger/core/MidiAi.h).
It prints "PROGRESS <0..1> <text>" lines, then "RESULT <answer.json>" or "ERROR <text>".

The model is the Anticipatory Music Transformer by John Thickstun, David Hall,
Chris Donahue and Percy Liang (https://github.com/jthickstun/anticipation,
Apache License 2.0), with weights from the Stanford CRFM on the Hugging Face
Hub. The token layout, the operations and the sampling loop below are taken
from that project (anticipation/config.py, vocab.py, ops.py, sample.py), so
the helper needs no git checkout. The changes: a part can be limited to some
instruments, long ranges go in pieces, and progress goes to the app.
"""

import json
import math
import os
import sys

MODELS = {
    "small": "stanford-crfm/music-small-800k",
    "medium": "stanford-crfm/music-medium-800k",
}
MODELS_DIR = os.environ.get("ARRANGER_AI_MODELS", os.path.join(os.path.dirname(os.path.abspath(__file__)), "models"))

# ---- anticipation/config.py and vocab.py -----------------------------------------------------
DELTA = 5
MAX_TIME_IN_SECONDS = 100
MAX_DURATION_IN_SECONDS = 10
TIME_RESOLUTION = 100
MAX_PITCH = 128
MAX_INSTR = 129
MAX_NOTE = MAX_PITCH * MAX_INSTR
MAX_TIME = TIME_RESOLUTION * MAX_TIME_IN_SECONDS
MAX_DUR = TIME_RESOLUTION * MAX_DURATION_IN_SECONDS
MAX_NOTES_AT_TIME = 8  # a chord, not a stuck time token

TIME_OFFSET = 0
DUR_OFFSET = TIME_OFFSET + MAX_TIME
NOTE_OFFSET = DUR_OFFSET + MAX_DUR
REST = NOTE_OFFSET + MAX_NOTE
CONTROL_OFFSET = NOTE_OFFSET + MAX_NOTE + 1
ATIME_OFFSET = CONTROL_OFFSET + 0
ADUR_OFFSET = ATIME_OFFSET + MAX_TIME
ANOTE_OFFSET = ADUR_OFFSET + MAX_DUR
SPECIAL_OFFSET = ANOTE_OFFSET + MAX_NOTE
SEPARATOR = SPECIAL_OFFSET
AUTOREGRESS = SPECIAL_OFFSET + 1
ANTICIPATE = SPECIAL_OFFSET + 2
DRUMS = 128


def say(line):
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def progress(p, text):
    say("PROGRESS %.2f %s" % (max(0.0, min(1.0, p)), text))


# ---- anticipation/ops.py ----------------------------------------------------------------------
def triples(tokens):
    return zip(tokens[0::3], tokens[1::3], tokens[2::3])


def sort(tokens):
    times = tokens[0::3]
    out = []
    for i in sorted(range(len(times)), key=times.__getitem__):
        out.extend(tokens[3 * i:3 * (i + 1)])
    return out


def split(tokens):
    events, controls = [], []
    for t, d, n in triples(tokens):
        (events if n < CONTROL_OFFSET else controls).extend([t, d, n])
    return events, controls


def max_time(tokens):
    mt = 0
    for t, d, n in triples(tokens):
        if n == SEPARATOR:
            continue
        mt = max(mt, t - (TIME_OFFSET if n < CONTROL_OFFSET else ATIME_OFFSET))
    return mt


def min_time(tokens):
    mt = None
    for t, d, n in triples(tokens):
        if n == SEPARATOR:
            break
        t -= TIME_OFFSET if n < CONTROL_OFFSET else ATIME_OFFSET
        mt = t if mt is None else min(mt, t)
    return mt or 0


def clip(tokens, start, end):
    out = []
    for t, d, n in triples(tokens):
        this = t - (TIME_OFFSET if n < CONTROL_OFFSET else ATIME_OFFSET)
        if start <= this <= end:
            out.extend([t, d, n])
    return out


def pad(tokens, end_time):
    end_time = TIME_OFFSET + end_time
    out = []
    previous = TIME_OFFSET
    for t, d, n in triples(tokens):
        while t > previous + TIME_RESOLUTION:
            out.extend([previous + TIME_RESOLUTION, DUR_OFFSET, REST])
            previous += TIME_RESOLUTION
        out.extend([t, d, n])
        previous = t
    while end_time > previous + TIME_RESOLUTION:
        out.extend([previous + TIME_RESOLUTION, DUR_OFFSET, REST])
        previous += TIME_RESOLUTION
    return out


def unpad(tokens):
    out = []
    for t, d, n in triples(tokens):
        if n != REST:
            out.extend([t, d, n])
    return out


def anticipate(events, controls, delta=DELTA * TIME_RESOLUTION):
    if not controls:
        return events, controls
    tokens = []
    event_time = 0
    control_time = controls[0] - ATIME_OFFSET
    for t, d, n in triples(events):
        while event_time >= control_time - delta:
            tokens.extend(controls[0:3])
            controls = controls[3:]
            control_time = controls[0] - ATIME_OFFSET if controls else float("inf")
        event_time = t - TIME_OFFSET
        tokens.extend([t, d, n])
    return tokens, controls


def instruments_of(tokens):
    found = set()
    for t, d, n in triples(tokens):
        if n >= SPECIAL_OFFSET:
            continue
        n -= NOTE_OFFSET if n < CONTROL_OFFSET else ANOTE_OFFSET
        found.add(n // MAX_PITCH)
    return found


# ---- anticipation/sample.py (with an instrument limit) ----------------------------------------
def sample_event(torch, model, z, tokens, top_p, current_time, allowed, force_later=False, min_step=12):
    F = torch.nn.functional
    history = tokens[max(len(tokens) - 1017, 0):]
    offset = min_time(history)
    history = list(history)
    history[::3] = [tok - offset for tok in history[::3]]
    new = []
    with torch.no_grad():
        for i in range(3):
            ids = torch.tensor(z + history + new).unsqueeze(0).to(model.device)
            logits = model(ids).logits[0, -1].float()
            idx = ids.shape[1] - 1
            logits[CONTROL_OFFSET:] = -float("inf")  # no controls, no special tokens
            if idx % 3 == 0:
                logits[DUR_OFFSET:DUR_OFFSET + MAX_DUR] = -float("inf")
                logits[NOTE_OFFSET:NOTE_OFFSET + MAX_NOTE] = -float("inf")
                if force_later:
                    # The model kept choosing this instant: make it move on.
                    end = max(0, min(MAX_TIME, current_time - offset + min_step))
                    if 0 < end < MAX_TIME:
                        logits[TIME_OFFSET:TIME_OFFSET + end] = -float("inf")
                elif current_time - offset > 0:
                    logits[TIME_OFFSET:TIME_OFFSET + current_time - offset] = -float("inf")
            elif idx % 3 == 1:
                logits[TIME_OFFSET:TIME_OFFSET + MAX_TIME] = -float("inf")
                logits[NOTE_OFFSET:NOTE_OFFSET + MAX_NOTE] = -float("inf")
            else:
                logits[TIME_OFFSET:TIME_OFFSET + MAX_TIME] = -float("inf")
                logits[DUR_OFFSET:DUR_OFFSET + MAX_DUR] = -float("inf")
                keep = torch.zeros_like(logits, dtype=torch.bool)
                for instr in allowed:
                    keep[NOTE_OFFSET + instr * MAX_PITCH:NOTE_OFFSET + (instr + 1) * MAX_PITCH] = True
                logits[~keep] = -float("inf")
            if top_p < 1.0:
                sorted_logits, sorted_idx = torch.sort(logits, descending=True)
                cumulative = torch.cumsum(F.softmax(sorted_logits, dim=-1), dim=-1)
                remove = cumulative > top_p
                remove[..., 1:] = remove[..., :-1].clone()
                remove[..., 0] = False
                logits[remove.scatter(0, sorted_idx, remove)] = -float("inf")
            new.append(int(torch.multinomial(F.softmax(logits, dim=-1), 1)))
    new[0] += offset
    return new


def generate(torch, model, start_time, end_time, inputs, controls, top_p, allowed, report, min_step=12):
    """start_time and end_time in 10 ms steps. Returns the new events (no controls)."""
    prompt = pad(clip(inputs, 0, start_time), start_time)
    future = clip(inputs, start_time + 1, max(max_time(inputs), start_time + 1))
    controls = clip(controls, DELTA * TIME_RESOLUTION, max(max_time(controls), DELTA * TIME_RESOLUTION))
    z = [ANTICIPATE] if controls or future else [AUTOREGRESS]
    tokens, pending = anticipate(prompt, sort(controls + [CONTROL_OFFSET + t for t in future]))
    current = max_time(prompt)
    if pending:
        atime, adur, anote = pending[0:3]
        pending = pending[3:]
        anticipated = atime - ATIME_OFFSET
    else:
        anticipated = math.inf
    made = []
    same_time = 0
    for _ in range(4000):  # a limit, in case the model never moves on
        while current >= anticipated - DELTA * TIME_RESOLUTION:
            tokens.extend([atime, adur, anote])
            if pending:
                atime, adur, anote = pending[0:3]
                pending = pending[3:]
                anticipated = atime - ATIME_OFFSET
            else:
                anticipated = math.inf
        new = sample_event(torch, model, z, tokens, top_p, max(start_time, current), allowed,
                           force_later=same_time >= MAX_NOTES_AT_TIME, min_step=min_step)
        new_time = new[0] - TIME_OFFSET
        if new_time >= end_time:
            break
        tokens.extend(new)
        made.extend(new)
        if new_time > current:
            same_time = 0
        else:
            same_time += 1
        current = max(current, new_time)
        report((current - start_time) / max(1, end_time - start_time))
    return unpad(made)


# ---- the app's notes ---------------------------------------------------------------------------
def instrument(note):
    return DRUMS if note.get("drum") else int(note.get("program", 0))


def to_tokens(notes, origin, spq, control=False):
    """Notes (quarters) to events at 10 ms steps from `origin` (seconds)."""
    out = []
    for n in notes:
        t = int(round((n["start"] * spq - origin) * TIME_RESOLUTION))
        if t < 0 or t >= MAX_TIME:
            continue
        d = max(1, min(MAX_DUR - 1, int(round(n["length"] * spq * TIME_RESOLUTION))))
        note = instrument(n) * MAX_PITCH + int(n["pitch"])
        if control:
            out.extend([ATIME_OFFSET + t, ADUR_OFFSET + d, ANOTE_OFFSET + note])
        else:
            out.extend([TIME_OFFSET + t, DUR_OFFSET + d, NOTE_OFFSET + note])
    return sort(out)


def from_tokens(tokens, origin, spq):
    out = []
    for t, d, n in triples(tokens):
        note = n - NOTE_OFFSET
        instr, pitch = note // MAX_PITCH, note % MAX_PITCH
        out.append({
            "start": ((t - TIME_OFFSET) / TIME_RESOLUTION + origin) / spq,
            "length": max(1, d - DUR_OFFSET) / TIME_RESOLUTION / spq,
            "pitch": pitch,
            "velocity": 100 if instr == DRUMS else 90,
            "program": 0 if instr == DRUMS else instr,
            "drum": instr == DRUMS,
        })
    return out


def model_dir(name):
    return os.path.join(MODELS_DIR, MODELS[name].split("/")[1])


def load_model(name):
    import torch
    from transformers import AutoModelForCausalLM

    path = model_dir(name)
    if not os.path.isdir(path):
        raise RuntimeError("the %s model is not downloaded yet. Choose AI > Install AI Music Tools in the app." % name)
    device = "cpu"
    if torch.backends.mps.is_available():
        device = "mps"
    elif torch.cuda.is_available():
        device = "cuda"
    model = AutoModelForCausalLM.from_pretrained(path).to(device)
    model.eval()
    return torch, model


def run_request(request, torch, model):
    spq = 60.0 / float(request.get("tempo", 120.0))  # seconds per quarter note
    length = float(request["quarters"]) * spq
    inputs = request.get("inputs", [])
    controls = request.get("controls", [])
    allowed = [int(i) for i in request["instruments"]]
    top_p = float(request.get("top_p", 0.95))
    context = 15.0  # seconds of music before each piece
    piece = 20.0    # seconds written at a time (the model sees at most 100 s)
    generated = []
    pieces = max(1, int(math.ceil(length / piece)))
    for k in range(pieces):
        begin = k * piece
        end = min(length, begin + piece)
        origin = begin - context
        before = [n for n in inputs + generated if origin <= n["start"] * spq < begin]
        after = [n for n in inputs if end <= n["start"] * spq < end + 2 * DELTA]
        along = [n for n in controls if begin <= n["start"] * spq < end + 2 * DELTA]
        events = sort(to_tokens(before, origin, spq) + to_tokens(after, origin, spq))
        ctrl = to_tokens(along, origin, spq, control=True)

        last = [None]

        def report(p, k=k, last=last):
            # Keep the status moving without flooding the app with messages.
            value = 0.1 + 0.9 * (k + p) / pieces
            if last[0] is None or value - last[0] >= 0.01 or p >= 1.0:
                last[0] = value
                progress(value, "Writing")

        made = generate(torch, model, int(round(context * TIME_RESOLUTION)), int(round((end - origin) * TIME_RESOLUTION)),
                        events, ctrl, top_p, allowed, report, max(1, int(round(spq * TIME_RESOLUTION / 4))))
        for n in from_tokens(made, origin, spq):
            if instrument(n) in allowed and begin <= n["start"] * spq < end:
                generated.append(n)
    return generated


def run(request_file, answer_file):
    with open(request_file) as f:
        request = json.load(f)
    if request.get("op") != "generate":
        raise RuntimeError("the Anticipatory Music Transformer helper can't do \"%s\"" % request.get("op"))
    name = request.get("model", "small")
    if name not in MODELS:
        raise RuntimeError("model is small or medium")
    progress(0.02, "Loading the Anticipatory Music Transformer (%s)" % name)
    torch, model = load_model(name)
    notes = run_request(request, torch, model)
    with open(answer_file, "w") as f:
        json.dump({"notes": notes}, f)
    progress(1.0, "Done")
    say("RESULT " + answer_file)


def prefetch(names):
    from huggingface_hub import list_repo_files, snapshot_download

    for i, name in enumerate(names):
        progress(i / len(names), "Downloading the Anticipatory Music Transformer (%s)" % name)
        files = list_repo_files(MODELS[name])
        weights = "*.safetensors" if any(f.endswith(".safetensors") for f in files) else "*.bin"
        snapshot_download(MODELS[name], local_dir=model_dir(name), allow_patterns=["*.json", weights])
    progress(1.0, "Model ready")


def main():
    args = sys.argv[1:]
    try:
        if args[:1] == ["run"] and len(args) == 3:
            run(args[1], args[2])
        elif args[:1] == ["prefetch"]:
            prefetch(args[1:] or ["small"])
        else:
            raise RuntimeError("use: arranger_generate.py run <request.json> <answer.json> | prefetch [small|medium]")
    except Exception as e:  # the app shows the text
        say("ERROR " + " ".join(str(e).split()))
        sys.exit(1)


if __name__ == "__main__":
    main()
