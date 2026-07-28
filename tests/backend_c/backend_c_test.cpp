#include "compiler/backend_c/backend_c.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using quarry::compiler::backend_c::Backend;
using quarry::compiler::backend_c::CodegenOptions;
using quarry::compiler::backend_c::CodegenResult;
using quarry::compiler::backend_c::GeneratedFile;
using quarry::compiler::backend_c::PlanResult;
using quarry::compiler::context::CompilerContext;
using quarry::compiler::diagnostics::DiagnosticEngine;
using quarry::compiler::schema_ir::SchemaIrModel;
using quarry::compiler::schema_ir::SchemaIrValidator;
using ::quarry::schema_ir::EnumIR;
using ::quarry::schema_ir::EnumValueIR;
using ::quarry::schema_ir::FieldIR;
using ::quarry::schema_ir::NamespaceIR;
using ::quarry::schema_ir::RecordIR;

// Asserts the given Schema IR is well-formed, the same discipline
// tests/backend/backend_codegen_test.cpp uses: backend_c should only ever
// see validated Schema IR in production, so tests feed it nothing less.
void assert_valid(const SchemaIrModel& schema_ir) {
    CompilerContext context;
    DiagnosticEngine diagnostics;
    SchemaIrValidator validator;
    validator.validate(schema_ir, context, diagnostics);
    ASSERT_TRUE(diagnostics.empty()) << "fixture Schema IR failed validation";
}

[[nodiscard]] NamespaceIR* add_child_namespace(NamespaceIR& parent, std::uint64_t ir_id,
                                               std::string_view name, std::string_view fqn) {
    NamespaceIR* child = parent.add_namespaces();
    child->set_ir_id(ir_id);
    child->set_name(std::string(name));
    child->set_fqn(std::string(fqn));
    return child;
}

[[nodiscard]] RecordIR* add_zero_field_record(NamespaceIR& ns, std::uint64_t ir_id,
                                              std::uint32_t record_id, std::string_view name,
                                              std::string_view fqn) {
    RecordIR* record = ns.add_records();
    record->set_ir_id(ir_id);
    record->set_record_id(record_id);
    record->set_name(std::string(name));
    record->set_fqn(std::string(fqn));
    return record;
}

[[nodiscard]] EnumIR* add_enum(NamespaceIR& ns, std::uint64_t ir_id, std::string_view name,
                               std::string_view fqn) {
    EnumIR* enum_ir = ns.add_enums();
    enum_ir->set_ir_id(ir_id);
    enum_ir->set_name(std::string(name));
    enum_ir->set_fqn(std::string(fqn));
    return enum_ir;
}

void add_enum_value(EnumIR& enum_ir, std::string_view name, std::int64_t value) {
    EnumValueIR* value_ir = enum_ir.add_values();
    value_ir->set_name(std::string(name));
    value_ir->set_value(value);
}

TEST(BackendCTest, EmptySchemaProducesNoFiles) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    schema_ir.mutable_root_namespace()->set_ir_id(1);
    assert_valid(schema_ir);

    Backend backend;
    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    ASSERT_TRUE(plan_result.success) << plan_result.error_message;
    EXPECT_TRUE(plan_result.plan.files.empty());

    const CodegenResult codegen_result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(codegen_result.success) << codegen_result.error_message;
    EXPECT_TRUE(codegen_result.files.empty());
}

TEST(BackendCTest, RootNamespaceZeroFieldRecordUsesRootFileStem) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    (void)add_zero_field_record(*root, 2, 1U, "Example", "Example");
    assert_valid(schema_ir);

    Backend backend;
    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    ASSERT_TRUE(plan_result.success) << plan_result.error_message;
    ASSERT_EQ(plan_result.plan.files.size(), 1U);
    EXPECT_EQ(plan_result.plan.files[0].relative_header_path, "schema.generated.h");
    EXPECT_EQ(plan_result.plan.files[0].relative_source_path, "schema.generated.c");
    EXPECT_EQ(plan_result.plan.files[0].generated_include_path, "schema.generated.h");
}

TEST(BackendCTest, NestedNamespaceProducesSymbolPrefixedFileAndSymbols) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* quarry_ns = add_child_namespace(*root, 2, "quarry", "quarry");
    NamespaceIR* telemetry_ns =
        add_child_namespace(*quarry_ns, 3, "telemetry", "quarry.telemetry");
    (void)add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "quarry.telemetry.Sample");
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);

    const GeneratedFile* header = nullptr;
    const GeneratedFile* source = nullptr;
    for (const auto& file : result.files) {
        if (file.path == "generated/quarry/telemetry.generated.h") {
            header = &file;
        } else if (file.path == "generated/quarry/telemetry.generated.c") {
            source = &file;
        }
    }
    ASSERT_NE(header, nullptr);
    ASSERT_NE(source, nullptr);

    EXPECT_NE(header->content.find("quarry_telemetry_Sample_t"), std::string::npos);
    EXPECT_NE(header->content.find("void quarry_telemetry_Sample_init("), std::string::npos);
    EXPECT_NE(header->content.find("#ifndef QUARRY_GENERATED_C_QUARRY_TELEMETRY_GENERATED_H_"),
             std::string::npos);
    EXPECT_NE(header->content.find("extern \"C\""), std::string::npos);
    EXPECT_NE(header->content.find("uint8_t _reserved;"), std::string::npos);

    EXPECT_NE(source->content.find("#include \"quarry/telemetry.generated.h\""),
             std::string::npos);
    EXPECT_NE(source->content.find("quarry_telemetry_Sample_init"), std::string::npos);
}

TEST(BackendCTest, EnumRendersUppercasePrefixedConstants) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* status = add_enum(*telemetry_ns, 3, "Status", "telemetry.Status");
    add_enum_value(*status, "ok", 0);
    add_enum_value(*status, "error", 1);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);

    const std::string& header_content = result.files[0].content;
    EXPECT_NE(header_content.find("TELEMETRY_STATUS_OK = 0"), std::string::npos);
    EXPECT_NE(header_content.find("TELEMETRY_STATUS_ERROR = 1"), std::string::npos);
}

TEST(BackendCTest, EnumValueOutsideInt32RangeFailsGenerationWithClearDiagnostic) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* status = add_enum(*telemetry_ns, 3, "Status", "telemetry.Status");
    add_enum_value(*status, "TooBig", 9'000'000'000LL);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("telemetry.Status.TooBig"), std::string::npos);
    EXPECT_NE(result.error_message.find("9000000000"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
}

TEST(BackendCTest, RecordWithFieldsFailsGenerationWithClearDiagnosticNamingRecordAndCount) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("count");
    field->set_field_index(0);
    field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("telemetry.Sample"), std::string::npos);
    EXPECT_NE(result.error_message.find("1 field(s)"), std::string::npos);
    EXPECT_TRUE(result.files.empty());

    // plan() must fail the same way generate() does -- the two must never
    // diverge (see compiler/backend_c/README.md).
    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    ASSERT_FALSE(plan_result.success);
    EXPECT_EQ(plan_result.error_message, result.error_message);
}

TEST(BackendCTest, DuplicateNamespaceFqnFailsWithClearDiagnostic) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* first = add_child_namespace(*root, 2, "telemetry", "telemetry");
    (void)add_zero_field_record(*first, 3, 1U, "Sample", "telemetry.Sample");
    NamespaceIR* second = add_child_namespace(*root, 4, "telemetry", "telemetry");
    (void)add_zero_field_record(*second, 5, 2U, "Other", "telemetry.Other");
    // Schema IR structural validation does not itself reject duplicate
    // sibling namespace FQNs (that is the namespace builder's job upstream
    // of Schema IR); intentionally not calling assert_valid() here so this
    // test exercises backend_c's own defensive duplicate-output-path
    // detection, which must not assume anything beyond Schema IR's own
    // structural validity (compiler/backend_c/README.md).

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("duplicate generated"), std::string::npos);
}

TEST(BackendCTest, PlanIsDeterministicAcrossRepeatedCalls) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* alpha = add_child_namespace(*root, 2, "alpha", "alpha");
    (void)add_zero_field_record(*alpha, 3, 1U, "One", "alpha.One");
    NamespaceIR* beta = add_child_namespace(*root, 4, "beta", "beta");
    (void)add_zero_field_record(*beta, 5, 2U, "Two", "beta.Two");
    assert_valid(schema_ir);

    Backend backend;
    const PlanResult first_result = backend.plan(schema_ir, CodegenOptions{});
    const PlanResult second_result = backend.plan(schema_ir, CodegenOptions{});
    ASSERT_TRUE(first_result.success);
    ASSERT_TRUE(second_result.success);
    ASSERT_EQ(first_result.plan.files.size(), 2U);
    ASSERT_EQ(second_result.plan.files.size(), 2U);
    for (std::size_t index = 0; index < first_result.plan.files.size(); ++index) {
        EXPECT_EQ(first_result.plan.files[index].relative_header_path,
                 second_result.plan.files[index].relative_header_path);
        EXPECT_EQ(first_result.plan.files[index].relative_source_path,
                 second_result.plan.files[index].relative_source_path);
    }
    // Declaration order is preserved (alpha before beta), not reordered.
    EXPECT_EQ(first_result.plan.files[0].relative_header_path, "alpha.generated.h");
    EXPECT_EQ(first_result.plan.files[1].relative_header_path, "beta.generated.h");
}

} // namespace
