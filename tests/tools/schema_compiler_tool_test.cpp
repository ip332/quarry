#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#ifndef QUARRY_TEST_GENERATED_CODE_API_VERSION
#error "QUARRY_TEST_GENERATED_CODE_API_VERSION must be defined"
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
    EXPECT_EQ(result.stdout_text, "quarry-schema-compiler 0.1.0\n");
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
    EXPECT_EQ(result.stdout_text, "quarry-schema-compiler 0.1.0\n");
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
    EXPECT_EQ(result.stdout_text, "quarry-schema-compiler 0.1.0\n");
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
    ASSERT_EQ(files.size(), 1);
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
    EXPECT_NE(result.stderr_text.find("'cpp' or 'c'"), std::string::npos);
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
    EXPECT_NE(source_text.find("quarry_telemetry_Sample_init"), std::string::npos);
}

TEST(SchemaCompilerToolTest, CBackendRejectsUnsupportedFieldType) {
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
                    "    type: string\n"
                    "    max_bytes: 32\n");

    const CommandResult result =
        run_tool({"--language", "c", "--output-directory", output.string(), input.string()},
                 root);

    EXPECT_NE(result.status, 0);
    EXPECT_NE(result.stderr_text.find("quarry.telemetry.Sample.label"), std::string::npos);
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
