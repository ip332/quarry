// Runs the Python runtime's own unit test suite
// (runtime/python/tests/test_binary_record.py) as part of `ctest`, so
// runtime-level regressions are caught by the normal build/test cycle
// rather than only by someone remembering to invoke `python3 -m unittest`
// by hand. Mirrors tests/backend_python/python_execution_test.cpp's
// find_program-guarded, real-subprocess pattern.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifndef QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR
#error "QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR must be defined"
#endif
#ifndef QUARRY_TEST_PYTHON_RUNTIME_TESTS_DIR
#error "QUARRY_TEST_PYTHON_RUNTIME_TESTS_DIR must be defined"
#endif
#ifndef QUARRY_TEST_PYTHON3
#define QUARRY_TEST_PYTHON3 ""
#endif

namespace {

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

TEST(PythonRuntimeTest, BinaryRecordUnitTestSuitePasses) {
    const std::string python3_executable = QUARRY_TEST_PYTHON3;
    if (python3_executable.empty()) {
        GTEST_SKIP() << "python3 interpreter not found; skipping Python runtime test suite";
    }

    const std::filesystem::path tests_dir(QUARRY_TEST_PYTHON_RUNTIME_TESTS_DIR);
    const std::filesystem::path test_script = tests_dir / "test_binary_record.py";
    ASSERT_TRUE(std::filesystem::exists(test_script)) << test_script;

    // `python3 -m unittest <absolute-path>` fails to import the module on
    // some Python versions (observed: 3.14) -- unittest's path-to-module
    // conversion only reliably handles a path relative to the current
    // working directory. Running from the test's own directory and passing
    // the bare module name (no ".py", no path prefix) sidesteps that
    // entirely and works identically everywhere.
    const std::string command = "cd " + shell_quote(tests_dir.string()) + " && PYTHONPATH=" +
                                shell_quote(QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR) + " " +
                                shell_quote(python3_executable) +
                                " -m unittest test_binary_record 2>&1";
    EXPECT_EQ(run_and_get_exit_code(command), 0) << command;
}

} // namespace
