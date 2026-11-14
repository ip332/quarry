// Hierarchical record IDs (A2): one authoritative mapping per root compilation.
//
// record_id = (domain_id << 16) | local_record_id, where domains are the
// namespaces declaring records, numbered by sorted namespace FQN, and local IDs
// number each namespace's records by sorted record name. The layout pass
// assigns the mapping once for the complete import closure; Schema IR, QBS and
// every backend consume it.
//
// The configuration below reproduces the original Top -> Mid -> Leaf chain,
// adds a diamond (b.Other also imports z.Leaf), a second file contributing to
// namespace z (Aardvark), and an imported but unreferenced record (c.Unused).
// Generating every namespace from ONE root compilation, all backends and QBS
// must embed exactly the expected IDs, for top-level and nested records alike.
//
// Generating dependencies through separate compiler runs (the current CLI
// workflow) defines different configurations; making that workflow consistent
// is A3 (root-wide generation), not part of this test.

#include "compiler/backend/backend.hpp"
#include "compiler/backend_c/backend_c.hpp"
#include "compiler/backend_go/backend_go.hpp"
#include "compiler/backend_python/backend_python.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/frontend/yaml_compiler.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/qbs/parser.hpp"
#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;
using quarry::compiler::layout::compose_record_id;
using IdMap = std::map<std::string, std::uint32_t>;

// Domains: a=1, b=2, c=3, m=4, z=5. Namespace z: Aardvark=1, Leaf=2.
const IdMap kExpected = {
    {"a.Top", compose_record_id(1U, 1U)},      {"b.Other", compose_record_id(2U, 1U)},
    {"c.Unused", compose_record_id(3U, 1U)},   {"m.Mid", compose_record_id(4U, 1U)},
    {"z.Aardvark", compose_record_id(5U, 1U)}, {"z.Leaf", compose_record_id(5U, 2U)},
};

constexpr std::string_view kLeaf = "namespace: z\nrecord: Leaf\nversion: 1\ntype: data\n"
                                   "fields:\n  v:\n    type: u32\n";
constexpr std::string_view kAardvark = "namespace: z\nrecord: Aardvark\nversion: 1\ntype: data\n"
                                       "fields:\n  w:\n    type: u8\n";
constexpr std::string_view kMid = "namespace: m\nrecord: Mid\nversion: 1\ntype: data\n"
                                  "imports:\n  - leaf.brd\nfields:\n  leaf:\n    type: z.Leaf\n";
constexpr std::string_view kOther = "namespace: b\nrecord: Other\nversion: 1\ntype: data\n"
                                    "imports:\n  - leaf.brd\nfields:\n  leaf:\n    type: z.Leaf\n"
                                    "  leaves:\n    type: z.Leaf[]\n    max_elements: 2\n";
constexpr std::string_view kUnused = "namespace: c\nrecord: Unused\nversion: 1\ntype: data\n"
                                     "fields:\n  u:\n    type: bool\n";

[[nodiscard]] std::string top_schema(const std::vector<std::string>& imports) {
    std::string text = "namespace: a\nrecord: Top\nversion: 1\ntype: data\nimports:\n";
    for (const std::string& import : imports) {
        text += "  - " + import + "\n";
    }
    return text + "fields:\n  mid:\n    type: m.Mid\n  other:\n    type: b.Other\n";
}

void write_text(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << text;
}

class HierarchicalRecordIdTest : public ::testing::Test {
protected:
    fs::path root;

    void SetUp() override {
        root = fs::temp_directory_path() /
               ("quarry-hierarchical-ids-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        write_text(root / "leaf.brd", kLeaf);
        write_text(root / "aardvark.brd", kAardvark);
        write_text(root / "mid.brd", kMid);
        write_text(root / "other.brd", kOther);
        write_text(root / "unused.brd", kUnused);
        write_text(root / "top.brd",
                   top_schema({"mid.brd", "other.brd", "aardvark.brd", "unused.brd"}));
        // Same configuration, imports in a different order (aardvark.brd before
        // the leaf's importers, so namespace z is first reached through it).
        write_text(root / "top_reordered.brd",
                   top_schema({"unused.brd", "aardvark.brd", "other.brd", "mid.brd"}));
    }

    [[nodiscard]] std::optional<quarry::compiler::schema_ir::SchemaIrModel>
    compile(const std::string& file, std::string* error = nullptr) const {
        quarry::compiler::context::CompilerContext context;
        quarry::compiler::diagnostics::DiagnosticEngine diagnostics;
        std::ifstream input(root / file);
        const std::string text{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
        const auto id = context.source_manager().add_source((root / file).string(), text);
        auto result = quarry::compiler::frontend::YamlCompiler{}.compile(id, context, diagnostics);
        if (error != nullptr) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                *error += diagnostic.id().str() + ": " + diagnostic.message() + "\n";
            }
        }
        return result.schema_ir;
    }
};

void collect_ids(const quarry::schema_ir::NamespaceIR& ns, IdMap& ids) {
    for (const auto& record : ns.records()) {
        ids[record.fqn()] = record.record_id();
    }
    for (const auto& child : ns.namespaces()) {
        collect_ids(child, ids);
    }
}

[[nodiscard]] IdMap schema_ir_ids(const quarry::compiler::schema_ir::SchemaIrModel& ir) {
    IdMap ids;
    collect_ids(ir.root_namespace(), ids);
    return ids;
}

struct QbsResult {
    std::vector<std::uint8_t> bytes;
    IdMap ids;
    std::array<std::uint8_t, 16> schema_id{};
};

[[nodiscard]] QbsResult build_qbs(const quarry::compiler::schema_ir::SchemaIrModel& ir) {
    QbsResult out;
    quarry::compiler::diagnostics::DiagnosticEngine diagnostics;
    const auto layout = quarry::compiler::layout::LayoutComputer{}.compute(ir, diagnostics);
    EXPECT_TRUE(diagnostics.empty());
    const auto model = quarry::compiler::qbs::QbsModelBuilder{}.build(
        ir, layout, quarry::compiler::qbs::QbsBuildOptions{}, diagnostics);
    EXPECT_TRUE(model.has_value());
    if (!model) {
        return out;
    }
    const auto serialized = quarry::compiler::qbs::serialize_qbs(*model, diagnostics);
    EXPECT_TRUE(serialized.has_value());
    if (!serialized) {
        return out;
    }
    out.bytes = serialized->bytes;
    // The parser validates the image, including the recomputed schema_id.
    const auto view = quarry::compiler::qbs::parse_qbs(out.bytes, diagnostics);
    EXPECT_TRUE(view.has_value());
    if (!view) {
        return out;
    }
    out.schema_id = view->header().schema_id;
    for (std::size_t i = 0; i < view->record_count(); ++i) {
        const auto record = view->record(i);
        out.ids[std::string(record.identity)] = record.record_id;
    }
    return out;
}

[[nodiscard]] std::string fqn_for_symbol(std::string_view symbol) {
    for (const auto& [fqn, id] : kExpected) {
        std::string candidate = fqn;
        std::replace(candidate.begin(), candidate.end(), '.', '_');
        if (candidate == symbol) {
            return fqn;
        }
    }
    return "<unknown:" + std::string(symbol) + ">";
}

[[nodiscard]] std::string fqn_for_name(std::string_view name) {
    // Go names nested encode/decode calls after the field; map each nested
    // field of the configuration to the record it references.
    static const std::map<std::string, std::string, std::less<>> kNestedFields = {
        {"Leaf", "z.Leaf"}, {"Leaves", "z.Leaf"}, {"Mid", "m.Mid"}, {"Other", "b.Other"}};
    if (const auto field = kNestedFields.find(name); field != kNestedFields.end()) {
        return field->second;
    }
    for (const auto& [fqn, id] : kExpected) {
        const std::string local = fqn.substr(fqn.rfind('.') + 1U);
        std::string lower = local;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (local == name || lower == name) {
            return fqn;
        }
    }
    return "<unknown:" + std::string(name) + ">";
}

// Every (fqn, id) occurrence found in generated text.
using Occurrences = std::vector<std::pair<std::string, std::uint32_t>>;

void scan(const std::string& text, const std::regex& pattern, bool by_symbol, Occurrences& out) {
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern);
         it != std::sregex_iterator(); ++it) {
        const std::string key = (*it)[1].str();
        out.emplace_back(by_symbol ? fqn_for_symbol(key) : fqn_for_name(key),
                         static_cast<std::uint32_t>(std::stoul((*it)[2].str())));
    }
}

template <typename Files> [[nodiscard]] std::string concatenate(const Files& files) {
    std::string all;
    for (const auto& file : files) {
        all += file.content + "\n";
    }
    return all;
}

[[nodiscard]] std::map<std::string, Occurrences>
backend_ids(const quarry::compiler::schema_ir::SchemaIrModel& ir) {
    std::map<std::string, Occurrences> result;
    {
        quarry::compiler::backend_c::CodegenOptions options;
        const auto generated = quarry::compiler::backend_c::Backend{}.generate(ir, options);
        EXPECT_TRUE(generated.success) << generated.error_message;
        const std::string text = concatenate(generated.files);
        scan(text, std::regex(R"((\w+)_brf_v2_layout = \{ (\d+)U)"), true, result["C"]);
        scan(text, std::regex(R"(&(\w+)_type, (\d+)U)"), true, result["C"]); // nested
    }
    {
        quarry::compiler::backend_c::CodegenOptions options;
        options.cpp_wrapper = true;
        const auto generated = quarry::compiler::backend_c::Backend{}.generate(ir, options);
        EXPECT_TRUE(generated.success) << generated.error_message;
        const std::string text = concatenate(generated.files);
        scan(text, std::regex(R"((\w+)_brf_v2_layout = \{ (\d+)U)"), true, result["C++/C-storage"]);
        scan(text, std::regex(R"(&(\w+)_type, (\d+)U)"), true, result["C++/C-storage"]);
    }
    {
        const auto generated = quarry::compiler::backend::Backend{}.generate(
            ir, quarry::compiler::backend::CodegenOptions{});
        EXPECT_TRUE(generated.success) << generated.error_message;
        const std::string text = concatenate(generated.files);
        scan(
            text,
            std::regex(
                R"(brf_v2_layout_(\w+)\(\) \{\s*static const ::quarry::runtime::BrfV2RecordLayout layout\{\s*\.record_id = (\d+)U)"),
            false, result["C++"]);
        scan(
            text,
            std::regex(
                R"(decode_(\w+)_result\(std::span<const std::byte> input\) \{[^}]*?record_id != (\d+)U)"),
            false, result["C++"]);
    }
    {
        const auto generated = quarry::compiler::backend_python::Backend{}.generate(
            ir, quarry::compiler::backend_python::CodegenOptions{});
        EXPECT_TRUE(generated.success) << generated.error_message;
        const std::string text = concatenate(generated.files);
        scan(text,
             std::regex(R"(def _quarry_encode_(\w+)\(value\):[\s\S]*?encode_record_v2\((\d+),)"),
             false, result["Python"]);
        scan(
            text,
            std::regex(
                R"(def _quarry_decode_(\w+)\(data\):\s*fields = _brf\.parse_record_v2\(data, (\d+),)"),
            false, result["Python"]);
    }
    {
        quarry::compiler::backend_go::CodegenOptions options;
        options.go_import_prefix = "example.com/ids";
        const auto generated = quarry::compiler::backend_go::Backend{}.generate(ir, options);
        EXPECT_TRUE(generated.success) << generated.error_message;
        const std::string text = concatenate(generated.files);
        scan(text, std::regex(R"(const (\w+)RecordID = (\d+))"), false, result["Go"]);
        // Nested encode/decode calls name the field, which matches the record here.
        scan(text, std::regex(R"(m\.(\w+)(?:\[i\])?\.AppendBRFWithRecordID\(b, (\d+)\))"), false,
             result["Go"]);
        scan(text, std::regex(R"(m\.(\w+)(?:\[i\])?\.UnmarshalBRFWithRecordID\(.*, (\d+)\); err)"),
             false, result["Go"]);
    }
    return result;
}

TEST_F(HierarchicalRecordIdTest, EveryBackendAndQbsUseTheRootCompilationsMapping) {
    const auto ir = compile("top.brd");
    ASSERT_TRUE(ir.has_value());
    // Unreferenced imported records (z.Aardvark, c.Unused) get IDs too.
    EXPECT_EQ(schema_ir_ids(*ir), kExpected);
    EXPECT_EQ(build_qbs(*ir).ids, kExpected);

    const auto per_backend = backend_ids(*ir);
    for (const std::string language : {"C", "C++/C-storage", "C++", "Python", "Go"}) {
        ASSERT_TRUE(per_backend.contains(language)) << language;
        const Occurrences& occurrences = per_backend.at(language);
        std::set<std::string> seen;
        for (const auto& [fqn, id] : occurrences) {
            ASSERT_TRUE(kExpected.contains(fqn)) << language << ": " << fqn;
            EXPECT_EQ(id, kExpected.at(fqn)) << language << " embeds a different ID for " << fqn;
            seen.insert(fqn);
        }
        // Every record of the configuration appears in every backend's output.
        EXPECT_EQ(seen.size(), kExpected.size()) << language;
    }
    // Nested references to the shared leaf (via m.Mid, b.Other and its array)
    // all use the same ID in the backends that embed nested IDs.
    for (const std::string language : {"C", "Go"}) {
        int leaf_references = 0;
        for (const auto& [fqn, id] : per_backend.at(language)) {
            leaf_references += fqn == "z.Leaf" ? 1 : 0;
        }
        EXPECT_GE(leaf_references, 3) << language;
    }
}

TEST_F(HierarchicalRecordIdTest, ImportOrderDoesNotAffectIdsOrSchemaId) {
    const auto forward = compile("top.brd");
    const auto reordered = compile("top_reordered.brd");
    ASSERT_TRUE(forward.has_value());
    ASSERT_TRUE(reordered.has_value());
    EXPECT_EQ(schema_ir_ids(*reordered), kExpected);
    const QbsResult a = build_qbs(*forward);
    const QbsResult b = build_qbs(*reordered);
    EXPECT_EQ(a.ids, b.ids);
    EXPECT_EQ(a.schema_id, b.schema_id);
}

TEST_F(HierarchicalRecordIdTest, RepeatedCompilationIsDeterministic) {
    const auto first = compile("top.brd");
    const auto second = compile("top.brd");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(build_qbs(*first).bytes, build_qbs(*second).bytes);
    const auto generated_first = quarry::compiler::backend::Backend{}.generate(
        *first, quarry::compiler::backend::CodegenOptions{});
    const auto generated_second = quarry::compiler::backend::Backend{}.generate(
        *second, quarry::compiler::backend::CodegenOptions{});
    EXPECT_EQ(concatenate(generated_first.files), concatenate(generated_second.files));
    quarry::compiler::backend_go::CodegenOptions go;
    go.go_import_prefix = "example.com/ids";
    EXPECT_EQ(concatenate(quarry::compiler::backend_go::Backend{}.generate(*first, go).files),
              concatenate(quarry::compiler::backend_go::Backend{}.generate(*second, go).files));
}

TEST_F(HierarchicalRecordIdTest, TheRootCompilationDefinesTheConfiguration) {
    // Compiling a component as the root is valid but is a different
    // configuration: only namespaces m and z exist, so the IDs and schema_id
    // differ from the complete truck-style configuration above.
    const auto component = compile("mid.brd");
    const auto configuration = compile("top.brd");
    ASSERT_TRUE(component.has_value());
    ASSERT_TRUE(configuration.has_value());
    const IdMap expected_component = {{"m.Mid", compose_record_id(1U, 1U)},
                                      {"z.Leaf", compose_record_id(2U, 1U)}};
    EXPECT_EQ(schema_ir_ids(*component), expected_component);
    EXPECT_NE(build_qbs(*component).schema_id, build_qbs(*configuration).schema_id);
}

TEST_F(HierarchicalRecordIdTest, DuplicateRecordNameAcrossFilesInOneNamespaceIsRejected) {
    write_text(root / "leaf_again.brd", "namespace: z\nrecord: Leaf\nversion: 1\ntype: data\n"
                                        "fields:\n  x:\n    type: u8\n");
    write_text(root / "dup.brd", "namespace: d\nrecord: Dup\nversion: 1\ntype: data\nimports:\n"
                                 "  - leaf.brd\n  - leaf_again.brd\nfields:\n  f:\n    type: u8\n");
    std::string error;
    EXPECT_FALSE(compile("dup.brd", &error).has_value());
    EXPECT_NE(error.find("z.Leaf"), std::string::npos) << error;
}

} // namespace
