# ADR 0016: Windows IME transaction adapter

Status: accepted for the text-editing-quality program.

## Decision

The Windows IMM32 backend translates composition messages into the shared
`nk::core::TextEditTransaction` model before emitting an
`NK_EVENT_TEXT_EDIT` event. Keyboard composition, committed result text,
deletion, and composition termination therefore publish the same atomic
replacement-plus-resulting-state shape.

IMM32 reports composition cursor positions in UTF-16 code units while
NativeKit positions are Unicode code-point offsets. The adapter converts those
positions explicitly and rejects offsets inside surrogate pairs or malformed
UTF-16 sequences. Committed IME text is converted to UTF-8 and validated
before it enters the transaction path.

## Consequences

- Japanese IME replacement uses the active composition range, not the stale
  selection range.
- Composition updates retain the IME's cursor position in code-point units.
- IME commit and cancel/finish events clear composition metadata atomically
  with the emitted transaction.
- The platform keeps ownership of the IMM32 session and candidate-window
  positioning; the editor remains the document authority.
