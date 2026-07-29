// Proves byte-for-byte BRF wire compatibility between the C and C++
// backends for a same-namespace nested record field (PR-113).
//
// Unlike tests/interop/c_cpp_codec_interop_test.cpp, this test cannot drive
// the schema through the YAML frontend and the installed `quarry-schema-
// compiler` CLI tool: the YAML frontend supports exactly one `record:` per
// schema file (CLAUDE.md's "one primary record per schema file"; there is no
// import/cross-file resolution pass -- compiler/imports/ is reserved but
// inactive), so a second, nestable record type is not expressible through
// any `.brd` document today. This is a genuine, pre-existing frontend
// limitation, not something introduced or fixable within backend_c's scope
// (see compiler/backend_c/README.md's "Nested record fields" section).
//
// Instead, this test builds the Schema IR directly (the same technique
// tests/backend/backend_codegen_test.cpp already uses for its own nested-
// record fixture, tests/fixtures/backend/schema_ir/forward_record_reference
// .pbtxt) and calls both quarry::compiler::backend::Backend::generate() (C++)
// and quarry::compiler::backend_c::Backend::generate() (C) directly, in-
// process, to obtain real generated code -- then compiles and runs small C
// and C++ harnesses against that real generated code, exactly like the
// YAML-driven interop test does downstream of code generation. The schema
// (record A embeds record B by value) intentionally mirrors
// forward_record_reference.pbtxt exactly, so this test's expected encoded
// byte sequences are the same ones already independently proven correct by
// BackendCodegenTest.NestedRecordFieldUsesCompleteEmbeddedRecord.
//
// Verifies: (1) the C and C++ encoders produce byte-for-byte identical
// output for an empty-but-present nested child and (2) for a populated
// nested child, (3) the C++ decoder accepts C-encoded bytes and the C
// decoder accepts C++-encoded bytes in both cases, and (4) both languages
// reject a nested payload with the wrong embedded record id and a nested
// payload with a truncated embedded header.

#include "compiler/backend/backend.hpp"
#include "compiler/backend_c/backend_c.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/wait.h>
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

using quarry::compiler::context::CompilerContext;
using quarry::compiler::diagnostics::DiagnosticEngine;
using quarry::compiler::schema_ir::SchemaIrModel;
using quarry::compiler::schema_ir::SchemaIrValidator;
using ::quarry::schema_ir::NamespaceIR;
using ::quarry::schema_ir::RecordIR;

namespace cpp_backend = quarry::compiler::backend;
namespace c_backend = quarry::compiler::backend_c;

// Mirrors tests/fixtures/backend/schema_ir/forward_record_reference.pbtxt
// exactly: root-namespace record A (record_id 1) embeds root-namespace
// record B (record_id 2) by value via field "value"; B has a single u32
// field "count". A is declared before B in Schema IR order, so this schema
// also exercises backend_c's topological reordering (see
// order_records_topologically in compiler/backend_c/backend_c.cpp).
[[nodiscard]] SchemaIrModel make_nested_record_schema_ir() {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);

    RecordIR* a = root->add_records();
    a->set_ir_id(2);
    a->set_record_id(1);
    a->set_name("A");
    a->set_fqn("A");
    auto* a_field = a->add_fields();
    a_field->set_name("value");
    a_field->mutable_type()->mutable_record()->set_target_record_ir_id(3);

    RecordIR* b = root->add_records();
    b->set_ir_id(3);
    b->set_record_id(2);
    b->set_name("B");
    b->set_fqn("B");
    auto* b_field = b->add_fields();
    b_field->set_name("count");
    b_field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);

    return schema_ir;
}

void assert_valid(const SchemaIrModel& schema_ir) {
    CompilerContext context;
    DiagnosticEngine diagnostics;
    SchemaIrValidator validator;
    validator.validate(schema_ir, context, diagnostics);
    ASSERT_TRUE(diagnostics.empty()) << "nested record fixture Schema IR failed validation";
}

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string("quarry-c-cpp-nested-record-interop-") + std::string(stem) + "-" +
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

void write_binary_file(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary};
    if (!output) {
        throw std::runtime_error("failed to open test file for writing: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] std::string read_binary_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

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

// Root-namespace C API (no symbol prefix): A_t/B_t, A_init/B_init,
// A_encode/A_decode, A_encode_result_t/A_decode_result_t.
constexpr std::string_view kCHarness =
    "#include \"generated/schema.generated.h\"\n"
    "#include <stdio.h>\n"
    "#include <stdint.h>\n"
    "#include <string.h>\n"
    "int main(int argc, char** argv) {\n"
    "  if (argc < 3) return 20;\n"
    "  if (strcmp(argv[1], \"encode_empty\") == 0) {\n"
    "    A_t a;\n"
    "    A_init(&a);\n"
    "    a.has_value = true;\n"
    "    B_init(&a.value);\n"
    "    uint8_t buf[128];\n"
    "    A_encode_result_t r = A_encode(&a, buf, sizeof(buf));\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 1;\n"
    "    FILE* f = fopen(argv[2], \"wb\");\n"
    "    if (!f) return 2;\n"
    "    fwrite(buf, 1, r.bytes_written, f);\n"
    "    fclose(f);\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"encode_populated\") == 0) {\n"
    "    A_t a;\n"
    "    A_init(&a);\n"
    "    a.has_value = true;\n"
    "    B_init(&a.value);\n"
    "    a.value.has_count = true;\n"
    "    a.value.count = 0x01020304U;\n"
    "    uint8_t buf[128];\n"
    "    A_encode_result_t r = A_encode(&a, buf, sizeof(buf));\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 1;\n"
    "    FILE* f = fopen(argv[2], \"wb\");\n"
    "    if (!f) return 2;\n"
    "    fwrite(buf, 1, r.bytes_written, f);\n"
    "    fclose(f);\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"decode_expect_empty\") == 0) {\n"
    "    FILE* f = fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    uint8_t buf[256];\n"
    "    size_t n = fread(buf, 1, sizeof(buf), f);\n"
    "    fclose(f);\n"
    "    A_decode_result_t r = A_decode(buf, n);\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 4;\n"
    "    if (!r.value.has_value) return 5;\n"
    "    if (r.value.value.has_count) return 6;\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"decode_expect_populated\") == 0) {\n"
    "    FILE* f = fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    uint8_t buf[256];\n"
    "    size_t n = fread(buf, 1, sizeof(buf), f);\n"
    "    fclose(f);\n"
    "    A_decode_result_t r = A_decode(buf, n);\n"
    "    if (r.status != QUARRY_C_STATUS_OK) return 4;\n"
    "    if (!r.value.has_value) return 5;\n"
    "    if (!r.value.value.has_count) return 6;\n"
    "    if (r.value.value.count != 0x01020304U) return 7;\n"
    "    return 0;\n"
    "  }\n"
    "  if (strcmp(argv[1], \"decode_expect_failure\") == 0) {\n"
    "    FILE* f = fopen(argv[2], \"rb\");\n"
    "    if (!f) return 3;\n"
    "    uint8_t buf[256];\n"
    "    size_t n = fread(buf, 1, sizeof(buf), f);\n"
    "    fclose(f);\n"
    "    A_decode_result_t r = A_decode(buf, n);\n"
    "    return (r.status != QUARRY_C_STATUS_OK) ? 0 : 1;\n"
    "  }\n"
    "  return 11;\n"
    "}\n";

constexpr std::string_view kCppHarness =
    "#include \"generated/schema.generated.hpp\"\n"
    "#include <cstdint>\n"
    "#include <cstring>\n"
    "#include <fstream>\n"
    "#include <iterator>\n"
    "#include <vector>\n"
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
    "  if (std::strcmp(argv[1], \"encode_empty\") == 0) {\n"
    "    B empty_child;\n"
    "    ABuilder builder;\n"
    "    if (!builder.set_value(empty_child)) return 1;\n"
    "    auto encoded = encode(builder.build());\n"
    "    if (!encoded.has_value()) return 2;\n"
    "    std::ofstream out(argv[2], std::ios::binary);\n"
    "    if (!out) return 3;\n"
    "    out.write(reinterpret_cast<const char*>(encoded->data()),\n"
    "              static_cast<std::streamsize>(encoded->size()));\n"
    "    return 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"encode_populated\") == 0) {\n"
    "    BBuilder child_builder;\n"
    "    if (!child_builder.set_count(0x01020304U)) return 1;\n"
    "    ABuilder builder;\n"
    "    if (!builder.set_value(child_builder.build())) return 1;\n"
    "    auto encoded = encode(builder.build());\n"
    "    if (!encoded.has_value()) return 2;\n"
    "    std::ofstream out(argv[2], std::ios::binary);\n"
    "    if (!out) return 3;\n"
    "    out.write(reinterpret_cast<const char*>(encoded->data()),\n"
    "              static_cast<std::streamsize>(encoded->size()));\n"
    "    return 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode_expect_empty\") == 0) {\n"
    "    auto bytes = read_bytes(argv[2]);\n"
    "    auto decoded = decode_A(bytes);\n"
    "    if (!decoded.has_value() || !decoded->has_value()) return 5;\n"
    "    if (decoded->value()->has_count()) return 6;\n"
    "    return 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode_expect_populated\") == 0) {\n"
    "    auto bytes = read_bytes(argv[2]);\n"
    "    auto decoded = decode_A(bytes);\n"
    "    if (!decoded.has_value() || !decoded->has_value()) return 5;\n"
    "    if (!decoded->value()->has_count()) return 6;\n"
    "    if (*decoded->value()->count() != 0x01020304U) return 7;\n"
    "    return 0;\n"
    "  }\n"
    "  if (std::strcmp(argv[1], \"decode_expect_failure\") == 0) {\n"
    "    auto bytes = read_bytes(argv[2]);\n"
    "    auto decoded = decode_A(bytes);\n"
    "    return decoded.has_value() ? 1 : 0;\n"
    "  }\n"
    "  return 11;\n"
    "}\n";

} // namespace

TEST(CCppNestedRecordInteropTest, ByteForByteCompatibleAndCrossDecodable) {
    const SchemaIrModel schema_ir = make_nested_record_schema_ir();
    assert_valid(schema_ir);

    const cpp_backend::CodegenResult cpp_result =
        cpp_backend::Backend{}.generate(schema_ir, cpp_backend::CodegenOptions{});
    ASSERT_TRUE(cpp_result.success) << cpp_result.error_message;
    const c_backend::CodegenResult c_result =
        c_backend::Backend{}.generate(schema_ir, c_backend::CodegenOptions{});
    ASSERT_TRUE(c_result.success) << c_result.error_message;

    const std::filesystem::path root = make_temp_directory("nested");
    const std::filesystem::path generated_c = root / "c";
    const std::filesystem::path generated_cpp = root / "cpp";
    for (const auto& file : c_result.files) {
        write_text_file(generated_c / file.path, file.content);
    }
    for (const auto& file : cpp_result.files) {
        write_text_file(generated_cpp / file.path, file.content);
    }

    const std::filesystem::path c_harness_source = root / "c_harness.c";
    const std::filesystem::path cpp_harness_source = root / "cpp_harness.cpp";
    write_text_file(c_harness_source, kCHarness);
    write_text_file(cpp_harness_source, kCppHarness);

    const std::filesystem::path c_harness_binary = root / "c_harness";
    const std::filesystem::path cpp_harness_binary = root / "cpp_harness";

    const std::filesystem::path generated_c_source = generated_c / "generated/schema.generated.c";
    const std::filesystem::path c_harness_object = root / "c_harness.o";
    const std::filesystem::path generated_c_object = root / "schema.generated.o";

    // Compile each C translation unit separately (with -c) and link them in a
    // distinct step -- see the identical rationale in
    // c_cpp_codec_interop_test.cpp for why -std=c99/-x c must not appear on
    // the same invocation that also performs an implicit link.
    const std::string compile_c_harness_command =
        shell_quote(QUARRY_TEST_CXX_COMPILER) +
        " -x c -std=c99 -Wall -Wextra -Wpedantic -Werror -I" + shell_quote(generated_c.string()) +
        " -I" + shell_quote(QUARRY_TEST_REPO_INCLUDE_DIR) + " -I" +
        shell_quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) + " -c " +
        shell_quote(c_harness_source.string()) + " -o " + shell_quote(c_harness_object.string());
    ASSERT_EQ(run_and_get_exit_code(compile_c_harness_command), 0) << compile_c_harness_command;

    const std::string compile_generated_c_command =
        shell_quote(QUARRY_TEST_CXX_COMPILER) +
        " -x c -std=c99 -Wall -Wextra -Wpedantic -Werror -I" + shell_quote(generated_c.string()) +
        " -I" + shell_quote(QUARRY_TEST_REPO_INCLUDE_DIR) + " -I" +
        shell_quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) + " -c " +
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
        shell_quote(cpp_harness_source.string()) + " -o " + shell_quote(cpp_harness_binary.string());
    ASSERT_EQ(run_and_get_exit_code(compile_cpp_command), 0) << compile_cpp_command;

    // --- Empty-but-present nested child ---
    const std::filesystem::path c_empty = root / "c_empty.bin";
    const std::filesystem::path cpp_empty = root / "cpp_empty.bin";
    ASSERT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) + " encode_empty " +
                                    shell_quote(c_empty.string())),
             0);
    ASSERT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) + " encode_empty " +
                                    shell_quote(cpp_empty.string())),
             0);
    const std::string c_empty_bytes = read_binary_file(c_empty);
    const std::string cpp_empty_bytes = read_binary_file(cpp_empty);
    ASSERT_FALSE(c_empty_bytes.empty());
    EXPECT_EQ(c_empty_bytes, cpp_empty_bytes)
        << "C and C++ encoders produced different bytes for an empty nested record";

    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " decode_expect_empty " + shell_quote(c_empty.string())),
             0)
        << "C++ failed to decode C-encoded empty nested record";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " decode_expect_empty " + shell_quote(cpp_empty.string())),
             0)
        << "C failed to decode C++-encoded empty nested record";

    // --- Populated nested child ---
    const std::filesystem::path c_populated = root / "c_populated.bin";
    const std::filesystem::path cpp_populated = root / "cpp_populated.bin";
    ASSERT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) + " encode_populated " +
                                    shell_quote(c_populated.string())),
             0);
    ASSERT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " encode_populated " + shell_quote(cpp_populated.string())),
             0);
    const std::string c_populated_bytes = read_binary_file(c_populated);
    const std::string cpp_populated_bytes = read_binary_file(cpp_populated);
    ASSERT_FALSE(c_populated_bytes.empty());
    EXPECT_EQ(c_populated_bytes, cpp_populated_bytes)
        << "C and C++ encoders produced different bytes for a populated nested record";

    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " decode_expect_populated " + shell_quote(c_populated.string())),
             0)
        << "C++ failed to decode C-encoded populated nested record";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " decode_expect_populated " + shell_quote(cpp_populated.string())),
             0)
        << "C failed to decode C++-encoded populated nested record";

    // --- Nested failure cases ---
    // Reuses the exact byte sequences already independently proven correct
    // by BackendCodegenTest.NestedRecordFieldUsesCompleteEmbeddedRecord
    // (tests/backend/backend_codegen_test.cpp): the populated encoding with
    // the embedded record's declared id changed from 2 to 3 (wrong nested
    // record id), and a populated encoding whose embedded record header
    // itself is truncated.
    const std::filesystem::path wrong_nested_id = root / "wrong_nested_id.bin";
    write_binary_file(wrong_nested_id,
                      {0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x17, 0x01, 0x00, 0x01,
                       0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x07, 0x00, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04});
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " decode_expect_failure " + shell_quote(wrong_nested_id.string())),
             0)
        << "C accepted a nested record with the wrong embedded record id";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " decode_expect_failure " + shell_quote(wrong_nested_id.string())),
             0)
        << "C++ accepted a nested record with the wrong embedded record id";

    const std::filesystem::path truncated_nested_header = root / "truncated_nested_header.bin";
    write_binary_file(truncated_nested_header,
                      {0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00});
    EXPECT_EQ(run_and_get_exit_code(shell_quote(c_harness_binary.string()) +
                                    " decode_expect_failure " +
                                    shell_quote(truncated_nested_header.string())),
             0)
        << "C accepted a nested record with a truncated embedded header";
    EXPECT_EQ(run_and_get_exit_code(shell_quote(cpp_harness_binary.string()) +
                                    " decode_expect_failure " +
                                    shell_quote(truncated_nested_header.string())),
             0)
        << "C++ accepted a nested record with a truncated embedded header";
}
