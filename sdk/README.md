# SDK

This directory is reserved for future language-specific SDK packaging.

The current installed SDK surface is the header-only C++ runtime package
exported as `Breadcrumbs::runtime` and the schema compiler executable target
exported as `Breadcrumbs::schema_compiler`. The runtime lives in `runtime/`
because it is generic binary-record support consumed by generated C++
artifacts; the compiler tool lives in `tools/`. See
`docs/distribution-model.md` for the supported downstream distribution
boundary.
