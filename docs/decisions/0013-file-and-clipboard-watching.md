# ADR 0013: File and clipboard change observation

## Status

Accepted. File and clipboard observation are independent optional capability
families. Their handles use the common generation-checked registry and their
notifications use the central NativeKit event queue.

## Decisions

- File-watch paths are absolute UTF-8 native filesystem paths. `nk_resource`
  URIs are deliberately rejected because a URI is not guaranteed to identify a
  watchable local inode.
- A file event carries added, removed, modified, or moved state, the item kind,
  the new path, and an old path when the backend can pair a rename. Rename
  pairing is best effort and is never a guarantee that a complete history was
  observed.
- Native queue overflow produces `NK_EVENT_FILE_WATCH_OVERFLOW` with the
  rescan flag. Applications must rescan all watched directories after that
  event rather than infer missing individual changes.
- Watcher backends copy events into NativeKit's queue. No native callback calls
  application code. Destroy and runtime shutdown stop the backend source and
  wait for worker/listener cleanup before the event queue is destroyed.
- Clipboard events contain only a process-local change sequence, known format
  flags, and an unknown-source flag. They never contain text, files, or other
  clipboard data; applications use the existing asynchronous read APIs after a
  notification.
- Starting a clipboard watch records a baseline and emits no initial event.
  NativeKit writes are allowed to produce events, and repeated changes may be
  coalesced to one pending event per watch.
- The target desktop contract treats Linux and Windows as full capability
  families. This milestone implements Linux; Windows and macOS backends remain
  deferred until their native sources are added. macOS clipboard notifications
  and mobile listeners/polling are advisory and may be delayed while an app is
  suspended. Mobile file access is sandbox-only. Web reports both families
  unsupported.

## Capability reporting

`NK_CAP_FILE_WATCH` and `NK_CAP_CLIPBOARD_WATCH` are advertised only when the
selected backend provides the corresponding implementation. Symbol presence
does not imply capability; unsupported builds keep the ABI symbols and return
`NK_ERROR_UNSUPPORTED`.
