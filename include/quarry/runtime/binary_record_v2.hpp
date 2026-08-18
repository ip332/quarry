#pragma once

#include "quarry/runtime/binary_record.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace quarry::runtime {

inline constexpr std::uint8_t kBinaryRecordV2FormatVersion = 2U;
inline constexpr std::uint16_t kBinaryRecordV2HeaderSize = 16U;
inline constexpr std::uint16_t kBinaryRecordV2VariableDescriptorSize = 8U;

enum class BrfV2TypeKind {
    Bool,
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64,
    F32,
    F64,
    Enum,
    String,
    Bytes,
    Record,
    Array,
};

enum class BrfV2FieldStorage {
    Fixed,
    InlineFixedNestedRecord,
    VariableDescriptor,
};

struct BrfV2TypeLayout {
    BrfV2TypeKind kind = BrfV2TypeKind::Bool;
    std::uint32_t encoded_width = 0U;
    std::uint32_t max_bytes = 0U;
    std::uint32_t max_elements = 0U;
    std::uint32_t nested_record_id = 0U;
    std::vector<std::uint64_t> enum_values;
    std::shared_ptr<const BrfV2TypeLayout> element_type;
};

struct BrfV2FieldLayout {
    std::uint32_t field_index = 0U;
    std::uint32_t presence_bit_index = 0U;
    std::uint32_t byte_offset = 0U;
    std::uint32_t bit_offset = 0U;
    std::uint32_t bit_width = 0U;
    std::uint32_t slot_size = 0U;
    BrfV2FieldStorage storage = BrfV2FieldStorage::Fixed;
    BrfV2TypeLayout type;
};

struct BrfV2RecordLayout {
    std::uint32_t record_id = 0U;
    std::uint32_t presence_bitmap_size = 0U;
    std::uint32_t fixed_region_size = 0U;
    // A value is present only for records whose complete BRF v2 encoding is
    // schema-determined.  Its value includes the 16-byte child header.
    std::optional<std::uint32_t> complete_fixed_record_size;
    std::vector<BrfV2FieldLayout> fields;
};

struct BrfV2LayoutRegistry {
    std::vector<BrfV2RecordLayout> records;

    [[nodiscard]] const BrfV2RecordLayout* find(std::uint32_t record_id) const {
        const auto found = std::find_if(
            records.begin(), records.end(),
            [record_id](const BrfV2RecordLayout& record) { return record.record_id == record_id; });
        return found == records.end() ? nullptr : &*found;
    }
};

struct BrfV2Header {
    std::uint8_t format_version = kBinaryRecordV2FormatVersion;
    std::uint8_t flags = 0U;
    std::uint16_t header_size = kBinaryRecordV2HeaderSize;
    std::uint32_t record_id = 0U;
    std::uint32_t fixed_region_size = 0U;
    std::uint32_t record_size = 0U;
};

enum class BrfV2Error {
    none,
    truncated_header,
    unsupported_version,
    unsupported_flags,
    invalid_header,
    invalid_record_size,
    invalid_fixed_region,
    invalid_presence,
    invalid_slot,
    invalid_descriptor,
    invalid_variable_range,
    noncanonical_tail,
    malformed_array,
    malformed_varuint,
    bounds_exceeded,
    unexpected_record_id,
    invalid_nested_record,
    overflow,
};

struct BrfV2ValidationResult {
    BrfV2Error error = BrfV2Error::none;
    std::uint64_t offset = 0U;

    [[nodiscard]] bool ok() const { return error == BrfV2Error::none; }
};

struct BrfV2EncodeResult {
    std::optional<std::vector<std::byte>> value;
    BrfV2Error error = BrfV2Error::none;

    [[nodiscard]] bool ok() const { return value.has_value(); }
};

inline BrfV2ValidationResult brf_v2_failure(BrfV2Error error, std::uint64_t offset = 0U) {
    return BrfV2ValidationResult{.error = error, .offset = offset};
}

inline void write_brf_v2_header(std::span<std::byte> output, const BrfV2Header& header) {
    output[0] = static_cast<std::byte>(header.format_version);
    output[1] = static_cast<std::byte>(header.flags);
    output[2] = static_cast<std::byte>((header.header_size >> 8U) & 0xFFU);
    output[3] = static_cast<std::byte>(header.header_size & 0xFFU);
    output[4] = static_cast<std::byte>((header.record_id >> 24U) & 0xFFU);
    output[5] = static_cast<std::byte>((header.record_id >> 16U) & 0xFFU);
    output[6] = static_cast<std::byte>((header.record_id >> 8U) & 0xFFU);
    output[7] = static_cast<std::byte>(header.record_id & 0xFFU);
    output[8] = static_cast<std::byte>((header.fixed_region_size >> 24U) & 0xFFU);
    output[9] = static_cast<std::byte>((header.fixed_region_size >> 16U) & 0xFFU);
    output[10] = static_cast<std::byte>((header.fixed_region_size >> 8U) & 0xFFU);
    output[11] = static_cast<std::byte>(header.fixed_region_size & 0xFFU);
    output[12] = static_cast<std::byte>((header.record_size >> 24U) & 0xFFU);
    output[13] = static_cast<std::byte>((header.record_size >> 16U) & 0xFFU);
    output[14] = static_cast<std::byte>((header.record_size >> 8U) & 0xFFU);
    output[15] = static_cast<std::byte>(header.record_size & 0xFFU);
}

inline std::optional<BrfV2Header> read_brf_v2_header(std::span<const std::byte> input) {
    if (input.size() < kBinaryRecordV2HeaderSize) {
        return std::nullopt;
    }
    return BrfV2Header{
        .format_version = byte_value(input[0]),
        .flags = byte_value(input[1]),
        .header_size = read_raw_u16(input.subspan(2U, 2U)),
        .record_id = read_raw_u32(input.subspan(4U, 4U)),
        .fixed_region_size = read_raw_u32(input.subspan(8U, 4U)),
        .record_size = read_raw_u32(input.subspan(12U, 4U)),
    };
}

inline std::uint32_t read_brf_v2_descriptor_offset(std::span<const std::byte> descriptor) {
    return read_raw_u32(descriptor.subspan(0U, 4U));
}

inline std::uint32_t read_brf_v2_descriptor_length(std::span<const std::byte> descriptor) {
    return read_raw_u32(descriptor.subspan(4U, 4U));
}

inline void write_brf_v2_descriptor(std::span<std::byte> descriptor, std::uint32_t data_offset,
                                    std::uint32_t byte_length) {
    descriptor[0] = static_cast<std::byte>((data_offset >> 24U) & 0xFFU);
    descriptor[1] = static_cast<std::byte>((data_offset >> 16U) & 0xFFU);
    descriptor[2] = static_cast<std::byte>((data_offset >> 8U) & 0xFFU);
    descriptor[3] = static_cast<std::byte>(data_offset & 0xFFU);
    descriptor[4] = static_cast<std::byte>((byte_length >> 24U) & 0xFFU);
    descriptor[5] = static_cast<std::byte>((byte_length >> 16U) & 0xFFU);
    descriptor[6] = static_cast<std::byte>((byte_length >> 8U) & 0xFFU);
    descriptor[7] = static_cast<std::byte>(byte_length & 0xFFU);
}

inline bool brf_v2_is_zero(std::span<const std::byte> bytes) {
    return std::all_of(bytes.begin(), bytes.end(),
                       [](std::byte value) { return value == std::byte{0}; });
}

inline bool brf_v2_is_valid_enum(std::span<const std::byte> value, const BrfV2TypeLayout& type) {
    std::uint64_t numeric_value = 0U;
    switch (type.encoded_width) {
    case 1U:
        if (value.size() != 1U) {
            return false;
        }
        numeric_value = byte_value(value[0]);
        break;
    case 2U:
        if (value.size() != 2U) {
            return false;
        }
        numeric_value = read_raw_u16(value);
        break;
    case 4U:
        if (value.size() != 4U) {
            return false;
        }
        numeric_value = read_raw_u32(value);
        break;
    case 8U:
        if (value.size() != 8U) {
            return false;
        }
        numeric_value = read_raw_u64(value);
        break;
    default:
        return false;
    }
    return std::find(type.enum_values.begin(), type.enum_values.end(), numeric_value) !=
           type.enum_values.end();
}

inline const BrfV2FieldLayout* brf_v2_find_field(const BrfV2RecordLayout& layout,
                                                 std::uint32_t field_index) {
    const auto found = std::find_if(
        layout.fields.begin(), layout.fields.end(),
        [field_index](const BrfV2FieldLayout& field) { return field.field_index == field_index; });
    return found == layout.fields.end() ? nullptr : &*found;
}

inline bool brf_v2_is_present(std::span<const std::byte> record, const BrfV2FieldLayout& field) {
    const std::size_t bitmap_offset = kBinaryRecordV2HeaderSize;
    const std::size_t byte_offset = bitmap_offset + field.presence_bit_index / 8U;
    return (byte_value(record[byte_offset]) &
            static_cast<std::uint8_t>(1U << (field.presence_bit_index % 8U))) != 0U;
}

inline void brf_v2_set_present(std::span<std::byte> record, const BrfV2FieldLayout& field) {
    const std::size_t byte_offset = kBinaryRecordV2HeaderSize + field.presence_bit_index / 8U;
    record[byte_offset] =
        static_cast<std::byte>(byte_value(record[byte_offset]) |
                               static_cast<std::uint8_t>(1U << (field.presence_bit_index % 8U)));
}

inline void brf_v2_clear_present(std::span<std::byte> record, const BrfV2FieldLayout& field) {
    const std::size_t byte_offset = kBinaryRecordV2HeaderSize + field.presence_bit_index / 8U;
    record[byte_offset] =
        static_cast<std::byte>(byte_value(record[byte_offset]) &
                               static_cast<std::uint8_t>(~(1U << (field.presence_bit_index % 8U))));
}

inline BrfV2ValidationResult validate_brf_v2(std::span<const std::byte> input,
                                             const BrfV2RecordLayout& layout,
                                             const BrfV2LayoutRegistry& registry);

class BrfV2Builder {
public:
    BrfV2Builder(const BrfV2RecordLayout& layout, const BrfV2LayoutRegistry& registry)
        : layout_(layout), registry_(registry), values_(layout.fields.size()) {}

    [[nodiscard]] bool set_field(std::uint32_t field_index, std::span<const std::byte> value) {
        const BrfV2FieldLayout* field = brf_v2_find_field(layout_, field_index);
        if (field == nullptr) {
            return false;
        }
        const std::size_t position = static_cast<std::size_t>(field - layout_.fields.data());
        values_[position] = std::vector<std::byte>(value.begin(), value.end());
        return true;
    }

    void clear_field(std::uint32_t field_index) {
        const BrfV2FieldLayout* field = brf_v2_find_field(layout_, field_index);
        if (field != nullptr) {
            values_[static_cast<std::size_t>(field - layout_.fields.data())].reset();
        }
    }

    [[nodiscard]] BrfV2EncodeResult finalize() const {
        const std::uint64_t fixed_end =
            static_cast<std::uint64_t>(kBinaryRecordV2HeaderSize) + layout_.fixed_region_size;
        if (fixed_end > std::numeric_limits<std::uint32_t>::max()) {
            return {.value = std::nullopt, .error = BrfV2Error::overflow};
        }

        std::vector<std::byte> output(static_cast<std::size_t>(fixed_end), std::byte{0});
        std::uint64_t tail_offset = fixed_end;
        for (std::size_t index = 0U; index < layout_.fields.size(); ++index) {
            const BrfV2FieldLayout& field = layout_.fields[index];
            if (!values_[index].has_value()) {
                continue;
            }
            const std::vector<std::byte>& value = *values_[index];
            const std::uint64_t slot_end =
                static_cast<std::uint64_t>(field.byte_offset) + field.slot_size;
            if (slot_end > fixed_end || field.byte_offset < kBinaryRecordV2HeaderSize ||
                field.bit_offset != 0U || field.bit_width != field.slot_size * 8U) {
                return {.value = std::nullopt, .error = BrfV2Error::invalid_slot};
            }
            brf_v2_set_present(output, field);
            if (field.storage == BrfV2FieldStorage::VariableDescriptor) {
                if (value.size() > std::numeric_limits<std::uint32_t>::max() ||
                    tail_offset > std::numeric_limits<std::uint32_t>::max() - value.size()) {
                    return {.value = std::nullopt, .error = BrfV2Error::overflow};
                }
                const std::uint32_t offset = static_cast<std::uint32_t>(tail_offset);
                const std::uint32_t length = static_cast<std::uint32_t>(value.size());
                output.insert(output.end(), value.begin(), value.end());
                write_brf_v2_descriptor(
                    std::span<std::byte>(output).subspan(field.byte_offset,
                                                         kBinaryRecordV2VariableDescriptorSize),
                    offset, length);
                tail_offset += value.size();
            } else {
                if (value.size() != field.slot_size) {
                    return {.value = std::nullopt, .error = BrfV2Error::invalid_slot};
                }
                std::copy(value.begin(), value.end(), output.begin() + field.byte_offset);
            }
        }

        if (tail_offset > std::numeric_limits<std::uint32_t>::max()) {
            return {.value = std::nullopt, .error = BrfV2Error::overflow};
        }
        write_brf_v2_header(std::span<std::byte>(output).first(kBinaryRecordV2HeaderSize),
                            BrfV2Header{.record_id = layout_.record_id,
                                        .fixed_region_size = layout_.fixed_region_size,
                                        .record_size = static_cast<std::uint32_t>(tail_offset)});

        const BrfV2ValidationResult validation = validate_brf_v2(output, layout_, registry_);
        if (!validation.ok()) {
            return {.value = std::nullopt, .error = validation.error};
        }
        return {.value = std::move(output), .error = BrfV2Error::none};
    }

private:
    const BrfV2RecordLayout& layout_;
    const BrfV2LayoutRegistry& registry_;
    std::vector<std::optional<std::vector<std::byte>>> values_;
};

inline BrfV2ValidationResult validate_brf_v2(std::span<const std::byte> input,
                                             const BrfV2RecordLayout& layout,
                                             const BrfV2LayoutRegistry& registry) {
    struct Frame {
        std::span<const std::byte> bytes;
        const BrfV2RecordLayout* layout;
    };

    std::vector<Frame> workstack;
    workstack.push_back({input, &layout});
    while (!workstack.empty()) {
        const Frame frame = workstack.back();
        workstack.pop_back();
        const auto header = read_brf_v2_header(frame.bytes);
        if (!header.has_value()) {
            return brf_v2_failure(BrfV2Error::truncated_header);
        }
        if (header->format_version != kBinaryRecordV2FormatVersion) {
            return brf_v2_failure(BrfV2Error::unsupported_version);
        }
        if (header->flags != 0U) {
            return brf_v2_failure(BrfV2Error::unsupported_flags, 1U);
        }
        if (header->header_size != kBinaryRecordV2HeaderSize) {
            return brf_v2_failure(BrfV2Error::invalid_header, 2U);
        }
        if (header->record_id != frame.layout->record_id) {
            return brf_v2_failure(BrfV2Error::unexpected_record_id, 4U);
        }
        const std::uint64_t fixed_end =
            static_cast<std::uint64_t>(header->header_size) + header->fixed_region_size;
        if (header->fixed_region_size != frame.layout->fixed_region_size ||
            fixed_end > std::numeric_limits<std::uint32_t>::max() ||
            fixed_end > frame.bytes.size() || header->record_size != frame.bytes.size() ||
            header->record_size < fixed_end) {
            return brf_v2_failure(BrfV2Error::invalid_record_size, 8U);
        }
        if (frame.layout->presence_bitmap_size != (frame.layout->fields.size() + 7U) / 8U) {
            return brf_v2_failure(BrfV2Error::invalid_presence);
        }
        const std::size_t bitmap_end =
            kBinaryRecordV2HeaderSize + frame.layout->presence_bitmap_size;
        if (bitmap_end > fixed_end) {
            return brf_v2_failure(BrfV2Error::invalid_fixed_region);
        }
        if (!frame.layout->fields.empty() && (frame.layout->fields.size() % 8U) != 0U) {
            const std::uint8_t unused = static_cast<std::uint8_t>(
                byte_value(frame.bytes[bitmap_end - 1U]) &
                static_cast<std::uint8_t>(0xFFU << (frame.layout->fields.size() % 8U)));
            if (unused != 0U) {
                return brf_v2_failure(BrfV2Error::invalid_presence, bitmap_end - 1U);
            }
        }

        std::uint64_t tail_cursor = fixed_end;
        std::uint64_t fixed_cursor = bitmap_end;
        for (std::size_t field_position = 0U; field_position < frame.layout->fields.size();
             ++field_position) {
            const BrfV2FieldLayout& field = frame.layout->fields[field_position];
            const std::uint64_t slot_end =
                static_cast<std::uint64_t>(field.byte_offset) + field.slot_size;
            if (field.presence_bit_index != field_position ||
                field.presence_bit_index / 8U >= frame.layout->presence_bitmap_size ||
                field.byte_offset != fixed_cursor || slot_end > fixed_end ||
                field.bit_offset != 0U ||
                field.slot_size > std::numeric_limits<std::uint32_t>::max() / 8U ||
                field.bit_width != field.slot_size * 8U) {
                return brf_v2_failure(BrfV2Error::invalid_slot, field.byte_offset);
            }
            fixed_cursor = slot_end;
            const bool present = brf_v2_is_present(frame.bytes, field);
            const std::span<const std::byte> slot =
                frame.bytes.subspan(field.byte_offset, field.slot_size);
            if (!present) {
                if (!brf_v2_is_zero(slot)) {
                    return brf_v2_failure(BrfV2Error::invalid_presence, field.byte_offset);
                }
                continue;
            }

            if (field.storage == BrfV2FieldStorage::VariableDescriptor) {
                if (field.slot_size != kBinaryRecordV2VariableDescriptorSize) {
                    return brf_v2_failure(BrfV2Error::invalid_descriptor, field.byte_offset);
                }
                const std::uint32_t data_offset = read_brf_v2_descriptor_offset(slot);
                const std::uint32_t byte_length = read_brf_v2_descriptor_length(slot);
                if (data_offset < fixed_end || data_offset > header->record_size ||
                    byte_length > header->record_size - data_offset || data_offset != tail_cursor) {
                    return brf_v2_failure(BrfV2Error::invalid_variable_range, field.byte_offset);
                }
                const std::span<const std::byte> value =
                    frame.bytes.subspan(data_offset, byte_length);
                if (field.type.kind == BrfV2TypeKind::String &&
                    (byte_length > field.type.max_bytes || !is_valid_utf8(value))) {
                    return brf_v2_failure(BrfV2Error::bounds_exceeded, data_offset);
                }
                if (field.type.kind == BrfV2TypeKind::Bytes && byte_length > field.type.max_bytes) {
                    return brf_v2_failure(BrfV2Error::bounds_exceeded, data_offset);
                }
                if (field.type.kind == BrfV2TypeKind::Array) {
                    std::size_t array_offset = 0U;
                    const auto count = read_varuint(value, array_offset);
                    if (!count.value.has_value() || *count.value > field.type.max_elements ||
                        array_offset > value.size()) {
                        return brf_v2_failure(BrfV2Error::malformed_array, data_offset);
                    }
                    const BrfV2TypeLayout* element = field.type.element_type.get();
                    if (element == nullptr) {
                        return brf_v2_failure(BrfV2Error::malformed_array, data_offset);
                    }
                    if (element->kind != BrfV2TypeKind::Record &&
                        element->kind != BrfV2TypeKind::String &&
                        element->kind != BrfV2TypeKind::Bytes) {
                        if (element->encoded_width == 0U ||
                            *count.value > (value.size() - array_offset) / element->encoded_width ||
                            array_offset + *count.value * element->encoded_width != value.size()) {
                            return brf_v2_failure(BrfV2Error::malformed_array, data_offset);
                        }
                        if (element->kind == BrfV2TypeKind::Bool) {
                            for (std::uint64_t item = 0U; item < *count.value; ++item) {
                                const std::size_t item_offset =
                                    array_offset + static_cast<std::size_t>(item);
                                const std::uint8_t encoded = byte_value(value[item_offset]);
                                if (encoded != 0U && encoded != 1U) {
                                    return brf_v2_failure(BrfV2Error::invalid_slot,
                                                          data_offset + item_offset);
                                }
                            }
                        }
                        if (element->kind == BrfV2TypeKind::Enum) {
                            for (std::uint64_t item = 0U; item < *count.value; ++item) {
                                const std::size_t item_offset =
                                    array_offset +
                                    static_cast<std::size_t>(item) * element->encoded_width;
                                if (!brf_v2_is_valid_enum(
                                        value.subspan(item_offset, element->encoded_width),
                                        *element)) {
                                    return brf_v2_failure(BrfV2Error::invalid_slot,
                                                          data_offset + item_offset);
                                }
                            }
                        }
                    } else if (element->kind == BrfV2TypeKind::Record) {
                        const BrfV2RecordLayout* child = registry.find(element->nested_record_id);
                        if (child == nullptr) {
                            return brf_v2_failure(BrfV2Error::invalid_nested_record, data_offset);
                        }
                        if (child->complete_fixed_record_size.has_value()) {
                            const std::uint64_t child_size = *child->complete_fixed_record_size;
                            if (child_size == 0U ||
                                *count.value > (value.size() - array_offset) / child_size ||
                                array_offset + *count.value * child_size != value.size()) {
                                return brf_v2_failure(BrfV2Error::malformed_array, data_offset);
                            }
                            for (std::uint64_t item = 0U; item < *count.value; ++item) {
                                workstack.push_back(
                                    {value.subspan(array_offset, child_size), child});
                                array_offset += static_cast<std::size_t>(child_size);
                            }
                        } else {
                            for (std::uint64_t item = 0U; item < *count.value; ++item) {
                                const auto length = read_varuint(value, array_offset);
                                if (!length.value.has_value() ||
                                    *length.value > value.size() - array_offset) {
                                    return brf_v2_failure(BrfV2Error::malformed_array, data_offset);
                                }
                                if (*length.value < kBinaryRecordV2HeaderSize) {
                                    return brf_v2_failure(BrfV2Error::invalid_nested_record,
                                                          data_offset);
                                }
                                workstack.push_back(
                                    {value.subspan(array_offset, *length.value), child});
                                array_offset += static_cast<std::size_t>(*length.value);
                            }
                        }
                        if (array_offset != value.size()) {
                            return brf_v2_failure(BrfV2Error::malformed_array, data_offset);
                        }
                    } else {
                        for (std::uint64_t item = 0U; item < *count.value; ++item) {
                            const auto length = read_varuint(value, array_offset);
                            if (!length.value.has_value() ||
                                *length.value > value.size() - array_offset) {
                                return brf_v2_failure(BrfV2Error::malformed_array, data_offset);
                            }
                            const std::span<const std::byte> item_bytes =
                                value.subspan(array_offset, *length.value);
                            if (element->kind == BrfV2TypeKind::String &&
                                (*length.value > element->max_bytes ||
                                 !is_valid_utf8(item_bytes))) {
                                return brf_v2_failure(BrfV2Error::bounds_exceeded, data_offset);
                            }
                            if (element->kind == BrfV2TypeKind::Bytes &&
                                *length.value > element->max_bytes) {
                                return brf_v2_failure(BrfV2Error::bounds_exceeded, data_offset);
                            }
                            array_offset += static_cast<std::size_t>(*length.value);
                        }
                        if (array_offset != value.size()) {
                            return brf_v2_failure(BrfV2Error::malformed_array, data_offset);
                        }
                    }
                }
                if (field.type.kind == BrfV2TypeKind::Record) {
                    const BrfV2RecordLayout* child = registry.find(field.type.nested_record_id);
                    if (child == nullptr || byte_length < kBinaryRecordV2HeaderSize) {
                        return brf_v2_failure(BrfV2Error::invalid_nested_record, data_offset);
                    }
                    workstack.push_back({value, child});
                }
                tail_cursor += byte_length;
            } else {
                if (field.type.kind == BrfV2TypeKind::Bool &&
                    (slot.size() != 1U ||
                     (byte_value(slot[0]) != 0U && byte_value(slot[0]) != 1U))) {
                    return brf_v2_failure(BrfV2Error::invalid_slot, field.byte_offset);
                }
                if (field.type.kind == BrfV2TypeKind::Enum &&
                    !brf_v2_is_valid_enum(slot, field.type)) {
                    return brf_v2_failure(BrfV2Error::invalid_slot, field.byte_offset);
                }
                if (field.type.kind == BrfV2TypeKind::Record) {
                    const BrfV2RecordLayout* child = registry.find(field.type.nested_record_id);
                    if (child == nullptr || !child->complete_fixed_record_size.has_value() ||
                        slot.size() != *child->complete_fixed_record_size) {
                        return brf_v2_failure(BrfV2Error::invalid_nested_record, field.byte_offset);
                    }
                    workstack.push_back({slot, child});
                }
            }
        }
        if (tail_cursor != header->record_size) {
            return brf_v2_failure(BrfV2Error::noncanonical_tail,
                                  static_cast<std::uint64_t>(tail_cursor));
        }
    }
    return {};
}

} // namespace quarry::runtime
