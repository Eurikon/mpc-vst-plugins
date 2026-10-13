# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import fnmatch
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any
from urllib.parse import quote, urlsplit
from urllib.request import Request, urlopen


VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
BADGE = re.compile(r"\[!\[(?:Download|Install)\b[^\]\n]*\]\([^\)\n]+\)\]\([^\)\n]+\)")
CURRENT = re.compile(r"(?m)^(\*\*Current release:\*\*\s*)([^\n]*)$")


def load_releases(repo: str, source: Path | None) -> list[dict[str, Any]]:
    if source is not None:
        data = json.loads(source.read_text(encoding="utf-8"))
        if not isinstance(data, list):
            raise ValueError("Release JSON must contain an array")
        return data
    api_token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    headers = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"}
    if api_token:
        headers["Authorization"] = "Bearer " + api_token
    releases = []
    page = 1
    while True:
        url = f"https://api.github.com/repos/{repo}/releases?per_page=100&page={page}"
        with urlopen(Request(url, headers=headers), timeout=30) as response:
            batch = json.load(response)
        if not isinstance(batch, list) or len(batch) > 100:
            raise ValueError("Unexpected GitHub release response")
        releases.extend(batch)
        if len(batch) < 100:
            return releases
        page += 1


def tracked_registries(root: Path, repo: str) -> list[tuple[Path, str]]:
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z", "--", "*/catalog/*.json"],
        check=True, capture_output=True, text=True,
    )
    registries = []
    seen = set()
    for name in sorted(filter(None, result.stdout.split("\0"))):
        parts = Path(name).parts
        if len(parts) != 3 or parts[1] != "catalog":
            continue
        path = root / name
        if not path.resolve().is_relative_to(root) or path.is_symlink():
            raise ValueError(f"Invalid registry path: {name}")
        entry = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(entry, dict):
            raise ValueError(f"Invalid registry record: {name}")
        if entry.get("repo") != repo:
            continue
        identity, pattern = entry.get("id"), entry.get("asset_pattern")
        if not isinstance(identity, str) or not identity or not isinstance(pattern, str):
            raise ValueError(f"Missing plugin identity or asset pattern: {name}")
        if pattern.count("*") != 1 or any(c in pattern for c in "?[]/\\") or not pattern.endswith(".zip"):
            raise ValueError(f"Expected one version wildcard in ZIP asset pattern: {name}")
        readme = root / parts[0] / "README.md"
        if identity in seen or readme in seen:
            raise ValueError(f"Conflicting plugin registry: {name}")
        seen.update((identity, readme))
        if not readme.is_file() or readme.is_symlink() or not readme.resolve().is_relative_to(root):
            raise ValueError(f"Missing or invalid plugin README: {parts[0]}/README.md")
        registries.append((readme, pattern))
    if not registries:
        raise ValueError("No tracked plugin registries match the requested repository")
    return registries


def newest_asset(releases: list[dict[str, Any]], repo: str, pattern: str) -> tuple[str, str] | None:
    prefix, suffix = pattern.split("*")
    candidates = {}
    for release in releases:
        if not isinstance(release, dict) or type(release.get("draft")) is not bool or type(release.get("prerelease")) is not bool:
            raise ValueError("Invalid GitHub release record")
        if release["draft"] or release["prerelease"]:
            continue
        assets = release.get("assets")
        if not isinstance(assets, list):
            raise ValueError("Invalid GitHub release assets")
        matches = []
        for asset in assets:
            if not isinstance(asset, dict) or not isinstance(asset.get("name"), str):
                raise ValueError("Invalid GitHub asset record")
            if fnmatch.fnmatchcase(asset["name"], pattern):
                matches.append(asset)
        if len(matches) > 1:
            raise ValueError(f"Multiple release assets match {pattern}")
        if not matches:
            continue
        asset = matches[0]
        name = asset["name"]
        version = name[len(prefix):len(name) - len(suffix)]
        if not VERSION.fullmatch(version):
            raise ValueError(f"Non-stable version in matching asset: {name}")
        tag, url = release.get("tag_name"), asset.get("browser_download_url")
        if not isinstance(tag, str) or not tag or any(ord(c) < 32 for c in tag):
            raise ValueError(f"Invalid release tag for {name}")
        if not isinstance(url, str):
            raise ValueError(f"Missing download URL for {name}")
        expected = f"https://github.com/{repo}/releases/download/{quote(tag, safe='')}/{quote(name, safe='')}"
        parsed = urlsplit(url)
        if parsed.scheme != "https" or parsed.netloc != "github.com" or url != expected:
            raise ValueError(f"Invalid GitHub download URL for {name}")
        numeric = tuple(int(n) for n in version.split("."))
        if numeric in candidates:
            raise ValueError(f"Duplicate release version for {pattern}: {version}")
        candidates[numeric] = (version, url)
    return candidates[max(candidates)] if candidates else None


def update_readme(text: str, version: str, url: str) -> str:
    badges = list(BADGE.finditer(text))
    if len(badges) != 1:
        raise ValueError("Plugin README must have exactly one Download or Install badge")
    badge = f"[![Download {version}](https://img.shields.io/badge/Download-{version}-brightgreen?logo=github)]({url})"
    text = BADGE.sub(lambda match: badge, text, count=1)
    current = list(CURRENT.finditer(text))
    if len(current) > 1:
        raise ValueError("Plugin README has multiple Current release lines")
    if current:
        match = current[0]
        old = re.match(r"[0-9]+\.[0-9]+\.[0-9]+(?=\D|$)", match[2])
        if old is None:
            raise ValueError("Plugin README has an invalid Current release version")
        text = text[:match.start(2)] + version + text[match.start(2) + old.end():]
    return text


def synchronize(root: Path, repo: str, releases: list[dict[str, Any]], check: bool) -> list[Path]:
    root = root.resolve()
    changes = []
    for readme, pattern in tracked_registries(root, repo):
        candidate = newest_asset(releases, repo, pattern)
        if candidate is None:
            continue
        with readme.open(encoding="utf-8", newline="") as stream:
            before = stream.read()
        after = update_readme(before, *candidate)
        if after != before:
            changes.append((readme, after))
    if check and changes:
        names = ", ".join(str(path.relative_to(root)) for path, _ in changes)
        raise ValueError(f"Stale release download links: {names}")
    for path, content in changes:
        with path.open("w", encoding="utf-8", newline="") as stream:
            stream.write(content)
    return [path for path, _ in changes]


def main() -> int:
    parser = argparse.ArgumentParser(description="Refresh plugin download badges from their own stable releases")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--repo", required=True)
    parser.add_argument("--releases-json", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repo):
        parser.error("Expected repository in owner/name format")
    try:
        changed = synchronize(args.root.resolve(), args.repo, load_releases(args.repo, args.releases_json), args.check)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Download link check failed: {error}", file=sys.stderr)
        return 1
    for path in changed:
        print(path.relative_to(args.root.resolve()))
    print(f"Download links {'checked' if args.check else 'updated'}: {len(changed)} changed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
