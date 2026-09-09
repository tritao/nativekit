# ADR 0004: wxWidgets boundary

wxWidgets may initially implement private platform backends, pinned to an exact
upstream commit. NativeKit core code and public headers do not use wxWidgets types.
Direct native backends will replace wx-backed subsystems independently behind the
same internal contract and public conformance suite.

