# ADR 0015: Skribidi text intrinsic metrics

Status: accepted for the text-editing-quality program.

## Decision

Skribidi is the source of truth for text intrinsic measurement. Its adapter
returns four separate values:

```text
min_content_width  widest segment that cannot legally break
max_content_width  width of the unwrapped paragraph
natural_height     height of the unwrapped paragraph
first_baseline     first-line baseline from the paragraph top
```

The minimum width uses the same Unicode line-breaking and grapheme boundaries
as final paragraph layout:

- no wrapping breaks only at mandatory line breaks;
- word wrapping breaks at allowed line-break opportunities;
- word-and-character wrapping can break at grapheme boundaries.

Each segment is measured with the same Skribidi shaping attributes as the
paragraph, including the resolved base direction. NativeKit passes the result
to Clay as `minWidth` and `unwrappedDimensions`; Clay remains responsible for
applying box constraints and distributing available space.

## Consequences

- FIT and GROW text participates in layout using a real unbreakable minimum.
- Clay no longer needs to guess a text minimum from UTF-8 bytes or generic
  character widths.
- Zero-sized containing blocks still collapse their descendants, including
  text with a non-zero intrinsic minimum.
- Intrinsic measurement can shape several temporary segment layouts; large
  document performance remains a later benchmark-driven optimization.
