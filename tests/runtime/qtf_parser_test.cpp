#include "compiler/layout/layout.hpp"
#include "compiler/qbs/parser.hpp"
#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"
#include "quarry/runtime/qtf_exporter.hpp"
#include "quarry/runtime/qtf_importer.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace {
using namespace quarry::compiler::qbs;
using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::schema_ir::SchemaIR;
using namespace quarry::runtime;

SchemaIR schema_ir() {
    SchemaIR schema;
    schema.set_schema_ir_version(1U);
    schema.mutable_root_namespace()->set_ir_id(1U);
    auto* child = schema.mutable_root_namespace()->add_records();
    child->set_ir_id(2U);
    child->set_record_id(2U);
    child->set_name("Child");
    child->set_fqn("Child");
    auto* child_value = child->add_fields();
    child_value->set_name("value");
    child_value->set_field_index(0U);
    child_value->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
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
    auto* f32 = record->add_fields();
    f32->set_name("f32");
    f32->set_field_index(2U);
    f32->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_F32);
    auto* f64 = record->add_fields();
    f64->set_name("f64");
    f64->set_field_index(3U);
    f64->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_F64);
    auto* samples = record->add_fields();
    samples->set_name("samples");
    samples->set_field_index(4U);
    samples->mutable_type()->mutable_array()->set_max_elements(2U);
    samples->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        quarry::schema_ir::PRIMITIVE_TYPE_U32);
    auto* nested = record->add_fields();
    nested->set_name("child");
    nested->set_field_index(5U);
    nested->mutable_type()->mutable_record()->set_target_record_ir_id(2U);
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
    ASSERT_EQ(input->fields.size(), 6U);
    const auto bytes =
        import_qtf("{ value: 42 name: \"hi\\nthere\" }", *schema, *record, diagnostics);
    if (!bytes.has_value()) {
        for (const auto& diagnostic : diagnostics.diagnostics())
            ADD_FAILURE() << diagnostic.message();
    }
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

TEST(QtfParserTest, CoversFloatArrayAndNestedRecordParsing) {
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
    const auto text = "{ f32: -0.0 f64: 1.5 samples: [1, 2] child: { value: 7 } }";
    const auto input = parse_qtf(text, *schema, *record, diagnostics);
    ASSERT_TRUE(input.has_value());
    const auto bytes = import_qtf(text, *schema, *record, diagnostics);
    ASSERT_TRUE(bytes.has_value());
    const auto view = validate_brf_record(*schema, *record, *bytes);
    ASSERT_TRUE(view.has_value());
    const auto exported = export_qtf(*view);
    ASSERT_TRUE(exported.text.has_value());
    EXPECT_NE(exported.text->find("f32: -0"), std::string::npos);
    EXPECT_NE(exported.text->find("samples: [1, 2]"), std::string::npos);
    EXPECT_NE(exported.text->find("child: {"), std::string::npos);
}

TEST(QtfParserTest, RejectsMalformedArraysAndFloats) {
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
    for (const auto text :
         {"{ samples: [1,] }", "{ samples: [1 2] }", "{ f64: 1.2.3 }", "{ f64: INF }"}) {
        diagnostics.clear();
        EXPECT_FALSE(parse_qtf(text, *schema, *record, diagnostics).has_value()) << text;
    }
}

TEST(QtfParserTest, InfinityPreservesSelectedFloatTypeThroughImportAndDecode) {
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
    for (const auto token : {"inf", "-inf"}) {
        SCOPED_TRACE(token);
        diagnostics.clear();
        const auto text = std::string("{ f32: ") + token + " f64: " + token + " }";
        const auto parsed = parse_qtf(text, *schema, *record, diagnostics);
        ASSERT_TRUE(parsed.has_value());
        ASSERT_TRUE(parsed->fields[2U].has_value());
        ASSERT_TRUE(parsed->fields[3U].has_value());
        const auto* f32 = std::get_if<float>(&*parsed->fields[2U]);
        const auto* f64 = std::get_if<double>(&*parsed->fields[3U]);
        EXPECT_NE(f32, nullptr);
        EXPECT_NE(f64, nullptr);
        const auto bytes = import_qtf(text, *schema, *record, diagnostics);
        EXPECT_TRUE(bytes.has_value());
        if (!bytes)
            continue;
        const auto view = validate_brf_record(*schema, *record, *bytes);
        ASSERT_TRUE(view.has_value());
        const auto decoded32 = view->field(2U)->as_float32();
        const auto decoded64 = view->field(3U)->as_float64();
        ASSERT_TRUE(decoded32.has_value());
        ASSERT_TRUE(decoded64.has_value());
        EXPECT_TRUE(std::isinf(*decoded32));
        EXPECT_TRUE(std::isinf(*decoded64));
        EXPECT_EQ(std::signbit(*decoded32), token[0] == '-');
        EXPECT_EQ(std::signbit(*decoded64), token[0] == '-');
        const auto exported = export_qtf(*view);
        ASSERT_TRUE(exported.text.has_value());
        EXPECT_NE(exported.text->find(std::string("f32: ") + token), std::string::npos);
        EXPECT_NE(exported.text->find(std::string("f64: ") + token), std::string::npos);
    }
}

TEST(QtfParserTest, OptInCommentsAreWhitespaceAndDoNotSurviveImport) {
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
    const auto plain = "{ value: 42 name: \"#inside\" samples: [1, 2] child: { value: 7 } }";
    const auto expected = import_qtf(plain, *schema, *record, diagnostics);
    ASSERT_TRUE(expected.has_value());
    const std::vector<std::string> comments{
        std::string("# beginning\n") + plain + "# end at EOF",
        std::string("# beginning\r\n") + plain + "\r\n# end\r\n",
        "{# record\nvalue# field\n:# colon\n42# number\nname:\"#inside\"# string\n"
        "samples:[1# element\n,# comma\n2] child:{value:7} }# EOF",
        "{\r\nvalue:42# value\r\nname:\"#inside\" samples:[# open\r\n"
        "1,# separator\r\n2# last\r\n] child:{# nested\r\nvalue:7} }"};
    for (const auto& text : comments) {
        SCOPED_TRACE(text);
        diagnostics.clear();
        EXPECT_FALSE(parse_qtf(text, *schema, *record, diagnostics).has_value());
        diagnostics.clear();
        const auto bytes =
            import_qtf(text, *schema, *record, diagnostics, {.allow_line_comments = true});
        ASSERT_TRUE(bytes.has_value());
        EXPECT_EQ(*bytes, *expected);
        const auto view = validate_brf_record(*schema, *record, *bytes);
        ASSERT_TRUE(view.has_value());
        const auto exported = export_qtf(*view);
        ASSERT_TRUE(exported.text.has_value());
        EXPECT_NE(exported.text->find("name: \"#inside\""), std::string::npos);
        EXPECT_EQ(exported.text->find("# beginning"), std::string::npos);
        diagnostics.clear();
        EXPECT_EQ(import_qtf(*exported.text, *schema, *record, diagnostics), expected);
    }
    // Comments consume input bytes but no value tokens; the existing resource bounds remain.
    diagnostics.clear();
    EXPECT_FALSE(parse_qtf("# over limit\n{}", *schema, *record, diagnostics,
                           {.max_input_bytes = 4U, .allow_line_comments = true})
                     .has_value());
    diagnostics.clear();
    EXPECT_TRUE(parse_qtf("# before\n{value:42}# after", *schema, *record, diagnostics,
                          {.max_tokens = 6U, .allow_line_comments = true})
                    .has_value());
    diagnostics.clear();
    EXPECT_FALSE(parse_qtf("# before\n{value:42}", *schema, *record, diagnostics,
                           {.max_tokens = 4U, .allow_line_comments = true})
                     .has_value());
    const auto location = qtf_source_location("# note\r\n{ missing: 1 }", 10U);
    EXPECT_EQ(location.line, 2U);
    EXPECT_EQ(location.column, 3U);
}

TEST(QtfParserTest, CommentsDoNotMakeMalformedInputValid) {
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
    for (const auto text :
         {"# only a comment", "{ value: 42 # missing closing brace",
          "{ name: \"unterminated# still a string }", "{ samples: [1 # missing comma\n2] }",
          "{ samples: [1,# trailing comma\n] }", "{ value: 4# cannot join tokens\n2 }",
          "{ value: 1 # duplicate\nvalue: 2 }", "{ missing: 1 # unknown\n}", "{} // not a comment",
          "{ value: 1 } \"unterminated# trailing lexical error"}) {
        SCOPED_TRACE(text);
        diagnostics.clear();
        EXPECT_FALSE(import_qtf(text, *schema, *record, diagnostics, {.allow_line_comments = true})
                         .has_value());
        EXPECT_TRUE(diagnostics.has_errors());
    }
    // A quoted # is also ordinary string content in strict v1, with no opt-in needed.
    diagnostics.clear();
    EXPECT_TRUE(parse_qtf("{name:\"# ordinary\"}", *schema, *record, diagnostics).has_value());
}

TEST(QtfParserTest, WhitespaceAndNanUseSelectedQbsTypes) {
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
    const auto bytes =
        import_qtf("\t\r\n{ f32:\tnan\r\nf64: nan }\r\n", *schema, *record, diagnostics);
    ASSERT_TRUE(bytes.has_value());
    const auto view = validate_brf_record(*schema, *record, *bytes);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(std::isnan(*view->field(2U)->as_float32()));
    EXPECT_TRUE(std::isnan(*view->field(3U)->as_float64()));
}

TEST(QtfParserTest, EnumMembershipUsesOneOwnedValueRange) {
    DiagnosticCollection diagnostics;
    auto ir = schema_ir();
    auto* mode = ir.mutable_root_namespace()->add_enums();
    mode->set_ir_id(10U);
    mode->set_name("Mode");
    mode->set_fqn("Mode");
    for (const auto value : {0U, 1U}) {
        auto* item = mode->add_values();
        item->set_name(value == 0U ? "zero" : "one");
        item->set_value(value);
    }
    auto* field = ir.mutable_root_namespace()->mutable_records(1)->add_fields();
    field->set_name("mode");
    field->set_field_index(6U);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(10U);
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
    EXPECT_TRUE(parse_qtf("{mode:0}", *schema, *record, diagnostics).has_value());
    EXPECT_TRUE(parse_qtf("{mode:1}", *schema, *record, diagnostics).has_value());
    diagnostics.clear();
    EXPECT_FALSE(parse_qtf("{mode:2}", *schema, *record, diagnostics).has_value());
}
} // namespace
