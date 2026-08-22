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

class FieldValueView {
public:
    GenericBrfValueKind kind() const { return kind_; }
    std::span<const std::uint8_t> bytes() const { return bytes_; }
    std::optional<bool> as_bool() const;
    std::optional<std::uint64_t> as_unsigned() const;
    std::optional<std::int64_t> as_signed() const;
    std::optional<std::string_view> as_string() const;

private:
    friend class ValidatedBrfRecordView;
    friend class BrfArrayView;
    FieldValueView(GenericBrfValueKind kind, std::span<const std::uint8_t> bytes,
                   std::uint16_t width)
        : kind_(kind), bytes_(bytes), width_(width) {}
    GenericBrfValueKind kind_;
    std::span<const std::uint8_t> bytes_;
    std::uint16_t width_;
};

class BrfArrayView {
public:
    std::size_t size() const { return count_; }
    std::optional<FieldValueView> element(std::size_t index) const;

private:
    friend class ValidatedBrfRecordView;
    BrfArrayView(quarry::compiler::qbs::QbsTypeView element_type,
                 std::span<const std::uint8_t> bytes, std::size_t count)
        : element_type_(element_type), bytes_(bytes), count_(count) {}
    quarry::compiler::qbs::QbsTypeView element_type_;
    std::span<const std::uint8_t> bytes_;
    std::size_t count_;
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

private:
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
    quarry::compiler::qbs::QbsRecordView record_;
    std::span<const std::uint8_t> bytes_;
    std::vector<FieldSpan> fields_;
    std::shared_ptr<const void> validation_cache_;
};

// Internal single-record validation state. Offsets and the variable tail are
// always relative to the record span, so this routine is reusable for future
// child records without changing the public reader API.
struct RecordValidationState {
    std::size_t qbs_record_index = 0U;
    std::span<const std::uint8_t> record_bytes;
    std::size_t fixed_region_end = 0U;
    std::size_t variable_region_start = 0U;
    std::size_t variable_tail_cursor = 0U;
    std::size_t field_cursor = 0U;
    std::size_t work_items = 0U;
};

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
