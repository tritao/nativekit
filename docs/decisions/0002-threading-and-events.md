# ADR 0002: Threading and events

The thread that successfully calls `nk_init` is the UI thread. Window, dialog,
WebView, and event-polling APIs must run on that thread unless an API is explicitly
documented otherwise.

Native completions cross the ABI as values in a FIFO event queue. Variable-length
event data is owned by NativeKit and must be released with `nk_event_release`.
Asynchronous operations correlate requests and completions with 64-bit request IDs.

