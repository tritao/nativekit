#!/usr/bin/env python3
"""Audit documentation coverage of NativeKit's C and generated HXI APIs.

The C inventory is produced by libclang.  This keeps declaration discovery
correct for typedefs, nested fields, enums, callbacks, and macro-expanded
handles.  HXI files are parsed only to enumerate the generated declarations;
their documentation status comes from the matching C declaration because HXI
files are generated artifacts and should not be edited by hand.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional

try:
    from clang import cindex
except ImportError as error:  # pragma: no cover - exercised on machines without libclang
    cindex = None  # type: ignore[assignment]
    CLANG_IMPORT_ERROR = error
else:
    CLANG_IMPORT_ERROR = None


IDENTIFIER = r"[A-Za-z_][A-Za-z0-9_]*"
PUBLIC_PREFIXES = ("NK_", "NKUI_", "NKS_")
PUBLIC_TYPE_PREFIXES = ("nk_", "nkui_", "nks_")
IMPLEMENTATION_MACROS = {
    "NK_API",
    "NK_CALL",
    "NK_OUT",
    "NK_INOUT",
    "NK_OUT_BUFFER",
    "NK_IN_ARRAY",
    "NK_IN_UTF8_ARRAY",
    "NK_RETURNS_BORROWED_UTF8",
    "NK_UTF8",
    "NK_NULLABLE_UTF8",
    "NK_BORROWED_BUFFER",
    "NK_BORROWED_ARRAY",
    "NK_STATIC",
    "NK_BUILDING_LIBRARY",
    "NKUI_API",
    "NKUI_OUT",
    "NKUI_IN_ARRAY",
    "NKUI_UTF8",
    "NKUI_BUILDING_LIBRARY",
    "NKS_API",
    "NKS_OUT",
    "NKS_UTF8",
    "NKS_RETURNS_BORROWED_UTF8",
    "NKS_HANDLE",
}


@dataclass(frozen=True)
class Comment:
    start: int
    end: int
    start_line: int
    end_line: int
    text: str


@dataclass
class Item:
    kind: str
    name: str
    path: Path
    line: int
    parent: Optional[str] = None
    documented: bool = False
    doc_line: Optional[int] = None
    doc: Optional[str] = None
    source: Optional["Item"] = None
    reason: Optional[str] = None

    @property
    def key(self) -> tuple[str, str, Optional[str]]:
        return (self.kind, self.name, self.parent)

    @property
    def qualified_name(self) -> str:
        return f"{self.parent}.{self.name}" if self.parent else self.name


@dataclass
class CReport:
    files: list[Path] = field(default_factory=list)
    items: list[Item] = field(default_factory=list)


@dataclass
class HxiReport:
    path: Path
    items: list[Item] = field(default_factory=list)


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def mask_comments(text: str) -> tuple[str, list[Comment]]:
    """Replace comments with whitespace while retaining offsets and newlines."""

    chars = list(text)
    comments: list[Comment] = []
    state = "normal"
    start = 0
    index = 0

    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "normal":
            if current == '"':
                state = "string"
            elif current == "'":
                state = "char"
            elif current == "/" and following == "/":
                state = "line-comment"
                start = index
                chars[index:index + 2] = [" ", " "]
                index += 2
                continue
            elif current == "/" and following == "*":
                state = "block-comment"
                start = index
                chars[index:index + 2] = [" ", " "]
                index += 2
                continue
        elif state == "string":
            if current == "\\":
                index += 2
                continue
            if current == '"':
                state = "normal"
        elif state == "char":
            if current == "\\":
                index += 2
                continue
            if current == "'":
                state = "normal"
        elif state == "line-comment":
            chars[index] = " "
            if current == "\n":
                comments.append(Comment(start, index, line_number(text, start),
                                        line_number(text, index), text[start:index]))
                state = "normal"
        elif state == "block-comment":
            if current == "*" and following == "/":
                chars[index:index + 2] = [" ", " "]
                end = index + 2
                comments.append(Comment(start, end, line_number(text, start),
                                        line_number(text, index), text[start:end]))
                state = "normal"
                index += 2
                continue
            if current != "\n":
                chars[index] = " "
        index += 1

    if state == "line-comment":
        comments.append(Comment(start, len(text), line_number(text, start),
                                line_number(text, max(0, len(text) - 1)), text[start:]))
    return "".join(chars), comments


def normalize_comment(text: str) -> str:
    text = re.sub(r"^\s*//", "", text)
    text = re.sub(r"^\s*/\*+", "", text)
    text = re.sub(r"\*/\s*$", "", text)
    lines = [re.sub(r"^\s*\* ?", "", line).strip() for line in text.splitlines()]
    return " ".join(line for line in lines if line).strip()


def is_documentation(text: str) -> bool:
    normalized = normalize_comment(text)
    return bool(normalized) and re.search(r"[A-Za-z0-9]", normalized) is not None


def comment_for(comments: list[Comment], text: str, offset: int) -> Optional[Comment]:
    """Find an ordinary C comment immediately preceding an AST declaration.

    libclang exposes `/** ... */` comments through `raw_comment`, but the
    existing headers also use ordinary `/* ... */` and `// ...` documentation.
    This small fallback preserves coverage for that established style.
    """

    candidates = [comment for comment in comments if comment.end <= offset]
    if not candidates:
        return None
    comment = candidates[-1]
    if text[comment.end:offset].strip():
        return None
    if line_number(text, offset) - comment.end_line > 2:
        return None
    if not is_documentation(comment.text):
        return None
    return comment


def attach_cursor(item: Item, cursor: object, source: str, comments: list[Comment],
                  offset: int, related: Iterable[object] = ()) -> Item:
    raw = getattr(cursor, "raw_comment", None)
    if not raw:
        for candidate in related:
            raw = getattr(candidate, "raw_comment", None)
            if raw:
                break
    if raw and is_documentation(raw):
        item.documented = True
        raw_offset = source.rfind(raw, 0, offset)
        item.doc_line = line_number(source, raw_offset if raw_offset >= 0 else offset)
        item.doc = normalize_comment(raw)
        return item
    comment = comment_for(comments, source, offset)
    if comment is not None:
        item.documented = True
        item.doc_line = comment.start_line
        item.doc = normalize_comment(comment.text)
    return item


def public_name(name: str, type_name: bool = False) -> bool:
    prefixes = PUBLIC_TYPE_PREFIXES if type_name else PUBLIC_PREFIXES
    return bool(re.fullmatch(IDENTIFIER, name)) and name.startswith(prefixes)


def resolve_location(location: object, root: Path) -> Optional[Path]:
    filename = getattr(location, "file", None)
    if filename is None:
        return None
    path = Path(str(filename))
    return (root / path).resolve() if not path.is_absolute() else path.resolve()


def cursor_children(cursor: object) -> Iterable[object]:
    for child in cursor.get_children():  # type: ignore[attr-defined]
        yield child
        yield from cursor_children(child)


def enum_parent(cursor: object) -> Optional[str]:
    parent = getattr(cursor, "semantic_parent", None)
    name = getattr(parent, "spelling", "") if parent is not None else ""
    if name and not name.startswith("enum (unnamed"):
        return name
    return None


def macro_is_function_like(cursor: object, source: str) -> bool:
    location = getattr(cursor, "location")
    line = source.splitlines()[location.line - 1] if location.line else ""
    return re.search(rf"#\s*define\s+{re.escape(cursor.spelling)}\s*\(", line) is not None  # type: ignore[attr-defined]


def cursor_start(cursor: object) -> tuple[int, int]:
    extent = getattr(cursor, "extent", None)
    start = getattr(extent, "start", None) if extent is not None else None
    if start is None:
        start = getattr(cursor, "location")
    return getattr(start, "offset", 0), getattr(start, "line", getattr(cursor.location, "line", 0))


def load_clang(library: Optional[str]) -> object:
    if cindex is None:
        raise RuntimeError(
            "libclang Python bindings are required; install the clang Python package "
            f"({CLANG_IMPORT_ERROR})"
        )
    library = library or os.environ.get("CLANG_LIBRARY_FILE")
    if library:
        try:
            cindex.Config.set_library_file(library)
        except Exception as error:
            raise RuntimeError(f"could not load libclang from {library}: {error}") from error
    try:
        return cindex.Index.create()
    except Exception as error:
        hint = " Set CLANG_LIBRARY_FILE or use --clang-library." if not library else ""
        raise RuntimeError(f"could not initialize libclang: {error}.{hint}") from error


def clang_resource_dir(explicit: Optional[str]) -> Optional[str]:
    resource_dir = explicit or os.environ.get("CLANG_RESOURCE_DIR")
    if resource_dir:
        return resource_dir
    compiler = shutil.which("clang")
    if compiler is None:
        return None
    try:
        result = subprocess.run([compiler, "-print-resource-dir"], check=True,
                                capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    value = result.stdout.strip()
    return value or None


def parse_c_file(index: object, path: Path, root: Path, include_fields: bool,
                 include_paths: list[Path], resource_dir: Optional[str]) -> list[Item]:
    source = path.read_text(encoding="utf-8")
    _, comments = mask_comments(source)
    args = ["-x", "c", "-std=c11"]
    if resource_dir:
        args.append(f"-resource-dir={resource_dir}")
    args.extend(f"-I{include}" for include in include_paths)
    try:
        translation_unit = index.parse(  # type: ignore[attr-defined]
            str(path), args=args,
            options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
        )
    except Exception as error:
        raise RuntimeError(f"libclang could not parse {path}: {error}") from error

    diagnostics = [str(diagnostic) for diagnostic in translation_unit.diagnostics
                   if diagnostic.severity >= cindex.Diagnostic.Error]
    if diagnostics:
        raise RuntimeError(f"libclang diagnostics for {path}: {'; '.join(diagnostics)}")

    target = path.resolve()
    items: list[Item] = []
    kinds = cindex.CursorKind
    for cursor in cursor_children(translation_unit.cursor):
        if resolve_location(cursor.location, root) != target:
            continue
        name = cursor.spelling
        if not name:
            continue
        kind = cursor.kind
        offset, start_line = cursor_start(cursor)
        if kind == kinds.FUNCTION_DECL and name.startswith(("nk_", "nkui_", "nks_")):
            items.append(attach_cursor(Item("function", name, path, start_line), cursor,
                                        source, comments, offset))
        elif kind == kinds.TYPEDEF_DECL and public_name(name, type_name=True):
            children = list(cursor.get_children())
            items.append(attach_cursor(Item("type", name, path, start_line), cursor,
                                        source, comments, offset, children))
        elif kind == kinds.ENUM_CONSTANT_DECL and public_name(name, type_name=False):
            items.append(attach_cursor(Item("constant", name, path, start_line,
                                            parent=enum_parent(cursor)), cursor, source, comments, offset))
        elif kind == kinds.FIELD_DECL and include_fields:
            parent = getattr(cursor, "semantic_parent", None)
            parent_name = getattr(parent, "spelling", "") if parent is not None else ""
            if public_name(parent_name, type_name=True):
                items.append(attach_cursor(Item("field", name, path, start_line,
                                                parent=parent_name), cursor, source, comments, offset))
        elif kind == kinds.MACRO_DEFINITION and public_name(name, type_name=False):
            if name in IMPLEMENTATION_MACROS or macro_is_function_like(cursor, source):
                continue
            items.append(attach_cursor(Item("macro", name, path, start_line), cursor,
                                        source, comments, offset))

    unique: dict[tuple[str, str, Optional[str]], Item] = {}
    for item in items:
        unique.setdefault(item.key, item)
    return sorted(unique.values(), key=lambda item: (item.line, item.kind, item.qualified_name))


def parse_c_headers(root: Path, headers: list[Path], include_fields: bool,
                    library: Optional[str], resource_dir: Optional[str]) -> CReport:
    index = load_clang(library)
    include_paths = [root / "include"]
    include_paths.extend(sorted(path.parent for path in headers if path.parent != root / "include"))
    report = CReport(files=headers)
    for path in headers:
        report.items.extend(parse_c_file(index, path, root, include_fields, include_paths,
                                         resource_dir))
    unique: dict[tuple[str, str, Optional[str]], Item] = {}
    for item in report.items:
        unique.setdefault(item.key, item)
    report.items = sorted(unique.values(), key=lambda item: (str(item.path), item.line,
                                                               item.kind, item.qualified_name))
    return report


def parse_hxi_file(path: Path, include_fields: bool) -> list[Item]:
    source = path.read_text(encoding="utf-8")
    masked, comments = mask_comments(source)
    lines = masked.splitlines(keepends=True)
    offsets: list[int] = []
    offset = 0
    for line in lines:
        offsets.append(offset)
        offset += len(line)
    items: list[Item] = []

    def add(kind: str, name: str, line: int, start: int, parent: Optional[str] = None) -> None:
        item = Item(kind, name, path, line, parent=parent)
        comment = comment_for(comments, source, start)
        if comment is not None:
            item.documented = True
            item.doc_line = comment.start_line
            item.doc = normalize_comment(comment.text)
        items.append(item)

    index = 0
    while index < len(lines):
        line = lines[index]
        start = offsets[index]
        match = re.match(rf"\s*const\s+({IDENTIFIER})\b", line)
        if match:
            add("constant", match.group(1), index + 1, start + match.start(1))
            index += 1
            continue

        match = re.match(rf"\s*(?:extern\s+)?fn\s+({IDENTIFIER})\s*\(", line)
        if match:
            add("function", match.group(1), index + 1, start + match.start(1))
            index += 1
            continue

        match = re.match(rf"\s*(struct|enum)\s+({IDENTIFIER})\b", line)
        if match:
            kind_name, name = match.groups()
            add("type", name, index + 1, start + match.start(2))
            depth = line.count("{") - line.count("}")
            index += 1
            while index < len(lines) and depth > 0:
                member_line = lines[index]
                member_start = offsets[index]
                if kind_name == "enum" and ";" in member_line:
                    value = re.match(rf"\s*({IDENTIFIER})\b", member_line)
                    if value and value.group(1).startswith(PUBLIC_PREFIXES):
                        add("constant", value.group(1), index + 1,
                            member_start + value.start(1), parent=name)
                elif include_fields and ";" in member_line:
                    field_match = re.match(rf"\s*({IDENTIFIER})\s*:", member_line)
                    if field_match:
                        add("field", field_match.group(1), index + 1,
                            member_start + field_match.start(1), parent=name)
                depth += member_line.count("{") - member_line.count("}")
                index += 1
            continue

        match = re.match(rf"\s*(?:type|callback|opaque)\s+({IDENTIFIER})\b", line)
        if match:
            add("type", match.group(1), index + 1, start + match.start(1))
        index += 1

    unique: dict[tuple[str, str, Optional[str]], Item] = {}
    for item in items:
        unique.setdefault(item.key, item)
    return sorted(unique.values(), key=lambda item: (item.line, item.kind, item.qualified_name))


def discover_headers(root: Path) -> list[Path]:
    paths = list((root / "include").glob("*.h"))
    for directory in sorted((root / "modules").glob("*/include")):
        paths.extend(directory.glob("*.h"))
    return sorted(path.resolve() for path in paths if path.is_file())


def discover_hxi(root: Path) -> list[Path]:
    paths = list((root / "bindings").rglob("*.hxi"))
    paths.extend((root / "modules").rglob("*.hxi"))
    return sorted(set(path.resolve() for path in paths if path.is_file()))


def relative(path: Path, root: Path) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def summary(items: list[Item]) -> dict[str, int]:
    return {"total": len(items), "documented": sum(item.documented for item in items),
            "missing": sum(not item.documented for item in items)}


def hxi_reports(root: Path, files: list[Path], source_items: list[Item],
                include_fields: bool) -> list[HxiReport]:
    source_by_key = {item.key: item for item in source_items}
    reports: list[HxiReport] = []
    for path in files:
        report = HxiReport(path)
        for item in parse_hxi_file(path, include_fields):
            source = source_by_key.get(item.key)
            item.source = source
            if source is None:
                item.reason = "no matching C declaration"
            else:
                item.documented = source.documented
                item.doc_line = source.doc_line
                item.doc = source.doc
                if not source.documented:
                    item.reason = "C declaration is undocumented"
            report.items.append(item)
        reports.append(report)
    return reports


def item_dict(item: Item, root: Path) -> dict[str, object]:
    result: dict[str, object] = {
        "kind": item.kind,
        "name": item.name,
        "qualified_name": item.qualified_name,
        "path": relative(item.path, root),
        "line": item.line,
        "documented": item.documented,
    }
    if item.parent:
        result["parent"] = item.parent
    if item.doc_line is not None:
        result["doc_line"] = item.doc_line
    if item.reason:
        result["reason"] = item.reason
    if item.source:
        result["source"] = {"path": relative(item.source.path, root), "line": item.source.line}
    return result


def text_report(root: Path, c: CReport, hxi: list[HxiReport], only_missing: bool,
                include_fields: bool) -> str:
    output = ["C public API documentation audit"]
    c_summary = summary(c.items)
    output.append(f"  parser: libclang  declarations: {c_summary['total']}  documented: {c_summary['documented']}  missing: {c_summary['missing']}")
    scope = "functions, types, constants, macros, and struct fields" if include_fields else \
        "functions, types, constants, and macros (use --fields for struct fields)"
    output.append(f"  scope: {scope}")
    for item in c.items:
        if only_missing and item.documented:
            continue
        status = "OK" if item.documented else "MISSING"
        detail = f" (comment at line {item.doc_line})" if item.documented else ""
        output.append(f"  [{status}] {relative(item.path, root)}:{item.line} {item.kind} {item.qualified_name}{detail}")

    output.extend(["", "Generated HXI documentation audit",
                   "  HXI files are artifacts; coverage is inherited from the matching C declaration."])
    for report in hxi:
        hxi_summary = summary(report.items)
        output.append(f"  {relative(report.path, root)}: declarations: {hxi_summary['total']}  documented: {hxi_summary['documented']}  missing: {hxi_summary['missing']}")
        for item in report.items:
            if only_missing and item.documented:
                continue
            status = "OK" if item.documented else "MISSING"
            source = f" -> {relative(item.source.path, root)}:{item.source.line}" if item.source else " -> no matching C declaration"
            output.append(f"    [{status}] {relative(item.path, root)}:{item.line} {item.kind} {item.qualified_name}{source}")
    return "\n".join(output)


def markdown_report(root: Path, c: CReport, hxi: list[HxiReport], only_missing: bool) -> str:
    c_summary = summary(c.items)
    lines = ["# NativeKit API documentation audit", "",
             f"C declarations: **{c_summary['total']}** · documented: **{c_summary['documented']}** · missing: **{c_summary['missing']}**",
             "", "Generated HXI files are checked against their matching C declaration; they are not edited directly.",
             "", "## C declarations", "", "| Status | Declaration | Location |", "| --- | --- | --- |"]
    for item in c.items:
        if only_missing and item.documented:
            continue
        status = "documented" if item.documented else "missing"
        lines.append(f"| {status} | `{item.qualified_name}` ({item.kind}) | `{relative(item.path, root)}:{item.line}` |")
    lines.extend(["", "## Generated HXI files", ""])
    for report in hxi:
        hxi_summary = summary(report.items)
        lines.extend([f"### `{relative(report.path, root)}`", "",
                      f"Declarations: **{hxi_summary['total']}** · documented: **{hxi_summary['documented']}** · missing: **{hxi_summary['missing']}**",
                      "", "| Status | Declaration | HXI line | C source |", "| --- | --- | --- | --- |"])
        for item in report.items:
            if only_missing and item.documented:
                continue
            status = "documented" if item.documented else "missing"
            source = f"`{relative(item.source.path, root)}:{item.source.line}`" if item.source else "not found"
            lines.append(f"| {status} | `{item.qualified_name}` ({item.kind}) | `{item.line}` | {source} |")
        lines.append("")
    return "\n".join(lines).rstrip()


def json_report(root: Path, c: CReport, hxi: list[HxiReport], include_fields: bool) -> dict[str, object]:
    return {
        "scope": {
            "c_parser": "libclang",
            "c_headers": [relative(path, root) for path in c.files],
            "include_struct_fields": include_fields,
            "hxi_coverage_basis": "matching C declaration documentation",
        },
        "c": {"summary": summary(c.items), "items": [item_dict(item, root) for item in c.items]},
        "hxi": [{"path": relative(report.path, root), "summary": summary(report.items),
                 "items": [item_dict(item, root) for item in report.items]} for report in hxi],
    }


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="repository root (default: parent of scripts/)")
    parser.add_argument("--c-header", action="append", type=Path, dest="c_headers",
                        help="C header to scan; repeatable (default: public headers)")
    parser.add_argument("--hxi", action="append", type=Path, dest="hxi_files",
                        help="generated HXI file to scan; repeatable (default: repository HXI files)")
    parser.add_argument("--clang-library", help="libclang shared library path (or use CLANG_LIBRARY_FILE)")
    parser.add_argument("--clang-resource-dir",
                        help="Clang resource directory (or use CLANG_RESOURCE_DIR; auto-detected by default)")
    parser.add_argument("--no-hxi", action="store_true", help="skip generated HXI files")
    parser.add_argument("--fields", action="store_true", help="also audit public struct fields")
    parser.add_argument("--only-missing", action="store_true", help="omit documented items from text/Markdown output")
    parser.add_argument("--format", choices=("text", "markdown", "json"), default="text")
    parser.add_argument("--fail-on-missing", action="store_true",
                        help="return status 1 when any C or HXI declaration is missing documentation")
    args = parser.parse_args(argv)

    root = args.root.resolve()
    headers = ([path if path.is_absolute() else root / path for path in args.c_headers]
               if args.c_headers else discover_headers(root))
    hxi_files = ([] if args.no_hxi else
                 [path if path.is_absolute() else root / path for path in args.hxi_files]
                 if args.hxi_files else discover_hxi(root))
    headers = sorted(set(path.resolve() for path in headers if path.is_file()))
    hxi_files = sorted(set(path.resolve() for path in hxi_files if path.is_file()))
    if not headers:
        parser.error("no C headers found")

    try:
        c = parse_c_headers(root, headers, args.fields, args.clang_library,
                            clang_resource_dir(args.clang_resource_dir))
    except RuntimeError as error:
        print(f"audit-api-docs: error: {error}", file=sys.stderr)
        return 2
    hxi = hxi_reports(root, hxi_files, c.items, args.fields)

    if args.format == "json":
        print(json.dumps(json_report(root, c, hxi, args.fields), indent=2, sort_keys=True))
    elif args.format == "markdown":
        print(markdown_report(root, c, hxi, args.only_missing))
    else:
        print(text_report(root, c, hxi, args.only_missing, args.fields))

    missing = summary(c.items)["missing"] + sum(summary(report.items)["missing"] for report in hxi)
    return 1 if args.fail_on_missing and missing else 0


if __name__ == "__main__":
    sys.exit(main())
