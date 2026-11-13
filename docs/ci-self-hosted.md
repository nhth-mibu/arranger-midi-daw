# Self-hosted CI: hand-off notes

Status on 2026-11-13. Codex set up the self-hosted GitHub Actions runners with
Nhat. This page is the hand-off from that work. From here the CI is maintained
together with the app.

## Rules for development

- Keep development workspaces separate from all CI workspaces.
- Do not change runner accounts, services, credentials or machine security settings.
- Do not weaken permissions on the runner, and do not read runner credentials.
- Do not log Claude Code, Codex, GitHub CLI or iCloud into the `ghrunner` account.
- Do not assume that the Windows or WSL builds work because their runners are registered.
- Keep `src/core` and `src/engine` free of JUCE (`scripts/check-no-juce.sh`).
- Keep the FluidSynth MSVC patch in `src/soundfont/CMakeLists.txt` unless you replace it on purpose.
- `[skip ci]` was used for the CI setup commits to keep the old hosted matrix quiet. It is not a rule for ordinary development commits.

## Project facts (verified by Codex)

- CMake 3.24 minimum, C++20.
- FetchContent dependencies: JUCE 8.0.15, FluidSynth 2.5.7, Catch2 3.7.1, nlohmann/json 3.11.3, yaml-cpp 0.9.0. No submodules, no CMake presets.
- Options: `ARRANGER_BUILD_APP`, `ARRANGER_BUILD_TESTS`, `ARRANGER_WARNINGS_AS_ERRORS`, `ARRANGER_JUCE_DIR` (a local JUCE checkout).
- App target `ArrangerMidiDaw`. Test target `arranger_tests` (82 tests passed).
- macOS app output: `<build-dir>/src/app/ArrangerMidiDaw_artefacts/Release/Arranger MIDI DAW.app`

## Machines and runners

Exactly three targets. There is no Linux ARM64 target.

| Runner | Machine | Labels | State at last check |
| --- | --- | --- | --- |
| `macmini-m2pro` | Mac mini M2 Pro, macOS ARM64, always on | `self-hosted, macOS, ARM64, ci-macos-arm64` | Online, verified |
| `win-9950x` | Ryzen 9 9950X, Windows 11 x64, on demand | `self-hosted, Windows, X64, ci-windows-x64` | Online, not verified |
| `wsl-9950x` | WSL2 Ubuntu on that PC, Linux x64, on demand | `self-hosted, Linux, X64, ci-linux-x64` | Offline, not verified |

For Windows and WSL the toolchains, isolation, startup behaviour and builds
are not verified yet.

## Mac runner

- Development user: `far-mini`. Dedicated standard (non-admin) CI user: `ghrunner`.
- Runner 2.338.2 in `/Users/ghrunner/actions-runner`; checkouts under its `_work` folder.
- Persistent build folder: `/Users/ghrunner/ci-builds/arranger-midi-daw/macos-arm64-release`
- `ghrunner` home and the runner folder have mode 700. `.runner`, `.credentials` and `.credentials_rsaparams` have mode 600.
- The runner uses the official per-user LaunchAgent `actions.runner.nhth-mibu-arranger-midi-daw.macmini-m2pro`.
- `ghrunner` must stay logged in. Fast User Switching back to `far-mini` is fine.
- After a reboot, unlock FileVault and log `ghrunner` in.
- Sleep is off, restart after power failure is on, FileVault and the firewall are on.
- Tools for `ghrunner`: Apple Clang 21 (Command Line Tools, no full Xcode), Git, CMake 4.4.5, Ninja 1.13.2, ccache 4.14.2, Homebrew in `/opt/homebrew/bin`.

## Workflows

| File | Trigger | What it does |
| --- | --- | --- |
| `ci.yml` | Push to `main` and `claude/**`, manual | Builds the app and tests on the Mac runner, runs the tests, renders screenshots and uploads the app (kept 7 days). It uses no hosted minutes. |
| `macos-runner-smoke.yml` | Manual | Checks the `ghrunner` identity, ARM64, the isolated workspace, checkout, tools and ccache. |
| `macos-build.yml` | Manual | Release build of the app and tests on the Mac runner with Ninja and ccache, warnings as errors, then CTest and a no-op rebuild check. No artifacts yet. |
| `runner-tool-inventory.yml` | Manual | Lists the build tools on the Windows and Linux runners. |
| `windows-cache-setup.yml` | Manual | Sets up sccache on the Windows runner. |
| `x64-builds.yml` | Manual | Release build of the app and tests on the Windows and Linux runners. Start the PC first. |

`macos-build.yml` has read-only permissions, does not keep checkout
credentials, and pins `actions/checkout` to v6 commit
`d23441a48e516b6c34aea4fa41551a30e30af803`.

First successful runs: smoke run 43388575294, full Mac build run 43388941658
(2 min 8 s, all 82 tests passed, ccache took 279 of 279 calls, all misses on
the first build). Cache hits after a recompile are not shown yet.

Known warnings, not blocking: a nested comment in `src/app/ui/Theme.h`, JUCE
deprecations against the macOS 27 SDK, duplicate static-library linker
warnings. The app target does not use the same warning helper as the core
targets, so do not call the full app warning-free.

## Still to do

### macOS DMG for private testing

Current `ci.yml` runs `scripts/package-macos.sh` after tests and screenshots.
It copies the app, removes extended attributes from that copy, applies a complete
ad-hoc bundle signature, and verifies it. It produces both the existing ZIP and
`ArrangerMidiDaw-macos-arm64.dmg`, with SHA-256 checksum files, in the
`ArrangerMidiDaw-macOS` artifact. The DMG includes the app, an Applications
shortcut, and private-test installation notes. Packaging verifies the image,
mounts it read-only, and checks the mounted app's signature and shortcut.

This requires no Apple Developer membership, certificate, or signing secret.
Ad-hoc signatures provide integrity checks but no verified developer identity.
The app and DMG are not notarized, and Gatekeeper may block downloaded copies.
For a build you trust, review Privacy & Security's Open Anyway option after a
blocked launch. Do not describe these packages as Gatekeeper-approved.

Release promotion includes a DMG/checksum when the selected Mac build provides
them. Older build artifacts remain supported as ZIP-only packages. Existing
tags, draft releases, and release assets are not modified by this change.

### Preparing a draft release

`release.yml` promotes existing build artifacts; it does not rebuild them.
Run it from `main` with an existing `vMAJOR.MINOR.PATCH` tag and the numeric IDs
of successful `ci.yml` and `x64-builds.yml` runs. Both runs must match the tag's
exact commit, which must be on main's history. The tag version must match the
CMake project VERSION. The x64 run must include both platform packages.

Keep `create_draft` false for the first run: this validates provenance, downloads
and checks archives/checksums, and saves the combined `release-assets` artifact
for seven days. Set `create_draft` true to perform the same checks and then
create a GitHub draft release with all three archives and checksum files.
Existing releases are never overwritten and publication is always manual.

The Mac artifact comes from the existing CI packaging step. Its checksum is
generated during promotion; Windows and Linux checksums are verified against
their build artifacts. The workflow rechecks the tag before creating the draft.
It uses the ephemeral workflow token: no new PAT or signing secret is needed.
Release-write permission is restricted to the final draft-creation job, which
does not check out or build application code.

These are development packages, without Windows signing or macOS Developer ID
signing/notarization. Signing, notarization, and public-distribution licensing
remain separate work. No automatic tag creation, tag-push release trigger, or
public publishing is configured.

### Downloading x64 development builds

After a successful manual `x64-builds.yml` run, open its Actions run page and
download the platform artifact. Artifacts are retained for seven days and
contain an application archive and its SHA-256 checksum file:

- Windows: `ArrangerMidiDaw-windows-x64.zip`. Extract the whole archive before
  launching the executable. The Microsoft Visual C++ v14 x64 Redistributable
  is a prerequisite; see the link in `BUILD-INFO.txt`. Runtime DLL imports are
  recorded in `windows-dependencies.txt`.
- Linux: `ArrangerMidiDaw-linux-x64-ubuntu-26.04.tar.gz`. Extract with
  `tar -xzf` to preserve executable permissions. This is an Ubuntu 26.04 x64
  build, not a portable Linux installer. System library dependencies are
  recorded in `linux-dependencies.txt`. The extracted app is tested by rendering
  a screenshot under Xvfb during packaging.

Packages include demo files, commit/run metadata and dependency license texts.
They do not bundle SoundFonts. These development artifacts are not signed
releases; public distribution and licensing still need a separate review.

### Manual-build availability gate

The x64 workflow checks runner availability on the always-on Mac before
scheduling selected Windows/Linux jobs. It requires the repository Actions
secret `RUNNER_STATUS_TOKEN`: a fine-grained token restricted to this repository
with **Administration: read** (and GitHub's automatic Metadata: read permission).
Do not reuse a broad developer token. Set an expiration and renew the secret
before it expires. No token needs to be stored in the runner account or checkout.

The `offline_policy` input defaults to `fail`: any unavailable selected target
fails preflight and prevents all build jobs from being scheduled. `skip` builds
only available selected targets and records omissions in the run summary; if
none are available, preflight fails. Missing/expired credentials and API errors
also prevent scheduling. Busy online runners may queue normally.

This is a status snapshot, not a reservation: keep the PC and WSL running until
the build finishes. A runner that disconnects after preflight can still leave a
job queued; cancel that run and restart the runner before retrying. The Mac must
also be online to execute preflight. No GitHub-hosted fallback is configured.

Start Windows, start Ubuntu, and confirm its runner service is active before
dispatching `x64-builds.yml`. The systemd service starts when Ubuntu boots; it
does not itself start the WSL distribution after Windows boots.

- Windows and WSL toolchain, security and startup preflights.
- Windows x64 and Linux x64 build validation.
- Manual cross-platform builds that handle an offline runner on purpose.
- Artifact packaging for all platforms.
- Release workflows, signing and notarisation decisions, release permissions.
- Cross-platform cache and incremental-build validation.
- Operations notes for startup, reboot, updates and recovery.
