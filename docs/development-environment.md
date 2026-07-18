# Development Environment

Breadcrumbs provides a Docker-based development environment so contributors
build against the same toolchain and library versions used by CI. CI
(`.github/workflows/ci.yml`) builds this same `Dockerfile` and runs its
configure/build/test steps with `docker compose run`, rather than installing
dependencies directly onto the runner — so the environment a contributor
builds in locally is, by construction, the same environment CI builds in.
Toolchain and dependency version skew between a contributor's host and CI has
previously caused build and `debug-clang-tidy` failures that did not
reproduce consistently; the Docker image exists to remove that variable.

## Build the development image

```sh
docker compose build
```

## Start a development shell

```sh
docker compose run --rm dev bash
```

The repository is bind-mounted at `/workspace` inside the container, so edits
made on the host are visible immediately inside the container, and build
output under `build/` is visible on the host.

## Building and testing inside the container

```sh
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
```

## Running clang-tidy inside the container

```sh
cmake --preset debug-clang-tidy
cmake --build --preset debug-clang-tidy --parallel
```

## Toolchain contents

The image (`Dockerfile`) installs:

* Ubuntu 24.04
* `protobuf-compiler`, `libprotobuf-dev`, `libabsl-dev`, `libyaml-dev` via
  `apt-get`
* `clang`, `clang-tidy`, `clang-format` for the `debug-clang-tidy` preset and
  the `.pre-commit-config.yaml` formatting hook
* `pre-commit`, installed with `pipx`

CI builds this image with `docker compose build` and runs every configure,
build, and test step inside it with `docker compose run`. Changing dependency
or toolchain versions for CI means changing the `Dockerfile`, not
`.github/workflows/ci.yml`.

## Native (non-Docker) builds

Docker is the recommended environment, but it is not required. The presets in
`CMakePresets.json` work on any host with the dependencies above installed
directly; see the `Dockerfile` for the authoritative package list.

Native builds are not guaranteed to reproduce CI's `debug-clang-tidy` result.
See "Why `debug-clang-tidy` could fail natively but not here" below before
debugging a local-only clang-tidy failure as a project bug.

## Generated-code lint policy

`compiler/CMakeLists.txt` does not set `SKIP_LINTING` (or an equivalent
source-file property) on the generated `schema_ir.pb.cc`/`schema_ir.pb.h`
Protobuf output. Generated Protobuf sources are linted by clang-tidy the same
as handwritten sources; only `yaml/yaml_parser.cpp` (a libyaml C-API wrapper)
carries `SKIP_LINTING`, for unrelated reasons. In the reference environment
(this Docker image, and CI) `debug-clang-tidy` lints generated Protobuf code
cleanly — it produces stylistic warnings (e.g.
`bugprone-reserved-identifier`, `readability-use-concise-preprocessor-directives`
on protoc-generated code) but no errors, since `.clang-tidy`'s
`WarningsAsErrors` is empty. There is currently no reason to exclude generated
Protobuf sources from analysis, and handwritten sources that include the
generated headers remain fully linted regardless.

## Why `debug-clang-tidy` could fail natively but not here

A `google/protobuf/runtime_version.h` file not found error compiling
generated `schema_ir.pb.cc` under `debug-clang-tidy` was previously seen and
treated as an infrastructure blocker. It does not reproduce in this Docker
image or in CI. Root cause, confirmed by running both the reference
(Ubuntu 24.04 + apt `protobuf-compiler`/`libprotobuf-dev` 3.21.12 + apt
`clang-tidy-18`) and a mismatched native macOS setup (Homebrew
`protobuf` 35.1 as the CXX toolchain's Protobuf, with a separately installed
Homebrew LLVM `clang-tidy` binary) side by side:

1. **Protobuf version skew.** `runtime_version.h` and its cross-version
   `#error` guard are a feature of newer Protobuf C++ generator output.
   Ubuntu 24.04's apt `protobuf-compiler` (3.21.12, matching CI and this
   image) does not generate an include of that header at all, so the failure
   mode cannot occur against that Protobuf version. It only appears when
   `protoc` is new enough to emit the guard (observed with Homebrew
   `protobuf` 35.1 on macOS).
2. **Toolchain incoherence, when the guard is present.** CMake's
   `target_include_directories(breadcrumbs_schema_ir_proto SYSTEM PUBLIC
   ${Protobuf_INCLUDE_DIRS})` (`compiler/CMakeLists.txt`) is correct — the
   Protobuf include directory reaches the target. CMake omits an explicit
   `-isystem` flag for it in the emitted compile command when it is already
   one of the *primary configured compiler's* implicit include directories
   (`CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES`). On a Homebrew macOS setup,
   Protobuf 35.1 headers live under `/usr/local/include`, which is an
   implicit search path for Xcode's `/usr/bin/c++` (Apple Clang) — so the
   normal compile succeeds with no explicit `-I`/`-isystem` needed. But
   `CMAKE_CXX_CLANG_TIDY` invokes a *different* `clang-tidy` binary (a
   separately installed Homebrew LLVM build) using that same omitted-include
   command line; that binary's own bundled Clang driver does not default to
   searching `/usr/local/include`, so it cannot find the header the real
   compiler found implicitly. In the reference environment there is a single
   coherent apt-installed toolchain (GCC as the configured compiler,
   `clang-tidy-18` as the tidy binary), both of which already search
   `/usr/include` — where Ubuntu's Protobuf headers live — by default, so
   the divergence never occurs.

No CMake, `.clang-tidy`, or generated-code change was needed to make the
authoritative (CI/Docker) build pass — it already passed once run against a
correctly paired Protobuf/compiler/clang-tidy toolchain. The fix is this
Docker image and CI's use of it (`.github/workflows/ci.yml`'s `clang-tidy`
job runs `debug-clang-tidy` on every push/PR as the regression check), which
removes the toolchain-pairing variable rather than adding an include path
that would mask it. Contributors who build natively outside Docker with a
heterogeneous compiler/clang-tidy pairing (most commonly: a package manager
that installs Protobuf and clang-tidy from different, uncoordinated sources)
may still see this class of failure; it reflects their local toolchain
pairing, not a defect in the CMake build.
