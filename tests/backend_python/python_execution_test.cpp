// Proves the Python backend (PR-118 skeleton, PR-119 scalar support,
// PR-120 enum support) produces a package that a real Python interpreter
// can actually import and use, matching the project's "verify end-to-end,
// don't just trust generated text" discipline already applied to the
// C/C++ interop tests. Covers: generating through the real
// quarry-schema-compiler binary and importing the result with a real
// `python3` subprocess; a zero-field record's real (not stubbed)
// encode/decode round trip; a scalar record's real encode/decode round
// trip with representative values; an absent scalar field decoding as
// None; an out-of-range scalar value raising EncodeError at encode time;
// malformed/truncated/trailing-byte input raising DecodeError at decode
// time; an enum field's real encode/decode round trip; an enum value not
// defined by the schema raising EncodeError at encode time; and a decoded
// enum value not defined by the schema raising DecodeError at decode time.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

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

} // namespace
