# API documentation audit

The combined C-header and generated-binding documentation audit is maintained in
Haxeon under `packages/platform/tools/audit-api-docs.py`. Its usage guide is
`packages/platform/docs/api-doc-audit.md` in that repository.

From a NativeKit checkout with Haxeon alongside it:

```sh
NATIVEKIT_DIR="$PWD" ../haxeon/packages/platform/tools/audit-api-docs.py --only-missing
```

Public C headers remain the authoritative source for API documentation.
