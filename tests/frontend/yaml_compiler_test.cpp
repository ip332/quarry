#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/frontend/yaml_compiler.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/output_planning/output_planning.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/support/source_manager.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace context = quarry::compiler::context;
namespace output_planning = quarry::compiler::output_planning;

using quarry::compiler::context::CompilerContext;
using quarry::compiler::diagnostics::DiagnosticEngine;
using quarry::compiler::frontend::YamlCompilationResult;
using quarry::compiler::frontend::YamlCompiler;
using quarry::compiler::schema_ir::SchemaIrModel;
using quarry::compiler::support::SourceFileId;

[[nodiscard]] std::string diagnostics_text(const DiagnosticEngine& diagnostics,
                                           const CompilerContext& context) {
    return quarry::compiler::diagnostics::DiagnosticFormatter::format_all(diagnostics,
                                                                          context.source_manager());
}

[[nodiscard]] bool has_diagnostic(const DiagnosticEngine& diagnostics, std::string_view pass,
                                  std::string_view id) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.compiler_pass() == pass && diagnostic.id().str() == id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const quarry::schema_ir::NamespaceIR*
find_namespace(const quarry::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.namespaces_size(); ++index) {
        const auto& child = parent.namespaces(index);
        if (child.name() == name) {
            return &child;
        }
    }
    return nullptr;
}

[[nodiscard]] const quarry::schema_ir::RecordIR*
find_record(const quarry::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.records_size(); ++index) {
        const auto& record = parent.records(index);
        if (record.name() == name) {
            return &record;
        }
    }
    for (int index = 0; index < parent.namespaces_size(); ++index) {
        const auto* record = find_record(parent.namespaces(index), name);
        if (record != nullptr) {
            return record;
        }
    }
    return nullptr;
}

[[nodiscard]] const quarry::schema_ir::EnumIR*
find_enum(const quarry::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.enums_size(); ++index) {
        const auto& enumeration = parent.enums(index);
        if (enumeration.name() == name) {
            return &enumeration;
        }
    }
    for (int index = 0; index < parent.namespaces_size(); ++index) {
        const auto* enumeration = find_enum(parent.namespaces(index), name);
        if (enumeration != nullptr) {
            return enumeration;
        }
    }
    return nullptr;
}

[[nodiscard]] YamlCompilationResult compile_yaml(std::string text, CompilerContext& context,
                                                 DiagnosticEngine& diagnostics,
                                                 SourceFileId* source_file_id = nullptr) {
    const SourceFileId file_id =
        context.source_manager().add_source("/test/schema.yaml", std::move(text));
    if (source_file_id != nullptr) {
        *source_file_id = file_id;
    }
    YamlCompiler compiler;
    return compiler.compile(file_id, context, diagnostics);
}

[[nodiscard]] std::filesystem::path make_graph_test_directory(std::string_view name) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / std::string(name);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return directory;
}

void write_graph_test_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    ASSERT_TRUE(output.is_open());
    output << text;
    ASSERT_TRUE(output.good());
}

[[nodiscard]] YamlCompilationResult compile_graph_root(const std::filesystem::path& root,
                                                       CompilerContext& context,
                                                       DiagnosticEngine& diagnostics) {
    std::ifstream input(root);
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const SourceFileId source_file_id = context.source_manager().add_source(root.string(), text);
    return YamlCompiler{}.compile(source_file_id, context, diagnostics);
}

} // namespace

TEST(YamlCompilerTest, CompilesMinimalYamlToValidatedSchemaIr) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result = compile_yaml(
        R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  count:
    type: uint32
)",
        context, diagnostics);

    ASSERT_TRUE(result.succeeded());
    ASSERT_TRUE(diagnostics.empty());
    ASSERT_TRUE(result.schema_ir.has_value());

    const SchemaIrModel& schema_ir = *result.schema_ir;
    ASSERT_TRUE(schema_ir.has_root_namespace());
    const auto& root = schema_ir.root_namespace();
    const auto* quarry = find_namespace(root, "quarry");
    ASSERT_NE(quarry, nullptr);
    const auto* telemetry = find_namespace(*quarry, "telemetry");
    ASSERT_NE(telemetry, nullptr);
    ASSERT_EQ(telemetry->records_size(), 1);

    const auto& record = telemetry->records(0);
    EXPECT_EQ(record.name(), "Sample");
    EXPECT_EQ(record.schema_version(), 1U);
    EXPECT_TRUE(record.has_schema_version());
    EXPECT_TRUE(record.has_record_type());
    EXPECT_EQ(record.record_type(), quarry::schema_ir::RECORD_TYPE_DATA);
    EXPECT_EQ(record.record_id(), quarry::compiler::layout::compose_record_id(1U, 1U));
    ASSERT_EQ(record.fields_size(), 1);
    EXPECT_EQ(record.fields(0).name(), "count");
    EXPECT_EQ(record.fields(0).field_index(), 0U);
    ASSERT_TRUE(record.fields(0).type().has_primitive());
    EXPECT_EQ(record.fields(0).type().primitive(), quarry::schema_ir::PRIMITIVE_TYPE_U32);
}

TEST(YamlCompilerTest, CompilesEnumsStringsAndBoundedArrays) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result = compile_yaml(
        R"(namespace: quarry.telemetry
record: Sample
version: 1
type: event
enums:
  Mode:
    values:
      inactive: 0
      active: 1
  Status:
    values:
      ok: 0
      degraded: 1
fields:
  mode:
    type: Mode
  status:
    type: Status
  label:
    type: string
    max_bytes: 16
  samples:
    type: uint32[]
    max_elements: 64
)",
        context, diagnostics);

    ASSERT_TRUE(result.succeeded());
    ASSERT_TRUE(diagnostics.empty());
    ASSERT_TRUE(result.schema_ir.has_value());

    const SchemaIrModel& schema_ir = *result.schema_ir;
    const auto* quarry = find_namespace(schema_ir.root_namespace(), "quarry");
    ASSERT_NE(quarry, nullptr);
    const auto* telemetry = find_namespace(*quarry, "telemetry");
    ASSERT_NE(telemetry, nullptr);
    const auto* enumeration = find_enum(*telemetry, "Mode");
    ASSERT_NE(enumeration, nullptr);
    ASSERT_EQ(enumeration->values_size(), 2);
    EXPECT_EQ(enumeration->values(0).name(), "inactive");
    EXPECT_EQ(enumeration->values(0).value(), 0);
    EXPECT_EQ(enumeration->values(1).name(), "active");
    EXPECT_EQ(enumeration->values(1).value(), 1);
    const auto* status = find_enum(*telemetry, "Status");
    ASSERT_NE(status, nullptr);
    ASSERT_EQ(status->values_size(), 2);
    EXPECT_EQ(status->values(0).name(), "ok");
    EXPECT_EQ(status->values(0).value(), 0);
    EXPECT_EQ(status->values(1).name(), "degraded");
    EXPECT_EQ(status->values(1).value(), 1);

    const auto* record = find_record(*telemetry, "Sample");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->schema_version(), 1U);
    EXPECT_EQ(record->record_type(), quarry::schema_ir::RECORD_TYPE_EVENT);
    ASSERT_EQ(record->fields_size(), 4);
    EXPECT_TRUE(record->fields(0).type().has_enum_type());
    EXPECT_EQ(record->fields(0).type().enum_type().target_enum_ir_id(), enumeration->ir_id());
    EXPECT_TRUE(record->fields(1).type().has_enum_type());
    EXPECT_EQ(record->fields(1).type().enum_type().target_enum_ir_id(), status->ir_id());
    ASSERT_TRUE(record->fields(2).type().has_string());
    EXPECT_EQ(record->fields(2).type().string().max_bytes(), 16U);
    ASSERT_TRUE(record->fields(3).type().has_array());
    EXPECT_EQ(record->fields(3).type().array().max_elements(), 64U);
    ASSERT_TRUE(record->fields(3).type().array().element_type().has_primitive());
    EXPECT_EQ(record->fields(3).type().array().element_type().primitive(),
              quarry::schema_ir::PRIMITIVE_TYPE_U32);
}

TEST(YamlCompilerTest, StopsAfterYamlSyntaxFailure) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result =
        compile_yaml("namespace: quarry.telemetry\nrecord: Sample\nversion: 1\nfields: [\n",
                     context, diagnostics);

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(diagnostics.empty());
    EXPECT_TRUE(has_diagnostic(diagnostics, "yaml-parser", "BC2101"));
}

TEST(YamlCompilerTest, StopsAfterSchemaDecodeFailure) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result = compile_yaml(
        R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
)",
        context, diagnostics);

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(diagnostics.empty());
    EXPECT_TRUE(has_diagnostic(diagnostics, "yaml-schema-decoder", "BC2303"));
}

TEST(YamlCompilerTest, ReportsMissingImportedSourceUnit) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result = compile_yaml(
        R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
imports:
  - ./other.yaml
fields:
  count:
    type: uint32
)",
        context, diagnostics);

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(diagnostics.empty());
    EXPECT_TRUE(has_diagnostic(diagnostics, "source-unit-graph", "BC2404"));
}

TEST(YamlCompilerTest, LoadsTransitiveDiamondOnceInDeterministicOrder) {
    const std::filesystem::path directory =
        make_graph_test_directory("quarry_pr135_source_graph_diamond");
    const std::filesystem::path root = directory / "root.yaml";
    write_graph_test_file(root, R"(namespace: quarry.root
record: Root
version: 1
type: data
imports:
  - a.yaml
  - b.yaml
fields: {}
)");
    write_graph_test_file(directory / "a.yaml", R"(namespace: quarry.a
record: A
version: 1
type: data
imports:
  - shared.yaml
fields: {}
)");
    write_graph_test_file(directory / "b.yaml", R"(namespace: quarry.b
record: B
version: 1
type: data
imports:
  - ./nested/../shared.yaml
fields: {}
)");
    write_graph_test_file(directory / "shared.yaml", R"(namespace: quarry.shared
record: Shared
version: 1
type: data
fields: {}
)");

    CompilerContext context;
    DiagnosticEngine diagnostics;
    const YamlCompilationResult result = compile_graph_root(root, context, diagnostics);

    ASSERT_TRUE(result.succeeded());
    ASSERT_TRUE(diagnostics.empty());
    ASSERT_EQ(context.source_units().size(), 4U);
    ASSERT_TRUE(result.output_plan.has_value());
    ASSERT_EQ(result.output_plan->units.size(), 4U);
    ASSERT_EQ(result.output_plan->generation_order.size(), 4U);
    EXPECT_EQ(result.output_plan->generation_order[0], "quarry.shared.Shared");
    EXPECT_EQ(result.output_plan->generation_order[3], "quarry.root.Root");
    EXPECT_FALSE(result.output_plan->units[0].emits_output);
    EXPECT_TRUE(result.output_plan->units[3].emits_output);
    ASSERT_EQ(result.output_plan->units[3].dependency_identities.size(), 2U);
    EXPECT_EQ(result.output_plan->units[3].dependency_identities[0], "quarry.a.A");
    EXPECT_EQ(result.output_plan->units[3].dependency_identities[1], "quarry.b.B");
    EXPECT_EQ(context.source_units()[0].identity, "quarry.shared.Shared");
    EXPECT_EQ(context.source_units()[1].identity, "quarry.a.A");
    EXPECT_EQ(context.source_units()[2].identity, "quarry.b.B");
    EXPECT_EQ(context.source_units()[3].identity, "quarry.root.Root");
    EXPECT_TRUE(context.source_units()[3].is_root);
    EXPECT_EQ(context.source_units()[0].source_file_id,
              context.find_source_unit_by_identity("quarry.shared.Shared")->source_file_id);

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

TEST(YamlCompilerTest, LowersImportedDeclarationsIntoOneResolvedSchemaIr) {
    const std::filesystem::path directory =
        make_graph_test_directory("quarry_pr138_schema_ir_dependencies");
    const std::filesystem::path root = directory / "root.yaml";
    write_graph_test_file(directory / "shared.yaml", R"(namespace: quarry.shared
record: Shared
version: 1
type: data
fields:
  value:
    type: uint32
enums:
  Mode:
    values:
      Off: 0
      On: 1
)");
    write_graph_test_file(root, R"(namespace: quarry.root
record: Root
version: 1
type: data
imports:
  - shared.yaml
fields:
  child:
    type: quarry.shared.Shared
  mode:
    type: quarry.shared.Mode
  children:
    type: quarry.shared.Shared[]
    max_elements: 2
  modes:
    type: quarry.shared.Mode[]
    max_elements: 2
)");

    CompilerContext context;
    DiagnosticEngine diagnostics;
    const YamlCompilationResult result = compile_graph_root(root, context, diagnostics);

    ASSERT_TRUE(result.succeeded()) << diagnostics.diagnostics().size();
    ASSERT_TRUE(diagnostics.empty());
    ASSERT_TRUE(result.schema_ir.has_value());
    const auto* root_namespace = find_namespace(result.schema_ir->root_namespace(), "quarry");
    ASSERT_NE(root_namespace, nullptr);
    const auto* shared_namespace = find_namespace(*root_namespace, "shared");
    const auto* root_namespace_child = find_namespace(*root_namespace, "root");
    ASSERT_NE(shared_namespace, nullptr);
    ASSERT_NE(root_namespace_child, nullptr);
    const auto* shared_record = find_record(*shared_namespace, "Shared");
    const auto* shared_enum = find_enum(*shared_namespace, "Mode");
    const auto* root_record = find_record(*root_namespace_child, "Root");
    ASSERT_NE(shared_record, nullptr);
    ASSERT_NE(shared_enum, nullptr);
    ASSERT_NE(root_record, nullptr);
    ASSERT_EQ(root_record->fields_size(), 4);
    EXPECT_EQ(root_record->fields(0).type().record().target_record_ir_id(),
              shared_record->ir_id());
    EXPECT_EQ(root_record->fields(1).type().enum_type().target_enum_ir_id(), shared_enum->ir_id());
    EXPECT_EQ(root_record->fields(2).type().array().element_type().record().target_record_ir_id(),
              shared_record->ir_id());
    EXPECT_EQ(root_record->fields(3).type().array().element_type().enum_type().target_enum_ir_id(),
              shared_enum->ir_id());

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

TEST(YamlCompilerTest, RejectsImportCycleWithCycleDiagnostic) {
    const std::filesystem::path directory =
        make_graph_test_directory("quarry_pr135_source_graph_cycle");
    const std::filesystem::path root = directory / "root.yaml";
    write_graph_test_file(root, R"(namespace: quarry.root
record: Root
version: 1
type: data
imports:
  - a.yaml
fields: {}
)");
    write_graph_test_file(directory / "a.yaml", R"(namespace: quarry.a
record: A
version: 1
type: data
imports:
  - b.yaml
fields: {}
)");
    write_graph_test_file(directory / "b.yaml", R"(namespace: quarry.b
record: B
version: 1
type: data
imports:
  - a.yaml
fields: {}
)");

    CompilerContext context;
    DiagnosticEngine diagnostics;
    const YamlCompilationResult result = compile_graph_root(root, context, diagnostics);

    EXPECT_FALSE(result.succeeded());
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_TRUE(has_diagnostic(diagnostics, "source-unit-graph", "BC2405"));
    const auto cycle_diagnostic = std::find_if(
        diagnostics.diagnostics().begin(), diagnostics.diagnostics().end(), [](const auto& item) {
            return item.compiler_pass() == "source-unit-graph" && item.id().str() == "BC2405";
        });
    ASSERT_NE(cycle_diagnostic, diagnostics.diagnostics().end());
    EXPECT_NE(cycle_diagnostic->message().find("a.yaml"), std::string::npos);
    EXPECT_NE(cycle_diagnostic->message().find("b.yaml"), std::string::npos);

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

TEST(YamlCompilerTest, RejectsDuplicateSourceUnitIdentity) {
    const std::filesystem::path directory =
        make_graph_test_directory("quarry_pr135_source_graph_duplicate");
    const std::filesystem::path root = directory / "root.yaml";
    write_graph_test_file(root, R"(namespace: quarry.root
record: Root
version: 1
type: data
imports:
  - first.yaml
  - second.yaml
fields: {}
)");
    const std::string duplicate = R"(namespace: quarry.duplicate
record: Same
version: 1
type: data
fields: {}
)";
    write_graph_test_file(directory / "first.yaml", duplicate);
    write_graph_test_file(directory / "second.yaml", duplicate);

    CompilerContext context;
    DiagnosticEngine diagnostics;
    const YamlCompilationResult result = compile_graph_root(root, context, diagnostics);

    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(has_diagnostic(diagnostics, "source-unit-graph", "BC2406"));

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

TEST(YamlCompilerTest, StopsAfterSemanticFailure) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result = compile_yaml(
        R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  missing:
    type: NotAType
)",
        context, diagnostics);

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(diagnostics.empty());
    EXPECT_TRUE(has_diagnostic(diagnostics, "semantic", "BC5001"));
}

TEST(YamlCompilerTest, AggregatesSourceUnitsOfOneNamespaceIntoOnePlannedOutput) {
    // Output is planned per namespace: two files contributing records to one
    // namespace share its generated output instead of colliding.
    context::CompilerContext context;
    context::SourceUnit first;
    first.canonical_path = "/test/first.yaml";
    first.identity = "vehicle.Telemetry";
    first.namespace_fqn = "vehicle";
    first.is_root = true;
    ASSERT_TRUE(context.register_source_unit(std::move(first)));

    context::SourceUnit second;
    second.canonical_path = "/test/second.yaml";
    second.identity = "vehicle.Configuration";
    second.namespace_fqn = "vehicle";
    ASSERT_TRUE(context.register_source_unit(std::move(second)));

    DiagnosticEngine diagnostics;
    const output_planning::OutputPlan plan =
        output_planning::OutputPlanner{}.plan(context, diagnostics);

    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(plan.units.size(), 2U);
    EXPECT_TRUE(plan.units[0].emits_output);
    EXPECT_TRUE(plan.units[1].emits_output);
    ASSERT_EQ(plan.namespaces.size(), 1U);
    EXPECT_EQ(plan.namespaces[0].namespace_fqn, "vehicle");
    EXPECT_TRUE(plan.namespaces[0].emits_output);
    EXPECT_EQ(plan.namespaces[0].source_unit_identities,
              (std::vector<std::string>{"vehicle.Telemetry", "vehicle.Configuration"}));
}

namespace {

// A small configuration: two unrelated components (one unreferenced by the
// other), a shared leaf imported by both (diamond), and a second file in the
// leaf's namespace.
[[nodiscard]] std::filesystem::path write_configuration_fixture(std::string_view name) {
    const std::filesystem::path directory = make_graph_test_directory(name);
    write_graph_test_file(directory / "leaf.yaml", R"(namespace: cfg.leaf
record: Leaf
version: 1
type: data
fields:
  v:
    type: u32
)");
    write_graph_test_file(directory / "leaf_extra.yaml", R"(namespace: cfg.leaf
record: Extra
version: 1
type: data
fields:
  w:
    type: u8
)");
    write_graph_test_file(directory / "a.yaml", R"(namespace: cfg.a
record: A
version: 1
type: data
imports:
  - leaf.yaml
fields:
  leaf:
    type: cfg.leaf.Leaf
)");
    write_graph_test_file(directory / "b.yaml", R"(namespace: cfg.b
record: B
version: 1
type: data
imports:
  - leaf.yaml
fields:
  leaves:
    type: cfg.leaf.Leaf[]
    max_elements: 2
)");
    write_graph_test_file(directory / "config.yaml", R"(configuration: fleet
imports:
  - a.yaml
  - b.yaml
  - leaf_extra.yaml
)");
    return directory;
}

void collect_record_ids(const quarry::schema_ir::NamespaceIR& ns,
                        std::map<std::string, std::uint32_t>& ids) {
    for (const auto& record : ns.records()) {
        ids[record.fqn()] = record.record_id();
    }
    for (const auto& child : ns.namespaces()) {
        collect_record_ids(child, ids);
    }
}

[[nodiscard]] YamlCompilationResult
compile_graph_root_selecting(const std::filesystem::path& root, CompilerContext& context,
                             DiagnosticEngine& diagnostics, std::vector<std::string> namespaces) {
    std::ifstream input(root);
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const SourceFileId source_file_id = context.source_manager().add_source(root.string(), text);
    return YamlCompiler{}.compile(source_file_id, context, diagnostics,
                                  output_planning::GenerationSelection{.namespaces = namespaces});
}

[[nodiscard]] std::vector<std::string> emitted_namespaces(const output_planning::OutputPlan& plan) {
    std::vector<std::string> emitted;
    for (const auto& planned : plan.namespaces) {
        if (planned.emits_output) {
            emitted.push_back(planned.namespace_fqn);
        }
    }
    return emitted;
}

} // namespace

TEST(YamlCompilerTest, ConfigurationRootCompilesItsCompleteImportClosure) {
    const std::filesystem::path directory =
        write_configuration_fixture("quarry_a3_configuration_root");
    CompilerContext context;
    DiagnosticEngine diagnostics;
    const YamlCompilationResult result =
        compile_graph_root(directory / "config.yaml", context, diagnostics);
    ASSERT_TRUE(result.succeeded()) << diagnostics_text(diagnostics, context);
    ASSERT_TRUE(result.output_plan.has_value());
    EXPECT_TRUE(result.output_plan->root_is_configuration);

    // The configuration declares nothing: no namespace of its own, no domain.
    // Every imported record, referenced or not, has an ID.
    using quarry::compiler::layout::compose_record_id;
    std::map<std::string, std::uint32_t> ids;
    collect_record_ids(result.schema_ir->root_namespace(), ids);
    EXPECT_EQ(ids, (std::map<std::string, std::uint32_t>{
                       {"cfg.a.A", compose_record_id(1U, 1U)},
                       {"cfg.b.B", compose_record_id(2U, 1U)},
                       {"cfg.leaf.Extra", compose_record_id(3U, 1U)},
                       {"cfg.leaf.Leaf", compose_record_id(3U, 2U)},
                   }));

    // Default for a configuration root: every namespace of the closure.
    EXPECT_EQ(emitted_namespaces(*result.output_plan),
              (std::vector<std::string>{"cfg.a", "cfg.b", "cfg.leaf"}));
    // The configuration unit is not an output unit; the leaf namespace
    // aggregates both of its files.
    for (const auto& unit : result.output_plan->units) {
        EXPECT_NE(unit.canonical_path, (directory / "config.yaml").string());
    }
    ASSERT_EQ(result.output_plan->namespaces.size(), 3U);
    EXPECT_EQ(result.output_plan->namespaces[2].source_unit_identities.size(), 2U);
}

TEST(YamlCompilerTest, GenerationSelectionNeverChangesSchemaIr) {
    const std::filesystem::path directory =
        write_configuration_fixture("quarry_a3_selection_schema_ir");
    std::optional<std::string> baseline;
    for (const std::vector<std::string>& selection : std::vector<std::vector<std::string>>{
             {}, {"cfg.a"}, {"cfg.leaf", "cfg.b"}, {"cfg.a", "cfg.b", "cfg.leaf"}}) {
        CompilerContext context;
        DiagnosticEngine diagnostics;
        const YamlCompilationResult result = compile_graph_root_selecting(
            directory / "config.yaml", context, diagnostics, selection);
        ASSERT_TRUE(result.succeeded()) << diagnostics_text(diagnostics, context);
        if (!selection.empty()) {
            std::vector<std::string> expected = selection;
            std::sort(expected.begin(), expected.end());
            EXPECT_EQ(emitted_namespaces(*result.output_plan), expected);
        }
        const std::string serialized = result.schema_ir->SerializeAsString();
        if (!baseline) {
            baseline = serialized;
        }
        EXPECT_EQ(serialized, *baseline) << "selection changed Schema IR";
    }
}

TEST(YamlCompilerTest, OutputPlanningIsDeterministic) {
    const std::filesystem::path directory =
        write_configuration_fixture("quarry_a3_plan_determinism");
    const auto plan_once = [&]() {
        CompilerContext context;
        DiagnosticEngine diagnostics;
        const YamlCompilationResult result = compile_graph_root_selecting(
            directory / "config.yaml", context, diagnostics, {"cfg.b", "cfg.leaf"});
        EXPECT_TRUE(result.succeeded());
        std::string text;
        for (const auto& planned : result.output_plan->namespaces) {
            text += planned.namespace_fqn + (planned.emits_output ? "+" : "-") + ":";
            for (const auto& identity : planned.source_unit_identities) {
                text += identity + ",";
            }
            text += "\n";
        }
        for (const auto& identity : result.output_plan->generation_order) {
            text += identity + "\n";
        }
        return text;
    };
    EXPECT_EQ(plan_once(), plan_once());
}

TEST(YamlCompilerTest, RecordRootDefaultsToItsOwnNamespace) {
    const std::filesystem::path directory =
        write_configuration_fixture("quarry_a3_record_root_default");
    CompilerContext context;
    DiagnosticEngine diagnostics;
    const YamlCompilationResult result =
        compile_graph_root(directory / "a.yaml", context, diagnostics);
    ASSERT_TRUE(result.succeeded()) << diagnostics_text(diagnostics, context);
    EXPECT_FALSE(result.output_plan->root_is_configuration);
    EXPECT_EQ(emitted_namespaces(*result.output_plan), (std::vector<std::string>{"cfg.a"}));
}

TEST(YamlCompilerTest, RejectsUnknownGenerationSelection) {
    const std::filesystem::path directory =
        write_configuration_fixture("quarry_a3_unknown_selection");
    CompilerContext context;
    DiagnosticEngine diagnostics;
    const YamlCompilationResult result = compile_graph_root_selecting(
        directory / "config.yaml", context, diagnostics, {"cfg.a", "cfg"});
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(has_diagnostic(diagnostics, "output-planning", "BC8002"));
}

TEST(YamlCompilerTest, RejectsInvalidConfigurationFiles) {
    struct Case {
        std::string_view text;
        std::string_view pass;
        std::string_view id;
    };
    const std::vector<Case> cases = {
        {"configuration: fleet\nnamespace: cfg\nimports:\n  - a.yaml\n", "yaml-schema-decoder",
         "BC2307"},
        {"configuration: fleet\nrecord: R\nimports:\n  - a.yaml\n", "yaml-schema-decoder",
         "BC2307"},
        {"configuration: fleet\nversion: 1\nimports:\n  - a.yaml\n", "yaml-schema-decoder",
         "BC2307"},
        {"configuration: fleet\ntype: data\nimports:\n  - a.yaml\n", "yaml-schema-decoder",
         "BC2307"},
        {"configuration: fleet\nimports:\n  - a.yaml\nfields:\n  x:\n    type: u8\n",
         "yaml-schema-decoder", "BC2307"},
        {"configuration: fleet\nimports:\n  - a.yaml\nenums:\n  E:\n    values:\n      A: 0\n",
         "yaml-schema-decoder", "BC2307"},
        {"configuration: fleet\n", "yaml-schema-decoder", "BC2308"},
        {"configuration: fleet\nimports: []\n", "yaml-schema-decoder", "BC2308"},
        {"configuration: 9fleet\nimports:\n  - a.yaml\n", "source-schema-normalization", "BC2401"},
        {"configuration: fleet\nconfiguration: other\nimports:\n  - a.yaml\n",
         "yaml-schema-decoder", "BC2304"},
    };
    const std::filesystem::path directory =
        write_configuration_fixture("quarry_a3_invalid_configurations");
    for (const Case& test_case : cases) {
        write_graph_test_file(directory / "bad.yaml", test_case.text);
        CompilerContext context;
        DiagnosticEngine diagnostics;
        const YamlCompilationResult result =
            compile_graph_root(directory / "bad.yaml", context, diagnostics);
        EXPECT_FALSE(result.succeeded()) << test_case.text;
        EXPECT_TRUE(has_diagnostic(diagnostics, test_case.pass, test_case.id))
            << test_case.text << diagnostics_text(diagnostics, context);
    }
}

TEST(YamlCompilerTest, RejectsImportedConfigurationFile) {
    const std::filesystem::path directory =
        write_configuration_fixture("quarry_a3_imported_configuration");
    write_graph_test_file(directory / "outer.yaml", R"(namespace: cfg.outer
record: Outer
version: 1
type: data
imports:
  - config.yaml
fields:
  x:
    type: u8
)");
    CompilerContext context;
    DiagnosticEngine diagnostics;
    const YamlCompilationResult result =
        compile_graph_root(directory / "outer.yaml", context, diagnostics);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(has_diagnostic(diagnostics, "source-unit-graph", "BC2408"));
}
