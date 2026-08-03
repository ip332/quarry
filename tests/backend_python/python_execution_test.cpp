// Proves the Python backend (PR-118 skeleton, PR-119 scalar support,
// PR-120 enum support, PR-121 string/bytes support, PR-123 variable-width
// arrays) produces a package
// that a real Python interpreter can actually import and use, matching
// the project's "verify end-to-end, don't just trust generated text"
// discipline already applied to the C/C++ interop tests. Covers:
// generating through the real quarry-schema-compiler binary and importing
// the result with a real `python3` subprocess; a zero-field record's real
// (not stubbed) encode/decode round trip; a scalar record's real
// encode/decode round trip with representative values; an absent scalar
// field decoding as None; an out-of-range scalar value raising
// EncodeError at encode time; malformed/truncated/trailing-byte input
// raising DecodeError at decode time; an enum field's real encode/decode
// round trip; an enum value not defined by the schema raising EncodeError
// at encode time; a decoded enum value not defined by the schema raising
// DecodeError at decode time; a string/bytes field's real encode/decode
// round trip (including empty and maximum-length values); an over-length
// string/bytes value raising EncodeError at encode time; and malformed
// UTF-8 raising DecodeError at decode time for a string field.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <string>
#include <string_view>

#include "compiler/backend_python/backend_python.hpp"

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifndef QUARRY_SCHEMA_COMPILER_TOOL
#error "QUARRY_SCHEMA_COMPILER_TOOL must be defined"
#endif
#ifndef QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR
#error "QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR must be defined"
#endif
#ifndef QUARRY_TEST_PYTHON3
#define QUARRY_TEST_PYTHON3 ""
#endif

namespace {

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string("quarry-python-backend-") + std::string(stem) + "-" +
         std::to_string(suffix));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

[[nodiscard]] std::string shell_quote(std::string_view value) {
    std::string quoted = "'";
    for (const char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += "'";
    return quoted;
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary};
    output << text;
}

// Runs `command` (already fully quoted by the caller) and returns the
// child's real exit code, unwrapping std::system()'s wait()-style encoded
// status the same way tests/interop/c_cpp_codec_interop_test.cpp does.
[[nodiscard]] int run_and_get_exit_code(const std::string& command) {
    const int raw_status = std::system(command.c_str());
#ifdef _WIN32
    return raw_status;
#else
    if (WIFEXITED(raw_status)) {
        return WEXITSTATUS(raw_status);
    }
    return 128 + raw_status;
#endif
}

constexpr std::string_view kSchema = "namespace: acme.telemetry\n"
                                     "record: Sample\n"
                                     "version: 1\n"
                                     "type: data\n"
                                     "fields: {}\n";

constexpr std::string_view kScalarSchema = "namespace: acme.telemetry\n"
                                          "record: Sample\n"
                                          "version: 1\n"
                                          "type: data\n"
                                          "fields:\n"
                                          "  active:\n"
                                          "    type: bool\n"
                                          "  count:\n"
                                          "    type: uint32\n"
                                          "  ratio:\n"
                                          "    type: float64\n"
                                          "  level:\n"
                                          "    type: int8\n";

constexpr std::string_view kEnumSchema = "namespace: acme.telemetry\n"
                                        "record: Sample\n"
                                        "version: 1\n"
                                        "type: data\n"
                                        "fields:\n"
                                        "  status:\n"
                                        "    type: Status\n"
                                        "  count:\n"
                                        "    type: uint32\n"
                                        "enums:\n"
                                        "  Status:\n"
                                        "    values:\n"
                                        "      OK: 0\n"
                                        "      WARNING: 1\n"
                                        "      ERROR: 2\n";

constexpr std::string_view kStringBytesSchema = "namespace: acme.telemetry\n"
                                               "record: Sample\n"
                                               "version: 1\n"
                                               "type: data\n"
                                               "fields:\n"
                                               "  label:\n"
                                               "    type: string\n"
                                               "    max_bytes: 16\n"
                                               "  blob:\n"
                                               "    type: bytes\n"
                                               "    max_bytes: 16\n";

constexpr std::string_view kKeywordSchema = "namespace: class\n"
                                           "record: def\n"
                                           "version: 1\n"
                                           "type: data\n"
                                           "fields:\n"
                                           "  Optional:\n"
                                           "    type: class\n"
                                           "enums:\n"
                                           "  class:\n"
                                           "    values:\n"
                                           "      import: 0\n";

// Runs `harness_body` (a fragment of Python source assuming `generated` is
// already on sys.path and `acme.telemetry.schema.Sample` is importable)
// against the schema compiled from `schema_source`, returning the real
// python3 subprocess exit code. `harness_body` is expected to print "OK"
// and exit 0 on success, or print to stderr and exit nonzero on failure --
// the caller asserts on the exit code.
[[nodiscard]] int run_python_harness(std::string_view stem, std::string_view schema_source,
                                    std::string_view harness_body) {
    const std::filesystem::path root = make_temp_directory(stem);
    const std::filesystem::path schema = root / "schema.brd";
    write_text_file(schema, schema_source);

    const std::filesystem::path generated = root / "generated";
    const std::string generate_command = shell_quote(QUARRY_SCHEMA_COMPILER_TOOL) +
                                        " --language python -o " + shell_quote(generated.string()) +
                                        " " + shell_quote(schema.string());
    if (run_and_get_exit_code(generate_command) != 0) {
        ADD_FAILURE() << "generation failed: " << generate_command;
        return 1;
    }

    const std::filesystem::path harness_script = root / "harness.py";
    write_text_file(harness_script, std::string(harness_body));

    const std::string python_path =
        generated.string() + ":" + std::string(QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR);
    const std::string run_command = "PYTHONPATH=" + shell_quote(python_path) + " " +
                                    shell_quote(QUARRY_TEST_PYTHON3) + " " +
                                    shell_quote(harness_script.string());
    return run_and_get_exit_code(run_command);
}

[[nodiscard]] int run_python_cross_namespace_harness(std::string_view stem,
                                                     std::string_view harness_body) {
    const std::filesystem::path root = make_temp_directory(stem);
    const std::filesystem::path shared = root / "shared.brd";
    const std::filesystem::path schema = root / "schema.brd";
    const std::filesystem::path generated = root / "generated";
    write_text_file(shared,
                    "namespace: acme.shared\n"
                    "record: Child\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  value:\n"
                    "    type: uint32\n"
                    "enums:\n"
                    "  Status:\n"
                    "    values:\n"
                    "      OK: 0\n"
                    "      ERROR: 1\n");
    write_text_file(schema,
                    "namespace: acme.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "imports:\n"
                    "  - shared.brd\n"
                    "fields:\n"
                    "  status:\n"
                    "    type: acme.shared.Status\n"
                    "  child:\n"
                    "    type: acme.shared.Child\n"
                    "  statuses:\n"
                    "    type: acme.shared.Status[]\n"
                    "    max_elements: 3\n"
                    "  children:\n"
                    "    type: acme.shared.Child[]\n"
                    "    max_elements: 3\n");

    for (const std::filesystem::path& input : {shared, schema}) {
        const std::string command = shell_quote(QUARRY_SCHEMA_COMPILER_TOOL) +
                                    " --language python -o " + shell_quote(generated.string()) +
                                    " " + shell_quote(input.string());
        if (run_and_get_exit_code(command) != 0) {
            ADD_FAILURE() << "cross-namespace generation failed: " << command;
            return 1;
        }
    }
    const std::filesystem::path harness_script = root / "harness.py";
    write_text_file(harness_script, std::string(harness_body));
    const std::string python_path =
        generated.string() + ":" + std::string(QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR);
    return run_and_get_exit_code("PYTHONPATH=" + shell_quote(python_path) + " " +
                                shell_quote(QUARRY_TEST_PYTHON3) + " " +
                                shell_quote(harness_script.string()));
}

// The source-schema frontend currently has no property spelling for the
// per-element max_bytes carried by a string/bytes array's Schema IR element.
// Exercise the validated backend boundary directly so generated code still
// runs through a real Python interpreter without expanding the frontend or
// compiler pipeline in this PR.
[[nodiscard]] int run_python_variable_array_harness(std::string_view stem,
                                                    std::string_view harness_body) {
    const std::filesystem::path root = make_temp_directory(stem);
    const std::filesystem::path generated = root / "generated";

    quarry::compiler::schema_ir::SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    auto* root_namespace = schema_ir.mutable_root_namespace();
    root_namespace->set_ir_id(1);
    auto* record = root_namespace->add_records();
    record->set_ir_id(2);
    record->set_record_id(1);
    record->set_name("Sample");
    record->set_fqn("Sample");

    auto* labels = record->add_fields();
    labels->set_name("labels");
    labels->set_field_index(0);
    auto* labels_array = labels->mutable_type()->mutable_array();
    labels_array->set_max_elements(3);
    labels_array->mutable_element_type()->mutable_string()->set_max_bytes(8);

    auto* blobs = record->add_fields();
    blobs->set_name("blobs");
    blobs->set_field_index(1);
    auto* blobs_array = blobs->mutable_type()->mutable_array();
    blobs_array->set_max_elements(2);
    blobs_array->mutable_element_type()->mutable_bytes()->set_max_bytes(4);

    quarry::compiler::backend_python::Backend backend;
    quarry::compiler::backend_python::CodegenOptions options;
    options.output_directory = generated.string();
    const auto result = backend.generate(schema_ir, options);
    if (!result.success) {
        ADD_FAILURE() << "direct Schema IR generation failed: " << result.error_message;
        return 1;
    }
    for (const auto& file : result.files) {
        write_text_file(generated / file.path, file.content);
    }

    const std::filesystem::path harness_script = root / "harness.py";
    write_text_file(harness_script, std::string(harness_body));
    const std::string python_path =
        generated.string() + ":" + std::string(QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR);
    const std::string run_command = "PYTHONPATH=" + shell_quote(python_path) + " " +
                                    shell_quote(QUARRY_TEST_PYTHON3) + " " +
                                    shell_quote(harness_script.string());
    return run_and_get_exit_code(run_command);
}

[[nodiscard]] int run_python_nested_record_harness(std::string_view stem,
                                                   std::string_view harness_body) {
    const std::filesystem::path root = make_temp_directory(stem);
    const std::filesystem::path generated = root / "generated";

    quarry::compiler::schema_ir::SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    auto* root_namespace = schema_ir.mutable_root_namespace();
    root_namespace->set_ir_id(1);

    auto* parent = root_namespace->add_records();
    parent->set_ir_id(2);
    parent->set_record_id(1);
    parent->set_name("Parent");
    parent->set_fqn("Parent");
    auto* child_field = parent->add_fields();
    child_field->set_name("child");
    child_field->set_field_index(0);
    child_field->mutable_type()->mutable_record()->set_target_record_ir_id(3);
    auto* optional_count = parent->add_fields();
    optional_count->set_name("count");
    optional_count->set_field_index(1);
    optional_count->mutable_type()->set_primitive(
        ::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    auto* items = parent->add_fields();
    items->set_name("items");
    items->set_field_index(2);
    items->mutable_type()->mutable_array()->set_max_elements(3);
    items->mutable_type()->mutable_array()->mutable_element_type()->mutable_record()->set_target_record_ir_id(3);

    auto* child = root_namespace->add_records();
    child->set_ir_id(3);
    child->set_record_id(2);
    child->set_name("Child");
    child->set_fqn("Child");
    auto* value = child->add_fields();
    value->set_name("value");
    value->set_field_index(0);
    value->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);

    quarry::compiler::backend_python::Backend backend;
    quarry::compiler::backend_python::CodegenOptions options;
    options.output_directory = generated.string();
    const auto result = backend.generate(schema_ir, options);
    if (!result.success) {
        ADD_FAILURE() << "direct nested-record Schema IR generation failed: "
                      << result.error_message;
        return 1;
    }
    for (const auto& file : result.files) {
        write_text_file(generated / file.path, file.content);
    }

    const std::filesystem::path harness_script = root / "harness.py";
    write_text_file(harness_script, std::string(harness_body));
    const std::string python_path =
        generated.string() + ":" + std::string(QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR);
    const std::string run_command = "PYTHONPATH=" + shell_quote(python_path) + " " +
                                    shell_quote(QUARRY_TEST_PYTHON3) + " " +
                                    shell_quote(harness_script.string());
    return run_and_get_exit_code(run_command);
}

TEST(PythonExecutionTest, GeneratedZeroFieldRecordRoundTripsThroughEncodeDecode) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("zero-field", kSchema,
                                "from acme.telemetry.schema import Sample\n"
                                "\n"
                                "sample = Sample()\n"
                                "data = sample.encode()\n"
                                "decoded = Sample.decode(data)\n"
                                "assert decoded == sample, (decoded, sample)\n"
                                "assert sample.encoded_size() == len(data)\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, CrossNamespaceGeneratedPackageImportsAndRoundTrips) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_cross_namespace_harness(
                  "cross-namespace",
                  "from acme.shared.schema import Child, Status\n"
                  "from acme.telemetry.schema import Sample\n"
                  "\n"
                  "sample = Sample(status=Status.OK, child=Child(value=7),\n"
                  "                statuses=[Status.OK, Status.ERROR],\n"
                  "                children=[Child(value=7), Child(value=9)])\n"
                  "data = sample.encode()\n"
                  "decoded = Sample.decode(data)\n"
                  "assert decoded == sample, (decoded, sample)\n"
                  "assert sample.encoded_size() == len(data)\n"
                  "assert decoded.children[1].value == 9\n"
                  "print('OK')\n"),
              0);
}

TEST(PythonExecutionTest, ScalarRecordEncodeDecodeRoundTripsWithRealValues) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("scalar-round-trip", kScalarSchema,
                                "from acme.telemetry.schema import Sample\n"
                                "\n"
                                "sample = Sample(active=True, count=42, ratio=3.14, level=-5)\n"
                                "data = sample.encode()\n"
                                "assert data == bytes.fromhex(\n"
                                "    '0100040000000001000000000000001a000001010104020508030d01'\n"
                                "    '010000002a40091eb851eb851ffb'), data.hex()\n"
                                "decoded = Sample.decode(data)\n"
                                "assert decoded == sample, (decoded, sample)\n"
                                "assert sample.encoded_size() == len(data)\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, AbsentScalarFieldDecodesAsNone) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("scalar-absent", kScalarSchema,
                                "from acme.telemetry.schema import Sample\n"
                                "\n"
                                "sample = Sample(count=7)\n"
                                "decoded = Sample.decode(sample.encode())\n"
                                "assert decoded.count == 7\n"
                                "assert decoded.active is None\n"
                                "assert decoded.ratio is None\n"
                                "assert decoded.level is None\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, OutOfRangeScalarValueRaisesEncodeErrorAtEncodeTime) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("scalar-out-of-range", kScalarSchema,
                                "from quarry.runtime.python import binary_record as brf\n"
                                "from acme.telemetry.schema import Sample\n"
                                "\n"
                                "try:\n"
                                "    Sample(count=-1).encode()\n"
                                "except brf.EncodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected EncodeError for count=-1')\n"
                                "\n"
                                "try:\n"
                                "    Sample(level=200).encode()\n"
                                "except brf.EncodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected EncodeError for level=200')\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, MalformedInputRaisesDecodeError) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("scalar-malformed", kScalarSchema,
                                "from quarry.runtime.python import binary_record as brf\n"
                                "from acme.telemetry.schema import Sample\n"
                                "\n"
                                "data = Sample(count=42).encode()\n"
                                "\n"
                                "try:\n"
                                "    Sample.decode(data[:-1])\n"
                                "except brf.DecodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected DecodeError for truncated input')\n"
                                "\n"
                                "try:\n"
                                "    Sample.decode(data + b'\\x00')\n"
                                "except brf.DecodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected DecodeError for trailing bytes')\n"
                                "\n"
                                "try:\n"
                                "    Sample.decode(b'not a record')\n"
                                "except brf.DecodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected DecodeError for garbage input')\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, EnumFieldEncodeDecodeRoundTripsWithRealValues) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("enum-round-trip", kEnumSchema,
                                "from acme.telemetry.schema import Sample, Status\n"
                                "\n"
                                "sample = Sample(status=Status.WARNING, count=42)\n"
                                "data = sample.encode()\n"
                                "decoded = Sample.decode(data)\n"
                                "assert decoded == sample, (decoded, sample)\n"
                                "assert decoded.status is Status.WARNING\n"
                                "assert sample.encoded_size() == len(data)\n"
                                "\n"
                                "# A raw int equal to a defined member round-trips identically.\n"
                                "raw_sample = Sample(status=2, count=1)\n"
                                "raw_decoded = Sample.decode(raw_sample.encode())\n"
                                "assert raw_decoded.status is Status.ERROR\n"
                                "\n"
                                "# Absence still works alongside an enum field.\n"
                                "absent = Sample(count=1)\n"
                                "assert Sample.decode(absent.encode()).status is None\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, InvalidEnumValueRaisesEncodeErrorAtEncodeTime) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("enum-invalid-encode", kEnumSchema,
                                "from quarry.runtime.python import binary_record as brf\n"
                                "from acme.telemetry.schema import Sample\n"
                                "\n"
                                "try:\n"
                                "    Sample(status=99).encode()\n"
                                "except brf.EncodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected EncodeError for status=99')\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, UnknownDecodedEnumValueRaisesDecodeError) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("enum-invalid-decode", kEnumSchema,
                                "from quarry.runtime.python import binary_record as brf\n"
                                "from acme.telemetry.schema import Sample, Status\n"
                                "\n"
                                "data = bytearray(Sample(status=Status.OK, count=1).encode())\n"
                                "# The status field is field_index 0, a single byte, stored\n"
                                "# first in the payload -- immediately after the 16-byte header\n"
                                "# and the 2-entry Field Directory (3 bytes per entry).\n"
                                "payload_start = 16 + 2 * 3\n"
                                "data[payload_start] = 99\n"
                                "try:\n"
                                "    Sample.decode(bytes(data))\n"
                                "except brf.DecodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected DecodeError for an unknown enum "
                                "value')\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, StringAndBytesFieldsEncodeDecodeRoundTripWithRealValues) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("string-bytes-round-trip", kStringBytesSchema,
                                "from acme.telemetry.schema import Sample\n"
                                "\n"
                                "sample = Sample(label='caf\\u00e9', blob=bytes([0, 255, 128]))\n"
                                "data = sample.encode()\n"
                                "decoded = Sample.decode(data)\n"
                                "assert decoded == sample, (decoded, sample)\n"
                                "assert decoded.label == 'caf\\u00e9'\n"
                                "assert decoded.blob == bytes([0, 255, 128])\n"
                                "assert sample.encoded_size() == len(data)\n"
                                "\n"
                                "# Empty-present values round-trip distinctly from absence.\n"
                                "empty = Sample(label='', blob=b'')\n"
                                "decoded_empty = Sample.decode(empty.encode())\n"
                                "assert decoded_empty.label == ''\n"
                                "assert decoded_empty.blob == b''\n"
                                "\n"
                                "# Absent fields decode as None.\n"
                                "absent = Sample()\n"
                                "decoded_absent = Sample.decode(absent.encode())\n"
                                "assert decoded_absent.label is None\n"
                                "assert decoded_absent.blob is None\n"
                                "\n"
                                "# Maximum-length values (max_bytes=16) round-trip.\n"
                                "maximum = Sample(label='x' * 16, blob=bytes(16))\n"
                                "decoded_max = Sample.decode(maximum.encode())\n"
                                "assert decoded_max.label == 'x' * 16\n"
                                "assert decoded_max.blob == bytes(16)\n"
                                "\n"
                                "# Bytes fields never validate UTF-8.\n"
                                "binary = Sample(blob=bytes([0xFF, 0xFE, 0x00, 0x80]))\n"
                                "decoded_binary = Sample.decode(binary.encode())\n"
                                "assert decoded_binary.blob == bytes([0xFF, 0xFE, 0x00, 0x80])\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, OverLengthStringOrBytesValueRaisesEncodeErrorAtEncodeTime) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("string-bytes-over-length", kStringBytesSchema,
                                "from quarry.runtime.python import binary_record as brf\n"
                                "from acme.telemetry.schema import Sample\n"
                                "\n"
                                "try:\n"
                                "    Sample(label='x' * 17).encode()\n"
                                "except brf.EncodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected EncodeError for an over-length "
                                "label')\n"
                                "\n"
                                "try:\n"
                                "    Sample(blob=bytes(17)).encode()\n"
                                "except brf.EncodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected EncodeError for an over-length "
                                "blob')\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, MalformedUtf8RaisesDecodeErrorForStringFieldAtDecodeTime) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("string-malformed-utf8", kStringBytesSchema,
                                "from quarry.runtime.python import binary_record as brf\n"
                                "from acme.telemetry.schema import Sample\n"
                                "\n"
                                "# label is field_index 0; its content bytes are the first\n"
                                "# payload bytes, immediately after the 16-byte header and the\n"
                                "# single-entry Field Directory (varuint offset/length both fit\n"
                                "# in one byte each for this small payload: 1 + 1 + 1 = 3 bytes).\n"
                                "data = bytearray(Sample(label='hi').encode())\n"
                                "payload_start = 16 + 3\n"
                                "data[payload_start] = 0xFF\n"
                                "try:\n"
                                "    Sample.decode(bytes(data))\n"
                                "except brf.DecodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected DecodeError for malformed UTF-8')\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, ArrayFieldsEncodeDecodeAndValidateWithRealPython) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_variable_array_harness("variable-array-round-trip",
                                "from quarry.runtime.python import binary_record as brf\n"
                                "from schema import Sample\n"
                                "\n"
                                "sample = Sample(labels=['', 'café', '🌍'],\n"
                                "               blobs=[b'', bytes([0, 255, 128])])\n"
                                "data = sample.encode()\n"
                                "decoded = Sample.decode(data)\n"
                                "assert decoded.labels == ['', 'café', '🌍'], decoded\n"
                                "assert decoded.blobs == [b'', bytes([0, 255, 128])], decoded\n"
                                "assert sample.encoded_size() == len(data)\n"
                                "\n"
                                "empty = Sample(labels=[], blobs=[])\n"
                                "decoded_empty = Sample.decode(empty.encode())\n"
                                "assert decoded_empty.labels == []\n"
                                "assert decoded_empty.blobs == []\n"
                                "\n"
                                "absent = Sample.decode(Sample().encode())\n"
                                "assert absent.labels is None\n"
                                "assert absent.blobs is None\n"
                                "\n"
                                "try:\n"
                                "    Sample(labels=['a', 'b', 'c', 'd']).encode()\n"
                                "except brf.EncodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected array bounds EncodeError')\n"
                                "\n"
                                "try:\n"
                                "    Sample(labels=['123456789']).encode()\n"
                                "except brf.EncodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected string element bounds EncodeError')\n"
                                "\n"
                                "try:\n"
                                "    Sample(blobs=[bytes(5)]).encode()\n"
                                "except brf.EncodeError:\n"
                                "    pass\n"
                                "else:\n"
                                "    raise SystemExit('expected bytes element bounds EncodeError')\n"
                                "print('OK')\n"),
             0);
}

TEST(PythonExecutionTest, NestedRecordRoundTripAndMalformedPayloadsWithRealPython) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_nested_record_harness(
                  "nested-record-round-trip",
                  "from quarry.runtime.python import binary_record as brf\n"
                  "from schema import Child, Parent\n"
                  "\n"
                  "sample = Parent(child=Child(value=7), count=42)\n"
                  "data = sample.encode()\n"
                  "decoded = Parent.decode(data)\n"
                  "assert decoded == sample, (decoded, sample)\n"
                  "assert decoded.child == Child(value=7)\n"
                  "assert sample.encoded_size() == len(data)\n"
                  "\n"
                  "# A present empty child is distinct from an absent child.\n"
                  "empty = Parent(child=Child())\n"
                  "assert Parent.decode(empty.encode()).child == Child()\n"
                  "absent = Parent(count=1)\n"
                  "assert Parent.decode(absent.encode()).child is None\n"
                  "\n"
                  "child_data = Child(value=7).encode()\n"
                  "for malformed_child in (child_data[:-1], child_data + b'\\x00'):\n"
                  "    malformed_parent = brf.encode_record(1, [(0, malformed_child)])\n"
                  "    try:\n"
                  "        Parent.decode(malformed_parent)\n"
                  "    except brf.DecodeError:\n"
                  "        pass\n"
                  "    else:\n"
                  "        raise SystemExit('malformed nested record was accepted')\n"
                  "\n"
                  "try:\n"
                  "    Parent.decode(data[:-1])\n"
                  "except brf.DecodeError:\n"
                  "    pass\n"
                  "else:\n"
                  "    raise SystemExit('truncated parent record was accepted')\n"
                  "print('OK')\n"),
              0);
}

TEST(PythonExecutionTest, RecordArrayRoundTripAndMalformedElementsWithRealPython) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_nested_record_harness(
                  "record-array-round-trip",
                  "from quarry.runtime.python import binary_record as brf\n"
                  "from schema import Child, Parent\n"
                  "\n"
                  "assert Parent.decode(Parent().encode()).items is None\n"
                  "assert Parent.decode(Parent(items=[]).encode()).items == []\n"
                  "sample = Parent(items=[Child(value=1), Child(), Child(value=3)])\n"
                  "assert Parent.decode(sample.encode()) == sample\n"
                  "try:\n"
                  "    Parent(items=[Child(), Child(), Child(), Child()]).encode()\n"
                  "except brf.EncodeError:\n"
                  "    pass\n"
                  "else:\n"
                  "    raise SystemExit('array max_elements was not enforced')\n"
                  "child = Child(value=7).encode()\n"
                  "def array_payload(payload):\n"
                  "    return b'\\x01' + bytes([len(payload)]) + payload\n"
                  "for payload in (child[:-1], child + b'\\x00'):\n"
                  "    malformed = brf.encode_record(1, [(2, array_payload(payload))])\n"
                  "    try:\n"
                  "        Parent.decode(malformed)\n"
                  "    except brf.DecodeError:\n"
                  "        pass\n"
                  "    else:\n"
                  "        raise SystemExit('malformed record array element was accepted')\n"
                  "trailing = brf.encode_record(1, [(2, b'\\x00\\x00')])\n"
                  "try:\n"
                  "    Parent.decode(trailing)\n"
                  "except brf.DecodeError:\n"
                  "    pass\n"
                  "else:\n"
                  "    raise SystemExit('record array trailing bytes were accepted')\n"
                  "print('OK')\n"),
              0);
}

TEST(PythonExecutionTest, EpochMismatchRaisesImportErrorAtImportTime) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness("epoch-mismatch", kSchema,
                                "import quarry.runtime.python as rt\n"
                                "rt.QUARRY_GENERATED_CODE_API_VERSION_PYTHON = 999\n"
                                "try:\n"
                                "    from acme.telemetry.schema import Sample\n"
                                "except ImportError:\n"
                                "    print('OK')\n"
                                "else:\n"
                                "    raise SystemExit('import did not raise ImportError')\n"),
             0);
}

TEST(PythonExecutionTest, EscapedKeywordNamesImportAndRoundTripInRealPython) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    EXPECT_EQ(run_python_harness(
                  "keyword-names", kKeywordSchema,
                  "from class_.schema import class_ as KeywordEnum, def_ as KeywordRecord\n"
                  "sample = KeywordRecord(Optional_=KeywordEnum.import_)\n"
                  "assert KeywordRecord.decode(sample.encode()) == sample\n"
                  "assert KeywordRecord.decode(KeywordRecord().encode()).Optional_ is None\n"
                  "print('OK')\n"),
              0);
}

} // namespace
