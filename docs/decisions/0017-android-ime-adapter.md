# Android IME adapter uses a UTF-16 mirror and normalized transactions

## Status

Accepted

## Decision

NativeKit's Android surface remains a custom `SurfaceView` text editor. Its
`InputConnection` exposes a small `SpannableStringBuilder` mirror so Android
keyboards can query surrounding text, selection, extracted text, and composing
spans. The mirror is synchronized from `nk_text_input_state`; it is not the
document authority.

Every mutating callback reports one `NK_EVENT_TEXT_EDIT` transaction. Android
UTF-16 offsets are converted to the public absolute code-point positions before
crossing JNI. Deletion ranges are expanded to scalar boundaries when a keyboard
reports a length that lands inside a surrogate pair. The native JNI boundary
rejects malformed UTF-16, invalid transactions, and unsupported replacement
payloads.

An empty `setComposingText` update that removes the composing span is normalized
to an empty commit transaction. This represents Android's clear-composition
behavior without sending a composition transaction that has no composition
range.

## Consequences

Skribidi/NativeKit continue to own text, selection, composition state, and
rendered geometry. Android owns only IME lifecycle and the platform-facing
`InputConnection` protocol. The mirror is limited to the text window published
by NativeKit, so future surrounding-text windowing must retain the same offset
contract rather than promoting the mirror into a second document model.

The pure Java offset mapper is covered by JVM tests for astral characters,
malformed UTF-16, exact boundaries, and scalar-safe deletion.
