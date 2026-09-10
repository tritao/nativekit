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
generated Doxygen comments must be present and match the normalized C comment;
the HXI files must not be edited to resolve a documentation finding. Fix the C
header or the importer/projection when the generated comment is missing.

Only Doxygen comments (`/** ... */` and `/// ...`) count as API documentation.
Section headings and implementation notes may continue to use ordinary C
comments. The scanner reports presence and normalized equality; it does not
assess the quality or completeness of the prose.

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
