#!/usr/bin/env python3
"""Resolve one version set for every job and check release intent before building."""

import json
import os
import re
import subprocess
import urllib.request
from pathlib import Path


MAX_VERSION_CODE = 2_100_000_000
VERSION_NAME = re.compile(r"v?[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?\Z")
APK_NAME = re.compile(r"^ZySU_.+_([0-9]+)-(?:arm64-v8a-)?release\.apk$")


def version_name(value, field):
    if not isinstance(value, str) or len(value) > 80 or not VERSION_NAME.fullmatch(value):
        raise ValueError(f"{field}: use a version such as 1.8.0 or 1.8.0-rc.1 (max 80 characters)")
    return value.removeprefix("v")


def version_code(value, field):
    if isinstance(value, bool) or not re.fullmatch(r"[1-9][0-9]*", str(value)):
        raise ValueError(f"{field}: expected a positive integer")
    code = int(value)
    if code > MAX_VERSION_CODE:
        raise ValueError(f"{field}: must not exceed {MAX_VERSION_CODE}")
    return code


def published_version_code(releases):
    """Include prereleases so returning to stable cannot accidentally downgrade."""
    codes = [0]
    for release in releases:
        if release.get("draft"):
            continue
        for asset in release.get("assets", []):
            match = APK_NAME.fullmatch(asset.get("name", ""))
            if match:
                codes.append(version_code(match[1], "published APK version"))
    return max(codes)


def automatic_version_code(commit_count, releases, release_commit_counts):
    candidates = [6865 + commit_count]
    for release in releases:
        code = published_version_code([release])
        if not code:
            continue
        tag = release.get("tag_name", "")
        if tag not in release_commit_counts:
            raise ValueError(f"Cannot find commit-count anchor for published tag {tag}")
        # Offset every new commit from the published version, even if its code was
        # manually raised far above the historical 6865 + count formula.
        candidates.append(code + max(0, commit_count - release_commit_counts[tag]))
    return max(candidates)


def resolve(inputs, *, describe, commit_count, releases=(), release_commit_counts=None,
            ref_type="branch", ref_name=""):
    publish = inputs.get("publish_release", False)
    prerelease = inputs.get("prerelease", False)
    if not isinstance(publish, bool) or not isinstance(prerelease, bool):
        raise ValueError("publish_release and prerelease must be booleans")
    tag = inputs.get("release_tag", "")
    if not tag and ref_type == "tag":
        tag = ref_name
    if publish and not tag:
        raise ValueError("发布时必须填写 Release 标签，例如 v1.8.0 / A release tag is required")
    if tag:
        tag = "v" + version_name(tag, "release_tag")
    if publish and any(release.get("tag_name") == tag for release in releases):
        raise ValueError(f"Release {tag} already exists; use a new tag (existing releases are never overwritten)")

    normalized = re.sub(r"-\d+-g([0-9a-fA-F]+)$", r"-\1", describe).removeprefix("v")
    if not VERSION_NAME.fullmatch(normalized):
        if not re.fullmatch(r"[0-9a-fA-F]{7,40}", normalized):
            raise ValueError(f"Cannot derive a version from Git: {describe}; supply version names")
        normalized = "0.0.0-" + normalized
    default_name = tag.removeprefix("v") if tag else normalized
    floor = published_version_code(releases)
    automatic_code = automatic_version_code(commit_count, releases, release_commit_counts or {})
    # A stable release of a commit already offered through CI must still upgrade it.
    automatic_code += int(publish)
    resolved = {"publish_release": publish, "prerelease": prerelease, "release_tag": tag}
    for component in ("manager", "ksud", "kernel"):
        name_field = f"{component}_version_name"
        code_field = f"{component}_version_code"
        name = version_name(inputs.get(name_field) or default_name, name_field)
        resolved[name_field] = "v" + name if component == "manager" else name
        resolved[code_field] = version_code(inputs.get(code_field) or automatic_code, code_field)
    if publish and resolved["manager_version_code"] <= floor:
        raise ValueError(f"manager_version_code must be greater than published version {floor}")
    if publish and resolved["manager_version_code"] < automatic_code:
        raise ValueError(f"manager_version_code must be at least {automatic_code} to upgrade builds of this commit")
    return resolved


def api_json(url, token):
    request = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2022-11-28",
    })
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def list_releases(repository, token):
    releases = []
    # A single summary request per page includes asset names and their version codes.
    for page in range(1, 101):
        batch = api_json(f"https://api.github.com/repos/{repository}/releases?per_page=100&page={page}", token)
        if not isinstance(batch, list):
            raise ValueError("Invalid GitHub releases response")
        releases.extend(batch)
        if len(batch) < 100:
            return releases
    raise ValueError("Too many release pages to determine a safe version code")


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def release_repository(repository):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("GITHUB_REPOSITORY must be an owner/repository name")
    return repository


def main():
    inputs = json.loads(os.environ.get("VERSION_INPUTS", "{}"))
    repository = release_repository(os.environ.get("GITHUB_REPOSITORY", ""))
    token = os.environ.get("GH_TOKEN", "")
    if not token:
        raise ValueError("GH_TOKEN is required to check published versions")
    releases = list_releases(repository, token)
    release_commit_counts = {
        release["tag_name"]: int(git("rev-list", "--count", f"refs/tags/{release['tag_name']}", "--"))
        for release in releases if published_version_code([release])
    }
    versions = resolve(
        inputs, describe=git("describe", "--tags", "--always", "--abbrev=8"),
        commit_count=int(git("rev-list", "--count", "HEAD")), releases=releases,
        release_commit_counts=release_commit_counts,
        ref_type=os.environ.get("GITHUB_REF_TYPE", "branch"), ref_name=os.environ.get("GITHUB_REF_NAME", ""),
    )
    if versions["publish_release"]:
        if os.environ.get("HAS_RELEASE_KEYSTORE") != "true":
            raise ValueError("Release builds require the existing production KEYSTORE secret")
        tag = versions["release_tag"]
        existing = subprocess.run(["git", "rev-parse", "--verify", f"refs/tags/{tag}^{{commit}}"],
                                  text=True, capture_output=True)
        if existing.returncode == 0 and existing.stdout.strip() != git("rev-parse", "HEAD"):
            raise ValueError(f"Tag {tag} points to another commit; select that tag or use a new name")
    versions["commit_sha"] = git("rev-parse", "HEAD")
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as output:
        for key, value in versions.items():
            value = str(value).lower() if isinstance(value, bool) else value
            output.write(f"{key}={value}\n")
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as summary:
        summary.write("\n## 版本 / Versions\n\n| Component | Version | Code |\n| --- | --- | --- |\n")
        for component in ("manager", "ksud", "kernel"):
            summary.write(f"| {component} | {versions[component + '_version_name']} | {versions[component + '_version_code']} |\n")
        summary.write(f"\nPublish Release: **{versions['publish_release']}**; tag: `{versions['release_tag']}`\n")
        summary.write(f"\nRelease repository: `{repository}`\n")


if __name__ == "__main__":
    main()
