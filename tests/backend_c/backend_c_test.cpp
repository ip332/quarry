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
    // Arrays of string/bytes elements, and cross-namespace or
    // negative-valued enum/record references (plain or array-element),
    // remain unsupported after PR-108 through PR-114 (scalars,
    // same-namespace enums, bounded strings/bytes, bounded arrays of
    // scalar/enum/same-namespace-record elements, and same-namespace
    // nested records are all supported as of PR-114). Uses `array<string>`
    // as a representative still-unsupported type (explicitly out of scope
    // for every PR to date: arrays of string/bytes elements).
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
    field->mutable_type()->mutable_array()->mutable_element_type()->mutable_string()->set_max_bytes(
        8);
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
    label_field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_string()
        ->set_max_bytes(8);
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

TEST(BackendCTest, ArrayOfScalarFieldGeneratesFixedCapacityStructFieldAndCodec) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
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

    const std::string& source = result.files[1].content;
    // Scratch buffer sized worst-case-varuint (5) + max_elements * width
    // (4 * 4 = 16) = 21.
    EXPECT_NE(source.find("uint8_t readings_bytes[21];"), std::string::npos);
    // Encode: bounds check against max_elements, then varuint count, then
    // per-element writes, in that order.
    EXPECT_NE(source.find("if (record->readings_count > 4U) {"), std::string::npos);
    EXPECT_NE(source.find("QUARRY_C_STATUS_BOUNDS_EXCEEDED"), std::string::npos);
    EXPECT_NE(source.find("quarry_c_write_varuint(&writer, record->readings_count)"),
             std::string::npos);
    EXPECT_NE(source.find("quarry_c_write_f32(&writer, record->readings[element_index])"),
             std::string::npos);
    // Decode: varuint count read and bounds check, then an overflow-safe,
    // division-guarded exact remaining-byte-count check (PR-116 hardening
    // -- see ArrayFieldDecodeUsesOverflowSafeLengthCheck below for the
    // dedicated test), then per-element reads directly into the
    // fixed-capacity array.
    EXPECT_NE(source.find("quarry_c_read_varuint(&array_reader, &element_count_raw)"),
             std::string::npos);
    EXPECT_NE(source.find("if (element_count_raw > 4U) {"), std::string::npos);
    EXPECT_NE(source.find("const size_t element_width = 4U;"), std::string::npos);
    EXPECT_NE(source.find("element_width == 0U || element_count > remaining_bytes / "
                         "element_width || remaining_bytes != (size_t)element_count * "
                         "element_width"),
             std::string::npos);
    EXPECT_NE(source.find("quarry_c_read_f32(&array_reader, &result.value.readings[element_index])"),
             std::string::npos);
    EXPECT_NE(source.find("result.value.readings_count = element_count;"), std::string::npos);
}

TEST(BackendCTest, ArrayFieldDecodeUsesOverflowSafeLengthCheck) {
    // PR-116 hardening: the fixed-width array decode's total-length check
    // must be overflow-safe on a 32-bit size_t platform. Uses u64 (width
    // 8, the widest scalar) as the most overflow-prone representative
    // element type. Mirrors the C++ backend's identical 3-part guard
    // (compiler/backend/backend.cpp): a zero-width defensive check, then a
    // division-based upper bound on element_count *before* any
    // multiplication, then the exact equality check -- so the
    // multiplication can never overflow once the division check has
    // already passed.
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
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
    const std::string& source = result.files[1].content;

    EXPECT_NE(source.find("const size_t element_width = 8U;"), std::string::npos);
    EXPECT_NE(source.find("const size_t remaining_bytes = field_view.length - "
                         "array_reader.offset;"),
             std::string::npos);
    // The three guards, in order, on one condition: zero-width defensive
    // check; division-based bound (established before any multiplication
    // runs); only then the exact equality check using the multiplication.
    const std::size_t guard_pos = source.find(
        "if (element_width == 0U || element_count > remaining_bytes / element_width || "
        "remaining_bytes != (size_t)element_count * element_width) {");
    EXPECT_NE(guard_pos, std::string::npos);
    // The division-based bound must appear before the multiplication in
    // the emitted condition text, matching the intended short-circuit
    // evaluation order (the multiplication is only ever reached once the
    // division check has already confirmed it cannot overflow).
    if (guard_pos != std::string::npos) {
        const std::size_t division_pos = source.find("remaining_bytes / element_width", guard_pos);
        const std::size_t multiply_pos =
            source.find("(size_t)element_count * element_width", guard_pos);
        ASSERT_NE(division_pos, std::string::npos);
        ASSERT_NE(multiply_pos, std::string::npos);
        EXPECT_LT(division_pos, multiply_pos);
    }
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
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("statuses");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(3);
    field->mutable_type()->mutable_array()->mutable_element_type()->mutable_enum_type()->
        set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("telemetry_Status_t statuses[3];"), std::string::npos);
    EXPECT_NE(header.find("uint32_t statuses_count;"), std::string::npos);

    const std::string& source = result.files[1].content;
    // Encode: per-element membership check before the write, using an
    // explicit cast, matching the plain enum field precedent exactly.
    EXPECT_NE(source.find("record->statuses[element_index] == 0 || "
                         "record->statuses[element_index] == 1"),
             std::string::npos);
    EXPECT_NE(source.find("quarry_c_write_u8(&writer, (uint8_t)record->statuses[element_index])"),
             std::string::npos);
    // Decode: read into a raw temp, validate membership, then cast into
    // the array element.
    EXPECT_NE(source.find("uint8_t element_raw = 0;"), std::string::npos);
    EXPECT_NE(source.find("quarry_c_read_u8(&array_reader, &element_raw)"), std::string::npos);
    EXPECT_NE(source.find("element_raw == 0 || element_raw == 1"), std::string::npos);
    EXPECT_NE(source.find("result.value.statuses[element_index] = (telemetry_Status_t)element_raw;"),
             std::string::npos);
}

TEST(BackendCTest, ArrayFieldEncodedSizeDoesNotValidateBoundsOrMembership) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
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

TEST(BackendCTest, ArrayOfUnsupportedElementTypeFailsWithClearDiagnostic) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* telemetry_ns = add_child_namespace(*root, 2, "telemetry", "telemetry");
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 3, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("labels");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()->mutable_array()->mutable_element_type()->mutable_string()->
        set_max_bytes(8);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("telemetry.Sample.labels"), std::string::npos);
    EXPECT_NE(result.error_message.find("array"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
}

TEST(BackendCTest, ArrayOfCrossNamespaceEnumFailsWithClearDiagnostic) {
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
    field->mutable_type()->mutable_array()->mutable_element_type()->mutable_enum_type()->
        set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("beta.Sample.statuses"), std::string::npos);
    EXPECT_NE(result.error_message.find("array element type"), std::string::npos);
    EXPECT_NE(result.error_message.find("different namespace"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
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
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "telemetry.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("signed_values");
    field->set_field_index(0);
    field->mutable_type()->mutable_array()->set_max_elements(4);
    field->mutable_type()->mutable_array()->mutable_element_type()->mutable_enum_type()->
        set_target_enum_ir_id(3);
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

    const std::string& source = result.files[1].content;
    // Scratch buffer sized from Inner's own worst-case max_encoded_size:
    // 16 (header) + (1 + 10 + 10) directory overhead + 4 (u32 payload) = 41.
    EXPECT_NE(source.find("uint8_t inner_bytes[41];"), std::string::npos);
    // Encode: pure composition -- call the child's own real _encode() into
    // the scratch buffer, propagate any failure status directly.
    EXPECT_NE(source.find("telemetry_Inner_encode_result_t inner_encode_result = "
                         "telemetry_Inner_encode(&record->inner, inner_bytes, "
                         "sizeof(inner_bytes));"),
             std::string::npos);
    EXPECT_NE(source.find("if (inner_encode_result.status != QUARRY_C_STATUS_OK) {"),
             std::string::npos);
    EXPECT_NE(source.find("fields[field_count].length = inner_encode_result.bytes_written;"),
             std::string::npos);
    // Decode: pure composition -- call the child's own real _decode() on the
    // isolated field-view byte span, propagate any failure status and an
    // absolute (parent-relative) byte offset directly.
    EXPECT_NE(source.find("telemetry_Inner_decode_result_t inner_decode_result = "
                         "telemetry_Inner_decode(field_view.bytes, field_view.length);"),
             std::string::npos);
    EXPECT_NE(source.find("if (inner_decode_result.status != QUARRY_C_STATUS_OK) {"),
             std::string::npos);
    EXPECT_NE(source.find("result.byte_offset = field_view.byte_offset + "
                         "inner_decode_result.byte_offset;"),
             std::string::npos);
    EXPECT_NE(source.find("result.value.inner = inner_decode_result.value;"), std::string::npos);
}

TEST(BackendCTest, NestedRecordFieldEncodedSizeUsesChildEncodedSizeWithoutEncoding) {
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
    const std::string& source = result.files[1].content;

    const std::size_t encoded_size_start = source.find("telemetry_Outer_encoded_size(");
    const std::size_t encode_start = source.find("telemetry_Outer_encode(");
    ASSERT_NE(encoded_size_start, std::string::npos);
    ASSERT_NE(encode_start, std::string::npos);
    ASSERT_LT(encoded_size_start, encode_start);
    const std::string encoded_size_body =
        source.substr(encoded_size_start, encode_start - encoded_size_start);
    EXPECT_NE(encoded_size_body.find("telemetry_Inner_encoded_size(&record->inner)"),
             std::string::npos);
    // _encoded_size() must never call the child's real, validating _encode()
    // -- only its own _encoded_size(), matching the string/bytes/array
    // precedent exactly.
    EXPECT_EQ(encoded_size_body.find("telemetry_Inner_encode("), std::string::npos);
}

TEST(BackendCTest, CrossNamespaceNestedRecordFieldFailsWithClearDiagnostic) {
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
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("beta.Outer.inner"), std::string::npos);
    EXPECT_NE(result.error_message.find("different namespace"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
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

TEST(BackendCTest, ArrayOfCrossNamespaceRecordElementTypeFailsWithClearDiagnostic) {
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
    field->mutable_type()->mutable_array()->mutable_element_type()->mutable_record()->
        set_target_record_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("beta.Outer.items"), std::string::npos);
    EXPECT_NE(result.error_message.find("array element type"), std::string::npos);
    EXPECT_NE(result.error_message.find("different namespace"), std::string::npos);
    EXPECT_TRUE(result.files.empty());
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
    items_field->mutable_type()->mutable_array()->mutable_element_type()->mutable_record()->
        set_target_record_ir_id(3);
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

    const std::string& source = result.files[1].content;
    // Scratch buffer sized from: 5 (worst-case count varuint) + 3 *
    // (10 (worst-case per-element length varuint) + 41 (Item's own
    // max_encoded_size: 16 header + 21 directory overhead + 4 payload)) =
    // 5 + 3 * 51 = 158.
    EXPECT_NE(source.find("uint8_t items_bytes[158];"), std::string::npos);
    // Encode (PR-114 §2A "Option A"): learn the element's length from its
    // own existing _encoded_size(), write that as the length-prefix
    // varuint, then encode the element directly into the writer's own
    // remaining tail space -- no temporary buffer, no raw-byte copy, no
    // new runtime function.
    EXPECT_NE(source.find("const size_t element_size = "
                         "tree_Item_encoded_size(&record->items[element_index]);"),
             std::string::npos);
    EXPECT_NE(source.find("quarry_c_write_varuint(&writer, (uint64_t)element_size)"),
             std::string::npos);
    EXPECT_NE(source.find("tree_Item_encode(&record->items[element_index], "
                         "writer.buffer + writer.length, writer.capacity - writer.length)"),
             std::string::npos);
    EXPECT_NE(source.find("writer.length += element_result.bytes_written;"), std::string::npos);
    // Decode: per-element varuint length prefix, bounds-checked, then pure
    // composition via the element type's own _decode() on the isolated
    // element byte span.
    EXPECT_NE(source.find("quarry_c_read_varuint(&array_reader, &element_length_raw)"),
             std::string::npos);
    EXPECT_NE(source.find("element_length_raw > array_reader.length - array_reader.offset"),
             std::string::npos);
    EXPECT_NE(source.find("tree_Item_decode(array_reader.buffer + array_reader.offset, "
                         "element_length)"),
             std::string::npos);
    EXPECT_NE(source.find("result.value.items[element_index] = element_result.value;"),
             std::string::npos);
    EXPECT_NE(source.find("array_reader.offset += element_length;"), std::string::npos);
    // Post-loop trailing-bytes check (only for variable-width/record
    // elements -- fixed-width arrays check total length up front instead).
    EXPECT_NE(source.find("if (array_reader.offset != array_reader.length) {"),
             std::string::npos);
    // PR-116 hardened the *fixed-width* array decode's up-front
    // total-length check for overflow safety; the record-array-element
    // path never had that multiplication (each element's length comes
    // from its own length-prefix varuint, not count * a fixed width), so
    // it needs no equivalent guard and must remain untouched by that
    // change -- confirmed by asserting no "element_width" variable
    // appears anywhere in this record-array field's generated decode.
    EXPECT_EQ(source.find("element_width"), std::string::npos);
}

TEST(BackendCTest, ArrayOfRecordFieldEncodedSizeUsesChildEncodedSizeWithoutEncoding) {
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
    items_field->mutable_type()->mutable_array()->mutable_element_type()->mutable_record()->
        set_target_record_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& source = result.files[1].content;

    const std::size_t encoded_size_start = source.find("tree_Group_encoded_size(");
    const std::size_t encode_start = source.find("tree_Group_encode(");
    ASSERT_NE(encoded_size_start, std::string::npos);
    ASSERT_NE(encode_start, std::string::npos);
    ASSERT_LT(encoded_size_start, encode_start);
    const std::string encoded_size_body =
        source.substr(encoded_size_start, encode_start - encoded_size_start);
    EXPECT_NE(encoded_size_body.find("tree_Item_encoded_size(&record->items[element_index])"),
             std::string::npos);
    // _encoded_size() must never call the child's real, validating
    // _encode() -- only its own _encoded_size(), matching the
    // string/bytes/array/plain-nested-record precedent exactly.
    EXPECT_EQ(encoded_size_body.find("tree_Item_encode(&record->items"), std::string::npos);
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
    field->mutable_type()->mutable_array()->mutable_element_type()->mutable_record()->
        set_target_record_ir_id(3);

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
    group_items->mutable_type()->mutable_array()->mutable_element_type()->mutable_record()->
        set_target_record_ir_id(4);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const std::string& header = result.files[0].content;
    EXPECT_NE(header.find("tree_Middle_t items[2];"), std::string::npos);
    const std::string& source = result.files[1].content;
    EXPECT_NE(source.find("tree_Middle_encoded_size(&record->items[element_index])"),
             std::string::npos);
    EXPECT_NE(source.find("tree_Middle_encode(&record->items[element_index], "
                         "writer.buffer + writer.length, writer.capacity - writer.length)"),
             std::string::npos);
    EXPECT_NE(source.find("tree_Middle_decode(array_reader.buffer + array_reader.offset, "
                         "element_length)"),
             std::string::npos);
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
    RecordIR* record =
        add_zero_field_record(*telemetry_ns, 5, 2U, "Sample", "telemetry.Sample");
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
    locations_field->mutable_type()->mutable_array()->mutable_element_type()->mutable_record()->
        set_target_record_ir_id(4);
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
