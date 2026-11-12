#!/usr/bin/env bash
# The core and engine layers must not depend on JUCE (see docs/build-plan.md).
# Fails if any file in them mentions JUCE.
set -euo pipefail
cd "$(dirname "$0")/.."
if grep -rniE '#\s*include\s*[<"]juce|juce::|JUCE_' src/core src/engine; then
    echo "error: src/core and src/engine must stay free of JUCE" >&2
    exit 1
fi
echo "OK: core and engine are JUCE-free"
