# ADR 0018: Apple IME adapters use cached editor-window mappings

Status: accepted for the text-editing-quality program.

## Decision

iOS and macOS remain platform adapters over the NativeKit/Skribidi editor
state. UIKit's transparent text responder and AppKit's `NSTextInputClient`
provide the IME session, while every replacement, composition update, commit,
selection change, and deletion is reported through the normalized text-edit
event contract.

Each active text-input window owns a cached `TextOffsetMap`. It converts
editor UTF-8 byte and code-point positions to the UTF-16 ranges required by
UIKit/AppKit without rescanning the string for every IME query. Native ranges
that split a surrogate pair are rejected rather than silently rounded to a
different editor position.

Caret, selection, and composition rectangles continue to come from the
rendered editor geometry. The adapters only convert those rectangles into the
coordinate system required by the platform candidate and accessibility APIs.
