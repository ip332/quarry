# Tools

This directory contains source-tree tools built from the compiler libraries.
They are not installed or exported as a package.

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

Generated files are written from backend-provided in-memory `GeneratedFile`
values. Compiler and backend failures do not write output files. Output writes
create parent directories as needed and write each file through a temporary
sibling path before replacing the target file.
