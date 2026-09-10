# API documentation audit

Run the audit from the repository root:

```sh
./scripts/audit-api-docs.py --only-missing
```

The C inventory is produced with libclang. It covers public functions, typedefs,
enum constants, public constant macros, and—when requested—struct fields:

```sh
./scripts/audit-api-docs.py --fields --format markdown
./scripts/audit-api-docs.py --format json > /tmp/nativekit-api-audit.json
```

Generated HXI files are parsed separately and matched to the C inventory. Their
documentation status is inherited from the matching C declaration, so the HXI
files must not be edited to resolve a documentation finding. A missing HXI
entry with a source location points back to the C header that needs the comment.

The scanner accepts both libclang's `raw_comment` documentation and the
repository's existing adjacent `/* ... */` and `// ...` comments. It reports
whether a comment is present; it does not assess the quality or completeness of
that comment.

Requirements are Python 3, the Python `clang` bindings, libclang, and a Clang
resource directory. The resource directory is discovered with
`clang -print-resource-dir`. Override discovery when necessary:

```sh
./scripts/audit-api-docs.py \
  --clang-library /path/to/libclang.so \
  --clang-resource-dir /path/to/clang/resource
```

`CLANG_LIBRARY_FILE` and `CLANG_RESOURCE_DIR` provide the same overrides for
automated environments. `--fail-on-missing` changes the exit status to 1 when
any C or HXI declaration lacks documentation; parser or dependency failures
exit with status 2.
