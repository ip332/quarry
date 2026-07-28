// Proves byte-for-byte BRF wire compatibility between the C and C++
// backends for the scalar/enum-free subset both support (PR-108). Generates
// the same schema through both `quarry-schema-compiler` backends, compiles
// small C and C++ harness programs against each generated output, and
// verifies: (1) the C encoder's bytes are byte-for-byte identical to the
// C++ encoder's bytes for the same field values, (2) the C++ decoder
// accepts the C-encoded bytes, and (3) the C decoder accepts the
// C++-encoded bytes.

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
                                     "    type: int8\n";

constexpr std::string_view kCHarness =
    "#include \"quarry/telemetry.generated.h\"\n"
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "int main(int argc, char** argv) {\n"
    "  if (argc < 3) return 20;\n"
    "  if (strcmp(argv[1], \"encode\") == 0) {\n"
    "    quarry_telemetry_Sample_t sample;\n"
    "    quarry_telemetry_Sample_init(&sample);\n"
    "    sample.has_count = true; sample.count = 42U;\n"
    "    sample.has_ratio = true; sample.ratio = 1.5f;\n"
    "    sample.has_active = true; sample.active = true;\n"
    "    sample.has_level = true; sample.level = -5;\n"
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
    "    if (!r.value.has_count || r.value.count != 42U) return 5;\n"
    "    if (!r.value.has_ratio || r.value.ratio != 1.5f) return 6;\n"
    "    if (!r.value.has_active || r.value.active != true) return 7;\n"
    "    if (!r.value.has_level || r.value.level != -5) return 8;\n"
    "    return 0;\n"
    "  }\n"
    "  return 9;\n"
    "}\n";

constexpr std::string_view kCppHarness =
    "#include \"quarry/telemetry.generated.hpp\"\n"
    "#include <cstdio>\n"
    "#include <cstring>\n"
    "#include <fstream>\n"
    "#include <vector>\n"
    "int main(int argc, char** argv) {\n"
    "  if (argc < 3) return 20;\n"
    "  if (std::strcmp(argv[1], \"encode\") == 0) {\n"
    "    quarry::telemetry::SampleBuilder builder;\n"
    "    if (!builder.set_count(42U)) return 1;\n"
    "    if (!builder.set_ratio(1.5f)) return 1;\n"
    "    if (!builder.set_active(true)) return 1;\n"
    "    if (!builder.set_level(-5)) return 1;\n"
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
    "    std::ifstream in(argv[2], std::ios::binary);\n"
    "    if (!in) return 4;\n"
    "    std::vector<char> raw((std::istreambuf_iterator<char>(in)),\n"
    "                          std::istreambuf_iterator<char>());\n"
    "    std::vector<std::byte> bytes(raw.size());\n"
    "    for (size_t i = 0; i < raw.size(); ++i) {\n"
    "      bytes[i] = static_cast<std::byte>(raw[i]);\n"
    "    }\n"
    "    auto decoded =\n"
    "        quarry::telemetry::decode_Sample(std::span<const std::byte>(bytes));\n"
    "    if (!decoded.has_value()) return 5;\n"
    "    if (!decoded->has_count() || *decoded->count() != 42U) return 6;\n"
    "    if (!decoded->has_ratio() || *decoded->ratio() != 1.5f) return 7;\n"
    "    if (!decoded->has_active() || *decoded->active() != true) return 8;\n"
    "    if (!decoded->has_level() || *decoded->level() != -5) return 9;\n"
    "    return 0;\n"
    "  }\n"
    "  return 10;\n"
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
    EXPECT_EQ(c_bytes, cpp_bytes) << "C and C++ encoders produced different bytes for identical "
                                    "field values";

    // C encodes -> C++ decodes.
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) + " decode " +
                                    shell_quote(c_encoded.string())),
             0)
        << "C++ failed to decode C-encoded bytes";

    // C++ encodes -> C decodes.
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) + " decode " +
                                    shell_quote(cpp_encoded.string())),
             0)
        << "C failed to decode C++-encoded bytes";
}
