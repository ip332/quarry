#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "compiler/qbs/serializer.hpp"
#include "quarry/runtime_c/generic_brf.h"
#include <gtest/gtest.h>

#ifndef QUARRY_TEST_GENERATED_CODE_API_VERSION
#error "QUARRY_TEST_GENERATED_CODE_API_VERSION must be defined"
#endif

#ifndef QUARRY_TEST_CXX_COMPILER
#error "QUARRY_TEST_CXX_COMPILER must be defined"
#endif

// Warnings generated C++ must compile cleanly under, as downstream projects
// commonly build with them (#83).
#define QUARRY_TEST_STRICT_CXX_WARNINGS " -Wall -Wextra -Wpedantic -Wshadow -Werror"

#ifndef QUARRY_TEST_REPO_INCLUDE_DIR
#error "QUARRY_TEST_REPO_INCLUDE_DIR must be defined"
#endif

#ifndef QUARRY_TEST_GENERATED_INCLUDE_DIR
#error "QUARRY_TEST_GENERATED_INCLUDE_DIR must be defined"
#endif

#ifndef QUARRY_TEST_VERSION_DISPLAY
#error "QUARRY_TEST_VERSION_DISPLAY must be defined"
#endif

#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string("quarry-schema-compiler-") + std::string(stem) + "-" +
         std::to_string(suffix));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path};
    if (!output) {
        throw std::runtime_error("failed to open test file for writing: " + path.string());
    }
    output << text;
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

struct CommandResult {
    int status = 0;
    std::string stdout_text;
    std::string stderr_text;
};

[[nodiscard]] CommandResult run_tool(const std::vector<std::string>& arguments,
                                     const std::filesystem::path& working_directory) {
    const std::filesystem::path stdout_path = working_directory / "stdout.txt";
    const std::filesystem::path stderr_path = working_directory / "stderr.txt";

    int status = 125;
#ifdef _WIN32
    throw std::runtime_error(
        "direct subprocess schema compiler tests are not implemented on Windows");
#else
    const pid_t child = fork();
    if (child < 0) {
        throw std::runtime_error("failed to fork schema compiler subprocess");
    }

    if (child == 0) {
        if (chdir(working_directory.string().c_str()) != 0) {
            _exit(125);
        }

        const int stdout_fd =
            open(stdout_path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (stdout_fd < 0) {
            _exit(125);
        }
        const int stderr_fd =
            open(stderr_path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (stderr_fd < 0) {
            close(stdout_fd);
            _exit(125);
        }
        if (dup2(stdout_fd, STDOUT_FILENO) < 0 || dup2(stderr_fd, STDERR_FILENO) < 0) {
            close(stdout_fd);
            close(stderr_fd);
            _exit(125);
        }
        close(stdout_fd);
        close(stderr_fd);

        std::vector<std::string> argument_storage;
        argument_storage.push_back(QUARRY_SCHEMA_COMPILER_TOOL);
        argument_storage.insert(argument_storage.end(), arguments.begin(), arguments.end());

        std::vector<char*> argv;
        argv.reserve(argument_storage.size() + 1U);
        for (auto& argument : argument_storage) {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);

        execv(argument_storage.front().c_str(), argv.data());
        _exit(127);
    }

    int wait_status = 0;
    if (waitpid(child, &wait_status, 0) < 0) {
        throw std::runtime_error("failed to wait for schema compiler subprocess");
    }

    status = 128;
    if (WIFEXITED(wait_status)) {
        status = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        status = 128 + WTERMSIG(wait_status);
    }
#endif

    return CommandResult{
        .status = status,
        .stdout_text = read_text_file(stdout_path),
        .stderr_text = read_text_file(stderr_path),
    };
}

[[nodiscard]] std::vector<std::filesystem::path>
regular_files_under(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(directory)) {
        return files;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

[[nodiscard]] std::vector<std::string> non_empty_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

} // namespace

TEST(SchemaCompilerToolTest, HelpReturnsSuccess) {
    const std::filesystem::path root = make_temp_directory("help");

    const CommandResult result = run_tool({"--help"}, root);

    EXPECT_EQ(result.status, 0);
    EXPECT_NE(result.stdout_text.find("quarry-schema-compiler [options] INPUT"),
              std::string::npos);
    EXPECT_NE(result.stdout_text.find("--list-outputs"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(SchemaCompilerToolTest, EmitsDeterministicQbsForBenchmarkWorkload) {
    const std::filesystem::path root = make_temp_directory("emit-qbs");
    const std::filesystem::path input =
        std::filesystem::path{QUARRY_TEST_SOURCE_DIR} / "benchmarks/schemas/workload.brd";
    const auto first = root / "first.qbs";
    const auto second = root / "second.qbs";
    ASSERT_EQ(run_tool({"--emit-qbs", first.string(), input.string()}, root).status, 0);
    ASSERT_EQ(run_tool({"--emit-qbs", second.string(), input.string()}, root).status, 0);
    std::ifstream a(first, std::ios::binary);
    std::ifstream b(second, std::ios::binary);
    const std::vector<uint8_t> first_bytes{std::istreambuf_iterator<char>(a), {}};
    const std::vector<uint8_t> second_bytes{std::istreambuf_iterator<char>(b), {}};
    EXPECT_EQ(first_bytes, second_bytes);
    ASSERT_FALSE(first_bytes.empty());
    quarry_qbs_record_view_t records[128]{}; quarry_qbs_field_view_t fields[128]{};
    quarry_qbs_type_view_t types[128]{}; quarry_qbs_enum_view_t enums[128]{}; uint64_t values[128]{};
    quarry_workspace_t workspace{};
    workspace.records = records;
    workspace.record_capacity = 128;
    workspace.fields = fields;
    workspace.field_capacity = 128;
    workspace.types = types;
    workspace.type_capacity = 128;
    workspace.enums = enums;
    workspace.enum_capacity = 128;
    workspace.enum_values = values;
    workspace.enum_value_capacity = 128;
    quarry_qbs_view_t view{};
    EXPECT_EQ(quarry_qbs_parse(first_bytes.data(), first_bytes.size(), &view, &workspace, nullptr),
              QUARRY_GENERIC_OK);
}

TEST(SchemaCompilerToolTest, EmitsDeterministicReflectiveQbsWhenRequested) {
    const std::filesystem::path root = make_temp_directory("emit-qbs-reflective");
    const std::filesystem::path input =
        std::filesystem::path{QUARRY_TEST_SOURCE_DIR} / "benchmarks/schemas/workload.brd";
    const auto minimal = root / "minimal.qbs";
    const auto first = root / "first.qbs";
    const auto second = root / "second.qbs";
    ASSERT_EQ(run_tool({"--emit-qbs", minimal.string(), input.string()}, root).status, 0);
    ASSERT_EQ(run_tool({"--emit-qbs", first.string(), "--qbs-reflective", input.string()}, root).status,
              0);
    ASSERT_EQ(run_tool({"--qbs-reflective", "--emit-qbs", second.string(), input.string()}, root).status,
              0);
    std::ifstream minimal_stream(minimal, std::ios::binary);
    std::ifstream first_stream(first, std::ios::binary);
    std::ifstream second_stream(second, std::ios::binary);
    const std::vector<uint8_t> minimal_bytes{std::istreambuf_iterator<char>(minimal_stream), {}};
    const std::vector<uint8_t> first_bytes{std::istreambuf_iterator<char>(first_stream), {}};
    const std::vector<uint8_t> second_bytes{std::istreambuf_iterator<char>(second_stream), {}};
    EXPECT_NE(first_bytes, minimal_bytes);
    EXPECT_EQ(first_bytes, second_bytes);

    quarry_qbs_record_view_t records[128]{}; quarry_qbs_field_view_t fields[128]{};
    quarry_qbs_type_view_t types[128]{}; quarry_qbs_enum_view_t enums[128]{}; uint64_t values[128]{};
    quarry_workspace_t workspace{};
    workspace.records = records; workspace.record_capacity = 128;
    workspace.fields = fields; workspace.field_capacity = 128;
    workspace.types = types; workspace.type_capacity = 128;
    workspace.enums = enums; workspace.enum_capacity = 128;
    workspace.enum_values = values; workspace.enum_value_capacity = 128;
    quarry_qbs_view_t view{};
    ASSERT_EQ(quarry_qbs_parse(first_bytes.data(), first_bytes.size(), &view, &workspace, nullptr),
              QUARRY_GENERIC_OK);
    EXPECT_NE(view.strings_offset, 0U);
    quarry_string_view_t name{};
    ASSERT_EQ(quarry_qbs_record_name(&view, &view.records[0], &name), QUARRY_GENERIC_OK);
    EXPECT_EQ(std::string(name.data, name.size), "Workload");
    ASSERT_EQ(quarry_qbs_field_name(&view, &view.records[0], 0U, &name), QUARRY_GENERIC_OK);
    EXPECT_EQ(std::string(name.data, name.size), "sequence");
}

TEST(SchemaCompilerToolTest, QbsReflectiveRequiresQbsOutput) {
    const std::filesystem::path root = make_temp_directory("qbs-reflective-validation");
    const std::filesystem::path input = root / "schema.brd";
    write_text_file(input,
                    "namespace: quarry.test\nrecord: Sample\nversion: 1\ntype: data\nfields:\n"
                    "  value:\n    type: uint32\n");
    const CommandResult result = run_tool({"--qbs-reflective", input.string()}, root);
    EXPECT_EQ(result.status, 2);
    EXPECT_NE(result.stderr_text.find("--qbs-reflective requires --emit-qbs"), std::string::npos);
}

TEST(SchemaCompilerToolTest, DuplicateQbsReflectiveFails) {
    const std::filesystem::path root = make_temp_directory("qbs-reflective-duplicate");
    const std::filesystem::path input = root / "schema.brd";
    write_text_file(input,
                    "namespace: quarry.test\nrecord: Sample\nversion: 1\ntype: data\nfields:\n"
                    "  value:\n    type: uint32\n");
    const CommandResult result = run_tool({"--qbs-reflective", "--qbs-reflective", input.string()}, root);
    EXPECT_EQ(result.status, 2);
    EXPECT_NE(result.stderr_text.find("duplicate --qbs-reflective option"), std::string::npos);
}

TEST(SchemaCompilerToolTest, HelpIsTerminalBeforeListOutputs) {
    const std::filesystem::path root = make_temp_directory("help-list-outputs");

    const CommandResult result = run_tool({"--help", "--list-outputs"}, root);

    EXPECT_EQ(result.status, 0);
    EXPECT_NE(result.stdout_text.find("quarry-schema-compiler [options] INPUT"),
              std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(SchemaCompilerToolTest, HelpIsTerminalBeforeGeneratedCodeApiVersionQuery) {
    const std::filesystem::path root = make_temp_directory("help-generated-code-api-version");

    const CommandResult result =
        run_tool({"--help", "--print-generated-code-api-version"}, root);

    EXPECT_EQ(result.status, 0);
    EXPECT_NE(result.stdout_text.find("quarry-schema-compiler [options] INPUT"),
              std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(SchemaCompilerToolTest, VersionReturnsSuccessWithoutInput) {
    const std::filesystem::path root = make_temp_directory("version");

    const CommandResult result = run_tool({"--version"}, root);

    EXPECT_EQ(result.status, 0);
    EXPECT_NE(result.stdout_text.find("quarry-schema-compiler " QUARRY_TEST_VERSION_DISPLAY),
              std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(SchemaCompilerToolTest, VersionIsTerminalWhenCombinedWithInputAndOptions) {
    const std::filesystem::path root = make_temp_directory("version-combined");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result =
        run_tool({"--version", "--output-directory", output.string(), input.string()}, root);

    EXPECT_EQ(result.status, 0);
    EXPECT_NE(result.stdout_text.find("quarry-schema-compiler " QUARRY_TEST_VERSION_DISPLAY),
              std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SchemaCompilerToolTest, VersionIsTerminalBeforeListOutputs) {
    const std::filesystem::path root = make_temp_directory("version-list-outputs");
    const std::filesystem::path input = root / "schema.brd";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result = run_tool({"--version", "--list-outputs", input.string()}, root);

    EXPECT_EQ(result.status, 0);
    EXPECT_NE(result.stdout_text.find("quarry-schema-compiler " QUARRY_TEST_VERSION_DISPLAY),
              std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(SchemaCompilerToolTest, VersionIsTerminalBeforeGeneratedCodeApiVersionQuery) {
    const std::filesystem::path root = make_temp_directory("version-generated-code-api-version");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result = run_tool(
        {"--version", "--print-generated-code-api-version", "--output-directory", output.string(),
         input.string()},
        root);

    EXPECT_EQ(result.status, 2);
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_NE(result.stderr_text.find("does not accept generation options or an input file"),
              std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SchemaCompilerToolTest, GeneratedCodeApiVersionQueryReturnsExactValue) {
    const std::filesystem::path root = make_temp_directory("generated-code-api-version");

    const CommandResult result = run_tool({"--print-generated-code-api-version"}, root);

    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(result.stdout_text,
              std::to_string(QUARRY_TEST_GENERATED_CODE_API_VERSION) + "\n");
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_FALSE(std::filesystem::exists(root / "generated"));
}

TEST(SchemaCompilerToolTest, GeneratedCodeApiVersionQueryRejectsGenerationArguments) {
    const std::filesystem::path root = make_temp_directory("generated-code-api-version-usage");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result = run_tool(
        {"--print-generated-code-api-version", "--output-directory", output.string(),
         input.string()},
        root);

    EXPECT_EQ(result.status, 2);
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_NE(result.stderr_text.find("does not accept generation options or an input file"),
              std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SchemaCompilerToolTest, UnknownOptionReturnsUsageError) {
    const std::filesystem::path root = make_temp_directory("unknown-option");

    const CommandResult result = run_tool({"--unknown-option"}, root);

    EXPECT_EQ(result.status, 2);
    EXPECT_NE(result.stderr_text.find("unknown option --unknown-option"), std::string::npos);
}

TEST(SchemaCompilerToolTest, MissingInputReturnsFailureWithoutOutput) {
    const std::filesystem::path root = make_temp_directory("missing-input");
    const std::filesystem::path output = root / "out";

    const CommandResult result =
        run_tool({"--output-directory", output.string(), (root / "missing.brd").string()}, root);

    EXPECT_NE(result.status, 0);
    EXPECT_NE(result.stderr_text.find("failed to read input file"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SchemaCompilerToolTest, InvalidYamlReturnsFailureWithoutOutput) {
    const std::filesystem::path root = make_temp_directory("invalid-yaml");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "out";
    write_text_file(input, "namespace: quarry.telemetry\nrecord: Sample\nfields: [\n");

    const CommandResult result =
        run_tool({"--output-directory", output.string(), input.string()}, root);

    EXPECT_NE(result.status, 0);
    EXPECT_NE(result.stderr_text.find("BC2101"), std::string::npos);
    EXPECT_TRUE(regular_files_under(output).empty());
}

TEST(SchemaCompilerToolTest, ListOutputsRequiresInput) {
    const std::filesystem::path root = make_temp_directory("list-outputs-no-input");

    const CommandResult result = run_tool({"--list-outputs"}, root);

    EXPECT_EQ(result.status, 2);
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_NE(result.stderr_text.find("expected exactly one input file"), std::string::npos);
}

TEST(SchemaCompilerToolTest, ListOutputsPrintsRelativePathsAndDoesNotWriteFiles) {
    const std::filesystem::path root = make_temp_directory("list-outputs-relative");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = "generated output with spaces";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result =
        run_tool({"--list-outputs", "--output-directory", output.string(), input.string()}, root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_EQ(result.stdout_text,
              "generated output with spaces/quarry/telemetry.generated.hpp\n");
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_FALSE(std::filesystem::exists(root / output));
}

TEST(SchemaCompilerToolTest, ListOutputsKeepsImportedUnitsAsDependenciesOnly) {
    const std::filesystem::path root = make_temp_directory("list-outputs-imports");
    const std::filesystem::path input = root / "schema.brd";
    write_text_file(root / "shared.brd",
                    "namespace: quarry.shared\n"
                    "record: Shared\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "imports:\n"
                    "  - shared.brd\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result =
        run_tool({"--list-outputs", "--output-directory", "generated", input.string()}, root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_EQ(result.stdout_text, "generated/quarry/telemetry.generated.hpp\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(SchemaCompilerToolTest, CppCrossNamespaceGenerationUsesPlannedDependencyHeader) {
    const std::filesystem::path root = make_temp_directory("cpp-cross-namespace");
    const std::filesystem::path shared = root / "shared.brd";
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";

    write_text_file(shared,
                    "namespace: quarry.shared\n"
                    "record: Shared\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  value:\n"
                    "    type: uint32\n"
                    "enums:\n"
                    "  Mode:\n"
                    "    values:\n"
                    "      Off: 0\n"
                    "      On: 1\n");
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "imports:\n"
                    "  - shared.brd\n"
                    "fields:\n"
                    "  child:\n"
                    "    type: quarry.shared.Shared\n"
                    "  mode:\n"
                    "    type: quarry.shared.Mode\n"
                    "  children:\n"
                    "    type: quarry.shared.Shared[]\n"
                    "    max_elements: 2\n"
                    "  modes:\n"
                    "    type: quarry.shared.Mode[]\n"
                    "    max_elements: 2\n");

    const CommandResult shared_result =
        // The dependency comes from the same compilation root: bindings
        // generated from different roots are rejected by the identity checks.
        run_tool({"--language", "cpp", "--output-directory", output.string(), "--generate",
                  "quarry.shared", input.string()},
                 root);
    ASSERT_EQ(shared_result.status, 0) << shared_result.stderr_text;

    const CommandResult root_result =
        run_tool({"--language", "cpp", "--output-directory", output.string(), input.string()},
                 root);
    ASSERT_EQ(root_result.status, 0) << root_result.stderr_text;

    const std::filesystem::path generated_header =
        output / "quarry" / "telemetry.generated.hpp";
    const std::string header = read_text_file(generated_header);
    EXPECT_NE(header.find("#include \"quarry/shared.generated.hpp\""), std::string::npos);
    EXPECT_EQ(header.find("#include \"quarry/shared.generated.hpp\""),
              header.rfind("#include \"quarry/shared.generated.hpp\""));
    EXPECT_NE(header.find("std::optional<::quarry::shared::Shared>"), std::string::npos);
    EXPECT_NE(header.find("std::optional<::quarry::shared::Mode>"), std::string::npos);
    EXPECT_NE(header.find("std::vector<::quarry::shared::Shared>"), std::string::npos);
    EXPECT_NE(header.find("std::vector<::quarry::shared::Mode>"), std::string::npos);

    const std::filesystem::path translation = root / "compile.cpp";
    const std::filesystem::path object = root / "compile.o";
    write_text_file(translation, "#include \"quarry/telemetry.generated.hpp\"\nint main() {}\n");
    std::ostringstream command;
    command << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER))
            << " -std=c++20" QUARRY_TEST_STRICT_CXX_WARNINGS " -I" << std::quoted(output.string())
            << " -I" << std::quoted(QUARRY_TEST_REPO_INCLUDE_DIR) << " -I"
            << std::quoted(QUARRY_TEST_GENERATED_INCLUDE_DIR) << " -c "
            << std::quoted(translation.string()) << " -o " << std::quoted(object.string());
    EXPECT_EQ(std::system(command.str().c_str()), 0);
}

TEST(SchemaCompilerToolTest, CCrossNamespaceGenerationUsesPlannedDependencyHeader) {
    const std::filesystem::path root = make_temp_directory("c-cross-namespace");
    const std::filesystem::path shared = root / "shared.brd";
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";

    write_text_file(shared,
                    "namespace: quarry.shared\n"
                    "record: Shared\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  value:\n"
                    "    type: uint32\n"
                    "enums:\n"
                    "  Mode:\n"
                    "    values:\n"
                    "      Off: 0\n"
                    "      On: 1\n");
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "imports:\n"
                    "  - shared.brd\n"
                    "fields:\n"
                    "  child:\n"
                    "    type: quarry.shared.Shared\n"
                    "  mode:\n"
                    "    type: quarry.shared.Mode\n"
                    "  children:\n"
                    "    type: quarry.shared.Shared[]\n"
                    "    max_elements: 2\n"
                    "  modes:\n"
                    "    type: quarry.shared.Mode[]\n"
                    "    max_elements: 2\n");

    const CommandResult shared_result =
        // The dependency comes from the same compilation root: bindings
        // generated from different roots are rejected by the identity checks.
        run_tool({"--language", "c", "--output-directory", output.string(), "--generate",
                  "quarry.shared", input.string()},
                 root);
    ASSERT_EQ(shared_result.status, 0) << shared_result.stderr_text;
    const CommandResult root_result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);
    ASSERT_EQ(root_result.status, 0) << root_result.stderr_text;

    const std::filesystem::path generated_header =
        output / "quarry" / "telemetry.generated.h";
    const std::string header = read_text_file(generated_header);
    ASSERT_FALSE(header.empty());
    EXPECT_EQ(header.find("#include \"quarry/shared.generated.h\""),
              header.rfind("#include \"quarry/shared.generated.h\""));
    EXPECT_NE(header.find("quarry_shared_Shared_t child;"), std::string::npos);
    EXPECT_NE(header.find("quarry_shared_Mode_t mode;"), std::string::npos);
    EXPECT_NE(header.find("quarry_shared_Shared_t children[2];"), std::string::npos);
    EXPECT_NE(header.find("quarry_shared_Mode_t modes[2];"), std::string::npos);

    for (const char* stem : {"shared", "telemetry"}) {
        const std::filesystem::path source = output / "quarry" / (std::string(stem) + ".generated.c");
        const std::filesystem::path object = root / (std::string(stem) + ".o");
        std::ostringstream command;
        command << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER)) << " -x c -std=c99"
                << " -Wall -Wextra -Wpedantic -Werror -I" << std::quoted(output.string())
                << " -I" << std::quoted(QUARRY_TEST_REPO_INCLUDE_DIR) << " -I"
                << std::quoted(QUARRY_TEST_GENERATED_INCLUDE_DIR) << " -c "
                << std::quoted(source.string()) << " -o " << std::quoted(object.string());
        EXPECT_EQ(std::system(command.str().c_str()), 0);
    }
}

TEST(SchemaCompilerToolTest, ListOutputsPrintsAbsolutePathsAndDoesNotCreateOutputDirectory) {
    const std::filesystem::path root = make_temp_directory("list-outputs-absolute");
    const std::filesystem::path working_directory = root / "working directory with spaces";
    const std::filesystem::path input_directory = root / "input directory with spaces";
    const std::filesystem::path input = input_directory / "schema.brd";
    const std::filesystem::path output = root / "generated output with spaces";
    std::filesystem::create_directories(working_directory);
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result =
        run_tool({"--list-outputs", "--output-directory", output.string(), input.string()},
                 working_directory);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_EQ(result.stdout_text, (output / "quarry" / "telemetry.generated.hpp").string() +
                                      "\n");
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_FALSE(std::filesystem::exists(output));
    EXPECT_FALSE(std::filesystem::exists(working_directory / "generated"));
}

TEST(SchemaCompilerToolTest, ListOutputsReflectsCustomFileExtension) {
    const std::filesystem::path root = make_temp_directory("list-outputs-custom-extension");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result =
        run_tool({"--list-outputs", "--output-directory", output.string(), "--file-extension",
                  ".hpp", input.string()},
                 root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_EQ(result.stdout_text, (output / "quarry" / "telemetry.hpp").string() + "\n");
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SchemaCompilerToolTest, ListOutputsFailureDoesNotWriteFiles) {
    const std::filesystem::path root = make_temp_directory("list-outputs-invalid-yaml");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "out";
    write_text_file(input, "namespace: quarry.telemetry\nrecord: Sample\nfields: [\n");

    const CommandResult result =
        run_tool({"--list-outputs", "--output-directory", output.string(), input.string()}, root);

    EXPECT_NE(result.status, 0);
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_NE(result.stderr_text.find("BC2101"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SchemaCompilerToolTest, ListOutputsMatchesNormalGenerationInventory) {
    const std::filesystem::path root = make_temp_directory("list-outputs-consistency");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult listed =
        run_tool({"--list-outputs", "--output-directory", output.string(), input.string()}, root);
    ASSERT_EQ(listed.status, 0) << listed.stderr_text;
    ASSERT_TRUE(listed.stderr_text.empty());
    const std::vector<std::string> listed_paths = non_empty_lines(listed.stdout_text);

    const CommandResult generated =
        run_tool({"--output-directory", output.string(), input.string()}, root);
    ASSERT_EQ(generated.status, 0) << generated.stderr_text;
    ASSERT_TRUE(generated.stdout_text.empty());
    ASSERT_TRUE(generated.stderr_text.empty());

    std::vector<std::string> generated_paths;
    for (const std::filesystem::path& file : regular_files_under(output)) {
        generated_paths.push_back(file.string());
    }
    EXPECT_EQ(listed_paths, generated_paths);
}

TEST(SchemaCompilerToolTest, ValidYamlWritesGeneratedFiles) {
    const std::filesystem::path root = make_temp_directory("valid-yaml");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result =
        run_tool({"--output-directory", output.string(), input.string()}, root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_TRUE(result.stderr_text.empty());

    const std::vector<std::filesystem::path> files = regular_files_under(output);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files.front().filename(), "telemetry.generated.hpp");
    const std::string generated = read_text_file(files.front());
    EXPECT_NE(generated.find("struct Sample"), std::string::npos);
    EXPECT_NE(generated.find("std::uint32_t"), std::string::npos);
}

TEST(SchemaCompilerToolTest, ExistingGeneratedFileIsReplacedAndUnrelatedOutputIsPreserved) {
    const std::filesystem::path root = make_temp_directory("replace-existing");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    const std::filesystem::path generated_file = output / "quarry" / "telemetry.generated.hpp";
    const std::filesystem::path temporary_file =
        output / "quarry" / "telemetry.generated.hpp.tmp-quarry-schema-compiler";
    const std::filesystem::path unrelated_file = output / "keep.txt";

    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    write_text_file(generated_file, "old generated content\n");
    write_text_file(unrelated_file, "do not touch\n");

    const CommandResult result =
        run_tool({"--output-directory", output.string(), input.string()}, root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_EQ(read_text_file(unrelated_file), "do not touch\n");
    EXPECT_FALSE(std::filesystem::exists(temporary_file));

    const std::string generated = read_text_file(generated_file);
    EXPECT_NE(generated.find("struct Sample"), std::string::npos);
    EXPECT_EQ(generated.find("old generated content"), std::string::npos);
}

TEST(SchemaCompilerToolTest, AbsolutePathsWorkFromUnrelatedWorkingDirectory) {
    const std::filesystem::path root = make_temp_directory("absolute-unrelated");
    const std::filesystem::path input_root = root / "input";
    const std::filesystem::path working_directory = root / "unrelated-work";
    const std::filesystem::path input = input_root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    std::filesystem::create_directories(working_directory);
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result =
        run_tool({"--output-directory", output.string(), input.string()}, working_directory);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_TRUE(std::filesystem::exists(output / "quarry" / "telemetry.generated.hpp"));
    EXPECT_FALSE(std::filesystem::exists(working_directory / "generated"));
}

TEST(SchemaCompilerToolTest, PathsWithSpacesWorkWithDirectArguments) {
    const std::filesystem::path root = make_temp_directory("paths with spaces");
    const std::filesystem::path working_directory = root / "working directory with spaces";
    const std::filesystem::path input_directory = root / "input directory with spaces";
    const std::filesystem::path output = root / "generated output with spaces";
    const std::filesystem::path input = input_directory / "schema file with spaces.brd";
    const std::filesystem::path generated_file =
        output / "quarry" / "telemetry.generated.hpp";
    const std::filesystem::path temporary_file =
        output / "quarry" / "telemetry.generated.hpp.tmp-quarry-schema-compiler";
    std::filesystem::create_directories(working_directory);
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    const CommandResult result =
        run_tool({"--output-directory", output.string(), input.string()}, working_directory);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_TRUE(std::filesystem::exists(generated_file));
    EXPECT_FALSE(std::filesystem::exists(temporary_file));
    EXPECT_FALSE(std::filesystem::exists(working_directory / "generated"));

    const std::string generated = read_text_file(generated_file);
    EXPECT_NE(generated.find("struct Sample"), std::string::npos);
    EXPECT_NE(generated.find("std::uint32_t"), std::string::npos);
}

// --- --language (PR-107) ---------------------------------------------------

TEST(SchemaCompilerToolTest, DefaultBackendIsCpp) {
    const std::filesystem::path root = make_temp_directory("language-default");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult result =
        run_tool({"--list-outputs", "--output-directory", output.string(), input.string()}, root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_EQ(result.stdout_text,
              (output / "quarry" / "telemetry.generated.hpp").string() + "\n");
}

TEST(SchemaCompilerToolTest, ExplicitCppBackendMatchesDefault) {
    const std::filesystem::path root = make_temp_directory("language-explicit-cpp");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult default_result =
        run_tool({"--list-outputs", "--output-directory", output.string(), input.string()}, root);
    const CommandResult explicit_result =
        run_tool({"--list-outputs", "--language", "cpp", "--output-directory", output.string(),
                  input.string()},
                 root);

    EXPECT_EQ(default_result.status, explicit_result.status);
    EXPECT_EQ(default_result.stdout_text, explicit_result.stdout_text);
    EXPECT_TRUE(explicit_result.stderr_text.empty());
}

TEST(SchemaCompilerToolTest, ExplicitCBackendListsHeaderAndSourcePaths) {
    const std::filesystem::path root = make_temp_directory("language-c-list-outputs");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult result = run_tool(
        {"--list-outputs", "--language", "c", "--output-directory", output.string(),
         input.string()},
        root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_EQ(result.stdout_text,
              (output / "quarry" / "telemetry.generated.h").string() + "\n" +
                  (output / "quarry" / "telemetry.generated.c").string() + "\n");
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SchemaCompilerToolTest, ListOutputsForCBackendIsDeterministic) {
    const std::filesystem::path root = make_temp_directory("language-c-deterministic");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult first =
        run_tool({"--list-outputs", "--language", "c", "--output-directory", output.string(),
                  input.string()},
                 root);
    const CommandResult second =
        run_tool({"--list-outputs", "--language", "c", "--output-directory", output.string(),
                  input.string()},
                 root);

    EXPECT_EQ(first.status, 0) << first.stderr_text;
    EXPECT_EQ(second.status, 0) << second.stderr_text;
    EXPECT_EQ(first.stdout_text, second.stdout_text);
}

TEST(SchemaCompilerToolTest, ExplicitPythonBackendListsPackageAndModulePaths) {
    const std::filesystem::path root = make_temp_directory("language-python-list-outputs");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: acme.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult result = run_tool(
        {"--list-outputs", "--language", "python", "--output-directory", output.string(),
         input.string()},
        root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_EQ(result.stdout_text,
              (output / "acme" / "__init__.py").string() + "\n" +
                  (output / "acme" / "telemetry" / "__init__.py").string() + "\n" +
                  (output / "acme" / "telemetry" / "schema.py").string() + "\n");
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SchemaCompilerToolTest, ListOutputsForPythonBackendIsDeterministic) {
    const std::filesystem::path root = make_temp_directory("language-python-deterministic");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: acme.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult first =
        run_tool({"--list-outputs", "--language", "python", "--output-directory", output.string(),
                  input.string()},
                 root);
    const CommandResult second =
        run_tool({"--list-outputs", "--language", "python", "--output-directory", output.string(),
                  input.string()},
                 root);

    EXPECT_EQ(first.status, 0) << first.stderr_text;
    EXPECT_EQ(second.status, 0) << second.stderr_text;
    EXPECT_EQ(first.stdout_text, second.stdout_text);
}

TEST(SchemaCompilerToolTest, CrossNamespaceGenerationIsByteForByteDeterministicForAllBackends) {
    const std::filesystem::path root = make_temp_directory("cross-namespace-deterministic");
    const std::filesystem::path shared = root / "shared.brd";
    const std::filesystem::path input = root / "root.brd";
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
                    "      READY: 0\n"
                    "      BUSY: 1\n");
    write_text_file(input,
                    "namespace: acme.root\n"
                    "record: Envelope\n"
                    "version: 1\n"
                    "type: data\n"
                    "imports:\n"
                    "  - shared.brd\n"
                    "fields:\n"
                    "  child:\n"
                    "    type: acme.shared.Child\n"
                    "  status:\n"
                    "    type: acme.shared.Status\n"
                    "  children:\n"
                    "    type: acme.shared.Child[]\n"
                    "    max_elements: 2\n"
                    "  statuses:\n"
                    "    type: acme.shared.Status[]\n"
                    "    max_elements: 2\n");

    const std::vector<std::string> languages = {"cpp", "c", "python"};
    for (const std::string& language : languages) {
        const std::filesystem::path first_output = root / (language + "-first");
        const std::filesystem::path second_output = root / (language + "-second");
        std::vector<std::string> first_list;
        std::vector<std::string> second_list;
        for (const auto& [output, list] :
             {std::pair{first_output, std::ref(first_list)},
              std::pair{second_output, std::ref(second_list)}}) {
            const CommandResult shared_list = run_tool(
                {"--list-outputs", "--language", language, "--output-directory",
                 output.string(), shared.string()},
                root);
            const CommandResult root_list = run_tool(
                {"--list-outputs", "--language", language, "--output-directory",
                 output.string(), input.string()},
                root);
            ASSERT_EQ(shared_list.status, 0) << shared_list.stderr_text;
            ASSERT_EQ(root_list.status, 0) << root_list.stderr_text;
            for (const std::string& line : non_empty_lines(shared_list.stdout_text)) {
                list.get().push_back(line);
            }
            for (const std::string& line : non_empty_lines(root_list.stdout_text)) {
                list.get().push_back(line);
            }
        }

        const CommandResult first_shared = run_tool(
            {"--language", language, "--output-directory", first_output.string(), shared.string()},
            root);
        const CommandResult first_root = run_tool(
            {"--language", language, "--output-directory", first_output.string(), input.string()},
            root);
        const CommandResult second_root = run_tool(
            {"--language", language, "--output-directory", second_output.string(), input.string()},
            root);
        const CommandResult second_shared = run_tool(
            {"--language", language, "--output-directory", second_output.string(), shared.string()},
            root);
        ASSERT_EQ(first_shared.status, 0) << first_shared.stderr_text;
        ASSERT_EQ(first_root.status, 0) << first_root.stderr_text;
        ASSERT_EQ(second_root.status, 0) << second_root.stderr_text;
        ASSERT_EQ(second_shared.status, 0) << second_shared.stderr_text;

        auto relative_files = [](const std::filesystem::path& directory) {
            std::vector<std::string> paths;
            for (const auto& file : regular_files_under(directory)) {
                paths.push_back(std::filesystem::relative(file, directory).generic_string());
            }
            return paths;
        };
        const std::vector<std::string> first_files = relative_files(first_output);
        const std::vector<std::string> second_files = relative_files(second_output);
        ASSERT_EQ(first_files, second_files) << language;
        std::sort(first_list.begin(), first_list.end());
        first_list.erase(std::unique(first_list.begin(), first_list.end()), first_list.end());
        std::sort(second_list.begin(), second_list.end());
        second_list.erase(std::unique(second_list.begin(), second_list.end()), second_list.end());
        ASSERT_EQ(first_list.size(), first_files.size()) << language;
        ASSERT_EQ(second_list.size(), second_files.size()) << language;
        for (const std::string& relative : first_files) {
            EXPECT_EQ(read_text_file(first_output / relative), read_text_file(second_output / relative))
                << language << ": " << relative;
        }
    }
}

TEST(SchemaCompilerToolTest, FileExtensionIsRejectedWithLanguagePython) {
    const std::filesystem::path root = make_temp_directory("language-python-file-extension");
    const std::filesystem::path input = root / "schema.brd";
    write_text_file(input,
                    "namespace: acme.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult result =
        run_tool({"--language", "python", "--file-extension", ".py", input.string()}, root);

    EXPECT_EQ(result.status, 2);
    EXPECT_NE(result.stderr_text.find("--file-extension is not supported with --language python"),
              std::string::npos);
}

TEST(SchemaCompilerToolTest, PythonBackendWritesPackageWithExpectedContent) {
    const std::filesystem::path root = make_temp_directory("language-python-generate");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: acme.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult result = run_tool(
        {"--language", "python", "--output-directory", output.string(), input.string()}, root);

    ASSERT_EQ(result.status, 0) << result.stderr_text;
    ASSERT_TRUE(std::filesystem::exists(output / "acme" / "__init__.py"));
    ASSERT_TRUE(std::filesystem::exists(output / "acme" / "telemetry" / "__init__.py"));
    ASSERT_TRUE(std::filesystem::exists(output / "acme" / "telemetry" / "schema.py"));

    const std::string module_content = read_text_file(output / "acme" / "telemetry" / "schema.py");
    EXPECT_NE(module_content.find("class Sample:"), std::string::npos);
    EXPECT_NE(module_content.find("def _quarry_encode_sample(value):"), std::string::npos);
    EXPECT_NE(module_content.find("QUARRY_GENERATED_CODE_API_VERSION_PYTHON"),
             std::string::npos);
}

TEST(SchemaCompilerToolTest, InvalidLanguageReturnsUsageErrorNamingAcceptedValues) {
    const std::filesystem::path root = make_temp_directory("language-invalid");
    const std::filesystem::path input = root / "schema.brd";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult result = run_tool({"--language", "rust", input.string()}, root);

    EXPECT_EQ(result.status, 2);
    EXPECT_NE(result.stderr_text.find("invalid value for --language: rust"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("'cpp', 'c', 'python', or 'go'"), std::string::npos);
}

TEST(SchemaCompilerToolTest, CompactPresenceOptionRequiresCAndChangesGeneratedLayout) {
    const auto root = make_temp_directory("c-optimize-size");
    const auto input = root / "schema.brd";
    write_text_file(input,
                    "namespace: demo\nrecord: Sample\nversion: 1\ntype: data\nfields:\n"
                    "  count:\n    type: uint32\n");
    auto rejected = run_tool({"--c-optimize-size", input.string()}, root);
    EXPECT_EQ(rejected.status, 2);
    EXPECT_NE(rejected.stderr_text.find("requires --language c"), std::string::npos);
    auto duplicate = run_tool({"--language", "c", "--c-optimize-size", "--c-optimize-size",
                               input.string()}, root);
    EXPECT_EQ(duplicate.status, 2);
    auto generated = run_tool({"--language", "c", "--c-optimize-size", input.string()}, root);
    ASSERT_EQ(generated.status, 0) << generated.stderr_text;
    const auto header = read_text_file(root / "generated/demo.generated.h");
    EXPECT_NE(header.find("uint8_t quarry_presence_bits[1]"), std::string::npos);
    EXPECT_NE(header.find("demo_Sample_set_count_present"), std::string::npos);
    auto planned = run_tool({"--language", "c", "--c-optimize-size", "--list-outputs",
                             input.string()}, root);
    EXPECT_EQ(planned.status, 0) << planned.stderr_text;
}

TEST(SchemaCompilerToolTest, CompactPresenceNamespaceSelectionsAreValidated) {
    const auto root = make_temp_directory("c-optimize-namespaces");
    const auto input = std::filesystem::path(QUARRY_TEST_SOURCE_DIR) /
                       "tests/fixtures/configuration_identity/rev_a/configuration.brd";
    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"--c-optimize-size=", "nonempty namespace list"},
        {"--c-optimize-size= fleet.common", "malformed"},
        {"--c-optimize-size=fleet.common, fleet.engine", "malformed"},
        {"--c-optimize-size=fleet..common", "malformed"},
        {"--c-optimize-size=.fleet", "malformed"},
        {"--c-optimize-size=fleet.common,", "malformed"},
        {"--c-optimize-size=1fleet", "malformed"},
        {"--c-optimize-size=fleet.common,fleet.common", "duplicate"},
        {"--c-optimize-size=fleet.unknown", "unknown"}};
    for (const auto& [option, diagnostic] : invalid) {
        for (const std::string_view mode : {"generate", "list", "qbs"}) {
            std::vector<std::string> args{"--language", "c", option, "--generate", "fleet.engine"};
            if (mode == "list")
                args.push_back("--list-outputs");
            if (mode == "qbs") {
                args.push_back("--emit-qbs");
                args.push_back((root / "invalid.qbs").string());
            }
            args.push_back(input.string());
            const auto result = run_tool(args, root);
            EXPECT_NE(result.status, 0) << option;
            EXPECT_NE(result.stderr_text.find("--c-optimize-size"), std::string::npos)
                << result.stderr_text;
            EXPECT_NE(result.stderr_text.find(diagnostic), std::string::npos) << result.stderr_text;
        }
    }
    for (const std::string other : {"--c-optimize-size", "--c-optimize-size=fleet.engine"}) {
        const auto result = run_tool(
            {"--language", "c", "--c-optimize-size=fleet.common", other, input.string()}, root);
        EXPECT_EQ(result.status, 2);
        EXPECT_NE(result.stderr_text.find("duplicate --c-optimize-size"), std::string::npos);
    }
    const auto result = run_tool({"--language", "c", "--c-optimize-size=fleet.common", "--generate",
                                  "fleet.engine", "--list-outputs", input.string()},
                                 root);
    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_EQ(result.stdout_text.find("fleet/common"), std::string::npos);
    const auto baseline = root / "default.qbs";
    const auto selected = root / "selected.qbs";
    const auto qbs_default = run_tool({"--emit-qbs", baseline.string(), input.string()}, root);
    const auto qbs_selected = run_tool({"--language", "c", "--c-optimize-size=fleet.common",
                                        "--emit-qbs", selected.string(), input.string()},
                                       root);
    ASSERT_EQ(qbs_default.status, 0) << qbs_default.stderr_text;
    ASSERT_EQ(qbs_selected.status, 0) << qbs_selected.stderr_text;
    EXPECT_EQ(read_text_file(baseline), read_text_file(selected));
}

TEST(SchemaCompilerToolTest, CppCStorageCompilesRoundTripsWithoutAllocations) {
    const auto root = make_temp_directory("cpp-c-storage");
    const auto schema = std::filesystem::path(QUARRY_TEST_SOURCE_DIR) / "benchmarks/schemas";
    for (const bool compact : {false, true}) {
        const auto output = root / (compact ? "compact" : "default");
        // Both namespaces come from one compilation root, workload.brd.
        {
            std::vector<std::string> options = {"--language",
                                                "cpp",
                                                "--cpp-storage",
                                                "c",
                                                "--output-directory",
                                                output.string(),
                                                "--generate",
                                                "benchmark.workload,benchmark.workload.shared",
                                                (schema / "workload.brd").string()};
            if (compact)
                options.insert(options.end(), {"--c-optimize-size"});
            const auto generated = run_tool(options, root);
            ASSERT_EQ(generated.status, 0) << generated.stderr_text;
        }
        auto planned =
            run_tool({"--language", "cpp", "--cpp-storage", "c", "--list-outputs",
                      "--output-directory", output.string(), (schema / "workload.brd").string()},
                     root);
        ASSERT_EQ(planned.status, 0) << planned.stderr_text;
        EXPECT_NE(planned.stdout_text.find("workload.generated.hpp"), std::string::npos);
        EXPECT_NE(planned.stdout_text.find("workload.generated.cpp"), std::string::npos);
        const auto harness = output / "test.cpp";
        write_text_file(harness, R"CPP(
#include "benchmark/workload.generated.hpp"
#include <array>
#include <cstdlib>
#include <new>
#include <type_traits>
static size_t allocations;
void* operator new(size_t n) { ++allocations; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
int main() {
    using Record = quarry::cxx::benchmark_workload_Workload;
    static_assert(sizeof(Record) == sizeof(Record::native_type));
    static_assert(std::is_trivially_copyable_v<Record>);
    const auto before = allocations;
    Record value;
    value.set_sequence(0); value.set_enabled(false); value.set_status(BENCHMARK_WORKLOAD_STATUS_ACTIVE);
    if (!value.has_sequence() || !value.has_enabled() || value.enabled()) return 1;
    if (!value.set_name("hello")) return 2;
    if (!value.set_name(value.name().substr(1)) || value.name() != "ello") return 3;
    const std::array<std::byte, 2> blob{std::byte{0}, std::byte{255}};
    if (!value.set_payload(blob)) return 4;
    const std::array<uint32_t, 2> numbers{0, 42};
    if (!value.set_values(numbers)) return 5;
    quarry::cxx::benchmark_workload_shared_Child child;
    child.set_id(7); if (!child.set_label("child")) return 6;
    value.set_child(child.native());
    const std::array<benchmark_workload_shared_Child_t, 1> children{child.native()};
    if (!value.set_children(children)) return 7;
    std::array<char, 257> too_long{};
    if (value.set_name(std::string_view(too_long.data(), too_long.size())) || value.name() != "ello") return 8;
    std::array<std::byte, 16384> bytes{}, reference{};
    const auto encoded = value.encode(bytes);
    const auto native = benchmark_workload_Workload_encode(&value.native(), reinterpret_cast<uint8_t*>(reference.data()), reference.size());
    if (encoded.status || native.status || encoded.bytes_written != native.bytes_written || bytes != reference) return 9;
    const auto measured = value.encoded_size();
    if (measured.status || measured.size != encoded.bytes_written) return 10;
    Record decoded;
    const auto input = std::span<const std::byte>(bytes).first(encoded.bytes_written);
    if (decoded.decode(input).status || decoded.sequence() != 0 || !decoded.has_sequence() ||
        decoded.name() != "ello" || decoded.payload()[1] != std::byte{255} ||
        decoded.values()[1] != 42 || decoded.child().id != 7 || decoded.children()[0].id != 7) return 11;
    decoded.clear_sequence(); decoded.clear_name(); decoded.clear_values(); decoded.clear_children();
    if (decoded.has_sequence() || decoded.has_name() || decoded.has_values() || decoded.has_children()) return 12;
    if (value.encode(std::span<std::byte>(bytes).first(1)).status == QUARRY_C_STATUS_OK) return 13;
    if (decoded.decode(input.first(1)).status == QUARRY_C_STATUS_OK) return 14;
    if (!value.set_name({}) || !value.set_payload({}) || !value.set_values({}) ||
        value.has_name() || value.has_payload() || value.has_values()) return 15;
    return allocations == before ? 0 : 16;
}
)CPP");
        const auto executable = output / "test";
        std::ostringstream command;
        command << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER))
                << " -std=c++20 -Wall -Wextra -Wpedantic -Werror"
                << " -I" << std::quoted(output.string()) << " -I"
                << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR)) << " -I"
                << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR)) << ' '
                << std::quoted(harness.string()) << ' '
                << std::quoted(std::string(QUARRY_TEST_RUNTIME_C_LIBRARY)) << " -o "
                << std::quoted(executable.string());
        ASSERT_EQ(std::system(command.str().c_str()), 0) << command.str();
        EXPECT_EQ(std::system(executable.string().c_str()), 0);
    }
    const auto unusual = root / "unusual.brd";
    write_text_file(
        unusual,
        "namespace: collision\nrecord: Sample\nversion: 1\ntype: data\nfields:\n"
        "  class:\n    type: uint32\n  native:\n    type: uint32\n  encode:\n    type: uint32\n"
        "  uint32_t:\n    type: uint32\n  std:\n    type: uint32\n  set_x:\n    type: uint32\n  "
        "x:\n    type: uint32\n"
        "  flags:\n    type: bool[]\n    max_elements: 2\n");
    const auto special = root / "unusual";
    const auto generated = run_tool(
        {"--cpp-storage", "c", "--output-directory", special.string(), unusual.string()}, root);
    ASSERT_EQ(generated.status, 0) << generated.stderr_text;
    const auto use = root / "unusual.cpp";
    write_text_file(use, "#include \"collision.generated.hpp\"\nint main() { "
                         "quarry::cxx::collision_Sample value; return value.has_flags(); }\n");
    std::ostringstream compile;
    compile
        << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER))
        << " -std=c++20 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti -fsyntax-only"
        << " -I" << std::quoted(special.string()) << " -I"
        << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR)) << " -I"
        << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR)) << ' '
        << std::quoted(use.string());
    EXPECT_EQ(std::system(compile.str().c_str()), 0);
    EXPECT_EQ(run_tool({"--language", "c", "--cpp-storage", "c", "schema.brd"}, root).status, 2);
    EXPECT_EQ(run_tool({"--cpp-storage", "invalid", "schema.brd"}, root).status, 2);
}

// Generated decode operations must reproduce the descriptor-only decode path
// exactly. For every field kind, in default and compact presence, decode
// mutated records through the generated types and through deep copies with
// decode_ops removed (nested records included), and require identical status,
// byte offset and destination bytes. Values are synthesized from the public
// descriptors alone, so the harness is schema-independent.
TEST(SchemaCompilerToolTest, GeneratedDecodeOpsMatchDescriptorDecodeForAllFieldKinds) {
    const auto root = make_temp_directory("c-decode-ops");
    const auto kinds = root / "kinds.brd";
    write_text_file(
        kinds,
        "namespace: opskinds\nrecord: Sample\nversion: 1\ntype: data\n"
        "enums:\n  Mode:\n    values:\n      off: 0\n      low: 3\n      high: 300\n"
        "fields:\n"
        "  flag:\n    type: bool\n  a:\n    type: i8\n  b:\n    type: u8\n"
        "  c:\n    type: i16\n  d:\n    type: u16\n  e:\n    type: i32\n  f:\n    type: u32\n"
        "  g:\n    type: i64\n  h:\n    type: u64\n  r:\n    type: f32\n  s:\n    type: f64\n"
        "  mode:\n    type: Mode\n  text:\n    type: string\n    max_bytes: 9\n"
        "  blob:\n    type: bytes\n    max_bytes: 5\n"
        "  numbers:\n    type: u16[]\n    max_elements: 4\n"
        "  modes:\n    type: Mode[]\n    max_elements: 3\n"
        "  flags:\n    type: bool[]\n    max_elements: 9\n");
    const auto schemas = std::filesystem::path(QUARRY_TEST_SOURCE_DIR) / "benchmarks/schemas";
    for (const bool compact : {false, true}) {
        const auto output = root / (compact ? "compact" : "default");
        // benchmark.workload and its dependency come from one compilation root.
        for (const auto& [schema, selection] :
             {std::pair{kinds, std::string("opskinds")},
              std::pair{schemas / "workload.brd",
                        std::string("benchmark.workload,benchmark.workload.shared")}}) {
            std::vector<std::string> options = {"--language",    "c",          "--output-directory",
                                                output.string(), "--generate", selection,
                                                schema.string()};
            if (compact)
                options.insert(options.begin(), "--c-optimize-size");
            const auto generated = run_tool(options, root);
            ASSERT_EQ(generated.status, 0) << generated.stderr_text;
        }
        const auto harness = output / "decode_ops.cpp";
        write_text_file(harness, R"CPP(
#include "opskinds.generated.h"
#include "benchmark/workload.generated.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
namespace {
uint32_t state = 2463534242U;
uint32_t next() { state ^= state << 13U; state ^= state >> 17U; state ^= state << 5U; return state; }
void fill_record(const quarry_c_type_descriptor_t& type, uint8_t* value);
void fill_element(const quarry_c_element_descriptor_t& e, uint8_t* p) {
    switch (e.kind) {
    case QUARRY_C_TYPED_BOOL: { const bool v = (next() & 1U) != 0U; std::memcpy(p, &v, sizeof(v)); break; }
    case QUARRY_C_TYPED_ENUM: e.enum_store(p, e.enum_values[next() % e.enum_count]); break;
    case QUARRY_C_TYPED_STRING:
    case QUARRY_C_TYPED_BYTES: {
        const uint32_t n = next() % (e.bound + 1U);
        for (uint32_t i = 0U; i < n; ++i)
            p[i] = static_cast<uint8_t>(e.kind == QUARRY_C_TYPED_STRING ? 'a' + next() % 26U : next());
        if (e.kind == QUARRY_C_TYPED_STRING) p[n] = 0U;
        std::memcpy(p + e.length_offset, &n, sizeof(n));
        break;
    }
    case QUARRY_C_TYPED_RECORD: fill_record(*e.record, p); break;
    default: for (size_t i = 0U; i < e.size; ++i) p[i] = static_cast<uint8_t>(next());
    }
}
void fill_record(const quarry_c_type_descriptor_t& type, uint8_t* value) {
    for (uint32_t i = 0U; i < type.layout->field_count; ++i) {
        const quarry_c_member_descriptor_t& m = type.members[i];
        const bool present = next() % 3U != 0U;
        if ((m.presence_offset & QUARRY_C_PRESENCE_COUNT_FLAG) != 0U) {
            if (!present) continue;
            if (m.max_count == 0U) { fill_element(m.element, value + m.value_offset); continue; }
            const uint32_t count = next() % (m.max_count + 1U);
            for (uint32_t j = 0U; j < count; ++j)
                fill_element(m.element, value + m.value_offset + j * m.element.size);
            std::memcpy(value + m.count_offset, &count, sizeof(count));
            continue;
        }
        if (!present) continue;
        if ((m.presence_offset & QUARRY_C_PRESENCE_PACKED_FLAG) != 0U) {
            const size_t bit = m.presence_offset & ~QUARRY_C_PRESENCE_PACKED_FLAG;
            value[bit / 8U] = static_cast<uint8_t>(value[bit / 8U] | (1U << (bit % 8U)));
        } else {
            const bool yes = true;
            std::memcpy(value + m.presence_offset, &yes, sizeof(yes));
        }
        fill_element(m.element, value + m.value_offset);
    }
}
struct Stripped { quarry_c_type_descriptor_t type; std::vector<quarry_c_member_descriptor_t> members; };
std::map<const quarry_c_type_descriptor_t*, std::unique_ptr<Stripped>> stripped;
const quarry_c_type_descriptor_t* strip(const quarry_c_type_descriptor_t* type) {
    auto& slot = stripped[type];
    if (slot) return &slot->type;
    slot = std::make_unique<Stripped>();
    Stripped* s = slot.get();
    s->type = *type;
    s->type.decode_ops = nullptr;
    s->members.assign(type->members, type->members + type->layout->field_count);
    for (auto& m : s->members)
        if (m.element.record != nullptr) m.element.record = strip(m.element.record);
    s->type.members = s->members.empty() ? nullptr : s->members.data();
    return &s->type;
}
int check(const char* name, const quarry_c_type_descriptor_t& type, uint32_t record_id) {
    if (type.decode_ops == nullptr) { std::printf("%s: no decode ops\n", name); return 1; }
    const quarry_c_type_descriptor_t* reference = strip(&type);
    size_t ok = 0U, failed = 0U;
    for (int round = 0; round < 60; ++round) {
        std::vector<uint8_t> value(type.size, 0U), wire(1U << 16U);
        fill_record(type, value.data());
        const auto encoded = quarry_c_typed_encode(&type, value.data(), record_id, wire.data(), wire.size());
        if (encoded.status != QUARRY_C_STATUS_OK) { std::printf("%s: encode %u\n", name, encoded.status); return 2; }
        wire.resize(encoded.bytes_written);
        for (int trial = 0; trial < 400; ++trial) {
            std::vector<uint8_t> bytes = wire;
            if (trial != 0) {
                static const uint8_t boundary[] = {0U, 1U, 2U, 0x80U, 0xFFU};
                for (uint32_t k = 0U, n = 1U + next() % 2U; k < n; ++k)
                    bytes[16U + next() % (bytes.size() - 16U)] =
                        next() % 2U ? boundary[next() % 5U] : static_cast<uint8_t>(next());
            }
            std::vector<uint8_t> expected(type.size, 0xA5U), actual(type.size, 0xA5U);
            const auto e = quarry_c_typed_decode(reference, bytes.data(), bytes.size(), record_id, expected.data());
            const auto a = quarry_c_typed_decode(&type, bytes.data(), bytes.size(), record_id, actual.data());
            if (a.status != e.status || a.has_byte_offset != e.has_byte_offset || a.byte_offset != e.byte_offset ||
                (e.status == QUARRY_C_STATUS_OK && actual != expected)) {
                std::printf("%s: round %d trial %d: ops %u@%zu vs descriptors %u@%zu\n", name, round, trial,
                            a.status, a.byte_offset, e.status, e.byte_offset);
                return 3;
            }
            (e.status == QUARRY_C_STATUS_OK ? ok : failed) += 1U;
        }
    }
    if (ok == 0U || failed == 0U) { std::printf("%s: ok %zu failed %zu\n", name, ok, failed); return 4; }
    return 0;
}
} // namespace
int main() {
    if (int s = check("Sample", opskinds_Sample_type, opskinds_Sample_type.layout->record_id)) return s;
    if (int s = check("Child", benchmark_workload_shared_Child_type,
                      benchmark_workload_shared_Child_type.layout->record_id)) return s;
    return check("Workload", benchmark_workload_Workload_type,
                 benchmark_workload_Workload_type.layout->record_id);
}
)CPP");
        const auto executable = output / "decode_ops";
        std::ostringstream command;
        command << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER))
                << " -std=c++20 -Wall -Wextra -Wpedantic -Werror"
                << " -I" << std::quoted(output.string()) << " -I"
                << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR)) << " -I"
                << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR)) << ' '
                << std::quoted(harness.string()) << ' '
                << std::quoted(std::string(QUARRY_TEST_RUNTIME_C_LIBRARY)) << " -o "
                << std::quoted(executable.string());
        ASSERT_EQ(std::system(command.str().c_str()), 0) << command.str();
        EXPECT_EQ(std::system(executable.string().c_str()), 0) << (compact ? "compact" : "default");
    }
}

// Two generated C++ headers that import the same record ("diamond" imports)
// must be includable in one translation unit; the shared dependency's header
// is reached twice.
TEST(SchemaCompilerToolTest, GeneratedCppHeadersSharingADependencyCompileTogether) {
    const auto root = make_temp_directory("cpp-diamond");
    write_text_file(root / "leaf.brd", "namespace: z\nrecord: Leaf\nversion: 1\ntype: data\n"
                                       "fields:\n  v:\n    type: u32\n");
    write_text_file(root / "left.brd",
                    "namespace: l\nrecord: Left\nversion: 1\ntype: data\n"
                    "imports:\n  - leaf.brd\nfields:\n  leaf:\n    type: z.Leaf\n");
    write_text_file(root / "right.brd",
                    "namespace: r\nrecord: Right\nversion: 1\ntype: data\n"
                    "imports:\n  - leaf.brd\nfields:\n  leaf:\n    type: z.Leaf\n");
    // One configuration root generates every namespace, so all three headers
    // share one configuration identity.
    write_text_file(root / "diamond.brd",
                    "configuration: diamond\nimports:\n  - left.brd\n  - right.brd\n");
    const auto output = root / "generated";
    const auto generated =
        run_tool({"--output-directory", output.string(), (root / "diamond.brd").string()}, root);
    ASSERT_EQ(generated.status, 0) << generated.stderr_text;
    EXPECT_TRUE(std::regex_search(read_text_file(output / "z.generated.hpp"),
                                  std::regex("\n#ifndef (QUARRY_GENERATED_CPP_NS_z_[0-9A-F]{32}"
                                             "_)\n#define \\1\n")));
    const auto use = root / "diamond.cpp";
    write_text_file(use, "#include \"l.generated.hpp\"\n#include \"r.generated.hpp\"\n"
                         "#include \"z.generated.hpp\"\n"
                         "int main() { return l::decode_Left({}).has_value() ||\n"
                         "                    r::decode_Right({}).has_value() ? 1 : 0; }\n");
    std::ostringstream compile;
    compile << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER))
            << " -std=c++20" QUARRY_TEST_STRICT_CXX_WARNINGS " -fsyntax-only" << " -I"
            << std::quoted(output.string()) << " -I"
            << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR)) << " -I"
            << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR)) << ' '
            << std::quoted(use.string());
    EXPECT_EQ(std::system(compile.str().c_str()), 0) << compile.str();
}

// Both translation units reach the shared leaf through a diamond and include
// the same generated functions. This must link at -O0, without relying on the
// optimizer to erase unused definitions.
TEST(SchemaCompilerToolTest, GeneratedCppHeadersSharingADependencyLinkAcrossTranslationUnits) {
    const auto root = make_temp_directory("cpp-multiple-translation-units");
    write_text_file(root / "leaf.brd", "namespace: z\nrecord: Leaf\nversion: 1\ntype: data\n"
                                       "fields:\n  v:\n    type: u32\n");
    for (const auto& [file, ns, record] :
         {std::tuple{"left.brd", "l", "Left"}, std::tuple{"right.brd", "r", "Right"}}) {
        write_text_file(root / file, "namespace: " + std::string(ns) + "\nrecord: " + record +
                                         "\nversion: 1\ntype: data\nimports:\n  - leaf.brd\n"
                                         "fields:\n  leaf:\n    type: z.Leaf\n");
    }
    write_text_file(root / "configuration.brd",
                    "configuration: diamond\nimports:\n  - left.brd\n  - right.brd\n");
    const auto output = root / "generated";
    const auto generated = run_tool(
        {"--output-directory", output.string(), (root / "configuration.brd").string()}, root);
    ASSERT_EQ(generated.status, 0) << generated.stderr_text;
    const std::string includes = "#include \"l.generated.hpp\"\n"
                                 "#include \"r.generated.hpp\"\n"
                                 "#include \"z.generated.hpp\"\n";
    const auto first = root / "first.cpp";
    write_text_file(first, includes + R"(
bool first_round_trip() {
    z::LeafBuilder leaf;
    leaf.set_v(42);
    l::LeftBuilder builder;
    builder.set_leaf(leaf.build());
    auto bytes = l::encode_result(builder.build());
    if (!bytes.value) return false;
    auto decoded = l::decode_Left_result(*bytes.value);
    return decoded.value && decoded.value->leaf() && decoded.value->leaf()->v() &&
           *decoded.value->leaf()->v() == 42;
}
)");
    const auto second = root / "second.cpp";
    write_text_file(second, includes + R"(
bool first_round_trip();
int main() {
    z::LeafBuilder leaf;
    leaf.set_v(7);
    r::RightBuilder builder;
    builder.set_leaf(leaf.build());
    auto bytes = r::encode(builder.build());
    if (!bytes) return 1;
    auto decoded = r::decode_Right(*bytes);
    return first_round_trip() && decoded && decoded->leaf() && decoded->leaf()->v() &&
           *decoded->leaf()->v() == 7 ? 0 : 1;
}
)");
    const auto executable = root / "multiple-tu";
    std::ostringstream compile;
    compile << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER))
            << " -std=c++20 -O0" QUARRY_TEST_STRICT_CXX_WARNINGS << " -I"
            << std::quoted(output.string()) << " -I"
            << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR)) << " -I"
            << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR)) << ' '
            << std::quoted(first.string()) << ' ' << std::quoted(second.string()) << " -o "
            << std::quoted(executable.string());
    ASSERT_EQ(std::system(compile.str().c_str()), 0) << compile.str();
    std::ostringstream run;
    run << std::quoted(executable.string());
    EXPECT_EQ(std::system(run.str().c_str()), 0);
}

TEST(SchemaCompilerToolTest, FileExtensionIsRejectedWithLanguageC) {
    const std::filesystem::path root = make_temp_directory("language-c-file-extension");
    const std::filesystem::path input = root / "schema.brd";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult result =
        run_tool({"--language", "c", "--file-extension", ".h", input.string()}, root);

    EXPECT_EQ(result.status, 2);
    EXPECT_NE(result.stderr_text.find("--file-extension is not supported with --language c"),
              std::string::npos);
}

TEST(SchemaCompilerToolTest, CBackendWritesHeaderAndSourceWithExpectedContent) {
    const std::filesystem::path root = make_temp_directory("language-c-generate");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    const CommandResult result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stderr_text.empty());

    const std::filesystem::path header = output / "quarry" / "telemetry.generated.h";
    const std::filesystem::path source = output / "quarry" / "telemetry.generated.c";
    ASSERT_TRUE(std::filesystem::exists(header));
    ASSERT_TRUE(std::filesystem::exists(source));

    const std::string header_text = read_text_file(header);
    EXPECT_NE(header_text.find("quarry_telemetry_Sample_t"), std::string::npos);
    EXPECT_NE(header_text.find("extern \"C\""), std::string::npos);

    const std::string source_text = read_text_file(source);
    EXPECT_NE(source_text.find("#include \"quarry/telemetry.generated.h\""), std::string::npos);
    EXPECT_EQ(source_text.find("quarry_telemetry_Sample_init"), std::string::npos);
    EXPECT_NE(header_text.find("static inline void quarry_telemetry_Sample_init"), std::string::npos);
}

TEST(SchemaCompilerToolTest, CBackendRejectsUnsupportedFieldType) {
    // An array whose element type is an enum with a negative declared
    // value remains unsupported after PR-112 added bounded arrays of
    // scalar/enum elements -- used here as the representative
    // still-unsupported type, reachable through the normative YAML
    // frontend within a single file/namespace (unlike array<string>/
    // array<bytes>, neither of which the current YAML grammar can express
    // standalone: arrays reject a field-level `max_bytes` outright.
    // Record-typed array elements are reachable standalone when
    // self-referential -- see
    // CBackendRejectsSelfReferentialArrayOfRecordsWithCycleDiagnostic
    // below -- but a *different*, distinctly-named record element would
    // need a second record declaration in this same file, which this
    // compiler does not support yet ("one primary record per schema
    // file"; see CLAUDE.md and compiler/backend_c/README.md).
    const std::filesystem::path root = make_temp_directory("language-c-unsupported-field");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  label:\n"
                    "    type: Status[]\n"
                    "    max_elements: 4\n"
                    "enums:\n"
                    "  Status:\n"
                    "    values:\n"
                    "      OK: 0\n"
                    "      NEGATIVE: -1\n");

    const CommandResult result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);

    EXPECT_NE(result.status, 0);
    EXPECT_NE(result.stderr_text.find("quarry.telemetry.Sample.label"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SchemaCompilerToolTest, CBackendRejectsSelfReferentialArrayOfRecordsWithCycleDiagnostic) {
    // A record containing an array of itself is a hard C-language
    // impossibility (a fixed-size array member requires a complete
    // element type, and there is no valid declaration order for a record
    // embedding an array of itself). Unlike a genuinely two-distinct-
    // record array-of-records schema (not YAML-expressible -- see the
    // comment on CBackendRejectsUnsupportedFieldType above), a
    // self-referential record needs only one record declaration, so this
    // is reachable through a real `.brd` file and the installed CLI tool,
    // not just direct Schema IR construction.
    const std::filesystem::path root =
        make_temp_directory("language-c-self-referential-array-cycle");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.tree\n"
                    "record: Node\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  children:\n"
                    "    type: Node[]\n"
                    "    max_elements: 4\n");

    const CommandResult result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);

    EXPECT_NE(result.status, 0);
    EXPECT_NE(result.stderr_text.find("cycle"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(SchemaCompilerToolTest, CBackendGeneratesScalarStructAndCodecApi) {
    const std::filesystem::path root = make_temp_directory("language-c-scalar-generate");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n"
                    "  ratio:\n"
                    "    type: float32\n");

    const CommandResult result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stderr_text.empty());

    const std::filesystem::path header = output / "quarry" / "telemetry.generated.h";
    const std::string header_text = read_text_file(header);
    EXPECT_NE(header_text.find("bool has_count;"), std::string::npos);
    EXPECT_NE(header_text.find("uint32_t count;"), std::string::npos);
    EXPECT_NE(header_text.find("bool has_ratio;"), std::string::npos);
    EXPECT_NE(header_text.find("float ratio;"), std::string::npos);
    EXPECT_NE(header_text.find("quarry_telemetry_Sample_encode_result_t"), std::string::npos);
    EXPECT_NE(header_text.find("quarry_telemetry_Sample_decode_result_t"), std::string::npos);
}

TEST(SchemaCompilerToolTest, CBackendGeneratesEnumFieldAndCompilesAsC) {
    const std::filesystem::path root = make_temp_directory("language-c-enum-field");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n"
                    "  status:\n"
                    "    type: Status\n"
                    "enums:\n"
                    "  Status:\n"
                    "    values:\n"
                    "      OK: 0\n"
                    "      WARNING: 1\n"
                    "      ERROR: 2\n");

    const CommandResult result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stderr_text.empty());

    const std::filesystem::path header = output / "quarry" / "telemetry.generated.h";
    const std::string header_text = read_text_file(header);
    EXPECT_NE(header_text.find("typedef enum {"), std::string::npos);
    EXPECT_NE(header_text.find("QUARRY_TELEMETRY_STATUS_OK = 0"), std::string::npos);
    EXPECT_NE(header_text.find("QUARRY_TELEMETRY_STATUS_WARNING = 1"), std::string::npos);
    EXPECT_NE(header_text.find("QUARRY_TELEMETRY_STATUS_ERROR = 2"), std::string::npos);
    EXPECT_NE(header_text.find("} quarry_telemetry_Status_t;"), std::string::npos);
    EXPECT_NE(header_text.find("quarry_telemetry_Status_t status;"), std::string::npos);

    const std::filesystem::path source = output / "quarry" / "telemetry.generated.c";
    const std::filesystem::path object_file = root / "telemetry.generated.o";
    ASSERT_TRUE(std::filesystem::exists(source));

    // Compile-only (no link stage) with strict warnings, matching the
    // already-safe pattern CBackendGeneratedSourceCompilesAsC uses; the
    // interop test's own history shows a combined compile+link invocation
    // has a GCC-specific -std=c99 rejection this pattern avoids entirely.
    std::ostringstream command;
    command << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER)) << " -x c -std=c99"
            << " -Wall -Wextra -Wpedantic -Werror"
            << " -I" << std::quoted(output.string())
            << " -I" << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR))
            << " -I" << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR))
            << " -c " << std::quoted(source.string()) << " -o "
            << std::quoted(object_file.string());
    const int status = std::system(command.str().c_str());
    EXPECT_EQ(status, 0) << "command: " << command.str();
    EXPECT_TRUE(std::filesystem::exists(object_file));
}

TEST(SchemaCompilerToolTest, CBackendGeneratesStringFieldAndCompilesAsC) {
    const std::filesystem::path root = make_temp_directory("language-c-string-field");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n"
                    "  label:\n"
                    "    type: string\n"
                    "    max_bytes: 16\n");

    const CommandResult result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stderr_text.empty());

    const std::filesystem::path header = output / "quarry" / "telemetry.generated.h";
    const std::string header_text = read_text_file(header);
    EXPECT_EQ(header_text.find("bool has_label;"), std::string::npos);
    EXPECT_NE(header_text.find("char label[17];"), std::string::npos);
    EXPECT_NE(header_text.find("uint32_t label_length;"), std::string::npos);

    const std::filesystem::path source = output / "quarry" / "telemetry.generated.c";
    const std::filesystem::path object_file = root / "telemetry.generated.o";
    ASSERT_TRUE(std::filesystem::exists(source));

    // Compile-only (no link stage) with strict warnings -- same safe
    // pattern as the scalar/enum field compile tests above.
    std::ostringstream command;
    command << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER)) << " -x c -std=c99"
            << " -Wall -Wextra -Wpedantic -Werror"
            << " -I" << std::quoted(output.string())
            << " -I" << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR))
            << " -I" << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR))
            << " -c " << std::quoted(source.string()) << " -o "
            << std::quoted(object_file.string());
    const int status = std::system(command.str().c_str());
    EXPECT_EQ(status, 0) << "command: " << command.str();
    EXPECT_TRUE(std::filesystem::exists(object_file));
}

TEST(SchemaCompilerToolTest, CBackendGeneratesBytesFieldAndCompilesAsC) {
    const std::filesystem::path root = make_temp_directory("language-c-bytes-field");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n"
                    "  blob:\n"
                    "    type: bytes\n"
                    "    max_bytes: 16\n");

    const CommandResult result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stderr_text.empty());

    const std::filesystem::path header = output / "quarry" / "telemetry.generated.h";
    const std::string header_text = read_text_file(header);
    EXPECT_EQ(header_text.find("bool has_blob;"), std::string::npos);
    EXPECT_NE(header_text.find("uint8_t blob[16];"), std::string::npos);
    EXPECT_NE(header_text.find("uint32_t blob_length;"), std::string::npos);

    const std::filesystem::path source = output / "quarry" / "telemetry.generated.c";
    const std::filesystem::path object_file = root / "telemetry.generated.o";
    ASSERT_TRUE(std::filesystem::exists(source));

    // Compile-only (no link stage) with strict warnings -- same safe
    // pattern as the scalar/enum/string field compile tests above.
    std::ostringstream command;
    command << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER)) << " -x c -std=c99"
            << " -Wall -Wextra -Wpedantic -Werror"
            << " -I" << std::quoted(output.string())
            << " -I" << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR))
            << " -I" << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR))
            << " -c " << std::quoted(source.string()) << " -o "
            << std::quoted(object_file.string());
    const int status = std::system(command.str().c_str());
    EXPECT_EQ(status, 0) << "command: " << command.str();
    EXPECT_TRUE(std::filesystem::exists(object_file));
}

TEST(SchemaCompilerToolTest, CBackendGeneratesArrayFieldAndCompilesAsC) {
    const std::filesystem::path root = make_temp_directory("language-c-array-field");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n"
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
                    "      ERROR: 2\n");

    const CommandResult result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);

    EXPECT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stderr_text.empty());

    const std::filesystem::path header = output / "quarry" / "telemetry.generated.h";
    const std::string header_text = read_text_file(header);
    EXPECT_EQ(header_text.find("bool has_readings;"), std::string::npos);
    EXPECT_NE(header_text.find("float readings[4];"), std::string::npos);
    EXPECT_NE(header_text.find("uint32_t readings_count;"), std::string::npos);
    EXPECT_NE(header_text.find("quarry_telemetry_Status_t statuses[3];"), std::string::npos);
    EXPECT_NE(header_text.find("uint32_t statuses_count;"), std::string::npos);

    const std::filesystem::path source = output / "quarry" / "telemetry.generated.c";
    const std::filesystem::path object_file = root / "telemetry.generated.o";
    ASSERT_TRUE(std::filesystem::exists(source));

    // Compile-only (no link stage) with strict warnings -- same safe
    // pattern as the scalar/enum/string/bytes field compile tests above.
    std::ostringstream command;
    command << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER)) << " -x c -std=c99"
            << " -Wall -Wextra -Wpedantic -Werror"
            << " -I" << std::quoted(output.string())
            << " -I" << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR))
            << " -I" << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR))
            << " -c " << std::quoted(source.string()) << " -o "
            << std::quoted(object_file.string());
    const int status = std::system(command.str().c_str());
    EXPECT_EQ(status, 0) << "command: " << command.str();
    EXPECT_TRUE(std::filesystem::exists(object_file));
}

TEST(SchemaCompilerToolTest, CBackendGeneratedSourceCompilesAsC) {
    const std::filesystem::path root = make_temp_directory("language-c-compiles");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: quarry.telemetry\n"
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
                    "    type: int8\n");

    const CommandResult generate_result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);
    ASSERT_EQ(generate_result.status, 0) << generate_result.stderr_text;

    const std::filesystem::path source = output / "quarry" / "telemetry.generated.c";
    const std::filesystem::path object_file = root / "telemetry.generated.o";
    ASSERT_TRUE(std::filesystem::exists(source));

    // Compiled as genuine C (via -x c on the configured C++ compiler, not
    // C++) with the same strict warning flags this repository builds with,
    // to prove the C backend's output is actually valid, clean ISO C99 --
    // not merely C-like text compiled by a C++ frontend.
    std::ostringstream command;
    command << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER)) << " -x c -std=c99"
            << " -Wall -Wextra -Wpedantic -Werror"
            << " -I" << std::quoted(output.string())
            << " -I" << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR))
            << " -I" << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR))
            << " -c " << std::quoted(source.string()) << " -o "
            << std::quoted(object_file.string());
    const int status = std::system(command.str().c_str());
    EXPECT_EQ(status, 0) << "command: " << command.str();
    EXPECT_TRUE(std::filesystem::exists(object_file));
}

// --- A3: configuration roots and --generate ---------------------------------

namespace {

[[nodiscard]] std::filesystem::path truck_schemas() {
    return std::filesystem::path(QUARRY_TEST_SOURCE_DIR) / "examples" / "truck" / "schemas";
}

const std::vector<std::string> kTruckNamespaces = {
    "truck", "truck.brakes", "truck.cms", "truck.common", "truck.engine", "truck.transmission"};

// Language options for every backend (C, C++ owning, C++/C storage, Python, Go).
const std::vector<std::pair<std::string, std::vector<std::string>>> kBackends = {
    {"c", {"--language", "c"}},
    {"cpp", {"--language", "cpp"}},
    {"cpp-c-storage", {"--language", "cpp", "--cpp-storage", "c"}},
    {"python", {"--language", "python"}},
    {"go", {"--language", "go", "--go-import-prefix", "example.com/truck"}},
};

[[nodiscard]] std::string read_binary_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

// Relative path -> content of every file under `directory`.
[[nodiscard]] std::map<std::string, std::string> tree(const std::filesystem::path& directory) {
    std::map<std::string, std::string> files;
    for (const std::filesystem::path& file : regular_files_under(directory)) {
        files[std::filesystem::relative(file, directory).generic_string()] = read_binary_file(file);
    }
    return files;
}

[[nodiscard]] std::vector<std::string> with(std::vector<std::string> arguments,
                                            const std::vector<std::string>& more) {
    arguments.insert(arguments.end(), more.begin(), more.end());
    return arguments;
}

[[nodiscard]] std::string join(const std::vector<std::string>& parts) {
    std::string joined;
    for (const std::string& part : parts) {
        joined += (joined.empty() ? "" : ",") + part;
    }
    return joined;
}

[[nodiscard]] std::string qbs_for(const std::filesystem::path& schema,
                                  const std::vector<std::string>& extra,
                                  const std::filesystem::path& root, const std::string& name) {
    const std::filesystem::path qbs = root / (name + ".qbs");
    const CommandResult result =
        run_tool(with({"--emit-qbs", qbs.string()}, with(extra, {schema.string()})), root);
    EXPECT_EQ(result.status, 0) << result.stderr_text;
    return read_binary_file(qbs);
}

[[nodiscard]] std::string hex_string(std::string_view bytes) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2U);
    for (const char character : bytes) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(kHexDigits[byte >> 4U]);
        result.push_back(kHexDigits[byte & 0x0FU]);
    }
    return result;
}

[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> bytes) {
    const auto digest = quarry::compiler::qbs::sha256(bytes);
    return hex_string(
        std::string_view(reinterpret_cast<const char*>(digest.data()), digest.size()));
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

[[nodiscard]] std::string tree_sha256(const std::filesystem::path& directory) {
    std::vector<std::uint8_t> contents;
    for (const auto& [relative_path, file_contents] : tree(directory)) {
        append_u64(contents, relative_path.size());
        contents.insert(contents.end(), relative_path.begin(), relative_path.end());
        append_u64(contents, file_contents.size());
        contents.insert(contents.end(), file_contents.begin(), file_contents.end());
    }
    return sha256_hex(contents);
}

[[nodiscard]] std::string qbs_schema_id(std::string_view image) {
    if (image.size() < 28U) {
        return {};
    }
    return hex_string(image.substr(12U, 16U));
}

// Every `<symbol>_brf_v2_layout = { <id>U` (own ID) and `&<symbol>_type, <id>U`
// (expected nested ID) in generated C sources.
[[nodiscard]] std::multimap<std::string, std::uint32_t>
c_record_ids(const std::filesystem::path& directory, const std::string& pattern) {
    std::multimap<std::string, std::uint32_t> ids;
    const std::regex expression(pattern);
    for (const auto& [path, content] : tree(directory)) {
        for (auto it = std::sregex_iterator(content.begin(), content.end(), expression);
             it != std::sregex_iterator(); ++it) {
            ids.emplace((*it)[1].str(), static_cast<std::uint32_t>(std::stoul((*it)[2].str())));
        }
    }
    return ids;
}

const std::string kOwnId = R"((\w+)_brf_v2_layout = \{ (\d+)U)";
const std::string kNestedId = R"(&(\w+)_type, (\d+)U)";

} // namespace

TEST(SchemaCompilerToolTest, PrintSchemaIdMatchesTruckGoldenAndQbsHeader) {
    const std::filesystem::path root = make_temp_directory("a4-1-schema-id-golden");
    const std::filesystem::path schema = truck_schemas() / "configuration.brd";

    const CommandResult query = run_tool({"--print-schema-id", schema.string()}, root);
    ASSERT_EQ(query.status, 0) << query.stderr_text;
    EXPECT_EQ(query.stdout_text, "9200ca494f010801b830d0857cf66dde\n");
    EXPECT_TRUE(query.stderr_text.empty());

    const std::string minimal = qbs_for(schema, {}, root, "minimal");
    const std::string reflective = qbs_for(schema, {"--qbs-reflective"}, root, "reflective");
    EXPECT_EQ(qbs_schema_id(minimal), query.stdout_text.substr(0U, 32U));
    EXPECT_EQ(qbs_schema_id(reflective), query.stdout_text.substr(0U, 32U));
}

TEST(SchemaCompilerToolTest, PrintSchemaIdIgnoresImportOrderConfigurationNameAndSelection) {
    const std::filesystem::path root = make_temp_directory("a4-1-schema-id-invariance");
    const std::filesystem::path first = root / "first";
    const std::filesystem::path second = root / "second";
    std::filesystem::copy(truck_schemas(), first, std::filesystem::copy_options::recursive);
    std::filesystem::copy(truck_schemas(), second, std::filesystem::copy_options::recursive);

    const std::vector<std::string> imports = {"vehicle.brd", "engine.brd", "transmission.brd",
                                              "brakes.brd", "cms.brd"};
    std::vector<std::string> reversed_imports = imports;
    std::reverse(reversed_imports.begin(), reversed_imports.end());
    const auto configuration = [](std::string_view name,
                                  const std::vector<std::string>& import_paths) {
        std::string text = "configuration: " + std::string(name) + "\nimports:\n";
        for (const std::string& import_path : import_paths) {
            text += "  - " + import_path + "\n";
        }
        return text;
    };
    write_text_file(first / "identity-root.brd", configuration("name_a", imports));
    write_text_file(second / "identity-root.brd", configuration("name_b", reversed_imports));

    const CommandResult first_query =
        run_tool({"--print-schema-id", (first / "identity-root.brd").string()}, root);
    const CommandResult second_query =
        run_tool({"--print-schema-id", (second / "identity-root.brd").string()}, root);
    ASSERT_EQ(first_query.status, 0) << first_query.stderr_text;
    ASSERT_EQ(second_query.status, 0) << second_query.stderr_text;
    EXPECT_EQ(first_query.stdout_text, second_query.stdout_text);

    const CommandResult selected = run_tool(
        {"--print-schema-id", "--generate", "truck.engine", (first / "identity-root.brd").string()},
        root);
    ASSERT_EQ(selected.status, 0) << selected.stderr_text;
    EXPECT_EQ(selected.stdout_text, first_query.stdout_text);
}

TEST(SchemaCompilerToolTest, PrintSchemaIdIgnoresSourcePathAndDiffersByResolvedConfiguration) {
    const std::filesystem::path root = make_temp_directory("a4-1-schema-id-path");
    const std::string schema_text =
        "namespace: quarry.identity\nrecord: Sample\nversion: 1\ntype: data\nfields:\n"
        "  value:\n    type: uint32\n";
    const std::filesystem::path first_schema = root / "one" / "schema.brd";
    const std::filesystem::path second_schema = root / "another" / "schema.brd";
    write_text_file(first_schema, schema_text);
    write_text_file(second_schema, schema_text);

    const CommandResult first = run_tool({"--print-schema-id", first_schema.string()}, root);
    const CommandResult second = run_tool({"--print-schema-id", second_schema.string()}, root);
    ASSERT_EQ(first.status, 0) << first.stderr_text;
    ASSERT_EQ(second.status, 0) << second.stderr_text;
    EXPECT_EQ(first.stdout_text, second.stdout_text);

    const CommandResult truck =
        run_tool({"--print-schema-id", (truck_schemas() / "configuration.brd").string()}, root);
    const CommandResult component =
        run_tool({"--print-schema-id", (truck_schemas() / "engine.brd").string()}, root);
    ASSERT_EQ(truck.status, 0) << truck.stderr_text;
    ASSERT_EQ(component.status, 0) << component.stderr_text;
    EXPECT_NE(truck.stdout_text, component.stdout_text);
}

TEST(SchemaCompilerToolTest, PrintSchemaIdInvalidSchemaHasDiagnosticsAndNoStdout) {
    const std::filesystem::path root = make_temp_directory("a4-1-invalid-schema-id");
    const std::filesystem::path schema = root / "invalid.brd";
    write_text_file(schema, "namespace: quarry.identity\nrecord: Sample\nfields: [\n");

    const CommandResult result = run_tool({"--print-schema-id", schema.string()}, root);

    EXPECT_NE(result.status, 0);
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_FALSE(result.stderr_text.empty());
}

TEST(SchemaCompilerToolTest, TruckBindingsAndQbsImagesMatchGoldenArtifacts) {
    const std::filesystem::path root = make_temp_directory("a4-1-main-output-golden");
    const std::filesystem::path schema = truck_schemas() / "configuration.brd";
    const std::map<std::string, std::string> expected_tree_hashes = {
        {"c", "7eb10c5b7a58b7dcda22ca46ca929dfc33887c6c3718bb01a92912cc1858b0bc"},
        {"cpp", "172a0080d871bfe77a6e932c7ea811d767b7f015544eece393843b9d439f2559"},
        {"cpp-c-storage", "f65075d8520dc0b635a3a3d0852a917d5e733bae2e570f8382a8e1220ff0e1de"},
        {"python", "18b06c9077b0efbf106c963289c68947fcf8d04278564b999775385a25b5497e"},
        {"go", "c8fe744942d020fa93488fd3bc9275ead9e1641c9b204ff50b53a33800f0d4fa"},
    };

    for (const auto& [name, options] : kBackends) {
        const std::filesystem::path output = root / name;
        const CommandResult generated = run_tool(
            with(with(options, {"--output-directory", output.string()}), {schema.string()}), root);
        ASSERT_EQ(generated.status, 0) << name << ": " << generated.stderr_text;
        ASSERT_TRUE(expected_tree_hashes.contains(name));
        EXPECT_EQ(tree_sha256(output), expected_tree_hashes.at(name)) << name;
    }

    const std::string minimal = qbs_for(schema, {}, root, "minimal");
    const std::string reflective = qbs_for(schema, {"--qbs-reflective"}, root, "reflective");
    EXPECT_EQ(sha256_hex(
                  std::span(reinterpret_cast<const std::uint8_t*>(minimal.data()), minimal.size())),
              "d6b1c74fd51d9b48083fbe849e8ae5cfe68f26ae18b126bb0fc05a76ab9ab960");
    EXPECT_EQ(sha256_hex(std::span(reinterpret_cast<const std::uint8_t*>(reflective.data()),
                                   reflective.size())),
              "a987d9b1abb7435e72fefec2a5eec6d7661ef4955db3bf0f44b163c8be7a3d3c");
}

TEST(SchemaCompilerToolTest, ConfigurationRootGeneratesEveryNamespaceByDefault) {
    const std::filesystem::path root = make_temp_directory("a3-configuration-default");
    const CommandResult listed =
        run_tool({"--language", "c", "--list-outputs", "--output-directory", "out",
                  (truck_schemas() / "configuration.brd").string()},
                 root);
    ASSERT_EQ(listed.status, 0) << listed.stderr_text;
    std::set<std::string> namespaces_seen;
    for (const std::string& line : non_empty_lines(listed.stdout_text)) {
        namespaces_seen.insert(line);
    }
    EXPECT_EQ(namespaces_seen.size(), 2U * kTruckNamespaces.size()) << listed.stdout_text;
    EXPECT_TRUE(namespaces_seen.contains("out/truck.generated.h"));
    EXPECT_TRUE(namespaces_seen.contains("out/truck/transmission.generated.c"));
}

TEST(SchemaCompilerToolTest, RecordRootStillGeneratesOnlyItsOwnNamespace) {
    const std::filesystem::path root = make_temp_directory("a3-record-root-default");
    const CommandResult listed =
        run_tool({"--language", "c", "--list-outputs", "--output-directory", "out",
                  (truck_schemas() / "engine.brd").string()},
                 root);
    ASSERT_EQ(listed.status, 0) << listed.stderr_text;
    EXPECT_EQ(
        non_empty_lines(listed.stdout_text),
        (std::vector<std::string>{"out/truck/engine.generated.h", "out/truck/engine.generated.c"}));
}

TEST(SchemaCompilerToolTest, ListOutputsMatchesGenerationForEverySelectionAndBackend) {
    const std::vector<std::vector<std::string>> selections = {
        {}, {"truck.engine"}, {"truck.engine", "truck.common"}, kTruckNamespaces};
    for (const auto& [name, language] : kBackends) {
        for (const std::vector<std::string>& selection : selections) {
            const std::filesystem::path root = make_temp_directory("a3-list-" + name);
            std::vector<std::string> arguments = language;
            if (!selection.empty()) {
                arguments = with(arguments, {"--generate", join(selection)});
            }
            arguments = with(arguments, {"--output-directory", "out",
                                         (truck_schemas() / "configuration.brd").string()});
            const CommandResult listed = run_tool(with({"--list-outputs"}, arguments), root);
            ASSERT_EQ(listed.status, 0) << name << ": " << listed.stderr_text;
            const CommandResult generated = run_tool(arguments, root);
            ASSERT_EQ(generated.status, 0) << name << ": " << generated.stderr_text;

            std::vector<std::string> listed_paths = non_empty_lines(listed.stdout_text);
            std::sort(listed_paths.begin(), listed_paths.end());
            std::vector<std::string> generated_paths;
            for (const auto& [path, content] : tree(root / "out")) {
                generated_paths.push_back("out/" + path);
            }
            EXPECT_EQ(listed_paths, generated_paths) << name << " --generate " << join(selection);
            EXPECT_FALSE(generated_paths.empty());
        }
    }
}

TEST(SchemaCompilerToolTest, SeparatelyGeneratedNamespacesReproduceFullGeneration) {
    // Components generated in separate runs from the same configuration root
    // are byte-identical to one full generation, so they compose exactly.
    for (const auto& [name, language] : kBackends) {
        const std::filesystem::path root = make_temp_directory("a3-compose-" + name);
        const std::string schema = (truck_schemas() / "configuration.brd").string();
        const CommandResult all =
            run_tool(with(language, {"--output-directory", "all", schema}), root);
        ASSERT_EQ(all.status, 0) << name << ": " << all.stderr_text;
        for (const std::string& ns : kTruckNamespaces) {
            const CommandResult one = run_tool(
                with(language, {"--generate", ns, "--output-directory", "split", schema}), root);
            ASSERT_EQ(one.status, 0) << name << " " << ns << ": " << one.stderr_text;
        }
        EXPECT_EQ(tree(root / "split"), tree(root / "all")) << name;
    }
}

TEST(SchemaCompilerToolTest, MultiFileNamespaceIsGeneratedAsOneUnit) {
    // truck.engine is declared by engine.brd and engine_faults.brd.
    const std::filesystem::path root = make_temp_directory("a3-multi-file-namespace");
    const CommandResult generated =
        run_tool({"--language", "c", "--generate", "truck.engine", "--output-directory", "out",
                  (truck_schemas() / "configuration.brd").string()},
                 root);
    ASSERT_EQ(generated.status, 0) << generated.stderr_text;
    const std::string header = read_text_file(root / "out" / "truck" / "engine.generated.h");
    EXPECT_NE(header.find("truck_engine_EngineStatus_t"), std::string::npos);
    EXPECT_NE(header.find("truck_engine_FaultCode_t"), std::string::npos);
    EXPECT_EQ(tree(root / "out").size(), 2U);
}

TEST(SchemaCompilerToolTest, GenerationSelectionNeverChangesIdsOrQbs) {
    const std::filesystem::path root = make_temp_directory("a3-qbs-invariance");
    const std::filesystem::path schema = truck_schemas() / "configuration.brd";
    const std::string full = qbs_for(schema, {}, root, "default");
    ASSERT_FALSE(full.empty());
    EXPECT_EQ(qbs_for(schema, {"--generate", "truck.engine"}, root, "one"), full);
    EXPECT_EQ(qbs_for(schema, {"--generate", "truck.engine,truck.common"}, root, "two"), full);
    EXPECT_EQ(qbs_for(schema, {"--generate", join(kTruckNamespaces)}, root, "all"), full);

    // The record IDs embedded in generated code are the reference
    // architecture's, whatever the selection.
    const std::map<std::string, std::uint32_t> expected = {
        {"truck_VehicleSnapshot", 0x00010001U},    {"truck_brakes_AbsEvent", 0x00020001U},
        {"truck_brakes_BrakeStatus", 0x00020002U}, {"truck_cms_CameraHealth", 0x00030001U},
        {"truck_common_Timestamp", 0x00040001U},   {"truck_engine_EngineStatus", 0x00050001U},
        {"truck_engine_FaultCode", 0x00050002U},   {"truck_transmission_GearState", 0x00060001U}};
    for (const std::vector<std::string>& selection : std::vector<std::vector<std::string>>{
             {"truck.engine", "truck.common"}, {"truck.transmission"}, kTruckNamespaces}) {
        const std::filesystem::path out = root / ("c-" + std::to_string(selection.size()));
        const CommandResult generated =
            run_tool({"--language", "c", "--generate", join(selection), "--output-directory",
                      out.string(), schema.string()},
                     root);
        ASSERT_EQ(generated.status, 0) << generated.stderr_text;
        for (const std::string& pattern : {kOwnId, kNestedId}) {
            for (const auto& [symbol, id] : c_record_ids(out, pattern)) {
                ASSERT_TRUE(expected.contains(symbol)) << symbol;
                EXPECT_EQ(id, expected.at(symbol)) << symbol << " with " << join(selection);
            }
        }
    }
}

TEST(SchemaCompilerToolTest, UnknownGenerateNamespaceFailsWithoutWritingFiles) {
    const std::filesystem::path root = make_temp_directory("a3-unknown-namespace");
    const CommandResult result =
        run_tool({"--language", "c", "--generate", "truck.engine,truck.bogus", "--output-directory",
                  "out", (truck_schemas() / "configuration.brd").string()},
                 root);
    EXPECT_EQ(result.status, 1);
    EXPECT_NE(result.stderr_text.find("BC8002"), std::string::npos) << result.stderr_text;
    EXPECT_NE(result.stderr_text.find("truck.bogus"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(root / "out"));

    const CommandResult empty = run_tool(
        {"--generate", "truck.engine,", (truck_schemas() / "configuration.brd").string()}, root);
    EXPECT_NE(empty.status, 0);
    EXPECT_NE(empty.stderr_text.find("empty namespace"), std::string::npos) << empty.stderr_text;
}

TEST(SchemaCompilerToolTest, ConfigurationMetadataDoesNotAffectSchemaIdentity) {
    // vehicle.brd already imports every truck schema, so a configuration that
    // imports only vehicle.brd, the full configuration, and vehicle.brd
    // compiled directly all resolve the same schema: identical QBS bytes.
    const std::filesystem::path root = make_temp_directory("a3-configuration-identity");
    write_text_file(root / "only_vehicle.brd",
                    "configuration: other_name\nimports:\n  - " +
                        (truck_schemas() / "vehicle.brd").generic_string() + "\n");
    const std::string direct = qbs_for(truck_schemas() / "vehicle.brd", {}, root, "direct");
    ASSERT_FALSE(direct.empty());
    EXPECT_EQ(qbs_for(truck_schemas() / "configuration.brd", {}, root, "full"), direct);
    EXPECT_EQ(qbs_for(root / "only_vehicle.brd", {}, root, "named"), direct);
}

TEST(SchemaCompilerToolTest, IndependentComponentRootIsADifferentConfiguration) {
    // Compiling engine.brd as its own root is valid but defines a different
    // configuration: truck.common = 1, truck.engine = 2.
    const std::filesystem::path root = make_temp_directory("a3-component-root");
    const std::string component = qbs_for(truck_schemas() / "engine.brd", {}, root, "component");
    const std::string configuration =
        qbs_for(truck_schemas() / "configuration.brd", {}, root, "configuration");
    EXPECT_NE(component, configuration);

    const CommandResult generated = run_tool(
        {"--language", "c", "--output-directory", "out", (truck_schemas() / "engine.brd").string()},
        root);
    ASSERT_EQ(generated.status, 0) << generated.stderr_text;
    const auto ids = c_record_ids(root / "out", kOwnId);
    ASSERT_TRUE(ids.contains("truck_engine_EngineStatus"));
    EXPECT_EQ(ids.find("truck_engine_EngineStatus")->second, 0x00020001U);
}

TEST(SchemaCompilerToolTest, RootBasedGenerationResolvesTopMidLeafIdDisagreement) {
    // The original defect: generating each schema from its own root gives each
    // file a different configuration, so a parent's expected nested record ID
    // disagrees with the ID the child's own bindings write.
    const std::filesystem::path root = make_temp_directory("a3-top-mid-leaf");
    write_text_file(root / "leaf.brd", "namespace: z\nrecord: Leaf\nversion: 1\ntype: data\n"
                                       "fields:\n  v:\n    type: u32\n");
    write_text_file(root / "mid.brd",
                    "namespace: m\nrecord: Mid\nversion: 1\ntype: data\n"
                    "imports:\n  - leaf.brd\nfields:\n  leaf:\n    type: z.Leaf\n");
    write_text_file(root / "top.brd", "namespace: a\nrecord: Top\nversion: 1\ntype: data\n"
                                      "imports:\n  - mid.brd\nfields:\n  mid:\n    type: m.Mid\n");
    write_text_file(root / "configuration.brd",
                    "configuration: top_mid_leaf\nimports:\n  - top.brd\n");

    const auto check = [&](const std::filesystem::path& out) {
        const auto own = c_record_ids(out, kOwnId);
        const auto nested = c_record_ids(out, kNestedId);
        bool consistent = true;
        for (const auto& [symbol, expected_id] : nested) {
            const auto written = own.find(symbol);
            EXPECT_NE(written, own.end()) << symbol;
            if (written != own.end() && written->second != expected_id) {
                consistent = false;
            }
        }
        EXPECT_EQ(nested.size(), 2U); // a.Top -> m.Mid, m.Mid -> z.Leaf
        return consistent;
    };

    // Old workflow: one compiler run per schema, each its own root.
    for (const char* schema : {"top.brd", "mid.brd", "leaf.brd"}) {
        const CommandResult result = run_tool(
            {"--language", "c", "--output-directory", "per-root", (root / schema).string()}, root);
        ASSERT_EQ(result.status, 0) << result.stderr_text;
    }
    EXPECT_FALSE(check(root / "per-root")) << "expected the per-root ID disagreement";

    // A3: one configuration root, each namespace generated separately.
    for (const char* ns : {"a", "m", "z"}) {
        const CommandResult result =
            run_tool({"--language", "c", "--generate", ns, "--output-directory", "configured",
                      (root / "configuration.brd").string()},
                     root);
        ASSERT_EQ(result.status, 0) << result.stderr_text;
    }
    EXPECT_TRUE(check(root / "configured"));
}

// Issue #92: the driver prepares the layout once and backends reuse it. When
// layout computation itself fails, generation and output planning must still
// report each backend's established diagnostic, and write nothing.
TEST(SchemaCompilerToolTest, LayoutFailureKeepsBackendDiagnosticsInGenerationAndListOutputs) {
    const std::filesystem::path root = make_temp_directory("issue-92-layout-failure");
    const std::filesystem::path input = root / "schema.brd";
    write_text_file(input, "namespace: quarry.telemetry\n"
                           "record: Sample\n"
                           "version: 1\n"
                           "type: data\n"
                           "fields:\n"
                           "  status:\n"
                           "    type: Status\n"
                           "enums:\n"
                           "  Status:\n"
                           "    values:\n"
                           "      OK: 0\n"
                           "      NEGATIVE: -1\n");
    const std::vector<std::pair<std::vector<std::string>, std::string>> variants = {
        {{}, "backend error: backend codegen BRF v2 layout computation failed: "},
        {{"--language", "c"}, "backend error: backend_c: field 'quarry.telemetry.Sample.status'"},
        {{"--cpp-storage", "c"},
         "backend error: backend_c: field 'quarry.telemetry.Sample.status'"},
        {{"--language", "python"}, "backend error: backend_python: BRF v2 layout computation "},
        {{"--language", "go"}, "backend error: backend_go: BRF v2 layout computation failed: "},
    };
    for (const auto& [variant, expected_prefix] : variants) {
        const std::filesystem::path output = root / "generated";
        std::vector<std::string> arguments = variant;
        arguments.insert(arguments.end(), {"--output-directory", output.string(), input.string()});
        const CommandResult generated = run_tool(arguments, root);
        arguments.insert(arguments.begin(), "--list-outputs");
        const CommandResult listed = run_tool(arguments, root);

        EXPECT_EQ(generated.status, 1) << expected_prefix;
        EXPECT_EQ(listed.status, 1) << expected_prefix;
        EXPECT_EQ(generated.stderr_text.rfind(expected_prefix, 0), 0U) << generated.stderr_text;
        EXPECT_EQ(listed.stderr_text, generated.stderr_text);
        EXPECT_TRUE(generated.stdout_text.empty());
        EXPECT_TRUE(listed.stdout_text.empty());
        EXPECT_FALSE(std::filesystem::exists(output));
    }
}

TEST(SchemaCompilerToolTest, QbsListOutputsPrintsPathWithoutWritingTheImage) {
    const std::filesystem::path root = make_temp_directory("issue-92-qbs-list-outputs");
    const std::filesystem::path schema = truck_schemas() / "configuration.brd";
    for (const bool reflective : {false, true}) {
        const std::filesystem::path image = root / "images" / "truck.qbs";
        std::vector<std::string> arguments = {"--emit-qbs", image.string(), "--list-outputs"};
        if (reflective) {
            arguments.emplace_back("--qbs-reflective");
        }
        arguments.push_back(schema.string());
        const CommandResult result = run_tool(arguments, root);
        ASSERT_EQ(result.status, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, image.string() + "\n");
        EXPECT_TRUE(result.stderr_text.empty());
        EXPECT_FALSE(std::filesystem::exists(image));
    }
}

TEST(SchemaCompilerToolTest, ListInputsReportsCompleteCanonicalImportClosure) {
    // A canonical root keeps every lexical path equal to its canonical path
    // (macOS temporary directories live behind the /var symbolic link).
    const auto root =
        std::filesystem::canonical(make_temp_directory("list-inputs closure with spaces"));
    const auto schemas = root / "schemas";
    std::filesystem::copy(truck_schemas(), schemas, std::filesystem::copy_options::recursive);
    const auto schema = schemas / "configuration.brd";
    std::vector<std::string> expected;
    for (const auto& path : regular_files_under(schemas)) {
        if (path.extension() == ".brd") {
            expected.push_back(std::filesystem::canonical(path).string());
        }
    }
    std::sort(expected.begin(), expected.end());
    ASSERT_GT(expected.size(), 5U);
    const auto result = run_tool({"--list-inputs", schema.string()}, root);
    ASSERT_EQ(result.status, 0) << result.stderr_text;
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_EQ(non_empty_lines(result.stdout_text), expected);
    EXPECT_EQ(result.stdout_text.back(), '\n');
    const auto selected =
        run_tool({"--list-inputs", "--generate", "truck.engine", schema.string()}, root);
    ASSERT_EQ(selected.status, 0) << selected.stderr_text;
    EXPECT_EQ(selected.stdout_text, result.stdout_text);
    EXPECT_FALSE(std::filesystem::exists(root / "generated"));

    write_text_file(root / "renamed.brd", "configuration: renamed\nimports:\n  - " +
                                              schema.parent_path().generic_string() +
                                              "/vehicle.brd\n");
    const auto transitive = run_tool({"--list-inputs", (root / "renamed.brd").string()}, root);
    ASSERT_EQ(transitive.status, 0) << transitive.stderr_text;
    auto transitive_paths = non_empty_lines(transitive.stdout_text);
    EXPECT_EQ(std::count(transitive_paths.begin(), transitive_paths.end(),
                         std::filesystem::canonical(schemas / "engine_faults.brd").string()),
              1);
}

TEST(SchemaCompilerToolTest, ListInputsErrorsDoNotPrintPartialInventories) {
    const auto root = make_temp_directory("list-inputs errors");
    const auto schema = root / "root.brd";
    write_text_file(schema, "configuration: broken\nimports:\n  - missing.brd\n");
    const auto result = run_tool({"--list-inputs", schema.string()}, root);
    EXPECT_NE(result.status, 0);
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_FALSE(result.stderr_text.empty());
#ifndef _WIN32
    const auto unsafe = root / "line\nbreak.brd";
    write_text_file(unsafe, "namespace: inputs\nrecord: Record\nversion: 1\ntype: data\n"
                            "fields:\n  value:\n    type: u32\n");
    const auto invalid_path = run_tool({"--list-inputs", unsafe.string()}, root);
    EXPECT_EQ(invalid_path.status, 1);
    EXPECT_TRUE(invalid_path.stdout_text.empty());
    EXPECT_NE(invalid_path.stderr_text.find("input path contains"), std::string::npos);
#endif
    for (const auto& options : std::vector<std::vector<std::string>>{{"--list-inputs"},
                                                                     {"--list-outputs"},
                                                                     {"--print-schema-id"},
                                                                     {"--output-directory", "out"},
                                                                     {"--emit-qbs", "out.qbs"}}) {
        const auto rejected =
            run_tool(with({"--list-inputs"}, with(options, {schema.string()})), root);
        EXPECT_EQ(rejected.status, 2);
        EXPECT_TRUE(rejected.stdout_text.empty());
    }
}

#ifndef _WIN32
TEST(SchemaCompilerToolTest, ListInputsReportsSymbolicLinkPathsAndTheirTargets) {
    const auto root = std::filesystem::canonical(make_temp_directory("list-inputs symlinks"));
    write_text_file(root / "versions" / "leaf.brd",
                    "namespace: z\nrecord: Leaf\nversion: 1\ntype: data\n"
                    "imports:\n  - sibling.brd\nfields:\n  value:\n    type: u32\n");
    write_text_file(root / "versions" / "sibling.brd",
                    "namespace: s\nrecord: Sibling\nversion: 1\ntype: data\n"
                    "fields:\n  value:\n    type: u32\n");
    write_text_file(root / "real" / "root.brd",
                    "configuration: linked\nimports:\n  - ../current.brd\n  - shared/leaf.brd\n"
                    "  - shared/../versions/sibling.brd\n");
    std::filesystem::create_symlink("versions/leaf.brd", root / "current.brd");
    std::filesystem::create_directory_symlink("../versions", root / "real" / "shared");
    std::filesystem::create_directory_symlink("real", root / "alias");

    // Root named through a directory link; one import through a file link and
    // one through a directory link. Imports inside a linked target resolve
    // against the target's directory, so sibling.brd is reached canonically.
    // `shared/..` leaves the link's target, so its lexical form,
    // real/versions/sibling.brd, names no schema and must not be listed.
    const auto result = run_tool({"--list-inputs", (root / "alias" / "root.brd").string()}, root);
    ASSERT_EQ(result.status, 0) << result.stderr_text;
    const std::vector<std::string> expected{
        (root / "alias" / "root.brd").string(),    (root / "current.brd").string(),
        (root / "real" / "root.brd").string(),     (root / "real" / "shared" / "leaf.brd").string(),
        (root / "versions" / "leaf.brd").string(), (root / "versions" / "sibling.brd").string(),
    };
    EXPECT_EQ(non_empty_lines(result.stdout_text), expected);
}
#endif

// Every header the C++ backends generate for a configuration, included by two
// translation units of one program (#82). A function or variable defined in a
// generated header without `inline` gives each object file its own strong
// definition and fails to link. -O0 keeps the compiler from dropping unused
// definitions, so no call is needed to expose one.
TEST(SchemaCompilerToolTest, EveryGeneratedCppHeaderLinksIntoMultipleTranslationUnits) {
    const auto root = make_temp_directory("cpp-odr-every-header");
    write_text_file(root / "benchmarks.brd", "configuration: benchmarks\nimports:\n  - " +
                                                 (std::filesystem::path(QUARRY_TEST_SOURCE_DIR) /
                                                  "benchmarks/schemas" / "workload.brd")
                                                     .generic_string() +
                                                 "\n");
    // The truck covers configuration-wide imports across namespaces; the
    // benchmark workload covers every field kind (scalars, enums, strings,
    // bytes, scalar arrays, nested records and record arrays).
    const std::vector<std::filesystem::path> configurations{truck_schemas() / "configuration.brd",
                                                            root / "benchmarks.brd"};
    const std::vector<std::pair<std::string, std::vector<std::string>>> variants{
        {"owning", {}}, {"c-storage", {"--cpp-storage", "c"}}};
    for (const auto& configuration : configurations) {
        for (const auto& [variant, options] : variants) {
            SCOPED_TRACE(configuration.filename().string() + " / " + variant);
            const auto work = root / (configuration.stem().string() + "-" + variant);
            const auto output = work / "generated";
            const auto generated = run_tool(with({"--output-directory", output.string()},
                                                 with(options, {configuration.string()})),
                                            root);
            ASSERT_EQ(generated.status, 0) << generated.stderr_text;
            std::string includes;
            for (const auto& header : regular_files_under(output)) {
                includes += "#include \"" +
                            std::filesystem::relative(header, output).generic_string() + "\"\n";
            }
            ASSERT_FALSE(includes.empty());
            write_text_file(work / "first.cpp", includes + "int first() { return 0; }\n");
            write_text_file(work / "second.cpp",
                            includes + "int first();\nint main() { return first(); }\n");
            const auto executable = work / "program";
            std::ostringstream compile;
            compile << std::quoted(std::string(QUARRY_TEST_CXX_COMPILER))
                    << " -std=c++20 -O0" QUARRY_TEST_STRICT_CXX_WARNINGS " -I"
                    << std::quoted(output.string()) << " -I"
                    << std::quoted(std::string(QUARRY_TEST_REPO_INCLUDE_DIR)) << " -I"
                    << std::quoted(std::string(QUARRY_TEST_GENERATED_INCLUDE_DIR)) << ' '
                    << std::quoted((work / "first.cpp").string()) << ' '
                    << std::quoted((work / "second.cpp").string()) << " -o "
                    << std::quoted(executable.string());
            ASSERT_EQ(std::system(compile.str().c_str()), 0) << compile.str();
            std::ostringstream run;
            run << std::quoted(executable.string());
            EXPECT_EQ(std::system(run.str().c_str()), 0);
        }
    }
}
