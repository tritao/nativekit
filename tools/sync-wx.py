#!/usr/bin/env python3
"""Report wxWidgets donor changes without modifying the working tree."""

import argparse
import json
import subprocess
import sys
from pathlib import Path


def git(repo: Path, *args: str) -> str:
    process = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if process.returncode:
        raise RuntimeError(process.stderr.strip() or "git command failed")
    return process.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("upstream", type=Path, help="local wxWidgets Git checkout")
    parser.add_argument(
        "--revision", default="HEAD", help="upstream revision to compare (default: HEAD)"
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    lock = json.loads((root / "tools/upstream-lock.json").read_text(encoding="utf-8"))
    baseline = lock["wxwidgets"]["commit"]
    git(args.upstream, "cat-file", "-e", f"{baseline}^{{commit}}")
    target = git(args.upstream, "rev-parse", f"{args.revision}^{{commit}}")

    changed = set(
        filter(None, git(args.upstream, "diff", "--name-only", baseline, target).splitlines())
    )
    print(f"wxWidgets baseline: {baseline}")
    print(f"wxWidgets target:   {target}")

    derived = lock.get("derived_files", {})
    affected = 0
    for local_path, provenance in sorted(derived.items()):
        sources = provenance.get("sources")
        if sources is None:
            source = provenance.get("source")
            sources = [source] if source else []
        touched = sorted(set(sources) & changed)
        if touched:
            affected += 1
            print(f"AFFECTED {local_path}: {', '.join(touched)}")

    print(f"Changed upstream files: {len(changed)}")
    print(f"Affected derived files: {affected}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, KeyError, json.JSONDecodeError) as error:
        print(f"sync-wx: {error}", file=sys.stderr)
        raise SystemExit(1)

