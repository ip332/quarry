# SDK

This directory is reserved for future language-specific SDK packaging.

The initial runtime serialization implementation lives in `runtime/` instead of
`sdk/` because it is generic binary-record support consumed by generated C++
artifacts, not an installed or language-packaged SDK surface.
