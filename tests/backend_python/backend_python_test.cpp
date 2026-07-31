#include "compiler/backend_python/backend_python.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using quarry::compiler::backend_python::Backend;
using quarry::compiler::backend_python::CodegenOptions;
using quarry::compiler::backend_python::CodegenResult;
using quarry::compiler::backend_python::GeneratedFile;
using quarry::compiler::backend_python::PlanResult;
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
// tests/backend_c/backend_c_test.cpp uses: backend_python should only ever
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

[[nodiscard]] const GeneratedFile* find_file(const CodegenResult& result, std::string_view path) {
    for (const GeneratedFile& file : result.files) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

TEST(BackendPythonTest, EmptySchemaProducesNoFiles) {
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

TEST(BackendPythonTest, RootNamespaceZeroFieldRecordUsesRootModuleStemWithNoPackage) {
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
    EXPECT_EQ(plan_result.plan.files[0].relative_output_path, "schema.py");
}

TEST(BackendPythonTest, NestedNamespaceProducesPackageDirectoriesAndInitFiles) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* acme_ns = add_child_namespace(*root, 2, "acme", "acme");
    NamespaceIR* telemetry_ns = add_child_namespace(*acme_ns, 3, "telemetry", "acme.telemetry");
    (void)add_zero_field_record(*telemetry_ns, 4, 1U, "Sample", "acme.telemetry.Sample");
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 3U);

    const GeneratedFile* acme_init = find_file(result, "generated/acme/__init__.py");
    const GeneratedFile* telemetry_init = find_file(result, "generated/acme/telemetry/__init__.py");
    const GeneratedFile* module = find_file(result, "generated/acme/telemetry/schema.py");
    ASSERT_NE(acme_init, nullptr);
    ASSERT_NE(telemetry_init, nullptr);
    ASSERT_NE(module, nullptr);

    EXPECT_NE(module->content.find("class Sample:"), std::string::npos);
}

TEST(BackendPythonTest, SiblingNamespacesShareAncestorInitFileExactlyOnce) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* acme_ns = add_child_namespace(*root, 2, "acme", "acme");
    NamespaceIR* telemetry_ns = add_child_namespace(*acme_ns, 3, "telemetry", "acme.telemetry");
    NamespaceIR* control_ns = add_child_namespace(*acme_ns, 4, "control", "acme.control");
    (void)add_zero_field_record(*telemetry_ns, 5, 1U, "Sample", "acme.telemetry.Sample");
    (void)add_zero_field_record(*control_ns, 6, 2U, "Command", "acme.control.Command");
    assert_valid(schema_ir);

    Backend backend;
    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    ASSERT_TRUE(plan_result.success) << plan_result.error_message;

    int acme_init_count = 0;
    for (const auto& file : plan_result.plan.files) {
        if (file.relative_output_path == "acme/__init__.py") {
            ++acme_init_count;
        }
    }
    EXPECT_EQ(acme_init_count, 1);
    ASSERT_EQ(plan_result.plan.files.size(), 5U); // acme/, acme/telemetry/, acme/control/ inits +
                                                  // 2 modules
}

TEST(BackendPythonTest, ZeroFieldRecordEmitsExactTemplate) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    (void)add_zero_field_record(*root, 2, 1U, "Sample", "Sample");
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1U);

    const std::string expected_template =
        "from dataclasses import dataclass\n"
        "from typing import Optional\n"
        "\n"
        "@dataclass\n"
        "class Sample:\n"
        "\n"
        "    def encode(self):\n"
        "        return _encode_sample(self)\n"
        "\n"
        "    @classmethod\n"
        "    def decode(cls, data):\n"
        "        return _decode_sample(data)\n"
        "\n"
        "    def encoded_size(self):\n"
        "        return _encoded_size_sample(self)\n"
        "\n"
        "\n"
        "def _encode_sample(value):\n"
        "    fields = []\n"
        "    return _brf.encode_record(1, fields)\n"
        "\n"
        "\n"
        "def _decode_sample(data):\n"
        "    record_id, fields = _brf.parse_record(data)\n"
        "    if record_id != 1:\n"
        "        raise _brf.DecodeError(\n"
        "            f\"unexpected record id: {record_id} (expected 1)\")\n"
        "    return Sample()\n"
        "\n"
        "\n"
        "def _encoded_size_sample(value):\n"
        "    return len(_encode_sample(value))\n";

    EXPECT_NE(result.files[0].content.find(expected_template), std::string::npos)
        << "generated content:\n"
        << result.files[0].content;
}

TEST(BackendPythonTest, HelperFunctionNamesUseSnakeCaseConversion) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    (void)add_zero_field_record(*root, 2, 1U, "SensorReading", "SensorReading");
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1U);

    EXPECT_NE(result.files[0].content.find("class SensorReading:"), std::string::npos);
    EXPECT_NE(result.files[0].content.find("def _encode_sensor_reading(value):"),
             std::string::npos);
    EXPECT_NE(result.files[0].content.find("def _decode_sensor_reading(data):"),
             std::string::npos);
    EXPECT_NE(result.files[0].content.find("def _encoded_size_sensor_reading(value):"),
             std::string::npos);
}

TEST(BackendPythonTest, PublicMethodsDelegateToHelpersRatherThanDuplicatingLogic) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    (void)add_zero_field_record(*root, 2, 1U, "Sample", "Sample");
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1U);

    const std::string& content = result.files[0].content;
    EXPECT_NE(content.find("        return _encode_sample(self)\n"), std::string::npos);
    EXPECT_NE(content.find("        return _decode_sample(data)\n"), std::string::npos);
    EXPECT_NE(content.find("        return _encoded_size_sample(self)\n"), std::string::npos);

    // Only the helper functions raise NotImplementedError; the public
    // methods delegate rather than duplicating that behavior themselves.
    EXPECT_EQ(content.find("    def encode(self):\n        raise"), std::string::npos);
    EXPECT_EQ(content.find("    def decode(cls, data):\n        raise"), std::string::npos);
    EXPECT_EQ(content.find("    def encoded_size(self):\n        raise"), std::string::npos);
}

TEST(BackendPythonTest, EpochCheckPreambleImportsRuntimeAndRaisesOnMismatch) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    (void)add_zero_field_record(*root, 2, 1U, "Sample", "Sample");
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1U);

    const std::string& content = result.files[0].content;
    EXPECT_NE(content.find("from quarry.runtime.python import QUARRY_GENERATED_CODE_API_VERSION_PYTHON"),
             std::string::npos);
    EXPECT_NE(content.find("if QUARRY_GENERATED_CODE_API_VERSION_PYTHON != 1:"), std::string::npos);
    EXPECT_NE(content.find("raise ImportError("), std::string::npos);
    // The epoch check must precede the dataclass import/definition.
    EXPECT_LT(content.find("QUARRY_GENERATED_CODE_API_VERSION_PYTHON"),
             content.find("from dataclasses import dataclass"));
}

TEST(BackendPythonTest, UnsupportedFieldTypeFailsGenerationNamingRecordAndField) {
    // string (like bytes, array, and record-reference fields) remains
    // unsupported -- matching the same "swap the representative
    // unsupported-type example once it becomes supported" maintenance
    // pattern the C backend's own test history established each time it
    // added a new field category (enum became supported in PR-120).
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    RecordIR* record = add_zero_field_record(*root, 2, 1U, "Sample", "Sample");
    FieldIR* field = record->add_fields();
    field->set_name("label");
    field->set_field_index(0);
    field->mutable_type()->mutable_string()->set_max_bytes(16);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("Sample"), std::string::npos);
    EXPECT_NE(result.error_message.find("label"), std::string::npos);

    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    EXPECT_FALSE(plan_result.success);
}

TEST(BackendPythonTest, AllElevenScalarTypesGenerateSuccessfully) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    RecordIR* record = add_zero_field_record(*root, 2, 1U, "Sample", "Sample");

    const std::vector<std::pair<std::string, ::quarry::schema_ir::PrimitiveType>> kFields = {
        {"f_bool", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_BOOL},
        {"f_i8", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_I8},
        {"f_u8", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_U8},
        {"f_i16", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_I16},
        {"f_u16", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_U16},
        {"f_i32", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_I32},
        {"f_u32", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_U32},
        {"f_i64", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_I64},
        {"f_u64", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_U64},
        {"f_f32", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_F32},
        {"f_f64", ::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_F64},
    };
    for (std::size_t index = 0; index < kFields.size(); ++index) {
        FieldIR* field = record->add_fields();
        field->set_name(kFields[index].first);
        field->set_field_index(static_cast<std::uint32_t>(index));
        field->mutable_type()->set_primitive(kFields[index].second);
    }
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1U);

    const std::string& content = result.files[0].content;
    EXPECT_NE(content.find("f_bool: Optional[bool] = None"), std::string::npos);
    EXPECT_NE(content.find("f_i8: Optional[int] = None"), std::string::npos);
    EXPECT_NE(content.find("f_u8: Optional[int] = None"), std::string::npos);
    EXPECT_NE(content.find("f_f32: Optional[float] = None"), std::string::npos);
    EXPECT_NE(content.find("f_f64: Optional[float] = None"), std::string::npos);
    EXPECT_NE(content.find("_brf.pack_scalar(\"bool\", value.f_bool)"), std::string::npos);
    EXPECT_NE(content.find("_brf.pack_scalar(\"int8\", value.f_i8)"), std::string::npos);
    EXPECT_NE(content.find("_brf.pack_scalar(\"uint64\", value.f_u64)"), std::string::npos);
    EXPECT_NE(content.find("_brf.pack_scalar(\"float32\", value.f_f32)"), std::string::npos);
    EXPECT_NE(content.find("_brf.unpack_scalar(\"float64\", fields[10])"), std::string::npos);
}

TEST(BackendPythonTest, ScalarRecordEncodeDecodeHelpersReferenceRuntimeCorrectly) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    RecordIR* record = add_zero_field_record(*root, 2, 7U, "Sample", "Sample");
    FieldIR* field = record->add_fields();
    field->set_name("count");
    field->set_field_index(0);
    field->mutable_type()->set_primitive(::quarry::schema_ir::PrimitiveType::PRIMITIVE_TYPE_U32);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1U);

    const std::string& content = result.files[0].content;
    EXPECT_NE(content.find("from quarry.runtime.python import binary_record as _brf"),
             std::string::npos);
    EXPECT_NE(content.find("if value.count is not None:\n"
                          "        fields.append((0, _brf.pack_scalar(\"uint32\", "
                          "value.count)))\n"),
             std::string::npos);
    EXPECT_NE(content.find("return _brf.encode_record(7, fields)"), std::string::npos);
    EXPECT_NE(content.find("record_id, fields = _brf.parse_record(data)"), std::string::npos);
    EXPECT_NE(content.find("if record_id != 7:"), std::string::npos);
    EXPECT_NE(content.find("count = _brf.unpack_scalar(\"uint32\", fields[0])"), std::string::npos);
    EXPECT_NE(content.find("return Sample(count=count)"), std::string::npos);
}

TEST(BackendPythonTest, EnumFieldGeneratesIntEnumClassBeforeReferencingRecord) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    EnumIR* enum_ir = add_enum(*root, 2, "Status", "Status");
    add_enum_value(*enum_ir, "OK", 0);
    add_enum_value(*enum_ir, "WARNING", 1);
    add_enum_value(*enum_ir, "ERROR", 2);
    RecordIR* record = add_zero_field_record(*root, 3, 1U, "Sample", "Sample");
    FieldIR* field = record->add_fields();
    field->set_name("status");
    field->set_field_index(0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(2);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1U);

    const std::string& content = result.files[0].content;
    EXPECT_NE(content.find("from enum import IntEnum"), std::string::npos);
    EXPECT_NE(content.find("class Status(IntEnum):\n"
                          "    OK = 0\n"
                          "    WARNING = 1\n"
                          "    ERROR = 2\n"),
             std::string::npos);
    EXPECT_NE(content.find("status: Optional[Status] = None"), std::string::npos);
    EXPECT_NE(content.find("fields.append((0, _brf.pack_enum(Status, value.status, \"uint8\")))"),
             std::string::npos);
    EXPECT_NE(content.find("status = _brf.unpack_enum(Status, \"uint8\", fields[0])"),
             std::string::npos);
    // The enum class must be fully defined before the record that
    // references it, since the dataclass annotation evaluates eagerly.
    EXPECT_LT(content.find("class Status(IntEnum):"), content.find("class Sample:"));
}

TEST(BackendPythonTest, EnumWidthMatchesSmallestUnsignedTypeForMaxDeclaredValue) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    EnumIR* enum_ir = add_enum(*root, 2, "Big", "Big");
    add_enum_value(*enum_ir, "SMALL", 0);
    add_enum_value(*enum_ir, "LARGE", 300); // exceeds uint8, needs uint16
    RecordIR* record = add_zero_field_record(*root, 3, 1U, "Sample", "Sample");
    FieldIR* field = record->add_fields();
    field->set_name("value");
    field->set_field_index(0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(2);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1U);

    const std::string& content = result.files[0].content;
    EXPECT_NE(content.find("_brf.pack_enum(Big, value.value, \"uint16\")"), std::string::npos);
    EXPECT_NE(content.find("_brf.unpack_enum(Big, \"uint16\", fields[0])"), std::string::npos);
}

TEST(BackendPythonTest, CrossNamespaceEnumFieldFailsGenerationNamingRecordAndField) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* enums_ns = add_child_namespace(*root, 2, "enums", "enums");
    EnumIR* enum_ir = add_enum(*enums_ns, 3, "Status", "enums.Status");
    add_enum_value(*enum_ir, "OK", 0);
    NamespaceIR* records_ns = add_child_namespace(*root, 4, "records", "records");
    RecordIR* record = add_zero_field_record(*records_ns, 5, 1U, "Sample", "records.Sample");
    FieldIR* field = record->add_fields();
    field->set_name("status");
    field->set_field_index(0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(3);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("records.Sample.status"), std::string::npos);
    EXPECT_NE(result.error_message.find("cross-namespace"), std::string::npos);

    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    EXPECT_FALSE(plan_result.success);
}

TEST(BackendPythonTest, NegativeEnumValueFieldFailsGenerationNamingRecordAndField) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    EnumIR* enum_ir = add_enum(*root, 2, "Status", "Status");
    add_enum_value(*enum_ir, "UNKNOWN", -1);
    add_enum_value(*enum_ir, "OK", 0);
    RecordIR* record = add_zero_field_record(*root, 3, 1U, "Sample", "Sample");
    FieldIR* field = record->add_fields();
    field->set_name("status");
    field->set_field_index(0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(2);
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("Sample.status"), std::string::npos);
    EXPECT_NE(result.error_message.find("negative declared value"), std::string::npos);

    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    EXPECT_FALSE(plan_result.success);
}

TEST(BackendPythonTest, NamespaceWithOnlyEnumsEmitsAFileContainingJustTheEnum) {
    // PR-120: enum-only namespaces now emit a file (fixing PR-118/PR-119's
    // documented "known limitation"). No record class appears since this
    // namespace declares none.
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* ns = add_child_namespace(*root, 2, "acme", "acme");
    EnumIR* enum_ir = add_enum(*ns, 3, "Status", "acme.Status");
    add_enum_value(*enum_ir, "OK", 0);
    add_enum_value(*enum_ir, "WARNING", 1);
    assert_valid(schema_ir);

    Backend backend;
    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    ASSERT_TRUE(plan_result.success) << plan_result.error_message;
    ASSERT_EQ(plan_result.plan.files.size(), 2U); // acme/__init__.py + acme/schema.py

    const CodegenResult result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    const GeneratedFile* module = find_file(result, "generated/acme/schema.py");
    ASSERT_NE(module, nullptr);
    EXPECT_NE(module->content.find("from enum import IntEnum"), std::string::npos);
    EXPECT_NE(module->content.find("class Status(IntEnum):\n"
                                  "    OK = 0\n"
                                  "    WARNING = 1\n"),
             std::string::npos);
    EXPECT_EQ(module->content.find("@dataclass"), std::string::npos);
    EXPECT_EQ(module->content.find("class Sample"), std::string::npos);
}

TEST(BackendPythonTest, PlanAndGenerateAgreeOnOutputPaths) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* acme_ns = add_child_namespace(*root, 2, "acme", "acme");
    (void)add_zero_field_record(*acme_ns, 3, 1U, "Sample", "acme.Sample");
    assert_valid(schema_ir);

    Backend backend;
    const PlanResult plan_result = backend.plan(schema_ir, CodegenOptions{});
    const CodegenResult codegen_result = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(plan_result.success) << plan_result.error_message;
    ASSERT_TRUE(codegen_result.success) << codegen_result.error_message;
    ASSERT_EQ(plan_result.plan.files.size(), codegen_result.files.size());

    for (std::size_t index = 0; index < plan_result.plan.files.size(); ++index) {
        EXPECT_EQ(quarry::compiler::backend_python::output_path_for_planned_file(
                      CodegenOptions{}, plan_result.plan.files[index].relative_output_path),
                  codegen_result.files[index].path);
    }
}

TEST(BackendPythonTest, GenerationIsDeterministicAcrossRepeatedCalls) {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    NamespaceIR* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    NamespaceIR* acme_ns = add_child_namespace(*root, 2, "acme", "acme");
    NamespaceIR* telemetry_ns = add_child_namespace(*acme_ns, 3, "telemetry", "acme.telemetry");
    NamespaceIR* control_ns = add_child_namespace(*acme_ns, 4, "control", "acme.control");
    (void)add_zero_field_record(*telemetry_ns, 5, 1U, "Sample", "acme.telemetry.Sample");
    (void)add_zero_field_record(*control_ns, 6, 2U, "Command", "acme.control.Command");
    assert_valid(schema_ir);

    Backend backend;
    const CodegenResult first = backend.generate(schema_ir, CodegenOptions{});
    const CodegenResult second = backend.generate(schema_ir, CodegenOptions{});
    ASSERT_TRUE(first.success) << first.error_message;
    ASSERT_TRUE(second.success) << second.error_message;
    ASSERT_EQ(first.files.size(), second.files.size());
    for (std::size_t index = 0; index < first.files.size(); ++index) {
        EXPECT_EQ(first.files[index].path, second.files[index].path);
        EXPECT_EQ(first.files[index].content, second.files[index].content);
    }
}

TEST(BackendPythonTest, OutputPathForPlannedFileHonorsOutputDirectory) {
    CodegenOptions options;
    options.output_directory = "out";
    EXPECT_EQ(quarry::compiler::backend_python::output_path_for_planned_file(options, "a/b.py"),
             "out/a/b.py");

    options.output_directory.clear();
    EXPECT_EQ(quarry::compiler::backend_python::output_path_for_planned_file(options, "a/b.py"),
             "a/b.py");
}

} // namespace
