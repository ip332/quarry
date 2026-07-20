#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string("quarry-runtime-package-") + std::string(stem) + "-" +
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

[[nodiscard]] CommandResult run_command(std::string_view command,
                                        const std::filesystem::path& working_directory,
                                        std::string_view label) {
    const std::filesystem::path stdout_path = working_directory / (std::string(label) + ".stdout");
    const std::filesystem::path stderr_path = working_directory / (std::string(label) + ".stderr");

    std::ostringstream wrapped;
    wrapped << command << " > " << shell_quote(stdout_path.string()) << " 2> "
            << shell_quote(stderr_path.string());
    const int status = std::system(wrapped.str().c_str());
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

TEST(RuntimePackageConsumerTest, InstalledRuntimePackageBuildsExternalConsumer) {
    const std::filesystem::path root = make_temp_directory("consumer");
    const std::filesystem::path install_prefix = root / "install";
    const std::filesystem::path consumer_build = root / "consumer-build";

    const std::string install_command = std::string(shell_quote(QUARRY_TEST_CMAKE_COMMAND)) +
                                        " --install " +
                                        shell_quote(QUARRY_TEST_BUILD_DIR) + " --prefix " +
                                        shell_quote(install_prefix.string());
    expect_success(run_command(install_command, root, "install"), "install runtime package");

    const std::string configure_command =
        std::string(shell_quote(QUARRY_TEST_CMAKE_COMMAND)) + " -S " +
        shell_quote(QUARRY_RUNTIME_PACKAGE_CONSUMER_SOURCE_DIR) + " -B " +
        shell_quote(consumer_build.string()) + " -DCMAKE_PREFIX_PATH=" +
        shell_quote(install_prefix.string()) + " -DCMAKE_CXX_COMPILER=" +
        shell_quote(QUARRY_TEST_CXX_COMPILER);
    expect_success(run_command(configure_command, root, "configure-consumer"),
                   "configure external consumer");

    const std::string build_command = std::string(shell_quote(QUARRY_TEST_CMAKE_COMMAND)) +
                                      " --build " + shell_quote(consumer_build.string());
    expect_success(run_command(build_command, root, "build-consumer"), "build external consumer");

    const std::filesystem::path executable = consumer_build / "runtime_package_consumer";
    expect_success(run_command(shell_quote(executable.string()), root, "run-consumer"),
                   "run external consumer");
}
