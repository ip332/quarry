#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#ifndef QUARRY_BRF_INSPECT_TOOL
#error "QUARRY_BRF_INSPECT_TOOL must be defined"
#endif
#ifndef QUARRY_TEST_SOURCE_DIR
#error "QUARRY_TEST_SOURCE_DIR must be defined"
#endif

namespace {
const std::filesystem::path fixture = std::filesystem::path{QUARRY_TEST_SOURCE_DIR} /
                                      "tests/fixtures/generic_runtime_conformance";
std::string command(const std::string& args, const std::filesystem::path& input = {}) {
    const auto dir = std::filesystem::temp_directory_path() / "quarry-brf-inspect-test";
    std::filesystem::create_directories(dir);
    const auto out = dir / "out", err = dir / "err";
    std::string shell = std::string{"\""} + QUARRY_BRF_INSPECT_TOOL + "\" " + args +
                        " >\"" + out.string() + "\" 2>\"" + err.string() + "\"";
    if (!input.empty()) shell += " <\"" + input.string() + "\"";
    const int status = std::system(shell.c_str());
    std::ifstream o(out), e(err);
    return std::to_string(status) + "\n" +
           std::string{std::istreambuf_iterator<char>(o), {}} + "\n---ERR---\n" +
           std::string{std::istreambuf_iterator<char>(e), {}};
}
}

TEST(BrfInspectToolTest, ReflectiveRecordIdIsDeterministic) {
    const std::string args = "--qbs " + (fixture / "schema.qbs").string() + " --brf " +
        (fixture / "record.brf").string() + " --record-id 1";
    EXPECT_EQ(command(args), command(args));
    EXPECT_NE(command(args).find("Parent {"), std::string::npos);
}
TEST(BrfInspectToolTest, ReflectiveRecordNameAndStdinWork) {
    const std::string args = "--qbs " + (fixture / "schema.qbs").string() +
        " --brf - --record-name Parent --indent-width 4 --max-output-bytes 4096";
    const auto result = command(args, fixture / "record.brf");
    EXPECT_NE(result.find("Parent {"), std::string::npos);
    EXPECT_NE(result.find("    sequence: 42"), std::string::npos);
}
TEST(BrfInspectToolTest, MissingOrAmbiguousSelectorFailsOnStderr) {
    const std::string prefix = "--qbs " + (fixture / "schema.qbs").string() + " --brf " +
        (fixture / "record.brf").string();
    const auto missing = command(prefix);
    EXPECT_NE(missing.find("---ERR---\n"), std::string::npos);
    EXPECT_EQ(missing.find("Parent {"), std::string::npos);
    EXPECT_NE(command(prefix + " --record-id 1 --record-name Parent").find("---ERR---"), std::string::npos);
}
TEST(BrfInspectToolTest, MalformedInputsFailWithoutStdout) {
    const auto bad = std::filesystem::temp_directory_path() / "quarry-bad-brf";
    std::ofstream{bad} << "not BRF";
    const std::string args = "--qbs " + (fixture / "schema.qbs").string() + " --brf " +
        bad.string() + " --record-id 1";
    const auto result = command(args);
    EXPECT_EQ(result.find("Parent {"), std::string::npos);
    EXPECT_NE(result.find("BRF validation failed"), std::string::npos);
    std::filesystem::remove(bad);
}

TEST(BrfInspectToolTest, ListsReflectiveRecordsInQbsTableOrder) {
    // The checked-in conformance QBS is reflective and exercises the public listing path.
    const auto result = command("--qbs " + (fixture / "schema.qbs").string() + " --list-records");
    EXPECT_EQ(result, "0\n2 Child (Child)\n3 Item (Item)\n1 Parent (Parent)\n\n---ERR---\n");
    EXPECT_EQ(result.find("---ERR---\n\n"), std::string::npos);
}

TEST(BrfInspectToolTest, ListingRejectsBrfAndSelectors) {
    const std::string prefix = "--qbs " + (fixture / "schema.qbs").string() + " --list-records";
    EXPECT_NE(command(prefix + " --brf " + (fixture / "record.brf").string()).find("standalone QBS"), std::string::npos);
    EXPECT_NE(command(prefix + " --record-id 1").find("standalone QBS"), std::string::npos);
    EXPECT_NE(command(prefix + " --record-name Parent").find("standalone QBS"), std::string::npos);
}
