# Tools

This directory contains source-tree tools built from the compiler libraries.
They are not installed or exported as a package.

The schema compiler's downstream distribution decision is documented in
`docs/schema-compiler-tool-distribution.md`. The current supported boundary is
build-tree execution only; installing the executable or exposing a CMake
imported executable target remains deferred.

## breadcrumbs-schema-compiler

`breadcrumbs-schema-compiler` is the first end-to-end schema compiler command.
It compiles one YAML `.brd` source file through the production YAML frontend,
generates C++ backend files, and writes those generated files to disk.

```text
breadcrumbs-schema-compiler [options] INPUT

Options:
  -o, --output-directory PATH  Directory for generated files (default: generated)
      --root-file-stem NAME     Root namespace file stem (default: schema)
      --file-extension EXT      Generated file extension (default: .generated.hpp)
  -h, --help                    Show help
```

The command prints compiler diagnostics and tool errors to stderr. Successful
compilation is quiet and returns exit code `0`.

Exit codes:

* `0`: success or help
* `1`: input read failure, compiler diagnostics, backend failure, or output
  write failure
* `2`: command-line usage error

The command supports exactly one input file. Import resolution, multiple input
files, stale-output deletion, response files, configuration files, installed
package integration, color diagnostics, and JSON diagnostics are intentionally
out of scope.

That input file contains one YAML document and one source schema unit. The
current schema unit has one dotted namespace path and one primary record, with
zero or more fields and zero or more enum declarations in that namespace.
Multiple primary records, multiple namespace roots, YAML document streams, and
imports remain unsupported. With the current YAML contract, successful
compilation normally produces one generated namespace file; richer multi-file
backend output is exercised through lower-level Schema IR inputs.

Generated files are written from backend-provided in-memory `GeneratedFile`
values. Compiler and backend failures do not write output files. Output writes
create parent directories as needed and write each file through a temporary
sibling path before replacing the target file.

Generated-output consistency is file-scoped, not invocation-scoped. Before any
file is written, the tool checks all backend-provided generated paths for
lexical containment under the selected output directory and duplicate
normalized paths. After that preflight, files are committed one at a time in
backend order. If a later file write or rename fails, earlier files from the
same invocation may already have been replaced.

The tool preserves unrelated files in the output directory and does not delete
stale generated files. It does not provide a manifest, a rollback transaction,
concurrent writer coordination, or symlink sandboxing.
