#include "compiler/layout/layout.hpp"
#include "compiler/qbs/parser.hpp"
#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"
#include "quarry/runtime/qbs_brf_encoder.hpp"
#include "quarry/runtime/qtf_exporter.hpp"

#include <gtest/gtest.h>

#include <memory>

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
    auto* value = child->add_fields();
    value->set_name("value");
    value->set_field_index(0U);
    value->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
    auto* parent = schema.mutable_root_namespace()->add_records();
    parent->set_ir_id(1U);
    parent->set_record_id(1U);
    parent->set_name("Parent");
    parent->set_fqn("Parent");
    auto* name = parent->add_fields();
    name->set_name("name");
    name->set_field_index(0U);
    name->mutable_type()->mutable_string()->set_max_bytes(16U);
    auto* samples = parent->add_fields();
    samples->set_name("samples");
    samples->set_field_index(1U);
    samples->mutable_type()->mutable_array()->set_max_elements(4U);
    samples->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        quarry::schema_ir::PRIMITIVE_TYPE_U16);
    auto* items = parent->add_fields();
    items->set_name("items");
    items->set_field_index(2U);
    items->mutable_type()->mutable_array()->set_max_elements(3U);
    items->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(2U);
    return schema;
}

TEST(QtfExporterTest, ReflectiveNestedAndRecordArrayGolden) {
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
    const auto parent = schema->find_record_by_identity("Parent");
    ASSERT_TRUE(parent.has_value());
    auto child = std::make_shared<BrfRecordInput>();
    child->record_id = 2U;
    child->identity = "Child";
    child->fields = {BrfEncodeValue{std::uint64_t{7U}}};
    auto item2 = std::make_shared<BrfRecordInput>(*child);
    item2->fields = {BrfEncodeValue{std::uint64_t{8U}}};
    auto items = std::make_shared<std::vector<BrfNestedRecordValue>>();
    items->push_back(child);
    items->push_back(item2);
    const std::vector<std::optional<BrfEncodeValue>> fields{
        BrfEncodeValue{std::string("sensor")},
        BrfEncodeValue{BrfEncodeArray{BrfUnsignedArray{1U, 2U, 3U}}}, BrfEncodeValue{items}};
    const auto bytes = encode_brf_record(*schema, *parent, fields);
    ASSERT_TRUE(bytes.has_value());
    GenericBrfError read_error = GenericBrfError::none;
    const auto view = validate_brf_record(*schema, *parent, *bytes, {}, &read_error);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(read_error, GenericBrfError::none);
    const auto items_value = view->field(2U);
    ASSERT_TRUE(items_value.has_value());
    EXPECT_EQ(items_value->type().code, 16U);
    EXPECT_EQ(schema->type(items_value->type().reference).code, 15U);
    ASSERT_TRUE(view->record_array(2U).has_value());
    const auto items_view = items_value->as_array();
    ASSERT_TRUE(items_view.has_value());
    ASSERT_EQ(items_view->size(), 2U);
    ASSERT_TRUE(items_view->element(0U).has_value());
    ASSERT_TRUE(items_view->element(0U)->as_record().has_value());
    const auto exported = export_qtf(*view);
    ASSERT_TRUE(exported.text.has_value()) << static_cast<int>(exported.error);
    EXPECT_EQ(*exported.text, "{\n  name: \"sensor\"\n  samples: [1, 2, 3]\n  items: [\n  {\n    "
                              "value: 7\n  },\n  {\n    value: 8\n  }\n  ]\n}\n");
}
} // namespace
