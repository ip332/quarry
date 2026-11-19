// Generated-code identity in C++ owning and C++/C-storage bindings (#111 G1).
//
// Black-box: the tests drive quarry-schema-compiler and the C++ compiler the
// project was configured with, with strict warnings. They cover the G1
// acceptance criteria: identical definitions share an identity whatever the
// output naming and --generate selection; any definition change gives a new
// identity, and a dependency's change propagates to its dependents; dependency
// presence, format and equality are checked at compile time; a second,
// different definition of a namespace in one translation unit is rejected
// explicitly; and the ODR reproduction cases of the #111 investigation link
// and run correctly from two translation units in both link orders.

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

#define QUARRY_TEST_STRICT_CXX_WARNINGS " -Wall -Wextra -Wpedantic -Wshadow -Werror"

[[nodiscard]] fs::path source_dir() { return fs::path(QUARRY_TEST_SOURCE_DIR); }
[[nodiscard]] fs::path fixtures() { return source_dir() / "tests/fixtures/configuration_identity"; }
[[nodiscard]] fs::path rev_a() { return fixtures() / "rev_a/configuration.brd"; }

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

[[nodiscard]] std::string replace_all(std::string text, std::string_view from,
                                      std::string_view to) {
    std::size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
    return text;
}

// One namespace, inv.core: a used enum, an unused enum and a record array.
constexpr std::string_view kCoreSchema = R"(namespace: inv.core
record: Status
version: 1
type: data
enums:
  Mode:
    values:
      off: 0
      on: 1
  Level:
    values:
      low: 0
      high: 1
fields:
  rpm:
    type: u16
  mode:
    type: Mode
  items:
    type: inv.core.Item[]
    max_elements: 4
imports:
  - item.brd
)";
constexpr std::string_view kItemSchema = R"(namespace: inv.core
record: Item
version: 1
type: data
fields:
  a:
    type: u32
)";

// Two namespaces: dep.engine embeds dep.common.Timestamp by value.
constexpr std::string_view kCommonSchema = R"(namespace: dep.common
record: Timestamp
version: 1
type: data
fields:
  unix_ms:
    type: u64
)";
constexpr std::string_view kEngineSchema = R"(namespace: dep.engine
record: EngineStatus
version: 1
type: data
imports:
  - common.brd
fields:
  time:
    type: dep.common.Timestamp
  rpm:
    type: u16
)";

// Language options of the two identified variants.
struct Variant {
    std::string_view name;
    std::string_view arguments;
    std::string_view extension;
};
constexpr Variant kOwning{"cpp", "--language cpp", ".generated.hpp"};
constexpr Variant kCStorage{"cpp_c_storage", "--language cpp --cpp-storage c", ".generated.hpp"};

class GeneratedCodeIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_ = fs::temp_directory_path() /
                ("quarry-gid-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                 "-" +
                 std::to_string(std::hash<std::string>{}(
                     ::testing::UnitTest::GetInstance()->current_test_info()->name())));
        fs::remove_all(work_);
        fs::create_directories(work_);
    }
    void TearDown() override {
        if (!HasFailure()) {
            fs::remove_all(work_);
        }
    }

    // Writes an inv configuration whose core.brd is `core` and returns its root.
    [[nodiscard]] fs::path inv_root(std::string_view name, std::string_view core) {
        const fs::path directory = work_ / "schemas" / std::string(name);
        write_text_file(directory / "configuration.brd",
                        "configuration: inv\nimports:\n  - core.brd\n");
        write_text_file(directory / "core.brd", core);
        write_text_file(directory / "item.brd", kItemSchema);
        return directory / "configuration.brd";
    }

    [[nodiscard]] fs::path dep_root(std::string_view name, std::string_view common,
                                    std::string_view engine) {
        const fs::path directory = work_ / "schemas" / std::string(name);
        write_text_file(directory / "configuration.brd",
                        "configuration: dep\nimports:\n  - engine.brd\n");
        write_text_file(directory / "common.brd", common);
        write_text_file(directory / "engine.brd", engine);
        return directory / "configuration.brd";
    }

    // Generates `root` into a fresh directory and returns it.
    [[nodiscard]] fs::path generate(const Variant& variant, const fs::path& root,
                                    std::string_view tag, const std::string& extra = {}) {
        const fs::path output = work_ / "gen" / std::string(tag);
        const RunResult result =
            run(quote(QUARRY_SCHEMA_COMPILER_TOOL) + " " + std::string(variant.arguments) + " " +
                    extra + " --output-directory " + quote(output.string()) + " " +
                    quote(root.string()),
                work_ / (std::string(tag) + ".log"));
        EXPECT_EQ(result.status, 0) << result.output;
        return output;
    }

    // The generated-code identity a header was given (its inline namespace).
    [[nodiscard]] static std::string gid(const fs::path& header) {
        const std::string text = read_text_file(header);
        std::smatch match;
        if (!std::regex_search(text, match,
                               std::regex("inline namespace quarry_gen_([0-9a-f]{32})"))) {
            ADD_FAILURE() << "no generated-code identity in " << header;
            return {};
        }
        return match[1].str();
    }

    [[nodiscard]] RunResult syntax_check(const std::string& source,
                                         const std::vector<fs::path>& includes,
                                         std::string_view tag) {
        const fs::path file = work_ / (std::string(tag) + ".cpp");
        write_text_file(file, source);
        std::string command = quote(QUARRY_TEST_CXX_COMPILER) +
                              " -std=c++20" QUARRY_TEST_STRICT_CXX_WARNINGS " -fsyntax-only";
        for (const fs::path& include : includes) {
            command += " -I" + quote(include.string());
        }
        command += " -I" + quote(QUARRY_TEST_REPO_INCLUDE_DIR) + " -I" +
                   quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) + " " + quote(file.string());
        return run(command, work_ / (std::string(tag) + ".log"));
    }

    // Links two translation units (`a` from tu_a with includes_a, `b` from tu_b
    // with includes_b) and runs the program; returns its "run_a=.. run_b=.." line.
    [[nodiscard]] std::string link_and_run(const std::string& tu_a, const fs::path& includes_a,
                                           const std::string& tu_b, const fs::path& includes_b,
                                           const std::string& flags, std::string_view tag) {
        const fs::path directory = work_ / "link" / std::string(tag);
        fs::create_directories(directory);
        write_text_file(directory / "main.cpp",
                        "#include <cstdio>\nextern \"C\" int run_a();\nextern \"C\" int run_b();\n"
                        "int main() { const int a = run_a(); const int b = run_b();\n"
                        "  std::printf(\"run_a=%d run_b=%d\\n\", a, b); return 0; }\n");
        write_text_file(directory / "a.cpp", tu_a);
        write_text_file(directory / "b.cpp", tu_b);
        const std::string compiler = quote(QUARRY_TEST_CXX_COMPILER) + " -std=c++20 " + flags +
                                     QUARRY_TEST_STRICT_CXX_WARNINGS;
        const std::string common = " -I" + quote(QUARRY_TEST_REPO_INCLUDE_DIR) + " -I" +
                                   quote(QUARRY_TEST_GENERATED_INCLUDE_DIR);
        for (const auto& [name, includes] :
             std::vector<std::pair<std::string, fs::path>>{{"a", includes_a}, {"b", includes_b}}) {
            const RunResult compiled =
                run(compiler + " -I" + quote(includes.string()) + common + " -c " +
                        quote((directory / (name + ".cpp")).string()) + " -o " +
                        quote((directory / (name + ".o")).string()),
                    directory / (name + ".log"));
            if (compiled.status != 0) {
                ADD_FAILURE() << compiled.output;
                return {};
            }
        }
        const fs::path program = directory / "program";
        const RunResult linked = run(
            compiler + " " + quote((directory / "main.cpp").string()) + " " +
                quote((directory / "a.o").string()) + " " + quote((directory / "b.o").string()) +
                " " + quote(QUARRY_TEST_RUNTIME_C_LIBRARY) + " -o " + quote(program.string()),
            directory / "link.log");
        if (linked.status != 0) {
            ADD_FAILURE() << linked.output;
            return {};
        }
        const RunResult ran = run(quote(program.string()), directory / "run.log");
        EXPECT_EQ(ran.status, 0) << ran.output;
        std::smatch match;
        if (!std::regex_search(ran.output, match, std::regex("run_a=-?[0-9]+ run_b=-?[0-9]+"))) {
            ADD_FAILURE() << ran.output;
            return {};
        }
        return match[0].str();
    }

    [[nodiscard]] bool sanitizer_available() {
        static const bool available = [&] {
            const fs::path probe = work_ / "asan-probe.cpp";
            write_text_file(probe, "int main() { return 0; }\n");
            return run(quote(QUARRY_TEST_CXX_COMPILER) + " -fsanitize=address " +
                           quote(probe.string()) + " -o " + quote((work_ / "asan-probe").string()),
                       work_ / "asan-probe.log")
                       .status == 0;
        }();
        return available;
    }

    [[nodiscard]] std::vector<std::string> link_flag_sets() {
        std::vector<std::string> flags{"-O0", "-O2"};
        if (sanitizer_available()) {
            flags.emplace_back("-O0 -g -fsanitize=address");
        }
        return flags;
    }

    fs::path work_;
};

// --- identity determinism --------------------------------------------------

TEST_F(GeneratedCodeIdentityTest, OutputNamingAndSelectionDoNotChangeIdentities) {
    for (const Variant* variant : {&kOwning, &kCStorage}) {
        const std::string name(variant->name);
        const fs::path all = generate(*variant, rev_a(), name + "-all");
        const fs::path again = generate(*variant, rev_a(), name + "-again");
        const fs::path common =
            generate(*variant, rev_a(), name + "-common", "--generate fleet.common");
        const fs::path engine =
            generate(*variant, rev_a(), name + "-engine", "--generate fleet.engine");
        const fs::path renamed =
            generate(*variant, rev_a(), name + "-renamed", "--root-file-stem other");
        for (const std::string_view ns : {"common", "engine"}) {
            const std::string file = "fleet/" + std::string(ns) + ".generated.hpp";
            const std::string expected = gid(all / file);
            EXPECT_EQ(gid(again / file), expected) << name << " " << ns;
            EXPECT_EQ(gid(renamed / file), expected) << name << " " << ns;
            EXPECT_EQ(gid((ns == "common" ? common : engine) / file), expected)
                << name << " " << ns << ": --generate must not change the identity";
        }
        EXPECT_EQ(read_text_file(all / "fleet/common.generated.hpp"),
                  read_text_file(common / "fleet/common.generated.hpp"))
            << name;
    }
    // --file-extension (C++ owning only): the file names and include paths
    // change, the identities do not.
    const fs::path base = generate(kOwning, rev_a(), "ext-base");
    const fs::path ext = generate(kOwning, rev_a(), "ext-hh", "--file-extension .gen.hh");
    EXPECT_EQ(gid(ext / "fleet/engine.gen.hh"), gid(base / "fleet/engine.generated.hpp"));
    EXPECT_EQ(gid(ext / "fleet/common.gen.hh"), gid(base / "fleet/common.generated.hpp"));
    EXPECT_NE(read_text_file(ext / "fleet/engine.gen.hh").find("#include \"fleet/common.gen.hh\""),
              std::string::npos);
}

TEST_F(GeneratedCodeIdentityTest, DefinitionChangesChangeIdentitiesButNotSchemaId) {
    const fs::path base = inv_root("base", kCoreSchema);
    const std::map<std::string, fs::path> variants = {
        {"field name",
         inv_root("field", replace_all(std::string(kCoreSchema), "  rpm:", "  speed:"))},
        {"enum member names",
         inv_root("enum", replace_all(replace_all(std::string(kCoreSchema), "      off: 0",
                                                  "      idle: 0"),
                                      "      on: 1", "      run: 1"))},
        {"unused enum values",
         inv_root("unused",
                  replace_all(replace_all(std::string(kCoreSchema), "      low: 0", "      low: 2"),
                              "      high: 1", "      high: 0"))},
    };
    const std::string base_schema_id =
        run(quote(QUARRY_SCHEMA_COMPILER_TOOL) + " --print-schema-id " + quote(base.string()),
            work_ / "sid-base.log")
            .output;
    for (const Variant* variant : {&kOwning, &kCStorage}) {
        const std::string name(variant->name);
        const std::string base_gid =
            gid(generate(*variant, base, name + "-base") / "inv/core.generated.hpp");
        for (const auto& [label, root] : variants) {
            EXPECT_EQ(run(quote(QUARRY_SCHEMA_COMPILER_TOOL) + " --print-schema-id " +
                              quote(root.string()),
                          work_ / "sid.log")
                          .output,
                      base_schema_id)
                << label << " must not change the wire identity";
            EXPECT_NE(gid(generate(*variant, root, name + "-" + label) / "inv/core.generated.hpp"),
                      base_gid)
                << name << ": " << label;
        }
    }
    // C++/C storage options change the C++/C-storage definitions.
    EXPECT_NE(gid(generate(kCStorage, base, "cs-default") / "inv/core.generated.hpp"),
              gid(generate(kCStorage, base, "cs-optimized", "--c-optimize-size") /
                  "inv/core.generated.hpp"));
    // The two variants of one namespace never share an identity.
    EXPECT_NE(gid(generate(kOwning, base, "owning") / "inv/core.generated.hpp"),
              gid(generate(kCStorage, base, "cs") / "inv/core.generated.hpp"));
}

TEST_F(GeneratedCodeIdentityTest, DependencyIdentityChangesPropagateToDependents) {
    const fs::path base = dep_root("base", kCommonSchema, kEngineSchema);
    const fs::path common_changed = dep_root(
        "common", replace_all(std::string(kCommonSchema), "unix_ms", "millis"), kEngineSchema);
    const fs::path engine_changed = dep_root(
        "engine", kCommonSchema, replace_all(std::string(kEngineSchema), "  rpm:", "  speed:"));
    for (const Variant* variant : {&kOwning, &kCStorage}) {
        const std::string name(variant->name);
        const fs::path a = generate(*variant, base, name + "-base");
        const fs::path b = generate(*variant, common_changed, name + "-common");
        const fs::path c = generate(*variant, engine_changed, name + "-engine");
        // The engine header's own text is identical in a and b; only its
        // dependency changed.
        EXPECT_NE(gid(b / "dep/common.generated.hpp"), gid(a / "dep/common.generated.hpp")) << name;
        EXPECT_NE(gid(b / "dep/engine.generated.hpp"), gid(a / "dep/engine.generated.hpp"))
            << name << ": a dependency's identity must propagate";
        // A change in the dependent leaves the dependency's identity alone.
        EXPECT_EQ(gid(c / "dep/common.generated.hpp"), gid(a / "dep/common.generated.hpp")) << name;
        EXPECT_NE(gid(c / "dep/engine.generated.hpp"), gid(a / "dep/engine.generated.hpp")) << name;
    }
}

TEST_F(GeneratedCodeIdentityTest, IdentityMetadataMatchesTheNamespaceName) {
    const fs::path output = generate(kOwning, rev_a(), "meta");
    const std::string id = gid(output / "fleet/engine.generated.hpp");
    const std::string source =
        "#include \"fleet/engine.generated.hpp\"\n"
        "static_assert(fleet::engine::kQuarryGeneratedCodeIdFormat == 1U);\n"
        "static_assert(QUARRY_GENERATED_CPP_NS_FLEET_ENGINE_CODE_ID_FORMAT == 1U);\n"
        "static_assert(fleet::engine::kQuarryGeneratedCodeId[0] == 0x" +
        id.substr(0, 2) +
        "U);\n"
        "static_assert(fleet::engine::kQuarryGeneratedCodeId[15] == 0x" +
        id.substr(30, 2) +
        "U);\n"
        "static_assert(QUARRY_GENERATED_CPP_NS_FLEET_ENGINE_CODE_ID_W0 == 0x" +
        id.substr(0, 8) +
        "UL);\n"
        "static_assert(QUARRY_GENERATED_CPP_NS_FLEET_ENGINE_CODE_ID_W3 == 0x" +
        id.substr(24, 8) +
        "UL);\n"
        "static_assert(&fleet::engine::kQuarryGeneratedCodeId == "
        "&fleet::engine::quarry_gen_" +
        id + "::kQuarryGeneratedCodeId);\n";
    const RunResult result = syntax_check(source, {output}, "meta");
    EXPECT_EQ(result.status, 0) << result.output;
}

// --- dependency checks --------------------------------------------------------

TEST_F(GeneratedCodeIdentityTest, RejectsDependenciesWithOtherDefinitions) {
    // Same configuration (field names are not wire identity), different
    // generated definitions of dep.common: the A4.2 checks pass, the
    // generated-code identity check does not.
    const fs::path base = dep_root("base", kCommonSchema, kEngineSchema);
    const fs::path renamed = dep_root(
        "renamed", replace_all(std::string(kCommonSchema), "unix_ms", "millis"), kEngineSchema);
    for (const Variant* variant : {&kOwning, &kCStorage}) {
        const std::string name(variant->name);
        const fs::path engine = generate(*variant, base, name + "-base");
        const fs::path common = generate(*variant, renamed, name + "-renamed");
        const fs::path mixed = work_ / ("mixed-" + name);
        fs::create_directories(mixed / "dep");
        fs::copy_file(engine / "dep/engine.generated.hpp", mixed / "dep/engine.generated.hpp");
        fs::copy_file(common / "dep/common.generated.hpp", mixed / "dep/common.generated.hpp");
        const RunResult result =
            syntax_check("#include \"dep/engine.generated.hpp\"\n", {mixed}, "mixed-" + name);
        EXPECT_NE(result.status, 0) << name;
        EXPECT_NE(result.output.find("dep/common.generated.hpp contains different generated "
                                     "definitions than dep/engine.generated.hpp was generated "
                                     "against"),
                  std::string::npos)
            << name << "\n"
            << result.output;
        EXPECT_EQ(result.output.find("different Quarry configuration"), std::string::npos)
            << name << ": the configuration identity still matches";
    }
}

TEST_F(GeneratedCodeIdentityTest, RejectsDependenciesThatPredateGeneratedCodeIdentity) {
    for (const Variant* variant : {&kOwning, &kCStorage}) {
        const std::string name(variant->name);
        const fs::path output = generate(*variant, rev_a(), name, "--generate fleet.engine");
        fs::copy_file(fixtures() / "pre_generated_code_identity" / std::string(name) /
                          "fleet/common.generated.hpp.pre_generated_code_identity",
                      output / "fleet/common.generated.hpp");
        const RunResult result =
            syntax_check("#include \"fleet/engine.generated.hpp\"\n", {output}, "pre-" + name);
        EXPECT_NE(result.status, 0) << name;
        EXPECT_NE(result.output.find("fleet/common.generated.hpp predates generated-code identity"),
                  std::string::npos)
            << name << "\n"
            << result.output;
    }
}

TEST_F(GeneratedCodeIdentityTest, RejectsDependenciesWithAnotherIdentityFormat) {
    for (const Variant* variant : {&kOwning, &kCStorage}) {
        const std::string name(variant->name);
        const fs::path output = generate(*variant, rev_a(), name + "-format");
        const fs::path header = output / "fleet/common.generated.hpp";
        const std::string text = read_text_file(header);
        const std::string macro =
            (variant == &kOwning ? std::string("QUARRY_GENERATED_CPP_NS_")
                                 : std::string("QUARRY_GENERATED_CPP_C_NS_")) +
            "FLEET_COMMON_CODE_ID_FORMAT";
        const std::string edited =
            replace_all(text, "#define " + macro + " 1U", "#define " + macro + " 2U");
        ASSERT_NE(edited, text) << name;
        write_text_file(header, edited);
        const RunResult result =
            syntax_check("#include \"fleet/engine.generated.hpp\"\n", {output}, "format-" + name);
        EXPECT_NE(result.status, 0) << name;
        EXPECT_NE(result.output.find("fleet/common.generated.hpp uses a different generated-code "
                                     "identity format than fleet/engine.generated.hpp"),
                  std::string::npos)
            << name << "\n"
            << result.output;
    }
}

// --- same translation unit ---------------------------------------------------

TEST_F(GeneratedCodeIdentityTest, RejectsTwoDefinitionsOfANamespaceInOneTranslationUnit) {
    const fs::path base = inv_root("base", kCoreSchema);
    const fs::path renamed =
        inv_root("renamed", replace_all(std::string(kCoreSchema), "  rpm:", "  speed:"));
    for (const Variant* variant : {&kOwning, &kCStorage}) {
        const std::string name(variant->name);
        const fs::path first = generate(*variant, base, name + "-first");
        const fs::path second = generate(*variant, renamed, name + "-second");
        fs::create_directories(second / "other");
        fs::rename(second / "inv/core.generated.hpp", second / "other/core.generated.hpp");
        const RunResult result = syntax_check(
            "#include \"inv/core.generated.hpp\"\n#include \"other/core.generated.hpp\"\n",
            {first, second}, "same-tu-" + name);
        EXPECT_NE(result.status, 0) << name;
        EXPECT_NE(result.output.find("this translation unit already includes a different "
                                     "generated definition of namespace inv.core"),
                  std::string::npos)
            << name << "\n"
            << result.output;

        // Identical definitions reached through two paths are deduplicated.
        const fs::path copy = generate(*variant, base, name + "-copy");
        fs::create_directories(copy / "other");
        fs::rename(copy / "inv/core.generated.hpp", copy / "other/core.generated.hpp");
        const RunResult same = syntax_check(
            "#include \"inv/core.generated.hpp\"\n#include \"other/core.generated.hpp\"\n",
            {first, copy}, "same-copy-" + name);
        EXPECT_EQ(same.status, 0) << name << "\n" << same.output;
    }
}

// --- ODR reproduction cases (#111 investigation) ----------------------------

constexpr std::string_view kOwningTu = R"(#include "inv/core.generated.hpp"
#include <cstdint>
#include <vector>
extern "C" int run_NAME() {
    inv::core::StatusBuilder b;
    if (!b.set_FIELD(1234U)) return 1;
    std::vector<inv::core::Item> items;
    for (std::uint32_t i = 0; i < 3U; ++i) {
        inv::core::ItemBuilder ib;
        if (!ib.set_a(7U + i)) return 1;
        items.push_back(ib.build());
    }
    if (!b.set_items(items)) return 1;
    const auto bytes = inv::core::encode(b.build());
    if (!bytes) return 2;
    const auto d = inv::core::decode_Status(*bytes);
    if (!d || !d->FIELD() || *d->FIELD() != 1234U) return 3;
    if (!d->items() || d->items()->size() != 3U || *(*d->items())[2].a() != 9U) return 5;
    return static_cast<int>(inv::core::Level::high) == LEVEL_HIGH ? 0 : 6;
}
)";

constexpr std::string_view kCStorageTu = R"(#include "inv/core.generated.hpp"
#include <array>
#include <cstddef>
#include <span>
extern "C" int run_NAME() {
    std::array<::inv_core_Item_t, 3> items{};
    for (unsigned i = 0; i < items.size(); ++i) {
        quarry::cxx::inv_core_Item w;
        w.set_a(7U + i);
        items[i] = w.native();
    }
    quarry::cxx::inv_core_Status s;
    s.set_rpm(1234U);
    if (!s.set_items(items)) return 1;
    std::array<std::byte, 256> buffer{};
    const auto encoded = s.encode(buffer);
    if (encoded.status != QUARRY_C_STATUS_OK) return 2;
    quarry::cxx::inv_core_Status d;
    if (d.decode(std::span<const std::byte>(buffer.data(), encoded.bytes_written)).status !=
        QUARRY_C_STATUS_OK) return 3;
    if (!d.has_rpm() || d.rpm() != 1234U) return 4;
    if (d.native().items_count != 3U) return 7; // this TU's own struct layout
    const auto got = d.items();
    if (got.size() != 3U) return 5;
    for (unsigned i = 0; i < got.size(); ++i) {
        quarry::cxx::inv_core_Item w;
        w.native() = got[i];
        if (!w.has_a() || w.a() != 7U + i) return 6;
    }
    return 0;
}
)";

[[nodiscard]] std::string tu(std::string_view text, std::string_view name, std::string_view field,
                             std::string_view level_high = "1") {
    return replace_all(replace_all(replace_all(std::string(text), "NAME", name), "FIELD", field),
                       "LEVEL_HIGH", level_high);
}

TEST_F(GeneratedCodeIdentityTest, DefaultAndSizeOptimizedCStorageCoexistInOneProgram) {
    // Formerly a 48-byte write into a 44-byte object at -O0 (#111 case 3).
    const fs::path root = inv_root("base", kCoreSchema);
    const fs::path standard = generate(kCStorage, root, "default");
    const fs::path compact = generate(kCStorage, root, "compact", "--c-optimize-size");
    ASSERT_NE(gid(standard / "inv/core.generated.hpp"), gid(compact / "inv/core.generated.hpp"));
    for (const std::string& flags : link_flag_sets()) {
        EXPECT_EQ(link_and_run(tu(kCStorageTu, "a", "rpm"), standard, tu(kCStorageTu, "b", "rpm"),
                               compact, flags, "cs-ab" + std::to_string(flags.size())),
                  "run_a=0 run_b=0")
            << flags;
        EXPECT_EQ(link_and_run(tu(kCStorageTu, "a", "rpm"), compact, tu(kCStorageTu, "b", "rpm"),
                               standard, flags, "cs-ba" + std::to_string(flags.size())),
                  "run_a=0 run_b=0")
            << flags;
    }
}

TEST_F(GeneratedCodeIdentityTest, WireEquivalentDefinitionsCoexistInOneProgram) {
    // #111 cases 1 and 6: same schema_id, different definitions.
    const fs::path base = generate(kOwning, inv_root("base", kCoreSchema), "base");
    const fs::path renamed = generate(
        kOwning, inv_root("renamed", replace_all(std::string(kCoreSchema), "  rpm:", "  speed:")),
        "renamed");
    const fs::path unused = generate(
        kOwning,
        inv_root("unused",
                 replace_all(replace_all(std::string(kCoreSchema), "      low: 0", "      low: 2"),
                             "      high: 1", "      high: 0")),
        "unused");
    for (const std::string& flags : link_flag_sets()) {
        const std::string tag = std::to_string(flags.size());
        EXPECT_EQ(link_and_run(tu(kOwningTu, "a", "rpm"), base, tu(kOwningTu, "b", "speed"),
                               renamed, flags, "names-ab" + tag),
                  "run_a=0 run_b=0")
            << flags;
        EXPECT_EQ(link_and_run(tu(kOwningTu, "a", "speed"), renamed, tu(kOwningTu, "b", "rpm"),
                               base, flags, "names-ba" + tag),
                  "run_a=0 run_b=0")
            << flags;
        // Each translation unit sees its own values of the unused enum.
        EXPECT_EQ(link_and_run(tu(kOwningTu, "a", "rpm", "1"), base, tu(kOwningTu, "b", "rpm", "0"),
                               unused, flags, "unused-ab" + tag),
                  "run_a=0 run_b=0")
            << flags;
        EXPECT_EQ(link_and_run(tu(kOwningTu, "a", "rpm", "0"), unused,
                               tu(kOwningTu, "b", "rpm", "1"), base, flags, "unused-ba" + tag),
                  "run_a=0 run_b=0")
            << flags;
    }
}

} // namespace
