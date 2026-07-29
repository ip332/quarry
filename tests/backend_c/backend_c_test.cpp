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
    EXPECT_NE(header->content.find("uint8_t reserved;"), std::string::npos);

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

TEST(BackendCTest, UnsupportedFieldTypeFailsGenerationWithClearDiagnosticNamingFieldAndRecord) {
    // Array/record-reference fields, and enum fields that don't meet the
    // same-namespace/non-negative-values constraints, remain unsupported
    // after PR-108/109/110/111 (scalars, same-namespace enums, and bounded
    // strings/bytes are now supported). Uses `array` as a representative
    // still-unsupported type.
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("label");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        ::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("telemetry.Sample.label"), std::string::npos);
    EXPECT_TRUE(result.files.empty());

    // plan() must fail the same way generate() does -- the two must never
    // diverge (see compiler/backend_c/README.md).
    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    ASSERT_FALSE(plan_result.success);
    EXPECT_EQ(plan_result.error_message, result.error_message);
}

TEST(BackendCTest, MixedSupportedAndUnsupportedFieldsFailsRecordAsAWhole) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* count_field = record->add_fields();
    count_field->set_name("count");
    count_field->set_field_index(0);
    count_field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    FieldIR* label_field = record->add_fields();
    label_field->set_name("label");
    label_field->set_field_index(1);
    label_field->mutable_type()->mutable_array()->set_max_elements(4);
    label_field->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        ::quarry::schema_ir::PRIMITIVE_TYPE_U32);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("telemetry.Sample.label"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
}

TEST(BackendCTest, GeneratesScalarStructFieldsAndCodecDeclarations) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
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
    EXPECT_NE(header.find("size_t telemetry_Sample_encoded_size("), std::string::npos);
    EXPECT_NE(header.find("#include <quarry/runtime_c/binary_record.h>"), std::string::npos);
    EXPECT_NE(header.find("QUARRY_C_GENERATED_CODE_API_VERSION"), std::string::npos);

    const std::string& source = result.files[1].content;
    EXPECT_NE(source.find("quarry_c_write_u32(&writer, record->count)"), std::string::npos);
    EXPECT_NE(source.find("quarry_c_write_f32(&writer, record->ratio)"), std::string::npos);
    EXPECT_NE(source.find("quarry_c_encode_record(1U, fields, field_count"), std::string::npos);
    EXPECT_NE(source.find("quarry_c_parse_record(input, input_length"), std::string::npos);
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
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
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

    const std::string& source = result.files[1].content;
    // Encode-side membership validation, matching the C++ backend's
    // generated `enum_numeric == 1 || enum_numeric == 2` pattern.
    EXPECT_NE(source.find("record->status == 0 || record->status == 1"), std::string::npos);
    EXPECT_NE(source.find("quarry_c_write_u8(&writer, (uint8_t)record->status)"),
             std::string::npos);
    // Decode-side: read into a raw unsigned temp, validate membership,
    // then cast into the enum-typed struct field.
    EXPECT_NE(source.find("uint8_t status_raw = 0;"), std::string::npos);
    EXPECT_NE(source.find("quarry_c_read_u8(&field_reader, &status_raw)"), std::string::npos);
    EXPECT_NE(source.find("status_raw == 0 || status_raw == 1"), std::string::npos);
    EXPECT_NE(source.find("QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE"), std::string::npos);
    EXPECT_NE(source.find("result.value.status = (telemetry_Status_t)status_raw;"),
             std::string::npos);
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
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("wide");
    field->set_field_index(0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& source = result.files[1].content;
    EXPECT_NE(source.find("uint8_t wide_bytes[2];"), std::string::npos);
    EXPECT_NE(source.find("quarry_c_write_u16(&writer, (uint16_t)record->wide)"),
             std::string::npos);
}

TEST(BackendCTest, CrossNamespaceEnumFieldFailsWithClearDiagnostic) {
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
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("beta.Sample.status"), std::string::npos);
    EXPECT_NE(result.error_message.find("different namespace"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
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
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
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
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
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

    const std::string& source = result.files[1].content;
    // Encode side: bounds check against schema max_bytes, then UTF-8
    // validation, both before the field is added to fields[]/field_count --
    // no scratch buffer, points directly at record->label.
    EXPECT_NE(source.find("if (record->label_length > 16U) {"), std::string::npos);
    EXPECT_NE(source.find("QUARRY_C_STATUS_BOUNDS_EXCEEDED"), std::string::npos);
    EXPECT_NE(source.find("quarry_c_is_valid_utf8((const uint8_t*)record->label, "
                         "record->label_length)"),
             std::string::npos);
    EXPECT_NE(source.find("QUARRY_C_STATUS_INVALID_UTF8"), std::string::npos);
    EXPECT_NE(source.find("fields[field_count].bytes = (const uint8_t*)record->label;"),
             std::string::npos);
    EXPECT_NE(source.find("fields[field_count].length = record->label_length;"),
             std::string::npos);
    EXPECT_EQ(source.find("label_bytes["), std::string::npos)
        << "string fields must not get a fixed-width scratch buffer";

    // Decode side: one checked bounded copy (bounds + copy in a single
    // runtime call), then UTF-8 validation, then commit (NUL-terminate,
    // set the authoritative length).
    EXPECT_NE(source.find("quarry_c_copy_bounded(\n"
                         "                (uint8_t*)result.value.label, 16U, field_view.bytes, "
                         "field_view.length);"),
             std::string::npos);
    EXPECT_NE(source.find("quarry_c_is_valid_utf8(field_view.bytes, field_view.length)"),
             std::string::npos);
    EXPECT_NE(source.find("result.value.label[field_view.length] = '\\0';"), std::string::npos);
    EXPECT_NE(source.find("result.value.label_length = (uint32_t)field_view.length;"),
             std::string::npos);
    EXPECT_NE(source.find("result.value.has_label = true;"), std::string::npos);
}

TEST(BackendCTest, StringFieldEncodedSizeDoesNotValidateBoundsOrUtf8) {
    // Matches the enum-membership precedent exactly: _encoded_size() reads
    // the field's current (possibly invalid) length/content directly,
    // without validating -- only the real _encode() call rejects an
    // invalid value. Verified by asserting the _encoded_size() function
    // body contains no bounds/UTF-8 check at all.
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* label_field = record->add_fields();
    label_field->set_name("label");
    label_field->set_field_index(0);
    label_field->mutable_type()->mutable_string()->set_max_bytes(16);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& source = result.files[1].content;

    const std::size_t encoded_size_start = source.find("telemetry_Sample_encoded_size(");
    const std::size_t encode_start = source.find("telemetry_Sample_encode(");
    ASSERT_NE(encoded_size_start, std::string::npos);
    ASSERT_NE(encode_start, std::string::npos);
    ASSERT_LT(encoded_size_start, encode_start);
    const std::string encoded_size_body =
        source.substr(encoded_size_start, encode_start - encoded_size_start);
    EXPECT_EQ(encoded_size_body.find("BOUNDS_EXCEEDED"), std::string::npos);
    EXPECT_EQ(encoded_size_body.find("INVALID_UTF8"), std::string::npos);
}

TEST(BackendCTest, MixedScalarEnumStringRecordGeneratesAllFieldKinds) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* status_enum = add_enum(*telemetry_ns, 3, "Status", "telemetry.Status");
    add_enum_value(*status_enum, "OK", 0);
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
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
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
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

    const std::string& source = result.files[1].content;
    // Encode side: bounds check against schema max_bytes only -- no UTF-8
    // validation (the BRF spec's "bytes" section: "No UTF-8 validation
    // applies") -- before the field is added to fields[]/field_count. No
    // scratch buffer, points directly at record->blob.
    EXPECT_NE(source.find("if (record->blob_length > 16U) {"), std::string::npos);
    EXPECT_NE(source.find("QUARRY_C_STATUS_BOUNDS_EXCEEDED"), std::string::npos);
    EXPECT_EQ(source.find("quarry_c_is_valid_utf8"), std::string::npos)
        << "bytes fields must never be UTF-8 validated";
    EXPECT_NE(source.find("fields[field_count].bytes = record->blob;"), std::string::npos);
    EXPECT_NE(source.find("fields[field_count].length = record->blob_length;"),
             std::string::npos);
    EXPECT_EQ(source.find("blob_bytes["), std::string::npos)
        << "bytes fields must not get a fixed-width scratch buffer";

    // Decode side: one checked bounded copy, no UTF-8 validation, no NUL
    // terminator write (unlike string decode).
    EXPECT_NE(source.find("quarry_c_copy_bounded(\n"
                         "                result.value.blob, 16U, field_view.bytes, "
                         "field_view.length);"),
             std::string::npos);
    EXPECT_NE(source.find("result.value.blob_length = (uint32_t)field_view.length;"),
             std::string::npos);
    EXPECT_NE(source.find("result.value.has_blob = true;"), std::string::npos);
    EXPECT_EQ(source.find("blob[field_view.length] = '\\0'"), std::string::npos)
        << "bytes fields must never write a NUL terminator";
}

TEST(BackendCTest, BytesFieldEncodedSizeDoesNotValidateBounds) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* blob_field = record->add_fields();
    blob_field->set_name("blob");
    blob_field->set_field_index(0);
    blob_field->mutable_type()->mutable_bytes()->set_max_bytes(16);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& source = result.files[1].content;

    const std::size_t encoded_size_start = source.find("telemetry_Sample_encoded_size(");
    const std::size_t encode_start = source.find("telemetry_Sample_encode(");
    ASSERT_NE(encoded_size_start, std::string::npos);
    ASSERT_NE(encode_start, std::string::npos);
    ASSERT_LT(encoded_size_start, encode_start);
    const std::string encoded_size_body =
        source.substr(encoded_size_start, encode_start - encoded_size_start);
    EXPECT_EQ(encoded_size_body.find("BOUNDS_EXCEEDED"), std::string::npos);
}

TEST(BackendCTest, MixedScalarEnumStringBytesRecordGeneratesAllFieldKinds) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    EnumIR* status_enum = add_enum(*telemetry_ns, 3, "Status", "telemetry.Status");
    add_enum_value(*status_enum, "OK", 0);
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
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
