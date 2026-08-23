#include "quarry/runtime/detail/brf_validation_cache.hpp"

#include <algorithm>
#include <limits>

namespace quarry::runtime::detail {

std::optional<std::size_t> ValidationCache::add_node(std::size_t qbs_record_index,
                                                     std::size_t brf_offset,
                                                     std::size_t brf_length) {
    if (nodes_.size() >= limits_.max_nested_records + 1U || !account_work())
        return std::nullopt;
    nodes_.push_back({qbs_record_index, brf_offset, brf_length});
    field_indexes_.emplace_back();
    return nodes_.size() - 1U;
}

std::optional<std::size_t> ValidationCache::add_child(std::size_t parent_node,
                                                      std::size_t parent_field,
                                                      std::size_t qbs_record_index,
                                                      std::size_t brf_offset,
                                                      std::size_t brf_length) {
    const auto relation =
        add_child_relation(parent_node, parent_field, qbs_record_index, brf_offset, brf_length);
    return relation.has_value() ? std::optional<std::size_t>(relation->first) : std::nullopt;
}

std::optional<std::pair<std::size_t, std::size_t>>
ValidationCache::add_child_relation(std::size_t parent_node, std::size_t parent_field,
                                    std::size_t qbs_record_index, std::size_t brf_offset,
                                    std::size_t brf_length) {
    if (parent_node >= nodes_.size() || !account_work())
        return std::nullopt;
    const auto child = add_node(qbs_record_index, brf_offset, brf_length);
    if (!child.has_value())
        return std::nullopt;
    children_.push_back({parent_node, parent_field, *child, brf_offset, brf_length});
    ++nodes_[parent_node].child_count;
    return std::pair{*child, children_.size() - 1U};
}

std::optional<std::size_t> ValidationCache::add_array(std::size_t parent_node,
                                                      std::size_t parent_field,
                                                      std::span<const std::size_t> child_nodes) {
    if (parent_node >= nodes_.size() || child_nodes.size() > limits_.max_array_elements_traversed ||
        child_nodes.size() > limits_.max_work_items - work_items_)
        return std::nullopt;
    const auto child_begin = array_children_.size();
    arrays_.push_back({parent_node, parent_field, child_begin, child_nodes.size()});
    for (const auto child : child_nodes) {
        if (child >= nodes_.size() || !account_work())
            return std::nullopt;
        array_children_.push_back(child);
    }
    nodes_[parent_node].array_count++;
    return arrays_.size() - 1U;
}

std::optional<std::size_t>
ValidationCache::begin_array(std::size_t parent_node, std::size_t parent_field, std::size_t count) {
    if (parent_node >= nodes_.size() || count > limits_.max_array_elements_traversed ||
        arrays_.size() >= limits_.max_array_elements_traversed || !account_work())
        return std::nullopt;
    arrays_.push_back({parent_node, parent_field, array_children_.size(), 0U});
    ++nodes_[parent_node].array_count;
    return arrays_.size() - 1U;
}

bool ValidationCache::add_array_element(std::size_t relation_index, std::size_t child_node) {
    if (relation_index >= arrays_.size() || child_node >= nodes_.size() || !account_work())
        return false;
    auto& relation = arrays_[relation_index];
    if (relation.count >= limits_.max_array_elements_traversed)
        return false;
    array_children_.push_back(child_node);
    ++relation.count;
    return true;
}

bool ValidationCache::account_work(std::size_t amount) {
    if (amount > limits_.max_work_items - std::min(work_items_, limits_.max_work_items))
        return false;
    work_items_ += amount;
    return true;
}

bool ValidationCache::begin_node(std::size_t node_index) {
    if (node_index >= nodes_.size() || !account_work())
        return false;
    ++validations_;
    return true;
}

bool ValidationCache::reuse_node(std::size_t node_index) {
    return node_index < nodes_.size() && account_work();
}

std::optional<std::size_t> ValidationCache::begin_fields(std::size_t node_index) {
    if (node_index >= nodes_.size())
        return std::nullopt;
    nodes_[node_index].first_validated_field = fields_.size();
    nodes_[node_index].validated_field_count = 0U;
    field_indexes_[node_index].clear();
    return fields_.size();
}

bool ValidationCache::add_field(std::size_t node_index, ValidatedFieldState field) {
    if (node_index >= nodes_.size() || !account_work())
        return false;
    fields_.push_back(field);
    field_indexes_[node_index].push_back(fields_.size() - 1U);
    ++nodes_[node_index].validated_field_count;
    nodes_[node_index].validated_fields = nodes_[node_index].validated_field_count;
    return true;
}

bool ValidationCache::set_child_relation(std::size_t field_index, std::size_t relation_index) {
    if (field_index >= fields_.size())
        return false;
    fields_[field_index].child_relation = relation_index;
    return true;
}

bool ValidationCache::set_array_relation(std::size_t field_index, std::size_t relation_index) {
    if (field_index >= fields_.size() || relation_index >= arrays_.size())
        return false;
    fields_[field_index].array_relation = relation_index;
    return true;
}

bool ValidationCache::complete_node(std::size_t node_index) {
    if (node_index >= nodes_.size() || !account_work())
        return false;
    nodes_[node_index].complete = true;
    return true;
}

bool validate_synthetic_work_graph(std::span<const std::size_t> parent_nodes, BrfReadLimits limits,
                                   ValidationCache& cache) {
    std::vector<ValidationWorkFrame> stack;
    const auto root = cache.add_node(0U, 0U, 0U);
    if (!root.has_value())
        return false;
    stack.push_back({0U, 0U, 0U, *root, 0U, false});
    while (!stack.empty()) {
        auto& frame = stack.back();
        if (!frame.entered) {
            if (!cache.begin_node(frame.node_index))
                return false;
            frame.entered = true;
        }
        if (frame.field_cursor != 0U || frame.node_index >= parent_nodes.size()) {
            stack.pop_back();
            continue;
        }
        const auto child = cache.add_child(frame.node_index, frame.field_cursor++,
                                           frame.node_index + 1U, frame.node_index + 1U, 0U);
        if (!child.has_value())
            return false;
        stack.push_back({frame.node_index + 1U, frame.node_index + 1U, 0U, *child, 0U, false});
    }
    return !cache.nodes().empty() && cache.work_items() <= limits.max_work_items;
}

} // namespace quarry::runtime::detail
