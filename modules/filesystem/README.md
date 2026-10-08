# NativeKit filesystem module

This optional module provides a root-scoped metadata and directory-listing
primitive for services that expose a filesystem over a separate protocol. It
does not open or transfer file contents.

The Linux backend pins an opened directory descriptor and resolves each
root-relative path with `openat2(RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS)`. It
therefore follows relative symlinks only when the kernel proves the result stays
beneath that root. Absolute symlink targets are conservatively rejected. The
Windows backend pins directory and file handles, follows reparse points only
when the resolved handle path remains under the granted root, and rejects
Windows alternate data stream paths. The module returns `NK_ERROR_UNSUPPORTED`
when the Linux kernel lacks secure `openat2` resolution; it does not fall back
to a weaker path walk. Other platforms currently compile a backend that
reports `NK_ERROR_UNSUPPORTED`.

Paths are canonical UTF-8, slash-separated and relative to the root. Empty path
addresses the root. Directory cursors retain a pending entry if the caller's
name buffer is too small. Native filenames that are not valid UTF-8 are returned
as explicit unsupported-name entries, without lossy replacement.

The returned file identity fields are local metadata, not authorization tokens
or protocol revisions. Callers must enforce workspace and client grants on each
operation, impose listing and cursor limits, and sort/page directory entries at
the service layer. Closing a root invalidates all cursors opened from it.
