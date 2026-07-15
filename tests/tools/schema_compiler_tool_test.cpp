#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string("breadcrumbs-schema-compiler-") + std::string(stem) + "-" +
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

struct CommandResult {
    int status = 0;
    std::string stdout_text;
    std::string stderr_text;
};

[[nodiscard]] CommandResult run_tool(const std::vector<std::string>& arguments,
                                     const std::filesystem::path& working_directory) {
    const std::filesystem::path stdout_path = working_directory / "stdout.txt";
    const std::filesystem::path stderr_path = working_directory / "stderr.txt";

    std::ostringstream command;
    command << shell_quote(BREADCRUMBS_SCHEMA_COMPILER_TOOL);
    for (const auto& argument : arguments) {
        command << ' ' << shell_quote(argument);
    }
    command << " > " << shell_quote(stdout_path.string()) << " 2> "
            << shell_quote(stderr_path.string());

    const int status = std::system(command.str().c_str());
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

} // namespace

TEST(SchemaCompilerToolTest, HelpReturnsSuccess) {
    const std::filesystem::path root = make_temp_directory("help");

    const CommandResult result = run_tool({"--help"}, root);

    EXPECT_EQ(result.status, 0);
    EXPECT_NE(result.stdout_text.find("breadcrumbs-schema-compiler [options] INPUT"),
              std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(SchemaCompilerToolTest, UnknownOptionReturnsUsageError) {
    const std::filesystem::path root = make_temp_directory("unknown-option");

    const CommandResult result = run_tool({"--unknown-option"}, root);

    EXPECT_NE(result.status, 0);
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
    write_text_file(input, "namespace: breadcrumbs.telemetry\nrecord: Sample\nfields: [\n");

    const CommandResult result =
        run_tool({"--output-directory", output.string(), input.string()}, root);

    EXPECT_NE(result.status, 0);
    EXPECT_NE(result.stderr_text.find("BC2101"), std::string::npos);
    EXPECT_TRUE(regular_files_under(output).empty());
}

TEST(SchemaCompilerToolTest, ValidYamlWritesGeneratedFiles) {
    const std::filesystem::path root = make_temp_directory("valid-yaml");
    const std::filesystem::path input = root / "schema.brd";
    const std::filesystem::path output = root / "generated";
    write_text_file(input,
                    "namespace: breadcrumbs.telemetry\n"
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
