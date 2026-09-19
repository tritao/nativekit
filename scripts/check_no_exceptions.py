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
PREPROCESSOR_DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def evaluate_no_exceptions_condition(directive: str) -> bool | None:
    """Evaluate a preprocessor condition involving NK_ENABLE_NO_EXCEPTIONS.

    The audit models the production configuration, where this option is enabled.
    Conditions for other platform or build macros remain unknown so both branches
    continue to be audited.
    """

    expression = directive.strip()
    if not re.search(r"\bNK_ENABLE_NO_EXCEPTIONS\b", expression):
        return None
    expression = re.sub(
        r"defined\s*\(\s*NK_ENABLE_NO_EXCEPTIONS\s*\)", "1", expression
    )
    expression = re.sub(r"\bNK_ENABLE_NO_EXCEPTIONS\b", "1", expression)
    if re.search(r"[A-Za-z_]", expression):
        return None
    expression = expression.replace("&&", " and ").replace("||", " or ")
    expression = re.sub(r"!(?!=)", " not ", expression)
    expression_without_operators = expression.replace("and", "").replace("or", "").replace(
        "not", ""
    )
    if re.search(r"[^0-9()<>!=+*/%\-\s]", expression_without_operators):
        return None
    try:
        return bool(eval(expression, {"__builtins__": {}}, {}))
    except (SyntaxError, TypeError, ValueError):
        return None


def mask_disabled_no_exception_branches(source: str) -> str:
    """Mask branches excluded when NK_ENABLE_NO_EXCEPTIONS is enabled."""

    output: list[str] = []
    current_active = True
    stack: list[dict[str, bool | None]] = []
    for line in source.splitlines(keepends=True):
        match = PREPROCESSOR_DIRECTIVE.match(line)
        if not match:
            output.append(line if current_active else "".join("\n" if c == "\n" else " " for c in line))
            continue

        directive, expression = match.groups()
        if directive in {"if", "ifdef", "ifndef"}:
            condition = evaluate_no_exceptions_condition(expression)
            if directive == "ifdef" and expression.strip() == "NK_ENABLE_NO_EXCEPTIONS":
                condition = True
            elif directive == "ifndef" and expression.strip() == "NK_ENABLE_NO_EXCEPTIONS":
                condition = False
            stack.append(
                {
                    "parent_active": current_active,
                    "branch_taken": condition,
                }
            )
            current_active = current_active and condition is not False
        elif directive == "elif" and stack:
            frame = stack[-1]
            condition = evaluate_no_exceptions_condition(expression)
            parent_active = bool(frame["parent_active"])
            branch_taken = frame["branch_taken"]
            if branch_taken is True:
                current_active = False
            elif branch_taken is None:
                current_active = parent_active
            else:
                current_active = parent_active and condition is not False
                frame["branch_taken"] = condition
        elif directive == "else" and stack:
            frame = stack[-1]
            parent_active = bool(frame["parent_active"])
            branch_taken = frame["branch_taken"]
            current_active = parent_active and branch_taken is not True
            frame["branch_taken"] = True
        elif directive == "endif" and stack:
            frame = stack.pop()
            current_active = bool(frame["parent_active"])

        output.append("".join("\n" if c == "\n" else " " for c in line))
    return "".join(output)


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
        masked = mask_comments_and_literals(
            mask_disabled_no_exception_branches(mask_emscripten_body(source))
        )
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
