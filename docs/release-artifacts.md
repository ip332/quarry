# Release artifacts

Quarry keeps the release asset list explicit in
[`release/artifact-manifest.json`](../release/artifact-manifest.json). The
manifest describes the assets intended for a public release; it does not
publish, sign, or checksum them.

## Build and validate a release staging directory

From a clean tracked checkout, build the complete non-publishing bundle:

```sh
release/build-artifacts.sh \
  --tag v0.1.7-rc.1 \
  --release-notes docs/release-notes-v0.1.7-rc.1.md \
  --output /tmp/quarry-release
```

The builder creates deterministic source tar/zip archives with the packaged
version fallback, builds the Python wheel and sdist, stages release notes,
validates the manifest, and writes `SHA256SUMS`. It never creates a tag,
publishes an asset, or uploads to PyPI.

## Validate a release staging directory

Place the finalized assets in one directory, then run:

```sh
python3 tools/validate_release_artifacts.py \
  --root /path/to/release-staging \
  --version 0.1.7 \
  --tag v0.1.7-rc.1
```

The validator requires the GitHub source archive (`.tar.gz` and `.zip`), the
Python wheel and source distribution, and the release notes. It validates the
Python package metadata and rejects development-only files such as `REPORT.md`,
`.coverage`, `.git`, and `*.egg-info` from source archives. Compiler,
translator, and benchmark assets are described as optional because the current
release process does not publish them automatically. The benchmark manifest
entry reserves this future-release category, while CI currently publishes
`quarry-benchmark-results-<commit>` as a GitHub Actions artifact on `main`.

The staging directory is intentionally separate from the repository checkout.
This prevents ignored or untracked developer files from becoming release
assets. The GitHub-generated source archives and manually built Python
artifacts should be copied into the staging directory before validation.

The manifest is a release checklist, not a publication workflow. Checksums,
signatures, SBOMs, provenance attestations, and GitHub upload automation remain
deferred. A future release workflow may package the same generated bundle as
`quarry-<version>-benchmarks.zip`; adding that upload is deferred until the
release workflow is automated so the RC release process and existing assets
remain unchanged.
