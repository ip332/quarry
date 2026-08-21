#include "compiler/qbs/parser.hpp"

#include "compiler/qbs/serializer.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace quarry::compiler::qbs {
namespace {

constexpr std::uint32_t kHeaderSize = 40U;
constexpr std::uint32_t kFieldStride = 28U;
constexpr std::uint32_t kTypeStride = 16U;
constexpr std::uint16_t kNoString = 0xFFFFU;

struct Section {
    std::uint16_t kind = 0U;
    std::uint16_t flags = 0U;
    std::uint32_t offset = 0U;
    std::uint32_t size = 0U;
};

[[nodiscard]] bool add32(std::uint32_t a, std::uint32_t b, std::uint32_t& out) {
    if (b > std::numeric_limits<std::uint32_t>::max() - a) {
        return false;
    }
    out = a + b;
    return true;
}

[[nodiscard]] bool mul32(std::uint32_t a, std::uint32_t b, std::uint32_t& out) {
    if (a != 0U && b > std::numeric_limits<std::uint32_t>::max() / a) {
        return false;
    }
    out = a * b;
    return true;
}

[[nodiscard]] std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[at]) << 8U) |
                                      bytes[at + 1U]);
}

[[nodiscard]] std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return (static_cast<std::uint32_t>(bytes[at]) << 24U) |
           (static_cast<std::uint32_t>(bytes[at + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 2U]) << 8U) | bytes[at + 3U];
}

[[nodiscard]] std::uint64_t u64(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint64_t value = 0U;
    for (std::size_t i = 0; i < 8U; ++i) {
        value = (value << 8U) | bytes[at + i];
    }
    return value;
}

[[nodiscard]] bool utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        std::size_t length = 0U;
        std::uint32_t code = 0U;
        if (first <= 0x7FU) {
            length = 1U;
            code = first;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            length = 2U;
            code = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            length = 3U;
            code = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            length = 4U;
            code = first & 0x07U;
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
            code = (code << 6U) | (continuation & 0x3FU);
        }
        if ((length == 3U && code < 0x800U) || (length == 4U && code < 0x10000U) ||
            code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU)) {
            return false;
        }
        i += length;
    }
    return true;
}

[[nodiscard]] bool fqn(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    bool start = true;
    for (const char c : value) {
        if (c == '.') {
            if (start)
                return false;
            start = true;
        } else if (start) {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'))
                return false;
            start = false;
        } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                     c == '_')) {
            return false;
        }
    }
    return !start;
}

bool fail(diagnostics::DiagnosticCollection& diagnostics, std::string_view message) {
    const auto id = diagnostics::DiagnosticId::parse("BC8001");
    if (id.has_value()) {
        diagnostics.emit(
            diagnostics::Diagnostic::create(*id, diagnostics::Severity::Error, std::string(message))
                .from_pass("qbs-parser")
                .build());
    }
    return false;
}

[[nodiscard]] std::uint32_t identity_offset(std::span<const std::uint8_t> bytes, std::size_t at,
                                            std::uint8_t width) {
    if (width == 1U)
        return bytes[at];
    if (width == 2U)
        return u16(bytes, at);
    return u32(bytes, at);
}

[[nodiscard]] std::uint8_t canonical_width(std::uint32_t size) {
    return size <= 256U ? 1U : size <= 65536U ? 2U : 4U;
}

} // namespace

QbsRecordView ValidatedQbsView::record(std::size_t index) const {
    if (index >= record_count_) {
        return {};
    }
    const auto at = record_offset_ + index * record_stride_;
    return QbsRecordView{
        u32(bytes_, at),
        u32(bytes_, at + 4U),
        u16(bytes_, at + 8U),
        (u16(bytes_, at + 10U) & 1U) != 0U,
        u32(bytes_, at + 12U),
        u32(bytes_, at + 16U),
        u32(bytes_, at + 20U),
        identity_at_offset(identity_offset(bytes_, at + 24U, header_.identity_offset_width)),
        string(u16(bytes_, at + 24U + header_.identity_offset_width))};
}

QbsFieldView ValidatedQbsView::field(std::size_t index) const {
    if (index >= field_count_) {
        return {};
    }
    const auto at = field_offset_ + index * kFieldStride;
    const auto flags = u16(bytes_, at + 2U);
    return QbsFieldView{u16(bytes_, at),
                        u16(bytes_, at + 14U),
                        u32(bytes_, at + 4U),
                        u16(bytes_, at + 8U),
                        u32(bytes_, at + 10U),
                        u16(bytes_, at + 16U),
                        u32(bytes_, at + 20U),
                        static_cast<std::uint8_t>(flags & 3U),
                        static_cast<std::uint8_t>((flags >> 2U) & 1U),
                        string(u16(bytes_, at + 24U))};
}

QbsTypeView ValidatedQbsView::type(std::size_t index) const {
    if (index >= type_count_) {
        return {};
    }
    const auto at = type_offset_ + index * kTypeStride;
    const auto flags = bytes_[at + 1U];
    return QbsTypeView{bytes_[at],           (flags & 1U) != 0U,   u16(bytes_, at + 2U),
                       u16(bytes_, at + 4U), u32(bytes_, at + 8U), u32(bytes_, at + 12U)};
}

QbsEnumView ValidatedQbsView::enum_type(std::size_t index) const {
    if (index >= enum_count_) {
        return {};
    }
    const auto at = enum_offset_ + index * enum_stride_;
    const auto start = u32(bytes_, at + 4U);
    const auto count = u32(bytes_, at + 8U);
    std::vector<std::uint64_t> values;
    values.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
        values.push_back(u64(bytes_, enum_values_offset_ + (start + i) * 8U));
    }
    // This view is intentionally low-copy for all metadata; enum values are bounded and copied
    // only because the wire representation is big-endian rather than native uint64_t storage.
    return QbsEnumView{
        u16(bytes_, at),
        identity_at_offset(identity_offset(bytes_, at + 12U, header_.identity_offset_width)),
        string(u16(bytes_, at + 12U + header_.identity_offset_width)), std::move(values)};
}

std::string_view ValidatedQbsView::identity_at_offset(std::uint32_t offset) const {
    if (offset >= iss_size_) {
        return {};
    }
    const auto begin = iss_offset_ + offset;
    std::size_t end = begin;
    while (end < iss_offset_ + iss_size_ && bytes_[end] != 0U)
        ++end;
    return {reinterpret_cast<const char*>(bytes_.data() + begin), end - begin};
}

std::string_view ValidatedQbsView::string(std::size_t index) const {
    if (index == kNoString)
        return {};
    const auto count = u32(bytes_, strings_offset_);
    if (index >= count) {
        return {};
    }
    const auto start = u32(bytes_, strings_offset_ + 4U + index * 4U);
    const auto end = u32(bytes_, strings_offset_ + 4U + (index + 1U) * 4U);
    const auto data = strings_offset_ + 4U + (count + 1U) * 4U;
    return {reinterpret_cast<const char*>(bytes_.data() + data + start), end - start};
}

std::optional<ValidatedQbsView> parse_qbs(std::span<const std::uint8_t> bytes,
                                          diagnostics::DiagnosticCollection& diagnostics,
                                          QbsParserLimits limits) {
    if (bytes.size() > limits.max_image_size || bytes.size() < kHeaderSize) {
        fail(diagnostics, "QBS image is truncated or exceeds the configured limit");
        return std::nullopt;
    }
    if (!(bytes[0] == 'Q' && bytes[1] == 'B' && bytes[2] == 'S' && bytes[3] == 0U) ||
        bytes[4] != 1U || bytes[5] != 0U || u16(bytes, 6U) != 40U || bytes[8] != 1U ||
        (bytes[9] != 1U && bytes[9] != 2U && bytes[9] != 4U) || bytes[10] != 0U ||
        bytes[11] != 16U || u16(bytes, 30U) != 0U) {
        fail(diagnostics, "QBS header is invalid");
        return std::nullopt;
    }
    const auto total = u32(bytes, 36U);
    const auto section_count = u16(bytes, 28U);
    const auto directory_end = static_cast<std::uint64_t>(40U) + section_count * 12ULL;
    if (total != bytes.size() || total < directory_end || section_count > limits.max_sections) {
        fail(diagnostics, "QBS image size or section directory is invalid");
        return std::nullopt;
    }
    std::vector<Section> sections;
    sections.reserve(section_count);
    std::uint16_t previous_kind = 0U;
    for (std::uint16_t i = 0U; i < section_count; ++i) {
        const auto at = 40U + i * 12U;
        Section section{u16(bytes, at), u16(bytes, at + 2U), u32(bytes, at + 4U),
                        u32(bytes, at + 8U)};
        if (section.kind <= previous_kind || section.flags != 0U ||
            section.offset < directory_end ||
            !add32(section.offset, section.size, section.offset) || section.offset > total) {
            fail(diagnostics, "QBS section directory is invalid");
            return std::nullopt;
        }
        section.offset = u32(bytes, at + 4U);
        if (section.kind > 7U || section.kind == 0U) {
            fail(diagnostics, "QBS contains an unsupported section");
            return std::nullopt;
        }
        if (!sections.empty()) {
            const auto previous_end =
                static_cast<std::uint64_t>(sections.back().offset) + sections.back().size;
            if (section.offset < previous_end) {
                fail(diagnostics, "QBS sections overlap");
                return std::nullopt;
            }
        }
        sections.push_back(section);
        previous_kind = section.kind;
    }
    auto find = [&sections](std::uint16_t kind) -> const Section* {
        const auto it = std::find_if(sections.begin(), sections.end(),
                                     [kind](const Section& s) { return s.kind == kind; });
        return it == sections.end() ? nullptr : &*it;
    };
    const auto records = find(1U);
    const auto fields = find(2U);
    const auto types = find(3U);
    const auto enums = find(4U);
    const auto enum_values = find(5U);
    const auto iss = find(6U);
    const auto strings = find(7U);
    if (records == nullptr || fields == nullptr || types == nullptr || iss == nullptr ||
        (enums == nullptr) != (enum_values == nullptr)) {
        fail(diagnostics, "QBS is missing required sections");
        return std::nullopt;
    }
    const auto width = bytes[9];
    if (canonical_width(iss->size) != width) {
        fail(diagnostics, "QBS identity offset width is not canonical");
        return std::nullopt;
    }
    if (iss->size == 0U && (records->size != 0U || (enums != nullptr && enums->size != 0U))) {
        fail(diagnostics, "QBS has an empty identity section for a non-empty schema");
        return std::nullopt;
    }
    std::set<std::uint32_t> identity_starts;
    std::string_view previous_identity;
    for (std::uint32_t cursor = 0U; cursor < iss->size;) {
        const auto start = cursor;
        while (cursor < iss->size && bytes[iss->offset + cursor] != 0U)
            ++cursor;
        if (cursor == start || cursor == iss->size) {
            fail(diagnostics, "QBS identity section contains an invalid string");
            return std::nullopt;
        }
        const std::string_view identity(
            reinterpret_cast<const char*>(bytes.data() + iss->offset + start), cursor - start);
        if (!utf8(identity) || !fqn(identity) ||
            (!previous_identity.empty() && identity <= previous_identity)) {
            fail(diagnostics, "QBS identity section is not canonical");
            return std::nullopt;
        }
        identity_starts.insert(start);
        previous_identity = identity;
        ++cursor;
    }
    if ((records->size % (28U + width)) != 0U || (fields->size % kFieldStride) != 0U ||
        (types->size % kTypeStride) != 0U ||
        (enums != nullptr && enums->size % (16U + width) != 0U) ||
        (enum_values != nullptr && enum_values->size % 8U != 0U)) {
        fail(diagnostics, "QBS table size is inconsistent with its descriptor width");
        return std::nullopt;
    }
    const auto record_count = records->size / (28U + width);
    const auto field_count = fields->size / kFieldStride;
    const auto type_count = types->size / kTypeStride;
    const auto enum_count = enums == nullptr ? 0U : enums->size / (16U + width);
    if (record_count > limits.max_records || field_count > limits.max_fields ||
        type_count > limits.max_types || enum_count > limits.max_enums ||
        (enum_values != nullptr && enum_values->size / 8U > limits.max_enum_values)) {
        fail(diagnostics, "QBS table exceeds parser limits");
        return std::nullopt;
    }
    if (strings != nullptr) {
        if (strings->size < 4U) {
            fail(diagnostics, "QBS string section is truncated");
            return std::nullopt;
        }
        const auto count = u32(bytes, strings->offset);
        std::uint32_t offset_bytes = 0U;
        if (count > limits.max_strings || count == std::numeric_limits<std::uint32_t>::max() ||
            !mul32(count + 1U, 4U, offset_bytes) || offset_bytes > strings->size - 4U) {
            fail(diagnostics, "QBS string offset table is invalid");
            return std::nullopt;
        }
        const auto data = strings->offset + 4U + offset_bytes;
        const auto data_size = strings->size - 4U - offset_bytes;
        if (u32(bytes, strings->offset + 4U) != 0U ||
            u32(bytes, strings->offset + 4U + count * 4U) != data_size) {
            fail(diagnostics, "QBS string offsets are invalid");
            return std::nullopt;
        }
        std::string_view previous;
        for (std::uint32_t i = 0U; i < count; ++i) {
            const auto begin = u32(bytes, strings->offset + 4U + i * 4U);
            const auto end = u32(bytes, strings->offset + 4U + (i + 1U) * 4U);
            if (begin > end || end > data_size) {
                fail(diagnostics, "QBS string range is invalid");
                return std::nullopt;
            }
            const std::string_view value(reinterpret_cast<const char*>(bytes.data() + data + begin),
                                         end - begin);
            if (!utf8(value) || (!previous.empty() && value <= previous)) {
                fail(diagnostics, "QBS strings are not canonical");
                return std::nullopt;
            }
            previous = value;
        }
    }
    auto identity = [&](std::uint32_t offset) -> std::string_view {
        if (!identity_starts.contains(offset))
            return {};
        const auto begin = iss->offset + offset;
        std::size_t end = begin;
        while (end < iss->offset + iss->size && bytes[end] != 0U)
            ++end;
        return {reinterpret_cast<const char*>(bytes.data() + begin), end - begin};
    };
    auto name_valid = [&](std::uint16_t index) {
        return index == kNoString || (strings != nullptr && index < u32(bytes, strings->offset));
    };
    for (std::uint32_t i = 0U; i < record_count; ++i) {
        const auto at = records->offset + i * (28U + width);
        const auto flags = u16(bytes, at + 10U);
        const auto id = identity(identity_offset(bytes, at + 24U, width));
        std::uint32_t field_end = 0U;
        if (id.empty() || (flags & ~1U) != 0U || u32(bytes, at + 4U) > field_count ||
            !add32(u32(bytes, at + 4U), u16(bytes, at + 8U), field_end) ||
            field_end > field_count || !name_valid(u16(bytes, at + 24U + width))) {
            fail(diagnostics, "QBS record descriptor is invalid");
            return std::nullopt;
        }
        if (i != 0U) {
            const auto previous = identity(identity_offset(bytes, at - (28U + width) + 24U, width));
            if (previous >= id) {
                fail(diagnostics, "QBS records are not canonical");
                return std::nullopt;
            }
        }
        if (((flags & 1U) == 0U && u32(bytes, at + 20U) != 16U + u32(bytes, at + 16U)) ||
            ((flags & 1U) != 0U && u32(bytes, at + 20U) != 0U)) {
            fail(diagnostics, "QBS record classification is inconsistent");
            return std::nullopt;
        }
    }
    for (std::uint32_t i = 0U; i < enum_count; ++i) {
        const auto at = enums->offset + i * (16U + width);
        const auto id = identity(identity_offset(bytes, at + 12U, width));
        if (id.empty() || u16(bytes, at + 2U) != 0U || !name_valid(u16(bytes, at + 12U + width))) {
            fail(diagnostics, "QBS enum descriptor is invalid");
            return std::nullopt;
        }
        if (i != 0U) {
            const auto previous = identity(identity_offset(bytes, at - (16U + width) + 12U, width));
            if (previous >= id) {
                fail(diagnostics, "QBS enums are not canonical");
                return std::nullopt;
            }
        }
        const auto value_start = u32(bytes, at + 4U);
        const auto value_count = u32(bytes, at + 8U);
        if (enum_values == nullptr || value_start > enum_values->size / 8U ||
            value_count > enum_values->size / 8U - value_start || u16(bytes, at) == 0U ||
            u16(bytes, at) > 8U) {
            fail(diagnostics, "QBS enum values are invalid");
            return std::nullopt;
        }
        const auto width_bits = static_cast<unsigned>(u16(bytes, at)) * 8U;
        for (std::uint32_t j = 0U; j < value_count; ++j) {
            const auto value = u64(bytes, enum_values->offset + (value_start + j) * 8U);
            if (j != 0U && value <= u64(bytes, enum_values->offset + (value_start + j - 1U) * 8U)) {
                fail(diagnostics, "QBS enum values are not canonical");
                return std::nullopt;
            }
            if (width_bits < 64U && value >= (std::uint64_t{1} << width_bits)) {
                fail(diagnostics, "QBS enum value exceeds its width");
                return std::nullopt;
            }
        }
    }
    for (std::uint32_t i = 0U; i < type_count; ++i) {
        const auto at = types->offset + i * kTypeStride;
        const auto code = bytes[at];
        const auto flags = bytes[at + 1U];
        if (code == 0U || code > 16U || (flags != 1U && flags != 2U) || u16(bytes, at + 6U) != 0U) {
            fail(diagnostics, "QBS type descriptor is invalid");
            return std::nullopt;
        }
        const auto reference = u16(bytes, at + 4U);
        if ((code == 12U && reference >= enum_count) ||
            (code == 15U && reference >= record_count) ||
            (code == 16U && reference >= type_count) ||
            (code != 12U && code != 15U && code != 16U && reference != 0U) ||
            (code == 16U && u32(bytes, at + 8U) == 0U)) {
            fail(diagnostics, "QBS type reference is invalid");
            return std::nullopt;
        }
    }
    std::vector<std::vector<std::uint8_t>> type_keys(type_count);
    for (std::uint32_t root = 0U; root < type_count; ++root) {
        struct Frame {
            std::uint16_t index;
            bool exit;
        };
        std::vector<Frame> work{{static_cast<std::uint16_t>(root), false}};
        std::vector<std::uint8_t> state(type_count, 0U);
        std::vector<std::vector<std::uint8_t>> keys(type_count);
        while (!work.empty()) {
            const auto frame = work.back();
            work.pop_back();
            if (frame.exit) {
                const auto at = types->offset + frame.index * kTypeStride;
                if (bytes[at] == 16U) {
                    const auto child = u16(bytes, at + 4U);
                    keys[frame.index].insert(keys[frame.index].end(), keys[child].begin(),
                                             keys[child].end());
                }
                state[frame.index] = 2U;
                continue;
            }
            if (state[frame.index] == 1U) {
                fail(diagnostics, "QBS type graph contains a cycle");
                return std::nullopt;
            }
            if (state[frame.index] == 2U) {
                continue;
            }
            state[frame.index] = 1U;
            const auto at = types->offset + frame.index * kTypeStride;
            const auto code = bytes[at];
            auto& key = keys[frame.index];
            const auto append32 = [&key](std::uint32_t value) {
                key.push_back(static_cast<std::uint8_t>(value >> 24U));
                key.push_back(static_cast<std::uint8_t>(value >> 16U));
                key.push_back(static_cast<std::uint8_t>(value >> 8U));
                key.push_back(static_cast<std::uint8_t>(value));
            };
            key.push_back(code);
            key.push_back(bytes[at + 1U] == 1U ? 0U : 1U);
            append32(u16(bytes, at + 2U));
            append32(u32(bytes, at + 8U));
            append32(u32(bytes, at + 12U));
            std::string_view reference;
            const auto ref = u16(bytes, at + 4U);
            if (code == 12U) {
                const auto enum_at = enums->offset + ref * (16U + width);
                reference = identity(identity_offset(bytes, enum_at + 12U, width));
            } else if (code == 15U) {
                const auto record_at = records->offset + ref * (28U + width);
                reference = identity(identity_offset(bytes, record_at + 24U, width));
            }
            append32(static_cast<std::uint32_t>(reference.size()));
            key.insert(key.end(), reference.begin(), reference.end());
            if (code == 12U) {
                const auto enum_at = enums->offset + ref * (16U + width);
                const auto start = u32(bytes, enum_at + 4U);
                const auto count = u32(bytes, enum_at + 8U);
                append32(count);
                for (std::uint32_t j = 0U; j < count; ++j) {
                    const auto value = u64(bytes, enum_values->offset + (start + j) * 8U);
                    for (int shift = 56; shift >= 0; shift -= 8)
                        key.push_back(value >> shift);
                }
            } else {
                append32(0U);
            }
            const bool array = code == 16U;
            key.push_back(array ? 1U : 0U);
            if (array) {
                work.push_back(Frame{frame.index, true});
                const auto child = u16(bytes, at + 4U);
                if (state[child] == 1U) {
                    fail(diagnostics, "QBS type graph contains a cycle");
                    return std::nullopt;
                }
                work.push_back(Frame{child, false});
            } else {
                state[frame.index] = 2U;
            }
        }
        type_keys[root] = std::move(keys[root]);
    }
    for (std::uint32_t i = 1U; i < type_count; ++i) {
        if (type_keys[i - 1U] >= type_keys[i]) {
            fail(diagnostics, "QBS types are not in canonical order");
            return std::nullopt;
        }
    }
    for (std::uint32_t i = 0U; i < field_count; ++i) {
        const auto at = fields->offset + i * kFieldStride;
        const auto flags = u16(bytes, at + 2U);
        if ((flags & 0xFFF8U) != 0U || (flags & 3U) > 2U || u16(bytes, at + 14U) >= type_count ||
            u16(bytes, at + 16U) >= field_count || u16(bytes, at + 8U) > 7U ||
            u32(bytes, at + 10U) == 0U || u16(bytes, at + 18U) != 0U ||
            !name_valid(u16(bytes, at + 24U))) {
            fail(diagnostics, "QBS field descriptor is invalid");
            return std::nullopt;
        }
    }
    for (std::uint32_t record_index = 0U; record_index < record_count; ++record_index) {
        const auto record_at = records->offset + record_index * (28U + width);
        const auto start = u32(bytes, record_at + 4U);
        const auto count = u16(bytes, record_at + 8U);
        for (std::uint32_t j = 0U; j < count; ++j) {
            const auto field_at = fields->offset + (start + j) * kFieldStride;
            if (u16(bytes, field_at) != j) {
                fail(diagnostics, "QBS fields are not canonical within their record");
                return std::nullopt;
            }
            const auto flags = u16(bytes, field_at + 2U);
            const auto type_at = types->offset + u16(bytes, field_at + 14U) * kTypeStride;
            const bool fixed = bytes[type_at + 1U] == 1U;
            const auto storage = flags & 3U;
            const auto descriptor = (flags >> 2U) & 1U;
            if ((storage == 0U && !fixed) || (storage == 1U && (bytes[type_at] != 15U || !fixed)) ||
                (storage == 2U && fixed) || (storage == 2U && descriptor != 1U) ||
                (storage != 2U && descriptor != 0U) ||
                (storage == 2U && u32(bytes, field_at + 20U) != 8U)) {
                fail(diagnostics, "QBS field storage is inconsistent with its type");
                return std::nullopt;
            }
            const auto presence_bits = static_cast<std::uint64_t>(u32(bytes, record_at + 12U)) * 8U;
            const auto fixed_end = static_cast<std::uint64_t>(16U) + u32(bytes, record_at + 16U);
            const auto field_end =
                static_cast<std::uint64_t>(u32(bytes, field_at + 4U)) + u32(bytes, field_at + 20U);
            if (u32(bytes, field_at + 4U) < 16U + u32(bytes, record_at + 12U) ||
                field_end > fixed_end || u16(bytes, field_at + 16U) >= presence_bits ||
                u16(bytes, field_at + 8U) > 7U || u32(bytes, field_at + 10U) == 0U) {
                fail(diagnostics, "QBS field location is outside its record");
                return std::nullopt;
            }
        }
    }
    std::vector<std::uint8_t> identity_input;
    const auto append_string = [&identity_input](std::string_view value) {
        const auto size = static_cast<std::uint32_t>(value.size());
        identity_input.push_back(static_cast<std::uint8_t>(size >> 24U));
        identity_input.push_back(static_cast<std::uint8_t>(size >> 16U));
        identity_input.push_back(static_cast<std::uint8_t>(size >> 8U));
        identity_input.push_back(static_cast<std::uint8_t>(size));
        identity_input.insert(identity_input.end(), value.begin(), value.end());
    };
    const auto append8 = [&identity_input](std::uint8_t value) { identity_input.push_back(value); };
    const auto append32 = [&identity_input](std::uint32_t value) {
        identity_input.push_back(static_cast<std::uint8_t>(value >> 24U));
        identity_input.push_back(static_cast<std::uint8_t>(value >> 16U));
        identity_input.push_back(static_cast<std::uint8_t>(value >> 8U));
        identity_input.push_back(static_cast<std::uint8_t>(value));
    };
    append_string("quarry.qbs.schema");
    append8(2U);
    for (std::uint32_t i = 0U; i < record_count; ++i) {
        const auto record_at = records->offset + i * (28U + width);
        append_string(identity(identity_offset(bytes, record_at + 24U, width)));
        append32(u32(bytes, record_at));
        const auto record_flags = u16(bytes, record_at + 10U);
        append8((record_flags & 1U) != 0U ? 1U : 0U);
        append32(16U);
        append32(u32(bytes, record_at + 12U));
        append32(u32(bytes, record_at + 16U));
        append32(u32(bytes, record_at + 20U));
        append32(u16(bytes, record_at + 8U));
        const auto start = u32(bytes, record_at + 4U);
        const auto count = u16(bytes, record_at + 8U);
        for (std::uint32_t j = 0U; j < count; ++j) {
            const auto field_at = fields->offset + (start + j) * kFieldStride;
            append32(u16(bytes, field_at));
            append32(u16(bytes, field_at + 16U));
            append32(u32(bytes, field_at + 4U));
            append8(static_cast<std::uint8_t>(u16(bytes, field_at + 8U)));
            append32(u32(bytes, field_at + 10U));
            append32(u32(bytes, field_at + 20U));
            append8(static_cast<std::uint8_t>(u16(bytes, field_at + 2U) & 3U));
            append8(static_cast<std::uint8_t>((u16(bytes, field_at + 2U) >> 2U) & 1U));
            const auto type_index = u16(bytes, field_at + 14U);
            identity_input.insert(identity_input.end(), type_keys[type_index].begin(),
                                  type_keys[type_index].end());
        }
    }
    append32(enum_count);
    for (std::uint32_t i = 0U; i < enum_count; ++i) {
        const auto enum_at = enums->offset + i * (16U + width);
        append_string(identity(identity_offset(bytes, enum_at + 12U, width)));
        append32(u16(bytes, enum_at));
        const auto start = u32(bytes, enum_at + 4U);
        const auto count = u32(bytes, enum_at + 8U);
        append32(count);
        for (std::uint32_t j = 0U; j < count; ++j) {
            const auto value = u64(bytes, enum_values->offset + (start + j) * 8U);
            for (int shift = 56; shift >= 0; shift -= 8)
                identity_input.push_back(value >> shift);
        }
    }
    const auto digest = sha256(identity_input);
    if (!std::equal(digest.begin(), digest.begin() + 16, bytes.begin() + 12U)) {
        fail(diagnostics, "QBS schema ID does not match structural content");
        return std::nullopt;
    }
    if (strings != nullptr) {
        const auto count = u32(bytes, strings->offset);
        if (count > limits.max_strings) {
            fail(diagnostics, "QBS string table exceeds parser limits");
            return std::nullopt;
        }
    }
    ValidatedQbsView view;
    view.bytes_ = bytes;
    view.header_ = QbsHeaderView{1U, 2U, 0U, width, {}, section_count, total};
    std::copy_n(bytes.begin() + 12U, 16U, view.header_.schema_id.begin());
    view.record_offset_ = records->offset;
    view.field_offset_ = fields->offset;
    view.type_offset_ = types->offset;
    view.enum_offset_ = enums == nullptr ? 0U : enums->offset;
    view.enum_values_offset_ = enum_values == nullptr ? 0U : enum_values->offset;
    view.iss_offset_ = iss->offset;
    view.iss_size_ = iss->size;
    view.strings_offset_ = strings == nullptr ? 0U : strings->offset;
    view.strings_size_ = strings == nullptr ? 0U : strings->size;
    view.record_stride_ = 28U + width;
    view.enum_stride_ = 16U + width;
    view.record_count_ = record_count;
    view.field_count_ = field_count;
    view.type_count_ = type_count;
    view.enum_count_ = enum_count;
    return view;
}

} // namespace quarry::compiler::qbs
