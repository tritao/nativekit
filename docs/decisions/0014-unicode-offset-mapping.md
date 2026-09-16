# ADR 0014: Explicit Unicode offset mapping

Status: accepted for the text-editing-quality program.

## Decision

NativeKit text APIs distinguish their coordinate systems by type:

```text
Utf8Offset       UTF-8 byte offset
CodepointOffset  Unicode scalar-value offset used by the editor
Utf16Offset      UTF-16 code-unit offset used by Android and Apple APIs
GraphemePosition code-point offset aligned to a user-perceived character
```

`TextOffsetMap` owns the conversions. It validates UTF-8, precomputes the
code-point-to-byte and code-point-to-UTF-16 boundaries, and rejects offsets
that split an encoded scalar value or surrogate pair. Grapheme navigation is
boundary-based and can accept shaping-engine boundaries when they are more
authoritative than the fallback Unicode rules.

The editor keeps one map for the current document and lazily caches the map
for the paragraph containing the active selection focus. Paragraph lookup is
indexed, so repeated IME queries do not rescan the document string.

## Consequences

- Platform adapters must convert UTF-8 or UTF-16 positions before creating an
  `EditTransaction`.
- Editor selection, composition, and transaction ranges are explicitly
  `CodepointOffset` values while remaining ABI-compatible integer values.
- Unicode tests cover ASCII, combining marks, emoji ZWJ sequences, flags,
  modifiers, Arabic, Hebrew, Devanagari, Japanese, and mixed RTL/LTR text.
