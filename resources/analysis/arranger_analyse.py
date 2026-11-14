#!/usr/bin/env python3
"""Audio analysis helper for Arranger MIDI DAW.

The app runs this in its own Python environment (Options > Audio Analysis
Tools). It splits a recording into stems (Demucs), finds the notes of each
stem (Basic Pitch) and names the instruments (PANNs). The app reads the
JSON it writes; chords and lyrics are found in the app.

    arranger_analyse.py check
    arranger_analyse.py prefetch
    arranger_analyse.py run --input song.wav --output folder [--stems] [--notes] [--instruments]
                            [--model htdemucs] [--device auto]

Progress goes to stdout as lines "PROGRESS <0..1> <text>". The last line is
"RESULT <path of analysis.json>" or "ERROR <text>".

All tools are open source: Demucs (MIT), Basic Pitch (Apache 2.0),
PANNs (MIT; the model weights are CC BY 4.0).
"""

import argparse
import importlib.util
import json
import os
import sys
import urllib.request

PANNS_FOLDER = os.path.join(os.path.expanduser("~"), "panns_data")
PANNS_LABELS = "http://storage.googleapis.com/us_audioset/youtube_corpus/v1/csv/class_labels_indices.csv"
PANNS_MODEL = "https://zenodo.org/record/3987831/files/Cnn14_mAP%3D0.431.pth?download=1"

# Stems that get notes. Drums: the app finds the hits itself.
NOTE_RANGES = {  # Hz: lowest, highest
    "vocals": (70.0, 1400.0),
    "bass": (30.0, 400.0),
    "guitar": (70.0, 1500.0),
    "piano": (27.0, 4200.0),
    "other": (40.0, 4200.0),
    "mix": (30.0, 4200.0),
}


def say(kind, *words):
    print(kind, *words, flush=True)


def progress(value, text):
    say("PROGRESS", f"{max(0.0, min(1.0, value)):.3f}", text)


def has(module):
    return importlib.util.find_spec(module) is not None


def device_name(wanted):
    import torch

    if wanted != "auto":
        return wanted
    if torch.cuda.is_available():
        return "cuda"
    if getattr(torch.backends, "mps", None) is not None and torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def check():
    info = {
        "python": sys.version.split()[0],
        "demucs": has("demucs"),
        "basic_pitch": has("basic_pitch"),
        "panns": has("panns_inference"),
        "soundfile": has("soundfile"),
    }
    if has("torch"):
        try:
            info["device"] = device_name("auto")
        except Exception as e:  # noqa: BLE001
            info["device"] = f"unknown ({e})"
    print(json.dumps(info), flush=True)
    return 0


def download(url, target, text):
    if os.path.exists(target) and os.path.getsize(target) > 0:
        return
    os.makedirs(os.path.dirname(target), exist_ok=True)
    progress(0.0, text)
    part = target + ".part"
    with urllib.request.urlopen(url, timeout=60) as r, open(part, "wb") as f:
        while True:
            block = r.read(1 << 20)
            if not block:
                break
            f.write(block)
    os.replace(part, target)


# ---- stems --------------------------------------------------------------------------


def separate(path, folder, model_name, device, report):
    import soundfile as sf
    import torch
    from demucs.apply import apply_model
    from demucs.audio import convert_audio
    from demucs.pretrained import get_model

    report(0.0, "Loading the stem model (the first time, it downloads)")
    model = get_model(model_name)
    model.eval()
    if device == "mps":
        os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")

    data, rate = sf.read(path, dtype="float32", always_2d=True)
    wav = torch.from_numpy(data.T.copy())
    wav = convert_audio(wav, rate, model.samplerate, model.audio_channels)
    ref = wav.mean(0)
    scale = float(ref.std()) or 1.0
    wav = (wav - float(ref.mean())) / scale

    report(0.1, "Splitting into stems")
    with torch.no_grad():
        try:
            sources = apply_model(model, wav[None], device=device, shifts=1, split=True, overlap=0.25, progress=False)[0]
        except Exception:  # noqa: BLE001  (some layers don't run on every GPU)
            if device == "cpu":
                raise
            report(0.1, "Splitting into stems (on the CPU)")
            sources = apply_model(model, wav[None], device="cpu", shifts=1, split=True, overlap=0.25, progress=False)[0]
    sources = sources * scale + float(ref.mean())

    stems = []
    for name, audio in zip(model.sources, sources):
        target = os.path.join(folder, f"{name}.wav")
        sf.write(target, audio.cpu().numpy().T, model.samplerate, subtype="PCM_24")
        stems.append({"name": name, "file": target})
    report(1.0, "Stems ready")
    return stems


# ---- notes --------------------------------------------------------------------------


def one_at_a_time(notes, keep_low):
    """A voice or a bass plays one note at a time: where notes overlap, keep the loudest (bass: the lowest)."""
    notes = sorted(notes, key=lambda n: (n["start"], n["pitch"]))
    out = []
    for n in notes:
        if out and n["start"] < out[-1]["end"] - 0.03:
            last = out[-1]
            better = n["pitch"] < last["pitch"] if keep_low else n["velocity"] > last["velocity"]
            if better:
                last["end"] = n["start"]
                if last["end"] - last["start"] < 0.05:
                    out.pop()
                out.append(n)
            continue
        out.append(n)
    return out


def note_model():
    """The ONNX model where onnxruntime is there: it runs the same on every system."""
    from basic_pitch import FilenameSuffix, ICASSP_2022_MODEL_PATH, build_icassp_2022_model_path

    onnx = build_icassp_2022_model_path(FilenameSuffix.onnx)
    if has("onnxruntime") and onnx.exists():
        return onnx
    return ICASSP_2022_MODEL_PATH


def find_notes(path, stem_name):
    from basic_pitch.inference import predict

    low, high = NOTE_RANGES.get(stem_name, NOTE_RANGES["mix"])
    _, _, events = predict(
        path,
        model_or_model_path=note_model(),
        onset_threshold=0.5,
        frame_threshold=0.3,
        minimum_note_length=80.0,
        minimum_frequency=low,
        maximum_frequency=high,
        melodia_trick=True,
    )
    notes = [
        {"start": round(float(e[0]), 4), "end": round(float(e[1]), 4), "pitch": int(e[2]),
         "velocity": int(max(1, min(127, round(float(e[3]) * 127))))}
        for e in events
    ]
    if stem_name in ("vocals", "bass"):
        notes = one_at_a_time(notes, keep_low=stem_name == "bass")
    return notes


# ---- instruments --------------------------------------------------------------------


class Tagger:
    def __init__(self, device):
        # PANNs downloads with wget, which a Mac doesn't have: fetch its files first.
        download(PANNS_LABELS, os.path.join(PANNS_FOLDER, "class_labels_indices.csv"), "Downloading the instrument labels")
        checkpoint = os.path.join(PANNS_FOLDER, "Cnn14_mAP=0.431.pth")
        download(PANNS_MODEL, checkpoint, "Downloading the instrument model (about 330 MB, once)")
        from panns_inference import AudioTagging, labels

        self.labels = labels
        self.model = AudioTagging(checkpoint_path=checkpoint, device="cuda" if device == "cuda" else "cpu")

    def tags(self, path, count=8):
        import librosa
        import numpy as np

        audio, _ = librosa.load(path, sr=32000, mono=True)
        if audio.size == 0 or float(np.max(np.abs(audio))) < 1e-4:
            return []
        scores = []
        # Ten-second pieces, averaged: long files at once take much memory.
        piece = 32000 * 10
        for start in range(0, max(1, audio.size), piece):
            chunk = audio[start:start + piece]
            if chunk.size < 32000:
                continue
            out, _ = self.model.inference(chunk[None, :])
            scores.append(out[0])
        if not scores:
            out, _ = self.model.inference(audio[None, :])
            scores.append(out[0])
        mean = np.mean(np.stack(scores), axis=0)
        best = np.argsort(mean)[::-1][:count]
        return [{"label": self.labels[i], "score": round(float(mean[i]), 4)} for i in best]


# ---- run ----------------------------------------------------------------------------


def run(args):
    os.makedirs(args.output, exist_ok=True)
    warnings = []
    result = {"source": args.input, "stems": [], "warnings": warnings}

    want_stems = args.stems and has("demucs")
    if args.stems and not want_stems:
        warnings.append("Demucs is not installed: no stems.")
    want_notes = args.notes and has("basic_pitch")
    if args.notes and not want_notes:
        warnings.append("Basic Pitch is not installed: no notes.")
    want_tags = args.instruments and has("panns_inference")
    if args.instruments and not want_tags:
        warnings.append("PANNs is not installed: no instrument names.")

    # How much of the bar each step takes.
    weights = {"stems": 0.55 if want_stems else 0.0, "notes": 0.3 if want_notes else 0.0, "tags": 0.15 if want_tags else 0.0}
    total = sum(weights.values()) or 1.0
    done = 0.0

    def step(name):
        def report(value, text):
            progress((done + weights[name] * value) / total, text)
        return report

    device = "cpu"
    if want_stems or want_tags:
        device = device_name(args.device)

    if want_stems:
        result["stems"] = separate(args.input, args.output, args.model, device, step("stems"))
        done += weights["stems"]
    else:
        result["stems"] = [{"name": "mix", "file": ""}]

    def path_of(stem):
        return stem["file"] or args.input

    if want_notes:
        report = step("notes")
        targets = [s for s in result["stems"] if s["name"] in NOTE_RANGES]
        for i, stem in enumerate(targets):
            report(i / max(1, len(targets)), f"Finding the notes: {stem['name']}")
            try:
                stem["notes"] = find_notes(path_of(stem), stem["name"])
            except Exception as e:  # noqa: BLE001
                warnings.append(f"No notes for {stem['name']}: {e}")
        done += weights["notes"]

    if want_tags:
        report = step("tags")
        report(0.0, "Loading the instrument model")
        try:
            tagger = Tagger(device)
            for i, stem in enumerate(result["stems"]):
                report(i / max(1, len(result["stems"])), f"Naming the instruments: {stem['name']}")
                stem["tags"] = tagger.tags(path_of(stem))
        except Exception as e:  # noqa: BLE001
            warnings.append(f"No instrument names: {e}")
        done += weights["tags"]

    target = os.path.join(args.output, "analysis.json")
    with open(target, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1)
    progress(1.0, "Done")
    say("RESULT", target)
    return 0


def prefetch(args):
    """Downloads the models now, so that the first analysis doesn't wait for them."""
    if has("demucs"):
        from demucs.pretrained import get_model

        progress(0.1, "Downloading the stem model (about 80 MB)")
        get_model(args.model)
    if has("panns_inference"):
        progress(0.6, "Downloading the instrument model (about 330 MB)")
        download(PANNS_LABELS, os.path.join(PANNS_FOLDER, "class_labels_indices.csv"), "Downloading the instrument labels")
        download(PANNS_MODEL, os.path.join(PANNS_FOLDER, "Cnn14_mAP=0.431.pth"), "Downloading the instrument model")
    progress(1.0, "Models ready")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("check")
    pre = sub.add_parser("prefetch")
    pre.add_argument("--model", default="htdemucs")
    r = sub.add_parser("run")
    r.add_argument("--input", required=True)
    r.add_argument("--output", required=True)
    r.add_argument("--stems", action="store_true")
    r.add_argument("--notes", action="store_true")
    r.add_argument("--instruments", action="store_true")
    r.add_argument("--model", default="htdemucs")
    r.add_argument("--device", default="auto")
    args = parser.parse_args()
    try:
        if args.command == "check":
            return check()
        if args.command == "prefetch":
            return prefetch(args)
        return run(args)
    except Exception as e:  # noqa: BLE001
        say("ERROR", str(e).replace("\n", " ") or type(e).__name__)
        return 1


if __name__ == "__main__":
    sys.exit(main())
