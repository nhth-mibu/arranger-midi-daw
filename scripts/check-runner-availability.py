#!/usr/bin/env python3
"""Check selected x64 runners before scheduling their build jobs."""

import json
import os
import sys
import urllib.error
import urllib.request


TARGETS = {
    "windows": {"self-hosted", "Windows", "X64", "ci-windows-x64"},
    "linux": {"self-hosted", "Linux", "X64", "ci-linux-x64"},
}


def evaluate(runners, platform, policy):
    if platform not in ("both", "windows", "linux") or policy not in ("fail", "skip"):
        raise ValueError("Invalid platform or offline policy")
    selected = list(TARGETS) if platform == "both" else [platform]
    ready = {target: False for target in TARGETS}
    messages = []
    for target in selected:
        matches = [r for r in runners if TARGETS[target].issubset(
            {label["name"] for label in r["labels"]})]
        ready[target] = any(r["status"] == "online" for r in matches)
        messages.append(f"{target}: {'online' if ready[target] else 'offline or unregistered'}")
    unavailable = [target for target in selected if not ready[target]]
    failed = bool(unavailable and policy == "fail") or not any(ready.values())
    if failed:
        ready = dict.fromkeys(TARGETS, False)
    return ready, messages, failed


def main():
    token = os.environ.get("RUNNER_STATUS_TOKEN", "")
    if not token:
        print("::error::Configure RUNNER_STATUS_TOKEN with Administration: read for this repository.")
        return 1
    runners = []
    try:
        page = 1
        while True:
            url = (f"{os.environ['GITHUB_API_URL']}/repos/"
                   f"{os.environ['GITHUB_REPOSITORY']}/actions/runners?per_page=100&page={page}")
            request = urllib.request.Request(url, headers={
                "Authorization": f"Bearer {token}",
                "Accept": "application/vnd.github+json",
                "User-Agent": "arranger-runner-preflight",
            })
            with urllib.request.urlopen(request, timeout=30) as response:
                batch = json.load(response)["runners"]
            runners.extend(batch)
            if len(batch) < 100:
                break
            page += 1
    except urllib.error.HTTPError as error:
        print(f"::error::Runner status API returned HTTP {error.code}; check token permissions and expiration.")
        return 1
    except (urllib.error.URLError, TimeoutError):
        print("::error::Cannot reach runner status API; no builds will be scheduled.")
        return 1
    ready, messages, failed = evaluate(
        runners, os.environ["BUILD_PLATFORM"], os.environ["OFFLINE_POLICY"])
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        for target, enabled in ready.items():
            output.write(f"{target}={str(enabled).lower()}\n")
    summary = ["### Runner availability", *messages]
    summary.append("Start Windows and the Ubuntu runner service, then rerun this workflow if a target is offline.")
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as output:
        output.write("\n\n".join(summary) + "\n")
    for message in messages:
        print(message)
        if "offline" in message:
            print(f"::warning::{message}")
    if failed:
        print("::error::Required runners are unavailable; no build jobs were scheduled.")
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
