// Generated configuration identity (#99, docs/design/configuration-identity.md
// §4 and §9): every backend variant embeds the configuration's schema_id,
// generated files that depend on each other check that they come from the same
// configuration, dependencies generated before configuration identity fail
// with an actionable message, and two configurations can live in one C, C++,
// C++/C-storage or Go program without the linker merging their definitions.
//
// The tests are black-box: they drive quarry-schema-compiler and compile,
// link, import or build the generated code with the real toolchains. Fixtures
// live in tests/fixtures/configuration_identity.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "compiler/support/namespace_macro.hpp"

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kStrictCxx = " -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Werror";
constexpr std::string_view kStrictC = " -x c -std=c99 -Wall -Wextra -Wpedantic -Werror";

// Pinned identities (design §9.1): the truck reference configuration and the
// three fixture configurations. A change means the canonical identity input
// changed, which changes every generated binding.
constexpr std::string_view kTruckSchemaId = "9200ca494f010801b830d0857cf66dde";
constexpr std::string_view kRevASchemaId = "9d543cf525b06a3364a1f41a293db92c";
constexpr std::string_view kRevBSameIdsSchemaId = "081f33889f48a1990848eb24973a6897";
constexpr std::string_view kRevBRenumberedSchemaId = "381dc857188ccf6121f98fecba2417cd";

[[nodiscard]] fs::path source_dir() { return fs::path(QUARRY_TEST_SOURCE_DIR); }
[[nodiscard]] fs::path fixtures() { return source_dir() / "tests/fixtures/configuration_identity"; }
[[nodiscard]] fs::path rev_a() { return fixtures() / "rev_a/configuration.brd"; }
[[nodiscard]] fs::path rev_b_same_ids() { return fixtures() / "rev_b_same_ids/configuration.brd"; }
[[nodiscard]] fs::path rev_b_renumbered() {
    return fixtures() / "rev_b_renumbered/configuration.brd";
}

[[nodiscard]] std::string quote(std::string_view text) {
    std::string quoted = "'";
    for (const char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

[[nodiscard]] fs::path make_temp_directory(std::string_view stem) {
    const fs::path directory =
        fs::temp_directory_path() /
        ("quarry-identity-" + std::string(stem) + "-" +
         std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
         std::to_string(std::hash<std::string>{}(
             ::testing::UnitTest::GetInstance()->current_test_info()->name())));
    fs::remove_all(directory);
    fs::create_directories(directory);
    return directory;
}

void write_text_file(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary};
    if (!output) {
        throw std::runtime_error("failed to open " + path.string());
    }
    output << text;
}

[[nodiscard]] std::string read_text_file(const fs::path& path) {
    std::ifstream input{path, std::ios::binary};
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

struct RunResult {
    int status = 0;
    std::string output; // stdout and stderr combined
};

[[nodiscard]] RunResult run(const std::string& command, const fs::path& log) {
    const int status = std::system((command + " > " + quote(log.string()) + " 2>&1").c_str());
#ifdef _WIN32
    const int exit_status = status;
#else
    const int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + status;
#endif
    return RunResult{.status = exit_status, .output = read_text_file(log)};
}

// Runs quarry-schema-compiler with `arguments` (already shell-quoted where needed).
[[nodiscard]] RunResult compiler(const std::string& arguments, const fs::path& log) {
    return run(quote(QUARRY_SCHEMA_COMPILER_TOOL) + " " + arguments, log);
}

[[nodiscard]] std::string print_schema_id(const fs::path& root, const fs::path& work) {
    const RunResult result =
        compiler("--print-schema-id " + quote(root.string()), work / "print-schema-id.log");
    EXPECT_EQ(result.status, 0) << result.output;
    std::string id = result.output;
    while (!id.empty() && (id.back() == '\n' || id.back() == '\r')) {
        id.pop_back();
    }
    return id;
}

[[nodiscard]] std::string upper(std::string text) {
    for (char& ch : text) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    return text;
}

// The five generated-code variants of design §4.
enum class Variant { C, Cpp, CppCStorage, Python, Go };

struct VariantInfo {
    Variant variant;
    std::string_view name;
    std::string_view arguments;    // language options for quarry-schema-compiler
    std::string_view common_file;  // fleet.common's generated file, relative to the output root
    std::string_view engine_file;  // fleet.engine's generated file
    std::string_view pre_identity; // fixture directory under pre_identity/
};

constexpr VariantInfo kVariants[] = {
    {Variant::C, "c", "--language c", "fleet/common.generated.h", "fleet/engine.generated.h", "c"},
    {Variant::Cpp, "cpp", "--language cpp", "fleet/common.generated.hpp",
     "fleet/engine.generated.hpp", "cpp"},
    {Variant::CppCStorage, "cpp_c_storage", "--language cpp --cpp-storage c",
     "fleet/common.generated.hpp", "fleet/engine.generated.hpp", "cpp_c_storage"},
    {Variant::Python, "python", "--language python", "fleet/common/schema.py",
     "fleet/engine/schema.py", "python"},
    {Variant::Go, "go", "--language go --go-import-prefix example.com/gen",
     "fleet/common/schema.quarry.go", "fleet/engine/schema.quarry.go", "go"},
};

constexpr std::string_view kGoMod = R"(module example.com/gen

go 1.22

require github.com/ip332/quarry/runtime/go v0.0.0

replace github.com/ip332/quarry/runtime/go => RUNTIME
)";

[[nodiscard]] bool toolchain_available(Variant variant) {
    if (variant == Variant::Python) {
        return !std::string_view(QUARRY_TEST_PYTHON3).empty();
    }
    if (variant == Variant::Go) {
        return !std::string_view(QUARRY_TEST_GO).empty();
    }
    return true;
}

[[nodiscard]] std::string include_flags(const std::vector<fs::path>& roots) {
    std::string flags;
    for (const fs::path& root : roots) {
        flags += " -I" + quote(root.string());
    }
    flags += " -I" + quote(QUARRY_TEST_REPO_INCLUDE_DIR) + " -I" +
             quote(QUARRY_TEST_GENERATED_INCLUDE_DIR);
    return flags;
}

// Generates `variant` bindings for `root` (optionally restricted to the
// `--generate` selection) into `output`.
void generate(const VariantInfo& variant, const fs::path& root, const fs::path& output,
              std::string_view selection = {}) {
    std::string arguments =
        std::string(variant.arguments) + " --output-directory " + quote(output.string());
    if (!selection.empty()) {
        arguments += " --generate " + std::string(selection);
    }
    const RunResult result = compiler(arguments + " " + quote(root.string()),
                                      output.parent_path() / (output.filename().string() + ".log"));
    ASSERT_EQ(result.status, 0) << result.output;
}

// Builds or imports code that uses fleet.engine from `output`, with the
// include path `include_roots` for C and C++ (defaults to `output`).
[[nodiscard]] RunResult use_engine(const VariantInfo& variant, const fs::path& work,
                                   const fs::path& output,
                                   std::vector<fs::path> include_roots = {}) {
    if (include_roots.empty()) {
        include_roots.push_back(output);
    }
    const fs::path log = work / (std::string(variant.name) + "-use.log");
    switch (variant.variant) {
    case Variant::C: {
        const fs::path source = work / "use_engine.c";
        write_text_file(source, "#include \"" + (output / variant.engine_file).string() +
                                    "\"\nint main(void) { return 0; }\n");
        return run(quote(QUARRY_TEST_CXX_COMPILER) + std::string(kStrictC) +
                       include_flags(include_roots) + " -fsyntax-only " + quote(source.string()),
                   log);
    }
    case Variant::Cpp:
    case Variant::CppCStorage: {
        const fs::path source = work / "use_engine.cpp";
        write_text_file(source, "#include \"" + (output / variant.engine_file).string() +
                                    "\"\nint main() { return 0; }\n");
        return run(quote(QUARRY_TEST_CXX_COMPILER) + std::string(kStrictCxx) +
                       include_flags(include_roots) + " -fsyntax-only " + quote(source.string()),
                   log);
    }
    case Variant::Python:
        return run(
            "PYTHONPATH=" + quote(output.string() + ":" + QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR) +
                " " + quote(QUARRY_TEST_PYTHON3) + " -c 'import fleet.engine.schema'",
            log);
    case Variant::Go: {
        std::string mod(kGoMod);
        mod.replace(mod.find("RUNTIME"), 7, (source_dir() / "runtime/go").string());
        write_text_file(output / "go.mod", mod);
        return run("cd " + quote(output.string()) +
                       " && env GOTOOLCHAIN=local GOPROXY=off GOFLAGS=-buildvcs=false "
                       "GO111MODULE=on GOCACHE=" +
                       quote((work / "gocache").string()) +
                       " GOPATH=" + quote((work / "gopath").string()) + " " +
                       quote(QUARRY_TEST_GO) + " build ./...",
                   log);
    }
    }
    return {};
}

class ConfigurationIdentityVariantTest : public ::testing::TestWithParam<VariantInfo> {
protected:
    void SetUp() override {
        if (!toolchain_available(GetParam().variant)) {
            GTEST_SKIP() << GetParam().name << " toolchain not found at configure time";
        }
        work_ = make_temp_directory(std::string(GetParam().name));
    }
    void TearDown() override {
        if (!work_.empty()) {
            fs::remove_all(work_);
        }
    }
    fs::path work_;
};

std::string variant_name(const ::testing::TestParamInfo<VariantInfo>& info) {
    return std::string(info.param.name);
}

INSTANTIATE_TEST_SUITE_P(AllVariants, ConfigurationIdentityVariantTest,
                         ::testing::ValuesIn(kVariants), variant_name);

// §9.1: the identity of each fixture and of the truck reference configuration
// is pinned, and neither --generate nor the configuration name nor the QBS
// mode changes it.
TEST(ConfigurationIdentityTest, SchemaIdIsPinnedAndIndependentOfSelectionNameAndQbsMode) {
    const fs::path work = make_temp_directory("pinned");
    EXPECT_EQ(print_schema_id(source_dir() / "examples/truck/schemas/configuration.brd", work),
              kTruckSchemaId);
    EXPECT_EQ(print_schema_id(rev_a(), work), kRevASchemaId);
    EXPECT_EQ(print_schema_id(rev_b_same_ids(), work), kRevBSameIdsSchemaId);
    EXPECT_EQ(print_schema_id(rev_b_renumbered(), work), kRevBRenumberedSchemaId);

    // A renamed copy of revision A is the same configuration.
    const fs::path renamed = work / "renamed";
    fs::copy(fixtures() / "rev_a", renamed);
    std::string configuration = read_text_file(renamed / "configuration.brd");
    configuration.replace(configuration.find("fleet_rev_a"), 11, "renamed_configuration");
    write_text_file(renamed / "configuration.brd", configuration);
    EXPECT_EQ(print_schema_id(renamed / "configuration.brd", work), kRevASchemaId);

    // Both QBS modes carry the printed identity in header bytes 12..27.
    for (const std::string_view mode : {"", " --qbs-reflective"}) {
        const fs::path qbs = work / "image.qbs";
        const RunResult result = compiler("--emit-qbs " + quote(qbs.string()) + std::string(mode) +
                                              " " + quote(rev_a().string()),
                                          work / "qbs.log");
        ASSERT_EQ(result.status, 0) << result.output;
        const std::string bytes = read_text_file(qbs);
        ASSERT_GE(bytes.size(), 28U);
        std::string hex;
        constexpr char kHexDigits[] = "0123456789abcdef";
        for (std::size_t index = 12; index < 28; ++index) {
            const auto byte = static_cast<unsigned char>(bytes[index]);
            hex.push_back(kHexDigits[byte >> 4U]);
            hex.push_back(kHexDigits[byte & 0x0FU]);
        }
        EXPECT_EQ(hex, kRevASchemaId) << "QBS mode:" << mode;
    }
    fs::remove_all(work);
}

// §9.2: every variant embeds exactly the --print-schema-id value, whatever the
// --generate selection.
TEST_P(ConfigurationIdentityVariantTest, EmbedsThePrintedSchemaIdForEverySelection) {
    const VariantInfo& variant = GetParam();
    const std::string id = print_schema_id(rev_a(), work_);
    ASSERT_EQ(id, kRevASchemaId);
    for (const std::string_view selection :
         {std::string_view{}, std::string_view{"fleet.common"}, std::string_view{"fleet.engine"},
          std::string_view{"fleet.common,fleet.engine"}}) {
        const fs::path output = work_ / ("out-" + std::to_string(selection.size()));
        generate(variant, rev_a(), output, selection);
        for (const std::string_view file : {variant.common_file, variant.engine_file}) {
            if (!fs::exists(output / file)) {
                continue; // not selected
            }
            const std::string text = read_text_file(output / file);
            switch (variant.variant) {
            case Variant::C:
            case Variant::CppCStorage:
                if (variant.variant == Variant::C) {
                    // Since #111 G2 the plain C guard carries the namespace
                    // encoding and the generated-code identity, not schema_id.
                    EXPECT_EQ(text.find(upper(id)), std::string::npos) << file;
                    EXPECT_NE(text.find("#ifndef QUARRY_GENERATED_C_NS_"), std::string::npos)
                        << file;
                }
                {
                    const std::string ns =
                        file == variant.common_file ? "fleet.common" : "fleet.engine";
                    const std::string stem =
                        std::string(quarry::compiler::support::kCNamespaceMacroPrefix) +
                        quarry::compiler::support::encode_namespace_macro(ns).value();
                    EXPECT_NE(text.find(stem + "_SCHEMA_ID_W0 0x" + id.substr(0, 8) + "UL"),
                              std::string::npos)
                        << file;
                    EXPECT_NE(text.find(stem + "_SCHEMA_ID_W3 0x" + id.substr(24, 8) + "UL"),
                              std::string::npos)
                        << file;
                }
                if (variant.variant == Variant::CppCStorage) {
                    // Linkage isolation is keyed by the generated-code identity
                    // (#111), not by schema_id; schema_id stays in the macros.
                    EXPECT_EQ(text.find("quarry_cfg_"), std::string::npos) << file;
                    EXPECT_NE(text.find("inline namespace quarry_gen_"), std::string::npos) << file;
                }
                break;
            case Variant::Cpp: {
                const std::string ns =
                    file == variant.common_file ? "fleet.common" : "fleet.engine";
                const std::string stem =
                    std::string(quarry::compiler::support::kCppOwningNamespaceMacroPrefix) +
                    quarry::compiler::support::encode_namespace_macro(ns).value();
                EXPECT_NE(text.find(stem + "_SCHEMA_ID_W0 0x" + id.substr(0, 8) + "UL"),
                          std::string::npos)
                    << file;
                EXPECT_NE(text.find(stem + "_SCHEMA_ID_W3 0x" + id.substr(24, 8) + "UL"),
                          std::string::npos)
                    << file;
                EXPECT_EQ(text.find("quarry_cfg_"), std::string::npos) << file;
                EXPECT_NE(text.find("inline namespace quarry_gen_"), std::string::npos) << file;
                break;
            }
            case Variant::Python:
                EXPECT_NE(text.find("QUARRY_SCHEMA_ID = bytes.fromhex(\"" + id + "\")"),
                          std::string::npos);
                break;
            case Variant::Go:
                EXPECT_NE(text.find("QuarrySchemaIDHi uint64 = 0x" + id.substr(0, 16)),
                          std::string::npos);
                EXPECT_NE(text.find("QuarrySchemaIDLo uint64 = 0x" + id.substr(16)),
                          std::string::npos);
                break;
            }
        }
    }
}

// §9.3: the dependency generated from another root is rejected, for both a
// revision B that keeps the mixed record's numeric ID and one that renumbers
// it; separate invocations from one root build.
TEST_P(ConfigurationIdentityVariantTest, RejectsDependenciesFromAnotherConfiguration) {
    const VariantInfo& variant = GetParam();
    const std::string engine = variant.variant == Variant::Python
                                   ? "fleet.engine.schema"
                                   : std::string(variant.engine_file);
    const std::string common = variant.variant == Variant::Python
                                   ? "fleet.common.schema"
                                   : std::string(variant.common_file);

    for (const fs::path& rev_b : {rev_b_same_ids(), rev_b_renumbered()}) {
        SCOPED_TRACE(rev_b.parent_path().filename().string());
        const fs::path output = work_ / ("mixed-" + rev_b.parent_path().filename().string());
        generate(variant, rev_a(), output, "fleet.engine");
        generate(variant, rev_b, output, "fleet.common");
        const RunResult mixed = use_engine(variant, work_, output);
        EXPECT_NE(mixed.status, 0) << mixed.output;
        if (variant.variant == Variant::Go) {
            EXPECT_NE(mixed.output.find("overflows"), std::string::npos) << mixed.output;
        } else {
            EXPECT_NE(mixed.output.find(common +
                                        " was generated from a different Quarry configuration "
                                        "than " +
                                        engine),
                      std::string::npos)
                << mixed.output;
        }
    }

    const fs::path consistent = work_ / "consistent";
    generate(variant, rev_a(), consistent, "fleet.engine");
    generate(variant, rev_a(), consistent, "fleet.common");
    const RunResult same_root = use_engine(variant, work_, consistent);
    EXPECT_EQ(same_root.status, 0) << same_root.output;
}

// Review decision 2: a dependency generated by a compiler that predates
// configuration identity fails with an actionable message (Go: a compile error
// on a line whose comment explains it).
TEST_P(ConfigurationIdentityVariantTest, RejectsDependenciesThatPredateIdentity) {
    const VariantInfo& variant = GetParam();
    const fs::path output = work_ / "older";
    const fs::path old_files = fixtures() / "pre_identity" / variant.pre_identity;
    for (const auto& entry : fs::recursive_directory_iterator(old_files)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        fs::path relative = fs::relative(entry.path(), old_files);
        relative.replace_extension(); // drop ".pre_identity"
        fs::create_directories((output / relative).parent_path());
        fs::copy_file(entry.path(), output / relative);
    }
    generate(variant, rev_a(), output, "fleet.engine");
    const RunResult result = use_engine(variant, work_, output);
    EXPECT_NE(result.status, 0) << result.output;
    if (variant.variant == Variant::Go) {
        EXPECT_NE(result.output.find("undefined: quarrydep0.QuarrySchemaID"), std::string::npos)
            << result.output;
        EXPECT_NE(read_text_file(output / variant.engine_file)
                      .find("means the dependency predates generated configuration identity"),
                  std::string::npos);
    } else {
        EXPECT_NE(result.output.find("predates generated configuration identity"),
                  std::string::npos)
            << result.output;
    }
}

// §9.4: with two configurations' output roots on the include path, a header
// that resolves its dependency from the other configuration fails to compile.
TEST_P(ConfigurationIdentityVariantTest, RejectsIncludePathMixing) {
    const VariantInfo& variant = GetParam();
    if (variant.variant == Variant::Python || variant.variant == Variant::Go) {
        GTEST_SKIP() << "include-path mixing applies to C and C++ only";
    }
    const fs::path a = work_ / "a";
    const fs::path b = work_ / "b";
    generate(variant, rev_a(), a);
    generate(variant, rev_b_same_ids(), b);
    const RunResult wrong_order = use_engine(variant, work_, a, {b, a});
    EXPECT_NE(wrong_order.status, 0) << wrong_order.output;
    EXPECT_NE(wrong_order.output.find("was generated from a different Quarry configuration"),
              std::string::npos)
        << wrong_order.output;
    const RunResult right_order = use_engine(variant, work_, a, {a, b});
    EXPECT_EQ(right_order.status, 0) << right_order.output;
}

// Writes one translation unit per configuration plus main, links them into
// one executable at -O0 (so template and inline definitions are emitted
// out of line and the linker would merge any shared ones), and runs it.
// Revision A and renumbered revision B differ in record IDs and in the
// layouts of FaultCode and EngineStatus.
[[nodiscard]] RunResult link_two_configurations(const fs::path& work, bool c_language,
                                                const std::string& unit_body,
                                                const std::string& header,
                                                const std::string& main_source) {
    const fs::path a = work / "a";
    const fs::path b = work / "b";
    const std::string extension = c_language ? ".c" : ".cpp";
    const std::string flags = std::string(c_language ? kStrictC : kStrictCxx) + " -O0";
    std::vector<fs::path> objects;
    for (const auto& [name, output, record_id, extra] :
         {std::tuple{std::string("rev_a"), a, std::string("131073U"), std::string("0")},
          std::tuple{std::string("rev_b"), b, std::string("196609U"), std::string("1")}}) {
        std::string body = unit_body;
        for (const auto& [token, value] :
             {std::pair{std::string("NAME"), name}, std::pair{std::string("RECORD_ID"), record_id},
              std::pair{std::string("REV_B"), extra}}) {
            for (std::size_t at = body.find(token); at != std::string::npos;
                 at = body.find(token, at + value.size())) {
                body.replace(at, token.size(), value);
            }
        }
        const fs::path source = work / (name + extension);
        write_text_file(source, "#include \"" + header + "\"\n" + body);
        const fs::path object = work / (name + ".o");
        const RunResult compiled =
            run(quote(QUARRY_TEST_CXX_COMPILER) + flags + include_flags({output}) + " -c " +
                    quote(source.string()) + " -o " + quote(object.string()),
                work / (name + ".log"));
        if (compiled.status != 0) {
            return compiled;
        }
        objects.push_back(object);
    }
    const fs::path main_file = work / ("main" + extension);
    write_text_file(main_file, main_source);
    std::string link = quote(QUARRY_TEST_CXX_COMPILER) + flags + " " + quote(main_file.string());
    for (const fs::path& object : objects) {
        link += " " + quote(object.string());
    }
    // C++ objects need the C++ driver mode even when main is compiled as C.
    if (c_language) {
        link = quote(QUARRY_TEST_CXX_COMPILER) + flags + " -c " + quote(main_file.string()) +
               " -o " + quote((work / "main.o").string()) + " && " +
               quote(QUARRY_TEST_CXX_COMPILER) + " " + quote((work / "main.o").string());
        for (const fs::path& object : objects) {
            link += " " + quote(object.string());
        }
    }
    link += " " + quote(QUARRY_TEST_RUNTIME_C_LIBRARY) + " -o " + quote((work / "two").string());
    const RunResult linked = run(link, work / "link.log");
    if (linked.status != 0) {
        return linked;
    }
    return run(quote((work / "two").string()), work / "run.log");
}

constexpr std::string_view kTwoConfigurationsMain = R"(#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
int rev_a_round_trip(void);
int rev_b_round_trip(void);
#ifdef __cplusplus
}
#endif
int main(void) {
    const int a = rev_a_round_trip();
    const int b = rev_b_round_trip();
    printf("rev_a=%d rev_b=%d\n", a, b);
    return a == 0 && b == 0 ? 0 : 1;
}
)";

// §9.5 and review decision 6: revision A and renumbered revision B, linked
// into one executable from separate translation units, each encode and
// decode their own records correctly. The C-storage unit exercises the
// wrapper's std::span-based array accessors, whose template instantiations
// over the C storage types are exactly the ODR hazard decision 6 names.
TEST(ConfigurationIdentityTest, CppCStorageConfigurationsWithDifferentLayoutsLinkTogether) {
    const fs::path work = make_temp_directory("link-cpp-c-storage");
    generate(kVariants[2], rev_a(), work / "a");
    generate(kVariants[2], rev_b_renumbered(), work / "b");
    const std::string unit = R"(
#include <array>
#include <cstddef>
extern "C" int NAME_round_trip(void) {
    std::array<::fleet_engine_FaultCode_t, 3> faults{};
    for (unsigned index = 0; index < faults.size(); ++index) {
        ::fleet_engine_FaultCode_init(&faults[index]);
        faults[index].has_spn = true;
        faults[index].spn = 1000U + index;
#if REV_B
        faults[index].has_occurrences = true;
        faults[index].occurrences = static_cast<::uint8_t>(10U + index);
#endif
    }
    quarry::cxx::fleet_engine_EngineStatus status;
    if (!status.set_faults(faults)) return 1;
    status.set_rpm(1500U + REV_B);
    std::array<std::byte, 512> buffer{};
    const auto encoded = status.encode(buffer);
    if (encoded.status != QUARRY_C_STATUS_OK) return 2;
    const ::uint8_t* bytes = reinterpret_cast<const ::uint8_t*>(buffer.data());
    const ::uint32_t record_id = (static_cast<::uint32_t>(bytes[4]) << 24U) |
                                 (static_cast<::uint32_t>(bytes[5]) << 16U) |
                                 (static_cast<::uint32_t>(bytes[6]) << 8U) | bytes[7];
    if (record_id != RECORD_ID) return 3;
    quarry::cxx::fleet_engine_EngineStatus decoded;
    if (decoded.decode(std::span<const std::byte>(buffer.data(), encoded.bytes_written)).status !=
        QUARRY_C_STATUS_OK) return 4;
    const auto decoded_faults = decoded.faults();
    if (decoded_faults.size() != faults.size()) return 5;
    for (unsigned index = 0; index < decoded_faults.size(); ++index) {
        if (decoded_faults[index].spn != 1000U + index) return 6;
#if REV_B
        if (decoded_faults[index].occurrences != 10U + index) return 7;
#endif
    }
    if (decoded.rpm() != 1500U + REV_B) return 8;
    return 0;
}
)";
    const RunResult result = link_two_configurations(
        work, false, unit, "fleet/engine.generated.hpp", std::string(kTwoConfigurationsMain));
    EXPECT_EQ(result.status, 0) << result.output;
    fs::remove_all(work);
}

TEST(ConfigurationIdentityTest, CppConfigurationsWithDifferentLayoutsLinkTogether) {
    const fs::path work = make_temp_directory("link-cpp");
    generate(kVariants[1], rev_a(), work / "a");
    generate(kVariants[1], rev_b_renumbered(), work / "b");
    const std::string unit = R"(
#include <cstdint>
#include <vector>
extern "C" int NAME_round_trip(void) {
    std::vector<fleet::engine::FaultCode> faults;
    for (std::uint32_t index = 0; index < 3U; ++index) {
        fleet::engine::FaultCodeBuilder builder;
        if (!builder.set_spn(1000U + index)) return 1;
#if REV_B
        if (!builder.set_occurrences(static_cast<std::uint8_t>(10U + index))) return 1;
#endif
        faults.push_back(builder.build());
    }
    fleet::engine::EngineStatusBuilder builder;
    if (!builder.set_rpm(static_cast<std::uint16_t>(1500U + REV_B)) || !builder.set_faults(faults))
        return 1;
    const auto encoded = fleet::engine::encode(builder.build());
    if (!encoded.has_value()) return 2;
    const auto& bytes = *encoded;
    const std::uint32_t record_id = (std::to_integer<std::uint32_t>(bytes[4]) << 24U) |
                                    (std::to_integer<std::uint32_t>(bytes[5]) << 16U) |
                                    (std::to_integer<std::uint32_t>(bytes[6]) << 8U) |
                                    std::to_integer<std::uint32_t>(bytes[7]);
    if (record_id != RECORD_ID) return 3;
    const auto decoded = fleet::engine::decode_EngineStatus(bytes);
    if (!decoded.has_value() || decoded->faults() == nullptr) return 4;
    const auto& decoded_faults = *decoded->faults();
    if (decoded_faults.size() != 3U) return 5;
    for (std::uint32_t index = 0; index < 3U; ++index) {
        if (decoded_faults[index].spn() == nullptr || *decoded_faults[index].spn() != 1000U + index)
            return 6;
#if REV_B
        if (decoded_faults[index].occurrences() == nullptr ||
            *decoded_faults[index].occurrences() != 10U + index)
            return 7;
#endif
    }
    if (decoded->rpm() == nullptr || *decoded->rpm() != 1500U + REV_B) return 8;
    return 0;
}
)";
    const RunResult result = link_two_configurations(
        work, false, unit, "fleet/engine.generated.hpp", std::string(kTwoConfigurationsMain));
    EXPECT_EQ(result.status, 0) << result.output;
    fs::remove_all(work);
}

TEST(ConfigurationIdentityTest, CConfigurationsWithDifferentLayoutsLinkTogether) {
    const fs::path work = make_temp_directory("link-c");
    generate(kVariants[0], rev_a(), work / "a");
    generate(kVariants[0], rev_b_renumbered(), work / "b");
    const std::string unit = R"(
int NAME_round_trip(void);
int NAME_round_trip(void) {
    static fleet_engine_EngineStatus_t status;
    static fleet_engine_EngineStatus_decode_result_t decoded;
    static uint8_t buffer[512];
    uint32_t index;
    fleet_engine_EngineStatus_init(&status);
    status.has_rpm = true;
    status.rpm = (uint16_t)(1500U + REV_B);
    status.faults_count = 3U;
    for (index = 0; index < 3U; ++index) {
        status.faults[index].has_spn = true;
        status.faults[index].spn = 1000U + index;
    }
    {
        const fleet_engine_EngineStatus_encode_result_t encoded =
            fleet_engine_EngineStatus_encode(&status, buffer, sizeof(buffer));
        uint32_t record_id;
        if (encoded.status != QUARRY_C_STATUS_OK) return 2;
        record_id = ((uint32_t)buffer[4] << 24U) | ((uint32_t)buffer[5] << 16U) |
                    ((uint32_t)buffer[6] << 8U) | buffer[7];
        if (record_id != RECORD_ID) return 3;
        decoded = fleet_engine_EngineStatus_decode(buffer, encoded.bytes_written);
    }
    if (decoded.status != QUARRY_C_STATUS_OK || decoded.value.faults_count != 3U) return 4;
    for (index = 0; index < 3U; ++index) {
        if (decoded.value.faults[index].spn != 1000U + index) return 6;
    }
    return decoded.value.rpm == 1500U + REV_B ? 0 : 8;
}
)";
    const RunResult result = link_two_configurations(work, true, unit, "fleet/engine.generated.h",
                                                     std::string(kTwoConfigurationsMain));
    EXPECT_EQ(result.status, 0) << result.output;
    fs::remove_all(work);
}

// §9.5: two configurations in one Go binary through distinct import paths.
TEST(ConfigurationIdentityTest, GoConfigurationsCoexistUnderDistinctImportPaths) {
    if (std::string_view(QUARRY_TEST_GO).empty()) {
        GTEST_SKIP() << "Go toolchain not found at configure time";
    }
    const fs::path work = make_temp_directory("go-two");
    const fs::path module = work / "module";
    for (const auto& [name, root] : {std::pair{std::string("rev_a"), rev_a()},
                                     std::pair{std::string("rev_b"), rev_b_renumbered()}}) {
        const RunResult result = compiler(
            "--language go --go-import-prefix example.com/gen/" + name + " --output-directory " +
                quote((module / name).string()) + " " + quote(root.string()),
            work / (name + ".log"));
        ASSERT_EQ(result.status, 0) << result.output;
    }
    write_text_file(module / "main.go", R"(package main

import (
	"bytes"
	"fmt"
	"os"

	enginea "example.com/gen/rev_a/fleet/engine"
	engineb "example.com/gen/rev_b/fleet/engine"
)

func main() {
	rpm := uint16(1500)
	a := enginea.EngineStatus{Rpm: &rpm}
	b := engineb.EngineStatus{Rpm: &rpm}
	ab, errA := a.MarshalBRF()
	bb, errB := b.MarshalBRF()
	if errA != nil || errB != nil || bytes.Equal(ab, bb) {
		fmt.Println("encode", errA, errB)
		os.Exit(1)
	}
	var da enginea.EngineStatus
	var db engineb.EngineStatus
	if da.UnmarshalBRF(ab) != nil || db.UnmarshalBRF(bb) != nil || da.UnmarshalBRF(bb) == nil {
		fmt.Println("decode")
		os.Exit(2)
	}
	if enginea.QuarrySchemaID() == engineb.QuarrySchemaID() {
		os.Exit(3)
	}
	fmt.Printf("%x %x\n", enginea.QuarrySchemaID(), engineb.QuarrySchemaID())
}
)");
    std::string mod(kGoMod);
    mod.replace(mod.find("RUNTIME"), 7, (source_dir() / "runtime/go").string());
    write_text_file(module / "go.mod", mod);
    const RunResult result = run(
        "cd " + quote(module.string()) +
            " && env GOTOOLCHAIN=local GOPROXY=off GOFLAGS=-buildvcs=false GO111MODULE=on "
            "GOCACHE=" +
            quote((work / "gocache").string()) + " GOPATH=" + quote((work / "gopath").string()) +
            " " + quote(QUARRY_TEST_GO) + " run .",
        work / "go-run.log");
    EXPECT_EQ(result.status, 0) << result.output;
    EXPECT_NE(
        result.output.find(std::string(kRevASchemaId) + " " + std::string(kRevBRenumberedSchemaId)),
        std::string::npos)
        << result.output;
    fs::remove_all(work);
}

// Review decision 5: schema names that would collide with a generated
// identity name are rejected, never generated into conflicting declarations.
struct CollisionCase {
    std::string_view name;
    std::string_view language;
    std::string_view schema;
    std::string_view expected_message;
};

constexpr CollisionCase kCollisionCases[] = {
    {"cpp_record", "--language cpp", R"(namespace: fleet.engine
record: kQuarrySchemaId
version: 1
type: data
fields:
  value:
    type: u32
)",
     "is named 'kQuarrySchemaId', which generated C++ reserves for configuration and "
     "generated-code identity"},
    {"cpp_algorithm_record", "--language cpp", R"(namespace: fleet.engine
record: kQuarrySchemaIdAlgorithm
version: 1
type: data
fields:
  value:
    type: u32
)",
     "is named 'kQuarrySchemaIdAlgorithm', which generated C++ reserves"},
    {"cpp_namespace", "--language cpp", R"(namespace: fleet.quarry_cfg_0
record: Sample
version: 1
type: data
fields:
  value:
    type: u32
)",
     "is named 'quarry_cfg_0', which generated C++ reserves"},
    // Generated-code identity names (#111 G1).
    {"cpp_generated_code_id_record", "--language cpp", R"(namespace: fleet.engine
record: kQuarryGeneratedCodeId
version: 1
type: data
fields:
  value:
    type: u32
)",
     "is named 'kQuarryGeneratedCodeId', which generated C++ reserves"},
    {"cpp_generated_code_id_format_record", "--language cpp", R"(namespace: fleet.engine
record: kQuarryGeneratedCodeIdFormat
version: 1
type: data
fields:
  value:
    type: u32
)",
     "is named 'kQuarryGeneratedCodeIdFormat', which generated C++ reserves"},
    {"cpp_generated_namespace", "--language cpp", R"(namespace: fleet.quarry_gen_0
record: Sample
version: 1
type: data
fields:
  value:
    type: u32
)",
     "is named 'quarry_gen_0', which generated C++ reserves"},
    {"c_storage_generated_namespace_prefix", "--language cpp --cpp-storage c",
     R"(namespace: quarry_gen_x
record: Sample
version: 1
type: data
enums:
  Mode:
    values:
      off: 0
fields:
  value:
    type: Mode
)",
     "uses the prefix quarry_gen_, which generated C and C++/C-storage code reserves"},
    // #111 G2: plain C headers compiled as C++ use the same inline namespace.
    {"c_generated_namespace_prefix", "--language c", R"(namespace: quarry_gen_x
record: Sample
version: 1
type: data
enums:
  Mode:
    values:
      off: 0
fields:
  value:
    type: Mode
)",
     "uses the prefix quarry_gen_, which generated C and C++/C-storage code reserves"},
    // #117 moved configuration-identity macros off the uppercased symbol
    // prefix, so this enum constant no longer collides with them. The
    // coexistence is checked in PreviouslyCollidingCEnumSpellingGenerates.
    {"go_record", "--language go", R"(namespace: fleet.engine
record: QuarrySchemaID
version: 1
type: data
fields:
  value:
    type: u32
)",
     "maps to Go identifier 'QuarrySchemaID', which generated Go reserves for configuration "
     "identity"},
    {"go_enum_value", "--language go", R"(namespace: fleet.engine
record: Sample
version: 1
type: data
enums:
  QuarrySchemaID:
    values:
      Hi: 0
fields:
  value:
    type: QuarrySchemaID
)",
     "which generated Go reserves for configuration identity"},
    {"python_record_pair", "--language python", R"(namespace: fleet.engine
record: QUARRY_SCHEMA_ID
version: 1
type: data
enums:
  QUARRY_SCHEMA_ID_:
    values:
      one: 0
fields:
  value:
    type: u32
)",
     "both normalize to 'QUARRY_SCHEMA_ID_'"},
};

class ConfigurationIdentityCollisionTest : public ::testing::TestWithParam<CollisionCase> {};

INSTANTIATE_TEST_SUITE_P(ReservedNames, ConfigurationIdentityCollisionTest,
                         ::testing::ValuesIn(kCollisionCases),
                         [](const ::testing::TestParamInfo<CollisionCase>& info) {
                             return std::string(info.param.name);
                         });

TEST_P(ConfigurationIdentityCollisionTest, RejectsSchemaNamesThatCollideWithIdentityNames) {
    const CollisionCase& collision = GetParam();
    const fs::path work = make_temp_directory("collision-" + std::string(collision.name));
    const fs::path schema = work / "schema.brd";
    write_text_file(schema, collision.schema);
    const RunResult result =
        compiler(std::string(collision.language) + " --output-directory " +
                     quote((work / "out").string()) + " " + quote(schema.string()),
                 work / "compile.log");
    EXPECT_NE(result.status, 0) << result.output;
    EXPECT_NE(result.output.find(collision.expected_message), std::string::npos) << result.output;
    EXPECT_FALSE(fs::exists(work / "out")) << "no output may be written";
    fs::remove_all(work);
}

// #117: the enum constants FLEET_ENGINE_QUARRY_SCHEMA_ID_* used to be the
// configuration-identity macros. They now coexist with QUARRY_NS_fleet_engine_*.
TEST(ConfigurationIdentityTest, PreviouslyCollidingCEnumSpellingGenerates) {
    const std::string_view schema = R"(namespace: fleet.engine
record: Sample
version: 1
type: data
enums:
  QUARRY:
    values:
      SCHEMA_ID_ALGORITHM: 0
      SCHEMA_ID_W0: 1
fields:
  value:
    type: QUARRY
)";
    const std::string stem =
        std::string(quarry::compiler::support::kCNamespaceMacroPrefix) +
        quarry::compiler::support::encode_namespace_macro("fleet.engine").value();
    for (const std::string_view language : {"--language c", "--language cpp --cpp-storage c"}) {
        const bool plain_c = language == "--language c";
        const fs::path work = make_temp_directory(plain_c ? "enum-c" : "enum-cstorage");
        const fs::path schema_path = work / "schema.brd";
        write_text_file(schema_path, schema);
        const fs::path output = work / "out";
        const RunResult generated =
            compiler(std::string(language) + " --output-directory " + quote(output.string()) + " " +
                         quote(schema_path.string()),
                     work / "compile.log");
        ASSERT_EQ(generated.status, 0) << language << "\n" << generated.output;
        const fs::path header =
            output / (plain_c ? "fleet/engine.generated.h" : "fleet/engine.generated.hpp");
        const std::string text = read_text_file(header);
        EXPECT_NE(text.find("FLEET_ENGINE_QUARRY_SCHEMA_ID_ALGORITHM"), std::string::npos) << text;
        EXPECT_NE(text.find("FLEET_ENGINE_QUARRY_SCHEMA_ID_W0"), std::string::npos);
        EXPECT_NE(text.find("#define " + stem + "_SCHEMA_ID_ALGORITHM"), std::string::npos);
        EXPECT_NE(text.find("#define " + stem + "_SCHEMA_ID_W0"), std::string::npos);
        EXPECT_NE(text.find(std::string(quarry::compiler::support::kCNamespaceAccessorPrefix) +
                            "fleet_engine_schema_id"),
                  std::string::npos);
        const fs::path source = work / (plain_c ? "use.c" : "use.cpp");
        write_text_file(source, std::string("#include \"") + header.string() +
                                    "\"\nint main(void) {\n"
                                    "  return FLEET_ENGINE_QUARRY_SCHEMA_ID_ALGORITHM == 0 &&\n"
                                    "                 FLEET_ENGINE_QUARRY_SCHEMA_ID_W0 == 1\n"
                                    "             ? 0\n"
                                    "             : 1;\n}\n");
        const RunResult used =
            run(quote(QUARRY_TEST_CXX_COMPILER) + std::string(plain_c ? kStrictC : kStrictCxx) +
                    " -I" + quote(QUARRY_TEST_REPO_INCLUDE_DIR) + " -I" +
                    quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) + " " + quote(source.string()) +
                    " -o " + quote((work / "use").string()),
                work / "use.log");
        EXPECT_EQ(used.status, 0) << language << "\n" << used.output;
        if (used.status == 0) {
            const RunResult ran = run(quote((work / "use").string()), work / "run.log");
            EXPECT_EQ(ran.status, 0) << ran.output;
        }
        fs::remove_all(work);
    }
}

// The Python backend's established policy renames a schema name equal to a
// reserved module-level name by appending an underscore; the identity
// constants keep their meaning.
TEST(ConfigurationIdentityTest, PythonRenamesRecordsNamedLikeIdentityConstants) {
    if (std::string_view(QUARRY_TEST_PYTHON3).empty()) {
        GTEST_SKIP() << "python3 not found at configure time";
    }
    const fs::path work = make_temp_directory("python-rename");
    const fs::path schema = work / "schema.brd";
    write_text_file(schema, R"(namespace: fleet.engine
record: QUARRY_SCHEMA_ID
version: 1
type: data
fields:
  value:
    type: u32
)");
    const fs::path output = work / "out";
    ASSERT_EQ(compiler("--language python --output-directory " + quote(output.string()) + " " +
                           quote(schema.string()),
                       work / "compile.log")
                  .status,
              0);
    const RunResult result =
        run("PYTHONPATH=" + quote(output.string() + ":" + QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR) +
                " " + quote(QUARRY_TEST_PYTHON3) +
                " -c 'import fleet.engine.schema as m; "
                "assert isinstance(m.QUARRY_SCHEMA_ID, bytes) and len(m.QUARRY_SCHEMA_ID) == 16; "
                "assert m.QUARRY_SCHEMA_ID_(value=1).encode()'",
            work / "run.log");
    EXPECT_EQ(result.status, 0) << result.output;
    fs::remove_all(work);
}

} // namespace
