#pragma once

#include "compiler/diagnostics/diagnostic.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace quarry::compiler::qbs {

struct QbsParserLimits {
    std::uint32_t max_image_size = 64U * 1024U * 1024U;
    std::uint32_t max_sections = 64U;
    std::uint32_t max_records = 65536U;
    std::uint32_t max_fields = 65536U;
    std::uint32_t max_types = 65536U;
    std::uint32_t max_enums = 65536U;
    std::uint32_t max_enum_values = 1U << 20U;
    std::uint32_t max_strings = 65536U;
    std::uint32_t max_work_items = 1U << 20U;
    std::uint32_t max_identity_key_bytes = 16U * 1024U * 1024U;
};

struct QbsHeaderView {
    std::uint8_t qbs_format_version = 0U;
    std::uint8_t brf_format_version = 0U;
    std::uint8_t flags = 0U;
    std::uint8_t identity_offset_width = 0U;
    std::array<std::uint8_t, 16> schema_id{};
    std::uint16_t section_count = 0U;
    std::uint32_t total_size = 0U;
};

struct QbsRecordView {
    std::uint32_t record_id = 0U;
    std::uint32_t field_start = 0U;
    std::uint16_t field_count = 0U;
    bool variable_size = false;
    std::uint32_t presence_bitmap_size = 0U;
    std::uint32_t fixed_region_size = 0U;
    std::uint32_t complete_fixed_record_size = 0U;
    std::string_view identity;
    std::string_view name;
};

struct QbsFieldView {
    std::uint16_t field_index = 0U;
    std::uint16_t type_index = 0U;
    std::uint32_t byte_offset = 0U;
    std::uint16_t bit_offset = 0U;
    std::uint32_t bit_width = 0U;
    std::uint16_t presence_bit_index = 0U;
    std::uint32_t slot_size = 0U;
    std::uint8_t storage = 0U;
    std::uint8_t descriptor_kind = 0U;
    std::string_view name;
};

struct QbsTypeView {
    std::uint8_t code = 0U;
    bool fixed_size = false;
    std::uint16_t encoded_width = 0U;
    std::uint16_t reference = 0U;
    std::uint32_t max_elements = 0U;
    std::uint32_t max_bytes = 0U;
};

struct QbsEnumView {
    std::uint16_t encoded_width = 0U;
    std::string_view identity;
    std::string_view name;
    std::vector<std::uint64_t> values;
};

class ValidatedQbsView {
public:
    QbsHeaderView header() const { return header_; }
    std::size_t record_count() const { return record_count_; }
    std::size_t field_count() const { return field_count_; }
    std::size_t type_count() const { return type_count_; }
    std::size_t enum_count() const { return enum_count_; }
    QbsRecordView record(std::size_t index) const;
    QbsFieldView field(std::size_t index) const;
    QbsTypeView type(std::size_t index) const;
    QbsEnumView enum_type(std::size_t index) const;
    std::string_view identity_at_offset(std::uint32_t offset) const;
    std::string_view string(std::size_t index) const;

private:
    friend std::optional<ValidatedQbsView>
    parse_qbs(std::span<const std::uint8_t>, diagnostics::DiagnosticCollection&, QbsParserLimits);
    std::span<const std::uint8_t> bytes_;
    QbsHeaderView header_;
    std::uint32_t record_offset_ = 0U;
    std::uint32_t field_offset_ = 0U;
    std::uint32_t type_offset_ = 0U;
    std::uint32_t enum_offset_ = 0U;
    std::uint32_t enum_values_offset_ = 0U;
    std::uint32_t iss_offset_ = 0U;
    std::uint32_t iss_size_ = 0U;
    std::uint32_t strings_offset_ = 0U;
    std::uint32_t strings_size_ = 0U;
    std::uint32_t record_stride_ = 0U;
    std::uint32_t enum_stride_ = 0U;
    std::size_t record_count_ = 0U;
    std::size_t field_count_ = 0U;
    std::size_t type_count_ = 0U;
    std::size_t enum_count_ = 0U;
};

[[nodiscard]] std::optional<ValidatedQbsView>
parse_qbs(std::span<const std::uint8_t> bytes, diagnostics::DiagnosticCollection& diagnostics,
          QbsParserLimits limits = {});

} // namespace quarry::compiler::qbs
