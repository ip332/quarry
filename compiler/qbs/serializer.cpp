#include "compiler/qbs/serializer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

namespace quarry::compiler::qbs {

namespace detail {

bool checked_iss_offset_advance(std::uint64_t identity_size, std::uint32_t current_offset,
                                std::uint32_t& next_offset) {
    constexpr auto kMax = std::numeric_limits<std::uint32_t>::max();
    if (identity_size > static_cast<std::uint64_t>(kMax) - 1U) {
        return false;
    }
    const auto encoded_size = static_cast<std::uint32_t>(identity_size) + 1U;
    if (current_offset > kMax - encoded_size) {
        return false;
    }
    next_offset = current_offset + encoded_size;
    return true;
}

} // namespace detail

namespace {

constexpr std::uint32_t kHeaderSize = 40U;
constexpr std::uint32_t kDirectoryEntrySize = 12U;
constexpr std::uint32_t kRecordDescriptorSize = 28U;
constexpr std::uint32_t kFieldDescriptorSize = 28U;
constexpr std::uint32_t kTypeDescriptorSize = 16U;
constexpr std::uint32_t kEnumDescriptorSize = 16U;
constexpr std::uint8_t kSchemaIdAlgorithmSha256 = 1U;

struct IdentityTable {
    std::vector<std::string> values;
    std::vector<std::uint32_t> record_offsets;
    std::vector<std::uint32_t> enum_offsets;
    std::vector<std::uint8_t> bytes;
    std::uint8_t offset_width = 1U;
};

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id() {
    const auto parsed = diagnostics::DiagnosticId::parse("BC8001");
    assert(parsed.has_value());
    return *parsed;
}

void error(diagnostics::DiagnosticCollection& diagnostics, std::string message) {
    diagnostics.emit(diagnostics::Diagnostic::create(diagnostic_id(), diagnostics::Severity::Error,
                                                     std::move(message))
                         .from_pass("qbs-serializer")
                         .build());
}

[[nodiscard]] bool add32(std::uint32_t lhs, std::uint32_t rhs, std::uint32_t& result) {
    if (rhs > std::numeric_limits<std::uint32_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool multiply32(std::uint32_t lhs, std::uint32_t rhs, std::uint32_t& result) {
    if (lhs != 0U && rhs > std::numeric_limits<std::uint32_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

void put8(std::vector<std::uint8_t>& out, std::uint8_t value) { out.push_back(value); }

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

[[nodiscard]] bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        std::size_t length = 0U;
        std::uint32_t code_point = 0U;
        if (first <= 0x7FU) {
            length = 1U;
            code_point = first;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            length = 2U;
            code_point = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            length = 3U;
            code_point = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            length = 4U;
            code_point = first & 0x07U;
        } else {
            return false;
        }
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t j = 1U; j < length; ++j) {
            const auto continuation = static_cast<unsigned char>(text[i + j]);
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if ((length == 3U && code_point < 0x800U) || (length == 4U && code_point < 0x10000U) ||
            code_point > 0x10FFFFU || (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            return false;
        }
        i += length;
    }
    return true;
}

struct Section {
    std::uint16_t kind = 0U;
    std::vector<std::uint8_t> bytes;
    std::uint32_t offset = 0U;
};

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U, 0x923F82A4U,
    0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU,
    0x9BDC06A7U, 0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU,
    0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU, 0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU,
    0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U, 0xA2BFE8A1U, 0xA81A664BU,
    0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U, 0x19A4C116U,
    0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U, 0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U,
    0xC67178F2U};

[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t value, unsigned amount) {
    return (value >> amount) | (value << (32U - amount));
}

} // namespace

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> padded(input.begin(), input.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(input.size()) * 8U;
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U) {
        padded.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<std::uint8_t>(bit_length >> shift));
    }

    std::array<std::uint32_t, 8> state = {0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
                                          0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};
    for (std::size_t block = 0; block < padded.size(); block += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16U; ++i) {
            const std::size_t at = block + i * 4U;
            words[i] = (static_cast<std::uint32_t>(padded[at]) << 24U) |
                       (static_cast<std::uint32_t>(padded[at + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(padded[at + 2U]) << 8U) |
                       static_cast<std::uint32_t>(padded[at + 3U]);
        }
        for (std::size_t i = 16U; i < words.size(); ++i) {
            const auto s0 =
                rotr(words[i - 15U], 7U) ^ rotr(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
            const auto s1 =
                rotr(words[i - 2U], 17U) ^ rotr(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
            words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
        }
        auto working = state;
        for (std::size_t i = 0; i < words.size(); ++i) {
            const auto s1 = rotr(working[4], 6U) ^ rotr(working[4], 11U) ^ rotr(working[4], 25U);
            const auto choose = (working[4] & working[5]) ^ ((~working[4]) & working[6]);
            const auto temp1 = working[7] + s1 + choose + kSha256RoundConstants[i] + words[i];
            const auto s0 = rotr(working[0], 2U) ^ rotr(working[0], 13U) ^ rotr(working[0], 22U);
            const auto majority =
                (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
            const auto temp2 = s0 + majority;
            working = {temp1 + temp2,      working[0], working[1], working[2],
                       working[3] + temp1, working[4], working[5], working[6]};
        }
        for (std::size_t i = 0; i < state.size(); ++i) {
            state[i] += working[i];
        }
    }

    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0; i < state.size(); ++i) {
        digest[i * 4U] = static_cast<std::uint8_t>(state[i] >> 24U);
        digest[i * 4U + 1U] = static_cast<std::uint8_t>(state[i] >> 16U);
        digest[i * 4U + 2U] = static_cast<std::uint8_t>(state[i] >> 8U);
        digest[i * 4U + 3U] = static_cast<std::uint8_t>(state[i]);
    }
    return digest;
}

namespace {

[[nodiscard]] bool valid_string_index(std::uint16_t index, const QbsImageModel& model) {
    return index == kQbsNoStringIndex || index < model.strings.size();
}

[[nodiscard]] bool valid_type_code(TypeCode code) {
    const auto value = static_cast<std::uint8_t>(code);
    return value >= 1U && value <= 16U;
}

[[nodiscard]] bool valid_storage(Storage storage) {
    switch (storage) {
    case Storage::Fixed:
    case Storage::InlineFixedNestedRecord:
    case Storage::VariableDescriptor:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_descriptor_kind(DescriptorKind kind) {
    switch (kind) {
    case DescriptorKind::None:
    case DescriptorKind::DataOffsetByteLength:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_fqn(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    bool segment_start = true;
    for (const char character : value) {
        if (character == '.') {
            if (segment_start) {
                return false;
            }
            segment_start = true;
        } else if (segment_start) {
            if (!((character >= 'A' && character <= 'Z') ||
                  (character >= 'a' && character <= 'z') || character == '_')) {
                return false;
            }
            segment_start = false;
        } else if (!((character >= 'A' && character <= 'Z') ||
                     (character >= 'a' && character <= 'z') ||
                     (character >= '0' && character <= '9') || character == '_')) {
            return false;
        }
    }
    return !segment_start;
}

void append_type_identity_key(std::vector<std::uint8_t>& key, const QbsImageModel& model,
                              std::size_t index, std::vector<bool>& visiting) {
    const auto& type = model.types[index];
    key.push_back(static_cast<std::uint8_t>(type.code));
    key.push_back(type.fixed_size ? 0U : 1U);
    const auto append32 = [&key](std::uint32_t value) {
        key.push_back(static_cast<std::uint8_t>(value >> 24U));
        key.push_back(static_cast<std::uint8_t>(value >> 16U));
        key.push_back(static_cast<std::uint8_t>(value >> 8U));
        key.push_back(static_cast<std::uint8_t>(value));
    };
    append32(type.encoded_width);
    append32(type.max_elements);
    append32(type.max_bytes);
    std::string_view reference;
    if (type.code == TypeCode::Enum) {
        reference = model.enums[type.reference].fqn;
    } else if (type.code == TypeCode::Record) {
        reference = model.records[type.reference].fqn;
    }
    append32(static_cast<std::uint32_t>(reference.size()));
    key.insert(key.end(), reference.begin(), reference.end());
    if (type.code == TypeCode::Enum) {
        const auto& values = model.enums[type.reference].values;
        append32(static_cast<std::uint32_t>(values.size()));
        for (const auto value : values) {
            for (int shift = 56; shift >= 0; shift -= 8) {
                key.push_back(static_cast<std::uint8_t>(value >> shift));
            }
        }
    } else {
        append32(0U);
    }
    const bool has_element = type.code == TypeCode::Array;
    key.push_back(has_element ? 1U : 0U);
    if (has_element) {
        if (visiting[type.reference]) {
            return;
        }
        visiting[type.reference] = true;
        append_type_identity_key(key, model, type.reference, visiting);
        visiting[type.reference] = false;
    }
}

[[nodiscard]] std::vector<std::uint8_t> type_identity_key(const QbsImageModel& model,
                                                          std::size_t index) {
    std::vector<std::uint8_t> key;
    std::vector<bool> visiting(model.types.size());
    visiting[index] = true;
    append_type_identity_key(key, model, index, visiting);
    return key;
}

[[nodiscard]] bool build_identity_table(const QbsImageModel& model, IdentityTable& table,
                                        diagnostics::DiagnosticCollection& diagnostics) {
    table.values.reserve(model.records.size() + model.enums.size());
    for (const auto& record : model.records) {
        table.values.push_back(record.fqn);
    }
    for (const auto& enumeration : model.enums) {
        table.values.push_back(enumeration.fqn);
    }
    for (const auto& identity : table.values) {
        if (!valid_fqn(identity) || identity.find('\0') != std::string::npos ||
            !valid_utf8(identity)) {
            error(diagnostics, "QBS identity section contains an invalid semantic identity");
            return false;
        }
    }
    std::sort(table.values.begin(), table.values.end());
    if (std::adjacent_find(table.values.begin(), table.values.end()) != table.values.end()) {
        error(diagnostics, "QBS identity section contains duplicate semantic identities");
        return false;
    }
    std::uint32_t offset = 0U;
    for (const auto& identity : table.values) {
        std::uint32_t next_offset = 0U;
        if (!detail::checked_iss_offset_advance(identity.size(), offset, next_offset)) {
            error(diagnostics, "QBS identity section exceeds 32-bit size");
            return false;
        }
        table.bytes.insert(table.bytes.end(), identity.begin(), identity.end());
        table.bytes.push_back(0U);
        offset = next_offset;
    }
    if (table.bytes.size() <= 256U) {
        table.offset_width = 1U;
    } else if (table.bytes.size() <= 65536U) {
        table.offset_width = 2U;
    } else {
        table.offset_width = 4U;
    }
    const auto offset_of = [&table](std::string_view identity) {
        const auto found = std::lower_bound(table.values.begin(), table.values.end(), identity);
        std::uint32_t result = 0U;
        for (auto it = table.values.begin(); it != found; ++it) {
            result += static_cast<std::uint32_t>(it->size() + 1U);
        }
        return result;
    };
    for (const auto& record : model.records) {
        table.record_offsets.push_back(offset_of(record.fqn));
    }
    for (const auto& enumeration : model.enums) {
        table.enum_offsets.push_back(offset_of(enumeration.fqn));
    }
    return true;
}

void put_identity_offset(std::vector<std::uint8_t>& out, std::uint32_t offset, std::uint8_t width) {
    if (width == 1U) {
        put8(out, static_cast<std::uint8_t>(offset));
    } else if (width == 2U) {
        put16(out, static_cast<std::uint16_t>(offset));
    } else {
        put32(out, offset);
    }
}

[[nodiscard]] bool validate_serializer_model(const QbsImageModel& model,
                                             diagnostics::DiagnosticCollection& diagnostics) {
    if (model.format_version != kQbsFormatVersion ||
        model.brf_format_version != kBrfFormatVersion || model.flags != 0U) {
        error(diagnostics, "QBS serializer received unsupported format metadata");
        return false;
    }
    QbsModelBuilder validator;
    if (!validator.validate(model, diagnostics)) {
        return false;
    }
    const auto fits = [](std::size_t size) { return size <= 65536U; };
    if (!fits(model.records.size()) || !fits(model.fields.size()) || !fits(model.types.size()) ||
        !fits(model.enums.size()) || !fits(model.strings.size())) {
        error(diagnostics, "QBS table exceeds 16-bit reference capacity");
        return false;
    }
    for (const auto& type : model.types) {
        if (!valid_type_code(type.code) || type.encoded_width > 0xFFFFU ||
            (type.code == TypeCode::Enum && type.reference >= model.enums.size()) ||
            (type.code == TypeCode::Record && type.reference >= model.records.size()) ||
            (type.code == TypeCode::Array && type.reference >= model.types.size())) {
            error(diagnostics, "QBS type cannot be represented by the v1 serializer");
            return false;
        }
    }
    for (std::size_t index = 1U; index < model.types.size(); ++index) {
        if (type_identity_key(model, index - 1U) >= type_identity_key(model, index)) {
            error(diagnostics, "QBS types are not in canonical order or are duplicated");
            return false;
        }
    }
    for (std::size_t index = 1U; index < model.records.size(); ++index) {
        if (model.records[index - 1U].fqn >= model.records[index].fqn) {
            error(diagnostics, "QBS records are not in canonical order");
            return false;
        }
    }
    for (std::size_t index = 1U; index < model.enums.size(); ++index) {
        if (model.enums[index - 1U].fqn >= model.enums[index].fqn) {
            error(diagnostics, "QBS enums are not in canonical order");
            return false;
        }
    }
    for (const auto& record : model.records) {
        if (!valid_string_index(record.name_string_index, model)) {
            error(diagnostics, "QBS record has an invalid name reference");
            return false;
        }
    }
    for (const auto& field : model.fields) {
        if (!valid_storage(field.storage) || !valid_descriptor_kind(field.descriptor_kind)) {
            error(diagnostics, "QBS field contains an unsupported enum value");
            return false;
        }
        if (!valid_string_index(field.name_string_index, model)) {
            error(diagnostics, "QBS field has an invalid name or bit reference");
            return false;
        }
    }
    for (const auto& enumeration : model.enums) {
        if (enumeration.encoded_width > 0xFFFFU ||
            !valid_string_index(enumeration.name_string_index, model)) {
            error(diagnostics, "QBS enum cannot be represented by the v1 serializer");
            return false;
        }
    }
    if (model.mode == BuildMode::Minimal && !model.strings.empty()) {
        error(diagnostics, "minimal QBS serializer input contains strings");
        return false;
    }
    if (model.mode == BuildMode::Reflective && model.strings.size() > 65535U) {
        error(diagnostics, "QBS string table exceeds 16-bit reference capacity");
        return false;
    }
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> record_section(const QbsImageModel& model,
                                                       const IdentityTable& identities) {
    std::vector<std::uint8_t> out;
    out.reserve(model.records.size() * (kRecordDescriptorSize + identities.offset_width));
    for (std::size_t index = 0; index < model.records.size(); ++index) {
        const auto& record = model.records[index];
        put32(out, record.record_id);
        put32(out, record.field_start);
        put16(out, record.field_count);
        put16(out, record.variable_size ? 1U : 0U);
        put32(out, record.presence_bitmap_size);
        put32(out, record.fixed_region_size);
        put32(out, record.complete_fixed_record_size.value_or(0U));
        put_identity_offset(out, identities.record_offsets[index], identities.offset_width);
        put16(out, record.name_string_index);
        put16(out, 0U);
    }
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> field_section(const QbsImageModel& model) {
    std::vector<std::uint8_t> out;
    out.reserve(model.fields.size() * kFieldDescriptorSize);
    for (const auto& field : model.fields) {
        std::uint16_t flags = static_cast<std::uint16_t>(field.storage);
        if (field.descriptor_kind == DescriptorKind::DataOffsetByteLength) {
            flags = static_cast<std::uint16_t>(flags | 0x0004U);
        }
        put16(out, field.field_index);
        put16(out, flags);
        put32(out, field.byte_offset);
        put16(out, field.bit_offset);
        put32(out, field.bit_width);
        put16(out, field.type_index);
        put16(out, field.presence_bit_index);
        put16(out, 0U);
        put32(out, field.slot_size);
        put16(out, field.name_string_index);
        put16(out, 0U);
    }
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> type_section(const QbsImageModel& model) {
    std::vector<std::uint8_t> out;
    out.reserve(model.types.size() * kTypeDescriptorSize);
    for (const auto& type : model.types) {
        put8(out, static_cast<std::uint8_t>(type.code));
        put8(out, type.fixed_size ? 0x01U : 0x02U);
        put16(out, static_cast<std::uint16_t>(type.encoded_width));
        put16(out, type.reference);
        put16(out, 0U);
        put32(out, type.max_elements);
        put32(out, type.max_bytes);
    }
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> enum_section(const QbsImageModel& model,
                                                     const IdentityTable& identities) {
    std::vector<std::uint8_t> out;
    out.reserve(model.enums.size() * (kEnumDescriptorSize + identities.offset_width));
    for (std::size_t index = 0; index < model.enums.size(); ++index) {
        const auto& enumeration = model.enums[index];
        put16(out, static_cast<std::uint16_t>(enumeration.encoded_width));
        put16(out, 0U);
        put32(out, enumeration.value_start);
        put32(out, static_cast<std::uint32_t>(enumeration.values.size()));
        put_identity_offset(out, identities.enum_offsets[index], identities.offset_width);
        put16(out, enumeration.name_string_index);
        put16(out, 0U);
    }
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> enum_values_section(const QbsImageModel& model) {
    std::vector<std::uint8_t> out;
    out.reserve(model.enum_values.size() * 8U);
    for (const auto value : model.enum_values) {
        put64(out, value);
    }
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> identity_section(const IdentityTable& identities) {
    return identities.bytes;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
string_section(const QbsImageModel& model, diagnostics::DiagnosticCollection& diagnostics) {
    if (model.strings.empty()) {
        return std::nullopt;
    }
    std::uint32_t data_size = 0U;
    for (std::size_t i = 0; i < model.strings.size(); ++i) {
        const auto& string = model.strings[i];
        if (!valid_utf8(string)) {
            error(diagnostics, "QBS string table contains invalid UTF-8");
            return std::nullopt;
        }
        if (string.size() > std::numeric_limits<std::uint32_t>::max()) {
            error(diagnostics, "QBS string exceeds 32-bit size");
            return std::nullopt;
        }
        if (i != 0U && model.strings[i - 1U] >= string) {
            error(diagnostics, "QBS strings are not strictly canonical and deduplicated");
            return std::nullopt;
        }
        if (!add32(data_size, static_cast<std::uint32_t>(string.size()), data_size)) {
            error(diagnostics, "QBS string data exceeds 32-bit size");
            return std::nullopt;
        }
    }
    std::uint32_t count = static_cast<std::uint32_t>(model.strings.size());
    std::uint32_t offset_count = 0U;
    std::uint32_t offset_bytes = 0U;
    if (!add32(count, 1U, offset_count) || !multiply32(offset_count, 4U, offset_bytes)) {
        error(diagnostics, "QBS string offset table overflows 32 bits");
        return std::nullopt;
    }
    std::uint32_t size = 4U;
    if (!add32(size, offset_bytes, size) || !add32(size, data_size, size)) {
        error(diagnostics, "QBS string section overflows 32 bits");
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(size);
    put32(out, count);
    std::uint32_t offset = 0U;
    put32(out, offset);
    for (const auto& string : model.strings) {
        offset += static_cast<std::uint32_t>(string.size());
        put32(out, offset);
    }
    for (const auto& string : model.strings) {
        out.insert(out.end(), string.begin(), string.end());
    }
    return out;
}

} // namespace

std::optional<QbsSerializeResult> serialize_qbs(const QbsImageModel& model,
                                                diagnostics::DiagnosticCollection& diagnostics) {
    if (!validate_serializer_model(model, diagnostics)) {
        return std::nullopt;
    }
    IdentityTable identities;
    if (!build_identity_table(model, identities, diagnostics)) {
        return std::nullopt;
    }
    std::vector<Section> sections;
    sections.push_back(Section{1U, record_section(model, identities)});
    sections.push_back(Section{2U, field_section(model)});
    sections.push_back(Section{3U, type_section(model)});
    if (!model.enums.empty()) {
        sections.push_back(Section{4U, enum_section(model, identities)});
        sections.push_back(Section{5U, enum_values_section(model)});
    }
    sections.push_back(Section{6U, identity_section(identities)});
    if (model.mode == BuildMode::Reflective && !model.strings.empty()) {
        const auto strings = string_section(model, diagnostics);
        if (!strings.has_value()) {
            return std::nullopt;
        }
        sections.push_back(Section{7U, *strings});
    }
    if (sections.size() > std::numeric_limits<std::uint16_t>::max()) {
        error(diagnostics, "QBS section count exceeds 16-bit capacity");
        return std::nullopt;
    }
    std::uint32_t directory_size = 0U;
    if (!multiply32(static_cast<std::uint32_t>(sections.size()), kDirectoryEntrySize,
                    directory_size)) {
        error(diagnostics, "QBS section directory overflows 32 bits");
        return std::nullopt;
    }
    std::uint32_t cursor = 0U;
    if (!add32(kHeaderSize, directory_size, cursor)) {
        error(diagnostics, "QBS image header and directory overflow 32 bits");
        return std::nullopt;
    }
    for (auto& section : sections) {
        if (section.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            error(diagnostics, "QBS section exceeds 32-bit size");
            return std::nullopt;
        }
        section.offset = cursor;
        if (!add32(cursor, static_cast<std::uint32_t>(section.bytes.size()), cursor)) {
            error(diagnostics, "QBS image exceeds 32-bit size");
            return std::nullopt;
        }
    }

    const auto digest = sha256(model.schema_identity_input);
    std::vector<std::uint8_t> out;
    out.reserve(cursor);
    out.insert(out.end(), {'Q', 'B', 'S', '\0'});
    put8(out, model.format_version);
    put8(out, model.flags);
    put16(out, kHeaderSize);
    put8(out, kSchemaIdAlgorithmSha256);
    put8(out, identities.offset_width);
    put16(out, 16U);
    out.insert(out.end(), digest.begin(), digest.begin() + 16);
    put16(out, static_cast<std::uint16_t>(sections.size()));
    put16(out, 0U);
    put32(out, kHeaderSize);
    put32(out, cursor);
    for (const auto& section : sections) {
        put16(out, section.kind);
        put16(out, 0U);
        put32(out, section.offset);
        put32(out, static_cast<std::uint32_t>(section.bytes.size()));
    }
    for (const auto& section : sections) {
        out.insert(out.end(), section.bytes.begin(), section.bytes.end());
    }
    assert(out.size() == cursor);
    return QbsSerializeResult{.bytes = std::move(out)};
}

} // namespace quarry::compiler::qbs
