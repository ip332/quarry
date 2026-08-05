# Quarry versioning

Quarry's development line is recorded in the tracked root file `git_version`.
It contains exactly `Major.Minor`, currently `0.1`, followed by a newline.

The numeric version is derived from Git history:

```text
Major.Minor = git_version
Revision    = commits after the last commit that changed git_version
```

The commit that introduces or changes `git_version` is revision `0`. The next
commit is revision `1`, and changing `0.1` to `0.2` starts `0.2.0`.

The CMake resolver in `cmake/QuarryVersion.cmake` is authoritative. It exposes
the numeric version to the CMake package, compiler, translator, and generated
version header. The human-readable CLI display appends `-dirty` when tracked
files differ from `HEAD`; untracked files are intentionally ignored. Numeric
package versions never contain the dirty suffix.

Complete Git history is required. Shallow checkouts fail clearly and should be
repaired with:

```sh
git fetch --unshallow
```

For a source archive without `.git`, the tracked
`cmake/QuarryResolvedVersion.cmake` template receives the release tag and Git
identity through Git's `export-subst` archive mechanism. A release tag such as
`vX.Y.Z-rc.N` supplies the numeric `X.Y.Z` fallback; the tag's Major.Minor must
match `git_version`. The same file is also generated beside normal build
artifacts for packaging. These fallbacks are used only when full Git history is
unavailable. Ordinary Git builds do not use them and cannot silently fall back
to an unknown version. An untagged or malformed archive fails with a clear
diagnostic.

Release tags must point to a clean commit whose numeric version matches the
tag's numeric portion, for example `vX.Y.Z-rc.N` for resolved version `X.Y.Z`.
Release creation must reject tracked modifications.

Release versioning is independent from compatibility contracts: C++ generated
code epoch `3`, C generated code epoch `2`, Python generated code epoch `1`,
Schema IR version `1`, and BRF header version `1` do not derive from this
version.

To build the native compiler with fresh version metadata and display its
version, run:

```sh
./tools/sem_version.sh
```

The wrapper configures the supported `debug` preset before building the
compiler, so an old build directory cannot silently retain version metadata
from an earlier checkout. Directly invoking an existing binary does not
reconfigure CMake; use the wrapper after changing Git state or switching
commits.
