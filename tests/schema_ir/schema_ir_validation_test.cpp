#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"
#include "compiler/support/source_manager.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::context::CompilerContext;
using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::diagnostics::DiagnosticFormatter;
using breadcrumbs::compiler::schema_ir::SchemaIrModel;
using breadcrumbs::compiler::schema_ir::SchemaIrValidator;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceManager;

[[nodiscard]] SourceFileId add_test_source(CompilerContext& context) {
    return context.source_manager().add_source("/test/schema.ir", "");
}

[[nodiscard]] std::string diagnostics_summary(const DiagnosticEngine& diagnostics,
                                              const SourceManager& source_manager) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << DiagnosticFormatter::format(diagnostic, source_manager) << '\n';
    }
    return stream.str();
}

[[nodiscard]] SchemaIrModel make_schema_ir() {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    schema_ir.mutable_root_namespace()->set_ir_id(1);
    schema_ir.mutable_root_namespace()->set_name("");
    schema_ir.mutable_root_namespace()->set_fqn("");
    return schema_ir;
}

void set_source_origin(::breadcrumbs::schema_ir::SourceOrigin* origin, std::string_view path,
                       std::uint32_t start_offset, std::uint32_t end_offset,
                       std::uint32_t start_line = 1, std::uint32_t start_column = 1,
                       std::uint32_t end_line = 1, std::uint32_t end_column = 1) {
    origin->set_source_unit(std::string(path));
    origin->set_file(std::string(path));
    origin->mutable_span()->set_start_offset(start_offset);
    origin->mutable_span()->set_end_offset(end_offset);
    origin->mutable_span()->set_start_line(start_line);
    origin->mutable_span()->set_start_column(start_column);
    origin->mutable_span()->set_end_line(end_line);
    origin->mutable_span()->set_end_column(end_column);
}

template <typename Message>
void set_origin(Message* message, std::string_view path, std::uint32_t start_offset,
                std::uint32_t end_offset, std::uint32_t start_line = 1,
                std::uint32_t start_column = 1, std::uint32_t end_line = 1,
                std::uint32_t end_column = 1) {
    set_source_origin(message->mutable_source_origin(), path, start_offset, end_offset, start_line,
                      start_column, end_line, end_column);
}

void validate_schema_ir(const SchemaIrModel& schema_ir, CompilerContext& context,
                        DiagnosticEngine& diagnostics) {
    SchemaIrValidator validator;
    validator.validate(schema_ir, context, diagnostics);
}

TEST(SchemaIrValidationTest, AcceptsDirectRepresentativeSchemaIr) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();
    auto* breadcrumbs = root->add_namespaces();
    breadcrumbs->set_ir_id(2);
    breadcrumbs->set_name("breadcrumbs");
    breadcrumbs->set_fqn("breadcrumbs");
    set_origin(breadcrumbs, "/test/schema.ir", 0, 12);

    auto* telemetry = breadcrumbs->add_namespaces();
    telemetry->set_ir_id(3);
    telemetry->set_name("telemetry");
    telemetry->set_fqn("breadcrumbs.telemetry");
    set_origin(telemetry, "/test/schema.ir", 0, 23);

    auto* mode = telemetry->add_enums();
    mode->set_ir_id(4);
    mode->set_name("Mode");
    mode->set_fqn("breadcrumbs.telemetry.Mode");
    set_origin(mode, "/test/schema.ir", 24, 28);
    auto* off = mode->add_values();
    off->set_name("Off");
    off->set_value(0);
    set_origin(off, "/test/schema.ir", 30, 33);
    auto* on = mode->add_values();
    on->set_name("On");
    on->set_value(1);
    set_origin(on, "/test/schema.ir", 34, 36);

    auto* location = telemetry->add_records();
    location->set_ir_id(5);
    location->set_record_id(2);
    location->set_name("Location");
    location->set_fqn("breadcrumbs.telemetry.Location");
    set_origin(location, "/test/schema.ir", 38, 46);
    location->set_schema_version(1);
    location->set_record_type(::breadcrumbs::schema_ir::RECORD_TYPE_CONFIGURATION);
    auto* latitude = location->add_fields();
    latitude->set_name("latitude");
    latitude->set_field_index(0);
    set_origin(latitude, "/test/schema.ir", 48, 56);
    latitude->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_F64);

    auto* sample = telemetry->add_records();
    sample->set_ir_id(6);
    sample->set_record_id(3);
    sample->set_name("Sample");
    sample->set_fqn("breadcrumbs.telemetry.Sample");
    set_origin(sample, "/test/schema.ir", 58, 64);
    sample->set_schema_version(7);
    sample->set_record_type(::breadcrumbs::schema_ir::RECORD_TYPE_DATA);

    auto* active = sample->add_fields();
    active->set_name("active");
    active->set_field_index(0);
    set_origin(active, "/test/schema.ir", 66, 72);
    active->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL);

    auto* current_mode = sample->add_fields();
    current_mode->set_name("mode");
    current_mode->set_field_index(1);
    set_origin(current_mode, "/test/schema.ir", 74, 78);
    current_mode->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(mode->ir_id());

    auto* destination = sample->add_fields();
    destination->set_name("destination");
    destination->set_field_index(2);
    set_origin(destination, "/test/schema.ir", 80, 91);
    destination->mutable_type()->mutable_record()->set_target_record_ir_id(location->ir_id());

    auto* label = sample->add_fields();
    label->set_name("label");
    label->set_field_index(3);
    set_origin(label, "/test/schema.ir", 93, 98);
    label->mutable_type()->mutable_string()->set_max_bytes(16);

    auto* samples = sample->add_fields();
    samples->set_name("samples");
    samples->set_field_index(4);
    set_origin(samples, "/test/schema.ir", 100, 107);
    samples->mutable_type()->mutable_array()->set_max_elements(64);
    samples->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);
    EXPECT_TRUE(diagnostics.empty()) << diagnostics_summary(diagnostics, context.source_manager());
}

TEST(SchemaIrValidationTest, RejectsDuplicateNamespaceNamesInTheSameScope) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();
    auto* first = root->add_namespaces();
    first->set_ir_id(2);
    first->set_name("geo");
    first->set_fqn("geo");
    set_origin(first, "/test/schema.brd", 0, 0);

    auto* second = root->add_namespaces();
    second->set_ir_id(3);
    second->set_name("geo");
    second->set_fqn("geo");
    set_origin(second, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6002");
}

TEST(SchemaIrValidationTest, RejectsDuplicateRecordNamesInTheSameScope) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* first = root->add_records();
    first->set_ir_id(2);
    first->set_record_id(2);
    first->set_name("Location");
    first->set_fqn("Location");
    set_origin(first, "/test/schema.brd", 0, 0);

    auto* second = root->add_records();
    second->set_ir_id(3);
    second->set_record_id(3);
    second->set_name("Location");
    second->set_fqn("Location");
    set_origin(second, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6002");
}

TEST(SchemaIrValidationTest, RejectsMissingRecordId) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(0);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6010");
}

TEST(SchemaIrValidationTest, RejectsPresentZeroSchemaVersion) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(2);
    record->set_schema_version(0);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6012");
}

TEST(SchemaIrValidationTest, RejectsUnknownNumericRecordType) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(2);
    record->set_schema_version(1);
    record->set_record_type(static_cast<breadcrumbs::schema_ir::RecordType>(1234));
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6013");
}

TEST(SchemaIrValidationTest, AcceptsAbsentSchemaVersion) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(2);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    EXPECT_TRUE(diagnostics.empty()) << diagnostics_summary(diagnostics, context.source_manager());
}

TEST(SchemaIrValidationTest, AcceptsAbsentRecordType) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(2);
    record->set_schema_version(1);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    EXPECT_TRUE(diagnostics.empty()) << diagnostics_summary(diagnostics, context.source_manager());
}

TEST(SchemaIrValidationTest, AcceptsExplicitUnspecifiedRecordType) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(2);
    record->set_schema_version(1);
    record->set_record_type(breadcrumbs::schema_ir::RECORD_TYPE_UNSPECIFIED);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    EXPECT_TRUE(diagnostics.empty()) << diagnostics_summary(diagnostics, context.source_manager());
}

TEST(SchemaIrValidationTest, AllowsTheSameNameInDifferentNamespaces) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* geo = root->add_namespaces();
    geo->set_ir_id(2);
    geo->set_name("geo");
    geo->set_fqn("geo");
    set_origin(geo, "/test/schema.brd", 0, 0);

    auto* geo_record = geo->add_records();
    geo_record->set_ir_id(3);
    geo_record->set_record_id(3);
    geo_record->set_name("Location");
    geo_record->set_fqn("geo.Location");
    set_origin(geo_record, "/test/schema.brd", 0, 0);

    auto* telemetry = root->add_namespaces();
    telemetry->set_ir_id(4);
    telemetry->set_name("telemetry");
    telemetry->set_fqn("telemetry");
    set_origin(telemetry, "/test/schema.brd", 0, 0);

    auto* telemetry_record = telemetry->add_records();
    telemetry_record->set_ir_id(5);
    telemetry_record->set_record_id(5);
    telemetry_record->set_name("Location");
    telemetry_record->set_fqn("telemetry.Location");
    set_origin(telemetry_record, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    EXPECT_TRUE(diagnostics.empty()) << diagnostics_summary(diagnostics, context.source_manager());
}

TEST(SchemaIrValidationTest, RejectsDuplicateRecordIdsInTheSameNamespace) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* first = root->add_records();
    first->set_ir_id(2);
    first->set_record_id(10);
    first->set_name("Location");
    first->set_fqn("Location");
    set_origin(first, "/test/schema.ir", 0, 0);

    auto* second = root->add_records();
    second->set_ir_id(3);
    second->set_record_id(10);
    second->set_name("Route");
    second->set_fqn("Route");
    set_origin(second, "/test/schema.ir", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6011");
    ASSERT_EQ(diagnostics.diagnostics()[0].related_locations().size(), 1U);
    const auto& related_location = diagnostics.diagnostics()[0].related_locations()[0];
    ASSERT_TRUE(related_location.range().has_value());
    EXPECT_EQ(related_location.message(), "previous record with this id is here");
    EXPECT_EQ(related_location.range()->begin().byte_offset(), 0U);
    EXPECT_EQ(related_location.range()->end().byte_offset(), 0U);
}

TEST(SchemaIrValidationTest, RejectsDuplicateRecordIdsInDifferentNamespaces) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* geo = root->add_namespaces();
    geo->set_ir_id(2);
    geo->set_name("geo");
    geo->set_fqn("geo");
    set_origin(geo, "/test/schema.brd", 0, 0);

    auto* first = geo->add_records();
    first->set_ir_id(3);
    first->set_record_id(11);
    first->set_name("Location");
    first->set_fqn("geo.Location");
    set_origin(first, "/test/schema.brd", 0, 0);

    auto* telemetry = root->add_namespaces();
    telemetry->set_ir_id(4);
    telemetry->set_name("telemetry");
    telemetry->set_fqn("telemetry");
    set_origin(telemetry, "/test/schema.brd", 0, 0);

    auto* second = telemetry->add_records();
    second->set_ir_id(5);
    second->set_record_id(11);
    second->set_name("Location");
    second->set_fqn("telemetry.Location");
    set_origin(second, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6011");
}

TEST(SchemaIrValidationTest, RejectsDuplicateFieldNames) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(6);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("origin");
    field->set_field_index(0);
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL);

    auto* duplicate_field = record->add_fields();
    duplicate_field->set_name("origin");
    duplicate_field->set_field_index(1);
    set_origin(duplicate_field, "/test/schema.brd", 0, 0);
    duplicate_field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6003");
}

TEST(SchemaIrValidationTest, AllowsGappedFieldIndexes) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(7);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("origin");
    field->set_field_index(0);
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL);

    auto* second = record->add_fields();
    second->set_name("destination");
    second->set_field_index(2);
    set_origin(second, "/test/schema.brd", 0, 0);
    second->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    EXPECT_TRUE(diagnostics.empty()) << diagnostics_summary(diagnostics, context.source_manager());
}

TEST(SchemaIrValidationTest, RejectsDuplicateFieldIndexes) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(8);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("origin");
    field->set_field_index(0);
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL);

    auto* duplicate_field = record->add_fields();
    duplicate_field->set_name("destination");
    duplicate_field->set_field_index(0);
    set_origin(duplicate_field, "/test/schema.brd", 0, 0);
    duplicate_field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6008");
}

TEST(SchemaIrValidationTest, RejectsFieldIndexesAboveUint8Limit) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(19);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("origin");
    field->set_field_index(256);
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6009");
}

TEST(SchemaIrValidationTest, RejectsDuplicateEnumValueNames) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* enum_ir = schema_ir.mutable_root_namespace()->add_enums();
    enum_ir->set_ir_id(2);
    enum_ir->set_name("Direction");
    enum_ir->set_fqn("Direction");
    set_origin(enum_ir, "/test/schema.brd", 0, 0);

    auto* first = enum_ir->add_values();
    first->set_name("north");
    first->set_value(0);
    set_origin(first, "/test/schema.brd", 0, 0);

    auto* duplicate = enum_ir->add_values();
    duplicate->set_name("north");
    duplicate->set_value(1);
    set_origin(duplicate, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6003");
}

TEST(SchemaIrValidationTest, RejectsDuplicateIds) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* first = root->add_records();
    first->set_ir_id(2);
    first->set_record_id(9);
    first->set_name("Location");
    first->set_fqn("Location");
    set_origin(first, "/test/schema.brd", 0, 0);

    auto* second = root->add_enums();
    second->set_ir_id(2);
    second->set_name("Mode");
    second->set_fqn("Mode");
    set_origin(second, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6004");
}

TEST(SchemaIrValidationTest, RejectsMissingRecordReferences) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(12);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("origin");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->mutable_record()->set_target_record_ir_id(999);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6005");
}

TEST(SchemaIrValidationTest, RejectsWrongKindRecordReferences) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* enum_ir = root->add_enums();
    enum_ir->set_ir_id(2);
    enum_ir->set_name("Mode");
    enum_ir->set_fqn("Mode");
    set_origin(enum_ir, "/test/schema.brd", 0, 0);

    auto* record = root->add_records();
    record->set_ir_id(3);
    record->set_record_id(13);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("mode");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->mutable_record()->set_target_record_ir_id(2);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6006");
}

TEST(SchemaIrValidationTest, RejectsMissingEnumReferences) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(14);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("mode");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(999);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6005");
}

TEST(SchemaIrValidationTest, RejectsWrongKindEnumReferences) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* record = root->add_records();
    record->set_ir_id(2);
    record->set_record_id(15);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* enum_ir = root->add_enums();
    enum_ir->set_ir_id(3);
    enum_ir->set_name("Mode");
    enum_ir->set_fqn("Mode");
    set_origin(enum_ir, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("mode");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(2);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6006");
}

TEST(SchemaIrValidationTest, RejectsMissingFieldTypes) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(16);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("origin");
    set_origin(field, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6007");
}

TEST(SchemaIrValidationTest, RejectsInvalidArrayElementTypes) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(17);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("samples");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->mutable_array()->set_max_elements(1);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6007");
}

TEST(SchemaIrValidationTest, UsesSourceMetadataInDiagnosticsWhenAvailable) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_record_id(18);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.ir", 0, 0, 1, 1, 1, 1);

    auto* field = record->add_fields();
    field->set_name("origin");
    field->set_field_index(0);
    set_origin(field, "/test/schema.ir", 0, 0, 1, 1, 1, 1);
    field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL);

    auto* duplicate = record->add_fields();
    duplicate->set_name("origin");
    duplicate->set_field_index(1);
    set_origin(duplicate, "/test/schema.ir", 0, 0, 1, 1, 1, 1);
    duplicate->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    const std::string formatted =
        DiagnosticFormatter::format(diagnostics.diagnostics()[0], context.source_manager());
    EXPECT_NE(formatted.find("/test/schema.ir:1:1"), std::string::npos);
    EXPECT_NE(formatted.find("BC6003"), std::string::npos);
}

} // namespace
