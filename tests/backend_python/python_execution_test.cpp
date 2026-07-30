// Proves the Python backend skeleton (PR-118) produces a package that a
// real Python interpreter can actually import and use, matching the
// project's "verify end-to-end, don't just trust generated text" discipline
// already applied to the C/C++ interop tests. Deliberately does not test
// any serialization behavior (there is none yet): only that (1) generating
// through the real quarry-schema-compiler binary and then importing the
// result with a real `python3` subprocess succeeds, (2) a zero-field record
// can be instantiated, and (3) calling encode() raises NotImplementedError,
// exactly as the generated stub promises.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifndef QUARRY_SCHEMA_COMPILER_TOOL
#error "QUARRY_SCHEMA_COMPILER_TOOL must be defined"
#endif
#ifndef QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR
#error "QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR must be defined"
#endif
#ifndef QUARRY_TEST_PYTHON3
#define QUARRY_TEST_PYTHON3 ""
#endif

namespace {

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string("quarry-python-backend-") + std::string(stem) + "-" +
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
    std::ofstream output{path, std::ios::binary};
    output << text;
}

// Runs `command` (already fully quoted by the caller) and returns the
// child's real exit code, unwrapping std::system()'s wait()-style encoded
// status the same way tests/interop/c_cpp_codec_interop_test.cpp does.
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

constexpr std::string_view kSchema = "namespace: acme.telemetry\n"
                                     "record: Sample\n"
                                     "version: 1\n"
                                     "type: data\n"
                                     "fields: {}\n";

TEST(PythonExecutionTest, GeneratedZeroFieldRecordImportsAndRaisesNotImplementedOnEncode) {
    const std::string python3_executable = QUARRY_TEST_PYTHON3;
    if (python3_executable.empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    const std::filesystem::path root = make_temp_directory("skeleton");
    const std::filesystem::path schema = root / "schema.brd";
    write_text_file(schema, kSchema);

    const std::filesystem::path generated = root / "generated";
    const std::string generate_command = shell_quote(QUARRY_SCHEMA_COMPILER_TOOL) +
                                        " --language python -o " + shell_quote(generated.string()) +
                                        " " + shell_quote(schema.string());
    ASSERT_EQ(run_and_get_exit_code(generate_command), 0) << generate_command;
    ASSERT_TRUE(std::filesystem::exists(generated / "acme" / "telemetry" / "schema.py"));
    ASSERT_TRUE(std::filesystem::exists(generated / "acme" / "__init__.py"));
    ASSERT_TRUE(std::filesystem::exists(generated / "acme" / "telemetry" / "__init__.py"));

    const std::filesystem::path harness_script = root / "harness.py";
    write_text_file(harness_script, "import sys\n"
                                   "from acme.telemetry.schema import Sample\n"
                                   "\n"
                                   "sample = Sample()\n"
                                   "try:\n"
                                   "    sample.encode()\n"
                                   "except NotImplementedError:\n"
                                   "    pass\n"
                                   "else:\n"
                                   "    print('ERROR: encode() did not raise "
                                   "NotImplementedError', file=sys.stderr)\n"
                                   "    sys.exit(1)\n"
                                   "\n"
                                   "print('OK')\n");

    const std::string python_path =
        generated.string() + ":" + std::string(QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR);
    const std::string run_command = "PYTHONPATH=" + shell_quote(python_path) + " " +
                                    shell_quote(python3_executable) + " " +
                                    shell_quote(harness_script.string());
    EXPECT_EQ(run_and_get_exit_code(run_command), 0) << run_command;
}

TEST(PythonExecutionTest, EpochMismatchRaisesImportErrorAtImportTime) {
    const std::string python3_executable = QUARRY_TEST_PYTHON3;
    if (python3_executable.empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python execution test";
    }

    const std::filesystem::path root = make_temp_directory("epoch-mismatch");
    const std::filesystem::path schema = root / "schema.brd";
    write_text_file(schema, kSchema);

    const std::filesystem::path generated = root / "generated";
    const std::string generate_command = shell_quote(QUARRY_SCHEMA_COMPILER_TOOL) +
                                        " --language python -o " + shell_quote(generated.string()) +
                                        " " + shell_quote(schema.string());
    ASSERT_EQ(run_and_get_exit_code(generate_command), 0) << generate_command;

    const std::filesystem::path harness_script = root / "harness.py";
    write_text_file(harness_script,
                    "import sys\n"
                    "import quarry.runtime.python as rt\n"
                    "rt.QUARRY_GENERATED_CODE_API_VERSION_PYTHON = 999\n"
                    "try:\n"
                    "    from acme.telemetry.schema import Sample\n"
                    "except ImportError:\n"
                    "    print('OK')\n"
                    "else:\n"
                    "    print('ERROR: import did not raise ImportError', file=sys.stderr)\n"
                    "    sys.exit(1)\n");

    const std::string python_path =
        generated.string() + ":" + std::string(QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR);
    const std::string run_command = "PYTHONPATH=" + shell_quote(python_path) + " " +
                                    shell_quote(python3_executable) + " " +
                                    shell_quote(harness_script.string());
    EXPECT_EQ(run_and_get_exit_code(run_command), 0) << run_command;
}

} // namespace
