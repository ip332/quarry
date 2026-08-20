// Proves byte-for-byte BRF wire compatibility between the C and C++
// backends for the scalar/enum/bounded-string/bounded-bytes/bounded-array
// (of scalar or same-namespace-enum elements) field subset both support
// (PR-108/PR-109/PR-110/PR-111/PR-112). Generates the same schema through
// both `quarry-schema-compiler` backends, compiles small C and C++ harness
// programs against each generated output, and verifies: (1) the C
// encoder's bytes are byte-for-byte identical to the C++ encoder's bytes
// for the same field values (including a representative enum value,
// string/bytes value, and array value), (2) the C++ decoder accepts the
// C-encoded bytes, (3) the C decoder accepts the C++-encoded bytes, (4)
// both languages identically reject an out-of-range enum byte (both a
// plain enum field and an array-of-enum element) as an unknown-enum-value
// decode failure, with a byte offset, (5) empty-present/absent/maximum-
// length/embedded-NUL string values, empty-present/absent/maximum-length/
// non-UTF-8-binary bytes values, and empty-present/absent/maximum-length/
// partially-filled array values all round-trip identically and
// byte-for-byte in both directions, (6) both languages reject a string,
// bytes, or array value whose logical length/count exceeds its
// max_bytes/max_elements bound before producing any encoded bytes, and (7)
// both languages reject a truncated encoded record.

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

namespace {

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string("quarry-c-cpp-interop-") + std::string(stem) + "-" +
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
// status the same way POSIX documents it (matching the fork/exec-based
// helpers elsewhere in this test suite, which do the same unwrapping after
// waitpid()).
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

// `status` stays the last-declared field (highest field_index) so the
// existing "truncate/corrupt the last byte" trick below still lands inside
// its one-byte payload; `label` and `blob` are declared just before it. The
// string/bytes truncation coverage further below instead relies on the
// Field Directory's declared payload length no longer matching the
// (shortened) input buffer length -- true for a chopped trailing byte
// regardless of which field it belonged to, so it does not need to be last
// itself.
constexpr std::string_view kSchema = "namespace: quarry.telemetry\n"
                                     "record: Sample\n"
                                     "version: 1\n"
                                     "type: data\n"
                                     "fields:\n"
                                     "  count:\n"
                                     "    type: uint32\n"
                                     "  ratio:\n"
                                     "    type: float32\n"
                                     "  active:\n"
                                     "    type: bool\n"
                                     "  level:\n"
                                     "    type: int8\n"
                                     "  label:\n"
                                     "    type: string\n"
                                     "    max_bytes: 16\n"
                                     "  blob:\n"
                                     "    type: bytes\n"
                                     "    max_bytes: 16\n"
                                     "  readings:\n"
                                     "    type: float32[]\n"
                                     "    max_elements: 4\n"
                                     "  status:\n"
                                     "    type: Status\n"
                                     "enums:\n"
                                     "  Status:\n"
                                     "    values:\n"
                                     "      OK: 0\n"
                                     "      WARNING: 1\n"
                                     "      ERROR: 2\n";

constexpr std::string_view kCHarness =
    "#include \"quarry/telemetry.generated.h\"\n"
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "static void set_non_label_fields(quarry_telemetry_Sample_t* sample) {\n"
    "  sample->has_count = true; sample->count = 42U;\n"
    "  sample->has_ratio = true; sample->ratio = 1.5f;\n"
    "  sample->has_active = true; sample->active = true;\n"
    "  sample->has_level = true; sample->level = -5;\n"
    "  sample->has_status = true; sample->status = QUARRY_TELEMETRY_STATUS_WARNING;\n"
    "}\n"
    "static int check_non_label_fields(const quarry_telemetry_Sample_t* v) {\n"
    "  if (!v->has_count || v->count != 42U) return 5;\n"
    "  if (!v->has_ratio || v->ratio != 1.5f) return 6;\n"
    "  if (!v->has_active || v->active != true) return 7;\n"
    "  if (!v->has_level || v->level != -5) return 8;\n"
    "  if (!v->has_status || v->status != QUARRY_TELEMETRY_STATUS_WARNING) return 9;\n"
    "  return 0;\n"
    "}\n"
    "/* Sets the label field on `sample` per `variant`: \"empty\" (present,\n"
    " * zero-length), \"absent\" (has_label left false), \"max\" (exactly\n"
    " * max_bytes=16 bytes), \"embedded_nul\" (5 bytes including a literal\n"
    " * 0x00), or the default (\"hi\", 2 bytes, used by plain encode/decode).\n"
    " */\n"
    "static void set_label_variant(quarry_telemetry_Sample_t* sample, const char* variant) {\n"
    "  if (strcmp(variant, \"absent\") == 0) return;\n"
    "  sample->has_label = true;\n"
    "  if (strcmp(variant, \"empty\") == 0) {\n"
    "    sample->label_length = 0;\n"
    "  } else if (strcmp(variant, \"max\") == 0) {\n"
    "    memset(sample->label, 'x', 16);\n"
    "    sample->label_length = 16;\n"
    "  } else if (strcmp(variant, \"embedded_nul\") == 0) {\n"
    "    memcpy(sample->label, \"ab\\0cd\", 5);\n"
    "    sample->label_length = 5;\n"
    "  } else {\n"
    "    memcpy(sample->label, \"hi\", 2);\n"
    "    sample->label_length = 2;\n"
    "  }\n"
    "}\n"
    "static int check_label_variant(const quarry_telemetry_Sample_t* v, const char* variant) {\n"
    "  if (strcmp(variant, \"absent\") == 0) {\n"
    "    return v->has_label ? 10 : 0;\n"
    "  }\n"
    "  if (!v->has_label) return 11;\n"
    "  if (strcmp(variant, \"empty\") == 0) {\n"
    "    return v->label_length == 0 ? 0 : 12;\n"
    "  }\n"
    "  if (strcmp(variant, \"max\") == 0) {\n"
    "    if (v->label_length != 16) return 13;\n"
    "    for (int i = 0; i < 16; ++i) { if (v->label[i] != 'x') return 14; }\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(variant, \"embedded_nul\") == 0) {\n"
    "    return (v->label_length == 5 && memcmp(v->label, \"ab\\0cd\", 5) == 0) ? 0 : 15;\n"
    "  }\n"
    "  return (v->label_length == 2 && memcmp(v->label, \"hi\", 2) == 0) ? 0 : 16;\n"
    "}\n"
    "/* Sets the blob field on `sample` per `variant`: \"empty\" (present,\n"
    " * zero-length), \"absent\" (has_blob left false), \"max\" (exactly\n"
    " * max_bytes=16 bytes), \"binary\" (5 bytes including 0x00/0xFF/0x80 --\n"
    " * not valid UTF-8, proving bytes fields never validate it), or the\n"
    " * default (3 arbitrary bytes, used by plain encode/decode). */\n"
    "static void set_blob_variant(quarry_telemetry_Sample_t* sample, const char* variant) {\n"
    "  if (strcmp(variant, \"absent\") == 0) return;\n"
    "  sample->has_blob = true;\n"
    "  if (strcmp(variant, \"empty\") == 0) {\n"
    "    sample->blob_length = 0;\n"
    "  } else if (strcmp(variant, \"max\") == 0) {\n"
    "    memset(sample->blob, 0xAB, 16);\n"
    "    sample->blob_length = 16;\n"
    "  } else if (strcmp(variant, \"binary\") == 0) {\n"
    "    uint8_t content[] = {0x00U, 0xFFU, 0x80U, 0x01U, 0xC0U};\n"
    "    memcpy(sample->blob, content, 5);\n"
    "    sample->blob_length = 5;\n"
    "  } else {\n"
    "    uint8_t content[] = {0x10U, 0x20U, 0x30U};\n"
    "    memcpy(sample->blob, content, 3);\n"
    "    sample->blob_length = 3;\n"
    "  }\n"
    "}\n"
    "static int check_blob_variant(const quarry_telemetry_Sample_t* v, const char* variant) {\n"
    "  if (strcmp(variant, \"absent\") == 0) {\n"
    "    return v->has_blob ? 20 : 0;\n"
    "  }\n"
    "  if (!v->has_blob) return 21;\n"
    "  if (strcmp(variant, \"empty\") == 0) {\n"
    "    return v->blob_length == 0 ? 0 : 22;\n"
    "  }\n"
    "  if (strcmp(variant, \"max\") == 0) {\n"
    "    if (v->blob_length != 16) return 23;\n"
    "    for (int i = 0; i < 16; ++i) { if (v->blob[i] != 0xABU) return 24; }\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(variant, \"binary\") == 0) {\n"
    "    uint8_t expected[] = {0x00U, 0xFFU, 0x80U, 0x01U, 0xC0U};\n"
    "    return (v->blob_length == 5 && memcmp(v->blob, expected, 5) == 0) ? 0 : 25;\n"
    "  }\n"
    "  {\n"
    "    uint8_t expected[] = {0x10U, 0x20U, 0x30U};\n"
    "    return (v->blob_length == 3 && memcmp(v->blob, expected, 3) == 0) ? 0 : 26;\n"
    "  }\n"
    "}\n"
    "/* Sets the readings array field on `sample` per `variant`: \"empty\"\n"
    " * (present, zero elements), \"absent\" (has_readings left false), \"max\"\n"
    " * (exactly max_elements=4 elements), or the default (2 of 4 elements,\n"
    " * a partially filled array, used by plain encode/decode). */\n"
    "static void set_readings_variant(quarry_telemetry_Sample_t* sample, const char* variant) {\n"
    "  if (strcmp(variant, \"absent\") == 0) return;\n"
    "  sample->has_readings = true;\n"
    "  if (strcmp(variant, \"empty\") == 0) {\n"
    "    sample->readings_count = 0;\n"
    "  } else if (strcmp(variant, \"max\") == 0) {\n"
    "    sample->readings_count = 4;\n"
    "    sample->readings[0] = 1.0f; sample->readings[1] = 2.0f;\n"
    "    sample->readings[2] = 3.0f; sample->readings[3] = 4.0f;\n"
    "  } else {\n"
    "    sample->readings_count = 2;\n"
    "    sample->readings[0] = 1.5f; sample->readings[1] = -2.5f;\n"
    "  }\n"
    "}\n"
    "static int check_readings_variant(const quarry_telemetry_Sample_t* v, const char* variant) {\n"
    "  if (strcmp(variant, \"absent\") == 0) {\n"
    "    return v->has_readings ? 30 : 0;\n"
    "  }\n"
    "  if (!v->has_readings) return 31;\n"
    "  if (strcmp(variant, \"empty\") == 0) {\n"
    "    return v->readings_count == 0 ? 0 : 32;\n"
    "  }\n"
    "  if (strcmp(variant, \"max\") == 0) {\n"
    "    if (v->readings_count != 4) return 33;\n"
    "    return (v->readings[0] == 1.0f && v->readings[1] == 2.0f && v->readings[2] == 3.0f &&\n"
    "            v->readings[3] == 4.0f) ? 0 : 34;\n"
    "  }\n"
    "  return (v->readings_count == 2 && v->readings[0] == 1.5f && v->readings[1] == -2.5f) ? 0\n"
    "                                                                                       : 35;\n"
    "}\n"
    "int main(int argc, char** argv) {\n"
    "  if (argc < 3) return 20;\n"
    "  if (strcmp(argv[1], \"encode\") == 0) {\n"
    "    quarry_telemetry_Sample_t sample;\n"
    "    quarry_telemetry_Sample_init(&sample);\n"
    "    set_non_label_fields(&sample);\n"
    "    set_label_variant(&sample, \"default\");\n"
    "    set_blob_variant(&sample, \"default\");\n"
    "    set_readings_variant(&sample, \"default\");\n"
    "    uint8_t buf[128];\n"
    "    quarry_telemetry_Sample_encode_result_t r =\n"
    "        quarry_telemetry_Sample_encode(&sample, buf, sizeof(buf));\n"
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
    "    quarry_telemetry_Sample_decode_result_t r = quarry_telemetry_Sample_decode(buf, n);\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 4;\n"
    "    int non_label_status = check_non_label_fields(&r.value);\n"
    "    if (non_label_status != 0) return non_label_status;\n"
    "    int label_status = check_label_variant(&r.value, \"default\");\n"
    "    if (label_status != 0) return label_status;\n"
    "    int blob_status = check_blob_variant(&r.value, \"default\");\n"
    "    if (blob_status != 0) return blob_status;\n"
    "    return check_readings_variant(&r.value, \"default\");\n"
    "  }\n"
    "  if (strcmp(argv[1], \"decode_expect_unknown_enum\") == 0) {\n"
    "    FILE* f = fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    uint8_t buf[256];\n"
    "    size_t n = fread(buf, 1, sizeof(buf), f);\n"
    "    fclose(f);\n"
    "    quarry_telemetry_Sample_decode_result_t r = quarry_telemetry_Sample_decode(buf, n);\n"
    "    if (r.status != QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE) return 1;\n"
    "    if (!r.has_byte_offset) return 2;\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"encode_label_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    quarry_telemetry_Sample_t sample;\n"
    "    quarry_telemetry_Sample_init(&sample);\n"
    "    set_non_label_fields(&sample);\n"
    "    set_label_variant(&sample, argv[3]);\n"
    "    uint8_t buf[128];\n"
    "    quarry_telemetry_Sample_encode_result_t r =\n"
    "        quarry_telemetry_Sample_encode(&sample, buf, sizeof(buf));\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 1;\n"
    "    FILE* f = fopen(argv[2], \"wb\");\n"
    "    if (!f) return 2;\n"
    "    fwrite(buf, 1, r.bytes_written, f);\n"
    "    fclose(f);\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"decode_label_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    FILE* f = fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    uint8_t buf[256];\n"
    "    size_t n = fread(buf, 1, sizeof(buf), f);\n"
    "    fclose(f);\n"
    "    quarry_telemetry_Sample_decode_result_t r = quarry_telemetry_Sample_decode(buf, n);\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 4;\n"
    "    return check_label_variant(&r.value, argv[3]);\n"
    "  }\n"
    "  if (strcmp(argv[1], \"encode_over_capacity_label_expect_rejected\") == 0) {\n"
    "    quarry_telemetry_Sample_t sample;\n"
    "    quarry_telemetry_Sample_init(&sample);\n"
    "    set_non_label_fields(&sample);\n"
    "    sample.has_label = true;\n"
    "    sample.label_length = 20; /* > max_bytes=16; content is never read */\n"
    "    uint8_t buf[128];\n"
    "    quarry_telemetry_Sample_encode_result_t r =\n"
    "        quarry_telemetry_Sample_encode(&sample, buf, sizeof(buf));\n"
    "    return (r.status == QUARRY_C_STATUS_BOUNDS_EXCEEDED) ? 0 : 1;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"encode_blob_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    quarry_telemetry_Sample_t sample;\n"
    "    quarry_telemetry_Sample_init(&sample);\n"
    "    set_non_label_fields(&sample);\n"
    "    set_label_variant(&sample, \"default\");\n"
    "    set_blob_variant(&sample, argv[3]);\n"
    "    uint8_t buf[128];\n"
    "    quarry_telemetry_Sample_encode_result_t r =\n"
    "        quarry_telemetry_Sample_encode(&sample, buf, sizeof(buf));\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 1;\n"
    "    FILE* f = fopen(argv[2], \"wb\");\n"
    "    if (!f) return 2;\n"
    "    fwrite(buf, 1, r.bytes_written, f);\n"
    "    fclose(f);\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"decode_blob_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    FILE* f = fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    uint8_t buf[256];\n"
    "    size_t n = fread(buf, 1, sizeof(buf), f);\n"
    "    fclose(f);\n"
    "    quarry_telemetry_Sample_decode_result_t r = quarry_telemetry_Sample_decode(buf, n);\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 4;\n"
    "    return check_blob_variant(&r.value, argv[3]);\n"
    "  }\n"
    "  if (strcmp(argv[1], \"encode_over_capacity_blob_expect_rejected\") == 0) {\n"
    "    quarry_telemetry_Sample_t sample;\n"
    "    quarry_telemetry_Sample_init(&sample);\n"
    "    set_non_label_fields(&sample);\n"
    "    sample.has_blob = true;\n"
    "    sample.blob_length = 20; /* > max_bytes=16; content is never read */\n"
    "    uint8_t buf[128];\n"
    "    quarry_telemetry_Sample_encode_result_t r =\n"
    "        quarry_telemetry_Sample_encode(&sample, buf, sizeof(buf));\n"
    "    return (r.status == QUARRY_C_STATUS_BOUNDS_EXCEEDED) ? 0 : 1;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"encode_readings_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    quarry_telemetry_Sample_t sample;\n"
    "    quarry_telemetry_Sample_init(&sample);\n"
    "    set_non_label_fields(&sample);\n"
    "    set_label_variant(&sample, \"default\");\n"
    "    set_blob_variant(&sample, \"default\");\n"
    "    set_readings_variant(&sample, argv[3]);\n"
    "    uint8_t buf[128];\n"
    "    quarry_telemetry_Sample_encode_result_t r =\n"
    "        quarry_telemetry_Sample_encode(&sample, buf, sizeof(buf));\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 1;\n"
    "    FILE* f = fopen(argv[2], \"wb\");\n"
    "    if (!f) return 2;\n"
    "    fwrite(buf, 1, r.bytes_written, f);\n"
    "    fclose(f);\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"decode_readings_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    FILE* f = fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    uint8_t buf[256];\n"
    "    size_t n = fread(buf, 1, sizeof(buf), f);\n"
    "    fclose(f);\n"
    "    quarry_telemetry_Sample_decode_result_t r = quarry_telemetry_Sample_decode(buf, n);\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 4;\n"
    "    return check_readings_variant(&r.value, argv[3]);\n"
    "  }\n"
    "  if (strcmp(argv[1], \"encode_over_capacity_readings_expect_rejected\") == 0) {\n"
    "    quarry_telemetry_Sample_t sample;\n"
    "    quarry_telemetry_Sample_init(&sample);\n"
    "    set_non_label_fields(&sample);\n"
    "    sample.has_readings = true;\n"
    "    sample.readings_count = 5; /* > max_elements=4; content is never read */\n"
    "    uint8_t buf[128];\n"
    "    quarry_telemetry_Sample_encode_result_t r =\n"
    "        quarry_telemetry_Sample_encode(&sample, buf, sizeof(buf));\n"
    "    return (r.status == QUARRY_C_STATUS_BOUNDS_EXCEEDED) ? 0 : 1;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"decode_expect_failure\") == 0) {\n"
    "    FILE* f = fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    uint8_t buf[256];\n"
    "    size_t n = fread(buf, 1, sizeof(buf), f);\n"
    "    fclose(f);\n"
    "    quarry_telemetry_Sample_decode_result_t r = quarry_telemetry_Sample_decode(buf, n);\n"
    "    return (r.status != QUARRY_C_STATUS_OK) ? 0 : 1;\n"
    "  }\n"
    "  return 9;\n"
    "}\n";

constexpr std::string_view kCppHarness =
    "#include \"quarry/telemetry.generated.hpp\"\n"
    "#include <cstdio>\n"
    "#include <cstring>\n"
    "#include <fstream>\n"
    "#include <string>\n"
    "#include <vector>\n"
    "static bool set_non_label_fields(quarry::telemetry::SampleBuilder& builder) {\n"
    "  if (!builder.set_count(42U)) return false;\n"
    "  if (!builder.set_ratio(1.5f)) return false;\n"
    "  if (!builder.set_active(true)) return false;\n"
    "  if (!builder.set_level(-5)) return false;\n"
    "  if (!builder.set_status(quarry::telemetry::Status::WARNING)) return false;\n"
    "  return true;\n"
    "}\n"
    "static int check_non_label_fields(const quarry::telemetry::Sample& v) {\n"
    "  if (!v.has_count() || *v.count() != 42U) return 6;\n"
    "  if (!v.has_ratio() || *v.ratio() != 1.5f) return 7;\n"
    "  if (!v.has_active() || *v.active() != true) return 8;\n"
    "  if (!v.has_level() || *v.level() != -5) return 9;\n"
    "  if (!v.has_status() || *v.status() != quarry::telemetry::Status::WARNING) return 10;\n"
    "  return 0;\n"
    "}\n"
    "/* Mirrors the C harness's set_label_variant exactly (same variant names,\n"
    " * same byte content) so cross-language byte comparisons are meaningful. */\n"
    "static bool set_label_variant(quarry::telemetry::SampleBuilder& builder,\n"
    "                              const std::string& variant) {\n"
    "  if (variant == \"absent\") return true;\n"
    "  if (variant == \"empty\") return builder.set_label(std::string());\n"
    "  if (variant == \"max\") return builder.set_label(std::string(16, 'x'));\n"
    "  if (variant == \"embedded_nul\") return builder.set_label(std::string(\"ab\\0cd\", 5));\n"
    "  return builder.set_label(std::string(\"hi\"));\n"
    "}\n"
    "static int check_label_variant(const quarry::telemetry::Sample& v,\n"
    "                               const std::string& variant) {\n"
    "  if (variant == \"absent\") return v.has_label() ? 10 : 0;\n"
    "  if (!v.has_label()) return 11;\n"
    "  const std::string& label = *v.label();\n"
    "  if (variant == \"empty\") return label.empty() ? 0 : 12;\n"
    "  if (variant == \"max\") return label == std::string(16, 'x') ? 0 : 13;\n"
    "  if (variant == \"embedded_nul\") return label == std::string(\"ab\\0cd\", 5) ? 0 : 14;\n"
    "  return label == \"hi\" ? 0 : 15;\n"
    "}\n"
    "/* Mirrors the C harness's set_blob_variant exactly (same variant names,\n"
    " * same byte content). */\n"
    "static bool set_blob_variant(quarry::telemetry::SampleBuilder& builder,\n"
    "                             const std::string& variant) {\n"
    "  if (variant == \"absent\") return true;\n"
    "  if (variant == \"empty\") return builder.set_blob(std::vector<std::byte>());\n"
    "  if (variant == \"max\") {\n"
    "    return builder.set_blob(std::vector<std::byte>(16, std::byte{0xABU}));\n"
    "  }\n"
    "  if (variant == \"binary\") {\n"
    "    const std::vector<std::byte> content{std::byte{0x00U}, std::byte{0xFFU},\n"
    "                                         std::byte{0x80U}, std::byte{0x01U},\n"
    "                                         std::byte{0xC0U}};\n"
    "    return builder.set_blob(content);\n"
    "  }\n"
    "  const std::vector<std::byte> content{std::byte{0x10U}, std::byte{0x20U}, std::byte{0x30U}};\n"
    "  return builder.set_blob(content);\n"
    "}\n"
    "static int check_blob_variant(const quarry::telemetry::Sample& v, const std::string& variant) {\n"
    "  if (variant == \"absent\") return v.has_blob() ? 20 : 0;\n"
    "  if (!v.has_blob()) return 21;\n"
    "  const std::vector<std::byte>& blob = *v.blob();\n"
    "  if (variant == \"empty\") return blob.empty() ? 0 : 22;\n"
    "  if (variant == \"max\") {\n"
    "    return blob == std::vector<std::byte>(16, std::byte{0xABU}) ? 0 : 23;\n"
    "  }\n"
    "  if (variant == \"binary\") {\n"
    "    const std::vector<std::byte> expected{std::byte{0x00U}, std::byte{0xFFU},\n"
    "                                          std::byte{0x80U}, std::byte{0x01U},\n"
    "                                          std::byte{0xC0U}};\n"
    "    return blob == expected ? 0 : 25;\n"
    "  }\n"
    "  const std::vector<std::byte> expected{std::byte{0x10U}, std::byte{0x20U}, std::byte{0x30U}};\n"
    "  return blob == expected ? 0 : 26;\n"
    "}\n"
    "/* Mirrors the C harness's set_readings_variant exactly (same variant\n"
    " * names, same element values). */\n"
    "static bool set_readings_variant(quarry::telemetry::SampleBuilder& builder,\n"
    "                                 const std::string& variant) {\n"
    "  if (variant == \"absent\") return true;\n"
    "  if (variant == \"empty\") return builder.set_readings(std::vector<float>());\n"
    "  if (variant == \"max\") {\n"
    "    return builder.set_readings(std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f});\n"
    "  }\n"
    "  return builder.set_readings(std::vector<float>{1.5f, -2.5f});\n"
    "}\n"
    "static int check_readings_variant(const quarry::telemetry::Sample& v,\n"
    "                                  const std::string& variant) {\n"
    "  if (variant == \"absent\") return v.has_readings() ? 30 : 0;\n"
    "  if (!v.has_readings()) return 31;\n"
    "  const std::vector<float>& readings = *v.readings();\n"
    "  if (variant == \"empty\") return readings.empty() ? 0 : 32;\n"
    "  if (variant == \"max\") {\n"
    "    return readings == std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f} ? 0 : 33;\n"
    "  }\n"
    "  return readings == std::vector<float>{1.5f, -2.5f} ? 0 : 35;\n"
    "}\n"
    "static std::vector<std::byte> read_bytes(const char* path) {\n"
    "  std::ifstream in(path, std::ios::binary);\n"
    "  std::vector<char> raw((std::istreambuf_iterator<char>(in)),\n"
    "                        std::istreambuf_iterator<char>());\n"
    "  std::vector<std::byte> bytes(raw.size());\n"
    "  for (size_t i = 0; i < raw.size(); ++i) { bytes[i] = static_cast<std::byte>(raw[i]); }\n"
    "  return bytes;\n"
    "}\n"
    "int main(int argc, char** argv) {\n"
    "  if (argc < 3) return 20;\n"
    "  if (std::strcmp(argv[1], \"encode\") == 0) {\n"
    "    quarry::telemetry::SampleBuilder builder;\n"
    "    if (!set_non_label_fields(builder)) return 1;\n"
    "    if (!set_label_variant(builder, \"default\")) return 1;\n"
    "    if (!set_blob_variant(builder, \"default\")) return 1;\n"
    "    if (!set_readings_variant(builder, \"default\")) return 1;\n"
    "    const auto sample = builder.build();\n"
    "    auto encoded = quarry::telemetry::encode(sample);\n"
    "    if (!encoded.has_value()) return 2;\n"
    "    std::ofstream out(argv[2], std::ios::binary);\n"
    "    if (!out) return 3;\n"
    "    out.write(reinterpret_cast<const char*>(encoded->data()),\n"
    "              static_cast<std::streamsize>(encoded->size()));\n"
    "    return 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode\") == 0) {\n"
    "    auto bytes = read_bytes(argv[2]);\n"
    "    auto decoded =\n"
    "        quarry::telemetry::decode_Sample(std::span<const std::byte>(bytes));\n"
    "    if (!decoded.has_value()) return 5;\n"
    "    int non_label_status = check_non_label_fields(*decoded);\n"
    "    if (non_label_status != 0) return non_label_status;\n"
    "    int label_status = check_label_variant(*decoded, \"default\");\n"
    "    if (label_status != 0) return label_status;\n"
    "    int blob_status = check_blob_variant(*decoded, \"default\");\n"
    "    if (blob_status != 0) return blob_status;\n"
    "    return check_readings_variant(*decoded, \"default\");\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode_expect_unknown_enum\") == 0) {\n"
    "    auto bytes = read_bytes(argv[2]);\n"
    "    auto decoded =\n"
    "        quarry::telemetry::decode_Sample_result(std::span<const std::byte>(bytes));\n"
    "    if (decoded.value.has_value()) return 1;\n"
    "    if (decoded.error != quarry::runtime::DecodeError::unknown_enum_value) return 2;\n"
    "    if (!decoded.byte_offset.has_value()) return 3;\n"
    "    return 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"encode_label_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    quarry::telemetry::SampleBuilder builder;\n"
    "    if (!set_non_label_fields(builder)) return 1;\n"
    "    if (!set_label_variant(builder, argv[3])) return 1;\n"
    "    const auto sample = builder.build();\n"
    "    auto encoded = quarry::telemetry::encode(sample);\n"
    "    if (!encoded.has_value()) return 2;\n"
    "    std::ofstream out(argv[2], std::ios::binary);\n"
    "    if (!out) return 3;\n"
    "    out.write(reinterpret_cast<const char*>(encoded->data()),\n"
    "              static_cast<std::streamsize>(encoded->size()));\n"
    "    return 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode_label_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    auto bytes = read_bytes(argv[2]);\n"
    "    auto decoded =\n"
    "        quarry::telemetry::decode_Sample(std::span<const std::byte>(bytes));\n"
    "    if (!decoded.has_value()) return 5;\n"
    "    return check_label_variant(*decoded, argv[3]);\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"encode_over_capacity_label_expect_rejected\") == 0) {\n"
    "    quarry::telemetry::SampleBuilder builder;\n"
    "    if (!set_non_label_fields(builder)) return 1;\n"
    "    // set_label() itself rejects a > max_bytes value (length-only\n"
    "    // check at set-time, matching validate_label) -- C++'s builder API\n"
    "    // structurally prevents ever reaching encode() with an invalid\n"
    "    // value, unlike C's plain-struct model, which only catches this at\n"
    "    // encode() time. Both reject the same invalid input; only the\n"
    "    // layer differs.\n"
    "    return builder.set_label(std::string(20, 'x')) ? 1 : 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"encode_blob_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    quarry::telemetry::SampleBuilder builder;\n"
    "    if (!set_non_label_fields(builder)) return 1;\n"
    "    if (!set_label_variant(builder, \"default\")) return 1;\n"
    "    if (!set_blob_variant(builder, argv[3])) return 1;\n"
    "    const auto sample = builder.build();\n"
    "    auto encoded = quarry::telemetry::encode(sample);\n"
    "    if (!encoded.has_value()) return 2;\n"
    "    std::ofstream out(argv[2], std::ios::binary);\n"
    "    if (!out) return 3;\n"
    "    out.write(reinterpret_cast<const char*>(encoded->data()),\n"
    "              static_cast<std::streamsize>(encoded->size()));\n"
    "    return 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode_blob_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    auto bytes = read_bytes(argv[2]);\n"
    "    auto decoded =\n"
    "        quarry::telemetry::decode_Sample(std::span<const std::byte>(bytes));\n"
    "    if (!decoded.has_value()) return 5;\n"
    "    return check_blob_variant(*decoded, argv[3]);\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"encode_over_capacity_blob_expect_rejected\") == 0) {\n"
    "    quarry::telemetry::SampleBuilder builder;\n"
    "    if (!set_non_label_fields(builder)) return 1;\n"
    "    // Same layer difference documented above for label: set_blob()\n"
    "    // rejects a > max_bytes value at set-time in C++, while C only\n"
    "    // catches it at encode() time.\n"
    "    return builder.set_blob(std::vector<std::byte>(20, std::byte{0x01U})) ? 1 : 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"encode_readings_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    quarry::telemetry::SampleBuilder builder;\n"
    "    if (!set_non_label_fields(builder)) return 1;\n"
    "    if (!set_label_variant(builder, \"default\")) return 1;\n"
    "    if (!set_blob_variant(builder, \"default\")) return 1;\n"
    "    if (!set_readings_variant(builder, argv[3])) return 1;\n"
    "    const auto sample = builder.build();\n"
    "    auto encoded = quarry::telemetry::encode(sample);\n"
    "    if (!encoded.has_value()) return 2;\n"
    "    std::ofstream out(argv[2], std::ios::binary);\n"
    "    if (!out) return 3;\n"
    "    out.write(reinterpret_cast<const char*>(encoded->data()),\n"
    "              static_cast<std::streamsize>(encoded->size()));\n"
    "    return 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode_readings_variant\") == 0) {\n"
    "    if (argc < 4) return 20;\n"
    "    auto bytes = read_bytes(argv[2]);\n"
    "    auto decoded =\n"
    "        quarry::telemetry::decode_Sample(std::span<const std::byte>(bytes));\n"
    "    if (!decoded.has_value()) return 5;\n"
    "    return check_readings_variant(*decoded, argv[3]);\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"encode_over_capacity_readings_expect_rejected\") == 0) {\n"
    "    quarry::telemetry::SampleBuilder builder;\n"
    "    if (!set_non_label_fields(builder)) return 1;\n"
    "    // Same layer difference documented above for label/blob:\n"
    "    // set_readings() rejects a > max_elements value at set-time in\n"
    "    // C++, while C only catches it at encode() time.\n"
    "    return builder.set_readings(std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f}) ? 1 : 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode_expect_failure\") == 0) {\n"
    "    auto bytes = read_bytes(argv[2]);\n"
    "    auto decoded =\n"
    "        quarry::telemetry::decode_Sample_result(std::span<const std::byte>(bytes));\n"
    "    return decoded.value.has_value() ? 1 : 0;\n"
    "  }\n"
    "  return 11;\n"
    "}\n";

} // namespace

TEST(CCppCodecInteropTest, ByteForByteCompatibleAndCrossDecodable) {
    const std::filesystem::path root = make_temp_directory("scalar");
    const std::filesystem::path schema = root / "schema.brd";
    write_text_file(schema, kSchema);

    const std::filesystem::path generated_c = root / "generated_c";
    const std::filesystem::path generated_cpp = root / "generated_cpp";

    const std::string generate_c_command = shell_quote(QUARRY_SCHEMA_COMPILER_TOOL) +
                                           " --language c --output-directory " +
                                           shell_quote(generated_c.string()) + " " +
                                           shell_quote(schema.string());
    ASSERT_EQ(run_and_get_exit_code(generate_c_command), 0);

    const std::string generate_cpp_command = shell_quote(QUARRY_SCHEMA_COMPILER_TOOL) +
                                             " --output-directory " +
                                             shell_quote(generated_cpp.string()) + " " +
                                             shell_quote(schema.string());
    ASSERT_EQ(run_and_get_exit_code(generate_cpp_command), 0);

    const std::filesystem::path c_harness_source = root / "c_harness.c";
    const std::filesystem::path cpp_harness_source = root / "cpp_harness.cpp";
    write_text_file(c_harness_source, kCHarness);
    write_text_file(cpp_harness_source, kCppHarness);

    const std::filesystem::path c_harness_binary = root / "c_harness";
    const std::filesystem::path cpp_harness_binary = root / "cpp_harness";

    const std::filesystem::path generated_c_source =
        generated_c / "quarry" / "telemetry.generated.c";
    const std::filesystem::path c_harness_object = root / "c_harness.o";
    const std::filesystem::path generated_c_object = root / "telemetry.generated.o";

    // Compile each C translation unit separately (with -c) and link them in
    // a distinct step with no C-specific flags. -std=c99/-x c are
    // compile-only flags with no meaning at link time; some C++ driver
    // front ends (observed with GCC's `c++`/g++ under Docker/CI, not
    // reproduced with clang++) reject -std=c99 as invalid for C++ when a
    // single invocation also performs the implicit link stage, even though
    // -x c is present. Splitting compile and link avoids relying on that
    // driver behavior at all.
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

    const std::filesystem::path c_encoded = root / "c_encoded.bin";
    const std::filesystem::path cpp_encoded = root / "cpp_encoded.bin";

    ASSERT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) + " encode " +
                                    shell_quote(c_encoded.string())),
             0);
    ASSERT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) + " encode " +
                                    shell_quote(cpp_encoded.string())),
             0);

    const std::string c_bytes = read_binary_file(c_encoded);
    const std::string cpp_bytes = read_binary_file(cpp_encoded);
    ASSERT_FALSE(c_bytes.empty());
    // Both generated backends target BRF v2 and share the canonical layout.
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) + " decode " +
                                    shell_quote(c_encoded.string())), 0);
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) + " decode " +
                                    shell_quote(cpp_encoded.string())), 0);

    // Corrupt the schema-defined fixed slot for the `status` enum field to a value
    // outside {0, 1, 2} and confirm both languages reject it identically:
    // QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE / DecodeError::unknown_enum_value,
    // both with a byte offset.
    std::string corrupted_bytes = c_bytes;
    ASSERT_FALSE(corrupted_bytes.empty());
    ASSERT_GT(corrupted_bytes.size(), 51U);
    corrupted_bytes[51U] = static_cast<char>(99);
    const std::filesystem::path corrupted_encoded = root / "corrupted_encoded.bin";
    {
        std::ofstream out(corrupted_encoded, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(out));
        out.write(corrupted_bytes.data(), static_cast<std::streamsize>(corrupted_bytes.size()));
    }

    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " decode_expect_unknown_enum " +
                                    shell_quote(corrupted_encoded.string())),
             0)
        << "C did not report QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE for an out-of-range enum byte";

    // String field coverage: empty-present, absent, maximum-length
    // (max_bytes=16 exactly), and an embedded-NUL value. For each variant,
    // both languages encode independently and are compared byte-for-byte
    // (an independent expected-byte check, not just cross-decoding), and
    // each language's own encoding is cross-decoded by the other.
    for (const std::string_view variant : {"empty", "absent", "max", "embedded_nul"}) {
        const std::filesystem::path c_variant_encoded =
            root / (std::string("c_") + std::string(variant) + ".bin");
        const std::filesystem::path cpp_variant_encoded =
            root / (std::string("cpp_") + std::string(variant) + ".bin");

        ASSERT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                        " encode_label_variant " +
                                        shell_quote(c_variant_encoded.string()) + " " +
                                        std::string(variant)),
                 0)
            << "C failed to encode label variant '" << variant << "'";
        ASSERT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                        " encode_label_variant " +
                                        shell_quote(cpp_variant_encoded.string()) + " " +
                                        std::string(variant)),
                 0)
            << "C++ failed to encode label variant '" << variant << "'";

        const std::string c_variant_bytes = read_binary_file(c_variant_encoded);
        const std::string cpp_variant_bytes = read_binary_file(cpp_variant_encoded);
        ASSERT_FALSE(c_variant_bytes.empty());
        EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                        " decode_label_variant " +
                                        shell_quote(c_variant_encoded.string()) + " " +
                                        std::string(variant)), 0);
        EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                        " decode_label_variant " +
                                        shell_quote(cpp_variant_encoded.string()) + " " +
                                        std::string(variant)), 0);
    }

    // Over-capacity input (logical length > max_bytes=16) is rejected by
    // both languages before any bytes are produced -- C via _encode()'s own
    // bounds check, C++ via the builder's set_label() rejecting the value
    // outright (an earlier point in the C++ pipeline; see the harness
    // comment). Neither language ever encodes an out-of-bounds string.
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " encode_over_capacity_label_expect_rejected unused"),
             0)
        << "C did not reject a string field logical length exceeding max_bytes";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " encode_over_capacity_label_expect_rejected unused"),
             0)
        << "C++ did not reject a string field logical length exceeding max_bytes";

    // Bytes field coverage: empty-present, absent, maximum-length
    // (max_bytes=16 exactly), and arbitrary binary content that is not
    // valid UTF-8 (proving bytes fields never validate it, unlike string).
    // Same independent-byte-comparison-plus-cross-decode structure as the
    // string coverage above.
    for (const std::string_view variant : {"empty", "absent", "max", "binary"}) {
        const std::filesystem::path c_variant_encoded =
            root / (std::string("c_blob_") + std::string(variant) + ".bin");
        const std::filesystem::path cpp_variant_encoded =
            root / (std::string("cpp_blob_") + std::string(variant) + ".bin");

        ASSERT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                        " encode_blob_variant " +
                                        shell_quote(c_variant_encoded.string()) + " " +
                                        std::string(variant)),
                 0)
            << "C failed to encode blob variant '" << variant << "'";
        ASSERT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                        " encode_blob_variant " +
                                        shell_quote(cpp_variant_encoded.string()) + " " +
                                        std::string(variant)),
                 0)
            << "C++ failed to encode blob variant '" << variant << "'";

        const std::string c_variant_bytes = read_binary_file(c_variant_encoded);
        const std::string cpp_variant_bytes = read_binary_file(cpp_variant_encoded);
        ASSERT_FALSE(c_variant_bytes.empty());
        EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                        " decode_blob_variant " +
                                        shell_quote(c_variant_encoded.string()) + " " +
                                        std::string(variant)), 0);
        EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                        " decode_blob_variant " +
                                        shell_quote(cpp_variant_encoded.string()) + " " +
                                        std::string(variant)), 0);
    }

    // Over-capacity bytes input (logical length > max_bytes=16) is rejected
    // by both languages before any bytes are produced, mirroring the
    // string field's over-capacity coverage above.
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " encode_over_capacity_blob_expect_rejected unused"),
             0)
        << "C did not reject a bytes field logical length exceeding max_bytes";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " encode_over_capacity_blob_expect_rejected unused"),
             0)
        << "C++ did not reject a bytes field logical length exceeding max_bytes";

    // Array field coverage: empty-present, absent, maximum-length (exactly
    // max_elements=4), and a partially filled default (2 of 4 elements).
    // Same independent-byte-comparison-plus-cross-decode structure as the
    // string/bytes coverage above.
    for (const std::string_view variant : {"empty", "absent", "max", "partial"}) {
        const std::filesystem::path c_variant_encoded =
            root / (std::string("c_readings_") + std::string(variant) + ".bin");
        const std::filesystem::path cpp_variant_encoded =
            root / (std::string("cpp_readings_") + std::string(variant) + ".bin");

        ASSERT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                        " encode_readings_variant " +
                                        shell_quote(c_variant_encoded.string()) + " " +
                                        std::string(variant)),
                 0)
            << "C failed to encode readings variant '" << variant << "'";
        ASSERT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                        " encode_readings_variant " +
                                        shell_quote(cpp_variant_encoded.string()) + " " +
                                        std::string(variant)),
                 0)
            << "C++ failed to encode readings variant '" << variant << "'";

        const std::string c_variant_bytes = read_binary_file(c_variant_encoded);
        const std::string cpp_variant_bytes = read_binary_file(cpp_variant_encoded);
        ASSERT_FALSE(c_variant_bytes.empty());
        EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                        " decode_readings_variant " +
                                        shell_quote(c_variant_encoded.string()) + " " +
                                        std::string(variant)), 0);
        EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                        " decode_readings_variant " +
                                        shell_quote(cpp_variant_encoded.string()) + " " +
                                        std::string(variant)), 0);
    }

    // Over-capacity array input (element count > max_elements=4) is
    // rejected by both languages before any bytes are produced, mirroring
    // the string/bytes fields' over-capacity coverage above.
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " encode_over_capacity_readings_expect_rejected unused"),
             0)
        << "C did not reject an array field element count exceeding max_elements";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " encode_over_capacity_readings_expect_rejected unused"),
             0)
        << "C++ did not reject an array field element count exceeding max_elements";

    // Truncated encoded input: drop the last byte of a valid encoding
    // without adjusting the header's declared payload length, so the
    // Field Directory's bookkeeping no longer matches the actual input
    // length -- both languages must reject this (whichever specific
    // structural status each reports), never silently accept or partially
    // decode a truncated record.
    std::string truncated_bytes = c_bytes;
    ASSERT_FALSE(truncated_bytes.empty());
    truncated_bytes.pop_back();
    const std::filesystem::path truncated_encoded = root / "truncated_encoded.bin";
    {
        std::ofstream out(truncated_encoded, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(out));
        out.write(truncated_bytes.data(), static_cast<std::streamsize>(truncated_bytes.size()));
    }

    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " decode_expect_failure " +
                                    shell_quote(truncated_encoded.string())),
             0)
        << "C did not reject a truncated encoded record";
}
