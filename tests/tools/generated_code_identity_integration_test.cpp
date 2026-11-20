// Fast/extended G4 commands and coverage limits: docs/generated-code-identity-validation.md.
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

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <process.h>
#endif

#include "compiler/support/namespace_macro.hpp"

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;
namespace support = quarry::compiler::support;

#define QUARRY_TEST_STRICT_CXX_WARNINGS " -Wall -Wextra -Wpedantic -Wshadow -Werror"

// The extended driver overrides child compilers/flags; default CTest behavior is unchanged.
[[nodiscard]] std::string identity_generator() {
    const char* value = std::getenv("QUARRY_IDENTITY_GENERATOR_A");
    return value == nullptr ? QUARRY_SCHEMA_COMPILER_TOOL : value;
}
[[nodiscard]] std::string identity_cxx() {
    const char* value = std::getenv("QUARRY_IDENTITY_CXX");
    return value == nullptr ? QUARRY_TEST_CXX_COMPILER : value;
}
[[nodiscard]] std::string identity_cc() {
    const char* value = std::getenv("QUARRY_IDENTITY_CC");
    return value == nullptr ? identity_cxx() : value;
}
[[nodiscard]] std::string matrix_flags() {
    const char* value = std::getenv("QUARRY_IDENTITY_FLAGS");
    return value == nullptr ? "-O0" : value;
}

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
constexpr Variant kPlainC{"c", "--language c", ".generated.h"};

// AddressSanitizer and UndefinedBehaviorSanitizer, with undefined behavior fatal.
constexpr std::string_view kSanitizerFlags =
    "-fsanitize=address,undefined -fno-sanitize-recover=undefined";

class GeneratedCodeIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifdef _WIN32
        const auto process_id = ::_getpid();
#else
        const auto process_id = ::getpid();
#endif
        work_ = fs::temp_directory_path() /
                ("quarry-gid-" + std::to_string(process_id) + "-" +
                 std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
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
            run(quote(identity_generator()) + " " + std::string(variant.arguments) + " " + extra +
                    " --output-directory " + quote(output.string()) + " " + quote(root.string()),
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
                                         std::string_view tag, bool plain_c = false) {
        const fs::path file = work_ / (std::string(tag) + (plain_c ? ".c" : ".cpp"));
        write_text_file(file, source);
        std::string command = quote(identity_cxx());
        if (plain_c) {
            command += " -x c -std=c99 -Wall -Wextra -Wpedantic -Werror -fsyntax-only";
        } else {
            command += " -std=c++20" QUARRY_TEST_STRICT_CXX_WARNINGS " -fsyntax-only";
        }
        for (const fs::path& include : includes) {
            command += " -I" + quote(include.string());
        }
        command += " -I" + quote(QUARRY_TEST_REPO_INCLUDE_DIR) + " -I" +
                   quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) + " " + quote(file.string());
        return run(command, work_ / (std::string(tag) + ".log"));
    }

    // Links two translation units (`a` from tu_a with includes_a, `b` from tu_b
    // with includes_b) and runs the program; returns its "run_a=.. run_b=.." line.
    // With `c_sources`, `a` and `b` are compiled as C99 (main stays C++).
    [[nodiscard]] std::string link_and_run(const std::string& tu_a, const fs::path& includes_a,
                                           const std::string& tu_b, const fs::path& includes_b,
                                           const std::string& flags, std::string_view tag,
                                           bool c_sources = false, bool mixed_sources = false,
                                           bool reverse_link_order = false,
                                           bool expect_success = true) {
        const fs::path directory = work_ / "link" / std::string(tag);
        fs::create_directories(directory);
        write_text_file(
            directory / "main.cpp",
            "#include <cstdio>\nextern \"C\" int run_a();\nextern \"C\" int run_b();\n"
            "int main() { const int a = run_a(); const int b = run_b();\n"
            "  std::printf(\"run_a=%d run_b=%d\\n\", a, b); return a == 0 && b == 0 ? 0 : 1; }\n");
        const std::string extension = (c_sources || mixed_sources) ? ".c" : ".cpp";
        const std::string b_extension = mixed_sources ? ".cpp" : extension;
        write_text_file(directory / ("a" + extension), tu_a);
        write_text_file(directory / ("b" + b_extension), tu_b);
        const std::string compiler =
            quote(identity_cxx()) + " -std=c++20 " + flags + QUARRY_TEST_STRICT_CXX_WARNINGS;
        const std::string c_compiler = quote(identity_cc()) + " -x c -std=c99 " +
                                       replace_all(flags, "-Wodr", "") +
                                       " -Wall -Wextra -Wpedantic -Wshadow -Werror";
        const std::string common = " -I" + quote(QUARRY_TEST_REPO_INCLUDE_DIR) + " -I" +
                                   quote(QUARRY_TEST_GENERATED_INCLUDE_DIR);
        for (const auto& [name, includes] :
             std::vector<std::pair<std::string, fs::path>>{{"a", includes_a}, {"b", includes_b}}) {
            const bool compile_c = c_sources || (mixed_sources && name == "a");
            const std::string& tu_compiler = compile_c ? c_compiler : compiler;
            const std::string& suffix = name == "a" ? extension : b_extension;
            const RunResult compiled =
                run(tu_compiler + " -I" + quote(includes.string()) + common + " -c " +
                        quote((directory / (name + suffix)).string()) + " -o " +
                        quote((directory / (name + ".o")).string()),
                    directory / (name + ".log"));
            if (compiled.status != 0) {
                ADD_FAILURE() << compiled.output;
                return {};
            }
        }
        const fs::path program = directory / "program";
        const RunResult linked =
            run(compiler + " " + quote((directory / "main.cpp").string()) + " " +
                    quote((directory / (reverse_link_order ? "b.o" : "a.o")).string()) + " " +
                    quote((directory / (reverse_link_order ? "a.o" : "b.o")).string()) + " " +
                    quote(QUARRY_TEST_RUNTIME_C_LIBRARY) + " -o " + quote(program.string()),
                directory / "link.log");
        if (linked.status != 0) {
            ADD_FAILURE() << linked.output;
            return {};
        }
        const RunResult ran = run(quote(program.string()), directory / "run.log");
        if (expect_success) {
            EXPECT_EQ(ran.status, 0) << ran.output;
        }
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
            return run(quote(identity_cxx()) + " " + std::string(kSanitizerFlags) + " " +
                           quote(probe.string()) + " -o " + quote((work_ / "asan-probe").string()),
                       work_ / "asan-probe.log")
                       .status == 0;
        }();
        return available;
    }

    [[nodiscard]] std::vector<std::string> link_flag_sets() {
        if (std::getenv("QUARRY_IDENTITY_FLAGS") != nullptr)
            return {matrix_flags()};
        std::vector<std::string> flags{"-O0", "-O2"};
        if (sanitizer_available()) {
            flags.emplace_back("-O0 -g " + std::string(kSanitizerFlags));
        }
        return flags;
    }

    fs::path work_;
};

// G3: selection changes output emission only, including for mixed storage.
TEST_F(GeneratedCodeIdentityTest, PerNamespaceStorageSupportsSeparateInvocations) {
    const fs::path root = fixtures() / "mixed_storage/configuration.brd";
    for (const Variant* variant : {&kPlainC, &kCStorage}) {
        const std::string tag(variant->name);
        const std::string policy = "--c-optimize-size=fleet.common";
        const std::string common = "fleet/common" + std::string(variant->extension);
        const std::string engine = "fleet/engine" + std::string(variant->extension);
        const fs::path all = generate(*variant, root, tag + "-all", policy);
        const fs::path defaults = generate(*variant, root, tag + "-default");
        const fs::path compact = generate(*variant, root, tag + "-compact", "--c-optimize-size");
        const fs::path explicit_all = generate(*variant, root, tag + "-explicit-all",
                                               "--c-optimize-size=fleet.engine,fleet.common");
        const fs::path reordered = generate(*variant, root, tag + "-reordered",
                                            "--c-optimize-size=fleet.common,fleet.engine");
        const fs::path container =
            generate(*variant, root, tag + "-container", "--c-optimize-size=fleet");
        EXPECT_EQ(read_text_file(defaults / common), read_text_file(container / common));
        EXPECT_EQ(read_text_file(defaults / engine), read_text_file(container / engine));
        EXPECT_EQ(read_text_file(reordered / common), read_text_file(explicit_all / common));
        EXPECT_EQ(read_text_file(reordered / engine), read_text_file(explicit_all / engine));
        EXPECT_EQ(read_text_file(compact / common), read_text_file(explicit_all / common));
        EXPECT_EQ(read_text_file(compact / engine), read_text_file(explicit_all / engine));
        EXPECT_EQ(gid(all / common), gid(compact / common));
        EXPECT_NE(gid(all / common), gid(defaults / common));
        // Only the dependency's policy changed; its dependent must be re-identified too.
        EXPECT_NE(gid(all / engine), gid(defaults / engine));
        EXPECT_EQ(read_text_file(all / engine).find("uint8_t quarry_presence_bits"),
                  std::string::npos);
        EXPECT_NE(read_text_file(all / common).find("uint8_t quarry_presence_bits"),
                  std::string::npos);
        const fs::path renamed =
            generate(*variant, root, tag + "-renamed", policy + " --root-file-stem other");
        EXPECT_EQ(gid(all / engine), gid(renamed / engine));
        for (const bool dependency_first : {false, true}) {
            const fs::path separate = work_ / "gen" / (tag + (dependency_first ? "-ab" : "-ba"));
            for (const std::string& ns :
                 dependency_first ? std::vector<std::string>{"fleet.common", "fleet.engine"}
                                  : std::vector<std::string>{"fleet.engine", "fleet.common"}) {
                const auto result =
                    run(quote(identity_generator()) + " " + std::string(variant->arguments) + " " +
                            policy + " --generate " + ns + " -o " + quote(separate.string()) + " " +
                            quote(root.string()),
                        work_ / "separate.log");
                ASSERT_EQ(result.status, 0) << result.output;
            }
            EXPECT_EQ(read_text_file(all / common), read_text_file(separate / common));
            EXPECT_EQ(read_text_file(all / engine), read_text_file(separate / engine));
            for (const bool common_first : {false, true}) {
                const std::string source =
                    common_first ? "#include \"" + common + "\"\n#include \"" + engine + "\"\n"
                                 : "#include \"" + engine + "\"\n#include \"" + common + "\"\n";
                const auto checked = syntax_check(source, {separate}, tag + "-mixed");
                EXPECT_EQ(checked.status, 0) << checked.output;
                if (variant == &kPlainC) {
                    const auto checked_c = syntax_check(source, {separate}, tag + "-mixed-c", true);
                    EXPECT_EQ(checked_c.status, 0) << checked_c.output;
                }
            }
        }
        // Undeclared policy fails in either direction, before codec compilation.
        for (const bool wrong_dependency : {false, true}) {
            const fs::path mixed = work_ / "gen" / (tag + "-wrong");
            write_text_file(mixed / engine,
                            read_text_file((wrong_dependency ? all : defaults) / engine));
            write_text_file(mixed / common,
                            read_text_file((wrong_dependency ? defaults : all) / common));
            for (const bool plain_c : {false, true}) {
                if (plain_c && variant != &kPlainC)
                    continue;
                const auto rejected = syntax_check("#include \"" + engine + "\"\n", {mixed},
                                                   tag + "-reject", plain_c);
                EXPECT_NE(rejected.status, 0);
                EXPECT_NE(rejected.output.find("contains different generated definitions"),
                          std::string::npos)
                    << rejected.output;
            }
        }
        const fs::path separate =
            generate(*variant, root, tag + "-runtime-engine", policy + " --generate fleet.engine");
        const fs::path dependency =
            generate(*variant, root, tag + "-runtime-common", policy + " --generate fleet.common");
        write_text_file(separate / common, read_text_file(dependency / common));
        const std::string workload = "#include \"" + engine + "\"\n" + R"(
#ifdef __cplusplus
extern "C"
#endif
int run_NAME(void) {
    fleet_engine_EngineStatus_t s;
    fleet_engine_EngineStatus_init(&s);
    s.rpm = 1234U;
    s.has_rpm = true;
    s.time.unix_ms = 987654U;
    fleet_common_Timestamp_set_unix_ms_present(&s.time, true);
    s.has_time = true;
    s.faults_count = 1U;
    fleet_engine_FaultCode_init(&s.faults[0]);
    s.faults[0].spn = 17U;
    s.faults[0].has_spn = true;
    s.mode = FLEET_COMMON_MODE_ACTIVE;
    s.has_mode = true;
    uint8_t bytes[256];
    const fleet_engine_EngineStatus_encode_result_t e =
        fleet_engine_EngineStatus_encode(&s, bytes, sizeof bytes);
    if (e.status != QUARRY_C_STATUS_OK) return 1;
    const fleet_engine_EngineStatus_decode_result_t d =
        fleet_engine_EngineStatus_decode(bytes, e.bytes_written);
    if (d.status != QUARRY_C_STATUS_OK) return 2;
    if (!d.value.has_rpm || d.value.rpm != 1234U || !d.value.has_time ||
        !fleet_common_Timestamp_has_unix_ms(&d.value.time) ||
        d.value.time.unix_ms != 987654U || d.value.faults_count != 1U ||
        d.value.faults[0].spn != 17U || !d.value.has_mode ||
        d.value.mode != FLEET_COMMON_MODE_ACTIVE) return 3;
    return 0;
}
)";
        for (const std::string& flags : link_flag_sets()) {
            for (const bool reverse : {false, true}) {
                const auto& a = reverse ? separate : all;
                const auto& b = reverse ? all : separate;
                for (const bool c_sources : {false, true}) {
                    if (c_sources && variant != &kPlainC)
                        continue;
                    EXPECT_EQ(link_and_run(replace_all(workload, "NAME", "a"), a,
                                           replace_all(workload, "NAME", "b"), b, flags,
                                           tag + "-mixed-link", c_sources),
                              "run_a=0 run_b=0");
                }
            }
        }
    }
}

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
        run(quote(identity_generator()) + " --print-schema-id " + quote(base.string()),
            work_ / "sid-base.log")
            .output;
    for (const Variant* variant : {&kOwning, &kCStorage}) {
        const std::string name(variant->name);
        const std::string base_gid =
            gid(generate(*variant, base, name + "-base") / "inv/core.generated.hpp");
        for (const auto& [label, root] : variants) {
            EXPECT_EQ(
                run(quote(identity_generator()) + " --print-schema-id " + quote(root.string()),
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
    const std::string code_id = std::string(support::kCppOwningNamespaceMacroPrefix) +
                                support::encode_namespace_macro("fleet.engine").value();
    const std::string source = "#include \"fleet/engine.generated.hpp\"\n"
                               "static_assert(fleet::engine::kQuarryGeneratedCodeIdFormat == 1U);\n"
                               "static_assert(" +
                               code_id +
                               "_CODE_ID_FORMAT == 1U);\n"
                               "static_assert(fleet::engine::kQuarryGeneratedCodeId[0] == 0x" +
                               id.substr(0, 2) +
                               "U);\n"
                               "static_assert(fleet::engine::kQuarryGeneratedCodeId[15] == 0x" +
                               id.substr(30, 2) +
                               "U);\n"
                               "static_assert(" +
                               code_id + "_CODE_ID_W0 == 0x" + id.substr(0, 8) +
                               "UL);\n"
                               "static_assert(" +
                               code_id + "_CODE_ID_W3 == 0x" + id.substr(24, 8) +
                               "UL);\n"
                               // The inline namespace is named by the same identity.
                               "static_assert(fleet::engine::quarry_gen_" +
                               id + "::kQuarryGeneratedCodeId[0] == 0x" + id.substr(0, 2) + "U);\n";
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
        // An A4.2 header still has configuration identity, but #117 renamed
        // those macros, so the configuration check reports it first.
        const fs::path migrated =
            generate(*variant, rev_a(), name + "-a42", "--generate fleet.engine");
        fs::copy_file(fixtures() / "pre_generated_code_identity" / std::string(name) /
                          "fleet/common.generated.hpp.pre_generated_code_identity",
                      migrated / "fleet/common.generated.hpp");
        const RunResult old = syntax_check("#include \"fleet/engine.generated.hpp\"\n", {migrated},
                                           "pre-a42-" + name);
        EXPECT_NE(old.status, 0) << name;
        EXPECT_NE(
            old.output.find("fleet/common.generated.hpp predates generated configuration identity"),
            std::string::npos)
            << name << "\n"
            << old.output;

        // A header from this compiler with its generated-code identity macros
        // removed still fails the generated-code identity check, after the
        // configuration check has passed. The whole configuration is generated
        // so the dependency header exists; --generate fleet.engine emits only
        // the dependent.
        const fs::path output = generate(*variant, rev_a(), name);
        const fs::path common = output / "fleet/common.generated.hpp";
        const std::string prefix =
            (variant == &kOwning ? std::string(support::kCppOwningNamespaceMacroPrefix)
                                 : std::string(support::kCppCStorageNamespaceMacroPrefix)) +
            support::encode_namespace_macro("fleet.common").value();
        std::string text = read_text_file(common);
        const std::string marker = "#define " + prefix + "_CODE_ID_FORMAT";
        const std::size_t at = text.find(marker);
        ASSERT_NE(at, std::string::npos) << name;
        const std::size_t line_end = text.find('\n', at);
        text.erase(at, (line_end == std::string::npos ? text.size() : line_end + 1) - at);
        ASSERT_EQ(text.find(marker), std::string::npos);
        write_text_file(common, text);
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
            (variant == &kOwning ? std::string(support::kCppOwningNamespaceMacroPrefix)
                                 : std::string(support::kCppCStorageNamespaceMacroPrefix)) +
            support::encode_namespace_macro("fleet.common").value() + "_CODE_ID_FORMAT";
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
    s.set_mode(INV_CORE_MODE_ON); // encoded through the enum_load callback
    if (!s.set_items(items)) return 1;
    std::array<std::byte, 256> buffer{};
    const auto encoded = s.encode(buffer);
    if (encoded.status != QUARRY_C_STATUS_OK) return 2;
    quarry::cxx::inv_core_Status d;
    if (d.decode(std::span<const std::byte>(buffer.data(), encoded.bytes_written)).status !=
        QUARRY_C_STATUS_OK) return 3;
    if (!d.has_rpm() || d.rpm() != 1234U) return 4;
    if (!d.has_mode() || d.mode() != INV_CORE_MODE_ON) return 8;
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

// --- C-storage enum callback language linkage (PR #116) --------------------
//
// The runtime declares quarry_c_element_descriptor_t::enum_load/enum_store in
// extern "C", so per [dcl.link] they are pointers to functions with C language
// linkage. Calling a function through a pointer whose type has a different
// language linkage is undefined behavior (SEI CERT C++ EXP56-CPP). GCC, Clang
// and MSVC do not distinguish the two function types, so accepting the code is
// no evidence; these tests check the linkage structurally and through rules
// those compilers do enforce: name linkage conflicts ([dcl.link]: different
// language linkages for one entity are ill-formed) and the identity of
// same-named C-linkage functions declared in different namespaces.

struct EnumCallbackDefinition {
    std::string prefix; // everything before the return type, e.g. `extern "C" inline `
    std::string name;   // the external symbol
};

[[nodiscard]] std::vector<EnumCallbackDefinition>
enum_callback_definitions(const std::string& header) {
    std::vector<EnumCallbackDefinition> definitions;
    const std::regex definition(
        R"(^(.*?)\b(?:uint64_t|void) (\w+_enum_(?:load|store))\((?:const void\* p|void\* p, uint64_t n)\) \{)");
    std::istringstream lines(header);
    std::string line;
    while (std::getline(lines, line)) {
        std::smatch match;
        if (std::regex_search(line, match, definition)) {
            definitions.push_back({.prefix = match[1].str(), .name = match[2].str()});
        }
    }
    return definitions;
}

// Every way a C-storage header can violate the callback contract, as
// human-readable findings; empty means the header satisfies it. `bases` gets
// the <record>_field_<index> part of every callback pair.
[[nodiscard]] std::vector<std::string> enum_callback_violations(const std::string& header,
                                                                const std::string& gid,
                                                                std::set<std::string>& bases) {
    std::vector<std::string> violations;
    const std::string identity_prefix = "quarry_gen_" + gid + "_";
    const std::vector<EnumCallbackDefinition> definitions = enum_callback_definitions(header);
    if (definitions.empty()) {
        violations.emplace_back("no enum callback definitions");
    }
    std::set<std::string> loads;
    std::set<std::string> stores;
    for (const EnumCallbackDefinition& definition : definitions) {
        if (definition.prefix != "extern \"C\" inline ") {
            violations.push_back(definition.name + " is defined with '" + definition.prefix +
                                 "', not extern \"C\" inline (C language linkage, external "
                                 "linkage)");
        }
        if (!definition.name.starts_with(identity_prefix)) {
            violations.push_back(definition.name +
                                 " does not carry the header's full generated-code identity");
            continue;
        }
        const std::string rest = definition.name.substr(identity_prefix.size());
        const bool load = rest.ends_with("_enum_load");
        const std::string base = rest.substr(0, rest.size() - (load ? 10U : 11U));
        if (!std::regex_match(base, std::regex(R"(inv_core_\w+_field_[0-9]+)"))) {
            violations.push_back(definition.name + " has an unexpected base name");
        }
        (load ? loads : stores).insert(base);
    }
    if (loads != stores) {
        violations.emplace_back("enum_load and enum_store callbacks are not paired");
    }
    bases = loads;
    // Every reference, including the member descriptors, names the
    // identity-bearing symbols, and each descriptor stores its own pair.
    const std::regex token(R"(\b\w+_enum_(?:load|store)\b)");
    for (auto it = std::sregex_iterator(header.begin(), header.end(), token);
         it != std::sregex_iterator(); ++it) {
        if (!it->str().starts_with(identity_prefix)) {
            violations.push_back("reference to " + it->str() +
                                 " without the generated-code identity");
        }
    }
    for (const std::string& base : bases) {
        const std::string pair =
            identity_prefix + base + "_enum_load, " + identity_prefix + base + "_enum_store } },";
        if (header.find(pair) == std::string::npos) {
            violations.push_back("no member descriptor references " + identity_prefix + base +
                                 "_enum_{load,store}");
        }
    }
    return violations;
}

// Succeeds only if the header's callbacks have C language linkage: per
// [dcl.link], C-linkage functions with the same name declared in different
// namespace scopes are the same function, so these extern "C" redeclarations in
// the unrelated namespace linkage_probe name the callbacks defined in the
// quarry_gen_ namespace. For C++-linkage callbacks they would be other,
// undefined functions: the program fails to link, or (when the optimizer folds
// the address comparison) runs and reports the mismatch. The descriptor must
// store exactly these functions, and calls through them must work. (A separate
// namespace, not global scope: GCC treats a ::-qualified name found both
// globally and through the inline quarry_gen_ namespace as an overload set.)
[[nodiscard]] std::string callback_linkage_probe(std::string_view name, const std::string& gid,
                                                 const std::string& base) {
    const std::string load = "quarry_gen_" + gid + "_" + base + "_enum_load";
    const std::string store = "quarry_gen_" + gid + "_" + base + "_enum_store";
    return std::string(R"(#include "inv/core.generated.hpp"
#include <cstdint>
namespace linkage_probe {
extern "C" std::uint64_t )") +
           load + R"((const void*);
extern "C" void )" +
           store + R"((void*, std::uint64_t);
} // namespace linkage_probe
extern "C" int run_)" +
           std::string(name) + R"(() {
    int found = 0;
    for (const quarry_c_member_descriptor_t& member : ::inv_core_Status_members) {
        if (member.element.enum_load == nullptr && member.element.enum_store == nullptr) continue;
        if (member.element.enum_load != &linkage_probe::)" +
           load + R"( || member.element.enum_store != &linkage_probe::)" + store + R"() return 1;
        ++found;
    }
    if (found != 1) return 2;
    ::inv_core_Mode_t value = INV_CORE_MODE_OFF;
    linkage_probe::)" +
           store +
           R"((&value, 1U);
    if (value != INV_CORE_MODE_ON || linkage_probe::)" +
           load + R"((&value) != 1U) return 3;
    return 0;
}
)";
}

// Must not compile: redeclaring a callback with C++ language linkage in its own
// namespace gives one entity two language linkages, which [dcl.link] makes
// ill-formed and which compilers diagnose. It compiles only if the callback
// already has C++ linkage.
[[nodiscard]] std::string callback_cpp_linkage_redeclaration(const std::string& gid,
                                                             const std::string& base) {
    return "#include \"inv/core.generated.hpp\"\n#include <cstdint>\ninline namespace quarry_gen_" +
           gid + " {\nextern \"C++\" std::uint64_t quarry_gen_" + gid + "_" + base +
           "_enum_load(const void*);\n}\n";
}

// Defined symbols of an object file that name an enum callback, as nm prints
// them (Mach-O keeps its leading underscore). Empty with `available` false if
// nm cannot be run.
[[nodiscard]] std::vector<std::string> callback_symbols(const fs::path& object, const fs::path& log,
                                                        bool& available) {
    const RunResult listed = run("nm " + quote(object.string()), log);
    available = listed.status == 0;
    std::vector<std::string> symbols;
    std::istringstream lines(listed.output);
    std::string line;
    const std::regex defined(R"(^[0-9a-fA-F]+ [TtWw] (\S*_enum_(?:load|store)\S*)$)");
    while (std::getline(lines, line)) {
        std::smatch match;
        if (std::regex_match(line, match, defined)) {
            symbols.push_back(match[1].str());
        }
    }
    return symbols;
}

TEST_F(GeneratedCodeIdentityTest, CStorageEnumCallbacksHaveCLanguageLinkageAndIdentityNames) {
    const fs::path root = inv_root("base", kCoreSchema);
    const fs::path standard = generate(kCStorage, root, "default");
    const fs::path compact = generate(kCStorage, root, "compact", "--c-optimize-size");
    const std::string standard_gid = gid(standard / "inv/core.generated.hpp");
    const std::string compact_gid = gid(compact / "inv/core.generated.hpp");
    ASSERT_NE(standard_gid, compact_gid);

    std::set<std::string> standard_bases;
    std::set<std::string> compact_bases;
    EXPECT_EQ(enum_callback_violations(read_text_file(standard / "inv/core.generated.hpp"),
                                       standard_gid, standard_bases),
              std::vector<std::string>{});
    EXPECT_EQ(enum_callback_violations(read_text_file(compact / "inv/core.generated.hpp"),
                                       compact_gid, compact_bases),
              std::vector<std::string>{});
    ASSERT_EQ(standard_bases, (std::set<std::string>{"inv_core_Status_field_1"}));
    ASSERT_EQ(compact_bases, standard_bases);
    const std::string& base = *standard_bases.begin();

    // The compiler sees C language linkage on the callback names.
    const RunResult redeclared =
        syntax_check(callback_cpp_linkage_redeclaration(standard_gid, base), {standard},
                     "cpp-linkage-redeclaration");
    EXPECT_NE(redeclared.status, 0) << "a C++-linkage redeclaration compiled";
    EXPECT_NE(redeclared.output.find("linkage"), std::string::npos) << redeclared.output;

    for (const std::string& flags : link_flag_sets()) {
        const std::string tag = std::to_string(flags.size());
        // Identical definitions in two translation units: one inline entity.
        EXPECT_EQ(link_and_run(callback_linkage_probe("a", standard_gid, base), standard,
                               callback_linkage_probe("b", standard_gid, base), standard, flags,
                               "callbacks-same" + tag),
                  "run_a=0 run_b=0")
            << flags;
        // Different definitions: distinct symbols, both link orders.
        EXPECT_EQ(link_and_run(callback_linkage_probe("a", standard_gid, base), standard,
                               callback_linkage_probe("b", compact_gid, base), compact, flags,
                               "callbacks-ab" + tag),
                  "run_a=0 run_b=0")
            << flags;
        EXPECT_EQ(link_and_run(callback_linkage_probe("a", compact_gid, base), compact,
                               callback_linkage_probe("b", standard_gid, base), standard, flags,
                               "callbacks-ba" + tag),
                  "run_a=0 run_b=0")
            << flags;
    }

    // External symbol isolation: each object defines exactly its own
    // unmangled, identity-bearing callbacks, and no C++-mangled callback.
    ASSERT_EQ(link_and_run(callback_linkage_probe("a", standard_gid, base), standard,
                           callback_linkage_probe("b", compact_gid, base), compact, "-O0",
                           "callbacks-nm"),
              "run_a=0 run_b=0");
    const fs::path objects = work_ / "link" / "callbacks-nm";
    bool nm_available = false;
    const std::vector<std::string> standard_symbols =
        callback_symbols(objects / "a.o", work_ / "nm-a.log", nm_available);
    if (!nm_available) {
        GTEST_SKIP() << "nm is not available; symbol isolation not checked";
    }
    const std::vector<std::string> compact_symbols =
        callback_symbols(objects / "b.o", work_ / "nm-b.log", nm_available);
    const auto expected = [&](const std::string& identity, const std::vector<std::string>& seen) {
        const std::string load = "quarry_gen_" + identity + "_" + base + "_enum_load";
        const std::string store = "quarry_gen_" + identity + "_" + base + "_enum_store";
        const bool underscored = std::find(seen.begin(), seen.end(), "_" + load) != seen.end();
        const std::string lead = underscored ? "_" : "";
        return std::set<std::string>{lead + load, lead + store};
    };
    EXPECT_EQ(std::set<std::string>(standard_symbols.begin(), standard_symbols.end()),
              expected(standard_gid, standard_symbols));
    EXPECT_EQ(std::set<std::string>(compact_symbols.begin(), compact_symbols.end()),
              expected(compact_gid, compact_symbols));
    EXPECT_EQ(standard_symbols.size(), 2U);
    EXPECT_EQ(compact_symbols.size(), 2U);
}

// Mutation tests of the checks above: a header without extern "C" on its
// callbacks, or without the identity in their names, must be rejected by the
// structural check and by the compiler- and linker-enforced probes. The
// mutations are applied to real generated output.
TEST_F(GeneratedCodeIdentityTest, CallbackLinkageChecksRejectMutatedHeaders) {
    const fs::path generated = generate(kCStorage, inv_root("base", kCoreSchema), "default");
    const std::string header = read_text_file(generated / "inv/core.generated.hpp");
    const std::string identity = gid(generated / "inv/core.generated.hpp");
    std::set<std::string> bases;
    ASSERT_EQ(enum_callback_violations(header, identity, bases), std::vector<std::string>{});
    ASSERT_EQ(bases.size(), 1U);
    const std::string& base = *bases.begin();

    const auto mutant = [&](std::string_view tag, const std::string& text) {
        const fs::path directory = work_ / "mutants" / std::string(tag);
        write_text_file(directory / "inv/core.generated.hpp", text);
        return directory;
    };

    // Mutation 1: callbacks lose C language linkage (C++-linkage inline functions).
    const std::string no_extern_c = replace_all(header, "extern \"C\" inline ", "inline ");
    ASSERT_NE(no_extern_c, header);
    std::set<std::string> ignored;
    EXPECT_NE(enum_callback_violations(no_extern_c, identity, ignored), std::vector<std::string>{});
    const fs::path cpp_linkage = mutant("no-extern-c", no_extern_c);
    EXPECT_EQ(syntax_check(callback_cpp_linkage_redeclaration(identity, base), {cpp_linkage},
                           "mutant-redeclaration")
                  .status,
              0)
        << "the C++-linkage redeclaration probe must accept a C++-linkage callback";
    {
        const fs::path directory = work_ / "mutant-link";
        fs::create_directories(directory);
        write_text_file(directory / "probe.cpp", callback_linkage_probe("a", identity, base) +
                                                     "int main() { return run_a(); }\n");
        const RunResult linked =
            run(quote(identity_cxx()) + " -std=c++20" QUARRY_TEST_STRICT_CXX_WARNINGS " -I" +
                    quote(cpp_linkage.string()) + " -I" + quote(QUARRY_TEST_REPO_INCLUDE_DIR) +
                    " -I" + quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) + " " +
                    quote((directory / "probe.cpp").string()) + " " +
                    quote(QUARRY_TEST_RUNTIME_C_LIBRARY) + " -o " +
                    quote((directory / "probe").string()),
                directory / "link.log");
        const bool accepted =
            linked.status == 0 &&
            run(quote((directory / "probe").string()), directory / "run.log").status == 0;
        EXPECT_FALSE(accepted) << "the linkage probe must reject C++-linkage callbacks";
    }

    // Mutation 2: callback names lose the generated-code identity.
    const std::string no_identity =
        replace_all(header, "quarry_gen_" + identity + "_" + base, base);
    ASSERT_NE(no_identity, header);
    EXPECT_NE(enum_callback_violations(no_identity, identity, ignored), std::vector<std::string>{});

    // Mutation 3: callbacks keep C linkage but drop external linkage (static).
    const std::string internal =
        replace_all(header, "extern \"C\" inline ", "extern \"C\" static inline ");
    ASSERT_NE(internal, header);
    EXPECT_NE(enum_callback_violations(internal, identity, ignored), std::vector<std::string>{});

    // Mutation 4: a descriptor stores another function than its own pair.
    const std::string swapped =
        replace_all(header, "quarry_gen_" + identity + "_" + base + "_enum_load, ",
                    "quarry_gen_" + identity + "_" + base + "_enum_load_other, ");
    ASSERT_NE(swapped, header);
    EXPECT_NE(enum_callback_violations(swapped, identity, ignored), std::vector<std::string>{});
}

// --- namespace macro injectivity (#117) --------------------------------------

[[nodiscard]] std::string header_relative(std::string_view namespace_fqn,
                                          std::string_view extension) {
    std::string path;
    path.reserve(namespace_fqn.size() + extension.size());
    for (const char character : namespace_fqn) {
        path.push_back(character == '.' ? '/' : character);
    }
    path.append(extension);
    return path;
}

// One configuration whose only generated namespace is `namespace_fqn`. Separate
// configurations are required: the planner still rejects a.b together with a_b
// on the path-derived guard, even though the emitted identity guard would not
// collide.
[[nodiscard]] fs::path single_namespace_root(const fs::path& work, std::string_view tag,
                                             std::string_view namespace_fqn,
                                             std::string_view record) {
    const fs::path directory = work / "schemas" / std::string(tag);
    write_text_file(directory / "configuration.brd",
                    "configuration: pair\nimports:\n  - unit.brd\n");
    write_text_file(directory / "unit.brd",
                    "namespace: " + std::string(namespace_fqn) +
                        "\nrecord: " + std::string(record) +
                        "\nversion: 1\ntype: data\nfields:\n  value:\n    type: u32\n");
    return directory / "configuration.brd";
}

// Copies the generated tree `from` into `destination` and returns the include
// path of its header relative to `destination`'s parent.
[[nodiscard]] std::string stage_header(const fs::path& from, const fs::path& destination,
                                       std::string_view relative) {
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(from)) {
        const fs::path target = destination / fs::relative(entry.path(), from);
        if (entry.is_directory()) {
            fs::create_directories(target);
            continue;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        fs::create_directories(target.parent_path());
        fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
    }
    return (fs::path(destination.filename()) / relative).generic_string();
}

TEST_F(GeneratedCodeIdentityTest, DistinctNamespaceMacroSpellingsCoexistInOneTranslationUnit) {
    // Pairs whose A4.2 uppercased spellings collided, plus names the frontend
    // accepts that contain digits or leading, trailing and consecutive
    // underscores. Both inclusion orders must compile. Plain C and C++/C-storage
    // use different record names: they share the non-injective C symbol prefix,
    // so the same record name would collide before the macros could.
    const std::pair<std::string_view, std::string_view> pairs[] = {
        {"dep.engine", "dep_engine"}, {"Ab", "ab"},        {"a.b", "a_b"}, {"a.b_c", "a_b.c"},
        {"n2.a_b3", "n2_a.b3"},       {"_lead", "trail_"}, {"a__b", "q0"},
    };
    std::size_t index = 0;
    for (const auto& [left_ns, right_ns] : pairs) {
        const std::string tag = std::to_string(index++);
        const auto left_body = support::encode_namespace_macro(left_ns);
        const auto right_body = support::encode_namespace_macro(right_ns);
        ASSERT_TRUE(left_body.has_value()) << left_ns;
        ASSERT_TRUE(right_body.has_value()) << right_ns;
        ASSERT_NE(*left_body, *right_body) << left_ns << " vs " << right_ns;

        for (const Variant* variant : {&kOwning, &kCStorage, &kPlainC}) {
            const bool plain_c = variant == &kPlainC;
            // C-storage emits the same C symbols as plain C, inside its inline
            // namespace. Identical record names still collide across dep.engine
            // and dep_engine; distinct record names keep that finding out of
            // this macro check.
            const bool c_symbols = plain_c || variant == &kCStorage;
            const std::string name = std::string(variant->name) + "-" + tag;
            const fs::path left_root =
                single_namespace_root(work_, name + "-l", left_ns, c_symbols ? "Left" : "Sample");
            const fs::path right_root =
                single_namespace_root(work_, name + "-r", right_ns, c_symbols ? "Right" : "Sample");
            const fs::path left = generate(*variant, left_root, name + "-gl");
            const fs::path right = generate(*variant, right_root, name + "-gr");
            const std::string left_header = header_relative(left_ns, variant->extension);
            const std::string right_header = header_relative(right_ns, variant->extension);
            ASSERT_TRUE(fs::exists(left / left_header)) << left_header;
            ASSERT_TRUE(fs::exists(right / right_header)) << right_header;

            const std::string prefix =
                plain_c
                    ? std::string(support::kCNamespaceMacroPrefix)
                    : std::string(variant == &kOwning ? support::kCppOwningNamespaceMacroPrefix
                                                      : support::kCppCStorageNamespaceMacroPrefix);
            const std::string left_macro =
                prefix + *left_body + (plain_c ? "_SCHEMA_ID_ALGORITHM" : "_CODE_ID_FORMAT");
            const std::string right_macro =
                prefix + *right_body + (plain_c ? "_SCHEMA_ID_ALGORITHM" : "_CODE_ID_FORMAT");
            EXPECT_TRUE(support::is_unreserved_macro_identifier(left_macro)) << left_macro;
            EXPECT_TRUE(support::is_unreserved_macro_identifier(right_macro)) << right_macro;
            EXPECT_NE(read_text_file(left / left_header).find("#define " + left_macro),
                      std::string::npos)
                << name;
            EXPECT_NE(read_text_file(right / right_header).find("#define " + right_macro),
                      std::string::npos)
                << name;
            if (plain_c) {
                EXPECT_NE(read_text_file(left / left_header)
                              .find(std::string(support::kCNamespaceAccessorPrefix) + *left_body +
                                    "_schema_id"),
                          std::string::npos);
            }

            const fs::path staged = work_ / "staged" / name;
            const std::string left_include = stage_header(left, staged / "left", left_header);
            const std::string right_include = stage_header(right, staged / "right", right_header);
            for (const bool left_first : {true, false}) {
                const std::string first = left_first ? left_include : right_include;
                const std::string second = left_first ? right_include : left_include;
                const std::string source = "#include \"" + first + "\"\n#include \"" + second +
                                           "\"\nint main(void) { return 0; }\n";
                const RunResult result =
                    syntax_check(source, {staged}, name + (left_first ? "-lr" : "-rl"), plain_c);
                EXPECT_EQ(result.status, 0) << name << "\n" << result.output;
                if (plain_c) {
                    // #111 G2: the same pair as C++, each in its own identity namespace.
                    const RunResult as_cpp =
                        syntax_check(source, {staged}, name + (left_first ? "-lr-cpp" : "-rl-cpp"));
                    EXPECT_EQ(as_cpp.status, 0) << name << " (C++)\n" << as_cpp.output;
                }
            }
        }
    }
}

// --- plain C bindings (#111 G2) ----------------------------------------------
//
// Plain C headers carry the generated-code identity in their guard, metadata
// macros and dependency checks, in C and in C++. Compiled as C++, their
// declarations go in inline namespace quarry_gen_<gid> inside extern "C": the
// struct and enum types, and every template instantiated over them, get
// mangled names specific to their definitions, while every function keeps a
// C-language-linkage type and internal linkage. Compiled as C, nothing but
// preprocessor text changes.

[[nodiscard]] std::string plain_c_prefix(std::string_view namespace_fqn) {
    return std::string(support::kCNamespaceCodeIdMacroPrefix) +
           support::encode_namespace_macro(namespace_fqn).value();
}

// Compiles `source` both as C99 and as C++20, strict, with `includes`.
struct DualResult {
    RunResult c;
    RunResult cpp;
};

// A round trip through the plain C API of inv.core, valid C99 and C++20. In
// C++ it also instantiates std::vector over the generated struct: #111 case 7,
// where two translation units with different definitions of
// inv_core_Status_t shared one std::vector instantiation (corrupted elements,
// ASan errors) before the C++ wrap.
//
// PRESENCE selects the presence API of the header (default or
// --c-optimize-size); FIELD is the u16 field (rpm, or speed when renamed).
constexpr std::string_view kPlainCTu = R"(PRESENCE
#include "inv/core.generated.h"
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
#include <vector>
#define QUARRY_TEST_RUN extern "C" int
#else
#define QUARRY_TEST_RUN int
#endif
QUARRY_TEST_RUN run_NAME(void) {
    inv_core_Status_t s;
    inv_core_Status_init(&s);
    s.FIELD = 1234U;
    SET_PRESENT(inv_core_Status, s, FIELD);
    s.mode = INV_CORE_MODE_ON; /* encoded through the enum_load callback */
    SET_PRESENT(inv_core_Status, s, mode);
    for (unsigned i = 0; i < 3U; ++i) {
        inv_core_Item_init(&s.items[i]);
        s.items[i].a = 7U + i;
        SET_PRESENT(inv_core_Item, s.items[i], a);
    }
    s.items_count = 3U;
    uint8_t buffer[256];
    const inv_core_Status_encode_result_t encoded = inv_core_Status_encode(&s, buffer, sizeof buffer);
    if (encoded.status != QUARRY_C_STATUS_OK) return 2;
    const inv_core_Status_decode_result_t decoded = inv_core_Status_decode(buffer, encoded.bytes_written);
    if (decoded.status != QUARRY_C_STATUS_OK) return 3;
    if (!HAS(inv_core_Status, decoded.value, FIELD) || decoded.value.FIELD != 1234U) return 4;
    if (!HAS(inv_core_Status, decoded.value, mode) || decoded.value.mode != INV_CORE_MODE_ON) return 8;
    if (decoded.value.items_count != 3U || decoded.value.items[2].a != 9U) return 5;
    if (LEVEL_CHECK) return 6;
#ifdef __cplusplus
    inv_core_Status_t fill;
    memset(&fill, 0x5A, sizeof fill);
    std::vector<inv_core_Status_t> elements;
    for (int i = 0; i < 5; ++i) elements.push_back(fill);
    for (const inv_core_Status_t& element : elements) {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&element);
        for (size_t k = 0; k < sizeof element; ++k) if (bytes[k] != 0x5A) return 7;
    }
#endif
    return 0;
}
)";

constexpr std::string_view kDefaultPresence =
    "#define SET_PRESENT(record, value, field) ((value).has_##field = true)\n"
    "#define HAS(record, value, field) ((value).has_##field)";
constexpr std::string_view kCompactPresence =
    "#define SET_PRESENT(record, value, field) record##_set_##field##_present(&(value), true)\n"
    "#define HAS(record, value, field) record##_has_##field(&(value))";

[[nodiscard]] std::string plain_c_tu(std::string_view name, bool compact,
                                     std::string_view field = "rpm",
                                     std::string_view level_high = "1") {
    return replace_all(
        replace_all(replace_all(replace_all(std::string(kPlainCTu), "PRESENCE",
                                            compact ? kCompactPresence : kDefaultPresence),
                                "NAME", name),
                    "FIELD", field),
        "LEVEL_CHECK", "INV_CORE_LEVEL_HIGH != " + std::string(level_high));
}

// Defined symbols with external linkage in an object file, as nm prints them;
// `available` is false if nm cannot be run.
[[nodiscard]] std::vector<std::string> external_symbols(const fs::path& object, const fs::path& log,
                                                        bool& available) {
    const RunResult listed = run("nm -g " + quote(object.string()), log);
    available = listed.status == 0;
    std::vector<std::string> symbols;
    std::istringstream lines(listed.output);
    std::string line;
    const std::regex defined(R"(^[0-9a-fA-F]+ ([A-TV-Za-tv-z]) (\S+)$)");
    while (std::getline(lines, line)) {
        std::smatch match;
        if (std::regex_match(line, match, defined)) {
            symbols.push_back(match[2].str());
        }
    }
    return symbols;
}

// External symbols that name a generated inv.core type but not the header's
// own identity namespace (mangled as quarry_gen_<gid>): every such symbol is
// one the linker could merge with another definition of the type.
[[nodiscard]] std::vector<std::string> unisolated_symbols(const std::vector<std::string>& symbols,
                                                          const std::string& gid) {
    std::vector<std::string> found;
    for (const std::string& symbol : symbols) {
        if (symbol.find("inv_core_") != std::string::npos &&
            symbol.find("quarry_gen_" + gid) == std::string::npos) {
            found.push_back(symbol);
        }
    }
    return found;
}

// Violations of the plain C callback contract: every enum callback is a
// static inline function defined inside the header's extern "C" block (so
// its function type has C language linkage, [dcl.link], CERT EXP56-CPP, and
// its name has internal linkage), and the member descriptors reference it.
[[nodiscard]] std::vector<std::string> plain_c_callback_violations(const std::string& header,
                                                                   std::set<std::string>& bases) {
    std::vector<std::string> violations;
    const std::size_t open = header.find("extern \"C\" {\n");
    const std::size_t close = header.find("} /* extern \"C\" */");
    if (open == std::string::npos || close == std::string::npos || close < open) {
        violations.emplace_back("no extern \"C\" block");
        return violations;
    }
    const std::regex definition(
        R"(^(.*?)\b(?:uint64_t|void) (\w+)_enum_(load|store)\((?:const void\* p|void\* p, uint64_t n)\) \{)");
    std::istringstream lines(header);
    std::string line;
    std::size_t offset = 0;
    std::set<std::string> loads;
    std::set<std::string> stores;
    while (std::getline(lines, line)) {
        std::smatch match;
        if (std::regex_search(line, match, definition)) {
            const std::string name = match[2].str() + "_enum_" + match[3].str();
            if (match[1].str() != "static inline ") {
                violations.push_back(name + " is defined with '" + match[1].str() +
                                     "', not static inline");
            }
            if (offset < open || offset > close) {
                violations.push_back(name + " is defined outside the extern \"C\" block");
            }
            (match[3].str() == "load" ? loads : stores).insert(match[2].str());
        }
        offset += line.size() + 1U;
    }
    if (loads.empty()) {
        violations.emplace_back("no enum callback definitions");
    }
    if (loads != stores) {
        violations.emplace_back("enum_load and enum_store callbacks are not paired");
    }
    for (const std::string& base : loads) {
        if (header.find(base + "_enum_load, " + base + "_enum_store } },") == std::string::npos) {
            violations.push_back("no member descriptor references " + base + "_enum_{load,store}");
        }
    }
    bases = loads;
    return violations;
}

TEST_F(GeneratedCodeIdentityTest, PlainCIdentitiesIgnoreOutputNamingAndSelection) {
    const fs::path all = generate(kPlainC, rev_a(), "c-all");
    const fs::path again = generate(kPlainC, rev_a(), "c-again");
    const fs::path common = generate(kPlainC, rev_a(), "c-common", "--generate fleet.common");
    const fs::path engine = generate(kPlainC, rev_a(), "c-engine", "--generate fleet.engine");
    const fs::path renamed = generate(kPlainC, rev_a(), "c-renamed", "--root-file-stem other");
    for (const std::string_view ns : {"common", "engine"}) {
        const std::string file = "fleet/" + std::string(ns) + ".generated.h";
        const std::string expected = gid(all / file);
        ASSERT_EQ(expected.size(), 32U);
        EXPECT_EQ(gid(again / file), expected) << ns;
        EXPECT_EQ(gid(renamed / file), expected) << ns;
        EXPECT_EQ(gid((ns == "common" ? common : engine) / file), expected)
            << ns << ": --generate must not change the identity";
        // Guard and metadata carry the same identity as the C++ namespace.
        const std::string text = read_text_file(all / file);
        std::string upper = expected;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const std::string prefix = plain_c_prefix("fleet." + std::string(ns));
        EXPECT_NE(text.find("#ifndef " + prefix + "_" + upper + "_\n"), std::string::npos) << ns;
        EXPECT_NE(text.find("#define " + prefix + "_CODE_ID_FORMAT 1U\n"), std::string::npos);
        EXPECT_NE(text.find("#define " + prefix + "_CODE_ID_W0 0x" + expected.substr(0, 8) + "UL"),
                  std::string::npos);
        EXPECT_NE(text.find("#define " + prefix + "_CODE_ID_W3 0x" + expected.substr(24, 8) + "UL"),
                  std::string::npos);
    }
    for (const std::string_view file : {"fleet/common.generated.h", "fleet/common.generated.c"}) {
        EXPECT_EQ(read_text_file(all / file), read_text_file(common / file)) << file;
        EXPECT_EQ(read_text_file(all / file), read_text_file(again / file)) << file;
    }
}

TEST_F(GeneratedCodeIdentityTest, PlainCDefinitionChangesChangeIdentitiesButNotSchemaId) {
    const fs::path base = inv_root("base", kCoreSchema);
    const std::map<std::string, fs::path> changed = {
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
    const auto schema_id = [&](const fs::path& root) {
        return run(quote(identity_generator()) + " --print-schema-id " + quote(root.string()),
                   work_ / "sid.log")
            .output;
    };
    const std::string base_gid = gid(generate(kPlainC, base, "c-base") / "inv/core.generated.h");
    for (const auto& [label, root] : changed) {
        EXPECT_EQ(schema_id(root), schema_id(base)) << label;
        EXPECT_NE(gid(generate(kPlainC, root, "c-" + label) / "inv/core.generated.h"), base_gid)
            << label;
    }
    EXPECT_NE(gid(generate(kPlainC, base, "c-os", "--c-optimize-size") / "inv/core.generated.h"),
              base_gid);
    // Every variant of one namespace has its own identity.
    const std::string cstorage = gid(generate(kCStorage, base, "cs") / "inv/core.generated.hpp");
    const std::string owning = gid(generate(kOwning, base, "own") / "inv/core.generated.hpp");
    EXPECT_NE(base_gid, cstorage);
    EXPECT_NE(base_gid, owning);
}

TEST_F(GeneratedCodeIdentityTest, PlainCDependencyIdentityChangesPropagate) {
    const fs::path a = generate(kPlainC, dep_root("base", kCommonSchema, kEngineSchema), "a");
    const fs::path b =
        generate(kPlainC,
                 dep_root("common", replace_all(std::string(kCommonSchema), "unix_ms", "millis"),
                          kEngineSchema),
                 "b");
    const fs::path c =
        generate(kPlainC,
                 dep_root("engine", kCommonSchema,
                          replace_all(std::string(kEngineSchema), "  rpm:", "  speed:")),
                 "c");
    EXPECT_NE(gid(b / "dep/common.generated.h"), gid(a / "dep/common.generated.h"));
    EXPECT_NE(gid(b / "dep/engine.generated.h"), gid(a / "dep/engine.generated.h"))
        << "a dependency's identity must propagate";
    EXPECT_EQ(gid(c / "dep/common.generated.h"), gid(a / "dep/common.generated.h"));
    EXPECT_NE(gid(c / "dep/engine.generated.h"), gid(a / "dep/engine.generated.h"));
}

TEST_F(GeneratedCodeIdentityTest, PlainCIdentityMetadataMatchesTheHeader) {
    const fs::path output = generate(kPlainC, rev_a(), "c-meta");
    const std::string id = gid(output / "fleet/engine.generated.h");
    const std::string prefix = plain_c_prefix("fleet.engine");
    // C: the macros are usable in #if.
    const std::string c_source =
        "#include \"fleet/engine.generated.h\"\n#if " + prefix + "_CODE_ID_FORMAT != 1U || " +
        prefix + "_CODE_ID_W0 != 0x" + id.substr(0, 8) + "UL || " + prefix + "_CODE_ID_W3 != 0x" +
        id.substr(24, 8) + "UL\n#error mismatch\n#endif\n" + "int main(void) { return 0; }\n";
    const RunResult in_c = syntax_check(c_source, {output}, "c-meta-c", true);
    EXPECT_EQ(in_c.status, 0) << in_c.output;
    // C++: the declarations are in the namespace named by the same identity,
    // and remain reachable by their C names.
    const std::string cpp_source =
        c_source + "using T = quarry_gen_" + id +
        "::fleet_engine_EngineStatus_t;\n"
        "static_assert(sizeof(T) == sizeof(::fleet_engine_EngineStatus_t));\n";
    const RunResult in_cpp = syntax_check(cpp_source, {output}, "c-meta-cpp");
    EXPECT_EQ(in_cpp.status, 0) << in_cpp.output;
}

TEST_F(GeneratedCodeIdentityTest, PlainCRejectsDependenciesWithOtherDefinitions) {
    const fs::path engine = generate(kPlainC, dep_root("base", kCommonSchema, kEngineSchema), "e");
    const fs::path common =
        generate(kPlainC,
                 dep_root("renamed", replace_all(std::string(kCommonSchema), "unix_ms", "millis"),
                          kEngineSchema),
                 "r");
    const fs::path mixed = work_ / "mixed";
    fs::create_directories(mixed / "dep");
    fs::copy_file(engine / "dep/engine.generated.h", mixed / "dep/engine.generated.h");
    fs::copy_file(common / "dep/common.generated.h", mixed / "dep/common.generated.h");
    for (const bool plain_c : {true, false}) {
        const RunResult result =
            syntax_check("#include \"dep/engine.generated.h\"\nint main(void) { return 0; }\n",
                         {mixed}, plain_c ? "mixed-c" : "mixed-cpp", plain_c);
        EXPECT_NE(result.status, 0);
        EXPECT_NE(result.output.find("dep/common.generated.h contains different generated "
                                     "definitions than dep/engine.generated.h was generated "
                                     "against"),
                  std::string::npos)
            << result.output;
        EXPECT_EQ(result.output.find("different Quarry configuration"), std::string::npos)
            << "the configuration identity still matches";
    }
}

TEST_F(GeneratedCodeIdentityTest, PlainCRejectsDependenciesThatPredateGeneratedCodeIdentity) {
    // Real pre-G2 output: configuration identity with its #117 names, no
    // generated-code identity.
    const fs::path migrated = generate(kPlainC, rev_a(), "c-pre", "--generate fleet.engine");
    fs::copy_file(fixtures() / "pre_generated_code_identity/c/fleet/"
                               "common.generated.h.pre_generated_code_identity",
                  migrated / "fleet/common.generated.h");
    for (const bool plain_c : {true, false}) {
        const RunResult result =
            syntax_check("#include \"fleet/engine.generated.h\"\nint main(void) { return 0; }\n",
                         {migrated}, plain_c ? "pre-c" : "pre-cpp", plain_c);
        EXPECT_NE(result.status, 0);
        EXPECT_NE(result.output.find("fleet/common.generated.h predates generated-code identity"),
                  std::string::npos)
            << result.output;
        EXPECT_EQ(result.output.find("predates generated configuration identity"),
                  std::string::npos)
            << result.output;
    }
}

TEST_F(GeneratedCodeIdentityTest, PlainCRejectsDependenciesWithAnotherIdentityFormat) {
    const fs::path output = generate(kPlainC, rev_a(), "c-format");
    const fs::path header = output / "fleet/common.generated.h";
    const std::string macro = plain_c_prefix("fleet.common") + "_CODE_ID_FORMAT";
    const std::string text = read_text_file(header);
    const std::string edited =
        replace_all(text, "#define " + macro + " 1U", "#define " + macro + " 2U");
    ASSERT_NE(edited, text);
    write_text_file(header, edited);
    for (const bool plain_c : {true, false}) {
        const RunResult result =
            syntax_check("#include \"fleet/engine.generated.h\"\nint main(void) { return 0; }\n",
                         {output}, plain_c ? "format-c" : "format-cpp", plain_c);
        EXPECT_NE(result.status, 0);
        EXPECT_NE(result.output.find("fleet/common.generated.h uses a different generated-code "
                                     "identity format than fleet/engine.generated.h"),
                  std::string::npos)
            << result.output;
    }
}

TEST_F(GeneratedCodeIdentityTest, PlainCRejectsTwoDefinitionsOfANamespaceInOneTranslationUnit) {
    const fs::path base = inv_root("base", kCoreSchema);
    const fs::path first = generate(kPlainC, base, "first");
    const fs::path second = generate(
        kPlainC, inv_root("renamed", replace_all(std::string(kCoreSchema), "  rpm:", "  speed:")),
        "second");
    const fs::path compact = generate(kPlainC, base, "compact", "--c-optimize-size");
    const fs::path copy = generate(kPlainC, base, "copy");
    for (const fs::path& other : {second, compact, copy}) {
        fs::create_directories(other / "other");
        fs::rename(other / "inv/core.generated.h", other / "other/core.generated.h");
    }
    const std::string pair =
        "#include \"inv/core.generated.h\"\n#include \"other/core.generated.h\"\n"
        "int main(void) { return 0; }\n";
    for (const bool plain_c : {true, false}) {
        const std::string tag = plain_c ? "-c" : "-cpp";
        for (const fs::path& different : {second, compact}) {
            const RunResult result = syntax_check(
                pair, {first, different}, "same-tu" + tag + different.filename().string(), plain_c);
            EXPECT_NE(result.status, 0) << different;
            EXPECT_NE(result.output.find("this translation unit already includes a different "
                                         "generated definition of namespace inv.core"),
                      std::string::npos)
                << different << "\n"
                << result.output;
        }
        // Identical definitions reached through two paths are deduplicated.
        const RunResult same = syntax_check(pair, {first, copy}, "same-copy" + tag, plain_c);
        EXPECT_EQ(same.status, 0) << same.output;
    }
    // A plain C and a C++/C-storage header of one namespace in one C++
    // translation unit: different definitions of the same C names.
    const fs::path cstorage = generate(kCStorage, base, "cstorage");
    const RunResult mixed =
        syntax_check("#include \"inv/core.generated.h\"\n#include \"inv/core.generated.hpp\"\n",
                     {first, cstorage}, "same-tu-variants");
    EXPECT_NE(mixed.status, 0);
    EXPECT_NE(mixed.output.find("already includes a different generated definition of namespace "
                                "inv.core"),
              std::string::npos)
        << mixed.output;
}

TEST_F(GeneratedCodeIdentityTest, PlainCHeadersConsumedFromCppAreIsolated) {
    const fs::path base_root = inv_root("base", kCoreSchema);
    const fs::path standard = generate(kPlainC, base_root, "default");
    const fs::path compact = generate(kPlainC, base_root, "compact", "--c-optimize-size");
    const fs::path renamed = generate(
        kPlainC, inv_root("renamed", replace_all(std::string(kCoreSchema), "  rpm:", "  speed:")),
        "renamed");
    const fs::path unused = generate(
        kPlainC,
        inv_root("unused",
                 replace_all(replace_all(std::string(kCoreSchema), "      low: 0", "      low: 2"),
                             "      high: 1", "      high: 0")),
        "unused");
    struct Side {
        std::string tu;
        fs::path includes;
    };
    const auto side = [&](std::string_view name, const fs::path& includes) {
        const bool is_compact = includes == compact;
        return Side{plain_c_tu(name, is_compact, includes == renamed ? "speed" : "rpm",
                               includes == unused ? "0" : "1"),
                    includes};
    };
    const std::vector<std::pair<std::string, std::pair<fs::path, fs::path>>> cases = {
        {"case7-default-vs-compact", {standard, compact}}, // the original case-7 reproduction
        {"field-names", {standard, renamed}},              // wire-equivalent definitions
        {"unused-enum", {standard, unused}},               // wire-equivalent definitions
        {"identical", {standard, standard}},               // one definition, two TUs
    };
    for (const std::string& flags : link_flag_sets()) {
        const std::string tag = std::to_string(flags.size());
        for (const auto& [label, pair] : cases) {
            for (const bool reversed : {false, true}) {
                const fs::path& first = reversed ? pair.second : pair.first;
                const fs::path& second = reversed ? pair.first : pair.second;
                const Side a = side("a", first);
                const Side b = side("b", second);
                const std::string name = label + (reversed ? "-ba" : "-ab") + tag;
                EXPECT_EQ(link_and_run(a.tu, a.includes, b.tu, b.includes, flags, "cpp-" + name),
                          "run_a=0 run_b=0")
                    << label << " (C++) " << flags << (reversed ? " reversed" : "");
                // Control: the same translation units compiled as C.
                EXPECT_EQ(
                    link_and_run(a.tu, a.includes, b.tu, b.includes, flags, "c-" + name, true),
                    "run_a=0 run_b=0")
                    << label << " (C) " << flags << (reversed ? " reversed" : "");
            }
        }
    }

    // External symbol isolation: every external symbol of a C++ object that
    // names a generated type carries that header's own identity, so the two
    // definitions share none; the C objects define no generated external
    // symbol at all.
    ASSERT_EQ(link_and_run(side("a", standard).tu, standard, side("b", compact).tu, compact, "-O0",
                           "nm-cpp"),
              "run_a=0 run_b=0");
    ASSERT_EQ(link_and_run(side("a", standard).tu, standard, side("b", compact).tu, compact, "-O0",
                           "nm-c", true),
              "run_a=0 run_b=0");
    bool nm_available = false;
    const auto symbols_of = [&](std::string_view directory, std::string_view object) {
        return external_symbols(work_ / "link" / std::string(directory) / std::string(object),
                                work_ / (std::string(directory) + std::string(object) + ".nm"),
                                nm_available);
    };
    const std::vector<std::string> cpp_a = symbols_of("nm-cpp", "a.o");
    if (!nm_available) {
        GTEST_SKIP() << "nm is not available; external symbol isolation not checked";
    }
    const std::vector<std::string> cpp_b = symbols_of("nm-cpp", "b.o");
    const std::string standard_gid = gid(standard / "inv/core.generated.h");
    const std::string compact_gid = gid(compact / "inv/core.generated.h");
    ASSERT_NE(standard_gid, compact_gid);
    const auto generated = [](const std::vector<std::string>& symbols) {
        std::set<std::string> found;
        for (const std::string& symbol : symbols) {
            if (symbol.find("inv_core_") != std::string::npos) {
                found.insert(symbol);
            }
        }
        return found;
    };
    EXPECT_FALSE(generated(cpp_a).empty()) << "std::vector<inv_core_Status_t> instantiations";
    EXPECT_EQ(unisolated_symbols(cpp_a, standard_gid), std::vector<std::string>{});
    EXPECT_EQ(unisolated_symbols(cpp_b, compact_gid), std::vector<std::string>{});
    std::vector<std::string> shared;
    const std::set<std::string> a_symbols = generated(cpp_a);
    const std::set<std::string> b_symbols = generated(cpp_b);
    std::set_intersection(a_symbols.begin(), a_symbols.end(), b_symbols.begin(), b_symbols.end(),
                          std::back_inserter(shared));
    EXPECT_EQ(shared, std::vector<std::string>{});
    for (const std::string_view object : {"a.o", "b.o"}) {
        EXPECT_EQ(generated(symbols_of("nm-c", object)), std::set<std::string>{})
            << object << ": a C object exports no generated symbol";
    }
}

TEST_F(GeneratedCodeIdentityTest, PlainCEnumCallbacksKeepCLanguageLinkage) {
    const fs::path standard = generate(kPlainC, inv_root("base", kCoreSchema), "default");
    const fs::path compact =
        generate(kPlainC, inv_root("base2", kCoreSchema), "compact", "--c-optimize-size");
    std::set<std::string> bases;
    for (const fs::path& output : {standard, compact}) {
        EXPECT_EQ(
            plain_c_callback_violations(read_text_file(output / "inv/core.generated.h"), bases),
            std::vector<std::string>{})
            << output;
        EXPECT_EQ(bases, (std::set<std::string>{"inv_core_Status_field_1"}));
    }
    // Compilers that track the language linkage of internal-linkage function
    // types (GCC does, Clang does not) reject a C++-linkage redeclaration of a
    // callback. Calibrated with a control, so the probe only runs where it can
    // discriminate.
    const RunResult control =
        syntax_check("namespace n {\nextern \"C\" { static int f(void) { return 0; } }\n"
                     "extern \"C++\" { static int f(void); }\n}\nint main() { return n::f(); }\n",
                     {}, "linkage-control");
    const bool tracks_linkage =
        control.status != 0 && (control.output.find("linkage") != std::string::npos ||
                                control.output.find("conflicting") != std::string::npos);
    if (!tracks_linkage) {
        std::printf("note: this compiler does not track the language linkage of internal-linkage "
                    "functions; only the structural check applies\n");
        return;
    }
    const std::string id = gid(standard / "inv/core.generated.h");
    const RunResult redeclared = syntax_check(
        "#include \"inv/core.generated.h\"\n#include <cstdint>\ninline namespace quarry_gen_" + id +
            " {\nextern \"C++\" { static std::uint64_t inv_core_Status_field_1_enum_load(const "
            "void*); }\n}\n",
        {standard}, "linkage-redeclared");
    EXPECT_NE(redeclared.status, 0) << "a C++-linkage redeclaration compiled";
    EXPECT_TRUE(redeclared.output.find("linkage") != std::string::npos ||
                redeclared.output.find("conflicting") != std::string::npos)
        << redeclared.output;
    const RunResult same = syntax_check(
        "#include \"inv/core.generated.h\"\n#include <cstdint>\ninline namespace quarry_gen_" + id +
            " {\nextern \"C\" { static std::uint64_t inv_core_Status_field_1_enum_load(const "
            "void*); }\n}\n",
        {standard}, "linkage-same");
    EXPECT_EQ(same.status, 0) << same.output;
}

// Mutation tests of the plain C checks, applied to real generated output: a
// header without the C++ identity namespace, a dependent without its
// generated-code identity checks, and callbacks outside extern "C" must each be
// caught.
TEST_F(GeneratedCodeIdentityTest, PlainCIsolationChecksRejectMutatedHeaders) {
    const fs::path base_root = inv_root("base", kCoreSchema);
    const fs::path standard = generate(kPlainC, base_root, "default");
    const fs::path compact = generate(kPlainC, base_root, "compact", "--c-optimize-size");
    const std::string standard_gid = gid(standard / "inv/core.generated.h");
    const std::string compact_gid = gid(compact / "inv/core.generated.h");

    // Mutation 1: no C++ namespace isolation (the pre-G2 C++ view). Only the
    // object is inspected: the mutant program has the case-7 ODR violation.
    const fs::path mutant = work_ / "mutant-default";
    fs::copy(standard, mutant, fs::copy_options::recursive);
    {
        const fs::path header = mutant / "inv/core.generated.h";
        const std::string text = read_text_file(header);
        const std::string stripped = replace_all(
            replace_all(text, "inline namespace quarry_gen_" + standard_gid + " {\n", ""),
            "} /* inline namespace quarry_gen_" + standard_gid + " */\n", "");
        ASSERT_NE(stripped, text);
        write_text_file(header, stripped);
    }
    const auto compile_object = [&](const fs::path& includes, std::string_view tag) {
        const fs::path source = work_ / (std::string(tag) + ".cpp");
        const fs::path object = work_ / (std::string(tag) + ".o");
        write_text_file(source, plain_c_tu("a", false));
        const RunResult compiled =
            run(quote(identity_cxx()) + " -std=c++20 -O0" QUARRY_TEST_STRICT_CXX_WARNINGS " -I" +
                    quote(includes.string()) + " -I" + quote(QUARRY_TEST_REPO_INCLUDE_DIR) + " -I" +
                    quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) + " -c " + quote(source.string()) +
                    " -o " + quote(object.string()),
                work_ / (std::string(tag) + ".log"));
        EXPECT_EQ(compiled.status, 0) << compiled.output;
        return object;
    };
    bool nm_available = false;
    const std::vector<std::string> original_symbols =
        external_symbols(compile_object(standard, "original"), work_ / "original.nm", nm_available);
    const std::vector<std::string> mutant_symbols =
        external_symbols(compile_object(mutant, "mutant"), work_ / "mutant.nm", nm_available);
    if (nm_available) {
        EXPECT_EQ(unisolated_symbols(original_symbols, standard_gid), std::vector<std::string>{});
        EXPECT_NE(unisolated_symbols(mutant_symbols, standard_gid), std::vector<std::string>{})
            << "the isolation check must flag std::vector<inv_core_Status_t> outside the "
               "identity namespace";
    }
    (void)compact_gid;

    // Mutation 2: the dependent loses its generated-code identity checks, so
    // a dependency with other definitions is no longer rejected.
    const fs::path engine = generate(kPlainC, dep_root("e", kCommonSchema, kEngineSchema), "e");
    const fs::path common = generate(
        kPlainC,
        dep_root("r", replace_all(std::string(kCommonSchema), "unix_ms", "millis"), kEngineSchema),
        "r");
    const fs::path mixed = work_ / "mixed";
    fs::create_directories(mixed / "dep");
    fs::copy_file(common / "dep/common.generated.h", mixed / "dep/common.generated.h");
    std::string dependent = read_text_file(engine / "dep/engine.generated.h");
    const std::string begin =
        "\n/* Every generated header this header includes must contain exactly";
    const std::size_t from = dependent.find(begin);
    ASSERT_NE(from, std::string::npos);
    const std::size_t to = dependent.find("#endif\n", from);
    ASSERT_NE(to, std::string::npos);
    const std::string without_checks = dependent.substr(0, from) + dependent.substr(to + 7U);
    const std::string probe = "#include \"dep/engine.generated.h\"\nint main(void) { return 0; }\n";
    write_text_file(mixed / "dep/engine.generated.h", dependent);
    EXPECT_NE(syntax_check(probe, {mixed}, "dep-original", true).status, 0);
    write_text_file(mixed / "dep/engine.generated.h", without_checks);
    EXPECT_EQ(syntax_check(probe, {mixed}, "dep-mutant", true).status, 0)
        << "without the identity checks the mixed set compiles: the dependency check is what "
           "rejects it";

    // Mutation 3: callbacks moved out of extern "C".
    std::set<std::string> bases;
    std::string header = read_text_file(standard / "inv/core.generated.h");
    const std::string moved =
        replace_all(replace_all(header, "extern \"C\" {\n", ""), "} /* extern \"C\" */\n", "");
    EXPECT_NE(plain_c_callback_violations(moved, bases), std::vector<std::string>{});
    const std::string not_static =
        replace_all(header, "static inline uint64_t inv_core_Status_field_1_enum_load",
                    "inline uint64_t inv_core_Status_field_1_enum_load");
    ASSERT_NE(not_static, header);
    EXPECT_NE(plain_c_callback_violations(not_static, bases), std::vector<std::string>{});
}

// G4's fast mandatory subset. Extended runs reuse these exact checks with
// explicit child compilers/flags; the original G1–G3 matrix remains intact.
TEST_F(GeneratedCodeIdentityTest, G4ConfigurationMetadataGuard) {
    const auto root = dep_root("metadata", kCommonSchema, kEngineSchema);
    for (const Variant* variant : {&kOwning, &kCStorage, &kPlainC}) {
        const std::string tag(variant->name);
        const auto generated = generate(*variant, root, tag);
        const fs::path dependency = generated / ("dep/common" + std::string(variant->extension));
        const std::string original = read_text_file(dependency);
        const std::regex metadata(
            "#define "
            "(QUARRY_NS_dep_common|QUARRY_GENERATED_CPP_NS_dep_common)_SCHEMA_ID_[^\\n]+\\n");
        const std::string removed = std::regex_replace(original, metadata, "");
        ASSERT_NE(removed, original);
        write_text_file(dependency, removed);
        const auto result =
            syntax_check("#include \"dep/engine" + std::string(variant->extension) + "\"\n",
                         {generated}, tag + "-metadata", variant == &kPlainC);
        EXPECT_NE(result.status, 0);
        EXPECT_NE(result.output.find("predates generated configuration identity"),
                  std::string::npos)
            << result.output;
    }
}

TEST_F(GeneratedCodeIdentityTest, G4TransitiveIdentityAndConfigurationNames) {
    const fs::path original = fixtures() / "transitive/configuration.brd";
    const fs::path changed = work_ / "changed";
    const fs::path renamed = work_ / "renamed";
    fs::copy(original.parent_path(), changed, fs::copy_options::recursive);
    fs::copy(original.parent_path(), renamed, fs::copy_options::recursive);
    write_text_file(changed / "clock.brd",
                    replace_all(read_text_file(changed / "clock.brd"), "  value:", "  renamed:"));
    write_text_file(renamed / "configuration.brd",
                    "configuration: another_name\nimports:\n  - engine.brd\n  - common.brd\n");
    const auto schema_id = [&](const fs::path& root) {
        const auto result =
            run(quote(identity_generator()) + " --print-schema-id " + quote(root.string()),
                work_ / "schema-id.log");
        EXPECT_EQ(result.status, 0) << result.output;
        return result.output;
    };
    EXPECT_EQ(schema_id(original), schema_id(changed / "configuration.brd"));
    EXPECT_EQ(schema_id(original), schema_id(renamed / "configuration.brd"));
    for (const Variant* variant : {&kOwning, &kCStorage, &kPlainC}) {
        const std::string name(variant->name);
        const std::string policy = variant == &kOwning ? "" : "--c-optimize-size=fleet.clock";
        const auto base = generate(*variant, original, name + "-base", policy);
        const auto leaf = generate(*variant, changed / "configuration.brd", name + "-leaf", policy);
        const auto other =
            generate(*variant, renamed / "configuration.brd", name + "-renamed", policy);
        const auto selected =
            generate(*variant, original, name + "-selected", policy + " --generate fleet.engine");
        for (const std::string_view ns : {"clock", "common", "engine"}) {
            const std::string header = "fleet/" + std::string(ns) + std::string(variant->extension);
            EXPECT_NE(gid(base / header), gid(leaf / header)) << "transitive " << ns;
            EXPECT_EQ(read_text_file(base / header), read_text_file(other / header));
        }
        const std::string engine = "fleet/engine" + std::string(variant->extension);
        EXPECT_EQ(read_text_file(base / engine), read_text_file(selected / engine));
        EXPECT_FALSE(fs::exists(selected / ("fleet/clock" + std::string(variant->extension))));
    }
}

TEST_F(GeneratedCodeIdentityTest, G4RuntimeEnumNamesWireChangesAndControls) {
    const auto base_root = inv_root("base", kCoreSchema);
    const auto enum_root =
        inv_root("enum", replace_all(std::string(kCoreSchema), "      on: 1", "      run: 1"));
    const auto wire_root =
        inv_root("wire", replace_all(std::string(kCoreSchema), "    type: u16", "    type: u32"));
    for (const Variant* variant : {&kOwning, &kCStorage, &kPlainC}) {
        const std::string name(variant->name);
        const auto base = generate(*variant, base_root, name + "-base");
        const auto en = generate(*variant, enum_root, name + "-enum");
        const auto wire = generate(*variant, wire_root, name + "-wire");
        const std::string header = "inv/core" + std::string(variant->extension);
        EXPECT_NE(gid(base / header), gid(en / header));
        EXPECT_NE(gid(base / header), gid(wire / header));
        const auto source = [&](std::string_view which) {
            if (variant == &kPlainC)
                return plain_c_tu(which, false);
            std::string text = tu(variant == &kOwning ? kOwningTu : kCStorageTu, which, "rpm");
            if (variant == &kOwning) {
                text = replace_all(text, "    inv::core::StatusBuilder b;",
                                   "    inv::core::StatusBuilder b;\n    if "
                                   "(!b.set_mode(inv::core::Mode::on)) return 9;");
                text = replace_all(text, "    if (!d ||",
                                   "    if (d && (!d->mode() || *d->mode() != "
                                   "inv::core::Mode::on)) return 10;\n    if (!d ||");
            }
            return text;
        };
        for (const auto& [label, other] : std::vector<std::pair<std::string, fs::path>>{
                 {"identical", base}, {"enum", en}, {"wire", wire}}) {
            std::string b = source("b");
            if (label == "enum")
                b = replace_all(replace_all(b, "Mode::on", "Mode::run"), "INV_CORE_MODE_ON",
                                "INV_CORE_MODE_RUN");
            for (const bool reversed : {false, true}) {
                EXPECT_EQ(link_and_run(source("a"), base, b, other, matrix_flags(), name + label,
                                       variant == &kPlainC, false, reversed),
                          "run_a=0 run_b=0");
                if (variant == &kPlainC) {
                    EXPECT_EQ(link_and_run(source("a"), base, b, other, matrix_flags(),
                                           name + label + "-cpp", false, false, reversed),
                              "run_a=0 run_b=0");
                }
            }
        }
    }
}

TEST_F(GeneratedCodeIdentityTest, G4MixedLanguageInterchangeFromSeparateInvocations) {
    const auto root = inv_root("base", kCoreSchema);
    for (const bool compact : {false, true}) {
        const std::string policy = compact ? "--c-optimize-size=inv.core" : "";
        const auto a = generate(kPlainC, root, compact ? "a-compact" : "a", policy);
        const auto b =
            generate(kPlainC, root, compact ? "b-compact" : "b", policy + " --generate inv.core");
        EXPECT_EQ(read_text_file(a / "inv/core.generated.h"),
                  read_text_file(b / "inv/core.generated.h"));
        const std::string c = std::string(compact ? kCompactPresence : kDefaultPresence) + R"(
#include "inv/core.generated.h"
void fill(inv_core_Status_t* value) {
    inv_core_Status_init(value);
    value->rpm = 1234U;
    SET_PRESENT(inv_core_Status, *value, rpm);
}
int run_a(void) {
    inv_core_Status_t value;
    fill(&value);
    return value.rpm == 1234U ? 0 : 1;
}
)";
        const std::string cpp = R"(#include "inv/core.generated.h"
extern "C" void fill(inv_core_Status_t*);
extern "C" int run_b() {
    inv_core_Status_t value;
    fill(&value); // C producer passes a generated object into C++ code.
    uint8_t bytes[256];
    const auto encoded = inv_core_Status_encode(&value, bytes, sizeof bytes);
    if (encoded.status != QUARRY_C_STATUS_OK) return 1;
    const auto decoded = inv_core_Status_decode(bytes, encoded.bytes_written);
    return decoded.status == QUARRY_C_STATUS_OK && decoded.value.rpm == 1234U ? 0 : 2;
}
)";
        for (const bool reverse : {false, true}) {
            EXPECT_EQ(link_and_run(c, a, cpp, b, matrix_flags(), "mixed", false, true, reverse),
                      "run_a=0 run_b=0");
        }
    }
}

TEST_F(GeneratedCodeIdentityTest, G4AlternateGeneratorDefinitionsAndValidationBodies) {
    const char* alternate = std::getenv("QUARRY_IDENTITY_GENERATOR_B");
    if (alternate == nullptr) {
        GTEST_SKIP() << "extended check requires a second generator build";
    }
    if (std::getenv("QUARRY_IDENTITY_OLDER") != nullptr) {
        GTEST_SKIP() << "older headers use the dependency diagnostic test";
    }
    const bool drift = std::getenv("QUARRY_IDENTITY_DRIFT") != nullptr;
    const auto root = inv_root("base", kCoreSchema);
    const auto a = generate(kOwning, root, "generator-a");
    const auto b = work_ / "generator-b";
    const auto generated =
        run(quote(alternate) + " -o " + quote(b.string()) + " " + quote(root.string()),
            work_ / "alternate.log");
    ASSERT_EQ(generated.status, 0) << generated.output;
    const std::string header = "inv/core.generated.hpp";
    if (drift) {
        EXPECT_NE(gid(a / header), gid(b / header));
    } else {
        EXPECT_EQ(read_text_file(a / header), read_text_file(b / header))
            << "equivalent compiler builds";
    }
    const auto id_a =
        run(quote(identity_generator()) + " --print-schema-id " + quote(root.string()),
            work_ / "id-a.log");
    const auto id_b =
        run(quote(alternate) + " --print-schema-id " + quote(root.string()), work_ / "id-b.log");
    ASSERT_EQ(id_a.status, 0);
    ASSERT_EQ(id_b.status, 0);
    EXPECT_EQ(id_a.output, id_b.output);
    const std::string ordinary = tu(kOwningTu, "a", "rpm");
    const std::string other = drift ? R"(#include "inv/core.generated.hpp"
extern "C" int run_b() {
    inv::core::StatusBuilder b;
    return b.set_rpm(1234U) ? 1 : 0;
}
)"
                                    : tu(kOwningTu, "b", "rpm");
    for (const bool reverse : {false, true}) {
        EXPECT_EQ(link_and_run(ordinary, a, other, b, matrix_flags(), "generator-drift", false,
                               false, reverse),
                  "run_a=0 run_b=0");
    }
    if (drift) {
        const auto mutant = work_ / "false-namespace";
        fs::copy(b, mutant, fs::copy_options::recursive);
        const auto path = mutant / header;
        const std::string before = read_text_file(path);
        const std::string falsified =
            replace_all(before, "quarry_gen_" + gid(b / header), "quarry_gen_" + gid(a / header));
        ASSERT_NE(before, falsified);
        write_text_file(path, falsified);
        for (const bool reverse : {false, true}) {
            EXPECT_NE(link_and_run(ordinary, a, other, mutant, "-O0", "drift-negative-control",
                                   false, false, reverse, false),
                      "run_a=0 run_b=0")
                << "without identity isolation, one inline validator wins at link time";
        }
    }
}

TEST_F(GeneratedCodeIdentityTest, G4OlderGeneratorDependencyDiagnostics) {
    const char* older = std::getenv("QUARRY_IDENTITY_GENERATOR_B");
    if (older == nullptr || std::getenv("QUARRY_IDENTITY_OLDER") == nullptr) {
        GTEST_SKIP() << "requires an explicitly supplied older generator build";
    }
    const auto root = dep_root("older", kCommonSchema, kEngineSchema);
    for (const Variant* variant : {&kOwning, &kCStorage, &kPlainC}) {
        const std::string name(variant->name);
        const auto current = generate(*variant, root, name + "-current");
        const auto previous = work_ / (name + "-older");
        const auto old = run(quote(older) + " " + std::string(variant->arguments) + " -o " +
                                 quote(previous.string()) + " " +
                                 quote((root.parent_path() / "common.brd").string()),
                             work_ / "older.log");
        if (old.status == 2 && variant == &kCStorage &&
            old.output.find("unknown option --cpp-storage") != std::string::npos) {
            std::cout << "older generator does not support C-storage wrappers; not tested\n";
            continue;
        }
        ASSERT_EQ(old.status, 0) << old.output;
        const std::string common = "dep/common" + std::string(variant->extension);
        write_text_file(current / common, read_text_file(previous / common));
        const auto checked =
            syntax_check("#include \"dep/engine" + std::string(variant->extension) + "\"\n",
                         {current}, name + "-older-check", variant == &kPlainC);
        EXPECT_NE(checked.status, 0);
        EXPECT_NE(checked.output.find("predates generated"), std::string::npos) << checked.output;
    }
}

} // namespace
