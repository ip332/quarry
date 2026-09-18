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

The CMake resolver in `cmake/QuarryVersion.cmake` is authoritative for a normal
Git checkout. It exposes the numeric development version to the CMake package,
compiler, translator, and generated version header. This means that a current
development checkout can legitimately report a version such as `0.1.262`; it
does not need to numerically match a future release tag. The human-readable CLI
display appends `-dirty` when tracked files differ from `HEAD`; untracked files
are intentionally ignored. Numeric package versions never contain the dirty
suffix.

Complete Git history is required. Shallow checkouts fail clearly and should be
repaired with:

```sh
git fetch --unshallow
```

For a source archive without `.git`, the release tooling writes the explicit
release identity and Git identity into
`cmake/QuarryResolvedVersion.cmake`. A release tag such as `vX.Y.Z-rc.N`
therefore supplies the numeric `X.Y.Z` version to the reconstructed archive,
wheel, and sdist. Release candidates intentionally retain the numeric package
version `X.Y.Z`; the `-rc.N` suffix identifies the release tag and is not part
of the numeric package version.

Release tags identify release artifacts; they are not required to match the
commit-count version of the tagged development checkout. Release creation must
still use a clean tracked commit, and the tag's major/minor must match
`git_version`. Release verification must confirm that reconstructed archives
and packaged artifacts report the numeric version derived from the explicit
release tag. Ordinary Git builds do not use archive fallback metadata and
cannot silently fall back to an unknown version. An untagged or malformed
archive fails with a clear diagnostic.

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
