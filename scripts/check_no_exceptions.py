#!/usr/bin/env python3
"""Reject C++ exception syntax in NativeKit production sources.

The web backend embeds JavaScript in EM_JS macros. Those bodies may use
JavaScript promises and exceptions, but they are not C++ exception syntax and
must be removed before scanning the C++ source tokens.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm"}
EM_JS_START = re.compile(r"\bEM_(?:ASYNC_)?JS\s*\(")
TOKEN = re.compile(r"\b(?:try|catch|throw)\b")


def mask_emscripten_body(source: str) -> str:
    """Replace EM_JS macro invocations with whitespace while preserving lines."""

    output = list(source)
    position = 0
    while match := EM_JS_START.search(source, position):
        opening = source.find("(", match.start(), match.end())
        depth = 0
        cursor = opening
        quote: str | None = None
        escaped = False
        line_comment = False
        block_comment = False
        while cursor < len(source):
            character = source[cursor]
            next_character = source[cursor + 1] if cursor + 1 < len(source) else ""
            if line_comment:
                if character == "\n":
                    line_comment = False
            elif block_comment:
                if character == "*" and next_character == "/":
                    block_comment = False
                    cursor += 1
            elif quote:
                if escaped:
                    escaped = False
                elif character == "\\":
                    escaped = True
                elif character == quote:
                    quote = None
            elif character in "'\"`":
                quote = character
            elif character == "/" and next_character == "/":
                line_comment = True
                cursor += 1
            elif character == "/" and next_character == "*":
                block_comment = True
                cursor += 1
            elif character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    cursor += 1
                    break
            cursor += 1
        for index in range(match.start(), min(cursor, len(source))):
            if source[index] != "\n":
                output[index] = " "
        position = max(cursor, match.end())
    return "".join(output)


def mask_comments_and_literals(source: str) -> str:
    """Mask comments and C/C++/Objective-C literals without changing lines."""

    output = list(source)
    cursor = 0
    while cursor < len(source):
        character = source[cursor]
        next_character = source[cursor + 1] if cursor + 1 < len(source) else ""
        if character == "/" and next_character == "/":
            end = source.find("\n", cursor)
            end = len(source) if end < 0 else end
            for index in range(cursor, end):
                output[index] = " "
            cursor = end
            continue
        if character == "/" and next_character == "*":
            end = source.find("*/", cursor + 2)
            end = len(source) if end < 0 else end + 2
            for index in range(cursor, end):
                if source[index] != "\n":
                    output[index] = " "
            cursor = end
            continue
        if character in "'\"":
            quote = character
            end = cursor + 1
            escaped = False
            while end < len(source):
                current = source[end]
                if escaped:
                    escaped = False
                elif current == "\\":
                    escaped = True
                elif current == quote:
                    end += 1
                    break
                end += 1
            for index in range(cursor, min(end, len(source))):
                if source[index] != "\n":
                    output[index] = " "
            cursor = end
            continue
        cursor += 1
    return "".join(output)


def production_sources(root: Path) -> list[Path]:
    directories = [
        root / "include",
        root / "src",
        root / "modules" / "gpu" / "include",
        root / "modules" / "gpu" / "src",
        root / "modules" / "ui" / "include",
        root / "modules" / "ui" / "src",
    ]
    return sorted(
        path
        for directory in directories
        if directory.exists()
        for path in directory.rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    violations: list[tuple[Path, int, str]] = []
    for path in production_sources(root):
        source = path.read_text(encoding="utf-8")
        masked = mask_comments_and_literals(mask_emscripten_body(source))
        for match in TOKEN.finditer(masked):
            line = masked.count("\n", 0, match.start()) + 1
            violations.append((path.relative_to(root), line, match.group()))
    if violations:
        for path, line, token in violations:
            print(f"{path}:{line}: forbidden C++ exception token: {token}", file=sys.stderr)
        return 1
    print("No C++ exception syntax found in NativeKit production sources.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
