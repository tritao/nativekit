#!/usr/bin/env python3
"""Build compact TTF assets for the fixed Haxeon UI showcase.

The NativeKit font API accepts OpenType TTF/OTF data directly.  This tool keeps
that runtime contract while removing glyphs that do not occur in the showcase
sources.  The source text is intentionally collected at build time so adding a
new localized sample automatically invalidates the generated assets.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import tempfile

try:
    from fontTools import subset
    from fontTools.ttLib import TTFont
except ModuleNotFoundError as error:
    raise SystemExit(
        "subset-showcase-fonts: requires Python fontTools; "
        "install the 'fonttools' package"
    ) from error


FONT_NAMES = (
    "IBMPlexSans-Regular.ttf",
    "IBMPlexSansArabic-Regular.ttf",
    "IBMPlexSansHebrew-Regular.ttf",
    "IBMPlexSansJP-Regular.ttf",
    "NotoEmoji-Regular.ttf",
)


def showcase_codepoints(showcase_dir: Path) -> set[int]:
    codepoints: set[int] = set()
    for source in sorted(showcase_dir.rglob("*.hx")):
        codepoints.update(ord(character) for character in source.read_text(encoding="utf-8"))
    if not codepoints:
        raise SystemExit(f"subset-showcase-fonts: no Haxe sources found under {showcase_dir}")
    return codepoints


def subset_font(source: Path, destination: Path, codepoints: set[int]) -> tuple[int, int, int, int]:
    font = TTFont(source)
    original_glyphs = len(font.getGlyphOrder())
    original_bytes = source.stat().st_size
    cmap = font.getBestCmap() or {}
    requested = sorted(codepoint for codepoint in codepoints if codepoint in cmap)
    if not requested:
        raise SystemExit(f"subset-showcase-fonts: {source.name} has no showcase characters")

    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = [1, 2, 4, 6]
    options.notdef_glyph = True
    options.notdef_outline = True
    options.recalc_average_width = True
    options.recalc_max_context = True
    options.canonical_order = True
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=requested)
    subsetter.subset(font)

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, suffix=".ttf", delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        font.save(temporary_path)
        os.replace(temporary_path, destination)
    finally:
        temporary_path.unlink(missing_ok=True)

    return (
        original_glyphs,
        len(font.getGlyphOrder()),
        original_bytes,
        destination.stat().st_size,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--showcase-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()

    codepoints = showcase_codepoints(arguments.showcase_dir)
    print(
        f"subset-showcase-fonts: collected {len(codepoints)} codepoints from "
        f"{arguments.showcase_dir}"
    )
    for name in FONT_NAMES:
        source = arguments.source_dir / name
        destination = arguments.output_dir / name
        if not source.is_file():
            raise SystemExit(f"subset-showcase-fonts: missing source font {source}")
        original_glyphs, output_glyphs, original_bytes, output_bytes = subset_font(
            source, destination, codepoints
        )
        print(
            f"subset-showcase-fonts: {name}: "
            f"{original_glyphs}->{output_glyphs} glyphs, "
            f"{original_bytes}->{output_bytes} bytes"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
