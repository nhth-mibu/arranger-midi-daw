#!/usr/bin/env python3
"""Validate a version tag and the successful build runs to promote."""

import base64
import json
import os
import re
import sys
import urllib.error
import urllib.request


def validate_version(tag, cmake):
    if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
        raise ValueError("Use a version tag such as v0.0.1")
    project = re.search(r"project\(ArrangerMidiDaw\s+VERSION\s+(\d+\.\d+\.\d+)\b", cmake)
    if not project or project[1] != tag[1:]:
        raise ValueError("Tag version does not match CMake project VERSION")


def validate_run(run, sha, repository, workflow):
    if run.get("status") != "completed" or run.get("conclusion") != "success":
        raise ValueError(f"{workflow}: build run must have completed successfully")
    if run.get("head_sha") != sha:
        raise ValueError(f"{workflow}: build commit differs from the tag")
    if run.get("head_repository", {}).get("full_name") != repository:
        raise ValueError("Build run must belong to this repository")
    if run.get("path") != f".github/workflows/{workflow}":
        raise ValueError(f"Expected a run of {workflow}")


def main():
    repository = os.environ["GITHUB_REPOSITORY"]
    tag = os.environ["RELEASE_TAG"]
    mac_run = os.environ["MACOS_RUN_ID"]
    x64_run = os.environ["X64_RUN_ID"]
    if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
        raise ValueError("Use an existing vMAJOR.MINOR.PATCH tag")
    if not all(re.fullmatch(r"[1-9]\d*", value) for value in (mac_run, x64_run)):
        raise ValueError("Build run IDs must be positive integers")

    def api(path):
        request = urllib.request.Request(
            f"{os.environ['GITHUB_API_URL']}/repos/{repository}/{path}",
            headers={"Authorization": f"Bearer {os.environ['GH_TOKEN']}",
                     "Accept": "application/vnd.github+json", "User-Agent": "arranger-release"})
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)

    obj = api(f"git/ref/tags/{tag}")["object"]
    for _ in range(10):
        if obj["type"] == "commit":
            break
        if obj["type"] != "tag":
            raise ValueError("Tag does not resolve to a commit")
        obj = api(f"git/tags/{obj['sha']}")["object"]
    else:
        raise ValueError("Too many nested annotated tags")
    sha = obj["sha"]
    comparison = api(f"compare/{sha}...main")
    if comparison["status"] not in ("ahead", "identical"):
        raise ValueError("Release commit must be on main's history")
    contents = api(f"contents/CMakeLists.txt?ref={sha}")
    validate_version(tag, base64.b64decode(contents["content"]).decode())
    for run_id, workflow, names in (
        (mac_run, "ci.yml", {"ArrangerMidiDaw-macOS"}),
        (x64_run, "x64-builds.yml", {"ArrangerMidiDaw-windows-x64", "ArrangerMidiDaw-linux-x64-ubuntu-26.04"}),
    ):
        validate_run(api(f"actions/runs/{run_id}"), sha, repository, workflow)
        # Each workflow produces fewer than 100 artifacts; reject incomplete sets.
        artifacts = api(f"actions/runs/{run_id}/artifacts?per_page=100")["artifacts"]
        available = {a["name"] for a in artifacts if not a["expired"]}
        if not names.issubset(available):
            raise ValueError(f"{workflow}: required build artifacts are missing or expired")
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"sha={sha}\ntag={tag}\n")
    print(f"Validated {tag} at {sha}; both build runs match")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, urllib.error.URLError, TimeoutError) as error:
        print(f"Release validation failed: {error}", file=sys.stderr)
        sys.exit(1)
