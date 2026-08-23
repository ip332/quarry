#pragma once

#include "compiler/qbs/parser.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace quarry::runtime {

namespace detail {
class ValidationCache;
}

struct RecordValidationState;
enum class RecordValidationStep;
class BrfRecordArrayView;

struct PendingChildValidation {
    std::size_t qbs_record_index = 0U;
    std::size_t brf_offset = 0U;
    std::size_t brf_length = 0U;
    std::size_t field_cache_index = 0U;
    std::size_t parent_field_index = 0U;
    std::optional<std::size_t> child_relation;
    std::optional<std::size_t> array_relation;
};

struct PendingRecordArrayValidation {
    std::size_t field_cache_index = 0U;
    std::size_t array_relation = 0U;
    std::size_t element_type = 0U;
    std::size_t count = 0U;
    std::size_t current = 0U;
    std::size_t cursor = 0U;
    std::size_t end = 0U;
    bool variable_elements = false;
    std::optional<std::size_t> child_relation;
};

enum class GenericBrfError {
    none,
    truncated_header,
    unsupported_version,
    unsupported_flags,
    invalid_header,
    unexpected_record_id,
    invalid_fixed_region,
    invalid_presence,
    invalid_slot,
    invalid_descriptor,
    invalid_variable_range,
    noncanonical_tail,
    invalid_bool,
    invalid_utf8,
    invalid_enum,
    malformed_array,
    unsupported_type,
    bounds_exceeded,
    resource_limit_exceeded,
};

struct BrfReadLimits {
    std::size_t max_record_bytes = 64U * 1024U * 1024U;
    std::size_t max_work_items = 1U << 20U;
    std::size_t max_nested_records = 1024U;
    std::size_t max_array_elements_traversed = 1U << 20U;
};

enum class GenericBrfValueKind {
    boolean,
    signed_integer,
    unsigned_integer,
    float32,
    float64,
    enumeration,
    string,
    bytes,
    array,
    record,
};

using BrfValueKind = GenericBrfValueKind;

class BrfArrayView;
class BrfArrayValueView;
class ValidatedBrfRecordView;

class FieldValueView {
public:
    GenericBrfValueKind kind() const { return kind_; }
    quarry::compiler::qbs::QbsTypeView type() const { return type_; }
    std::span<const std::uint8_t> bytes() const { return bytes_; }
    std::optional<bool> as_bool() const;
    std::optional<std::uint64_t> as_unsigned() const;
    std::optional<std::int64_t> as_signed() const;
    std::optional<std::string_view> as_string() const;
    std::optional<std::span<const std::uint8_t>> as_bytes() const;
    std::optional<float> as_float32() const;
    std::optional<double> as_float64() const;
    std::optional<std::uint64_t> as_enum() const;
    std::optional<BrfArrayValueView> as_array() const;
    std::optional<ValidatedBrfRecordView> as_record() const;

private:
    friend class ValidatedBrfRecordView;
    friend class BrfArrayView;
    friend class BrfArrayValueView;
    FieldValueView(GenericBrfValueKind kind, std::span<const std::uint8_t> bytes,
                   std::uint16_t width)
        : kind_(kind), bytes_(bytes), width_(width) {}
    GenericBrfValueKind kind_;
    std::span<const std::uint8_t> bytes_;
    std::uint16_t width_;
    quarry::compiler::qbs::QbsTypeView type_;
    const ValidatedBrfRecordView* owner_ = nullptr;
    std::uint16_t field_index_ = 0U;
    std::shared_ptr<const ValidatedBrfRecordView> record_value_;
};

using BrfValueView = FieldValueView;
struct BrfFieldValueView {
    quarry::compiler::qbs::QbsFieldView field;
    bool present = false;
    std::optional<BrfValueView> value;
};

class BrfArrayView {
public:
    std::size_t size() const { return count_; }
    quarry::compiler::qbs::QbsTypeView element_type() const { return element_type_; }
    std::optional<FieldValueView> element(std::size_t index) const;

private:
    friend class ValidatedBrfRecordView;
    BrfArrayView(quarry::compiler::qbs::QbsTypeView element_type,
                 std::span<const std::uint8_t> bytes, std::size_t count, std::size_t data_start)
        : element_type_(element_type), bytes_(bytes), count_(count), data_start_(data_start) {}
    quarry::compiler::qbs::QbsTypeView element_type_;
    std::span<const std::uint8_t> bytes_;
    std::size_t count_;
    std::size_t data_start_;
};

class ValidatedBrfRecordView {
public:
    const quarry::compiler::qbs::QbsRecordView& schema() const { return record_; }
    std::span<const std::uint8_t> bytes() const { return bytes_; }
    bool is_present(const quarry::compiler::qbs::QbsFieldView& field) const;
    std::optional<FieldValueView> field(const quarry::compiler::qbs::QbsFieldView& field) const;
    std::optional<FieldValueView> field(std::uint16_t field_index) const;
    std::optional<BrfArrayView> array(const quarry::compiler::qbs::QbsFieldView& field) const;
    std::optional<BrfArrayView> array(std::uint16_t field_index) const;
    std::optional<quarry::compiler::qbs::QbsTypeView>
    array_element_type(std::uint16_t field_index) const;
    std::optional<BrfRecordArrayView>
    record_array(const quarry::compiler::qbs::QbsFieldView& field) const;
    std::optional<BrfRecordArrayView> record_array(std::uint16_t field_index) const;
    std::optional<ValidatedBrfRecordView>
    nested_record(const quarry::compiler::qbs::QbsFieldView& field) const;
    std::optional<ValidatedBrfRecordView> nested_record(std::uint16_t field_index) const;
    std::vector<BrfFieldValueView> fields() const;

private:
    friend class BrfRecordArrayView;
    friend bool validate_next_field(const quarry::compiler::qbs::ValidatedQbsView&,
                                    const quarry::compiler::qbs::QbsRecordView&,
                                    std::span<const std::uint8_t>, RecordValidationState&,
                                    detail::ValidationCache&, ValidatedBrfRecordView&,
                                    std::uint64_t&, BrfReadLimits, GenericBrfError*);
    friend RecordValidationStep advance_record_validation(
        const quarry::compiler::qbs::ValidatedQbsView&, const quarry::compiler::qbs::QbsRecordView&,
        std::span<const std::uint8_t>, RecordValidationState&, detail::ValidationCache&,
        ValidatedBrfRecordView&, std::uint64_t&, BrfReadLimits, GenericBrfError*);
    friend std::optional<ValidatedBrfRecordView>
    validate_record_span(const quarry::compiler::qbs::ValidatedQbsView&,
                         const quarry::compiler::qbs::QbsRecordView&, std::span<const std::uint8_t>,
                         BrfReadLimits, GenericBrfError*);
    friend std::optional<ValidatedBrfRecordView>
    validate_brf_record(const quarry::compiler::qbs::ValidatedQbsView&,
                        const quarry::compiler::qbs::QbsRecordView&, std::span<const std::uint8_t>,
                        BrfReadLimits, GenericBrfError*);
    struct FieldSpan {
        std::uint16_t field_index;
        std::span<const std::uint8_t> bytes;
        bool present;
    };
    const quarry::compiler::qbs::ValidatedQbsView* schema_ = nullptr;
    std::size_t record_index_ = 0U;
    std::size_t node_index_ = 0U;
    quarry::compiler::qbs::QbsRecordView record_;
    std::span<const std::uint8_t> root_bytes_;
    std::span<const std::uint8_t> bytes_;
    std::vector<FieldSpan> fields_;
    std::shared_ptr<const detail::ValidationCache> validation_cache_;
    std::optional<ValidatedBrfRecordView> view_for_node(std::size_t node_index) const;
};

class BrfRecordArrayView {
public:
    std::size_t size() const { return count_; }
    std::optional<ValidatedBrfRecordView> element(std::size_t index) const;

private:
    friend class ValidatedBrfRecordView;
    BrfRecordArrayView(const ValidatedBrfRecordView& owner, std::size_t relation, std::size_t count)
        : owner_(owner), relation_(relation), count_(count) {}
    ValidatedBrfRecordView owner_;
    std::size_t relation_;
    std::size_t count_;
};

class BrfArrayValueView {
public:
    std::size_t size() const {
        return primitive_.has_value() ? primitive_->size() : records_->size();
    }
    quarry::compiler::qbs::QbsTypeView element_type() const {
        return primitive_.has_value() ? primitive_->element_type() : element_type_;
    }
    std::optional<BrfValueView> element(std::size_t index) const;

private:
    friend class ValidatedBrfRecordView;
    friend class FieldValueView;
    BrfArrayValueView(BrfArrayView primitive) : primitive_(std::move(primitive)) {}
    BrfArrayValueView(BrfRecordArrayView records, quarry::compiler::qbs::QbsTypeView element_type)
        : records_(std::move(records)), element_type_(element_type) {}
    std::optional<BrfArrayView> primitive_;
    std::optional<BrfRecordArrayView> records_;
    quarry::compiler::qbs::QbsTypeView element_type_;
};

// Internal single-record validation state. Offsets and the variable tail are
// always relative to the record span, so this routine is reusable for future
// child records without changing the public reader API.
struct RecordValidationState {
    enum class Phase { Header, Presence, Fields, FinalizeTail, Complete, Failed };
    Phase phase = Phase::Header;
    std::size_t qbs_record_index = 0U;
    std::size_t node_index = 0U;
    std::size_t record_offset = 0U;
    std::span<const std::uint8_t> record_bytes;
    std::size_t fixed_region_end = 0U;
    std::size_t variable_region_start = 0U;
    std::size_t variable_tail_cursor = 0U;
    std::size_t field_cursor = 0U;
    std::size_t work_items = 0U;
    std::optional<PendingChildValidation> pending_child;
    std::optional<PendingRecordArrayValidation> pending_array;
};

[[nodiscard]] bool validate_next_field(const quarry::compiler::qbs::ValidatedQbsView& schema,
                                       const quarry::compiler::qbs::QbsRecordView& record_schema,
                                       std::span<const std::uint8_t> bytes,
                                       RecordValidationState& state, detail::ValidationCache& cache,
                                       ValidatedBrfRecordView& result, std::uint64_t& work,
                                       BrfReadLimits limits, GenericBrfError* error);

enum class RecordValidationStep { Continue, NeedChild, Complete, Error };

[[nodiscard]] RecordValidationStep
advance_record_validation(const quarry::compiler::qbs::ValidatedQbsView& schema,
                          const quarry::compiler::qbs::QbsRecordView& record_schema,
                          std::span<const std::uint8_t> bytes, RecordValidationState& state,
                          detail::ValidationCache& cache, ValidatedBrfRecordView& result,
                          std::uint64_t& work, BrfReadLimits limits, GenericBrfError* error);

[[nodiscard]] std::optional<ValidatedBrfRecordView>
validate_record_span(const quarry::compiler::qbs::ValidatedQbsView& schema,
                     const quarry::compiler::qbs::QbsRecordView& record_schema,
                     std::span<const std::uint8_t> bytes, BrfReadLimits limits = {},
                     GenericBrfError* error = nullptr);

[[nodiscard]] std::optional<ValidatedBrfRecordView>
validate_brf_record(const quarry::compiler::qbs::ValidatedQbsView& schema,
                    const quarry::compiler::qbs::QbsRecordView& record_schema,
                    std::span<const std::uint8_t> bytes, BrfReadLimits limits = {},
                    GenericBrfError* error = nullptr);

} // namespace quarry::runtime
