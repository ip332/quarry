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
