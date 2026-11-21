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

Release versioning is independent from compatibility contracts. Current generated
code API epochs are C++ owning `3`, C and C++/C-storage `7`, Python `1` and Go
`1`. Generated bindings target BRF v2; QBS and Schema IR are version `1`.
The wire `schema_id` algorithm and generated-code identity format are both `1`,
with separate meanings. None derives from the package version, and equal epochs
do not establish native ABI compatibility. See the
[consumer compatibility and migration guide](generated-binding-compatibility.md)
for the three mechanisms and regeneration requirements. #111 G1–G4 changes none
of these versions or epochs.

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

## Informational release metadata

`QUARRY_RELEASE_TAG` and the compiler's `--version` / `--describe` report a
separate release tag. In Git this is an exact HEAD tag, annotated or lightweight,
matching `vX.Y.Z` or `vX.Y.Z-rc.N` and the current `git_version` major/minor.
Ancestor tags, unrelated tags and tags for another development line report
`untagged`. The numeric commit counter and tracked-file `-dirty` semantics above
are unchanged: an exact `v0.1.31-rc.1` tag can accompany numeric version
`0.1.288`. A dirty checkout retains its HEAD tag as informational metadata;
release creation still requires a clean commit.

Archives use explicit `QUARRY_ARCHIVE_TAG` metadata alongside their existing
numeric fallback. A valid numeric fallback without a tag reports `untagged`;
package version strings never imply a release tag. The generated build fallback
preserves both fields when a tag exists. Missing or invalid numeric fallback
metadata remains an error without Git. When archive Git SHA metadata is absent,
the CLI explicitly reports `Git SHA unavailable`. None of these fields enters
schema identity, generated-code identity or generated bindings.
