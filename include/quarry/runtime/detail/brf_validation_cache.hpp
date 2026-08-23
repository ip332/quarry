#pragma once

#include "quarry/runtime/qbs_brf_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace quarry::runtime::detail {

struct ValidatedRecordNode {
    std::size_t qbs_record_index = 0U;
    std::size_t brf_offset = 0U;
    std::size_t brf_length = 0U;
    std::size_t child_begin = 0U;
    std::size_t child_count = 0U;
    std::size_t array_begin = 0U;
    std::size_t array_count = 0U;
    // These counters describe append activity only.  They are not a logical
    // field lookup range: child validation may interleave entries in the
    // global append-only field-state vector.
    std::size_t validated_fields = 0U;
    std::size_t first_validated_field = 0U;
    std::size_t validated_field_count = 0U;
    bool complete = false;
};

struct ValidatedFieldState {
    std::size_t qbs_field_index = 0U;
    bool present = false;
    std::size_t fixed_offset = 0U;
    std::size_t fixed_length = 0U;
    std::size_t payload_offset = 0U;
    std::size_t payload_length = 0U;
    std::size_t array_count = 0U;
    std::optional<std::size_t> child_relation;
    std::optional<std::size_t> array_relation;
};

struct ChildRelation {
    std::size_t parent_node = 0U;
    std::size_t parent_field = 0U;
    std::size_t child_node = 0U;
    std::size_t brf_offset = 0U;
    std::size_t brf_length = 0U;
};

struct RecordArrayRelation {
    std::size_t parent_node = 0U;
    std::size_t parent_field = 0U;
    std::size_t child_begin = 0U;
    std::size_t count = 0U;
};

struct ValidationWorkFrame {
    std::size_t qbs_record_index = 0U;
    std::size_t brf_offset = 0U;
    std::size_t brf_length = 0U;
    std::size_t node_index = 0U;
    std::size_t field_cursor = 0U;
    bool entered = false;
};

class ValidationCache {
public:
    explicit ValidationCache(BrfReadLimits limits = {}) : limits_(limits) {}

    std::optional<std::size_t> add_node(std::size_t qbs_record_index, std::size_t brf_offset,
                                        std::size_t brf_length);
    std::optional<std::size_t> add_child(std::size_t parent_node, std::size_t parent_field,
                                         std::size_t qbs_record_index, std::size_t brf_offset,
                                         std::size_t brf_length);
    std::optional<std::pair<std::size_t, std::size_t>>
    add_child_relation(std::size_t parent_node, std::size_t parent_field,
                       std::size_t qbs_record_index, std::size_t brf_offset,
                       std::size_t brf_length);
    std::optional<std::size_t> add_array(std::size_t parent_node, std::size_t parent_field,
                                         std::span<const std::size_t> child_nodes);
    std::optional<std::size_t> begin_array(std::size_t parent_node, std::size_t parent_field,
                                           std::size_t count);
    bool add_array_element(std::size_t relation_index, std::size_t child_node);
    bool account_work(std::size_t amount = 1U);
    bool begin_node(std::size_t node_index);
    bool reuse_node(std::size_t node_index);
    std::optional<std::size_t> begin_fields(std::size_t node_index);
    bool add_field(std::size_t node_index, ValidatedFieldState field);
    bool set_child_relation(std::size_t field_index, std::size_t relation_index);
    bool set_array_relation(std::size_t field_index, std::size_t relation_index);
    bool complete_node(std::size_t node_index);

    const std::vector<ValidatedRecordNode>& nodes() const { return nodes_; }
    const std::vector<ValidatedFieldState>& fields() const { return fields_; }
    // Field-state storage is global and append-only.  Per-node logical field
    // lookup is defined by this stable mapping; physical contiguity in
    // fields() is deliberately not required.
    const std::vector<std::size_t>& field_indexes(std::size_t node_index) const {
        return field_indexes_[node_index];
    }
    const std::vector<ChildRelation>& children() const { return children_; }
    const std::vector<RecordArrayRelation>& arrays() const { return arrays_; }
    const std::vector<std::size_t>& array_children() const { return array_children_; }
    std::size_t work_items() const { return work_items_; }
    std::size_t validations() const { return validations_; }

private:
    BrfReadLimits limits_;
    std::vector<ValidatedRecordNode> nodes_;
    std::vector<ValidatedFieldState> fields_;
    std::vector<std::vector<std::size_t>> field_indexes_;
    std::vector<ChildRelation> children_;
    std::vector<RecordArrayRelation> arrays_;
    std::vector<std::size_t> array_children_;
    std::size_t work_items_ = 0U;
    std::size_t validations_ = 0U;
};

[[nodiscard]] bool validate_synthetic_work_graph(std::span<const std::size_t> parent_nodes,
                                                 BrfReadLimits limits, ValidationCache& cache);

} // namespace quarry::runtime::detail
