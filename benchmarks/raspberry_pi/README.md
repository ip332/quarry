# Raspberry Pi 3 / minimal Yocto image

This runner cross-builds the existing public API benchmark programs as static
ARMv7 hard-float Linux executables and runs them over SSH. It needs no compiler,
Python, package manager, or Protobuf installation on the target. Everything is
deployed to a unique `/tmp/quarry-perf-*` directory; the OS is not modified.

The comparison includes Quarry C (default presence), owning Quarry C++, a C++
wrapper over C storage, Protobuf C++, and Protobuf C++ Arena, over all five existing datasets and encode/decode/round-trip
operations. It does not include compact C presence, Nanopb, or Python codecs.
The existing programs' allocation and buffer policies apply; these are public
API measurements, not an allocation-free codec-only comparison.

Build the normal **host** schema compiler first, then:

```sh
docker build -t quarry-pi-armhf:noble benchmarks/raspberry_pi
python3 benchmarks/raspberry_pi/run.py \
  --host root@100.81.6.20 \
  --compiler build/benchmark/tools/quarry-schema-compiler \
  --version-include build/benchmark/generated \
  --build-dir /tmp/quarry-pi-armhf \
  --output-dir /tmp/quarry-pi-smoke --smoke --runs 1
```

Use the compiler and generated version headers from the same Quarry checkout.
The target must be ARMv7 Linux with hard-float support (the Pi 3's 32-bit Yocto
Kirkstone image is supported). SSH access and a previously verified host key
must already be configured. The runner uses batch authentication, never stores
passwords, and does not change SSH configuration.

For measured runs, remove `--smoke` and choose a new output directory. Defaults
are three repetitions, five timing samples, ten measured iterations, and two
warmups with the full datasets. Each repetition writes 75 JSON results and
captures CPU, memory, governor, frequency, and temperature before/after. Results
are pinned to CPU 2 through a bundled `sched_setaffinity` launcher; select a
permitted CPU using `--cpu`. Governor and clock settings are recorded, not changed.
Use an idle, cooled board and inspect the system snapshots before comparing.

The manifest records source/generated/dataset/binary hashes, Quarry revision,
tracked changes, host schema compiler version, cross-toolchain flags, Protobuf
version, and the Docker image ID. Target results are validated for matching
logical cases and encoded-size parity within each wire format. `/tmp` output
is volatile: the runner retrieves each JSON result immediately and retains the
remote bundle for inspection until it is removed or the board reboots.

The toolchain packages are installed from Ubuntu 24.04 repositories. The image
ID and actual package toolchain version are captured; the Dockerfile does not
promise identical package versions across future rebuilds. Static libc and
Protobuf are included in executable sizes, so those sizes are not equivalent
to dynamically linked native benchmark footprints.

After a complete measured run, generate the comparison table:

```sh
python3 benchmarks/raspberry_pi/summarize.py /path/to/measured-output
```

This writes `report.md` and `summary.json`, aggregating the median of the run
medians while preserving the per-run minimum/maximum in JSON. Encoded byte sizes
refer to the **first record** of each dataset, as reported by the underlying
harness; they are not averages over all records.

The C-storage wrapper benchmark reuses the C harness loader, buffers, and timing
boundaries; timed calls use the wrapper native-storage overloads. This avoids
copying between different record representations in the timed region.
