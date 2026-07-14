# Imports

Owns the legacy declaration-syntax import boundary and compilation-unit
construction.

Responsibilities:

* preserve already-parsed AST documents as a lightweight compilation unit
* preserve import declarations as source syntax on the legacy AST path
* provide the current placeholder boundary for future multi-file import work

Allowed dependencies:

* `compiler/ast`
* `compiler/diagnostics`
* `compiler/support`

Current implementation status:

* `ImportResolver` currently returns the input AST documents in order as a
  compilation unit wrapper
* it does not load files, parse files, canonicalize paths, build a graph, or
  detect missing, duplicate, or cyclic imports yet
* imports remain AST-owned until a dedicated import-graph design is added
