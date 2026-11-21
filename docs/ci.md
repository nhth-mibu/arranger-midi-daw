# CI and Actions cost

CI runs on GitHub-hosted runners. The workflows are set up to use as few
Actions minutes as possible. The self-hosted runners stay available as a
fallback (see [ci-self-hosted.md](ci-self-hosted.md)).

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

## Go back to the self-hosted runners

In the repository, open Settings > Secrets and variables > Actions >
Variables. Add the variable `CI_RUNNERS` with the value `self-hosted`.
Then every workflow uses the self-hosted runners again, as before: the Mac
mini runs CI, sanitizers and fuzzing at every push and every night, and
Windows and Linux wait for the PC. Delete the variable to use the
GitHub-hosted runners again.
