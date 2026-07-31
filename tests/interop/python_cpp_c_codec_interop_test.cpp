// Proves byte-for-byte BRF wire compatibility between the Python, C++, and
// C backends for a scalar-plus-enum-plus-string/bytes-plus-array schema
// (PR-119 scalars, PR-120 enum, PR-121 string/bytes, PR-122 arrays): generates
// the same schema
// through all three `quarry-schema-compiler` backends, compiles small C
// and C++ harness programs and writes a Python harness script against
// each generated output, and verifies (1) all three encoders produce
// identical bytes for identical field values covering every one of
// PR-119's eleven supported scalar types plus a same-namespace enum field,
// bounded string/bytes fields, and fixed-width scalar/enum arrays, (2) each language's decoder accepts
// bytes produced by either of the other two languages, (3) all three
// languages identically reject a truncated buffer and identically reject
// extra trailing bytes appended after a valid record, (4) all three
// languages identically reject a decoded enum byte the schema does not
// define, and (5) all three languages identically reject malformed UTF-8
// in a string field.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifndef QUARRY_SCHEMA_COMPILER_TOOL
#error "QUARRY_SCHEMA_COMPILER_TOOL must be defined"
#endif
#ifndef QUARRY_TEST_CXX_COMPILER
#error "QUARRY_TEST_CXX_COMPILER must be defined"
#endif
#ifndef QUARRY_TEST_REPO_INCLUDE_DIR
#error "QUARRY_TEST_REPO_INCLUDE_DIR must be defined"
#endif
#ifndef QUARRY_TEST_GENERATED_INCLUDE_DIR
#error "QUARRY_TEST_GENERATED_INCLUDE_DIR must be defined"
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
        (std::string("quarry-python-cpp-c-interop-") + std::string(stem) + "-" +
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
    std::ofstream output{path};
    if (!output) {
        throw std::runtime_error("failed to open test file for writing: " + path.string());
    }
    output << text;
}

[[nodiscard]] std::string read_binary_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
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

// A scalar-only schema covering all eleven types PR-119's Python backend
// (and the pre-existing C/C++ backends) support: bool and every
// fixed-width signed/unsigned integer and f32/f64 primitive.
constexpr std::string_view kSchema = "namespace: acme.telemetry\n"
                                     "record: Sample\n"
                                     "version: 1\n"
                                     "type: data\n"
                                     "fields:\n"
                                     "  flag:\n"
                                     "    type: bool\n"
                                     "  i8:\n"
                                     "    type: int8\n"
                                     "  u8:\n"
                                     "    type: uint8\n"
                                     "  i16:\n"
                                     "    type: int16\n"
                                     "  u16:\n"
                                     "    type: uint16\n"
                                     "  i32:\n"
                                     "    type: int32\n"
                                     "  u32:\n"
                                     "    type: uint32\n"
                                     "  i64:\n"
                                     "    type: int64\n"
                                     "  u64:\n"
                                     "    type: uint64\n"
                                     "  f32:\n"
                                     "    type: float32\n"
                                     "  f64:\n"
                                     "    type: float64\n"
                                     "  status:\n"
                                     "    type: Status\n"
                                     "  label:\n"
                                     "    type: string\n"
                                     "    max_bytes: 16\n"
                                     "  blob:\n"
                                     "    type: bytes\n"
                                     "    max_bytes: 16\n"
                                     "  readings:\n"
                                     "    type: float32[]\n"
                                     "    max_elements: 4\n"
                                     "  statuses:\n"
                                     "    type: Status[]\n"
                                     "    max_elements: 3\n"
                                     "enums:\n"
                                     "  Status:\n"
                                     "    values:\n"
                                     "      OK: 0\n"
                                     "      WARNING: 1\n"
                                     "      ERROR: 2\n";

// Every harness (C, C++, Python) below sets/checks exactly these same
// values, so their encoded bytes are directly comparable and their
// decoders can cross-validate each other's output.
constexpr std::string_view kCHarness =
    "#include \"acme/telemetry.generated.h\"\n"
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "static void set_fields(acme_telemetry_Sample_t* sample) {\n"
    "  sample->has_flag = true; sample->flag = true;\n"
    "  sample->has_i8 = true; sample->i8 = -5;\n"
    "  sample->has_u8 = true; sample->u8 = 250U;\n"
    "  sample->has_i16 = true; sample->i16 = -1000;\n"
    "  sample->has_u16 = true; sample->u16 = 60000U;\n"
    "  sample->has_i32 = true; sample->i32 = -100000;\n"
    "  sample->has_u32 = true; sample->u32 = 4000000000U;\n"
    "  sample->has_i64 = true; sample->i64 = -5000000000LL;\n"
    "  sample->has_u64 = true; sample->u64 = 10000000000000ULL;\n"
    "  sample->has_f32 = true; sample->f32 = 1.5f;\n"
    "  sample->has_f64 = true; sample->f64 = 2.71828182845904;\n"
    "  sample->has_status = true; sample->status = ACME_TELEMETRY_STATUS_WARNING;\n"
    "  sample->has_label = true;\n"
    "  memcpy(sample->label, \"caf\\xc3\\xa9\", 5); sample->label_length = 5;\n"
    "  sample->has_blob = true;\n"
    "  {\n"
    "    uint8_t blob_content[] = {0x00U, 0xFFU, 0x80U};\n"
    "    memcpy(sample->blob, blob_content, 3);\n"
    "    sample->blob_length = 3;\n"
    "  }\n"
    "  sample->has_readings = true; sample->readings_count = 2;\n"
    "  sample->readings[0] = 1.5f; sample->readings[1] = -2.0f;\n"
    "  sample->has_statuses = true; sample->statuses_count = 2;\n"
    "  sample->statuses[0] = ACME_TELEMETRY_STATUS_OK;\n"
    "  sample->statuses[1] = ACME_TELEMETRY_STATUS_ERROR;\n"
    "}\n"
    "static int check_fields(const acme_telemetry_Sample_t* v) {\n"
    "  if (!v->has_flag || v->flag != true) return 1;\n"
    "  if (!v->has_i8 || v->i8 != -5) return 2;\n"
    "  if (!v->has_u8 || v->u8 != 250U) return 3;\n"
    "  if (!v->has_i16 || v->i16 != -1000) return 4;\n"
    "  if (!v->has_u16 || v->u16 != 60000U) return 5;\n"
    "  if (!v->has_i32 || v->i32 != -100000) return 6;\n"
    "  if (!v->has_u32 || v->u32 != 4000000000U) return 7;\n"
    "  if (!v->has_i64 || v->i64 != -5000000000LL) return 8;\n"
    "  if (!v->has_u64 || v->u64 != 10000000000000ULL) return 9;\n"
    "  if (!v->has_f32 || v->f32 != 1.5f) return 10;\n"
    "  if (!v->has_f64 || v->f64 != 2.71828182845904) return 11;\n"
    "  if (!v->has_status || v->status != ACME_TELEMETRY_STATUS_WARNING) return 12;\n"
    "  if (!v->has_label || v->label_length != 5 || memcmp(v->label, \"caf\\xc3\\xa9\", 5) != 0)\n"
    "    return 13;\n"
    "  {\n"
    "    uint8_t expected_blob[] = {0x00U, 0xFFU, 0x80U};\n"
    "    if (!v->has_blob || v->blob_length != 3 || memcmp(v->blob, expected_blob, 3) != 0)\n"
    "      return 14;\n"
    "  }\n"
    "  if (!v->has_readings || v->readings_count != 2 || v->readings[0] != 1.5f ||\n"
    "      v->readings[1] != -2.0f) return 15;\n"
    "  if (!v->has_statuses || v->statuses_count != 2 ||\n"
    "      v->statuses[0] != ACME_TELEMETRY_STATUS_OK ||\n"
    "      v->statuses[1] != ACME_TELEMETRY_STATUS_ERROR) return 16;\n"
    "  return 0;\n"
    "}\n"
    "int main(int argc, char** argv) {\n"
    "  if (argc < 3) return 20;\n"
    "  if (strcmp(argv[1], \"encode\") == 0) {\n"
    "    acme_telemetry_Sample_t sample;\n"
    "    acme_telemetry_Sample_init(&sample);\n"
    "    set_fields(&sample);\n"
    "    uint8_t buf[256];\n"
    "    acme_telemetry_Sample_encode_result_t r =\n"
    "        acme_telemetry_Sample_encode(&sample, buf, sizeof(buf));\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 1;\n"
    "    FILE* f = fopen(argv[2], \"wb\");\n"
    "    if (!f) return 2;\n"
    "    fwrite(buf, 1, r.bytes_written, f);\n"
    "    fclose(f);\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"decode\") == 0) {\n"
    "    FILE* f = fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    uint8_t buf[256];\n"
    "    size_t n = fread(buf, 1, sizeof(buf), f);\n"
    "    fclose(f);\n"
    "    acme_telemetry_Sample_decode_result_t r = acme_telemetry_Sample_decode(buf, n);\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 4;\n"
    "    return check_fields(&r.value);\n"
    "  }\n"
    "  if (strcmp(argv[1], \"decode_expect_failure\") == 0) {\n"
    "    FILE* f = fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    uint8_t buf[256];\n"
    "    size_t n = fread(buf, 1, sizeof(buf), f);\n"
    "    fclose(f);\n"
    "    acme_telemetry_Sample_decode_result_t r = acme_telemetry_Sample_decode(buf, n);\n"
    "    return r.status == QUARRY_C_STATUS_OK ? 1 : 0;\n"
    "  }\n"
    "  return 20;\n"
    "}\n";

constexpr std::string_view kCppHarness =
    "#include \"acme/telemetry.generated.hpp\"\n"
    "#include <cstdio>\n"
    "#include <cstring>\n"
    "#include <vector>\n"
    "static bool set_fields(acme::telemetry::SampleBuilder& builder) {\n"
    "  if (!builder.set_flag(true)) return false;\n"
    "  if (!builder.set_i8(-5)) return false;\n"
    "  if (!builder.set_u8(250U)) return false;\n"
    "  if (!builder.set_i16(-1000)) return false;\n"
    "  if (!builder.set_u16(60000U)) return false;\n"
    "  if (!builder.set_i32(-100000)) return false;\n"
    "  if (!builder.set_u32(4000000000U)) return false;\n"
    "  if (!builder.set_i64(-5000000000LL)) return false;\n"
    "  if (!builder.set_u64(10000000000000ULL)) return false;\n"
    "  if (!builder.set_f32(1.5f)) return false;\n"
    "  if (!builder.set_f64(2.71828182845904)) return false;\n"
    "  if (!builder.set_status(acme::telemetry::Status::WARNING)) return false;\n"
    "  if (!builder.set_label(std::string(\"caf\\xc3\\xa9\"))) return false;\n"
    "  {\n"
    "    const std::vector<std::byte> blob_content{std::byte{0x00U}, std::byte{0xFFU},\n"
    "                                               std::byte{0x80U}};\n"
    "    if (!builder.set_blob(blob_content)) return false;\n"
    "  }\n"
    "  if (!builder.set_readings(std::vector<float>{1.5f, -2.0f})) return false;\n"
    "  if (!builder.set_statuses(std::vector<acme::telemetry::Status>{\n"
    "          acme::telemetry::Status::OK, acme::telemetry::Status::ERROR})) return false;\n"
    "  return true;\n"
    "}\n"
    "static int check_fields(const acme::telemetry::Sample& v) {\n"
    "  if (!v.has_flag() || *v.flag() != true) return 1;\n"
    "  if (!v.has_i8() || *v.i8() != -5) return 2;\n"
    "  if (!v.has_u8() || *v.u8() != 250U) return 3;\n"
    "  if (!v.has_i16() || *v.i16() != -1000) return 4;\n"
    "  if (!v.has_u16() || *v.u16() != 60000U) return 5;\n"
    "  if (!v.has_i32() || *v.i32() != -100000) return 6;\n"
    "  if (!v.has_u32() || *v.u32() != 4000000000U) return 7;\n"
    "  if (!v.has_i64() || *v.i64() != -5000000000LL) return 8;\n"
    "  if (!v.has_u64() || *v.u64() != 10000000000000ULL) return 9;\n"
    "  if (!v.has_f32() || *v.f32() != 1.5f) return 10;\n"
    "  if (!v.has_f64() || *v.f64() != 2.71828182845904) return 11;\n"
    "  if (!v.has_status() || *v.status() != acme::telemetry::Status::WARNING) return 12;\n"
    "  if (!v.has_label() || *v.label() != std::string(\"caf\\xc3\\xa9\")) return 13;\n"
    "  {\n"
    "    const std::vector<std::byte> expected_blob{std::byte{0x00U}, std::byte{0xFFU},\n"
    "                                                std::byte{0x80U}};\n"
    "    if (!v.has_blob() || *v.blob() != expected_blob) return 14;\n"
    "  }\n"
    "  if (!v.has_readings() || v.readings()->size() != 2 ||\n"
    "      (*v.readings())[0] != 1.5f || (*v.readings())[1] != -2.0f) return 15;\n"
    "  if (!v.has_statuses() || v.statuses()->size() != 2 ||\n"
    "      (*v.statuses())[0] != acme::telemetry::Status::OK ||\n"
    "      (*v.statuses())[1] != acme::telemetry::Status::ERROR) return 16;\n"
    "  return 0;\n"
    "}\n"
    "int main(int argc, char** argv) {\n"
    "  if (argc < 3) return 20;\n"
    "  if (std::strcmp(argv[1], \"encode\") == 0) {\n"
    "    acme::telemetry::SampleBuilder builder;\n"
    "    if (!set_fields(builder)) return 1;\n"
    "    auto encoded = acme::telemetry::encode(builder.build());\n"
    "    if (!encoded.has_value()) return 1;\n"
    "    FILE* f = std::fopen(argv[2], \"wb\");\n"
    "    if (!f) return 2;\n"
    "    std::fwrite(encoded->data(), 1, encoded->size(), f);\n"
    "    std::fclose(f);\n"
    "    return 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode\") == 0) {\n"
    "    FILE* f = std::fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    std::vector<std::byte> buf(256);\n"
    "    size_t n = std::fread(buf.data(), 1, buf.size(), f);\n"
    "    std::fclose(f);\n"
    "    buf.resize(n);\n"
    "    auto decoded = acme::telemetry::decode_Sample(buf);\n"
    "    if (!decoded.has_value()) return 4;\n"
    "    return check_fields(*decoded);\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode_expect_failure\") == 0) {\n"
    "    FILE* f = std::fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    std::vector<std::byte> buf(256);\n"
    "    size_t n = std::fread(buf.data(), 1, buf.size(), f);\n"
    "    std::fclose(f);\n"
    "    buf.resize(n);\n"
    "    auto decoded = acme::telemetry::decode_Sample(buf);\n"
    "    return decoded.has_value() ? 1 : 0;\n"
    "  }\n"
    "  return 20;\n"
    "}\n";

// Mirrors the C/C++ harnesses' set_fields/check_fields exactly (same
// values), and the same argv contract (encode/decode/decode_expect_failure
// <path>), so exit codes are directly comparable across all three
// languages.
constexpr std::string_view kPythonHarness =
    "import sys\n"
    "from acme.telemetry.schema import Sample, Status\n"
    "\n"
    "\n"
    "def make_sample():\n"
    "    return Sample(\n"
    "        flag=True, i8=-5, u8=250, i16=-1000, u16=60000, i32=-100000,\n"
    "        u32=4000000000, i64=-5000000000, u64=10000000000000,\n"
    "        f32=1.5, f64=2.71828182845904, status=Status.WARNING,\n"
    "        label=\"caf\\u00e9\", blob=bytes([0x00, 0xFF, 0x80]),\n"
    "        readings=[1.5, -2.0], statuses=[Status.OK, Status.ERROR],\n"
    "    )\n"
    "\n"
    "\n"
    "mode = sys.argv[1]\n"
    "path = sys.argv[2]\n"
    "\n"
    "if mode == \"encode\":\n"
    "    with open(path, \"wb\") as f:\n"
    "        f.write(make_sample().encode())\n"
    "    sys.exit(0)\n"
    "elif mode == \"decode\":\n"
    "    with open(path, \"rb\") as f:\n"
    "        data = f.read()\n"
    "    decoded = Sample.decode(data)\n"
    "    sys.exit(0 if decoded == make_sample() else 1)\n"
    "elif mode == \"decode_expect_failure\":\n"
    "    with open(path, \"rb\") as f:\n"
    "        data = f.read()\n"
    "    try:\n"
    "        Sample.decode(data)\n"
    "    except Exception:\n"
    "        sys.exit(0)\n"
    "    sys.exit(1)\n"
    "else:\n"
    "    sys.exit(20)\n";

TEST(PythonCppCCodecInteropTest, ByteForByteCompatibleAndCrossDecodable) {
    const std::string python3_executable = QUARRY_TEST_PYTHON3;
    if (python3_executable.empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping three-way interop test";
    }

    const std::filesystem::path root = make_temp_directory("scalar");
    const std::filesystem::path schema = root / "schema.brd";
    write_text_file(schema, kSchema);

    const std::filesystem::path generated_c = root / "generated_c";
    const std::filesystem::path generated_cpp = root / "generated_cpp";
    const std::filesystem::path generated_python = root / "generated_python";

    ASSERT_EQ(run_and_get_exit_code(shell_quote(QUARRY_SCHEMA_COMPILER_TOOL) +
                                    " --language c --output-directory " +
                                    shell_quote(generated_c.string()) + " " +
                                    shell_quote(schema.string())),
             0);
    ASSERT_EQ(run_and_get_exit_code(shell_quote(QUARRY_SCHEMA_COMPILER_TOOL) +
                                    " --output-directory " + shell_quote(generated_cpp.string()) +
                                    " " + shell_quote(schema.string())),
             0);
    ASSERT_EQ(run_and_get_exit_code(shell_quote(QUARRY_SCHEMA_COMPILER_TOOL) +
                                    " --language python --output-directory " +
                                    shell_quote(generated_python.string()) + " " +
                                    shell_quote(schema.string())),
             0);

    const std::filesystem::path c_harness_source = root / "c_harness.c";
    const std::filesystem::path cpp_harness_source = root / "cpp_harness.cpp";
    const std::filesystem::path python_harness_script = root / "python_harness.py";
    write_text_file(c_harness_source, kCHarness);
    write_text_file(cpp_harness_source, kCppHarness);
    write_text_file(python_harness_script, kPythonHarness);

    const std::filesystem::path c_harness_binary = root / "c_harness";
    const std::filesystem::path cpp_harness_binary = root / "cpp_harness";

    const std::filesystem::path generated_c_source =
        generated_c / "acme" / "telemetry.generated.c";
    const std::filesystem::path c_harness_object = root / "c_harness.o";
    const std::filesystem::path generated_c_object = root / "telemetry.generated.o";

    // Compile each C translation unit separately (with -c) and link them in
    // a distinct step -- see c_cpp_codec_interop_test.cpp's identical
    // comment for why (some C++ driver front ends reject -std=c99 for a
    // combined compile+link invocation).
    const std::string compile_c_harness_command =
        shell_quote(QUARRY_TEST_CXX_COMPILER) + " -x c -std=c99 -Wall -Wextra -Wpedantic -Werror -I" +
        shell_quote(generated_c.string()) + " -I" + shell_quote(QUARRY_TEST_REPO_INCLUDE_DIR) +
        " -I" + shell_quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) + " -c " +
        shell_quote(c_harness_source.string()) + " -o " + shell_quote(c_harness_object.string());
    ASSERT_EQ(run_and_get_exit_code(compile_c_harness_command), 0) << compile_c_harness_command;

    const std::string compile_generated_c_command =
        shell_quote(QUARRY_TEST_CXX_COMPILER) + " -x c -std=c99 -Wall -Wextra -Wpedantic -Werror -I" +
        shell_quote(generated_c.string()) + " -I" + shell_quote(QUARRY_TEST_REPO_INCLUDE_DIR) +
        " -I" + shell_quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) + " -c " +
        shell_quote(generated_c_source.string()) + " -o " + shell_quote(generated_c_object.string());
    ASSERT_EQ(run_and_get_exit_code(compile_generated_c_command), 0) << compile_generated_c_command;

    const std::string link_c_command = shell_quote(QUARRY_TEST_CXX_COMPILER) + " " +
                                       shell_quote(c_harness_object.string()) + " " +
                                       shell_quote(generated_c_object.string()) + " -o " +
                                       shell_quote(c_harness_binary.string());
    ASSERT_EQ(run_and_get_exit_code(link_c_command), 0) << link_c_command;

    const std::string compile_cpp_command =
        shell_quote(QUARRY_TEST_CXX_COMPILER) + " -std=c++20 -Wall -Wpedantic -I" +
        shell_quote(generated_cpp.string()) + " -I" + shell_quote(QUARRY_TEST_REPO_INCLUDE_DIR) +
        " -I" + shell_quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) + " " +
        shell_quote(cpp_harness_source.string()) + " -o " +
        shell_quote(cpp_harness_binary.string());
    ASSERT_EQ(run_and_get_exit_code(compile_cpp_command), 0) << compile_cpp_command;

    const std::string python_path =
        generated_python.string() + ":" + std::string(QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR);
    const auto run_python = [&](std::string_view mode, const std::filesystem::path& path) {
        const std::string command = "PYTHONPATH=" + shell_quote(python_path) + " " +
                                    shell_quote(python3_executable) + " " +
                                    shell_quote(python_harness_script.string()) + " " +
                                    std::string(mode) + " " + shell_quote(path.string());
        return run_and_get_exit_code(command);
    };

    const std::filesystem::path c_encoded = root / "c_encoded.bin";
    const std::filesystem::path cpp_encoded = root / "cpp_encoded.bin";
    const std::filesystem::path python_encoded = root / "python_encoded.bin";

    ASSERT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) + " encode " +
                                    shell_quote(c_encoded.string())),
             0);
    ASSERT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) + " encode " +
                                    shell_quote(cpp_encoded.string())),
             0);
    ASSERT_EQ(run_python("encode", python_encoded), 0);

    const std::string c_bytes = read_binary_file(c_encoded);
    const std::string cpp_bytes = read_binary_file(cpp_encoded);
    const std::string python_bytes = read_binary_file(python_encoded);
    ASSERT_FALSE(c_bytes.empty());
    EXPECT_EQ(c_bytes, cpp_bytes) << "C and C++ encoders produced different bytes for identical "
                                    "field values";
    EXPECT_EQ(c_bytes, python_bytes) << "C and Python encoders produced different bytes for "
                                       "identical field values";
    EXPECT_EQ(cpp_bytes, python_bytes) << "C++ and Python encoders produced different bytes for "
                                         "identical field values";

    // Cross-decode: every language decodes every other language's bytes.
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) + " decode " +
                                    shell_quote(c_encoded.string())),
             0)
        << "C++ failed to decode C-encoded bytes";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) + " decode " +
                                    shell_quote(cpp_encoded.string())),
             0)
        << "C failed to decode C++-encoded bytes";
    EXPECT_EQ(run_python("decode", c_encoded), 0) << "Python failed to decode C-encoded bytes";
    EXPECT_EQ(run_python("decode", cpp_encoded), 0) << "Python failed to decode C++-encoded bytes";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) + " decode " +
                                    shell_quote(python_encoded.string())),
             0)
        << "C failed to decode Python-encoded bytes";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) + " decode " +
                                    shell_quote(python_encoded.string())),
             0)
        << "C++ failed to decode Python-encoded bytes";

    // Truncated buffer: chop the last byte and confirm all three reject it.
    const std::filesystem::path truncated = root / "truncated.bin";
    {
        std::string truncated_bytes = c_bytes;
        ASSERT_FALSE(truncated_bytes.empty());
        truncated_bytes.pop_back();
        std::ofstream out(truncated, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(out));
        out.write(truncated_bytes.data(), static_cast<std::streamsize>(truncated_bytes.size()));
    }
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " decode_expect_failure " + shell_quote(truncated.string())),
             0)
        << "C did not reject a truncated buffer";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " decode_expect_failure " + shell_quote(truncated.string())),
             0)
        << "C++ did not reject a truncated buffer";
    EXPECT_EQ(run_python("decode_expect_failure", truncated), 0)
        << "Python did not reject a truncated buffer";

    // Extra trailing data: append one byte after a valid record and confirm
    // all three reject it.
    const std::filesystem::path trailing = root / "trailing.bin";
    {
        std::string trailing_bytes = c_bytes;
        trailing_bytes.push_back('\x00');
        std::ofstream out(trailing, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(out));
        out.write(trailing_bytes.data(), static_cast<std::streamsize>(trailing_bytes.size()));
    }
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " decode_expect_failure " + shell_quote(trailing.string())),
             0)
        << "C did not reject extra trailing data";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " decode_expect_failure " + shell_quote(trailing.string())),
             0)
        << "C++ did not reject extra trailing data";
    EXPECT_EQ(run_python("decode_expect_failure", trailing), 0)
        << "Python did not reject extra trailing data";

    // Both remaining corruption cases locate their target byte by searching
    // for the label field's own known plaintext bytes, rather than
    // hand-computing payload offsets that would need updating every time
    // the schema's field list changes.
    const std::string label_bytes = "caf\xc3\xa9";
    const std::size_t label_pos = c_bytes.find(label_bytes);
    ASSERT_NE(label_pos, std::string::npos)
        << "could not locate the label field's bytes in the encoded record";

    // Unknown enum value: corrupt the status field's one-byte payload (the
    // field immediately preceding label in declaration/field_index order,
    // so its payload byte immediately precedes label's own bytes) to a
    // value outside {0, 1, 2} and confirm all three languages reject it.
    const std::filesystem::path unknown_enum = root / "unknown_enum.bin";
    {
        std::string corrupted_bytes = c_bytes;
        ASSERT_GT(label_pos, 0U);
        corrupted_bytes[label_pos - 1] = static_cast<char>(99);
        std::ofstream out(unknown_enum, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(out));
        out.write(corrupted_bytes.data(), static_cast<std::streamsize>(corrupted_bytes.size()));
    }
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " decode_expect_failure " + shell_quote(unknown_enum.string())),
             0)
        << "C did not reject an out-of-range enum byte";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " decode_expect_failure " + shell_quote(unknown_enum.string())),
             0)
        << "C++ did not reject an out-of-range enum byte";
    EXPECT_EQ(run_python("decode_expect_failure", unknown_enum), 0)
        << "Python did not reject an out-of-range enum byte";

    // Malformed UTF-8: corrupt one byte of the label field's content (the
    // 0xA9 continuation byte of its one accented character) to an invalid
    // UTF-8 lead byte and confirm all three languages reject it.
    const std::filesystem::path malformed_utf8 = root / "malformed_utf8.bin";
    {
        std::string corrupted_bytes = c_bytes;
        corrupted_bytes[label_pos + 4] = static_cast<char>(0xFF);
        std::ofstream out(malformed_utf8, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(out));
        out.write(corrupted_bytes.data(), static_cast<std::streamsize>(corrupted_bytes.size()));
    }
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " decode_expect_failure " +
                                    shell_quote(malformed_utf8.string())),
             0)
        << "C did not reject malformed UTF-8 in the label field";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " decode_expect_failure " +
                                    shell_quote(malformed_utf8.string())),
             0)
        << "C++ did not reject malformed UTF-8 in the label field";
    EXPECT_EQ(run_python("decode_expect_failure", malformed_utf8), 0)
        << "Python did not reject malformed UTF-8 in the label field";
}

} // namespace
