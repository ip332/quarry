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

namespace {

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string("breadcrumbs-schema-compiler-package-") + std::string(stem) + "-" +
         std::to_string(suffix));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
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

} // namespace

TEST(SchemaCompilerPackageTest, ImportedExecutableTargetGeneratesDownstreamCode) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("consumer with spaces");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    const std::filesystem::path consumer_build = root / "consumer build with spaces";

    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
                                  {"--install", BREADCRUMBS_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Breadcrumbs package");

    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
                                  {"-S", BREADCRUMBS_SCHEMA_COMPILER_PACKAGE_CONSUMER_SOURCE_DIR,
                                   "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string(),
                                   "-DCMAKE_CXX_COMPILER=" +
                                       std::string(BREADCRUMBS_TEST_CXX_COMPILER),
                                   "-DEXPECTED_BREADCRUMBS_PREFIX=" + install_prefix.string()},
                                  root, "configure-consumer"),
                   "configure external consumer");

    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()}, root, "build-consumer"),
                   "build external consumer");

    const std::filesystem::path generated_header =
        consumer_build / "generated" / "breadcrumbs" / "telemetry.generated.hpp";
    ASSERT_TRUE(std::filesystem::exists(generated_header));
    const std::string generated = read_text_file(generated_header);
    EXPECT_NE(generated.find("struct Sample"), std::string::npos);
    EXPECT_NE(generated.find("std::uint32_t"), std::string::npos);
    EXPECT_EQ(generated.find(BREADCRUMBS_TEST_BUILD_DIR), std::string::npos);

    const std::filesystem::path executable =
        consumer_build / "breadcrumbs_schema_compiler_cmake";
    expect_success(run_executable(executable, {}, root, "run-consumer"),
                   "run external consumer");

    const std::filesystem::path targets_file =
        install_prefix / "lib" / "cmake" / "Breadcrumbs" / "BreadcrumbsTargets.cmake";
    const std::string targets = read_text_file(targets_file);
    EXPECT_NE(targets.find("add_executable(Breadcrumbs::schema_compiler IMPORTED)"),
              std::string::npos);
    EXPECT_EQ(targets.find("breadcrumbs_compiler_backend"), std::string::npos);
    EXPECT_EQ(targets.find("protobuf::"), std::string::npos);
    EXPECT_EQ(targets.find("absl::"), std::string::npos);
    EXPECT_EQ(targets.find("breadcrumbs_yaml"), std::string::npos);
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
                    "namespace: breadcrumbs.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    write_text_file(consumer_source / "main.cpp",
                    "#include <breadcrumbs/telemetry.generated.hpp>\n"
                    "int main() {\n"
                    "  breadcrumbs::telemetry::SampleBuilder builder;\n"
                    "  if (!builder.set_count(7)) { return 1; }\n"
                    "  const auto sample = builder.build();\n"
                    "  return sample.has_count() ? 0 : 1;\n"
                    "}\n");
    write_text_file(consumer_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(manual_schema_compiler_consumer LANGUAGES CXX)\n"
                    "find_package(Breadcrumbs CONFIG REQUIRED)\n"
                    "set(generated_dir \"${CMAKE_CURRENT_BINARY_DIR}/generated\")\n"
                    "set(generated_header \"${generated_dir}/breadcrumbs/telemetry.generated.hpp\")\n"
                    "add_custom_command(\n"
                    "  OUTPUT \"${generated_header}\"\n"
                    "  COMMAND \"$<TARGET_FILE:Breadcrumbs::schema_compiler>\"\n"
                    "          --output-directory \"${generated_dir}\"\n"
                    "          \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\"\n"
                    "  DEPENDS \"${CMAKE_CURRENT_SOURCE_DIR}/schema.brd\"\n"
                    "          Breadcrumbs::schema_compiler\n"
                    "  VERBATIM)\n"
                    "add_executable(manual_consumer main.cpp \"${generated_header}\")\n"
                    "target_include_directories(manual_consumer PRIVATE \"${generated_dir}\")\n"
                    "target_link_libraries(manual_consumer PRIVATE Breadcrumbs::runtime)\n");

    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
                                  {"--install", BREADCRUMBS_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Breadcrumbs package");
    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
                                  {"-S", consumer_source.string(), "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string(),
                                   "-DCMAKE_CXX_COMPILER=" +
                                       std::string(BREADCRUMBS_TEST_CXX_COMPILER)},
                                  root, "configure-manual"),
                   "configure manual consumer");
    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
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
                    "namespace: breadcrumbs.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    write_text_file(consumer_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(helper_reconfigure LANGUAGES NONE)\n"
                    "find_package(Breadcrumbs CONFIG REQUIRED)\n"
                    "breadcrumbs_generate_cpp(\n"
                    "  SCHEMA schema.brd\n"
                    "  OUTPUT_DIR generated\n"
                    "  OUT_FILES generated_files\n"
                    "  FILE_EXTENSION .hpp)\n"
                    "file(WRITE \"${CMAKE_CURRENT_BINARY_DIR}/outputs.txt\" "
                    "\"${generated_files}\\n\")\n"
                    "add_custom_target(generated ALL DEPENDS ${generated_files})\n");

    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
                                  {"--install", BREADCRUMBS_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Breadcrumbs package");
    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
                                  {"-S", consumer_source.string(), "-B", consumer_build.string(),
                                   "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
                                  root, "configure-helper-reconfigure"),
                   "configure helper reconfigure consumer");
    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()},
                                  root, "build-helper-reconfigure"),
                   "build helper reconfigure consumer");
    EXPECT_TRUE(std::filesystem::exists(consumer_build / "generated" / "breadcrumbs" /
                                        "telemetry.hpp"));

    write_text_file(schema,
                    "namespace: breadcrumbs.telemetry.v2\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");

    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
                                  {"--build", consumer_build.string()},
                                  root, "rebuild-helper-reconfigure"),
                   "rebuild helper reconfigure consumer");
    EXPECT_TRUE(std::filesystem::exists(consumer_build / "generated" / "breadcrumbs" /
                                        "telemetry" / "v2.hpp"));
    const std::string outputs = read_text_file(consumer_build / "outputs.txt");
    EXPECT_NE(outputs.find("generated/breadcrumbs/telemetry/v2.hpp"), std::string::npos);
}

TEST(SchemaCompilerPackageTest, HelperReportsConfigureFailures) {
#ifdef _WIN32
    GTEST_SKIP() << "schema compiler package subprocess test is not implemented on Windows";
#endif

    const std::filesystem::path root = make_temp_directory("helper failures");
    const std::filesystem::path install_prefix = root / "install prefix with spaces";
    expect_success(run_executable(BREADCRUMBS_TEST_CMAKE_COMMAND,
                                  {"--install", BREADCRUMBS_TEST_BUILD_DIR, "--prefix",
                                   install_prefix.string()},
                                  root, "install"),
                   "install Breadcrumbs package");

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
            BREADCRUMBS_TEST_CMAKE_COMMAND,
            {"-S", source.string(), "-B", build.string(),
             "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
            root, failure_case.name);
        EXPECT_NE(result.status, 0) << failure_case.name << " unexpectedly configured";
        EXPECT_NE(result.stderr_text.find(failure_case.expected), std::string::npos)
            << failure_case.name << " stderr:\n"
            << result.stderr_text;
    };

    const std::string module_path =
        (install_prefix / "lib" / "cmake" / "Breadcrumbs" / "BreadcrumbsGenerate.cmake").string();
    expect_configure_failure({
        .name = "missing-compiler-target",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(missing_compiler_target LANGUAGES NONE)\n"
        "include(\"" +
            module_path +
            "\")\n"
            "breadcrumbs_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated OUT_FILES files)\n",
        .expected = "target is not",
    });

    const std::filesystem::path non_imported_source = root / "non imported target source";
    write_text_file(non_imported_source / "schema.brd",
                    "namespace: breadcrumbs.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");
    write_text_file(non_imported_source / "dummy.cpp", "int main() { return 0; }\n");
    write_text_file(non_imported_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(non_imported_compiler LANGUAGES CXX)\n"
                    "add_executable(local_compiler dummy.cpp)\n"
                    "add_executable(Breadcrumbs::schema_compiler ALIAS local_compiler)\n"
                    "include(\"" +
                        module_path +
                        "\")\n"
                        "breadcrumbs_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                        "OUT_FILES files)\n");
    const CommandResult non_imported_result = run_executable(
        BREADCRUMBS_TEST_CMAKE_COMMAND,
        {"-S", non_imported_source.string(), "-B", (root / "non imported target build").string(),
         "-DCMAKE_CXX_COMPILER=" + std::string(BREADCRUMBS_TEST_CXX_COMPILER)},
        root, "non-imported-target");
    EXPECT_NE(non_imported_result.status, 0);
    EXPECT_NE(non_imported_result.stderr_text.find("imported executable target"),
              std::string::npos);

    expect_configure_failure({
        .name = "missing-schema",
        .cmake_lists =
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(missing_schema LANGUAGES NONE)\n"
        "find_package(Breadcrumbs CONFIG REQUIRED)\n"
        "breadcrumbs_generate_cpp(SCHEMA missing.brd OUTPUT_DIR generated OUT_FILES files)\n",
        .expected = "schema file does not exist",
    });

    const std::filesystem::path invalid_source = root / "invalid schema source";
    write_text_file(invalid_source / "schema.brd",
                    "namespace: breadcrumbs.telemetry\nrecord: Sample\nfields: [\n");
    write_text_file(invalid_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(invalid_schema LANGUAGES NONE)\n"
                    "find_package(Breadcrumbs CONFIG REQUIRED)\n"
                    "breadcrumbs_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                    "OUT_FILES files)\n");
    const CommandResult invalid_result = run_executable(
        BREADCRUMBS_TEST_CMAKE_COMMAND,
        {"-S", invalid_source.string(), "-B", (root / "invalid schema build").string(),
         "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
        root, "invalid-schema");
    EXPECT_NE(invalid_result.status, 0);
    EXPECT_NE(invalid_result.stderr_text.find("failed to list outputs"), std::string::npos);

    const std::filesystem::path duplicate_source = root / "duplicate output source";
    write_text_file(duplicate_source / "schema.brd",
                    "namespace: breadcrumbs.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields:\n"
                    "  count:\n"
                    "    type: uint32\n");
    write_text_file(duplicate_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(duplicate_outputs LANGUAGES NONE)\n"
                    "find_package(Breadcrumbs CONFIG REQUIRED)\n"
                    "breadcrumbs_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                    "OUT_FILES first)\n"
                    "breadcrumbs_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                    "OUT_FILES second)\n");
    const CommandResult duplicate_result = run_executable(
        BREADCRUMBS_TEST_CMAKE_COMMAND,
        {"-S", duplicate_source.string(), "-B", (root / "duplicate output build").string(),
         "-DCMAKE_PREFIX_PATH=" + install_prefix.string()},
        root, "duplicate-output");
    EXPECT_NE(duplicate_result.status, 0);
    EXPECT_NE(duplicate_result.stderr_text.find("is already claimed"), std::string::npos);

    const std::filesystem::path cross_source = root / "cross source";
    write_text_file(cross_source / "schema.brd",
                    "namespace: breadcrumbs.telemetry\n"
                    "record: Sample\n"
                    "version: 1\n"
                    "type: data\n"
                    "fields: {}\n");
    write_text_file(cross_source / "CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\n"
                    "project(cross_rejected LANGUAGES NONE)\n"
                    "find_package(Breadcrumbs CONFIG REQUIRED)\n"
                    "breadcrumbs_generate_cpp(SCHEMA schema.brd OUTPUT_DIR generated "
                    "OUT_FILES files)\n");
    const CommandResult cross_result = run_executable(
        BREADCRUMBS_TEST_CMAKE_COMMAND,
        {"-S", cross_source.string(), "-B", (root / "cross build").string(),
         "-DCMAKE_PREFIX_PATH=" + install_prefix.string(), "-DCMAKE_SYSTEM_NAME=Generic"},
        root, "cross-rejected");
    EXPECT_NE(cross_result.status, 0);
    EXPECT_NE(cross_result.stderr_text.find("cross-compiling is not supported"),
              std::string::npos);
}
