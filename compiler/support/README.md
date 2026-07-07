# Support

Owns shared compiler infrastructure.

Responsibilities:

* source locations and ranges
* source manager
* file system abstraction
* compiler context
* shared infrastructure utilities

## SourceLocation

`SourceLocation` is a compact value type. It stores only a `SourceFileId`
assigned by `SourceManager` and a byte offset into that source buffer.

It does not own source text, paths, line text, diagnostics, or compiler pass
state. It does not store line or column numbers. Line and column values are
derived by `SourceManager` when needed.

Invalid or unknown locations are represented explicitly by an invalid
`SourceFileId`.

## SourceRange

`SourceRange` is a compact value type representing a half-open byte range:

```text
[begin, end)
```

Both endpoints are `SourceLocation` values. A valid range must have valid
endpoints in the same source file and `begin <= end`. Empty ranges are valid
when both endpoints are equal. Invalid or unknown ranges are represented
explicitly by invalid endpoints.

`SourceRange` does not own source text, paths, diagnostics, or compiler pass
state.

## SourceManager

`SourceManager` owns source buffers for one compilation. It assigns stable,
graph-local `SourceFileId` values, associates each buffer with a canonicalized
or otherwise normalized path supplied by callers, and provides lookup of:

* source text by `SourceFileId`
* source path by `SourceFileId`
* line and column for a `SourceLocation`
* source text slices for valid `SourceRange` values

Source buffers are immutable after registration. `SourceManager` does not read
from the filesystem and has no global state.

Line and column numbering are one-based. Offsets are byte offsets. The manager
caches line start offsets so line and column lookup does not rescan the entire
file for each query. Empty files, final lines without trailing newlines, and
CRLF input are handled as byte-oriented source text. Unicode display width is
not computed.

Invalid `SourceFileId` values and out-of-range byte offsets are reported with
empty optional results or false validation helpers.

## FileSystem

`FileSystem` is the minimal abstraction needed by later import resolution. It
can:

* read a source file
* test whether a path exists
* normalize a path

`RealFileSystem` implements this interface with the C++ standard library.
Import search paths, import resolution, caching, virtual include roots, and
overlays are intentionally outside this layer.

## CompilerContext

`CompilerContext` is a shell for shared compiler infrastructure. For now it
owns a `SourceManager` and a `FileSystem` service.

It must remain infrastructure rather than semantic state. Do not add diagnostic
engines, semantic models, layout state, identifier allocation state, or
compatibility policy here until the relevant compiler layer requires them and
the ownership boundary is explicit.

Allowed dependencies:

* C++ standard library

Support may not depend on compiler passes or diagnostics.
