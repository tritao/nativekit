# ADR 0013: NativeKit text editing contract

Status: accepted for the text-editing-quality program.

## Decision

The rendered editor, not a platform text widget, owns the document model:

Skribidi/NKUI owns:

- document text;
- the logical selection and caret affinity;
- the active composition range;
- paragraph layout and visual geometry;
- editor-owned undo units.

The NativeKit platform backend owns:

- the platform IME session and keyboard lifecycle;
- candidate-window integration;
- platform services such as autocorrect, dictation, and handwriting;
- conversion between platform offsets and the editor's offsets.

Platform adapters report what changed. They do not become the document
authority or apply keyboard-specific editing rules to the rendered text.

## Mutation boundary

Every document mutation enters through one atomic `EditTransaction`:

```text
replacement range + replacement text
resulting selection
optional resulting composition range
```

Typing, paste, IME composition updates and commits, accessibility replacement,
autocorrect, and future dictation adapters must use this boundary. A
transaction changes the document at most once and updates its selection and
composition metadata as one state transition.

The Haxe editor currently exposes positions as Unicode code-point offsets,
matching the NativeKit text-input ABI. This is an explicit temporary boundary:
platform adapters must convert their native representation before constructing
an editor transaction. The UTF-8 byte, UTF-16 code-unit, code-point, and
grapheme mapping layer is tracked separately for Phase 2.

## Composition lifecycle

Composition text is ordinary document text annotated with a composition range.
Repeated composition updates replace the active composition range. Committing
removes the annotation and keeps the composed text. Cancelling restores the
pre-composition replacement and selection when the editor has a saved
composition baseline; metadata-only composition ranges are simply cleared.

This keeps rendering independent of platform-specific underline or clause
styles while giving each backend the same edit protocol.
