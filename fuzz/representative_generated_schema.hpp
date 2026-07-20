#pragma once

#include "runtime/binary_record.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quarry::fuzz_generated {

enum class Mode : std::uint8_t {
    Off = 0,
    On = 1,
};

struct Child {
    std::optional<std::uint32_t> code;
    std::optional<std::string> label;
};

struct Example {
    std::optional<bool> active;
    std::optional<std::uint32_t> count;
    std::optional<std::int16_t> delta;
    std::optional<float> ratio;
    std::optional<double> precise;
    std::optional<Mode> mode;
    std::optional<std::string> name;
    std::optional<std::vector<std::byte>> payload;
    std::optional<std::vector<std::uint32_t>> counts;
    std::optional<std::vector<std::string>> tags;
    std::optional<std::vector<std::vector<std::byte>>> blobs;
    std::optional<Child> child;
    std::optional<std::vector<Child>> children;
};

inline runtime::EncodeResult<std::vector<std::byte>> encode_result(const Child& value) {
    std::vector<runtime::FieldBytes> fields;
    fields.reserve(2U);
    if (value.code.has_value()) {
        std::vector<std::byte> bytes;
        (void)runtime::append_u32(bytes, *value.code);
        fields.push_back(runtime::FieldBytes{.field_index = 0U, .bytes = std::move(bytes)});
    }
    if (value.label.has_value()) {
        if (value.label->size() > 4U) {
            return runtime::encode_failure<std::vector<std::byte>>(
                runtime::EncodeError::bounds_exceeded);
        }
        std::vector<std::byte> bytes;
        if (!runtime::append_string_utf8(bytes, *value.label)) {
            return runtime::encode_failure<std::vector<std::byte>>(
                runtime::EncodeError::invalid_utf8);
        }
        fields.push_back(runtime::FieldBytes{.field_index = 1U, .bytes = std::move(bytes)});
    }
    return runtime::encode_record_result(2U, fields);
}

inline runtime::DecodeResult<Child> decode_Child_result(std::span<const std::byte> input) {
    const auto parsed = runtime::parse_record(input);
    if (!parsed.record.has_value()) {
        return runtime::decode_failure<Child>(parsed.error);
    }
    if (parsed.record->record_id != 2U) {
        return runtime::decode_failure<Child>(runtime::DecodeError::unexpected_record_id);
    }

    Child value;
    if (const auto* field = runtime::find_field(*parsed.record, 0U); field != nullptr) {
        const auto decoded = runtime::read_u32(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Child>(decoded.error);
        }
        value.code = *decoded.value;
    }
    if (const auto* field = runtime::find_field(*parsed.record, 1U); field != nullptr) {
        if (field->bytes.size() > 4U) {
            return runtime::decode_failure<Child>(runtime::DecodeError::bounds_exceeded);
        }
        const auto decoded = runtime::read_string_utf8(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Child>(decoded.error);
        }
        value.label = *decoded.value;
    }
    return runtime::decoded_value<Child>(std::move(value));
}

inline std::optional<Child> decode_Child(std::span<const std::byte> input) {
    auto decoded = decode_Child_result(input);
    if (!decoded.value.has_value()) {
        return std::nullopt;
    }
    return std::move(decoded.value);
}

inline runtime::EncodeResult<std::vector<std::byte>> encode_result(const Example& value) {
    std::vector<runtime::FieldBytes> fields;
    fields.reserve(13U);

    if (value.active.has_value()) {
        std::vector<std::byte> bytes;
        (void)runtime::append_bool(bytes, *value.active);
        fields.push_back(runtime::FieldBytes{.field_index = 0U, .bytes = std::move(bytes)});
    }
    if (value.count.has_value()) {
        std::vector<std::byte> bytes;
        (void)runtime::append_u32(bytes, *value.count);
        fields.push_back(runtime::FieldBytes{.field_index = 1U, .bytes = std::move(bytes)});
    }
    if (value.delta.has_value()) {
        std::vector<std::byte> bytes;
        (void)runtime::append_i16(bytes, *value.delta);
        fields.push_back(runtime::FieldBytes{.field_index = 2U, .bytes = std::move(bytes)});
    }
    if (value.ratio.has_value()) {
        std::vector<std::byte> bytes;
        (void)runtime::append_f32(bytes, *value.ratio);
        fields.push_back(runtime::FieldBytes{.field_index = 3U, .bytes = std::move(bytes)});
    }
    if (value.precise.has_value()) {
        std::vector<std::byte> bytes;
        (void)runtime::append_f64(bytes, *value.precise);
        fields.push_back(runtime::FieldBytes{.field_index = 4U, .bytes = std::move(bytes)});
    }
    if (value.mode.has_value()) {
        std::vector<std::byte> bytes;
        (void)runtime::append_u8(bytes, static_cast<std::uint8_t>(*value.mode));
        fields.push_back(runtime::FieldBytes{.field_index = 5U, .bytes = std::move(bytes)});
    }
    if (value.name.has_value()) {
        if (value.name->size() > 8U) {
            return runtime::encode_failure<std::vector<std::byte>>(
                runtime::EncodeError::bounds_exceeded);
        }
        std::vector<std::byte> bytes;
        if (!runtime::append_string_utf8(bytes, *value.name)) {
            return runtime::encode_failure<std::vector<std::byte>>(
                runtime::EncodeError::invalid_utf8);
        }
        fields.push_back(runtime::FieldBytes{.field_index = 6U, .bytes = std::move(bytes)});
    }
    if (value.payload.has_value()) {
        if (value.payload->size() > 8U) {
            return runtime::encode_failure<std::vector<std::byte>>(
                runtime::EncodeError::bounds_exceeded);
        }
        std::vector<std::byte> bytes;
        (void)runtime::append_bytes(bytes, *value.payload);
        fields.push_back(runtime::FieldBytes{.field_index = 7U, .bytes = std::move(bytes)});
    }
    if (value.counts.has_value()) {
        if (value.counts->size() > 4U) {
            return runtime::encode_failure<std::vector<std::byte>>(
                runtime::EncodeError::bounds_exceeded);
        }
        std::vector<std::byte> bytes;
        runtime::append_varuint(bytes, value.counts->size());
        for (std::uint32_t element : *value.counts) {
            (void)runtime::append_u32(bytes, element);
        }
        fields.push_back(runtime::FieldBytes{.field_index = 8U, .bytes = std::move(bytes)});
    }
    if (value.tags.has_value()) {
        if (value.tags->size() > 3U) {
            return runtime::encode_failure<std::vector<std::byte>>(
                runtime::EncodeError::bounds_exceeded);
        }
        std::vector<std::byte> bytes;
        runtime::append_varuint(bytes, value.tags->size());
        for (const std::string& element : *value.tags) {
            if (element.size() > 4U) {
                return runtime::encode_failure<std::vector<std::byte>>(
                    runtime::EncodeError::bounds_exceeded);
            }
            runtime::append_varuint(bytes, element.size());
            if (!runtime::append_string_utf8(bytes, element)) {
                return runtime::encode_failure<std::vector<std::byte>>(
                    runtime::EncodeError::invalid_utf8);
            }
        }
        fields.push_back(runtime::FieldBytes{.field_index = 9U, .bytes = std::move(bytes)});
    }
    if (value.blobs.has_value()) {
        if (value.blobs->size() > 3U) {
            return runtime::encode_failure<std::vector<std::byte>>(
                runtime::EncodeError::bounds_exceeded);
        }
        std::vector<std::byte> bytes;
        runtime::append_varuint(bytes, value.blobs->size());
        for (const std::vector<std::byte>& element : *value.blobs) {
            if (element.size() > 4U) {
                return runtime::encode_failure<std::vector<std::byte>>(
                    runtime::EncodeError::bounds_exceeded);
            }
            runtime::append_varuint(bytes, element.size());
            (void)runtime::append_bytes(bytes, element);
        }
        fields.push_back(runtime::FieldBytes{.field_index = 10U, .bytes = std::move(bytes)});
    }
    if (value.child.has_value()) {
        auto encoded = encode_result(*value.child);
        if (!encoded.value.has_value()) {
            return runtime::encode_failure<std::vector<std::byte>>(encoded.error);
        }
        fields.push_back(runtime::FieldBytes{.field_index = 11U, .bytes = std::move(*encoded.value)});
    }
    if (value.children.has_value()) {
        if (value.children->size() > 3U) {
            return runtime::encode_failure<std::vector<std::byte>>(
                runtime::EncodeError::bounds_exceeded);
        }
        std::vector<std::byte> bytes;
        runtime::append_varuint(bytes, value.children->size());
        for (const Child& element : *value.children) {
            auto encoded = encode_result(element);
            if (!encoded.value.has_value()) {
                return runtime::encode_failure<std::vector<std::byte>>(encoded.error);
            }
            runtime::append_varuint(bytes, encoded.value->size());
            (void)runtime::append_bytes(bytes, *encoded.value);
        }
        fields.push_back(runtime::FieldBytes{.field_index = 12U, .bytes = std::move(bytes)});
    }

    return runtime::encode_record_result(1U, fields);
}

inline runtime::DecodeResult<Example> decode_Example_result(std::span<const std::byte> input) {
    const auto parsed = runtime::parse_record(input);
    if (!parsed.record.has_value()) {
        return runtime::decode_failure<Example>(parsed.error);
    }
    if (parsed.record->record_id != 1U) {
        return runtime::decode_failure<Example>(runtime::DecodeError::unexpected_record_id);
    }

    Example value;
    if (const auto* field = runtime::find_field(*parsed.record, 0U); field != nullptr) {
        const auto decoded = runtime::read_bool(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Example>(decoded.error);
        }
        value.active = *decoded.value;
    }
    if (const auto* field = runtime::find_field(*parsed.record, 1U); field != nullptr) {
        const auto decoded = runtime::read_u32(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Example>(decoded.error);
        }
        value.count = *decoded.value;
    }
    if (const auto* field = runtime::find_field(*parsed.record, 2U); field != nullptr) {
        const auto decoded = runtime::read_i16(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Example>(decoded.error);
        }
        value.delta = *decoded.value;
    }
    if (const auto* field = runtime::find_field(*parsed.record, 3U); field != nullptr) {
        const auto decoded = runtime::read_f32(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Example>(decoded.error);
        }
        value.ratio = *decoded.value;
    }
    if (const auto* field = runtime::find_field(*parsed.record, 4U); field != nullptr) {
        const auto decoded = runtime::read_f64(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Example>(decoded.error);
        }
        value.precise = *decoded.value;
    }
    if (const auto* field = runtime::find_field(*parsed.record, 5U); field != nullptr) {
        const auto decoded = runtime::read_u8(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Example>(decoded.error);
        }
        if (*decoded.value > static_cast<std::uint8_t>(Mode::On)) {
            return runtime::decode_failure<Example>(runtime::DecodeError::unknown_enum_value);
        }
        value.mode = static_cast<Mode>(*decoded.value);
    }
    if (const auto* field = runtime::find_field(*parsed.record, 6U); field != nullptr) {
        if (field->bytes.size() > 8U) {
            return runtime::decode_failure<Example>(runtime::DecodeError::bounds_exceeded);
        }
        const auto decoded = runtime::read_string_utf8(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Example>(decoded.error);
        }
        value.name = *decoded.value;
    }
    if (const auto* field = runtime::find_field(*parsed.record, 7U); field != nullptr) {
        if (field->bytes.size() > 8U) {
            return runtime::decode_failure<Example>(runtime::DecodeError::bounds_exceeded);
        }
        const auto decoded = runtime::read_bytes(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Example>(decoded.error);
        }
        value.payload = *decoded.value;
    }
    if (const auto* field = runtime::find_field(*parsed.record, 8U); field != nullptr) {
        std::size_t offset = 0U;
        const auto count = runtime::read_varuint(field->bytes, offset);
        if (!count.value.has_value()) {
            return runtime::decode_failure<Example>(count.error);
        }
        if (*count.value > 4U) {
            return runtime::decode_failure<Example>(runtime::DecodeError::bounds_exceeded);
        }
        const std::size_t element_count = static_cast<std::size_t>(*count.value);
        const std::size_t remaining = field->bytes.size() - offset;
        if (element_count > remaining / 4U || remaining != element_count * 4U) {
            return runtime::decode_failure<Example>(runtime::DecodeError::invalid_field_length);
        }
        std::vector<std::uint32_t> elements;
        elements.reserve(element_count);
        for (std::size_t index = 0U; index < element_count; ++index) {
            const auto decoded = runtime::read_u32(field->bytes.subspan(offset, 4U));
            if (!decoded.value.has_value()) {
                return runtime::decode_failure<Example>(decoded.error);
            }
            elements.push_back(*decoded.value);
            offset += 4U;
        }
        value.counts = std::move(elements);
    }
    if (const auto* field = runtime::find_field(*parsed.record, 9U); field != nullptr) {
        std::size_t offset = 0U;
        const auto count = runtime::read_varuint(field->bytes, offset);
        if (!count.value.has_value()) {
            return runtime::decode_failure<Example>(count.error);
        }
        if (*count.value > 3U) {
            return runtime::decode_failure<Example>(runtime::DecodeError::bounds_exceeded);
        }
        std::vector<std::string> elements;
        elements.reserve(static_cast<std::size_t>(*count.value));
        for (std::size_t index = 0U; index < static_cast<std::size_t>(*count.value); ++index) {
            const auto length = runtime::read_varuint(field->bytes, offset);
            if (!length.value.has_value()) {
                return runtime::decode_failure<Example>(length.error);
            }
            if (*length.value > 4U || *length.value > field->bytes.size() - offset) {
                return runtime::decode_failure<Example>(
                    *length.value > 4U ? runtime::DecodeError::bounds_exceeded
                                       : runtime::DecodeError::invalid_field_length);
            }
            const auto bytes = field->bytes.subspan(offset, static_cast<std::size_t>(*length.value));
            const auto decoded = runtime::read_string_utf8(bytes);
            if (!decoded.value.has_value()) {
                return runtime::decode_failure<Example>(decoded.error);
            }
            elements.push_back(*decoded.value);
            offset += static_cast<std::size_t>(*length.value);
        }
        if (offset != field->bytes.size()) {
            return runtime::decode_failure<Example>(runtime::DecodeError::invalid_field_length);
        }
        value.tags = std::move(elements);
    }
    if (const auto* field = runtime::find_field(*parsed.record, 10U); field != nullptr) {
        std::size_t offset = 0U;
        const auto count = runtime::read_varuint(field->bytes, offset);
        if (!count.value.has_value()) {
            return runtime::decode_failure<Example>(count.error);
        }
        if (*count.value > 3U) {
            return runtime::decode_failure<Example>(runtime::DecodeError::bounds_exceeded);
        }
        std::vector<std::vector<std::byte>> elements;
        elements.reserve(static_cast<std::size_t>(*count.value));
        for (std::size_t index = 0U; index < static_cast<std::size_t>(*count.value); ++index) {
            const auto length = runtime::read_varuint(field->bytes, offset);
            if (!length.value.has_value()) {
                return runtime::decode_failure<Example>(length.error);
            }
            if (*length.value > 4U || *length.value > field->bytes.size() - offset) {
                return runtime::decode_failure<Example>(
                    *length.value > 4U ? runtime::DecodeError::bounds_exceeded
                                       : runtime::DecodeError::invalid_field_length);
            }
            const auto decoded = runtime::read_bytes(
                field->bytes.subspan(offset, static_cast<std::size_t>(*length.value)));
            elements.push_back(std::move(*decoded.value));
            offset += static_cast<std::size_t>(*length.value);
        }
        if (offset != field->bytes.size()) {
            return runtime::decode_failure<Example>(runtime::DecodeError::invalid_field_length);
        }
        value.blobs = std::move(elements);
    }
    if (const auto* field = runtime::find_field(*parsed.record, 11U); field != nullptr) {
        const auto decoded = decode_Child_result(field->bytes);
        if (!decoded.value.has_value()) {
            return runtime::decode_failure<Example>(decoded.error);
        }
        value.child = *decoded.value;
    }
    if (const auto* field = runtime::find_field(*parsed.record, 12U); field != nullptr) {
        std::size_t offset = 0U;
        const auto count = runtime::read_varuint(field->bytes, offset);
        if (!count.value.has_value()) {
            return runtime::decode_failure<Example>(count.error);
        }
        if (*count.value > 3U) {
            return runtime::decode_failure<Example>(runtime::DecodeError::bounds_exceeded);
        }
        std::vector<Child> elements;
        elements.reserve(static_cast<std::size_t>(*count.value));
        for (std::size_t index = 0U; index < static_cast<std::size_t>(*count.value); ++index) {
            const auto length = runtime::read_varuint(field->bytes, offset);
            if (!length.value.has_value()) {
                return runtime::decode_failure<Example>(length.error);
            }
            if (*length.value > field->bytes.size() - offset) {
                return runtime::decode_failure<Example>(runtime::DecodeError::invalid_field_length);
            }
            const auto decoded = decode_Child_result(
                field->bytes.subspan(offset, static_cast<std::size_t>(*length.value)));
            if (!decoded.value.has_value()) {
                return runtime::decode_failure<Example>(decoded.error);
            }
            elements.push_back(*decoded.value);
            offset += static_cast<std::size_t>(*length.value);
        }
        if (offset != field->bytes.size()) {
            return runtime::decode_failure<Example>(runtime::DecodeError::invalid_field_length);
        }
        value.children = std::move(elements);
    }

    return runtime::decoded_value<Example>(std::move(value));
}

inline std::optional<Example> decode_Example(std::span<const std::byte> input) {
    auto decoded = decode_Example_result(input);
    if (!decoded.value.has_value()) {
        return std::nullopt;
    }
    return std::move(decoded.value);
}

} // namespace quarry::fuzz_generated
