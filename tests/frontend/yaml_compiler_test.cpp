#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/frontend/yaml_compiler.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/support/source_manager.hpp"

#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::context::CompilerContext;
using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::frontend::YamlCompilationResult;
using breadcrumbs::compiler::frontend::YamlCompiler;
using breadcrumbs::compiler::schema_ir::SchemaIrModel;
using breadcrumbs::compiler::support::SourceFileId;

[[nodiscard]] bool has_diagnostic(const DiagnosticEngine& diagnostics, std::string_view pass,
                                  std::string_view id) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.compiler_pass() == pass && diagnostic.id().str() == id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const breadcrumbs::schema_ir::NamespaceIR*
find_namespace(const breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.namespaces_size(); ++index) {
        const auto& child = parent.namespaces(index);
        if (child.name() == name) {
            return &child;
        }
    }
    return nullptr;
}

[[nodiscard]] const breadcrumbs::schema_ir::RecordIR*
find_record(const breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
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

[[nodiscard]] const breadcrumbs::schema_ir::EnumIR*
find_enum(const breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
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

} // namespace

TEST(YamlCompilerTest, CompilesMinimalYamlToValidatedSchemaIr) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result = compile_yaml(
        R"(namespace: breadcrumbs.telemetry
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
    const auto* breadcrumbs = find_namespace(root, "breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    const auto* telemetry = find_namespace(*breadcrumbs, "telemetry");
    ASSERT_NE(telemetry, nullptr);
    ASSERT_EQ(telemetry->records_size(), 1);

    const auto& record = telemetry->records(0);
    EXPECT_EQ(record.name(), "Sample");
    EXPECT_EQ(record.schema_version(), 1U);
    EXPECT_TRUE(record.has_schema_version());
    EXPECT_TRUE(record.has_record_type());
    EXPECT_EQ(record.record_type(), breadcrumbs::schema_ir::RECORD_TYPE_DATA);
    EXPECT_EQ(record.record_id(), 1U);
    ASSERT_EQ(record.fields_size(), 1);
    EXPECT_EQ(record.fields(0).name(), "count");
    EXPECT_EQ(record.fields(0).field_index(), 0U);
    ASSERT_TRUE(record.fields(0).type().has_primitive());
    EXPECT_EQ(record.fields(0).type().primitive(), breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);
}

TEST(YamlCompilerTest, CompilesEnumsStringsAndBoundedArrays) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result = compile_yaml(
        R"(namespace: breadcrumbs.telemetry
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
    const auto* breadcrumbs = find_namespace(schema_ir.root_namespace(), "breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    const auto* telemetry = find_namespace(*breadcrumbs, "telemetry");
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
    EXPECT_EQ(record->record_type(), breadcrumbs::schema_ir::RECORD_TYPE_EVENT);
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
              breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);
}

TEST(YamlCompilerTest, StopsAfterYamlSyntaxFailure) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result =
        compile_yaml("namespace: breadcrumbs.telemetry\nrecord: Sample\nversion: 1\nfields: [\n",
                     context, diagnostics);

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(diagnostics.empty());
    EXPECT_TRUE(has_diagnostic(diagnostics, "yaml-parser", "BC2101"));
}

TEST(YamlCompilerTest, StopsAfterSchemaDecodeFailure) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result = compile_yaml(
        R"(namespace: breadcrumbs.telemetry
record: Sample
version: 1
type: data
)",
        context, diagnostics);

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(diagnostics.empty());
    EXPECT_TRUE(has_diagnostic(diagnostics, "yaml-schema-decoder", "BC2303"));
}

TEST(YamlCompilerTest, StopsAfterSourceLoweringFailure) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result = compile_yaml(
        R"(namespace: breadcrumbs.telemetry
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
    EXPECT_TRUE(has_diagnostic(diagnostics, "source-schema-normalization", "BC2403"));
}

TEST(YamlCompilerTest, StopsAfterSemanticFailure) {
    CompilerContext context;
    DiagnosticEngine diagnostics;

    const YamlCompilationResult result = compile_yaml(
        R"(namespace: breadcrumbs.telemetry
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
