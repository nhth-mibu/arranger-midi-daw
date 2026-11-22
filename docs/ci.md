# CI and Actions cost

CI runs on GitHub-hosted runners (there are no self-hosted runners since
2026-11-21). The workflows are set up to use as few Actions minutes as possible.

## What runs when

| Workflow | When | Runner |
| --- | --- | --- |
| `ci.yml` (CI) | Every pull request and every merge to main. Not for changes to `docs/` or `.md` files only. | Linux (Ubuntu 26.04 container) |
| `ci.yml` with **macos** ticked | By hand only | macOS |
| `x64-builds.yml` | By hand. Run it on the release branch before you raise the VERSION. | Windows and Linux |
| `auto-release.yml` + `build-release.yml` | When the VERSION in `CMakeLists.txt` changes on main | macOS, Windows, Linux |
| `sanitizers.yml` | Mondays and by hand | Linux |
| `fuzz-nightly.yml` | By hand only | Linux |
| `purge-green-runs.yml`, `delete-merged-branches.yml` | Once a day | Linux (about 1 minute) |

Other cost controls:

- A new push to a pull request cancels the CI run that is still going.
- The Linux job keeps a compiler cache (ccache). Only main saves it; pull
  requests read it. A warm build takes a fraction of a cold build.
- Workflow artifacts are kept for 3 days.
- Pull requests do not upload artifacts.

## Cost

GitHub bills by the minute. A macOS minute costs about 10 times a Linux
minute, a Windows minute about 2 times. Linux minutes also come out of the
monthly free minutes first. Rough cost per run:

| Run | Time | Cost |
| --- | --- | --- |
| CI on a pull request (Linux, warm cache) | 10 to 20 min | a few cents |
| CI with a cold cache | 40 to 60 min | about $0.30 |
| `x64-builds.yml` (Windows and Linux) | about 1 to 1.5 h | about $1 |
| A release (all three systems) | about 2 to 3 h total | about $4 |
| CI with macOS | 30 to 45 min of macOS | about $2 to $3 |

Look at the real numbers in GitHub: Settings > Billing and licensing > Usage.

## Rules

- Keep `src/core` and `src/engine` free of JUCE (`scripts/check-no-juce.sh`).
- Keep the FluidSynth MSVC patch in `src/soundfont/CMakeLists.txt` unless you replace it on purpose.
- Before a new workflow job, think about cost: Linux first; macOS and Windows only by hand or for releases.

## Releasing a new version

Releases need no manual tag. To release, raise `VERSION` in `CMakeLists.txt`,
add `docs/release-notes-vX.Y.Z.md`, and merge to `main`.
For a pre-release, also set `ARRANGER_VERSION_SUFFIX` in `CMakeLists.txt`
(for example `"beta.1"`). The tag is then `vX.Y.Z-beta.1`, the notes file is
`docs/release-notes-vX.Y.Z-beta.1.md`, and both releases are marked as
pre-releases. Set it back to `""` for the final release.
[`auto-release.yml`](../.github/workflows/auto-release.yml) then:

1. creates the `vX.Y.Z` tag on the merge commit (workflow token),
2. runs `build-release.yml` to build and verify the three packages,
3. publishes the GitHub release with the release notes and the packages,
4. runs `publish-public-release.yml` to copy it to the public repository.

Before you raise the VERSION, run `x64-builds.yml` on the release branch and
wait for green: the Windows tests must pass first. To retry a failed run, open
`Actions > Release > Run workflow` on `main`. It reuses the tag if the tag is on
the same commit, and it does nothing if the release exists. It never moves a tag
or replaces a release.

A release made with the workflow token does not start other workflows. That is
why the workflow calls `publish-public-release.yml` itself.

## Versioned release packages

Use `build-release.yml` from `main` with the version tag and its exact commit
SHA. For releases after v0.0.5, the macOS download is **DMG only**, with a
matching SHA-256 checksum; do not publish a macOS ZIP. Windows remains a ZIP
and Linux remains a tar.gz. The Mac job verifies the Universal app and mounts
the DMG read-only to check its version, signatures, architectures, and
Applications shortcut. The final job verifies all three packages together.

Existing release downloads are retained. The older CI artifacts and promotion
workflow described below are legacy paths and do not define the current
versioned release package selection.

## Downloading x64 development builds

After a successful manual `x64-builds.yml` run, open its Actions run page and
download the platform artifact. Artifacts are retained for three days and
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

## Manual-build availability gate

The x64 workflow checks runner availability on the always-on Mac before
scheduling selected Windows/Linux jobs. It requires the repository Actions
secret `RUNNER_STATUS_TOKEN`: a fine-grained token restricted to this repository
with **Administration: read** (and GitHub's automatic Metadata: read permission).
Do not reuse a broad developer token. Set an expiration and renew the secret
before it expires. No token needs to be stored in the runner account or checkout.

The `offline_policy` input defaults to `fail`: any unavailable selected target
fails preflight and prevents all build jobs from being scheduled. `skip` builds
only available selected targets and records omissions in the run summary; if
none are available, the run ends green with a warning and builds nothing. Missing/expired credentials and API errors
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

## Artifact storage

The account has a small Actions storage quota. Keep it low:

- Upload artifacts with `retention-days: 3` (fuzz findings: 7). Release builds use their artifacts in the same run. The published GitHub Release keeps the files.
- If storage is full, delete old artifacts: `gh api -X DELETE repos/nhth-mibu/arranger-midi-daw/actions/artifacts/<id>`. Do not delete releases or release assets.
