# A5 black-box interoperability matrix

`ctest --preset debug -R '^interop_matrix$' --output-on-failure` runs the
five-by-five producer/consumer grid. Each producer writes real binary files;
every consumer verifies populated/default fields and re-encodes to the identical
bytes. The adapters are rendered from one explicit expected-value model, using
only generated binding APIs. No compiler layout or identity implementation is
used as an oracle.

The authoritative root is `examples/truck/schemas/configuration.brd`. Its eight
records cover signed/unsigned integers, boolean values, bounded strings, fixed
nested records, variable nested records, and two-element record arrays with
distinct values. Empty/default records cover absence independently. GearState → EngineStatus → Timestamp and VehicleSnapshot →
GearState → EngineStatus exercise depth two; arrays add deeper paths. Every
actual nested span, including every record-array element, is extracted using
the public C generic view, compared with a standalone encoding, then exchanged
through the same full grid. The companion `enum.brd` fixture adds enum coverage
without changing the truck root or its pinned schema identity.

C99 and C++20 adapters compile with `-Wall -Wextra -Wpedantic -Wshadow -Werror`.
C-storage encode/decode calls go through the C++ wrapper. Python and Go use
their public generated codecs. Go builds without network/toolchain downloads.
A missing Go toolchain yields visible SKIP cells; those cells are never passes.
`-DQUARRY_INTEROP_REQUIRE_ALL=ON` makes missing toolchains fatal in Linux CI.
Python is the orchestration host, and the source Python runtime is mandatory
when this CTest is registered.

The C generic QBS/BRF API checks every populated producer while extracting
nested spans. For the representative C producer it checks populated/default
records in both QBS modes; the inspector checks the same cases and must expose
the expected RPM, temperature and fault values. Every other producer's bytes
must match these streams. Python generic checks the complete populated/default
model for every producer in both modes. A separate C++ QBS tooling probe checks
EngineStatus, timestamps and its two fault records for every producer in both
modes. Repeated CLI launches on identical bytes are avoided.
The C++
probe links the existing QBS parser/tooling library; it does not suggest that
this source-tree tooling API is an independently installed header-only runtime.
The driver stages an actual CMake installation and uses its CLI tools and
public runtime headers for all native generated adapters, generic C probes and
negative compilation controls. The library comes from the selected build, so
the extended run still exercises a Clang-built C runtime. The separate C++ QBS
tooling probe retains its existing source-tree API boundary.

The driver compares the complete stored 128-bit truck schema_id from each
binding, CLI queries and both QBS images. Dependency replacement tests reject
incompatible configurations in all five languages. A separate wire-identical
field rename tests native gid guards. Python and Go do not expose the native
per-namespace gid mechanism; this distinction is reported as N/A, not a pass.
The existing `public_examples_test` runs the A4.5 truck revision A/B coexistence
application in all five variants, including EngineStatus's shared ID and rev
B's additional MirrorFold record. It is reused rather than duplicated here.

Legacy controls replace each expected type's ID with its RC3 local component
below 0x00010000 and require rejection by every available generated consumer
and the inspector. These are mapping-specific checks, not an assertion that
all small numeric IDs are intrinsically invalid. Positive controls use the
unmodified streams in the full grid.

Mutation controls route an altered header ID, valid differently populated bytes,
and a valid record delivered to the wrong type through the same consumer
entry points and value checks. The changed-value and misrouted streams must also
pass with their correct values/type, proving that the intended assertions fail.
Truncation, trailing bytes, invalid UTF-8, unknown enums and a legacy nested ID
are rejected by generated decoders, generic C/Python and both inspector modes.
Rejections require the expected normal error exit (including inspector exit 6);
signal exits and runtime-fault diagnostics cannot count as success. A fourth
control changes the last nibble of the observed schema_id and requires the same
full-width identity assertion to fail. The separate dependency
mixing checks must fail on the generated configuration guard's diagnostics,
not on missing includes or unrelated application errors.

Repeated generation, a new output directory, and selected/full generation must
agree on shared source bytes. Reordered root imports, selection and prefixing
must preserve the pinned IDs, schema_id and both complete QBS byte streams.
Source-byte equality after import reordering is deliberately not required
(issue #90). Every populated/default producer fixture is also encoded through prefixed APIs
and compared byte for byte with its original stream. Existing `name_prefix_test`
retains additional native storage-policy coverage.

Evidence is written to `<build>/interop-matrix/report.json` and retained
`run-*/commands.log` files. Reports include each fixture, producer, consumer,
expected/observed ID, byte comparison and decode result, toolchain versions,
checks, mutation results and elapsed wall time. Linux CI uploads these and
executes all five rows. Its additional Clang 18 run builds the C runtime and
C++ QBS probe with Clang 18, then compiles all native generated adapters with
that toolchain. The existing GCC-built compiler and inspector supply the same
black-box CLI boundary. This is a targeted matrix run, not a claim that the
unrelated full Clang 18 conformance suite (#85) passes.

No BRF/QBS/IR, record allocation, identity encoding, runtime API or epoch changes
are part of this test stage. The experimental FQN-hash branch is untouched.
