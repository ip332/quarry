#pragma once

#include "compiler/qbs/parser.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace quarry::runtime {

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
    bounds_exceeded,
    resource_limit_exceeded,
};

struct BrfReadLimits {
    std::size_t max_record_bytes = 64U * 1024U * 1024U;
    std::size_t max_work_items = 1U << 20U;
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
    FieldValueView(GenericBrfValueKind kind, std::span<const std::uint8_t> bytes,
                   std::uint16_t width)
        : kind_(kind), bytes_(bytes), width_(width) {}
    GenericBrfValueKind kind_;
    std::span<const std::uint8_t> bytes_;
    std::uint16_t width_;
};

class ValidatedBrfRecordView {
public:
    const quarry::compiler::qbs::QbsRecordView& schema() const { return record_; }
    std::span<const std::uint8_t> bytes() const { return bytes_; }
    bool is_present(const quarry::compiler::qbs::QbsFieldView& field) const;
    std::optional<FieldValueView> field(const quarry::compiler::qbs::QbsFieldView& field) const;
    std::optional<FieldValueView> field(std::uint16_t field_index) const;

private:
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
};

[[nodiscard]] std::optional<ValidatedBrfRecordView>
validate_brf_record(const quarry::compiler::qbs::ValidatedQbsView& schema,
                    const quarry::compiler::qbs::QbsRecordView& record_schema,
                    std::span<const std::uint8_t> bytes, BrfReadLimits limits = {},
                    GenericBrfError* error = nullptr);

} // namespace quarry::runtime
