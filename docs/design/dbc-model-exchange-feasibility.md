# DBC model exchange: BRD/QBS/QTF feasibility study

Status: feasibility only. The J1939 DBC model exporter remains outside this study.
No full exporter,
BRF extension, transport change, install/export rule or new public SDK is included.
The QTF fixture is hand-authored from one DBC message, not exporter output.

## QTF documentation and parser changes

The QTF specification now describes whitespace, type-directed special float tokens,
and duplicate/unknown-field rejection in the present tense. Tests verify whitespace
including CRLF, float types, and field rejection against the implementation.

### Opt-in comments, strict v1 unchanged

The existing v1 specification explicitly rejected comments; the repository's
versioning/identity documents define package, BRF, QBS and generated-code epochs,
but do not authorize an implicit QTF v1 grammar amendment. The reviewed decision
is an additive, explicit input extension: `QtfParseLimits::allow_line_comments`,
default `false`. Strict QTF v1 remains comment-free. No new QTF version, BRF/QBS
version, schema identity algorithm or generated-code epoch is introduced.

With the flag, `#` to CR/LF/EOF is whitespace, including immediately after a value
or before array punctuation. It does not concatenate token fragments. A hash inside
quoted text is string data; bytes still require hex digits. No `//` or block
comments. Comments are discarded, consume input-byte limits, and do not count as
value tokens. Canonical export stays comment-free. Import/re-export does not
preserve comments; persistent units, descriptions and provenance belong in fields.
Older/default readers reject comment-bearing files, so interoperable producers
must emit canonical comment-free QTF unless the extension is explicitly negotiated.

Only the C++ QTF parser/importer/exporter was found in `include/`, `runtime/`,
`tools/` and generated backends. `import_qtf` already forwards the parse limits,
so no second implementation needed updating. C/Python/Go runtimes do not supply
another QTF parser. This does not claim cross-language QTF interoperability.
The internal C++ options struct changed: source-tree native consumers must rebuild;
there is no new native ABI promise.

### Separate infinity fix, verified before/after

`QtfParserTest.InfinityPreservesSelectedFloatTypeThroughImportAndDecode` failed
before the fix for both `f64: inf` and `f64: -inf`: the parser returned a float32
variant and import failed. `INFINITY` is float-typed in that branch. The independent
fix selects `numeric_limits<float>::infinity()` or
`numeric_limits<double>::infinity()` before constructing the variant. The test
now passes for both signs and both widths, checks variant types, imports QTF,
validates/decodes BRF, checks infinity/sign, and exports the spelling again.
`nan` and whitespace behavior have a separate regression test. This implements
the existing type-directed contract; it does not change the format.

Malformed-input checks also found two small parser defects:

* A trailing lexer failure could appear as clean EOF after a complete root. Parsing
  now rejects newly emitted lexer errors; an unterminated trailing string is tested.
* Enum membership used begin/end iterators from different temporary value vectors.
  The isolated `{mode:2}` test for an enum permitting only 0/1 failed before the fix.
  The parser now owns one value range for the membership check; valid 0/1 and invalid
  2 are covered. The study's malformed byte-order probe now rejects correctly.

These are separate correctness changes in the parser, not changes to intended QTF
semantics. Broader numeric grammar/locale hardening is not claimed; finite float
conversion still uses `strtof`/`strtod`. Exact DBC decimals in this study are strings.

## Reproducible model instance

Fixtures: `tests/fixtures/dbc_model_exchange/`. One primary record per BRD is
composed by a configuration root, matching the existing compiler architecture:
`DbcModel`, `Source`, `FrameDefinition`, `SignalDefinition`, and `EnumValue`.
The CLI compiles their import closure into reflective QBS. Bounds are deliberately
small study limits: two sources/messages, three signals per message, four enum
labels per signal, and bounded names/units/decimal strings. They are not proposed
production limits. Oversized data is rejected, never truncated.

`source.dbc` contains one classic 8-byte message, an unsigned mux switch and two
signed 12-bit alternatives: Intel and Motorola. `model.qtf` retains its bit
positions, widths, byte order, signedness, decimal tokens, units, SPN presence,
source lines, source SHA-256 and mux-switch references. The consumer does not open
or parse DBC. The Python probe independently verifies the fixture's source digest.

| Capability | Observed result |
| --- | --- |
| Message/signal collections | Nested record arrays compile to QBS and parse into the expected host model graph |
| Optional metadata | Direct parsed-QTF preserves absent versus present-empty unit and explicit SPN 0; BRF empty-value normalization is noted below |
| Layout metadata | Original bit positions/widths, Intel/Motorola enum and signedness survive parsing and generated decoding |
| Exact scaling | `0.123456789012345678901234567890` and `-2.000000000000000000000000000001` survive exactly as strings; no binary float round trip |
| Dynamic DBC enums | `EnumValue { raw: u64, label: string }` carries full `UINT64_MAX` exactly |
| Native BRD enum maximum | A BRD enum literal `18446744073709551615` fails the YAML signed-int64 property decoder; do not model arbitrary DBC raw domains as native schema enums |
| Multiplexing | Switch/branch names, explicit zero/one selection values and optional switch flags are representable; semantic validation remains the model consumer's responsibility |
| Strict failures | Unknown/duplicate fields, uint64 overflow, unit bounds, bad byte-order enum and oversized message arrays reject |

The root `u64_probe` is an independent representation probe, **not** a valid enum of
the fixture's 4-bit Selector. This avoids pretending that a 64-bit signal and a
separate switch fit together in an 8-byte frame. The study covers representability,
not automatic DBC validation, signal decoding, NA/error interpretation or BRD/C
adapter generation. The hand-written model does not constitute a final export schema.

## Confirmed generic BRF reader defect (resolved)

Before the follow-up fix, direct `parse_qtf` consumption worked and `import_qtf`
produced a valid 771-byte BRF record, but generic
`record_array(messages).element(0)` returned `dbc.exchange.EnumValue` rather than
`dbc.exchange.FrameDefinition`. The follow-up now passes the complete round trip.
This is a reader/cache defect, not a study schema or consumer-indexing error:

1. The schema compiles normally. QTF is resolved by QBS field names and parsed into
   the correct message/signal graph, without hardcoded field indexes.
2. The **same imported bytes** decode correctly through the generated owning C++
   decoder (`decode_DbcModel`). That independent path reads the expected frame,
   three signals, exact decimal text, and the uint64 maximum; it does not use the
   generic reader's validation cache.
3. A schema-free cache probe reproduces the operation order used by the reader:
   begin an outer array, begin an inner array, append the inner child, then append
   the outer child. The outer relation expects node 1 but observes inner node 2.
   No DBC, QTF, schema, encoder or field lookup participates in this probe.
4. `ValidationCache::begin_array` captures `array_children_.size()` as `child_begin`
   but does not reserve the declared count's slots. `add_array_element` appends to
   one global vector after recursive validation. Inner children therefore precede
   the outer child, while `BrfRecordArrayView::element` assumes a contiguous range
   beginning at the earlier offset. Existing field mappings already avoid a similar
   contiguity assumption, but array-child mappings do not.

The focused follow-up replaces the global array-child range with an ordered child
list owned by each array relation. `add_array` and the incremental
`begin_array`/`add_array_element` path populate the same logical representation;
`BrfRecordArrayView::element` reads the relation's list. Nested appends and sibling
arrays cannot change another relation's logical order. Unlike reserving declared
counts, this only grows child storage as elements are validated. Existing work,
node, array and traversal limits and BRF bytes are unchanged. Per-array vectors add
allocation/container overhead; the C++ generic reader already uses heap-backed
cache storage. The C allocation-free workspace API is unchanged.

The fix does not change the existing encoder policy: empty strings/bytes and
zero-count arrays normalize to absence in BRF. Parsed QTF still distinguishes
omitted fields from explicit empty values, and the reader still accepts legacy
present-empty encodings. The full BRF round trip preserves the normalized bytes,
not every original QTF presence distinction. If the exchange needs to preserve
"unknown unit" versus "explicitly no unit" through BRF, its model must carry that
semantic distinction explicitly (for example a presence/status field); no BRF
change is included here.

The retained schema-free reproduction is now a passing cache regression. The study
consumer always requires the generic BRF/canonical-QTF round trip; the historical
`--require-roundtrip` invocation remains accepted and succeeds. It also compares
all imported bytes with the generated owning decoder's decode/re-encode. This
closes the reader blocker for the measured model without resuming the exporter.

Focused tests cover distinct Root/Branch/Leaf identities and scalar values,
multiple nested/sibling arrays of unequal lengths, normalized empty arrays and
legacy present-empty arrays, all indexed elements and out-of-range accesses,
structured value views, restarted depth-first traversal, visitor stop, traversal
work/depth limits, validation limits, wrong child IDs and truncation. Empty-array
framing declaring a missing child is rejected.

Other implementations were checked: C assigns all element-node slots for each
array before descending into child validation, so it does not have this defect.
A test reads the same nested BRF using its unchanged workspace API. Python stores
record-array children in local per-frame lists; a regression independently builds
nested BRF with an empty middle child and verifies later elements and traversal.
C and Python reader implementations are unchanged. C++ direct record-array access,
FieldValueView/BrfArrayValueView, traversal and QTF export share the corrected mapping.

## Host API and distribution boundary

The separately built `dbc_model_exchange_consumer` executable uses the QBS emitted
by `quarry_schema_compiler`; it is not embedded in that compiler and does not use
J1939 or a second DBC parser. It links internal `quarry_compiler_qbs` plus
`quarry_runtime` for the independent generated decoder.

Required generic interfaces include `compiler/qbs/parser.hpp`,
`compiler/diagnostics/diagnostic.hpp`, `compiler/support/configuration_identity.hpp`,
`quarry/runtime/qtf_parser.hpp`, `qtf_importer.hpp`, `qtf_exporter.hpp`, and the generic
BRF reader/encoder headers. The cache header is used only for defect isolation.
The QBS target brings compiler layout/diagnostic/Schema IR dependencies transitively.
The full host compiler build uses C++20, protobuf/Abseil and libyaml; the study adds
no new library dependency or install target.

The supported installed compiler executable can produce QBS. Generic C++ QTF APIs
are **source-tree composition**, not a supported installed QTF SDK. The current
recursive header installation copies the QTF headers, but a clean-prefix include
fails at non-installed `compiler/diagnostics/diagnostic.hpp`; the required
`quarry_compiler_qbs` library/target is not installed/exported. `Quarry::runtime`
alone is insufficient. The installed-package compile probe confirms this. No public
distribution boundary was expanded. A standalone external consumer needs either
an explicitly accepted pinned-source arrangement or a separately reviewed host
API/tool packaging decision before this is a supported deployment path.

## Version coordination

Keep five distinct concepts:

* Model BRD version and explicit `model_version`: the semantic exchange contract.
  The consumer checks version 1; BRD presence alone does not enforce required model
  fields or mux semantics. Omitted fields are generally permitted by Quarry, so the
  model consumer must enforce its own required information and cross-references.
* QBS `schema_id`, algorithm 1: structural identity of the **complete model
  configuration**, not DBC content and not a substitute for model versioning.
  The study pins `0bfc6746ad642a7dc4b577479115b2ea` and rejects a mismatched QBS.
* Reflective QBS artifact identity: field names matter to named QTF. A field rename
  changes reflective QBS bytes and rejects old named QTF, but leaves structural
  `schema_id` unchanged. Pin an exact reflective artifact/digest as well as the
  structural identity when deploying independently versioned tools.
* Exporter build/version: provenance and supported model/dialect capability, not
  a schema compatibility proof. The study records `hand-authored-study`; no real
  exporter behavior is certified.
* QTF dialect: strict v1 by default; comments require explicit opt-in. Canonical
  producer output should remain comment-free for older readers.

Measured probes show that incrementing only a BRD `version` does not change the
QBS image or structural identity; changing a field's type does. Therefore a version
bump alone cannot protect a consumer. Agree an external manifest/selection protocol
for model version, root FQN, QBS structural ID and exact reflective artifact before
parsing. Unknown QTF fields reject; there is no ignore-unknown fallback. Unknown
model version is rejected by consumer policy, after the matching QBS has been
selected. Incompatible types/bounds and semantics require deliberate regeneration
and compatibility review. Source DBC changes alter model instance/provenance, not
necessarily the model's QBS identity. Telemetry BRD schema identity generated later
from these definitions is a separate Quarry mapping concern.

## Recommendation

Proceed with a **pinned source-tree, direct parsed-QTF prototype**, retaining decimal
strings and enum-value records. Before a production exporter/independent host tool:

1. Decide and validate the host QTF packaging boundary; current installed interfaces
   cannot support that consumer. Do not assume header visibility means a supported API.
2. The nested reader prerequisite is now resolved. Keep the full generic round-trip
   and generated-decoder comparison as regressions; this does not resolve the
   remaining host packaging or model-contract decisions.
3. Agree a minimal versioned model contract, bounds and failure policy, required-field
   and mux validation, artifact pinning, and Quarry's mapping/precision/quality rules.
   Dynamic DBC enum patterns should use u64 data fields, avoiding the native BRD
   enum-literal range restriction.

These findings do not justify resuming or finishing the paused J1939 exporter before
review. There is no need to extend BRF or duplicate DBC parsing to run this study.

## Commands and observed results

Commands below run from the Quarry repository root. Build output stays in `/tmp`.
Host: macOS, AppleClang 17; existing protobuf 7.35.1/protoc 35.1. No RTOS, Windows,
Linux SocketCAN or cross-target validation is claimed.

```sh
cmake -S . -B /tmp/quarry-qtf-study/build -DCMAKE_BUILD_TYPE=Debug \
  -DQUARRY_WARNINGS_AS_ERRORS=ON -DQUARRY_ENABLE_CLANG_TIDY=OFF
cmake --build /tmp/quarry-qtf-study/build --target \
  runtime_qtf_parser_test runtime_qtf_exporter_test runtime_qbs_brf_reader_test \
  runtime_qbs_brf_encoder_test qbs_parser_test runtime_brf_validation_cache_test \
  dbc_model_exchange_consumer -j 4
ctest --test-dir /tmp/quarry-qtf-study/build --output-on-failure \
  -R '^(runtime_qtf_parser_test|runtime_qtf_exporter_test|runtime_qbs_brf_reader_test|runtime_qbs_brf_encoder_test|qbs_parser_test|runtime_brf_validation_cache_test|dbc_model_exchange_consumer)$'
cmake --build /tmp/quarry-qtf-study/build --target quarry_brf_inspect -j 4
cmake --install /tmp/quarry-qtf-study/build --prefix /tmp/quarry-qtf-study/install
python3 tests/dbc_model_exchange_probes.py \
  /tmp/quarry-qtf-study/build /tmp/quarry-qtf-study/install
clang-format --dry-run --Werror include/quarry/runtime/qtf_parser.cpp \
  include/quarry/runtime/qtf_parser.hpp tests/runtime/qtf_parser_test.cpp \
  tests/runtime/dbc_model_exchange_consumer.cpp
git diff --check
```

The initial study passed seven selected CTest targets. The parser suite has 12 tests; the infinity
and enum regressions demonstrably failed before their independent fixes. The probe
script now passes the full generic round trip and its positive/negative checks;
the native-enum and installed-SDK gaps remain. `--require-roundtrip` now requires
and passes the same complete round trip. Generated owning decode/re-encode is
byte-identical to the imported BRF.
Formatting and whitespace checks pass. Existing external Abseil nullability warnings
and duplicate-library linker warnings are present; no warning policy was relaxed.
The initial study did not run the full suite or sanitizers. Follow-up validation
is recorded below; fuzzing and target-platform validation are not claimed.

The study consumer can also be invoked directly:

```sh
/tmp/quarry-qtf-study/build/tests/dbc_model_exchange_consumer \
  /tmp/quarry-qtf-study/build/tests/dbc_model_exchange/model.qbs \
  tests/fixtures/dbc_model_exchange/model.qtf \
  tests/fixtures/dbc_model_exchange/expected-schema-id.txt
# Repeat with --require-roundtrip to run the formerly failing regression (now exit 0).
```

## Reader follow-up validation

The reader follow-up changes only the internal C++ cache representation and its
lookup path, with cache/reader regressions, C/Python comparison tests and mandatory
study round trips. QTF syntax and the previously tested parser fixes are unchanged.
There are no BRF/QBS identity/version changes, new library dependencies, install/export
changes or generated-code API epoch changes. Native source-tree consumers rebuild.

Follow-up commands (from the Quarry root):

```sh
cmake --preset debug -B /tmp/quarry-qtf-study/build
cmake --build /tmp/quarry-qtf-study/build --parallel 4
ctest --test-dir /tmp/quarry-qtf-study/build --output-on-failure --parallel 4
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=.:runtime/python/src python3 -m unittest discover \
  -s runtime/python/tests -p 'test_generic_runtime*.py'
python3 tests/dbc_model_exchange_probes.py /tmp/quarry-qtf-study/build \
  /tmp/quarry-qtf-study/install
```

Follow-up results: full Debug configure and build passed; **all 77 CTest entries
passed**. The five new nested-array reader tests and six cache tests pass; the
28 Python generic-runtime tests pass. The schema-free cache regression also passed
all six cache tests under AddressSanitizer/UBSan with `halt_on_error=1`, with no
findings. That sanitizer check instruments the cache implementation and its tests,
not the entire repository. Formatting and `git diff --check` pass. Full fuzzing,
Windows/Linux and actual constrained-target testing were not performed.

The study imports the same 771-byte BRF as before, reads the expected frame/signals
through the generic cache, exports/reimports canonical QTF byte-identically, and
compares every imported byte with the generated owning decoder's re-encoding.
The probe script now requires the formerly failing `--require-roundtrip` command
to succeed. The remaining native BRD enum-literal range, installed QTF SDK and
model/versioning decisions are unchanged; BRF empty-value normalization is also
unchanged as noted above.

Standalone cache sanitizer command used on this host:

```sh
c++ -std=c++20 -Wall -Wextra -Werror -Wno-sign-compare \
  -fsanitize=address,undefined -isystem /usr/local/include -I . -I include \
  include/quarry/runtime/detail/brf_validation_cache.cpp \
  tests/runtime/brf_validation_cache_test.cpp /usr/local/lib/libgtest.a \
  /usr/local/lib/libgtest_main.a -pthread -o /tmp/quarry-reader-fix/cache-sanitized
UBSAN_OPTIONS=halt_on_error=1 /tmp/quarry-reader-fix/cache-sanitized
```
