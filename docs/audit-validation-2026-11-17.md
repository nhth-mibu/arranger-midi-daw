# Audit fixes, video audio import, and local validation

This change fixes the six source-audit findings at upstream commit `35ef7d95eaf75529058b6bbc38fd4bd088a23d70` and adds video audio import. Validation ran on Windows x64 on 17 November 2026.

## Changes

- Keep the old playback snapshot and instrument protected until transition reads finish. Clear retained raw pointers when the audio device stops.
- Reject pending AI edits when the document revision changes, including when another project opens. This deliberately rejects results after any song edit; run the tool again to use the changed song.
- Give queued message-thread work owned request and result storage. Skip work abandoned before execution and return exceptions to its waiting worker.
- Guard runner and installer completion callbacks with lifetime tokens.
- Preserve audible loop repetitions when joining clips. Preserve controller repetitions when splitting loops.
- Read a video's first audio stream through a separate FFmpeg process. File import, drag-and-drop, audio loops and sample readers share this path. Surface decoding errors in the import UI.

FFmpeg receives separate arguments rather than a shell command. The decoder accepts local video containers, disables network protocols, limits decoded audio to 45 minutes, and stops after two minutes. Temporary WAV output is removed after reading. The clip keeps its original video source path. FFmpeg remains an optional external program; CMake does not download or link it.

## Build and tests

The local Release build used Visual Studio 2022, MSVC 19.44, Windows SDK 10.0.26100.0, and CMake 3.31.6. The build enabled the desktop app, Whisper, all unit tests and the two test VST3 plugins. Warnings are errors for targets configured with the repository's warning helper.

```powershell
cmake -S . -B ../build -G "Visual Studio 17 2022" -A x64 `
  -DARRANGER_BUILD_APP=ON -DARRANGER_BUILD_TESTS=ON `
  -DARRANGER_BUILD_TEST_PLUGINS=ON -DARRANGER_WARNINGS_AS_ERRORS=ON
cmake --build ../build --config Release --parallel 6
ctest --test-dir ../build -C Release --output-on-failure --no-tests=error --timeout 120
```

FFmpeg 9.0.2 was on PATH for the final test run. All **246 CTest cases passed**, with no failures or skips. New tests cover the handoff transition, device restart after snapshot collection, looped clip edits, stale AI results, cancelled message-thread work, exception delivery, and video import. The media tests use small synthetic MP4, MKV and WebM files with a 440 Hz first audio stream and an 880 Hz second stream. They also cover silent video, corrupt input, missing FFmpeg, and Unicode paths with spaces.

The built app imported the MP4 fixture into an audio track in screenshot mode. It also reopened a project referring to that video and rendered 0.5 seconds to a WAV with a peak of approximately 0.111. The app did not open an audio device for these checks.

The handoff regression controls the reclamation interleaving. The queued-task tests exercise the JUCE message queue. The runner and installer lifetime-token guards were verified by source inspection and compilation; these tests do not reproduce every full-app shutdown race.

macOS and Linux binaries, physical MIDI devices, external user plugins, and downloaded AI models were not exercised locally.

## Dependency scan

The scan is **not clean**. The source fixes do not upgrade the existing AI dependency stacks or replace JUCE's embedded libraries. Raw results, resolved npm and Python dependency snapshots, and build/test logs are included in the local delivery folder.

| Scope | Method | Result |
| --- | --- | --- |
| Magenta helper | `npm install --package-lock-only --ignore-scripts`, then `npm audit --json --package-lock-only` | 9 affected packages: 3 critical, 6 high |
| Optional Python analysis and music tools | Resolve all installer requirements for Python 3.10 on Windows with `uv pip compile`; audit all 92 pinned packages with `pip-audit --no-deps --disable-pip` | 62 raw advisory entries across filelock 4.1.0, Pillow 12.3.0 and setuptools 80.10.2; one setuptools advisory appears twice |
| Native CMake dependencies | OSV commit queries for the five Git checkouts; OSS-Fuzz version queries for all six direct dependencies | No returned matches; the JSON release archive has no commit metadata |
| Selected JUCE embedded libraries and Node fallback | NVD CPE version queries | libpng 1.6.37: 10 candidates; zlib 1.3.1: 2; HarfBuzz 10.1.0: 1; Node 22.11.0: 18; libjpeg 9f and FLAC 1.4.3: 0 |
| Local FFmpeg 9.0.2 decoder | NVD CPE version query | 7 candidates; these require reachability and backport review |

Native version matches are candidates, not proof that every listed issue is reachable in this app. Conversely, an empty OSV result does not establish that a C++ dependency is free from vulnerabilities. The scan does not recursively identify every embedded codec, model file or dynamically downloaded speech binary.

### Remediation priorities

1. Review JUCE's embedded libpng and HarfBuzz against upstream fixes. The version strings alone do not establish whether a patch was backported. The zlib `untgz` finding, CVE-2026-22184, concerns a demonstration utility that is not built into this app; retain it as an excluded component finding. Review CVE-2026-27171 separately.
2. Update or replace Magenta's old transitive dependency chain. npm reports no automatic compatible fix. `static-eval`, `minimist` and `protobufjs` have direct advisories; other entries include propagated dependency findings. Do not use an untested forced major-version override as a security fix.
3. Modernize the Python 3.10 tool environment and remove or replace the legacy `setuptools<81` constraint after compatibility tests. The resolved environment currently contains known advisory matches. Resolution and scanning did not install or execute the heavy model packages.
4. Update the pinned Node fallback. Many reported Node advisories concern server or permission-model features; this scan did not demonstrate their use by the Magenta helper.
5. Review FFmpeg CVE-2026-107678 for crafted MOV `pssh` records and CVE-2026-107676 for Matroska metadata. The import path excludes the SFTP, DASH and RTSP protocols/demuxers named in other findings. The two older git-master memory-leak candidates need range verification. A separate decoder process and timeout limit impact but do not make a vulnerable decoder safe for arbitrary hostile files.

Advisory sources: [npm audit](https://docs.npmjs.com/cli/commands/npm-audit), [pip-audit](https://pypi.org/project/pip-audit/), [OSV](https://osv.dev/), and [NVD](https://nvd.nist.gov/). FFmpeg was obtained from the [Gyan build linked by FFmpeg](https://ffmpeg.org/download.html), and its archive SHA-256 was checked against the publisher's checksum: `4705843ccaaf54257c16ad90f3e952ece33c17df964ecf7bfdbb0f49c7171077`.

## GitHub Actions

The repository's only push-triggered CI workflow selects `main` and `claude/**`. Other build workflows require manual dispatch; no workflow currently selects pull requests. The PR branch uses `fix/` and its commit contains `[skip ci]`. No workflow dispatch or release publication is part of this change.
