#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace breadcrumbs::runtime {

inline constexpr std::uint8_t kBinaryRecordHeaderVersion = 1U;
inline constexpr std::size_t kBinaryRecordHeaderSize = 16U;

struct FieldBytes {
    std::uint8_t field_index = 0U;
    std::vector<std::byte> bytes;
};

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

inline bool append_f32(std::vector<std::byte>& output, float value) {
    static_assert(std::numeric_limits<float>::is_iec559,
                  "Breadcrumbs f32 encoding requires IEEE 754 binary32 floats");
    return append_u32(output, std::bit_cast<std::uint32_t>(value));
}

inline bool append_f64(std::vector<std::byte>& output, double value) {
    static_assert(std::numeric_limits<double>::is_iec559,
                  "Breadcrumbs f64 encoding requires IEEE 754 binary64 doubles");
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

inline std::optional<std::vector<std::byte>>
encode_record(std::uint32_t record_id, std::span<const FieldBytes> fields) {
    if (record_id == 0U || fields.size() > std::numeric_limits<std::uint8_t>::max()) {
        return std::nullopt;
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
            return std::nullopt;
        }
    }

    std::vector<std::byte> directory;
    std::vector<std::byte> payload;
    for (const FieldBytes& field : ordered_fields) {
        if (field.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        if (payload.size() > std::numeric_limits<std::uint32_t>::max() - field.bytes.size()) {
            return std::nullopt;
        }

        const std::uint64_t field_offset = payload.size();
        append_u8(directory, field.field_index);
        append_varuint(directory, field_offset);
        append_varuint(directory, field.bytes.size());
        payload.insert(payload.end(), field.bytes.begin(), field.bytes.end());
    }

    if (directory.size() > std::numeric_limits<std::uint32_t>::max() - payload.size()) {
        return std::nullopt;
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
    return output;
}

} // namespace breadcrumbs::runtime
