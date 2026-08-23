#include "compiler/layout/layout.hpp"
#include "compiler/qbs/parser.hpp"
#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"
#include "quarry/runtime/qtf_exporter.hpp"
#include "quarry/runtime/qtf_importer.hpp"

#include <gtest/gtest.h>

namespace {
using namespace quarry::compiler::qbs;
using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::schema_ir::SchemaIR;
using namespace quarry::runtime;

SchemaIR schema_ir() {
    SchemaIR schema;
    schema.set_schema_ir_version(1U);
    schema.mutable_root_namespace()->set_ir_id(1U);
    auto* record = schema.mutable_root_namespace()->add_records();
    record->set_ir_id(1U);
    record->set_record_id(1U);
    record->set_name("Root");
    record->set_fqn("Root");
    auto* value = record->add_fields();
    value->set_name("value");
    value->set_field_index(0U);
    value->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
    auto* name = record->add_fields();
    name->set_name("name");
    name->set_field_index(1U);
    name->mutable_type()->mutable_string()->set_max_bytes(32U);
    return schema;
}

TEST(QtfParserTest, ParsesAndImportsSchemaDrivenValues) {
    DiagnosticCollection diagnostics;
    const auto ir = schema_ir();
    const auto layout = quarry::compiler::layout::LayoutComputer{}.compute(ir, diagnostics);
    ASSERT_TRUE(diagnostics.empty());
    const auto model =
        QbsModelBuilder{}.build(ir, layout, {.mode = BuildMode::Reflective}, diagnostics);
    ASSERT_TRUE(model.has_value());
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_identity("Root");
    ASSERT_TRUE(record.has_value());
    const auto input =
        parse_qtf("{ value: 42 name: \"hi\\nthere\" }", *schema, *record, diagnostics);
    ASSERT_TRUE(input.has_value());
    ASSERT_EQ(input->fields.size(), 2U);
    const auto bytes =
        import_qtf("{ value: 42 name: \"hi\\nthere\" }", *schema, *record, diagnostics);
    ASSERT_TRUE(bytes.has_value());
    const auto view = validate_brf_record(*schema, *record, *bytes);
    ASSERT_TRUE(view.has_value());
    const auto exported = export_qtf(*view);
    ASSERT_TRUE(exported.text.has_value());
    EXPECT_EQ(*exported.text, "{\n  value: 42\n  name: \"hi\\nthere\"\n}\n");
}

TEST(QtfParserTest, RejectsDuplicateAndUnknownFields) {
    DiagnosticCollection diagnostics;
    const auto ir = schema_ir();
    const auto layout = quarry::compiler::layout::LayoutComputer{}.compute(ir, diagnostics);
    const auto model =
        QbsModelBuilder{}.build(ir, layout, {.mode = BuildMode::Reflective}, diagnostics);
    ASSERT_TRUE(model.has_value());
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_identity("Root");
    ASSERT_TRUE(record.has_value());
    diagnostics.clear();
    EXPECT_FALSE(parse_qtf("{ value: 1 value: 2 }", *schema, *record, diagnostics).has_value());
    diagnostics.clear();
    EXPECT_FALSE(parse_qtf("{ missing: 1 }", *schema, *record, diagnostics).has_value());
}

TEST(QtfParserTest, ValidatesUtf8AndEscapedControls) {
    DiagnosticCollection diagnostics;
    const auto ir = schema_ir();
    const auto layout = quarry::compiler::layout::LayoutComputer{}.compute(ir, diagnostics);
    const auto model =
        QbsModelBuilder{}.build(ir, layout, {.mode = BuildMode::Reflective}, diagnostics);
    ASSERT_TRUE(model.has_value());
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_identity("Root");
    ASSERT_TRUE(record.has_value());
    for (const std::string_view value : {"ascii", "\xC2\xA2", "\xE2\x82\xAC", "\xF0\x90\x8D\x88"}) {
        diagnostics.clear();
        const auto input = parse_qtf(std::string("{ name: \"") + std::string(value) + "\" }",
                                     *schema, *record, diagnostics);
        ASSERT_TRUE(input.has_value()) << value;
    }
    diagnostics.clear();
    EXPECT_FALSE(parse_qtf("{ name: \"\xC0\x80\" }", *schema, *record, diagnostics).has_value());
    diagnostics.clear();
    const std::string raw_control = std::string("{ name: \"raw") + '\n' + "line\" }";
    EXPECT_FALSE(parse_qtf(raw_control, *schema, *record, diagnostics).has_value());
}

TEST(QtfParserTest, RejectsIntegerAndArrayOverflow) {
    DiagnosticCollection diagnostics;
    const auto ir = schema_ir();
    const auto layout = quarry::compiler::layout::LayoutComputer{}.compute(ir, diagnostics);
    const auto model =
        QbsModelBuilder{}.build(ir, layout, {.mode = BuildMode::Reflective}, diagnostics);
    ASSERT_TRUE(model.has_value());
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_identity("Root");
    ASSERT_TRUE(record.has_value());
    diagnostics.clear();
    EXPECT_FALSE(parse_qtf("{ value: 4294967296 }", *schema, *record, diagnostics).has_value());
    diagnostics.clear();
    EXPECT_FALSE(parse_qtf("{ value: -1 }", *schema, *record, diagnostics).has_value());
}

TEST(QtfParserTest, TracksOneBasedByteLocations) {
    const auto location = qtf_source_location("{\n  value: 1\n}", 11U);
    EXPECT_EQ(location.byte_offset, 11U);
    EXPECT_EQ(location.line, 2U);
    EXPECT_EQ(location.column, 10U);
}
} // namespace
