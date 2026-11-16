#!/usr/bin/env python3
"""Fetch pinned upstream source roots without overwriting existing checkouts.

This prepares source, not running compatibility engines. Darling's submodules,
OpenHarmony's manifest projects and Android's images are separate build inputs.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def git(root, *args):
    return subprocess.check_output(
        ["git", "-C", str(root), *args], text=True
    ).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "build/compat-sources")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lock = Path(__file__).with_name("compat-sources.tsv")
    for line in lock.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        name, url, commit, purpose = line.split("\t", 3)
        root = args.output / name
        if root.is_symlink():
            raise SystemExit(f"Refusing source symlink: {root}")
        if root.exists():
            # Existing work is never reset, cleaned, or silently switched.
            if git(root, "rev-parse", "--show-toplevel") != str(root.resolve()):
                raise SystemExit(f"Not a source checkout: {root}")
            if git(root, "status", "--porcelain"):
                raise SystemExit(f"Source checkout has local changes: {root}")
        else:
            # A failed download must not leave a half-created source root.
            with tempfile.TemporaryDirectory(prefix=f".{name}-", dir=args.output) as temp:
                staging = Path(temp) / name
                staging.mkdir()
                git(staging, "init", "--quiet")
                git(staging, "remote", "add", "origin", url)
                git(staging, "fetch", "--depth=1", "origin", commit)
                git(staging, "checkout", "--quiet", "--detach", "FETCH_HEAD")
                if git(staging, "rev-parse", "HEAD") != commit:
                    raise SystemExit(f"Downloaded revision mismatch: {name}")
                git(staging, "fsck", "--connectivity-only")
                staging.rename(root)
        if git(root, "rev-parse", "HEAD") != commit:
            raise SystemExit(f"Source revision mismatch; preserving checkout: {root}")
        git(root, "fsck", "--connectivity-only")
        print(f"{name}: {commit} ({purpose})", flush=True)


if __name__ == "__main__":
    main()
