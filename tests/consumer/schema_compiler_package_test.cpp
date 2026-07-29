#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef QUARRY_TEST_GENERATED_CODE_API_VERSION
#error "QUARRY_TEST_GENERATED_CODE_API_VERSION must be defined"
#endif

#ifndef QUARRY_TEST_C_COMPILER
#error "QUARRY_TEST_C_COMPILER must be defined"
#endif

namespace {

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string("quarry-schema-compiler-package-") + std::string(stem) + "-" +
         std::to_string(suffix));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

[[nodiscard]] bool contains_case_insensitive(std::string_view haystack, std::string_view needle) {
    const auto to_lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                        [&](char a, char b) { return to_lower(a) == to_lower(b); }) !=
           haystack.end();
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path};
    if (!output) {
        throw std::runtime_error("failed to open test file for writing: " + path.string());
    }
    output << text;
}

void write_executable_file(const std::filesystem::path& path, std::string_view text) {
    write_text_file(path, text);
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);
}

[[nodiscard]] std::string shell_quote(std::string_view text) {
    std::string quoted = "'";
    for (const char character : text) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += "'";
    return quoted;
}

[[nodiscard]] std::string join_cmake_list(const std::vector<std::filesystem::path>& paths) {
    std::string joined;
    for (const std::filesystem::path& path : paths) {
        if (!joined.empty()) {
            joined += ';';
        }
        joined += path.string();
    }
    return joined;
}

struct CommandResult {
    int status = 0;
    std::string stdout_text;
    std::string stderr_text;
};

[[nodiscard]] CommandResult run_executable(const std::filesystem::path& executable,
                                           const std::vector<std::string>& arguments,
                                           const std::filesystem::path& working_directory,
                                           std::string_view label) {
    const std::filesystem::path stdout_path = working_directory / (std::string(label) + ".stdout");
    const std::filesystem::path stderr_path = working_directory / (std::string(label) + ".stderr");

    int status = 125;
#ifdef _WIN32
    throw std::runtime_error("direct subprocess package tests are not implemented on Windows");
#else
    const pid_t child = fork();
    if (child < 0) {
        throw std::runtime_error("failed to fork package test subprocess");
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
        argument_storage.push_back(executable.string());
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
        throw std::runtime_error("failed to wait for package test subprocess");
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

void expect_success(const CommandResult& result, std::string_view step) {
    EXPECT_EQ(result.status, 0) << step << " stdout:\n"
                                << result.stdout_text << "\nstderr:\n"
                                << result.stderr_text;
}

[[nodiscard]] std::filesystem::path installed_schema_compiler(
    const std::filesystem::path& install_prefix) {
    return install_prefix / "bin" / "quarry-schema-compiler";
}

[[nodiscard]] std::filesystem::path write_schema_compiler_wrapper(
    const std::filesystem::path& path, const std::filesystem::path& real_compiler,
    const std::filesystem::path& log_path) {
    write_executable_file(path,
                          "#!/bin/sh\n"
                          "printf '%s\\n' \"$*\" >> " +
                              shell_quote(log_path.string()) +
                              "\n"
                              "exec " +
                              shell_quote(real_compiler.string()) + " \"$@\"\n");
    return path;
}

[[nodiscard]] std::filesystem::path write_schema_compiler_query_wrapper(
    const std::filesystem::path& path, const std::filesystem::path& real_compiler,
    const std::filesystem::path& log_path, std::string_view generated_code_api_version) {
    write_executable_file(path,
                          "#!/bin/sh\n"
                          "printf '%s\\n' \"$*\" >> " +
                              shell_quote(log_path.string()) +
                              "\n"
                              "if [ \"$1\" = \"--print-generated-code-api-version\" ]; then\n"
                              "  printf '%s\\n' " +
                              shell_quote(generated_code_api_version) +
                              "\n"
                              "  exit 0\n"
                              "fi\n"
                              "exec " +
                              shell_quote(real_compiler.string()) + " \"$@\"\n");
    return path;
}

[[nodiscard]] std::filesystem::path write_schema_compiler_failing_query_wrapper(
    const std::filesystem::path& path, std::string_view stderr_text) {
    write_executable_file(path,
                          "#!/bin/sh\n"
                          "if [ \"$1\" = \"--print-generated-code-api-version\" ]; then\n"
                          "  printf '%s\\n' " +
                              shell_quote(stderr_text) +
                              " >&2\n"
                              "  exit 9\n"
                              "fi\n"
                              "exit 0\n");
    return path;
}

} // namespace

TEST(SchemaCompilerPackageTest, ImportedExecutableTargetGeneratesDownstreamCode) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("consumer with spaces");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    const std::filesystem::path consumer_build = root / "consumer build with spaces";

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"-S", QUARRY_SCHEMA_COMPILER_PACKAGE_CONSUMER_SOURCE_DIR,
                                   "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string(),
                                   "-DCMAKE_CXX_COMPILER=" +
                                       std::string(QUARRY_TEST_CXX_COMPILER),
                                   "-DEXPECTED_QUARRY_PREFIX=" + install_prefix.string()},
                                  root, "configure-consumer"),
                   "configure external consumer");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()}, root, "build-consumer"),
                   "build external consumer");

    const std::filesystem::path generated_header =
        consumer_build / "generated" / "quarry" / "telemetry.generated.hpp";
    ASSERT_TRUE(std::filesystem::exists(generated_header));
    const std::string generated = read_text_file(generated_header);
    EXPECT_NE(generated.find("struct Sample"), std::string::npos);
    EXPECT_NE(generated.find("std::uint32_t"), std::string::npos);
    EXPECT_EQ(generated.find(QUARRY_TEST_BUILD_DIR), std::string::npos);

    const std::filesystem::path executable =
        consumer_build / "quarry_schema_compiler_cmake";
    const CommandResult consumer_result = run_executable(executable, {}, root, "run-consumer");
    expect_success(consumer_result, "run external consumer");

    // Regression guard (PR-105): the example must actually exercise the
    // structured decode-failure API (`decode_Sample_result`'s `.error`,
    // `.path`, `.byte_offset`), not just the optional-collapsing convenience
    // wrapper, and must demonstrate both an empty and a populated `path`.
    EXPECT_NE(consumer_result.stdout_text.find("truncated_header"), std::string::npos);
    EXPECT_NE(consumer_result.stdout_text.find("invalid_field_length"), std::string::npos);
    EXPECT_NE(consumer_result.stdout_text.find("path: (empty"), std::string::npos);
    EXPECT_NE(consumer_result.stdout_text.find("field_index=0"), std::string::npos);
    EXPECT_NE(consumer_result.stdout_text.find("byte_offset: 19"), std::string::npos);

    const std::filesystem::path targets_file =
        install_prefix / "lib" / "cmake" / "Quarry" / "QuarryTargets.cmake";
    const std::string targets = read_text_file(targets_file);
    EXPECT_NE(targets.find("add_executable(Quarry::schema_compiler IMPORTED)"),
              std::string::npos);
    EXPECT_EQ(targets.find("quarry_compiler_backend"), std::string::npos);
    EXPECT_EQ(targets.find("protobuf::"), std::string::npos);
    EXPECT_EQ(targets.find("absl::"), std::string::npos);
    EXPECT_EQ(targets.find("quarry_yaml"), std::string::npos);
}

TEST(SchemaCompilerPackageTest, ManualCustomCommandStillGeneratesDownstreamCode) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("manual consumer");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    const std::filesystem::path consumer_source = root / "manual source with spaces";
    const std::filesystem::path consumer_build = root / "manual build with spaces";

    write_text_file(consumer_source / "schema.brd",
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    write_text_file(consumer_source / "main.cpp",
                    "#include <quarry/telemetry.generated.hpp>\n"
                    "int main() {\n"
                    "  quarry::telemetry::SampleBuilder builder;\n"
                    "  if (!builder.set_count(7)) { return 1; }\n"
                    "  const auto sample = builder.build();\n"
                    "  return sample.has_count() ? 0 : 1;\n"
                    "}\n");
    write_text_file(consumer_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(manual_schema_compiler_consumer LANGUAGES CXX)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "set(generated_dir \"${CMAKE_CURRENT_BINARY_DIR}/generated\")\n"
                    "set(generated_header \"${generated_dir}/quarry/telemetry.generated.hpp\")\n"
                    "add_custom_command(\n"
                    "  OUTPUT \"${generated_header}\"\n"
                    "  COMMAND \"$<TARGET_FILE:Quarry::schema_compiler>\"\n"
                    "          --output-directory \"${generated_dir}\"\n"
                    "          \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\"\n"
                    "  DEPENDS \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\"\n"
                    "          Quarry::schema_compiler\n"
                    "  VERBATIM)\n"
                    "add_executable(manual_consumer main.cpp \"${generated_header}\")\n"
                    "target_include_directories(manual_consumer PRIVATE \"${generated_dir}\")\n"
                    "target_link_libraries(manual_consumer PRIVATE Quarry::runtime)\n");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"-S", consumer_source.string(), "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string(),
                                   "-DCMAKE_CXX_COMPILER=" +
                                       std::string(QUARRY_TEST_CXX_COMPILER)},
                                  root, "configure-manual"),
                   "configure manual consumer");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()}, root, "build-manual"),
                   "build manual consumer");
    expect_success(run_executable(consumer_build / "manual_consumer", {}, root, "run-manual"),
                   "run manual consumer");
}

TEST(SchemaCompilerPackageTest, HelperReconfiguresWhenSchemaInventoryChanges) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("helper reconfigure");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    const std::filesystem::path consumer_source = root / "helper source with spaces";
    const std::filesystem::path consumer_build = root / "helper build with spaces";
    const std::filesystem::path schema = consumer_source / "schema.brd";

    write_text_file(schema,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    write_text_file(consumer_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(helper_reconfigure LANGUAGES NONE)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "quarry_generate_cpp(\n"
                    "  SCHEMA schema.brd\n"
                    "  OUTPUT_DIR generated\n"
                    "  OUT_FILES generated_files\n"
                    "  FILE_EXTENSION .hpp)\n"
                    "file(WRITE \"${CMAKE_CURRENT_BINARY_DIR}/outputs.txt\" "
                    "\"${generated_files}\\n\")\n"
                    "add_custom_target(generated ALL DEPENDS ${generated_files})\n");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"-S", consumer_source.string(), "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
                                  root, "configure-helper-reconfigure"),
                   "configure helper reconfigure consumer");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()},
                                  root, "build-helper-reconfigure"),
                   "build helper reconfigure consumer");
    EXPECT_TRUE(std::filesystem::exists(consumer_build / "generated" / "quarry" /
                                        "telemetry.hpp"));

    write_text_file(schema,
                    "namespace: quarry.telemetry.v2\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()},
                                  root, "rebuild-helper-reconfigure"),
                   "rebuild helper reconfigure consumer");
    EXPECT_TRUE(std::filesystem::exists(consumer_build / "generated" / "quarry" /
                                        "telemetry" / "v2.hpp"));
    const std::string outputs = read_text_file(consumer_build / "outputs.txt");
    EXPECT_NE(outputs.find("generated/quarry/telemetry/v2.hpp"), std::string::npos);
}

TEST(SchemaCompilerPackageTest, HelperNativeExplicitOverrideUsesSelectedCompiler) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("native override");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    const std::filesystem::path consumer_source = root / "override source with spaces";
    const std::filesystem::path consumer_build = root / "override build with spaces";
    const std::filesystem::path log_path = root / "wrapper invocations.log";
    const std::filesystem::path wrapper =
        write_schema_compiler_wrapper(root / "host compiler wrapper.sh",
                                      installed_schema_compiler(install_prefix), log_path);

    write_text_file(consumer_source / "schema.brd",
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    write_text_file(consumer_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(native_override LANGUAGES NONE)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "quarry_generate_cpp(\n"
                    "  SCHEMA schema.brd\n"
                    "  OUTPUT_DIR generated\n"
                    "  OUT_FILES generated_files\n"
                    "  SCHEMA_COMPILER \"" +
                        wrapper.string() +
                        "\")\n"
                        "file(WRITE \"${CMAKE_CURRENT_BINARY_DIR}/outputs.txt\" "
                        "\"${generated_files}\\n\")\n"
                        "add_custom_target(generated ALL DEPENDS ${generated_files})\n");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"-S", consumer_source.string(), "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
                                  root, "configure-native-override"),
                   "configure native override consumer");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()},
                                  root, "build-native-override"),
                   "build native override consumer");

    EXPECT_TRUE(std::filesystem::exists(consumer_build / "generated" / "quarry" /
                                        "telemetry.generated.hpp"));
    const std::string log = read_text_file(log_path);
    EXPECT_NE(log.find("--print-generated-code-api-version"), std::string::npos);
    EXPECT_NE(log.find("--list-outputs"), std::string::npos);
    EXPECT_NE(log.find("--output-directory"), std::string::npos);
    EXPECT_EQ(log.find("--version"), std::string::npos);
}

TEST(SchemaCompilerPackageTest, HelperCrossCompilingRequiresExplicitCompiler) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("cross no override");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    const std::filesystem::path consumer_source = root / "cross source";
    write_text_file(consumer_source / "schema.brd",
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");
    write_text_file(consumer_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(cross_no_override LANGUAGES NONE)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                    "OUT_FILES files)\n");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");
    const CommandResult result = run_executable(
        QUARRY_TEST_CMAKE_COMMAND,
        {"-S", consumer_source.string(), "-B", (root / "cross build").string(),
         "-DCMAKE_PREFIX_PATH=" + install_prefix.string(), "-DCMAKE_SYSTEM_NAME=Generic"},
        root, "configure-cross-no-override");
    EXPECT_NE(result.status, 0);
    EXPECT_NE(result.stderr_text.find("cross-compiling requires SCHEMA_COMPILER"),
              std::string::npos);
}

TEST(SchemaCompilerPackageTest, HelperCrossCompilingAcceptsExplicitCompiler) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("cross override");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    const std::filesystem::path consumer_source = root / "cross override source";
    const std::filesystem::path consumer_build = root / "cross override build";
    const std::filesystem::path log_path = root / "cross wrapper invocations.log";
    const std::filesystem::path wrapper =
        write_schema_compiler_wrapper(root / "cross host compiler.sh",
                                      installed_schema_compiler(install_prefix), log_path);

    write_text_file(consumer_source / "schema.brd",
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");
    write_text_file(consumer_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(cross_override LANGUAGES NONE)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "if(NOT DEFINED Quarry_GENERATED_CODE_API_VERSION)\n"
                    "  message(FATAL_ERROR \"Quarry_GENERATED_CODE_API_VERSION is not defined\")\n"
                    "endif()\n"
                    "if(NOT Quarry_GENERATED_CODE_API_VERSION MATCHES \"^[0-9]+$\")\n"
                    "  message(FATAL_ERROR \"Quarry_GENERATED_CODE_API_VERSION is not numeric\")\n"
                    "endif()\n"
                    "quarry_generate_cpp(\n"
                    "  SCHEMA schema.brd\n"
                    "  OUTPUT_DIR generated\n"
                    "  OUT_FILES generated_files\n"
                    "  SCHEMA_COMPILER \"" +
                        wrapper.string() +
                        "\")\n"
                        "add_custom_target(generated ALL DEPENDS ${generated_files})\n");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"-S", consumer_source.string(), "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string(),
                                   "-DCMAKE_SYSTEM_NAME=Generic"},
                                  root, "configure-cross-override"),
                   "configure cross override consumer");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()},
                                  root, "build-cross-override"),
                   "build cross override consumer");
    EXPECT_TRUE(std::filesystem::exists(consumer_build / "generated" / "quarry" /
                                        "telemetry.generated.hpp"));
    const std::string log = read_text_file(log_path);
    EXPECT_NE(log.find("--print-generated-code-api-version"), std::string::npos);
    EXPECT_NE(log.find("--list-outputs"), std::string::npos);
    EXPECT_EQ(log.find("--version"), std::string::npos);
}

TEST(SchemaCompilerPackageTest,
     HelperRejectsCompilerRuntimeVersionMismatchBeforeListingOutputs) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("query mismatch");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    const std::filesystem::path consumer_source = root / "query mismatch source";
    const std::filesystem::path consumer_build = root / "query mismatch build";
    const std::filesystem::path log_path = root / "query mismatch invocations.log";
    // Deliberately different from the real package epoch (QUARRY_TEST_GENERATED_CODE_API_VERSION)
    // so this test keeps exercising a genuine mismatch after future epoch bumps, without needing
    // a matching manual update here.
    const std::string mismatched_compiler_api_version =
        std::to_string(QUARRY_TEST_GENERATED_CODE_API_VERSION + 1);
    const std::filesystem::path wrapper = write_schema_compiler_query_wrapper(
        root / "mismatch compiler.sh", installed_schema_compiler(install_prefix), log_path,
        mismatched_compiler_api_version);

    write_text_file(consumer_source / "schema.brd",
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    write_text_file(consumer_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(query_mismatch LANGUAGES NONE)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "quarry_generate_cpp(\n"
                    "  SCHEMA schema.brd\n"
                    "  OUTPUT_DIR generated\n"
                    "  OUT_FILES generated_files\n"
                    "  SCHEMA_COMPILER \"" +
                        wrapper.string() +
                        "\")\n");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");
    const CommandResult result = run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                                {"-S", consumer_source.string(), "-B",
                                                 consumer_build.string(),
                                                 "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
                                                root, "configure-query-mismatch");
    EXPECT_NE(result.status, 0);
    EXPECT_NE(result.stderr_text.find("incompatible with the target runtime package"),
              std::string::npos);
    EXPECT_NE(result.stderr_text.find("Compiler generated-code API version"),
              std::string::npos);
    EXPECT_NE(result.stderr_text.find("Target runtime generated-code API version"),
              std::string::npos);
    EXPECT_NE(result.stderr_text.find(
                  "  " + std::to_string(QUARRY_TEST_GENERATED_CODE_API_VERSION)),
              std::string::npos);
    EXPECT_NE(result.stderr_text.find("  " + mismatched_compiler_api_version),
              std::string::npos);
    const std::string log = read_text_file(log_path);
    EXPECT_NE(log.find("--print-generated-code-api-version"), std::string::npos);
    EXPECT_EQ(log.find("--list-outputs"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(consumer_build / "generated"));
}

TEST(SchemaCompilerPackageTest, HelperAllowsOnlyOneCompilerPerBuildTree) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("one compiler");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");

    auto write_two_schema_project = [&](const std::filesystem::path& source,
                                        std::string_view first_compiler,
                                        std::string_view second_compiler) {
        write_text_file(source / "first.brd",
                        "namespace: quarry.one\n"
                        "record: First\n"
                        "version: 1\n"
                        "type: data\n"
                        "fields: {}\n");
        write_text_file(source / "second.brd",
                        "namespace: quarry.two\n"
                        "record: Second\n"
                        "version: 1\n"
                        "type: data\n"
                        "fields: {}\n");
        write_text_file(source / "CMakeLists.txt",
                        "cmake_minimum_required(VERSION 3.20)\n"
                        "project(one_compiler LANGUAGES NONE)\n"
                        "find_package(Quarry CONFIG REQUIRED)\n"
                        "quarry_generate_cpp(\n"
                        "  SCHEMA first.brd\n"
                        "  OUTPUT_DIR one\n"
                        "  OUT_FILES first_files\n" +
                            std::string(first_compiler) +
                            ")\n"
                            "quarry_generate_cpp(\n"
                            "  SCHEMA second.brd\n"
                            "  OUTPUT_DIR two\n"
                            "  OUT_FILES second_files\n" +
                            std::string(second_compiler) +
                            ")\n"
                            "add_custom_target(generated ALL DEPENDS ${first_files} "
                            "${second_files})\n");
    };

    const std::filesystem::path real_compiler = installed_schema_compiler(install_prefix);
    const std::filesystem::path wrapper_a =
        write_schema_compiler_wrapper(root / "compiler a.sh", real_compiler, root / "a.log");
    const std::filesystem::path wrapper_b =
        write_schema_compiler_wrapper(root / "compiler b.sh", real_compiler, root / "b.log");

    const std::filesystem::path same_source = root / "same compiler source";
    write_two_schema_project(same_source,
                             "  SCHEMA_COMPILER \"" + wrapper_a.string() + "\"\n",
                             "  SCHEMA_COMPILER \"" + wrapper_a.string() + "\"\n");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"-S", same_source.string(), "-B",
                                   (root / "same compiler build").string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
                                  root, "configure-same-compiler"),
                   "configure same compiler project");

    const std::filesystem::path native_same_source = root / "native same source";
    write_two_schema_project(native_same_source, "",
                             "  SCHEMA_COMPILER \"" + real_compiler.string() + "\"\n");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"-S", native_same_source.string(), "-B",
                                   (root / "native same build").string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
                                  root, "configure-native-same-compiler"),
                   "configure native and explicit same compiler project");

    const std::filesystem::path different_source = root / "different compiler source";
    write_two_schema_project(different_source,
                             "  SCHEMA_COMPILER \"" + wrapper_a.string() + "\"\n",
                             "  SCHEMA_COMPILER \"" + wrapper_b.string() + "\"\n");
    const CommandResult different_result = run_executable(
        QUARRY_TEST_CMAKE_COMMAND,
        {"-S", different_source.string(), "-B", (root / "different compiler build").string(),
         "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
        root, "configure-different-compilers");
    EXPECT_NE(different_result.status, 0);
    EXPECT_NE(different_result.stderr_text.find("only one schema compiler is allowed"),
              std::string::npos);
    EXPECT_NE(different_result.stderr_text.find(wrapper_a.string()), std::string::npos);
    EXPECT_NE(different_result.stderr_text.find(wrapper_b.string()), std::string::npos);

    const std::filesystem::path native_different_source = root / "native different source";
    write_two_schema_project(native_different_source, "",
                             "  SCHEMA_COMPILER \"" + wrapper_a.string() + "\"\n");
    const CommandResult native_different_result = run_executable(
        QUARRY_TEST_CMAKE_COMMAND,
        {"-S", native_different_source.string(), "-B",
         (root / "native different build").string(),
         "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
        root, "configure-native-different-compiler");
    EXPECT_NE(native_different_result.status, 0);
    EXPECT_NE(native_different_result.stderr_text.find("only one schema compiler is allowed"),
              std::string::npos);
}

TEST(SchemaCompilerPackageTest, BuildTimeInventoryVerificationReportsDrift) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("verify outputs");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");

    const std::filesystem::path module =
        install_prefix / "lib" / "cmake" / "Quarry" / "QuarryGenerate.cmake";
    const std::filesystem::path schema = root / "schema.brd";
    const std::filesystem::path output_dir = root / "output dir with spaces";
    std::filesystem::create_directories(output_dir);
    write_text_file(schema,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");

    auto write_fake_compiler = [&](std::string_view name,
                                   const std::vector<std::filesystem::path>& outputs,
                                   std::string_view trailer = {}) {
        const std::filesystem::path script = root / (std::string(name) + ".sh");
        std::string body = "#!/bin/sh\n";
        if (!outputs.empty()) {
            body += "printf '%s\\n'";
            for (const std::filesystem::path& output : outputs) {
                body += " " + shell_quote(output.string());
            }
            body += "\n";
        }
        body += trailer;
        write_executable_file(script, body);
        return script;
    };

    auto run_verify = [&](std::string_view name, const std::filesystem::path& compiler,
                          const std::vector<std::filesystem::path>& expected) {
        return run_executable(
            QUARRY_TEST_CMAKE_COMMAND,
            {"-DQUARRY_VERIFY_OUTPUT_INVENTORY=TRUE",
             "-DQUARRY_VERIFY_COMPILER=" + compiler.string(),
             "-DQUARRY_VERIFY_SCHEMA=" + schema.string(),
             "-DQUARRY_VERIFY_OUTPUT_DIR=" + output_dir.string(),
             "-DQUARRY_VERIFY_EXPECTED_OUTPUTS=" + join_cmake_list(expected), "-P",
             module.string()},
            root, name);
    };

    const std::filesystem::path first = output_dir / "first file.generated.hpp";
    const std::filesystem::path second = output_dir / "second.generated.hpp";
    const std::filesystem::path changed = output_dir / "changed.generated.hpp";

    expect_success(run_verify("verify-match", write_fake_compiler("matching", {first, second}),
                              {first, second}),
                   "matching verification");

    const CommandResult missing =
        run_verify("verify-missing", write_fake_compiler("missing", {first}), {first, second});
    EXPECT_NE(missing.status, 0);
    EXPECT_NE(missing.stderr_text.find("generated-output inventory changed"),
              std::string::npos);

    const CommandResult extra = run_verify(
        "verify-extra", write_fake_compiler("extra", {first, second}), {first});
    EXPECT_NE(extra.status, 0);
    EXPECT_NE((extra.stdout_text + extra.stderr_text).find("Current outputs"),
              std::string::npos);

    const CommandResult changed_result = run_verify(
        "verify-changed", write_fake_compiler("changed", {changed}), {first});
    EXPECT_NE(changed_result.status, 0);
    EXPECT_NE((changed_result.stdout_text + changed_result.stderr_text)
                  .find("Reconfigure the CMake build"),
              std::string::npos);

    const CommandResult reordered = run_verify(
        "verify-reordered", write_fake_compiler("reordered", {second, first}), {first, second});
    EXPECT_NE(reordered.status, 0);

    const CommandResult duplicate_current = run_verify(
        "verify-duplicate", write_fake_compiler("duplicate", {first, first}), {first, second});
    EXPECT_NE(duplicate_current.status, 0);
    EXPECT_NE(duplicate_current.stderr_text.find("duplicate output"), std::string::npos);

    const CommandResult malformed = run_verify(
        "verify-malformed", write_fake_compiler("malformed", {first}, "printf '\\n'\n"),
        {first});
    EXPECT_NE(malformed.status, 0);
    EXPECT_NE((malformed.stdout_text + malformed.stderr_text).find("empty output line"),
              std::string::npos);

    const std::filesystem::path failing = root / "failing.sh";
    write_executable_file(failing, "#!/bin/sh\necho schema failure >&2\nexit 1\n");
    const CommandResult query_failure = run_verify("verify-query-failure", failing, {first});
    EXPECT_NE(query_failure.status, 0);
    EXPECT_NE((query_failure.stdout_text + query_failure.stderr_text).find("schema failure"),
              std::string::npos);

    const CommandResult unsafe = run_verify(
        "verify-unsafe",
        write_fake_compiler("unsafe", {output_dir / "bad;name.generated.hpp"}), {first});
    EXPECT_NE(unsafe.status, 0);
    EXPECT_NE(unsafe.stderr_text.find("semicolon"), std::string::npos);

    const CommandResult outside = run_verify(
        "verify-outside",
        write_fake_compiler("outside", {root / "outside.generated.hpp"}), {first});
    EXPECT_NE(outside.status, 0);
    EXPECT_NE(outside.stderr_text.find("outside OUTPUT_DIR"), std::string::npos);

    write_text_file(first, "existing generated content\n");
    const CommandResult preservation = run_verify(
        "verify-preserves-existing", write_fake_compiler("preservation", {changed}), {first});
    EXPECT_NE(preservation.status, 0);
    EXPECT_EQ(read_text_file(first), "existing generated content\n");
}

TEST(SchemaCompilerPackageTest, HelperFailsBeforeGenerationWhenInventoryDrifts) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("helper drift");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    const std::filesystem::path consumer_source = root / "drift source with spaces";
    const std::filesystem::path consumer_build = root / "drift build with spaces";
    const std::filesystem::path schema = consumer_source / "schema.brd";

    write_text_file(schema,
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    write_text_file(consumer_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "set(CMAKE_SUPPRESS_REGENERATION TRUE)\n"
                    "project(helper_drift LANGUAGES NONE)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "quarry_generate_cpp(\n"
                    "  SCHEMA schema.brd\n"
                    "  OUTPUT_DIR generated\n"
                    "  OUT_FILES generated_files\n"
                    "  FILE_EXTENSION .hpp)\n"
                    "file(WRITE \"${CMAKE_CURRENT_BINARY_DIR}/outputs.txt\" "
                    "\"${generated_files}\\n\")\n"
                    "add_custom_target(generated ALL DEPENDS ${generated_files})\n");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"-S", consumer_source.string(), "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
                                  root, "configure-helper-drift"),
                   "configure helper drift consumer");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()},
                                  root, "build-helper-drift"),
                   "initial helper drift build");

    const std::filesystem::path original_output =
        consumer_build / "generated" / "quarry" / "telemetry.hpp";
    ASSERT_TRUE(std::filesystem::exists(original_output));

    write_text_file(schema,
                    "namespace: quarry.telemetry.v2\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    std::filesystem::remove(original_output);

    const CommandResult stale_build = run_executable(
        QUARRY_TEST_CMAKE_COMMAND, {"--build", consumer_build.string()}, root,
        "stale-helper-drift-build");
    EXPECT_NE(stale_build.status, 0);
    const std::string combined_diagnostics = stale_build.stdout_text + stale_build.stderr_text;
    EXPECT_NE(combined_diagnostics.find("generated-output inventory changed"),
              std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(original_output));
    EXPECT_FALSE(std::filesystem::exists(consumer_build / "generated" / "quarry" /
                                         "telemetry" / "v2.hpp"));

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"-S", consumer_source.string(), "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
                                  root, "reconfigure-helper-drift"),
                   "reconfigure helper drift consumer");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()},
                                  root, "rebuild-helper-drift"),
                   "rebuild helper drift consumer");
    EXPECT_TRUE(std::filesystem::exists(consumer_build / "generated" / "quarry" /
                                        "telemetry" / "v2.hpp"));
}

TEST(SchemaCompilerPackageTest, HelperReportsConfigureFailures) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("helper failures");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");

    struct ConfigureFailureCase {
        std::string_view name;
        std::string_view cmake_lists;
        std::string_view expected;
    };

    auto expect_configure_failure = [&](const ConfigureFailureCase& failure_case) {
        const std::filesystem::path source = root / (std::string(failure_case.name) + " source");
        const std::filesystem::path build = root / (std::string(failure_case.name) + " build");
        write_text_file(source / "CMakeLists.txt", failure_case.cmake_lists);
        const CommandResult result = run_executable(
            QUARRY_TEST_CMAKE_COMMAND,
            {"-S", source.string(), "-B", build.string(),
             "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
            root, failure_case.name);
        EXPECT_NE(result.status, 0) << failure_case.name << " unexpectedly configured";
        EXPECT_TRUE(contains_case_insensitive(result.stderr_text, failure_case.expected))
            << failure_case.name << " stderr:\n"
            << result.stderr_text;
    };

    const std::string module_path =
        (install_prefix / "lib" / "cmake" / "Quarry" / "QuarryGenerate.cmake").string();
    expect_configure_failure({
        .name = "missing-compiler-target",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(missing_compiler_target LANGUAGES NONE)\n"
        "include(\"" +
            module_path +
            "\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files)\n",
        .expected = "target is not",
    });

    expect_configure_failure({
        .name = "missing-runtime-metadata",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(missing_runtime_metadata LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "unset(Quarry_GENERATED_CODE_API_VERSION)\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\" "
        "\"namespace: quarry.telemetry\\nrecord: Sample\\nversion: 1\\ntype: data\\n"
        "fields: {}\\n\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files)\n",
        .expected = "does not provide a valid",
    });

    expect_configure_failure({
        .name = "malformed-runtime-metadata",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(malformed_runtime_metadata LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "set(Quarry_GENERATED_CODE_API_VERSION invalid)\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\" "
        "\"namespace: quarry.telemetry\\nrecord: Sample\\nversion: 1\\ntype: data\\n"
        "fields: {}\\n\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files)\n",
        .expected = "canonical non-negative decimal integer",
    });

    const std::filesystem::path non_imported_source = root / "non imported target source";
    write_text_file(non_imported_source / "schema.brd",
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");
    write_text_file(non_imported_source / "dummy.cpp", "int main() { return 0; }\n");
    write_text_file(non_imported_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(non_imported_compiler LANGUAGES CXX)\n"
                    "add_executable(local_compiler dummy.cpp)\n"
                    "add_executable(Quarry::schema_compiler ALIAS local_compiler)\n"
                    "include(\"" +
                        module_path +
                        "\")\n"
                        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                        "OUT_FILES files)\n");
    const CommandResult non_imported_result = run_executable(
        QUARRY_TEST_CMAKE_COMMAND,
        {"-S", non_imported_source.string(), "-B", (root / "non imported target build").string(),
         "-DCMAKE_CXX_COMPILER=" + std::string(QUARRY_TEST_CXX_COMPILER)},
        root, "non-imported-target");
    EXPECT_NE(non_imported_result.status, 0);
    EXPECT_NE(non_imported_result.stderr_text.find("imported executable target"),
              std::string::npos);

    expect_configure_failure({
        .name = "missing-schema",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(missing_schema LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "quarry_generate_cpp(SCHEMA missing.brd OUTPUT_DIR generated OUT_FILES files)\n",
        .expected = "schema file does not exist",
    });

    expect_configure_failure({
        .name = "relative-compiler",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(relative_compiler LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\" "
        "\"namespace: quarry.telemetry\\nrecord: Sample\\nversion: 1\\ntype: data\\n"
        "fields: {}\\n\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files "
        "SCHEMA_COMPILER relative/compiler)\n",
        .expected = "SCHEMA_COMPILER must be an absolute path",
    });

    expect_configure_failure({
        .name = "missing-compiler",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(missing_compiler LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\" "
        "\"namespace: quarry.telemetry\\nrecord: Sample\\nversion: 1\\ntype: data\\n"
        "fields: {}\\n\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files "
        "SCHEMA_COMPILER \"" +
            (root / "missing compiler").string() +
            "\")\n",
        .expected = "executable does not exist",
    });

    const std::filesystem::path compiler_directory = root / "compiler directory";
    std::filesystem::create_directories(compiler_directory);
    expect_configure_failure({
        .name = "directory-compiler",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(directory_compiler LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\" "
        "\"namespace: quarry.telemetry\\nrecord: Sample\\nversion: 1\\ntype: data\\n"
        "fields: {}\\n\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files "
        "SCHEMA_COMPILER \"" +
            compiler_directory.string() +
            "\")\n",
        .expected = "directory:",
    });

    const std::filesystem::path non_runnable = root / "non runnable compiler";
    write_text_file(non_runnable, "#!/bin/sh\nexit 0\n");
    expect_configure_failure({
        .name = "non-runnable-compiler",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(non_runnable_compiler LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\" "
        "\"namespace: quarry.telemetry\\nrecord: Sample\\nversion: 1\\ntype: data\\n"
        "fields: {}\\n\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files "
        "SCHEMA_COMPILER \"" +
            non_runnable.string() +
            "\")\n",
        .expected = "permission denied",
    });

    const std::filesystem::path bad_version = root / "bad version compiler.sh";
    write_executable_file(bad_version, "#!/bin/sh\necho bad compiler >&2\nexit 9\n");
    expect_configure_failure({
        .name = "bad-version-compiler",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(bad_version_compiler LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\" "
        "\"namespace: quarry.telemetry\\nrecord: Sample\\nversion: 1\\ntype: data\\n"
        "fields: {}\\n\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files "
        "SCHEMA_COMPILER \"" +
            bad_version.string() +
            "\")\n",
        .expected = "bad compiler",
    });

    const std::filesystem::path failing_query =
        write_schema_compiler_failing_query_wrapper(root / "failing query compiler.sh",
                                                    "query failure");
    expect_configure_failure({
        .name = "failing-query-compiler",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(failing_query_compiler LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\" "
        "\"namespace: quarry.telemetry\\nrecord: Sample\\nversion: 1\\ntype: data\\n"
        "fields: {}\\n\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files "
        "SCHEMA_COMPILER \"" +
            failing_query.string() +
            "\")\n",
        .expected = "failed to query the generated-code API version",
    });

    expect_configure_failure({
        .name = "genex-compiler",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(genex_compiler LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\" "
        "\"namespace: quarry.telemetry\\nrecord: Sample\\nversion: 1\\ntype: data\\n"
        "fields: {}\\n\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files "
        "SCHEMA_COMPILER \"$<TARGET_FILE:Quarry::schema_compiler>\")\n",
        .expected = "generator",
    });

    expect_configure_failure({
        .name = "missing-compiler-value",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(missing_compiler_value LANGUAGES NONE)\n"
        "find_package(Quarry CONFIG REQUIRED)\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\" "
        "\"namespace: quarry.telemetry\\nrecord: Sample\\nversion: 1\\ntype: data\\n"
        "fields: {}\\n\")\n"
        "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files "
        "SCHEMA_COMPILER)\n",
        .expected = "missing value for SCHEMA_COMPILER",
    });

    const std::filesystem::path invalid_source = root / "invalid schema source";
    write_text_file(invalid_source / "schema.brd",
                    "namespace: quarry.telemetry\nrecord: Sample\nfields: [\n");
    write_text_file(invalid_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(invalid_schema LANGUAGES NONE)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                    "OUT_FILES files)\n");
    const CommandResult invalid_result = run_executable(
        QUARRY_TEST_CMAKE_COMMAND,
        {"-S", invalid_source.string(), "-B", (root / "invalid schema build").string(),
         "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
        root, "invalid-schema");
    EXPECT_NE(invalid_result.status, 0);
    EXPECT_NE(invalid_result.stderr_text.find("failed to list outputs"), std::string::npos);

    const std::filesystem::path duplicate_source = root / "duplicate output source";
    write_text_file(duplicate_source / "schema.brd",
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    write_text_file(duplicate_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(duplicate_outputs LANGUAGES NONE)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                    "OUT_FILES first)\n"
                    "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                    "OUT_FILES second)\n");
    const CommandResult duplicate_result = run_executable(
        QUARRY_TEST_CMAKE_COMMAND,
        {"-S", duplicate_source.string(), "-B", (root / "duplicate output build").string(),
         "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
        root, "duplicate-output");
    EXPECT_NE(duplicate_result.status, 0);
    EXPECT_NE(duplicate_result.stderr_text.find("is already claimed"), std::string::npos);

    const std::filesystem::path cross_source = root / "cross source";
    write_text_file(cross_source / "schema.brd",
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");
    write_text_file(cross_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(cross_rejected LANGUAGES NONE)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "quarry_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                    "OUT_FILES files)\n");
    const CommandResult cross_result = run_executable(
        QUARRY_TEST_CMAKE_COMMAND,
        {"-S", cross_source.string(), "-B", (root / "cross build").string(),
         "-DCMAKE_PREFIX_PATH=" + install_prefix.string(), "-DCMAKE_SYSTEM_NAME=Generic"},
        root, "cross-rejected");
    EXPECT_NE(cross_result.status, 0);
    EXPECT_NE(cross_result.stderr_text.find("cross-compiling requires SCHEMA_COMPILER"),
              std::string::npos);
}

TEST(SchemaCompilerPackageTest, CConsumerBuildsAndRunsAgainstInstalledPackage) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    // No quarry_generate_c() helper exists yet (PR-108; see
    // docs/design/c-backend.md and docs/distribution-model.md) -- this
    // mirrors ManualCustomCommandStillGeneratesDownstreamCode's manual
    // add_custom_command() pattern, the documented supported path for
    // callers who want explicit control, applied to --language c.
    const std::filesystem::path root = make_temp_directory("c consumer");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    const std::filesystem::path consumer_source = root / "c consumer source with spaces";
    const std::filesystem::path consumer_build = root / "c consumer build with spaces";

    write_text_file(consumer_source / "schema.brd",
                    "namespace: quarry.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n"
                    "  label:\n"
                    "    type: string\n"
                    "    max_bytes: 16\n"
                    "  blob:\n"
                    "    type: bytes\n"
                    "    max_bytes: 16\n"
                    "  readings:\n"
                    "    type: float32[]\n"
                    "    max_elements: 4\n");
    write_text_file(consumer_source / "main.c",
                    "#include <quarry/telemetry.generated.h>\n"
                    "#include <string.h>\n"
                    "int main(void) {\n"
                    "  quarry_telemetry_Sample_t sample;\n"
                    "  quarry_telemetry_Sample_init(&sample);\n"
                    "  sample.has_count = true;\n"
                    "  sample.count = 42U;\n"
                    "  sample.has_label = true;\n"
                    "  memcpy(sample.label, \"hello\", 5);\n"
                    "  sample.label_length = 5;\n"
                    "  sample.has_blob = true;\n"
                    "  { unsigned char content[] = {0x00U, 0xFFU, 0x10U};\n"
                    "    memcpy(sample.blob, content, 3); }\n"
                    "  sample.blob_length = 3;\n"
                    "  sample.has_readings = true;\n"
                    "  sample.readings_count = 2;\n"
                    "  sample.readings[0] = 1.5f;\n"
                    "  sample.readings[1] = -2.5f;\n"
                    "  unsigned char buf[64];\n"
                    "  quarry_telemetry_Sample_encode_result_t encoded =\n"
                    "      quarry_telemetry_Sample_encode(&sample, buf, sizeof(buf));\n"
                    "  if (encoded.status != QUARRY_C_STATUS_OK) { return 1; }\n"
                    "  quarry_telemetry_Sample_decode_result_t decoded =\n"
                    "      quarry_telemetry_Sample_decode(buf, encoded.bytes_written);\n"
                    "  if (decoded.status != QUARRY_C_STATUS_OK) { return 2; }\n"
                    "  if (!decoded.value.has_count || decoded.value.count != 42U) { return 3; }\n"
                    "  if (!decoded.value.has_label || decoded.value.label_length != 5) { return 4; }\n"
                    "  if (strcmp(decoded.value.label, \"hello\") != 0) { return 5; }\n"
                    "  if (!decoded.value.has_blob || decoded.value.blob_length != 3) { return 6; }\n"
                    "  { unsigned char expected[] = {0x00U, 0xFFU, 0x10U};\n"
                    "    if (memcmp(decoded.value.blob, expected, 3) != 0) { return 7; } }\n"
                    "  if (!decoded.value.has_readings || decoded.value.readings_count != 2) { return 8; }\n"
                    "  if (decoded.value.readings[0] != 1.5f || decoded.value.readings[1] != -2.5f) {\n"
                    "    return 9;\n"
                    "  }\n"
                    "  return 0;\n"
                    "}\n");
    write_text_file(consumer_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(c_consumer C)\n"
                    "find_package(Quarry CONFIG REQUIRED)\n"
                    "set(generated_dir \"${CMAKE_CURRENT_BINARY_DIR}/generated\")\n"
                    "set(generated_header \"${generated_dir}/quarry/telemetry.generated.h\")\n"
                    "set(generated_source \"${generated_dir}/quarry/telemetry.generated.c\")\n"
                    "add_custom_command(\n"
                    "  OUTPUT \"${generated_header}\" \"${generated_source}\"\n"
                    "  COMMAND \"$<TARGET_FILE:Quarry::schema_compiler>\"\n"
                    "          --language c\n"
                    "          --output-directory \"${generated_dir}\"\n"
                    "          \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\"\n"
                    "  DEPENDS \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\"\n"
                    "          Quarry::schema_compiler\n"
                    "  VERBATIM)\n"
                    "add_executable(c_consumer main.c \"${generated_header}\" \"${generated_source}\")\n"
                    "target_include_directories(c_consumer PRIVATE \"${generated_dir}\")\n"
                    "target_link_libraries(c_consumer PRIVATE Quarry::runtime_c)\n"
                    "set_target_properties(c_consumer PROPERTIES C_STANDARD 99 "
                    "C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)\n");

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--install", QUARRY_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Quarry package");

    // Regression guard: the installed tree must expose the C runtime under
    // its own canonical path, with no duplicate or generic unprefixed
    // headers (mirroring runtime_package_test.cpp's C++ equivalent guard).
    EXPECT_TRUE(std::filesystem::exists(install_prefix / "include" / "quarry" / "runtime_c" /
                                        "binary_record.h"));
    EXPECT_TRUE(std::filesystem::exists(install_prefix / "include" / "quarry" / "runtime_c" /
                                        "version.h"));

    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"-S", consumer_source.string(), "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string(),
                                   "-DCMAKE_C_COMPILER=" + std::string(QUARRY_TEST_C_COMPILER)},
                                  root, "configure-c-consumer"),
                   "configure C consumer");
    expect_success(run_executable(QUARRY_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()}, root, "build-c-consumer"),
                   "build C consumer");
    expect_success(run_executable(consumer_build / "c_consumer", {}, root, "run-c-consumer"),
                   "run C consumer");
}
