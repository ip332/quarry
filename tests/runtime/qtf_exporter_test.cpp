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
    auto add_record = [&](std::uint64_t ir_id, std::uint32_t record_id, const char* name) {
        auto* record = schema.mutable_root_namespace()->add_records();
        record->set_ir_id(ir_id);
        record->set_record_id(record_id);
        record->set_name(name);
        record->set_fqn(name);
        return record;
    };
    auto* detail = add_record(2U, 2U, "Detail");
    auto* value = detail->add_fields();
    value->set_name("value");
    value->set_field_index(0U);
    value->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
    auto* label = detail->add_fields();
    label->set_name("label");
    label->set_field_index(1U);
    label->mutable_type()->mutable_string()->set_max_bytes(16U);
    auto* item = add_record(3U, 3U, "Item");
    auto* id = item->add_fields();
    id->set_name("id");
    id->set_field_index(0U);
    id->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
    auto* item_detail = item->add_fields();
    item_detail->set_name("detail");
    item_detail->set_field_index(1U);
    item_detail->mutable_type()->mutable_record()->set_target_record_ir_id(2U);
    auto* parent = add_record(1U, 1U, "Parent");
    auto* timestamp = parent->add_fields();
    timestamp->set_name("timestamp");
    timestamp->set_field_index(0U);
    timestamp->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U64);
    auto* name = parent->add_fields();
    name->set_name("name");
    name->mutable_type()->mutable_string()->set_max_bytes(16U);
    name->set_field_index(1U);
    auto* child = parent->add_fields();
    child->set_name("child");
    child->set_field_index(2U);
    child->mutable_type()->mutable_record()->set_target_record_ir_id(2U);
    auto* items = parent->add_fields();
    items->set_name("items");
    items->set_field_index(3U);
    items->mutable_type()->mutable_array()->set_max_elements(3U);
    items->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(3U);
    auto* absent = parent->add_fields();
    absent->set_name("absent");
    absent->set_field_index(4U);
    absent->mutable_type()->mutable_string()->set_max_bytes(16U);
    auto* empty = parent->add_fields();
    empty->set_name("empty");
    empty->set_field_index(5U);
    empty->mutable_type()->mutable_string()->set_max_bytes(16U);
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
    child->identity = "Detail";
    child->fields = {BrfEncodeValue{std::uint64_t{7U}}, BrfEncodeValue{std::string("child")}};
    auto item = std::make_shared<BrfRecordInput>();
    item->record_id = 3U;
    item->identity = "Item";
    item->fields = {BrfEncodeValue{std::uint64_t{8U}}, BrfEncodeValue{child}};
    auto item2 = std::make_shared<BrfRecordInput>(*item);
    item2->fields[0] = BrfEncodeValue{std::uint64_t{9U}};
    auto items = std::make_shared<std::vector<BrfNestedRecordValue>>();
    items->push_back(item);
    items->push_back(item2);
    const std::vector<std::optional<BrfEncodeValue>> fields{
        BrfEncodeValue{std::uint64_t{18446744073709551615ULL}},
        BrfEncodeValue{std::string("sensor")},
        BrfEncodeValue{child},
        BrfEncodeValue{items},
        std::nullopt,
        BrfEncodeValue{std::string("")}};
    const auto bytes = encode_brf_record(*schema, *parent, fields);
    ASSERT_TRUE(bytes.has_value());
    GenericBrfError read_error = GenericBrfError::none;
    const auto view = validate_brf_record(*schema, *parent, *bytes, {}, &read_error);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(read_error, GenericBrfError::none);
    ASSERT_TRUE(view->nested_record(2U).has_value());
    ASSERT_TRUE(view->record_array(3U).has_value());
    ASSERT_TRUE(view->record_array(3U)->element(0U)->nested_record(1U).has_value());
    const auto items_value = view->field(3U);
    ASSERT_TRUE(items_value.has_value());
    EXPECT_EQ(items_value->type().code, 16U);
    EXPECT_EQ(schema->type(items_value->type().reference).code, 15U);
    ASSERT_TRUE(view->record_array(3U).has_value());
    const auto items_view = items_value->as_array();
    ASSERT_TRUE(items_view.has_value());
    ASSERT_EQ(items_view->size(), 2U);
    ASSERT_TRUE(items_view->element(0U).has_value());
    ASSERT_TRUE(items_view->element(0U)->as_record().has_value());
    const auto exported = export_qtf(*view);
    ASSERT_TRUE(exported.text.has_value()) << static_cast<int>(exported.error);
    EXPECT_EQ(*exported.text,
              "{\n  timestamp: 18446744073709551615\n  name: \"sensor\"\n  child: {\n    value: "
              "7\n    label: \"child\"\n  }\n  items: [\n  {\n    id: 8\n    detail: {\n      "
              "value: 7\n      label: \"child\"\n    }\n  },\n  {\n    id: 9\n    detail: {\n      "
              "value: 7\n      label: \"child\"\n    }\n  }\n  ]\n  empty: \"\"\n}\n");
}
} // namespace
