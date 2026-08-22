#include "quarry/runtime/detail/brf_validation_cache.hpp"

#include <gtest/gtest.h>

#include <numeric>
#include <vector>

namespace {

using namespace quarry::runtime;
using namespace quarry::runtime::detail;

TEST(BrfValidationCacheTest, ValidatesDeepGraphWithExplicitStack) {
    constexpr std::size_t depth = 2048U;
    std::vector<std::size_t> parents(depth - 1U);
    std::iota(parents.begin(), parents.end(), 0U);
    BrfReadLimits limits;
    limits.max_nested_records = depth + 10U;
    limits.max_work_items = depth * 8U;
    ValidationCache cache(limits);
    ASSERT_TRUE(validate_synthetic_work_graph(parents, limits, cache))
        << "nodes=" << cache.nodes().size() << " work=" << cache.work_items();
    EXPECT_GE(cache.nodes().size(), depth);
    EXPECT_EQ(cache.validations(), cache.nodes().size());
}

TEST(BrfValidationCacheTest, RejectsWorkAndDepthLimits) {
    std::vector<std::size_t> parents(64U);
    std::iota(parents.begin(), parents.end(), 0U);
    BrfReadLimits work_limit;
    work_limit.max_nested_records = parents.size();
    work_limit.max_work_items = 10U;
    ValidationCache work_cache(work_limit);
    EXPECT_FALSE(validate_synthetic_work_graph(parents, work_limit, work_cache));

    BrfReadLimits depth_limit;
    depth_limit.max_nested_records = 8U;
    depth_limit.max_work_items = 1024U;
    ValidationCache depth_cache(depth_limit);
    EXPECT_FALSE(validate_synthetic_work_graph(parents, depth_limit, depth_cache));
}

TEST(BrfValidationCacheTest, RelationsUseStableIndexesAcrossGrowth) {
    BrfReadLimits limits;
    limits.max_nested_records = 128U;
    limits.max_work_items = 4096U;
    ValidationCache cache(limits);
    const auto root = cache.add_node(0U, 10U, 20U);
    ASSERT_TRUE(root.has_value());
    std::vector<std::size_t> children;
    for (std::size_t i = 0U; i < 32U; ++i) {
        const auto child = cache.add_child(*root, i, i + 1U, 100U + i, 4U);
        ASSERT_TRUE(child.has_value());
        children.push_back(*child);
    }
    const auto array = cache.add_array(*root, 99U, children);
    ASSERT_TRUE(array.has_value());
    EXPECT_EQ(cache.children().front().child_node, 1U);
    EXPECT_EQ(cache.arrays()[*array].count, children.size());
    EXPECT_EQ(cache.array_children()[cache.arrays()[*array].child_begin], 1U);
    EXPECT_TRUE(cache.reuse_node(children.back()));
    EXPECT_EQ(cache.nodes().size(), 33U);
}

TEST(BrfValidationCacheTest, FieldRangesRemainStableAcrossGrowth) {
    BrfReadLimits limits;
    limits.max_nested_records = 128U;
    limits.max_work_items = 4096U;
    ValidationCache cache(limits);
    const auto root = cache.add_node(0U, 0U, 10U);
    ASSERT_TRUE(root.has_value());
    ASSERT_TRUE(cache.begin_fields(*root).has_value());
    for (std::size_t i = 0U; i < 32U; ++i)
        ASSERT_TRUE(cache.add_field(*root, {.qbs_field_index = i, .present = i % 2U != 0U}));
    ASSERT_EQ(cache.nodes()[*root].first_validated_field, 0U);
    ASSERT_EQ(cache.nodes()[*root].validated_field_count, 32U);
    EXPECT_EQ(cache.fields()[cache.nodes()[*root].first_validated_field + 31U].qbs_field_index,
              31U);
    EXPECT_TRUE(cache.complete_node(*root));
    EXPECT_TRUE(cache.nodes()[*root].complete);
}

} // namespace
