# Development Environment

Breadcrumbs provides a Docker-based development environment so contributors
can build against the same toolchain CI uses. CI (`.github/workflows/ci.yml`)
builds this repository's `Dockerfile` and runs every configure/build/test step
with `docker compose run`, rather than installing dependencies directly onto
the runner.

## Support policy

| Build | Status |
| --- | --- |
| Docker `debug` | **Authoritative, CI-equivalent.** This is what CI runs. |
| Docker `debug-clang-tidy` | **Authoritative, CI-equivalent.** This is what CI runs. |
| Native `debug` | **Supported, host-dependent.** Works with the dependencies below installed directly; not guaranteed identical to CI's toolchain versions. |
| Native `debug-clang-tidy` | **Best-effort.** Available, but Breadcrumbs does not verify or enforce that a native compiler, `protoc`, Protobuf headers, and `clang-tidy` come from a coherent toolchain installation. A native-only clang-tidy include-resolution failure is not treated as a project build defect unless it also reproduces in Docker/CI. |

Native `debug-clang-tidy` failures that don't reproduce in Docker are, by
policy, host toolchain-pairing issues, not Breadcrumbs bugs — see
"Troubleshooting a native-only clang-tidy failure" below.

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

The presets in `CMakePresets.json` work on any host with the dependencies
above installed directly; see the `Dockerfile` for the authoritative package
list. Native `debug` is supported. Native `debug-clang-tidy` is best-effort
per the support policy above.

## Troubleshooting a native-only clang-tidy failure

1. Reproduce inside Docker first:

   ```sh
   docker compose build
   docker compose run --rm dev cmake --preset debug-clang-tidy
   docker compose run --rm dev cmake --build --preset debug-clang-tidy --parallel
   ```

2. If Docker passes, the failure is specific to your host — inspect your host
   toolchain pairing rather than the CMake build.
3. Verify the paths and versions of the configured C++ compiler, `clang-tidy`,
   `protoc`, and the Protobuf headers your compiler resolves at build time.
   A mismatch between where your compiler's implicit include search finds
   Protobuf headers and where your `clang-tidy` binary's implicit search
   looks is the most common cause (see background below).
4. Do not modify Breadcrumbs CMake files merely to mask a host-only implicit
   include-path mismatch — add an explicit include path, disable a warning,
   or exclude a generated source. If Docker/CI is unaffected, the fix belongs
   in your host environment, not the repository.

## Generated-code lint policy

Generated Protobuf sources (`schema_ir.pb.cc`/`schema_ir.pb.h`) are linted by
clang-tidy the same as handwritten sources; `compiler/CMakeLists.txt` does not
set `SKIP_LINTING` on them. (Only `yaml/yaml_parser.cpp`, a libyaml C-API
wrapper, carries `SKIP_LINTING`, for unrelated reasons.) In the authoritative
Docker/CI environment, `debug-clang-tidy` lints generated Protobuf code
cleanly — stylistic warnings only, no errors, since `.clang-tidy`'s
`WarningsAsErrors` is empty.

## Background: why native clang-tidy can diverge from Docker/CI

A `google/protobuf/runtime_version.h` file-not-found error compiling generated
`schema_ir.pb.cc` under `debug-clang-tidy` was previously seen on one native
macOS setup and mistaken for a project infrastructure blocker. It does not
reproduce in Docker or CI. Two conditions, confirmed by comparing the
reference environment (Ubuntu 24.04, apt Protobuf 3.21.12, apt
`clang-tidy-18`) against the failing native setup (Homebrew Protobuf 35.1,
Apple Clang as the configured compiler, a separately installed Homebrew LLVM
`clang-tidy`), both have to hold:

1. **A Protobuf generator new enough to emit the guard.** `runtime_version.h`
   is generated only by newer `protoc` versions. Ubuntu's apt Protobuf
   (matching Docker/CI) doesn't emit it, so the failure mode cannot occur
   there regardless of toolchain pairing.
2. **A compiler and clang-tidy binary with different implicit include search
   paths.** CMake omits an explicit `-isystem` flag for `Protobuf_INCLUDE_DIRS`
   when it's already implicit for the *configured compiler* — correct, and
   harmless when the same binary (or a coherently paired one) also runs
   clang-tidy. It becomes a problem only when `CMAKE_CXX_CLANG_TIDY` invokes a
   separately installed `clang-tidy` binary whose own implicit search path
   doesn't include the directory the configured compiler resolved implicitly
   (e.g. a Homebrew LLVM `clang-tidy` next to Apple Clang as the compiler).
   Docker/CI use one coherent apt-installed toolchain for both roles, so this
   divergence cannot occur there.

Both conditions are host toolchain-installation choices, not project build
configuration. No CMake, `.clang-tidy`, or generated-code change fixes them
without either overfitting one host's layout or masking the actual
divergence — so none was made. `.github/workflows/ci.yml`'s `clang-tidy` job
runs `debug-clang-tidy` in Docker on every push/PR as the durable regression
check for the authoritative environment.
