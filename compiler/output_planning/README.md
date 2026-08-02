# Output Planning

The output-planning layer builds the language-neutral generation graph after
source-unit loading and semantic analysis.

Each planned unit records its source identity, canonical path, namespace,
root/output status, logical output key, and imported-unit dependencies. Units
are emitted in the deterministic dependency-first order established by
`CompilerContext`. Repeated dependency edges are deduplicated.

The current compiler contract has one explicit root input. The root is the only
unit marked as emitting an output; transitive imports are retained as
dependency nodes and do not create standalone backend files yet. This keeps
single-source output behavior unchanged while exposing the graph required by
The C++ backend now consumes this metadata for cross-namespace generated
header dependencies. Because the CLI still has one explicit generation root,
dependency headers are made available by generating imported source units as
separate explicit roots in the same output directory. C and Python backend
dependency emission remains future work.

Logical output keys detect collisions between generating roots before backend
rendering. Backend-specific extensions, include paths, Python package files,
and concrete artifact rendering remain backend responsibilities.
