# SDK

This directory is reserved for future language-specific SDK packaging.

The current installed SDK surface is the header-only C++ runtime package
exported as `Breadcrumbs::runtime`. That package lives in `runtime/` because it
is generic binary-record support consumed by generated C++ artifacts rather
than a language-specific SDK directory. See `docs/distribution-model.md` for
the supported downstream distribution boundary.
