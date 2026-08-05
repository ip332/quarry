# Release artifacts

Quarry keeps the release asset list explicit in
[`release/artifact-manifest.json`](../release/artifact-manifest.json). The
manifest describes the assets intended for a public release; it does not
publish, sign, or checksum them.

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
release process does not publish them automatically.

The staging directory is intentionally separate from the repository checkout.
This prevents ignored or untracked developer files from becoming release
assets. The GitHub-generated source archives and manually built Python
artifacts should be copied into the staging directory before validation.

The manifest is a release checklist, not a publication workflow. Checksums,
signatures, SBOMs, provenance attestations, and GitHub upload automation remain
deferred.
