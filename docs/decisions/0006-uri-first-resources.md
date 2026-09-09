# ADR 0006: URI-first resources and sharing

NativeKit distinguishes local filesystem paths from portable resource
identifiers. A path names an entry in the process-visible filesystem. A
resource URI names content that may instead be owned by a document provider,
be remote, or require a platform grant. Neither representation is silently
converted to the other.

## Public model

The resource API uses `nk_resource`, an extensible input structure containing
an absolute UTF-8 URI plus optional MIME type and display name. The URI is the
resource identity. `file:` is the portable representation of a local file in
this API; platform-native schemes such as Android `content:` remain intact.
Callers must not assume that a resource URI can be opened with filesystem I/O.

Resource results use one packed event representation and one decoder across
dialogs, clipboard reads, drops, and future resource-producing APIs. Each item
contains its URI, optional MIME type and display name, and flags describing
known access properties. Returned string views are owned by the event.

Existing APIs whose names or fields say `path` remain local-path-only. In
particular, `nk_shell_open_file`, `nk_shell_reveal_file`,
`nk_clipboard_set_files`, and `nk_clipboard_read_files` never accept or return
`content:` URIs. Backends return `NK_ERROR_UNSUPPORTED` when a requested path
operation cannot be represented honestly. They do not copy provider content
to a temporary file as an implicit conversion.

New resource entry points cover the non-path cases:

- `nk_shell_open_resource` asks the platform to view one URI.
- `nk_share` submits optional text and zero or more resource URIs to the system
  share UI.
- `nk_clipboard_set_resources` and `nk_clipboard_read_resources` exchange URI
  items without reducing them to paths.
- Resource variants of open, save, and directory dialogs return URI items.

Clipboard reads and dialogs stay asynchronous and correlate their completion
events with request IDs. Sharing and shell opening report whether the request
was handed to the platform synchronously. They do not claim that another
application consumed the content: Android and several desktops provide no
reliable, portable completion signal for that action.

## Access and lifetime

NativeKit copies input descriptors before returning. It validates URI syntax
and item counts but does not dereference content merely to validate it.
Optional MIME types are hints; a receiving platform may resolve or replace
them. A mixed-type share uses the closest platform representation and must not
mislabel all items as one caller-provided type.

A URI does not itself confer access. NativeKit forwards temporary read or write
grants supplied by the platform and requests persistable access for document
dialog results when the provider offers it. Whether that succeeds is exposed
in resource flags. Applications must retain the URI, not a synthesized path,
and must tolerate a later access failure if a provider revokes its grant.

On Android, sharing uses `ACTION_SEND` or `ACTION_SEND_MULTIPLE`, `ClipData`,
and `FLAG_GRANT_READ_URI_PERMISSION`. Viewing uses `ACTION_VIEW` with matching
grant flags. File and directory selection uses the Storage Access Framework
and returns the original `content:` URI. NativeKit does not emit `file:` URIs
to other Android applications, because modern Android rejects that exposure
and a correct conversion requires an application-owned `FileProvider` policy.

Desktop backends may convert a confirmed local selection to an RFC 8089
`file:` URI for the resource APIs. Conversion is confined to that explicit
URI contract; the existing path APIs continue to expose native local paths.

## Compatibility

The first resource API is additive to ABI version 1. A dedicated capability
bit lets callers distinguish resource sharing from the older shell and
clipboard capabilities. Existing path entry points keep their signatures and
semantics. Android's current use of path-labelled dialog results for
`content:` values is transitional and is replaced by the resource dialog
variants; new integrations should use only the resource variants there.
