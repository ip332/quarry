#include "compiler/layout/layout.hpp"
#include "compiler/qbs/parser.hpp"
#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"
#include "quarry/runtime/qbs_brf_encoder.hpp"
#include "quarry/runtime/qtf_exporter.hpp"
#include "quarry/runtime_c/generic_brf.h"

#include <gtest/gtest.h>

namespace {
using namespace quarry::runtime;
using namespace quarry::compiler::qbs;
using quarry::compiler::diagnostics::DiagnosticCollection;

BrfRecordArrayValue records(std::initializer_list<BrfNestedRecordValue> values) {
    return std::make_shared<const std::vector<BrfNestedRecordValue>>(values);
}
BrfNestedRecordValue leaf(std::uint64_t value) {
    return std::make_shared<const BrfRecordInput>(
        BrfRecordInput{3U, "Leaf", {BrfEncodeValue{value}}});
}
BrfNestedRecordValue branch(std::uint64_t value, BrfRecordArrayValue first,
                            BrfRecordArrayValue second) {
    return std::make_shared<const BrfRecordInput>(
        BrfRecordInput{2U,
                       "Branch",
                       {BrfEncodeValue{value}, BrfEncodeValue{first}, BrfEncodeValue{second},
                        BrfEncodeValue{records({})}}});
}

class NestedArrayReaderTest : public testing::Test {
protected:
    std::vector<std::uint8_t> qbs, bytes;
    std::optional<ValidatedQbsView> schema;
    std::optional<QbsRecordView> root;
    void SetUp() override {
        quarry::schema_ir::SchemaIR ir;
        ir.set_schema_ir_version(1U);
        ir.mutable_root_namespace()->set_ir_id(1U);
        auto add_record = [&](unsigned id, const char* name) {
            auto* r = ir.mutable_root_namespace()->add_records();
            r->set_ir_id(id);
            r->set_record_id(id);
            r->set_name(name);
            r->set_fqn(name);
            return r;
        };
        auto* l = add_record(3U, "Leaf");
        auto* v = l->add_fields();
        v->set_name("value");
        v->set_field_index(0U);
        v->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
        auto array = [](auto* r, unsigned index, const char* name, unsigned target) {
            auto* f = r->add_fields();
            f->set_name(name);
            f->set_field_index(index);
            auto* a = f->mutable_type()->mutable_array();
            a->set_max_elements(5U);
            a->mutable_element_type()->mutable_record()->set_target_record_ir_id(target);
        };
        auto* b = add_record(2U, "Branch");
        auto* marker = b->add_fields();
        marker->set_name("marker");
        marker->set_field_index(0U);
        marker->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
        array(b, 1U, "first", 3U);
        array(b, 2U, "second", 3U);
        array(b, 3U, "empty", 3U);
        auto* r = add_record(1U, "Root");
        array(r, 0U, "groups", 2U);
        array(r, 1U, "after", 3U);
        array(r, 2U, "empty", 2U);
        array(r, 3U, "ending", 3U);
        DiagnosticCollection diagnostics;
        const auto layout = quarry::compiler::layout::LayoutComputer{}.compute(ir, diagnostics);
        const auto model =
            QbsModelBuilder{}.build(ir, layout, {.mode = BuildMode::Reflective}, diagnostics);
        ASSERT_TRUE(model);
        const auto image = serialize_qbs(*model, diagnostics);
        ASSERT_TRUE(image);
        qbs = image->bytes;
        schema = parse_qbs(qbs, diagnostics);
        ASSERT_TRUE(schema);
        root = schema->find_record_by_id(1U);
        ASSERT_TRUE(root);
        const std::vector<std::optional<BrfEncodeValue>> fields{
            BrfEncodeValue{
                records({branch(10U, records({leaf(100U), leaf(101U)}), records({leaf(102U)})),
                         branch(20U, records({}), records({leaf(200U), leaf(201U), leaf(202U)}))})},
            BrfEncodeValue{records({leaf(300U)})}, BrfEncodeValue{records({})},
            BrfEncodeValue{records({leaf(400U), leaf(401U)})}};
        auto encoded = encode_brf_record(*schema, *root, fields);
        ASSERT_TRUE(encoded);
        bytes = *encoded;
    }
    void check_leaves(const BrfRecordArrayView& array, const std::vector<std::uint64_t>& expected) {
        ASSERT_EQ(array.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const auto child = array.element(i);
            ASSERT_TRUE(child);
            EXPECT_EQ(child->schema().identity, "Leaf");
            EXPECT_EQ(child->field(0U)->as_unsigned(), expected[i]);
        }
        EXPECT_FALSE(array.element(array.size()));
    }
};

TEST_F(NestedArrayReaderTest, IndexedAndStructuredViewsKeepNestedAndSiblingOrders) {
    const auto view = validate_brf_record(*schema, *root, bytes);
    ASSERT_TRUE(view);
    const auto groups = view->record_array(0U);
    ASSERT_TRUE(groups);
    ASSERT_EQ(groups->size(), 2U);
    for (std::size_t i = 0; i < 2U; ++i) {
        const auto b = groups->element(i);
        ASSERT_TRUE(b);
        EXPECT_EQ(b->schema().identity, "Branch");
        EXPECT_EQ(b->field(0U)->as_unsigned(), i == 0 ? 10U : 20U);
        if (i == 0U) {
            check_leaves(*b->record_array(1U), {100U, 101U});
            check_leaves(*b->record_array(2U), {102U});
        } else {
            EXPECT_FALSE(b->field(1U));
            check_leaves(*b->record_array(2U), {200U, 201U, 202U});
        }
        EXPECT_FALSE(b->field(3U)); // Empty input arrays normalize to absence.
    }
    EXPECT_FALSE(groups->element(2U));
    check_leaves(*view->record_array(1U), {300U});
    EXPECT_FALSE(view->field(2U));
    check_leaves(*view->record_array(3U), {400U, 401U});
    // Exercise FieldValueView/BrfArrayValueView as well as the direct record-array API.
    const auto structured = view->field(0U)->as_array();
    ASSERT_TRUE(structured);
    const auto second = structured->element(1U)->as_record();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->schema().identity, "Branch");
    EXPECT_EQ(second->field(0U)->as_unsigned(), 20U);
    check_leaves(*groups->element(0U)->record_array(1U), {100U, 101U});
}

TEST_F(NestedArrayReaderTest, TraversesEveryIdentityAndValueAndCanRestart) {
    const auto view = validate_brf_record(*schema, *root, bytes);
    ASSERT_TRUE(view);
    const std::vector<std::pair<std::string, std::uint64_t>> expected{
        {"Branch", 10}, {"Leaf", 100}, {"Leaf", 101}, {"Leaf", 102}, {"Branch", 20}, {"Leaf", 200},
        {"Leaf", 201},  {"Leaf", 202}, {"Leaf", 300}, {"Leaf", 400}, {"Leaf", 401}};
    for (unsigned repeat = 0; repeat < 2U; ++repeat) {
        std::vector<std::pair<std::string, std::uint64_t>> observed;
        EXPECT_EQ(traverse_brf(*view,
                               [&](const BrfTraversalEvent& event) {
                                   if (event.kind == BrfTraversalEventKind::record_begin &&
                                       event.record->schema().record_id != 1U)
                                       observed.emplace_back(
                                           event.record->schema().identity,
                                           *event.record->field(0U)->as_unsigned());
                                   return BrfTraversalControl::Continue;
                               }),
                  BrfTraversalResult::completed);
        EXPECT_EQ(observed, expected);
    }
    const auto visit = [](const BrfTraversalEvent&) { return BrfTraversalControl::Continue; };
    EXPECT_EQ(traverse_brf(*view, visit, {.max_work_items = 1U}), BrfTraversalResult::work_limit);
    EXPECT_EQ(traverse_brf(*view, visit, {.max_depth = 1U}), BrfTraversalResult::depth_limit);
    EXPECT_EQ(traverse_brf(*view, [](const auto&) { return BrfTraversalControl::Stop; }),
              BrfTraversalResult::stopped);
    check_leaves(*view->record_array(3U), {400U, 401U});
    EXPECT_TRUE(export_qtf(*view).text);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4U; ++i)
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (24U - 8U * i));
}
TEST_F(NestedArrayReaderTest, ReadsLegacyPresentEmptyRecordArray) {
    const std::vector<std::optional<BrfEncodeValue>> absent(4U);
    auto encoded = encode_brf_record(*schema, *root, absent);
    ASSERT_TRUE(encoded);
    auto empty = *encoded;
    const auto field = schema->field(root->field_start + 2U);
    ASSERT_EQ(field.storage, 2U);
    ASSERT_EQ(field.slot_size, 8U);
    empty.at(16U + field.presence_bit_index / 8U) |= 1U << (field.presence_bit_index % 8U);
    put_u32(empty, field.byte_offset, static_cast<std::uint32_t>(empty.size()));
    put_u32(empty, field.byte_offset + 4U, 1U);
    empty.push_back(0U);
    put_u32(empty, 12U, static_cast<std::uint32_t>(empty.size()));
    const auto view = validate_brf_record(*schema, *root, empty);
    ASSERT_TRUE(view);
    const auto array = view->record_array(2U);
    ASSERT_TRUE(array);
    EXPECT_EQ(array->size(), 0U);
    EXPECT_FALSE(array->element(0U));
    EXPECT_TRUE(export_qtf(*view).text);
    empty.back() = 1U; // Declared child is missing.
    EXPECT_FALSE(validate_brf_record(*schema, *root, empty));
}

TEST_F(NestedArrayReaderTest, MalformedChildrenAndValidationLimitsStillReject) {
    const auto view = validate_brf_record(*schema, *root, bytes);
    ASSERT_TRUE(view);
    const auto child = view->record_array(0U)->element(0U)->record_array(1U)->element(0U);
    ASSERT_TRUE(child);
    const auto offset = static_cast<std::size_t>(child->bytes().data() - bytes.data());
    auto bad = bytes;
    put_u32(bad, offset + 4U, 2U); // Branch ID where Leaf is required.
    EXPECT_FALSE(validate_brf_record(*schema, *root, bad));
    bad = bytes;
    bad.pop_back();
    EXPECT_FALSE(validate_brf_record(*schema, *root, bad));
    GenericBrfError error = GenericBrfError::none;
    EXPECT_FALSE(validate_brf_record(*schema, *root, bytes, {.max_work_items = 0U}, &error));
    EXPECT_EQ(error, GenericBrfError::resource_limit_exceeded);
    EXPECT_FALSE(validate_brf_record(*schema, *root, bytes, {.max_nested_records = 1U}, &error));
    EXPECT_EQ(error, GenericBrfError::resource_limit_exceeded);
    EXPECT_FALSE(
        validate_brf_record(*schema, *root, bytes, {.max_array_elements_traversed = 1U}, &error));
    EXPECT_EQ(error, GenericBrfError::bounds_exceeded);
    EXPECT_TRUE(validate_brf_record(*schema, *root, bytes));
}

TEST_F(NestedArrayReaderTest, CReaderUsesStablePreassignedElementSlots) {
    quarry_qbs_record_view_t rs[16]{};
    quarry_qbs_field_view_t fs[64]{};
    quarry_qbs_type_view_t ts[64]{};
    quarry_qbs_enum_view_t es[1]{};
    std::uint64_t ev[1]{};
    quarry_brf_record_node_t ns[64]{};
    quarry_brf_field_state_t states[256]{};
    std::uint32_t maps[256]{}, elements[128]{};
    quarry_brf_child_relation_t cs[64]{};
    quarry_brf_record_array_relation_t as[64]{};
    quarry_brf_validation_frame_t frames[64]{};
    quarry_workspace_t w{rs,     16U,    fs,   64U,  ts,   64U, es,  1U, ev,  1U,       ns,
                         64U,    states, 256U, maps, 256U, cs,  64U, as, 64U, elements, 128U,
                         frames, 64U,    0U,   0U,   0U,   0U,  0U,  0U, 0U};
    quarry_qbs_view_t q{};
    ASSERT_EQ(quarry_qbs_parse(qbs.data(), qbs.size(), &q, &w, nullptr), QUARRY_GENERIC_OK);
    const quarry_qbs_record_view_t* r = nullptr;
    ASSERT_EQ(quarry_qbs_find_record_by_id(&q, 1U, &r), QUARRY_GENERIC_OK);
    quarry_brf_record_view_t v{};
    ASSERT_EQ(
        quarry_brf_validate_with_workspace(&q, r, bytes.data(), bytes.size(), &v, &w, nullptr),
        QUARRY_GENERIC_OK);
    quarry_brf_array_view_t groups{};
    ASSERT_EQ(quarry_brf_get_record_array(&v, 0U, &groups), QUARRY_GENERIC_OK);
    for (std::size_t i = 0; i < 2U; ++i) {
        quarry_brf_record_view_t b{};
        ASSERT_EQ(quarry_brf_record_array_get(&v, &groups, i, &b), QUARRY_GENERIC_OK);
        EXPECT_EQ(b.schema->record_id, 2U);
        std::uint64_t value = 0;
        ASSERT_EQ(quarry_brf_get_uint(&b, 0U, &value), QUARRY_GENERIC_OK);
        EXPECT_EQ(value, i == 0U ? 10U : 20U);
        quarry_brf_array_view_t leaves{};
        ASSERT_EQ(quarry_brf_get_record_array(&b, 2U, &leaves), QUARRY_GENERIC_OK);
        const std::vector<std::uint64_t> expected =
            i == 0U ? std::vector<std::uint64_t>{102U}
                    : std::vector<std::uint64_t>{200U, 201U, 202U};
        for (std::size_t j = 0; j < expected.size(); ++j) {
            quarry_brf_record_view_t l{};
            ASSERT_EQ(quarry_brf_record_array_get(&b, &leaves, j, &l), QUARRY_GENERIC_OK);
            EXPECT_EQ(l.schema->record_id, 3U);
            ASSERT_EQ(quarry_brf_get_uint(&l, 0U, &value), QUARRY_GENERIC_OK);
            EXPECT_EQ(value, expected[j]);
        }
    }
}
} // namespace
