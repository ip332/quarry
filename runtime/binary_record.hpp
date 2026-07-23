#pragma once

#include "runtime/version.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace quarry::runtime {

inline constexpr std::uint8_t kBinaryRecordHeaderVersion = 1U;
inline constexpr std::size_t kBinaryRecordHeaderSize = 16U;

struct FieldBytes {
    std::uint8_t field_index = 0U;
    std::vector<std::byte> bytes;
};

struct FieldView {
    std::uint8_t field_index = 0U;
    std::span<const std::byte> bytes;
    std::uint64_t field_offset = 0U;
};

struct ParsedRecord {
    std::uint32_t record_id = 0U;
    std::vector<FieldView> fields;
};

enum class DecodeError {
    none,
    truncated_header,
    unsupported_version,
    unsupported_flags,
    invalid_header,
    invalid_payload_length,
    malformed_directory,
    malformed_varuint,
    duplicate_field,
    unsorted_directory,
    invalid_field_range,
    overlapping_field_range,
    invalid_field_length,
    invalid_bool,
    invalid_utf8,
    unexpected_record_id,
    bounds_exceeded,
    unknown_enum_value,
    unsupported_field_type,
    overflow,
};

enum class EncodeError {
    none,
    bounds_exceeded,
    invalid_utf8,
    unknown_enum_value,
    unsupported_field_type,
    overflow,
};

struct PathElement {
    std::uint8_t field_index = 0U;
    std::optional<std::uint32_t> array_index;
};

template <typename T, typename E>
struct CodecResult {
    std::optional<T> value;
    E error;
    std::vector<PathElement> path;
    std::optional<std::uint64_t> byte_offset;

    CodecResult() = delete;
    CodecResult(std::optional<T> result_value, E result_error,
                std::vector<PathElement> result_path = {},
                std::optional<std::uint64_t> result_byte_offset = std::nullopt)
        : value(std::move(result_value)), error(result_error), path(std::move(result_path)),
          byte_offset(result_byte_offset) {}

    [[nodiscard]] bool has_value() const { return value.has_value(); }
    explicit operator bool() const { return has_value(); }
};

template <typename T>
using EncodeResult = CodecResult<T, EncodeError>;

template <typename T>
using DecodeResult = CodecResult<T, DecodeError>;

struct ParseRecordResult {
    std::optional<ParsedRecord> record;
    DecodeError error = DecodeError::none;
    std::optional<std::uint64_t> offset;
};

template <typename T>
struct DecodeValueResult {
    std::optional<T> value;
    DecodeError error = DecodeError::none;
};

inline ParseRecordResult parse_error(DecodeError error,
                                     std::optional<std::uint64_t> offset = std::nullopt) {
    return ParseRecordResult{.record = std::nullopt, .error = error, .offset = offset};
}

inline ParseRecordResult parse_success(ParsedRecord record) {
    return ParseRecordResult{.record = std::move(record), .error = DecodeError::none,
                             .offset = std::nullopt};
}

template <typename T>
inline DecodeValueResult<T> decode_error(DecodeError error) {
    return DecodeValueResult<T>{.value = std::nullopt, .error = error};
}

template <typename T>
inline DecodeValueResult<T> decode_success(T value) {
    return DecodeValueResult<T>{.value = std::move(value), .error = DecodeError::none};
}

template <typename T>
inline EncodeResult<T> encode_failure(EncodeError error, std::vector<PathElement> path = {}) {
    return EncodeResult<T>(std::nullopt, error, std::move(path));
}

template <typename T>
inline EncodeResult<T> encode_success(T value) {
    return EncodeResult<T>(std::move(value), EncodeError::none);
}

template <typename T>
inline DecodeResult<T> decode_failure(DecodeError error, std::vector<PathElement> path = {},
                                      std::optional<std::uint64_t> byte_offset = std::nullopt) {
    return DecodeResult<T>(std::nullopt, error, std::move(path), byte_offset);
}

template <typename T>
inline DecodeResult<T> decoded_value(T value) {
    return DecodeResult<T>(std::move(value), DecodeError::none);
}

inline bool append_u8(std::vector<std::byte>& output, std::uint8_t value) {
    output.push_back(static_cast<std::byte>(value));
    return true;
}

inline bool append_u16(std::vector<std::byte>& output, std::uint16_t value) {
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>(value & 0xFFU));
    return true;
}

inline bool append_u32(std::vector<std::byte>& output, std::uint32_t value) {
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>(value & 0xFFU));
    return true;
}

inline bool append_u64(std::vector<std::byte>& output, std::uint64_t value) {
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 56U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 48U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 40U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 32U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    (void)append_u8(output, static_cast<std::uint8_t>(value & 0xFFU));
    return true;
}

inline bool append_i8(std::vector<std::byte>& output, std::int8_t value) {
    return append_u8(output, static_cast<std::uint8_t>(value));
}

inline bool append_i16(std::vector<std::byte>& output, std::int16_t value) {
    return append_u16(output, static_cast<std::uint16_t>(value));
}

inline bool append_i32(std::vector<std::byte>& output, std::int32_t value) {
    return append_u32(output, static_cast<std::uint32_t>(value));
}

inline bool append_i64(std::vector<std::byte>& output, std::int64_t value) {
    return append_u64(output, static_cast<std::uint64_t>(value));
}

inline bool append_bool(std::vector<std::byte>& output, bool value) {
    return append_u8(output, value ? 1U : 0U);
}

inline std::uint8_t byte_value(std::byte value) {
    return static_cast<std::uint8_t>(value);
}

inline bool append_bytes(std::vector<std::byte>& output, std::span<const std::byte> value) {
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

inline bool is_valid_utf8(std::span<const std::byte> input) {
    std::size_t index = 0U;
    while (index < input.size()) {
        const std::uint8_t first = byte_value(input[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        std::uint32_t code_point = 0U;
        std::size_t continuation_count = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            code_point = static_cast<std::uint32_t>(first & 0x1FU);
            continuation_count = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            code_point = static_cast<std::uint32_t>(first & 0x0FU);
            continuation_count = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            code_point = static_cast<std::uint32_t>(first & 0x07U);
            continuation_count = 3U;
        } else {
            return false;
        }

        if (input.size() - index - 1U < continuation_count) {
            return false;
        }
        for (std::size_t continuation = 0U; continuation < continuation_count; ++continuation) {
            const std::uint8_t byte = byte_value(input[index + continuation + 1U]);
            if ((byte & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | static_cast<std::uint32_t>(byte & 0x3FU);
        }

        if (continuation_count == 2U) {
            if (code_point < 0x800U || (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
                return false;
            }
        }
        if (continuation_count == 3U) {
            if (code_point < 0x10000U || code_point > 0x10FFFFU) {
                return false;
            }
        }

        index += continuation_count + 1U;
    }
    return true;
}

inline bool append_string_utf8(std::vector<std::byte>& output, std::string_view value) {
    const auto bytes = std::as_bytes(std::span<const char>(value.data(), value.size()));
    if (!is_valid_utf8(bytes)) {
        return false;
    }
    return append_bytes(output, bytes);
}

inline bool append_f32(std::vector<std::byte>& output, float value) {
    static_assert(std::numeric_limits<float>::is_iec559,
                  "Quarry f32 encoding requires IEEE 754 binary32 floats");
    return append_u32(output, std::bit_cast<std::uint32_t>(value));
}

inline bool append_f64(std::vector<std::byte>& output, double value) {
    static_assert(std::numeric_limits<double>::is_iec559,
                  "Quarry f64 encoding requires IEEE 754 binary64 doubles");
    return append_u64(output, std::bit_cast<std::uint64_t>(value));
}

inline void append_varuint(std::vector<std::byte>& output, std::uint64_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0U) {
            byte = static_cast<std::uint8_t>(byte | 0x80U);
        }
        (void)append_u8(output, byte);
    } while (value != 0U);
}

inline std::uint16_t read_raw_u16(std::span<const std::byte> input) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(byte_value(input[0])) << 8U) |
                                      static_cast<std::uint16_t>(byte_value(input[1])));
}

inline std::uint32_t read_raw_u32(std::span<const std::byte> input) {
    return (static_cast<std::uint32_t>(byte_value(input[0])) << 24U) |
           (static_cast<std::uint32_t>(byte_value(input[1])) << 16U) |
           (static_cast<std::uint32_t>(byte_value(input[2])) << 8U) |
           static_cast<std::uint32_t>(byte_value(input[3]));
}

inline std::uint64_t read_raw_u64(std::span<const std::byte> input) {
    return (static_cast<std::uint64_t>(byte_value(input[0])) << 56U) |
           (static_cast<std::uint64_t>(byte_value(input[1])) << 48U) |
           (static_cast<std::uint64_t>(byte_value(input[2])) << 40U) |
           (static_cast<std::uint64_t>(byte_value(input[3])) << 32U) |
           (static_cast<std::uint64_t>(byte_value(input[4])) << 24U) |
           (static_cast<std::uint64_t>(byte_value(input[5])) << 16U) |
           (static_cast<std::uint64_t>(byte_value(input[6])) << 8U) |
           static_cast<std::uint64_t>(byte_value(input[7]));
}

inline DecodeValueResult<std::uint64_t> read_varuint(std::span<const std::byte> input,
                                                     std::size_t& offset) {
    std::uint64_t value = 0U;
    std::uint8_t shift = 0U;
    for (std::uint8_t index = 0U; index < 10U; ++index) {
        if (offset >= input.size()) {
            return decode_error<std::uint64_t>(DecodeError::malformed_varuint);
        }
        const std::uint8_t byte = byte_value(input[offset]);
        ++offset;
        const std::uint64_t payload = static_cast<std::uint64_t>(byte & 0x7FU);
        if (shift == 63U && payload > 1U) {
            return decode_error<std::uint64_t>(DecodeError::malformed_varuint);
        }
        value |= payload << shift;
        if ((byte & 0x80U) == 0U) {
            return decode_success<std::uint64_t>(value);
        }
        shift = static_cast<std::uint8_t>(shift + 7U);
    }
    return decode_error<std::uint64_t>(DecodeError::malformed_varuint);
}

inline const FieldView* find_field(const ParsedRecord& record, std::uint8_t field_index) {
    for (const FieldView& field : record.fields) {
        if (field.field_index == field_index) {
            return &field;
        }
        if (field.field_index > field_index) {
            return nullptr;
        }
    }
    return nullptr;
}

inline ParseRecordResult parse_record(std::span<const std::byte> input) {
    if (input.size() < kBinaryRecordHeaderSize) {
        return parse_error(DecodeError::truncated_header, 0U);
    }

    if (byte_value(input[0]) != kBinaryRecordHeaderVersion) {
        return parse_error(DecodeError::unsupported_version, 0U);
    }
    const std::uint8_t flags = byte_value(input[1]);
    const std::uint8_t directory_entry_count = byte_value(input[2]);
    const std::uint8_t reserved0 = byte_value(input[3]);
    const std::uint32_t record_id = read_raw_u32(input.subspan(4U, 4U));
    const std::uint32_t reserved1 = read_raw_u32(input.subspan(8U, 4U));
    const std::uint32_t payload_length = read_raw_u32(input.subspan(12U, 4U));
    if (flags != 0U) {
        return parse_error(DecodeError::unsupported_flags, 1U);
    }
    if (reserved0 != 0U) {
        return parse_error(DecodeError::invalid_header, 3U);
    }
    if (reserved1 != 0U) {
        return parse_error(DecodeError::invalid_header, 8U);
    }
    if (payload_length > input.size() - kBinaryRecordHeaderSize) {
        return parse_error(DecodeError::invalid_payload_length, 12U);
    }
    if (input.size() != kBinaryRecordHeaderSize + static_cast<std::size_t>(payload_length)) {
        return parse_error(DecodeError::invalid_payload_length, 12U);
    }

    struct DirectoryEntry {
        std::uint8_t field_index = 0U;
        std::uint64_t field_offset = 0U;
        std::uint64_t field_length = 0U;
    };

    const std::span<const std::byte> body =
        input.subspan(kBinaryRecordHeaderSize, static_cast<std::size_t>(payload_length));
    std::vector<DirectoryEntry> entries;
    entries.reserve(directory_entry_count);
    std::size_t directory_offset = 0U;
    for (std::uint16_t index = 0U; index < directory_entry_count; ++index) {
        const std::size_t entry_start = directory_offset;
        if (directory_offset >= body.size()) {
            return parse_error(DecodeError::malformed_directory,
                              kBinaryRecordHeaderSize + entry_start);
        }
        DirectoryEntry entry;
        entry.field_index = byte_value(body[directory_offset]);
        ++directory_offset;

        const std::size_t field_offset_varuint_start = directory_offset;
        DecodeValueResult<std::uint64_t> field_offset = read_varuint(body, directory_offset);
        if (!field_offset.value.has_value()) {
            return parse_error(field_offset.error,
                              kBinaryRecordHeaderSize + field_offset_varuint_start);
        }
        const std::size_t field_length_varuint_start = directory_offset;
        DecodeValueResult<std::uint64_t> field_length = read_varuint(body, directory_offset);
        if (!field_length.value.has_value()) {
            return parse_error(field_length.error,
                              kBinaryRecordHeaderSize + field_length_varuint_start);
        }
        entry.field_offset = *field_offset.value;
        entry.field_length = *field_length.value;

        if (!entries.empty()) {
            if (entries.back().field_index == entry.field_index) {
                return parse_error(DecodeError::duplicate_field,
                                  kBinaryRecordHeaderSize + entry_start);
            }
            if (entries.back().field_index > entry.field_index) {
                return parse_error(DecodeError::unsorted_directory,
                                  kBinaryRecordHeaderSize + entry_start);
            }
        }
        entries.push_back(entry);
    }

    const std::size_t payload_start = directory_offset;
    const std::size_t payload_size = body.size() - payload_start;
    std::vector<FieldView> fields;
    fields.reserve(entries.size());
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    ranges.reserve(entries.size());

    const std::uint64_t payload_region_start =
        static_cast<std::uint64_t>(kBinaryRecordHeaderSize) + static_cast<std::uint64_t>(payload_start);
    for (const DirectoryEntry& entry : entries) {
        if (entry.field_offset > payload_size || entry.field_length > payload_size) {
            return parse_error(DecodeError::invalid_field_range,
                              payload_region_start + entry.field_offset);
        }
        if (entry.field_offset > std::numeric_limits<std::uint64_t>::max() - entry.field_length) {
            return parse_error(DecodeError::invalid_field_range,
                              payload_region_start + entry.field_offset);
        }
        const std::uint64_t field_end = entry.field_offset + entry.field_length;
        if (field_end > payload_size) {
            return parse_error(DecodeError::invalid_field_range,
                              payload_region_start + entry.field_offset);
        }
        const auto start = static_cast<std::size_t>(entry.field_offset);
        const auto length = static_cast<std::size_t>(entry.field_length);
        ranges.emplace_back(start, start + length);
        fields.push_back(FieldView{
            .field_index = entry.field_index,
            .bytes = body.subspan(payload_start + start, length),
            .field_offset = payload_region_start + start,
        });
    }

    std::sort(ranges.begin(), ranges.end());
    for (std::size_t index = 1U; index < ranges.size(); ++index) {
        if (ranges[index - 1U].second > ranges[index].first) {
            return parse_error(DecodeError::overlapping_field_range,
                              kBinaryRecordHeaderSize + payload_start + ranges[index].first);
        }
    }

    return parse_success(ParsedRecord{.record_id = record_id, .fields = fields});
}

inline DecodeValueResult<bool> read_bool(std::span<const std::byte> input) {
    if (input.size() != 1U) {
        return decode_error<bool>(DecodeError::invalid_field_length);
    }
    if (byte_value(input[0]) == 0U) {
        return decode_success<bool>(false);
    }
    if (byte_value(input[0]) == 1U) {
        return decode_success<bool>(true);
    }
    return decode_error<bool>(DecodeError::invalid_bool);
}

inline DecodeValueResult<std::uint8_t> read_u8(std::span<const std::byte> input) {
    if (input.size() != 1U) {
        return decode_error<std::uint8_t>(DecodeError::invalid_field_length);
    }
    return decode_success<std::uint8_t>(byte_value(input[0]));
}

inline DecodeValueResult<std::uint16_t> read_u16(std::span<const std::byte> input) {
    if (input.size() != 2U) {
        return decode_error<std::uint16_t>(DecodeError::invalid_field_length);
    }
    return decode_success<std::uint16_t>(read_raw_u16(input));
}

inline DecodeValueResult<std::uint32_t> read_u32(std::span<const std::byte> input) {
    if (input.size() != 4U) {
        return decode_error<std::uint32_t>(DecodeError::invalid_field_length);
    }
    return decode_success<std::uint32_t>(read_raw_u32(input));
}

inline DecodeValueResult<std::uint64_t> read_u64(std::span<const std::byte> input) {
    if (input.size() != 8U) {
        return decode_error<std::uint64_t>(DecodeError::invalid_field_length);
    }
    return decode_success<std::uint64_t>(read_raw_u64(input));
}

inline DecodeValueResult<std::vector<std::byte>> read_bytes(std::span<const std::byte> input) {
    return decode_success<std::vector<std::byte>>(std::vector<std::byte>(input.begin(),
                                                                         input.end()));
}

inline DecodeValueResult<std::string> read_string_utf8(std::span<const std::byte> input) {
    if (!is_valid_utf8(input)) {
        return decode_error<std::string>(DecodeError::invalid_utf8);
    }
    std::string output;
    output.reserve(input.size());
    for (std::byte byte : input) {
        output.push_back(static_cast<char>(byte_value(byte)));
    }
    return decode_success<std::string>(std::move(output));
}

inline DecodeValueResult<std::int8_t> read_i8(std::span<const std::byte> input) {
    DecodeValueResult<std::uint8_t> value = read_u8(input);
    if (!value.value.has_value()) {
        return decode_error<std::int8_t>(value.error);
    }
    return decode_success<std::int8_t>(static_cast<std::int8_t>(*value.value));
}

inline DecodeValueResult<std::int16_t> read_i16(std::span<const std::byte> input) {
    DecodeValueResult<std::uint16_t> value = read_u16(input);
    if (!value.value.has_value()) {
        return decode_error<std::int16_t>(value.error);
    }
    return decode_success<std::int16_t>(static_cast<std::int16_t>(*value.value));
}

inline DecodeValueResult<std::int32_t> read_i32(std::span<const std::byte> input) {
    DecodeValueResult<std::uint32_t> value = read_u32(input);
    if (!value.value.has_value()) {
        return decode_error<std::int32_t>(value.error);
    }
    return decode_success<std::int32_t>(static_cast<std::int32_t>(*value.value));
}

inline DecodeValueResult<std::int64_t> read_i64(std::span<const std::byte> input) {
    DecodeValueResult<std::uint64_t> value = read_u64(input);
    if (!value.value.has_value()) {
        return decode_error<std::int64_t>(value.error);
    }
    return decode_success<std::int64_t>(static_cast<std::int64_t>(*value.value));
}

inline DecodeValueResult<float> read_f32(std::span<const std::byte> input) {
    static_assert(std::numeric_limits<float>::is_iec559,
                  "Quarry f32 decoding requires IEEE 754 binary32 floats");
    DecodeValueResult<std::uint32_t> value = read_u32(input);
    if (!value.value.has_value()) {
        return decode_error<float>(value.error);
    }
    return decode_success<float>(std::bit_cast<float>(*value.value));
}

inline DecodeValueResult<double> read_f64(std::span<const std::byte> input) {
    static_assert(std::numeric_limits<double>::is_iec559,
                  "Quarry f64 decoding requires IEEE 754 binary64 floats");
    DecodeValueResult<std::uint64_t> value = read_u64(input);
    if (!value.value.has_value()) {
        return decode_error<double>(value.error);
    }
    return decode_success<double>(std::bit_cast<double>(*value.value));
}

inline EncodeResult<std::vector<std::byte>>
encode_record_result(std::uint32_t record_id, std::span<const FieldBytes> fields) {
    if (record_id == 0U || fields.size() > std::numeric_limits<std::uint8_t>::max()) {
        return encode_failure<std::vector<std::byte>>(EncodeError::overflow);
    }

    std::vector<FieldBytes> ordered_fields(fields.begin(), fields.end());
    for (std::size_t outer = 0; outer < ordered_fields.size(); ++outer) {
        for (std::size_t inner = outer + 1U; inner < ordered_fields.size(); ++inner) {
            if (ordered_fields[inner].field_index < ordered_fields[outer].field_index) {
                std::swap(ordered_fields[inner], ordered_fields[outer]);
            }
        }
    }

    for (std::size_t index = 1U; index < ordered_fields.size(); ++index) {
        if (ordered_fields[index - 1U].field_index == ordered_fields[index].field_index) {
            return encode_failure<std::vector<std::byte>>(EncodeError::overflow);
        }
    }

    std::vector<std::byte> directory;
    std::vector<std::byte> payload;
    for (const FieldBytes& field : ordered_fields) {
        if (field.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return encode_failure<std::vector<std::byte>>(EncodeError::overflow);
        }
        if (payload.size() > std::numeric_limits<std::uint32_t>::max() - field.bytes.size()) {
            return encode_failure<std::vector<std::byte>>(EncodeError::overflow);
        }

        const std::uint64_t field_offset = payload.size();
        append_u8(directory, field.field_index);
        append_varuint(directory, field_offset);
        append_varuint(directory, field.bytes.size());
        payload.insert(payload.end(), field.bytes.begin(), field.bytes.end());
    }

    if (directory.size() > std::numeric_limits<std::uint32_t>::max() - payload.size()) {
        return encode_failure<std::vector<std::byte>>(EncodeError::overflow);
    }
    const auto payload_length = static_cast<std::uint32_t>(directory.size() + payload.size());

    std::vector<std::byte> output;
    output.reserve(kBinaryRecordHeaderSize + directory.size() + payload.size());
    (void)append_u8(output, kBinaryRecordHeaderVersion);
    (void)append_u8(output, 0U);
    (void)append_u8(output, static_cast<std::uint8_t>(ordered_fields.size()));
    (void)append_u8(output, 0U);
    (void)append_u32(output, record_id);
    (void)append_u32(output, 0U);
    (void)append_u32(output, payload_length);
    output.insert(output.end(), directory.begin(), directory.end());
    output.insert(output.end(), payload.begin(), payload.end());
    return encode_success<std::vector<std::byte>>(std::move(output));
}

inline std::optional<std::vector<std::byte>>
encode_record(std::uint32_t record_id, std::span<const FieldBytes> fields) {
    auto encoded = encode_record_result(record_id, fields);
    if (!encoded.value.has_value()) {
        return std::nullopt;
    }
    return std::move(encoded.value);
}

} // namespace quarry::runtime
