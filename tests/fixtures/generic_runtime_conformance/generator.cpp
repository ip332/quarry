#include "compiler/layout/layout.hpp"
#include "compiler/qbs/parser.hpp"
#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"
#include "quarry/runtime/qbs_brf_encoder.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace quarry::compiler::qbs;
using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::schema_ir::SchemaIR;
using quarry::runtime::BrfEncodeValue;
using quarry::runtime::BrfEncodeArray;
using quarry::runtime::BrfNestedRecordValue;
using quarry::runtime::BrfRecordArrayValue;
using quarry::runtime::BrfRecordInput;
using quarry::runtime::BrfUnsignedArray;
using quarry::runtime::GenericBrfEncodeError;
using quarry::runtime::GenericBrfError;

SchemaIR fixture_schema() {
    SchemaIR schema;
    schema.set_schema_ir_version(1U);
    schema.mutable_root_namespace()->set_ir_id(1U);
    auto* state = schema.mutable_root_namespace()->add_enums();
    state->set_ir_id(2U); state->set_name("State"); state->set_fqn("State");
    for (const auto& [name, value] : {std::pair{"idle", 0}, std::pair{"ready", 1}}) {
        auto* item = state->add_values(); item->set_name(name); item->set_value(value);
    }
    auto add_record = [&](std::uint64_t ir, std::uint32_t id, const char* name) {
        auto* record = schema.mutable_root_namespace()->add_records();
        record->set_ir_id(ir); record->set_record_id(id); record->set_name(name); record->set_fqn(name);
        return record;
    };
    auto* child = add_record(2U, 2U, "Child");
    auto* value = child->add_fields(); value->set_name("value"); value->set_field_index(0U);
    value->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
    auto* label = child->add_fields(); label->set_name("label"); label->set_field_index(1U);
    label->mutable_type()->mutable_string()->set_max_bytes(32U);
    auto* item = add_record(3U, 3U, "Item");
    auto* item_value = item->add_fields(); item_value->set_name("value"); item_value->set_field_index(0U);
    item_value->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_I32);
    auto* item_child = item->add_fields(); item_child->set_name("child"); item_child->set_field_index(1U);
    item_child->mutable_type()->mutable_record()->set_target_record_ir_id(2U);
    auto* parent = add_record(1U, 1U, "Parent");
    auto add = [&](const char* name, std::uint32_t index) { auto* f = parent->add_fields(); f->set_name(name); f->set_field_index(index); return f; };
    add("sequence", 0U)->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
    add("delta", 1U)->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_I32);
    add("enabled", 2U)->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_BOOL);
    add("temperature", 3U)->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_F32);
    add("ratio", 4U)->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_F64);
    add("state", 5U)->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(2U);
    add("name", 6U)->mutable_type()->mutable_string()->set_max_bytes(32U);
    add("payload", 7U)->mutable_type()->mutable_bytes()->set_max_bytes(32U);
    auto* samples = add("samples", 8U); samples->mutable_type()->mutable_array()->set_max_elements(8U);
    samples->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U16);
    auto* child_field = add("child", 9U); child_field->mutable_type()->mutable_record()->set_target_record_ir_id(2U);
    auto* items = add("items", 10U); items->mutable_type()->mutable_array()->set_max_elements(4U);
    items->mutable_type()->mutable_array()->mutable_element_type()->mutable_record()->set_target_record_ir_id(3U);
    add("optional", 11U)->mutable_type()->mutable_string()->set_max_bytes(16U);
    auto* empty = add("empty_samples", 12U); empty->mutable_type()->mutable_array()->set_max_elements(8U);
    empty->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U16);
    return schema;
}

BrfNestedRecordValue child(std::uint32_t value, std::string label) {
    auto result = std::make_shared<BrfRecordInput>();
    result->record_id = 2U; result->identity = "Child";
    result->fields = {BrfEncodeValue{std::uint64_t{value}}, BrfEncodeValue{std::move(label)}};
    return result;
}

std::vector<std::uint8_t> generate(std::vector<std::uint8_t>& qbs) {
    auto source = fixture_schema(); DiagnosticCollection diagnostics;
    quarry::compiler::layout::LayoutComputer computer;
    const auto layout = computer.compute(source, diagnostics);
    if (!diagnostics.empty()) throw std::runtime_error("layout failed");
    auto model = QbsModelBuilder{}.build(source, layout, {.mode = BuildMode::Reflective}, diagnostics);
    if (!model) throw std::runtime_error("QBS model failed");
    auto image = serialize_qbs(*model, diagnostics); if (!image) throw std::runtime_error("QBS serialization failed");
    qbs = image->bytes;
    auto parsed = parse_qbs(qbs, diagnostics); if (!parsed) throw std::runtime_error("QBS parse failed");
    auto parent = parsed->find_record_by_identity("Parent"); if (!parent) throw std::runtime_error("Parent missing");
    std::size_t parent_index = 0U;
    while (parent_index < parsed->record_count() && parsed->record(parent_index).record_id != parent->record_id) ++parent_index;
    auto first = std::make_shared<BrfRecordInput>(); first->record_id = 3U; first->identity = "Item";
    first->fields = {BrfEncodeValue{std::int64_t{-4}}, child(101U, "item-child")};
    auto second = std::make_shared<BrfRecordInput>(); second->record_id = 3U; second->identity = "Item";
    second->fields = {BrfEncodeValue{std::int64_t{8}}, child(202U, "second-child")};
    auto items = std::make_shared<std::vector<BrfNestedRecordValue>>(); items->push_back(first); items->push_back(second);
    std::vector<std::optional<BrfEncodeValue>> fields{
        BrfEncodeValue{std::uint64_t{42}}, BrfEncodeValue{std::int64_t{-17}}, BrfEncodeValue{true},
        BrfEncodeValue{12.5F}, BrfEncodeValue{-3.25}, BrfEncodeValue{std::uint64_t{1}},
        BrfEncodeValue{std::string{"quarry"}}, BrfEncodeValue{std::vector<std::uint8_t>{0x01, 0x02, 0xFF}},
        BrfEncodeValue{BrfEncodeArray{BrfUnsignedArray{1, 2, 3}}}, BrfEncodeValue{child(100U, "child")},
        BrfEncodeValue{items}, std::nullopt, BrfEncodeValue{BrfEncodeArray{BrfUnsignedArray{}}}};
    GenericBrfEncodeError error = GenericBrfEncodeError::none;
    auto brf = quarry::runtime::encode_brf_record(*parsed, *parent, fields, &error);
    if (!brf) throw std::runtime_error("BRF encoding failed");
    GenericBrfError read_error = GenericBrfError::none;
    auto view = quarry::runtime::validate_brf_record(*parsed, *parent, *brf, {}, &read_error);
    auto optional_field = parsed->find_field(parent_index, 11U);
    auto empty_field = parsed->find_field(parent_index, 12U);
    if (!view || !view->field(0U) || view->field(0U)->as_unsigned() != 42U ||
        !optional_field || view->is_present(*optional_field) || !empty_field || !view->is_present(*empty_field) ||
        !view->array(12U) || view->array(12U)->size() != 0U)
        throw std::runtime_error("BRF self-check failed");
    return *brf;
}

void write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool equal_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> actual((std::istreambuf_iterator<char>(input)), {});
    return actual == bytes;
}
}

int main(int argc, char** argv) {
    if (argc != 3 || (std::string_view(argv[1]) != "--write" && std::string_view(argv[1]) != "--check")) {
        std::cerr << "usage: generic_runtime_conformance_fixture_generator (--write|--check) DIRECTORY\n"; return 2;
    }
    try {
        std::filesystem::create_directories(argv[2]); std::vector<std::uint8_t> qbs; auto brf = generate(qbs);
        const auto directory = std::filesystem::path(argv[2]);
        if (std::string_view(argv[1]) == "--write") {
            write(directory / "schema.qbs", qbs); write(directory / "record.brf", brf);
        } else if (!equal_file(directory / "schema.qbs", qbs) || !equal_file(directory / "record.brf", brf)) {
            std::cerr << "generic runtime conformance fixture differs from regenerated bytes\n"; return 1;
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
