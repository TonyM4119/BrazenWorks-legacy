#!/usr/bin/env python3
"""Rename the product in one step.

The code never hard-codes the product name: CMake reads branding.json and
generates Branding.h, the executable name, the macOS bundle and the Linux
desktop entry from it. This script updates branding.json and then replaces the
old name in prose files (README, docs, LICENSE...) so everything matches.

    python3 tools/rename.py --name "New Name"                 # slug derived: newname
    python3 tools/rename.py --name "New Name" --slug new-name
    python3 tools/rename.py --name "New Name" --dry-run       # show what would change

Then delete your build folder (or re-run CMake) and rebuild.
Repo URL and domain are updated by substitution too; check them afterwards.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BRANDING = ROOT / "branding.json"

# Prose and config files where the name may appear in text.
TEXT_SUFFIXES = {".md", ".txt", ".yml", ".yaml", ".toml", ".in", ".qss", ""}
SKIP_DIRS = {".git", "build", "target", "cargo", ".cache", "node_modules"}
SKIP_FILES = {"branding.json", "rename.py", "Cargo.lock"}


def derive_slug(name: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return slug.replace("-", "") if len(slug) <= 20 else slug


def candidate_files() -> list[Path]:
    files = []
    for path in ROOT.rglob("*"):
        if not path.is_file() or path.name in SKIP_FILES:
            continue
        rel = path.relative_to(ROOT)
        if any(part in SKIP_DIRS or part.startswith("build") for part in rel.parts[:-1]):
            continue
        if path.suffix in TEXT_SUFFIXES or path.name in {"LICENSE", ".gitignore"}:
            files.append(path)
    return sorted(files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--name", required=True, help='New display name, e.g. "Billet"')
    parser.add_argument("--slug", help="New lowercase id for binaries/paths (default: derived from name)")
    parser.add_argument("--dry-run", action="store_true", help="Show changes without writing")
    args = parser.parse_args()

    brand = json.loads(BRANDING.read_text(encoding="utf-8"))
    old_name, old_slug = brand["displayName"], brand["slug"]
    new_name = args.name.strip()
    new_slug = args.slug or derive_slug(new_name)

    if not re.fullmatch(r"[a-z][a-z0-9-]*", new_slug):
        sys.exit(f"error: slug '{new_slug}' must be lowercase letters, digits and dashes")
    if (old_name, old_slug) == (new_name, new_slug):
        print("Nothing to do: name and slug are unchanged.")
        return 0

    # Order matters: longest/most specific strings first.
    replacements = [(old_name, new_name), (old_slug.upper(), new_slug.upper()), (old_slug, new_slug)]

    def rename(text: str) -> str:
        for old, new in replacements:
            text = text.replace(old, new)
        return text

    new_brand = {key: rename(value) if isinstance(value, str) else value for key, value in brand.items()}
    new_brand["displayName"], new_brand["slug"] = new_name, new_slug

    print(f"Renaming {old_name} ({old_slug}) -> {new_name} ({new_slug})\n")
    print("branding.json:")
    for key in brand:
        if brand[key] != new_brand[key]:
            print(f"  {key}: {brand[key]!r} -> {new_brand[key]!r}")

    changed = []
    for path in candidate_files():
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        updated = rename(text)
        if updated != text:
            changed.append(path)
            if not args.dry_run:
                path.write_text(updated, encoding="utf-8")

    if not args.dry_run:
        BRANDING.write_text(json.dumps(new_brand, indent=2) + "\n", encoding="utf-8")

    print(f"\n{'Would update' if args.dry_run else 'Updated'} {len(changed)} other file(s):")
    for path in changed:
        print(f"  {path.relative_to(ROOT)}")
    if not args.dry_run:
        print("\nDone. Re-run CMake (or delete your build folder) and rebuild.")
        print("If the GitHub repo is renamed too, update the git remote: git remote set-url origin <new-url>")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
