#!/usr/bin/env bash
# Sets up the audio analysis tools without the app (the app's
# Options > Audio Analysis Tools does the same). CI uses it with its own HOME.
#
#   scripts/install-analysis-tools.sh [whisper-model]
#
# whisper-model: large-v3-turbo-q5_0 (default), small, base or medium-q5_0.
set -euo pipefail
model="${1:-large-v3-turbo-q5_0}"
here="$(cd "$(dirname "$0")/.." && pwd)"

case "$(uname -s)" in
  Darwin) data="$HOME/Library/Application Support/Arranger MIDI DAW" ;;
  *) data="${XDG_CONFIG_HOME:-$HOME/.config}/Arranger MIDI DAW" ;;
esac
venv="$data/Python"
export PATH="$HOME/.local/bin:/opt/homebrew/bin:/usr/local/bin:$PATH"

if ! command -v uv >/dev/null 2>&1; then
  echo "Getting uv"
  curl -LsSf https://astral.sh/uv/install.sh | sh
fi
uv venv --allow-existing --python 3.10 "$venv"
# Keep this list the same as kPackages in src/app/AudioAnalysis.cpp.
uv pip install --python "$venv/bin/python" demucs 'basic-pitch[onnx]' panns-inference soundfile 'setuptools<81'
"$venv/bin/python" -I "$here/resources/analysis/arranger_analyse.py" prefetch

target="$data/Analysis/whisper/ggml-$model.bin"
if [ ! -s "$target" ]; then
  mkdir -p "$(dirname "$target")"
  echo "Downloading the Whisper model $model"
  curl --fail --location --retry 2 --silent --show-error -o "$target.part" \
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-$model.bin"
  mv "$target.part" "$target"
fi
printf '%s' "$model" > "$data/Analysis/whisper/model.txt"
"$venv/bin/python" -I "$here/resources/analysis/arranger_analyse.py" check
