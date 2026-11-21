#!/usr/bin/env bash
# Installs the build tools and JUCE system packages on Ubuntu.
# CI runs it inside an ubuntu:26.04 container (as root), so the
# packages match the Ubuntu 26.04 release package. Locally: sudo scripts/ci-linux-deps.sh
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends \
  build-essential cmake ninja-build ccache git ca-certificates pkg-config file python3 \
  xvfb xauth fonts-dejavu-core \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libgl1-mesa-dev
