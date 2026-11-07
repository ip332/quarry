#include "compiler/backend_c/backend_c.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"

#include <algorithm>
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
    NamespaceIR* telemetry_ns = add_child_namespace(*quarry_ns, 3, "telemetry", "quarry.telemetry");
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
    EXPECT_NE(header->content.find("uint8_t reserved;"), std::string::npos);

    EXPECT_NE(source->content.find("#include \"quarry/telemetry.generated.h\""), std::string::npos);
    EXPECT_EQ(source->content.find("quarry_telemetry_Sample_init"), std::string::npos);
}

TEST(BackendCTest, KeywordNamespaceSegmentRemainsValidWithSymbolPrefix) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* keyword_ns = add_child_namespace(*root, 2, "switch", "switch");
    (void)add_zero_field_record(*keyword_ns, 3, 1U, "Sample", "switch.Sample");
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);
    EXPECT_NE(result.files[0].content.find("switch_Sample_t"), std::string::npos);
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

TEST(BackendCTest, HardenedNamesMapKeywordsReservedNamesAndDerivedCollisions) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    RecordIR* record = add_zero_field_record(*root, 2, 1U, "switch", "switch");

    FieldIR* keyword = record->add_fields();
    keyword->set_name("switch");
    keyword->set_field_index(0);
    keyword->mutable_type()->mutable_string()->set_max_bytes(8U);

    FieldIR* payload = record->add_fields();
    payload->set_name("payload");
    payload->set_field_index(1);
    payload->mutable_type()->mutable_bytes()->set_max_bytes(8U);

    FieldIR* explicit_length = record->add_fields();
    explicit_length->set_name("payload_length");
    explicit_length->set_field_index(2);
    explicit_length->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);

    FieldIR* samples = record->add_fields();
    samples->set_name("samples");
    samples->set_field_index(3);
    samples->mutable_type()->mutable_array()->set_max_elements(2U);
    samples->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_string()
        ->set_max_bytes(8U);

    FieldIR* explicit_count = record->add_fields();
    explicit_count->set_name("samples_count");
    explicit_count->set_field_index(4);
    explicit_count->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);

    FieldIR* double_underscore = record->add_fields();
    double_underscore->set_name("__value");
    double_underscore->set_field_index(5);
    double_underscore->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);

    FieldIR* underscore_upper = record->add_fields();
    underscore_upper->set_name("_Value");
    underscore_upper->set_field_index(6);
    underscore_upper->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);

    FieldIR* underscore_lower = record->add_fields();
    underscore_lower->set_name("_internal");
    underscore_lower->set_field_index(7);
    underscore_lower->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);

    FieldIR* escaped_candidate = record->add_fields();
    escaped_candidate->set_name("quarry_switch");
    escaped_candidate->set_field_index(8);
    escaped_candidate->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);

    EnumIR* enum_ir = add_enum(*root, 3, "enum", "enum");
    add_enum_value(*enum_ir, "ok", 0);
    add_enum_value(*enum_ir, "OK", 1);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);
    const std::string& header = result.files[0].content;

    EXPECT_NE(header.find("quarry_switch_t"), std::string::npos);
    EXPECT_NE(header.find("quarry_switch_length"), std::string::npos);
    EXPECT_NE(header.find("payload_length_2"), std::string::npos);
    EXPECT_NE(header.find("samples_count_2"), std::string::npos);
    EXPECT_NE(header.find("quarry___value"), std::string::npos);
    EXPECT_NE(header.find("quarry__Value"), std::string::npos);
    EXPECT_NE(header.find("quarry__internal"), std::string::npos);
    EXPECT_NE(header.find("quarry_switch_2"), std::string::npos);
    EXPECT_NE(header.find("bool has_payload_length_2;"), std::string::npos);
    EXPECT_NE(header.find("uint32_t payload_length_2;"), std::string::npos);
    EXPECT_NE(header.find("bool has_samples_count_2;"), std::string::npos);
    EXPECT_NE(header.find("uint32_t samples_count_2;"), std::string::npos);
    EXPECT_NE(header.find("ENUM_OK = 0"), std::string::npos);
    EXPECT_NE(header.find("ENUM_OK_2 = 1"), std::string::npos);
    EXPECT_NE(header.find("quarry_enum_t"), std::string::npos);
    EXPECT_EQ(header.find("bool has_payload_length;"), std::string::npos);
    EXPECT_EQ(header.find("bool has_samples_count;"), std::string::npos);
}

TEST(BackendCTest, SafeFieldNamesRemainUnchanged) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    RecordIR* record = add_zero_field_record(*root, 2, 1U, "Sample", "Sample");
    FieldIR* field = record->add_fields();
    field->set_name("count");
    field->set_field_index(0);
    field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_NE(result.files[0].content.find("bool has_count;"), std::string::npos);
    EXPECT_NE(result.files[0].content.find("uint32_t count;"), std::string::npos);
}

TEST(BackendCTest, ArrayElementTypeCollisionIsDisambiguated) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    RecordIR* record = add_zero_field_record(*root, 2, 1U, "Sample", "Sample");
    (void)add_zero_field_record(*root, 3, 2U, "Sample_array_0_string_element",
                                "Sample_array_0_string_element");
    FieldIR* field = record->add_fields();
    field->set_name("labels");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(2U);
    field->mutable_type()->mutable_array()->mutable_element_type()->mutable_string()->set_max_bytes(
        8U);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_NE(result.files[0].content.find("} Sample_array_0_string_element_t;"),
              std::string::npos);
    EXPECT_NE(result.files[0].content.find("} Sample_array_0_string_element_t_2;"),
              std::string::npos);
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

TEST(BackendCTest, ArrayOfStringGeneratesNamedElementType) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("label");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()->mutable_array()->mutable_element_type()->mutable_string()->set_max_bytes(
        8);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);
    EXPECT_NE(result.files[0].content.find("} telemetry_Sample_array_0_string_element_t;"),
              std::string::npos);
    EXPECT_NE(result.files[0].content.find("telemetry_Sample_array_0_string_element_t label[4];"),
              std::string::npos);

    // plan() must fail the same way generate() does -- the two must never
    // diverge (see compiler/backend_c/README.md).
    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    ASSERT_TRUE(plan_result.success) << plan_result.error_message;
}

TEST(BackendCTest, MixedSupportedAndUnsupportedFieldsFailsRecordAsAWhole) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* count_field = record->add_fields();
    count_field->set_name("count");
    count_field->set_field_index(0);
    count_field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    FieldIR* label_field = record->add_fields();
    label_field->set_name("label");
    label_field->set_field_index(1);
    label_field->mutable_type()->mutable_array()->set_max_elements(4);
    label_field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_string()
        ->set_max_bytes(8);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_NE(result.files[0].content.find("telemetry_Sample_array_1_string_element_t label[4];"),
              std::string::npos);
}

TEST(BackendCTest, GeneratesScalarStructFieldsAndCodecDeclarations) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* count_field = record->add_fields();
    count_field->set_name("count");
    count_field->set_field_index(0);
    count_field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    FieldIR* ratio_field = record->add_fields();
    ratio_field->set_name("ratio");
    ratio_field->set_field_index(1);
    ratio_field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_F32);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);

    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("bool has_count;"), std::string::npos);
    EXPECT_NE(header.find("uint32_t count;"), std::string::npos);
    EXPECT_NE(header.find("bool has_ratio;"), std::string::npos);
    EXPECT_NE(header.find("float ratio;"), std::string::npos);
    EXPECT_NE(header.find("telemetry_Sample_encode_result_t"), std::string::npos);
    EXPECT_NE(header.find("telemetry_Sample_decode_result_t"), std::string::npos);
    EXPECT_NE(header.find("telemetry_Sample_encoded_size_result_t telemetry_Sample_encoded_size("), std::string::npos);
    EXPECT_NE(header.find("#include <quarry/runtime_c/typed_record.h>"), std::string::npos);
    EXPECT_NE(header.find("QUARRY_C_GENERATED_CODE_API_VERSION"), std::string::npos);

    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_U32"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
}

TEST(BackendCTest, EnumFieldGeneratesTypedefTypeStructFieldAndCodec) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* status_enum = add_enum(*telemetry_ns, 3, "Status", "telemetry.Status");
    add_enum_value(*status_enum, "OK", 0);
    add_enum_value(*status_enum, "ERROR", 1);
    RecordIR* record = add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
    FieldIR* status_field = record->add_fields();
    status_field->set_name("status");
    status_field->set_field_index(0);
    status_field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);

    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("typedef enum {"), std::string::npos);
    EXPECT_NE(header.find("TELEMETRY_STATUS_OK = 0"), std::string::npos);
    EXPECT_NE(header.find("TELEMETRY_STATUS_ERROR = 1"), std::string::npos);
    EXPECT_NE(header.find("} telemetry_Status_t;"), std::string::npos);
    EXPECT_NE(header.find("bool has_status;"), std::string::npos);
    EXPECT_NE(header.find("telemetry_Status_t status;"), std::string::npos);

    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_ENUM"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
    EXPECT_NE(binding.find("_enum_values[]"), std::string::npos);
    EXPECT_NE(binding.find("_enum_load"), std::string::npos);
}

TEST(BackendCTest, EnumFieldWidthMatchesMaxDeclaredValue) {
    // 300 requires 2 bytes (exceeds uint8_t's range), matching
    // enum_width_for_max_value / the C++ backend's identical rule.
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* wide_enum = add_enum(*telemetry_ns, 3, "Wide", "telemetry.Wide");
    add_enum_value(*wide_enum, "SMALL", 0);
    add_enum_value(*wide_enum, "LARGE", 300);
    RecordIR* record = add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("wide");
    field->set_field_index(0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_ENUM"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
    EXPECT_NE(binding.find("_enum_values[]"), std::string::npos);
    EXPECT_NE(binding.find("_enum_load"), std::string::npos);
}

TEST(BackendCTest, CrossNamespaceEnumFieldGeneratesDependencyInclude) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* alpha_ns = add_child_namespace(*root, 2, "alpha", "alpha");
    EnumIR* alpha_enum = add_enum(*alpha_ns, 3, "Status", "alpha.Status");
    add_enum_value(*alpha_enum, "OK", 0);
    NamespaceIR* beta_ns = add_child_namespace(*root, 4, "beta", "beta");
    RecordIR* record = add_zero_field_record(*beta_ns, 5, 1U, "Sample", "beta.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("status");
    field->set_field_index(0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 4U);
    EXPECT_NE(result.files[2].content.find("#include \"alpha.generated.h\""), std::string::npos);
    EXPECT_NE(result.files[2].content.find("alpha_Status_t status;"), std::string::npos);
}

TEST(BackendCTest, NegativeValueEnumFieldFailsWithClearDiagnostic) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    // The enum declaration itself is valid (fits int32 range) even though
    // it has a negative value; only *using it as a field type* is rejected.
    EnumIR* signed_enum = add_enum(*telemetry_ns, 3, "Signed", "telemetry.Signed");
    add_enum_value(*signed_enum, "NEGATIVE", -1);
    add_enum_value(*signed_enum, "POSITIVE", 1);
    RecordIR* record = add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("signed_field");
    field->set_field_index(0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("telemetry.Sample.signed_field"), std::string::npos);
    EXPECT_NE(result.error_message.find("non-negative"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
}

TEST(BackendCTest, StringFieldGeneratesFixedCapacityStructFieldAndCodec) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* label_field = record->add_fields();
    label_field->set_name("label");
    label_field->set_field_index(0);
    label_field->mutable_type()->mutable_string()->set_max_bytes(16);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);

    // Capacity is max_bytes + 1 (room for the trailing NUL the decoder
    // always writes); label_length is the authoritative wire byte length.
    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("bool has_label;"), std::string::npos);
    EXPECT_NE(header.find("char label[17];"), std::string::npos);
    EXPECT_NE(header.find("uint32_t label_length;"), std::string::npos);

    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_STRING"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
}

TEST(BackendCTest, StringFieldUsesSharedSizeValidation) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* label_field = record->add_fields();
    label_field->set_name("label");
    label_field->set_field_index(0);
    label_field->mutable_type()->mutable_string()->set_max_bytes(16);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_STRING"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
}

TEST(BackendCTest, MixedScalarEnumStringRecordGeneratesAllFieldKinds) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* status_enum = add_enum(*telemetry_ns, 3, "Status", "telemetry.Status");
    add_enum_value(*status_enum, "OK", 0);
    RecordIR* record = add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
    FieldIR* count_field = record->add_fields();
    count_field->set_name("count");
    count_field->set_field_index(0);
    count_field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    FieldIR* status_field = record->add_fields();
    status_field->set_name("status");
    status_field->set_field_index(1);
    status_field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(3);
    FieldIR* label_field = record->add_fields();
    label_field->set_name("label");
    label_field->set_field_index(2);
    label_field->mutable_type()->mutable_string()->set_max_bytes(8);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("uint32_t count;"), std::string::npos);
    EXPECT_NE(header.find("telemetry_Status_t status;"), std::string::npos);
    EXPECT_NE(header.find("char label[9];"), std::string::npos);
    EXPECT_NE(header.find("uint32_t label_length;"), std::string::npos);
}

TEST(BackendCTest, BytesFieldGeneratesFixedCapacityStructFieldAndCodec) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* blob_field = record->add_fields();
    blob_field->set_name("blob");
    blob_field->set_field_index(0);
    blob_field->mutable_type()->mutable_bytes()->set_max_bytes(16);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);

    // Capacity is exactly max_bytes -- unlike string, no "+1" for a NUL
    // terminator (arbitrary binary data has no such convenience).
    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("bool has_blob;"), std::string::npos);
    EXPECT_NE(header.find("uint8_t blob[16];"), std::string::npos);
    EXPECT_NE(header.find("uint32_t blob_length;"), std::string::npos);

    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_BYTES"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
}

TEST(BackendCTest, BytesFieldUsesSharedSizeValidation) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* blob_field = record->add_fields();
    blob_field->set_name("blob");
    blob_field->set_field_index(0);
    blob_field->mutable_type()->mutable_bytes()->set_max_bytes(16);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_BYTES"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
}

TEST(BackendCTest, MixedScalarEnumStringBytesRecordGeneratesAllFieldKinds) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* status_enum = add_enum(*telemetry_ns, 3, "Status", "telemetry.Status");
    add_enum_value(*status_enum, "OK", 0);
    RecordIR* record = add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
    FieldIR* count_field = record->add_fields();
    count_field->set_name("count");
    count_field->set_field_index(0);
    count_field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    FieldIR* status_field = record->add_fields();
    status_field->set_name("status");
    status_field->set_field_index(1);
    status_field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(3);
    FieldIR* label_field = record->add_fields();
    label_field->set_name("label");
    label_field->set_field_index(2);
    label_field->mutable_type()->mutable_string()->set_max_bytes(8);
    FieldIR* blob_field = record->add_fields();
    blob_field->set_name("blob");
    blob_field->set_field_index(3);
    blob_field->mutable_type()->mutable_bytes()->set_max_bytes(8);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("uint32_t count;"), std::string::npos);
    EXPECT_NE(header.find("telemetry_Status_t status;"), std::string::npos);
    EXPECT_NE(header.find("char label[9];"), std::string::npos);
    EXPECT_NE(header.find("uint8_t blob[8];"), std::string::npos);
    EXPECT_NE(header.find("uint32_t blob_length;"), std::string::npos);
}

TEST(BackendCTest, ArrayOfScalarFieldGeneratesFixedCapacityStructFieldAndCodec) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("readings");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        ::quarry::schema_ir::PRIMITIVE_TYPE_F32);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);

    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("bool has_readings;"), std::string::npos);
    EXPECT_NE(header.find("float readings[4];"), std::string::npos);
    EXPECT_NE(header.find("uint32_t readings_count;"), std::string::npos);

    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_F32"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
}

TEST(BackendCTest, ArrayFieldDescribesBoundedElementStorage) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("values");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        ::quarry::schema_ir::PRIMITIVE_TYPE_U64);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_U64"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
}

TEST(BackendCTest, ArrayOfEnumFieldChecksMembershipOnEncodeAndDecode) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* status_enum = add_enum(*telemetry_ns, 3, "Status", "telemetry.Status");
    add_enum_value(*status_enum, "OK", 0);
    add_enum_value(*status_enum, "ERROR", 1);
    RecordIR* record = add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("statuses");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(3);
    field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_enum_type()
        ->set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("telemetry_Status_t statuses[3];"), std::string::npos);
    EXPECT_NE(header.find("uint32_t statuses_count;"), std::string::npos);

    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_ENUM"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
    EXPECT_NE(binding.find("_enum_values[]"), std::string::npos);
    EXPECT_NE(binding.find("_enum_load"), std::string::npos);
}

TEST(BackendCTest, ArrayFieldUsesSharedSizeValidation) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("readings");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        ::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_U32"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
}

TEST(BackendCTest, ArrayOfBytesGeneratesNamedElementType) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("labels");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()->mutable_array()->mutable_element_type()->mutable_bytes()->set_max_bytes(
        8);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_NE(result.files[0].content.find("} telemetry_Sample_array_0_bytes_element_t;"),
              std::string::npos);
    EXPECT_NE(result.files[0].content.find("telemetry_Sample_array_0_bytes_element_t labels[4];"),
              std::string::npos);
}

TEST(BackendCTest, ArrayOfCrossNamespaceEnumGeneratesDependencyInclude) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* alpha_ns = add_child_namespace(*root, 2, "alpha", "alpha");
    EnumIR* alpha_enum = add_enum(*alpha_ns, 3, "Status", "alpha.Status");
    add_enum_value(*alpha_enum, "OK", 0);
    NamespaceIR* beta_ns = add_child_namespace(*root, 4, "beta", "beta");
    RecordIR* record = add_zero_field_record(*beta_ns, 5, 1U, "Sample", "beta.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("statuses");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_enum_type()
        ->set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 4U);
    EXPECT_NE(result.files[2].content.find("#include \"alpha.generated.h\""), std::string::npos);
    EXPECT_NE(result.files[2].content.find("alpha_Status_t statuses[4];"), std::string::npos);
}

TEST(BackendCTest, ArrayOfNegativeValueEnumFailsWithClearDiagnostic) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* signed_enum = add_enum(*telemetry_ns, 3, "Signed", "telemetry.Signed");
    add_enum_value(*signed_enum, "NEGATIVE", -1);
    add_enum_value(*signed_enum, "POSITIVE", 1);
    RecordIR* record = add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("signed_values");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_enum_type()
        ->set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("telemetry.Sample.signed_values"), std::string::npos);
    EXPECT_NE(result.error_message.find("array element type"), std::string::npos);
    EXPECT_NE(result.error_message.find("non-negative"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
}

TEST(BackendCTest, NestedRecordFieldGeneratesEmbeddedStructFieldAndCodec) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* inner = add_zero_field_record(*telemetry_ns, 3, 1U, "Inner", "telemetry.Inner");
    FieldIR* inner_value = inner->add_fields();
    inner_value->set_name("value");
    inner_value->set_field_index(0);
    inner_value->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    RecordIR* outer = add_zero_field_record(*telemetry_ns, 4, 2U, "Outer", "telemetry.Outer");
    FieldIR* outer_field = outer->add_fields();
    outer_field->set_name("inner");
    outer_field->set_field_index(0);
    outer_field->mutable_type()->mutable_record()->set_target_record_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);

    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("bool has_inner;"), std::string::npos);
    // By-value embedding, not a pointer: no heap allocation, and the whole
    // struct is one flat, fixed-size, memset-zeroable value (see
    // compiler/backend_c/README.md's "Nested record fields" section).
    EXPECT_NE(header.find("telemetry_Inner_t inner;"), std::string::npos);
    EXPECT_EQ(header.find("telemetry_Inner_t* inner;"), std::string::npos);

    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_RECORD"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
    EXPECT_NE(binding.find("QUARRY_C_BRF_V2_STORAGE_"), std::string::npos);
}

TEST(BackendCTest, NestedRecordFieldReferencesChildDescriptor) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* inner = add_zero_field_record(*telemetry_ns, 3, 1U, "Inner", "telemetry.Inner");
    FieldIR* inner_value = inner->add_fields();
    inner_value->set_name("value");
    inner_value->set_field_index(0);
    inner_value->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    RecordIR* outer = add_zero_field_record(*telemetry_ns, 4, 2U, "Outer", "telemetry.Outer");
    FieldIR* outer_field = outer->add_fields();
    outer_field->set_name("inner");
    outer_field->set_field_index(0);
    outer_field->mutable_type()->mutable_record()->set_target_record_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_RECORD"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
    EXPECT_NE(binding.find("QUARRY_C_BRF_V2_STORAGE_"), std::string::npos);
}

TEST(BackendCTest, CrossNamespaceNestedRecordFieldGeneratesDependencyInclude) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* alpha_ns = add_child_namespace(*root, 2, "alpha", "alpha");
    (void)add_zero_field_record(*alpha_ns, 3, 1U, "Inner", "alpha.Inner");
    NamespaceIR* beta_ns = add_child_namespace(*root, 4, "beta", "beta");
    RecordIR* outer = add_zero_field_record(*beta_ns, 5, 2U, "Outer", "beta.Outer");
    FieldIR* field = outer->add_fields();
    field->set_name("inner");
    field->set_field_index(0);
    field->mutable_type()->mutable_record()->set_target_record_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 4U);
    EXPECT_NE(result.files[2].content.find("#include \"alpha.generated.h\""), std::string::npos);
    EXPECT_NE(result.files[2].content.find("alpha_Inner_t inner;"), std::string::npos);
}

TEST(BackendCTest, CrossNamespaceNestedRecordUsesCanonicalTargetRecordId) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* alpha_ns = add_child_namespace(*root, 2, "alpha", "alpha");
    (void)add_zero_field_record(*alpha_ns, 3, 2U, "Inner", "alpha.Inner");
    NamespaceIR* beta_ns = add_child_namespace(*root, 4, "beta", "beta");
    RecordIR* outer = add_zero_field_record(*beta_ns, 5, 1U, "Outer", "beta.Outer");
    FieldIR* field = outer->add_fields();
    field->set_name("inner");
    field->set_field_index(0);
    field->mutable_type()->mutable_record()->set_target_record_ir_id(3);
    assert_valid(schema_ir);

    const CodegenResult result = Backend{}.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_NE(result.files[2].content.find("&alpha_Inner_type, 2U"), std::string::npos);
}

TEST(BackendCTest, SelfReferentialNestedRecordFailsWithCycleDiagnostic) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record = add_zero_field_record(*telemetry_ns, 3, 1U, "Node", "telemetry.Node");
    FieldIR* field = record->add_fields();
    field->set_name("child");
    field->set_field_index(0);
    field->mutable_type()->mutable_record()->set_target_record_ir_id(3);
    // A record embedding itself by value is a structurally-infinite type
    // that Schema IR validation itself does not reject (cycle rejection is
    // backend_c's own responsibility, mirroring the C++ backend's
    // order_declarations_topologically in compiler/backend/backend.cpp) --
    // so intentionally not calling assert_valid() here, matching
    // backend_codegen_test.cpp's own CyclicNamespaceDependencyFailsClearly
    // precedent for the C++ backend's equivalent cycle check.

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("cycle"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
}

TEST(BackendCTest, ForwardDeclaredNestedRecordIsReorderedBeforeDependent) {
    // Mirrors tests/fixtures/backend/schema_ir/forward_record_reference.pbtxt
    // (the C++ backend's own forward-reference fixture): A is declared
    // before B in Schema IR order, but A embeds B by value, so B's struct
    // must be emitted first regardless of declaration order.
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    RecordIR* a = add_zero_field_record(*root, 2, 1U, "A", "A");
    FieldIR* a_field = a->add_fields();
    a_field->set_name("value");
    a_field->set_field_index(0);
    a_field->mutable_type()->mutable_record()->set_target_record_ir_id(3);
    RecordIR* b = add_zero_field_record(*root, 3, 2U, "B", "B");
    FieldIR* b_field = b->add_fields();
    b_field->set_name("count");
    b_field->set_field_index(0);
    b_field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);
    const std::string& header = result.files[0].content;

    const std::size_t b_struct_pos = header.find("} B_t;");
    const std::size_t a_struct_pos = header.find("} A_t;");
    ASSERT_NE(b_struct_pos, std::string::npos);
    ASSERT_NE(a_struct_pos, std::string::npos);
    EXPECT_LT(b_struct_pos, a_struct_pos);
    EXPECT_NE(header.find("B_t value;"), std::string::npos);
}

TEST(BackendCTest, ArrayOfCrossNamespaceRecordElementTypeGeneratesDependencyInclude) {
    // Same-namespace array-of-record elements are supported (PR-114); a
    // cross-namespace record element remains unsupported, mirroring the
    // identical restriction on a plain nested-record field (PR-113) and on
    // a cross-namespace enum array element (PR-112) -- no
    // cross-generated-file include-dependency mechanism exists yet.
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* alpha_ns = add_child_namespace(*root, 2, "alpha", "alpha");
    (void)add_zero_field_record(*alpha_ns, 3, 1U, "Item", "alpha.Item");
    NamespaceIR* beta_ns = add_child_namespace(*root, 4, "beta", "beta");
    RecordIR* outer = add_zero_field_record(*beta_ns, 5, 2U, "Outer", "beta.Outer");
    FieldIR* field = outer->add_fields();
    field->set_name("items");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(2);
    field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 4U);
    EXPECT_NE(result.files[2].content.find("#include \"alpha.generated.h\""), std::string::npos);
    EXPECT_NE(result.files[2].content.find("alpha_Item_t items[2];"), std::string::npos);
}

TEST(BackendCTest, ArrayOfRecordFieldGeneratesFixedCapacityStructFieldAndCodec) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* tree_ns = add_child_namespace(*root, 2, "tree", "tree");
    RecordIR* item = add_zero_field_record(*tree_ns, 3, 1U, "Item", "tree.Item");
    FieldIR* item_value = item->add_fields();
    item_value->set_name("value");
    item_value->set_field_index(0);
    item_value->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    RecordIR* group = add_zero_field_record(*tree_ns, 4, 2U, "Group", "tree.Group");
    FieldIR* items_field = group->add_fields();
    items_field->set_name("items");
    items_field->set_field_index(0);
    items_field->mutable_type()->mutable_array()->set_max_elements(3);
    items_field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2U);

    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("bool has_items;"), std::string::npos);
    // Fixed-capacity array of the element record's own by-value struct
    // type -- the exact same [max_elements]/_count shape scalar/enum
    // arrays already use, with no new rendering code (see
    // compiler/backend_c/README.md's "Record array fields" section).
    EXPECT_NE(header.find("tree_Item_t items[3];"), std::string::npos);
    EXPECT_NE(header.find("uint32_t items_count;"), std::string::npos);

    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_RECORD"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
    EXPECT_NE(binding.find("QUARRY_C_BRF_V2_STORAGE_"), std::string::npos);
}

TEST(BackendCTest, ArrayOfRecordFieldReferencesChildDescriptor) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* tree_ns = add_child_namespace(*root, 2, "tree", "tree");
    RecordIR* item = add_zero_field_record(*tree_ns, 3, 1U, "Item", "tree.Item");
    FieldIR* item_value = item->add_fields();
    item_value->set_name("value");
    item_value->set_field_index(0);
    item_value->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    RecordIR* group = add_zero_field_record(*tree_ns, 4, 2U, "Group", "tree.Group");
    FieldIR* items_field = group->add_fields();
    items_field->set_name("items");
    items_field->set_field_index(0);
    items_field->mutable_type()->mutable_array()->set_max_elements(3);
    items_field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_RECORD"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
    EXPECT_NE(binding.find("QUARRY_C_BRF_V2_STORAGE_"), std::string::npos);
}

TEST(BackendCTest, SelfReferentialArrayOfRecordsFailsWithCycleDiagnostic) {
    // A record containing an array of itself is a hard C-language
    // impossibility (a fixed-size array member, like a plain by-value
    // struct member, requires a complete element type -- there is no
    // valid declaration order for a record embedding an array of itself,
    // directly or transitively), the exact same by-value-storage
    // constraint that motivated PR-113's plain self-referential-record
    // cycle diagnostic, now generalized to array-element dependencies.
    // Not calling assert_valid(): Schema IR validation itself does not
    // reject this (matching PR-113's identical precedent).
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* tree_ns = add_child_namespace(*root, 2, "tree", "tree");
    RecordIR* node = add_zero_field_record(*tree_ns, 3, 1U, "Node", "tree.Node");
    FieldIR* field = node->add_fields();
    field->set_name("children");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(3);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("cycle"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
}

TEST(BackendCTest, ArrayOfRecordComposesWithNestedRecordField) {
    // Mirrors the C++ backend's own
    // BackendCodegenTest.RecordArrayComposesWithNestedRecordFields
    // (tests/fixtures/backend/schema_ir/nested_record_fields.pbtxt):
    // Group.items is an array of Middle, and Middle itself embeds a
    // nested Inner record -- proving the recursive max_encoded_size and
    // pure-composition model generalizes without a depth limit.
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* tree_ns = add_child_namespace(*root, 2, "tree", "tree");
    RecordIR* inner = add_zero_field_record(*tree_ns, 3, 1U, "Inner", "tree.Inner");
    FieldIR* inner_count = inner->add_fields();
    inner_count->set_name("count");
    inner_count->set_field_index(0);
    inner_count->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    RecordIR* middle = add_zero_field_record(*tree_ns, 4, 2U, "Middle", "tree.Middle");
    FieldIR* middle_inner = middle->add_fields();
    middle_inner->set_name("inner");
    middle_inner->set_field_index(0);
    middle_inner->mutable_type()->mutable_record()->set_target_record_ir_id(3);
    RecordIR* group = add_zero_field_record(*tree_ns, 5, 3U, "Group", "tree.Group");
    FieldIR* group_items = group->add_fields();
    group_items->set_name("items");
    group_items->set_field_index(0);
    group_items->mutable_type()->mutable_array()->set_max_elements(2);
    group_items->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(4);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("tree_Middle_t items[2];"), std::string::npos);
    const std::string& binding = result.files[0].content;
    EXPECT_NE(binding.find("QUARRY_C_TYPED_RECORD"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_encode(&"), std::string::npos);
    EXPECT_NE(binding.find("quarry_c_typed_decode(&"), std::string::npos);
    EXPECT_EQ(binding.find("_bytes["), std::string::npos);
    EXPECT_NE(binding.find("QUARRY_C_BRF_V2_STORAGE_"), std::string::npos);
}

TEST(BackendCTest, MixedScalarEnumStringBytesArrayRecordGeneratesAllFieldKinds) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* status_enum = add_enum(*telemetry_ns, 3, "Status", "telemetry.Status");
    add_enum_value(*status_enum, "OK", 0);
    RecordIR* inner_record =
        add_zero_field_record(*telemetry_ns, 4, 1U, "Location", "telemetry.Location");
    FieldIR* location_field = inner_record->add_fields();
    location_field->set_name("code");
    location_field->set_field_index(0);
    location_field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    RecordIR* record = add_zero_field_record(*telemetry_ns, 5, 2U, "Sample", "telemetry.Sample");
    FieldIR* count_field = record->add_fields();
    count_field->set_name("count");
    count_field->set_field_index(0);
    count_field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    FieldIR* status_field = record->add_fields();
    status_field->set_name("status");
    status_field->set_field_index(1);
    status_field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(3);
    FieldIR* label_field = record->add_fields();
    label_field->set_name("label");
    label_field->set_field_index(2);
    label_field->mutable_type()->mutable_string()->set_max_bytes(8);
    FieldIR* blob_field = record->add_fields();
    blob_field->set_name("blob");
    blob_field->set_field_index(3);
    blob_field->mutable_type()->mutable_bytes()->set_max_bytes(8);
    FieldIR* readings_field = record->add_fields();
    readings_field->set_name("readings");
    readings_field->set_field_index(4);
    readings_field->mutable_type()->mutable_array()->set_max_elements(4);
    readings_field->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        ::quarry::schema_ir::PRIMITIVE_TYPE_F32);
    FieldIR* location_ref_field = record->add_fields();
    location_ref_field->set_name("location");
    location_ref_field->set_field_index(5);
    location_ref_field->mutable_type()->mutable_record()->set_target_record_ir_id(4);
    FieldIR* locations_field = record->add_fields();
    locations_field->set_name("locations");
    locations_field->set_field_index(6);
    locations_field->mutable_type()->mutable_array()->set_max_elements(2);
    locations_field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(4);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("uint32_t count;"), std::string::npos);
    EXPECT_NE(header.find("telemetry_Status_t status;"), std::string::npos);
    EXPECT_NE(header.find("char label[9];"), std::string::npos);
    EXPECT_NE(header.find("uint8_t blob[8];"), std::string::npos);
    EXPECT_NE(header.find("float readings[4];"), std::string::npos);
    EXPECT_NE(header.find("uint32_t readings_count;"), std::string::npos);
    EXPECT_NE(header.find("telemetry_Location_t location;"), std::string::npos);
    EXPECT_NE(header.find("telemetry_Location_t locations[2];"), std::string::npos);
    EXPECT_NE(header.find("uint32_t locations_count;"), std::string::npos);
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
