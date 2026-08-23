#include "quarry/runtime/qtf_exporter.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace quarry::runtime {
namespace {

std::string indent(std::size_t depth) { return std::string(depth * 2U, ' '); }

void append_string(std::string& output, std::string_view value) {
    output.push_back('"');
    for (const auto c : value) {
        switch (c) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20U) {
                constexpr char digits[] = "0123456789abcdef";
                output += "\\u00";
                output.push_back(digits[(static_cast<unsigned char>(c) >> 4U) & 0x0FU]);
                output.push_back(digits[static_cast<unsigned char>(c) & 0x0FU]);
            } else {
                output.push_back(c);
            }
        }
    }
    output.push_back('"');
}

template <typename T> bool append_number(std::string& output, T value) {
    char buffer[128];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
        return false;
    output.append(buffer, result.ptr);
    return true;
}

bool append_float(std::string& output, float value) {
    if (std::isnan(value)) {
        output += "nan";
        return true;
    }
    if (std::isinf(value)) {
        output += value < 0.0F ? "-inf" : "inf";
        return true;
    }
    return append_number(output, value);
}

bool append_float(std::string& output, double value) {
    if (std::isnan(value)) {
        output += "nan";
        return true;
    }
    if (std::isinf(value)) {
        output += value < 0.0 ? "-inf" : "inf";
        return true;
    }
    return append_number(output, value);
}

bool append_value(std::string& output, const BrfValueView& value) {
    switch (value.kind()) {
    case GenericBrfValueKind::boolean:
        if (!value.as_bool().has_value())
            return false;
        output += *value.as_bool() ? "true" : "false";
        return true;
    case GenericBrfValueKind::signed_integer:
        return value.as_signed().has_value() && append_number(output, *value.as_signed());
    case GenericBrfValueKind::unsigned_integer:
    case GenericBrfValueKind::enumeration:
        return value.as_unsigned().has_value() && append_number(output, *value.as_unsigned());
    case GenericBrfValueKind::float32:
        return value.as_float32().has_value() && append_float(output, *value.as_float32());
    case GenericBrfValueKind::float64:
        return value.as_float64().has_value() && append_float(output, *value.as_float64());
    case GenericBrfValueKind::string:
        return value.as_string().has_value() && (append_string(output, *value.as_string()), true);
    case GenericBrfValueKind::bytes: {
        const auto bytes = value.as_bytes();
        if (!bytes.has_value())
            return false;
        constexpr char digits[] = "0123456789abcdef";
        output += "hex\"";
        for (const auto byte : *bytes) {
            output.push_back(digits[byte >> 4U]);
            output.push_back(digits[byte & 0x0FU]);
        }
        output += "\"";
        return true;
    }
    case GenericBrfValueKind::array:
    case GenericBrfValueKind::record:
        return false;
    }
    return false;
}

struct Writer {
    std::string output;
    std::vector<bool> record_from_array;
    std::vector<bool> record_is_last;
    std::vector<BrfArrayValueView> arrays;
    bool pending_array_record = false;
    bool pending_array_record_last = false;
    bool failed = false;
};

} // namespace

QtfExportResult export_qtf(const ValidatedBrfRecordView& record, BrfTraversalLimits limits) {
    Writer writer;
    const auto result = traverse_brf(
        record,
        [&](const BrfTraversalEvent& event) {
            switch (event.kind) {
            case BrfTraversalEventKind::record_begin:
                if (writer.pending_array_record) {
                    writer.output += "{\n";
                    writer.record_from_array.push_back(true);
                    writer.record_is_last.push_back(writer.pending_array_record_last);
                    writer.pending_array_record = false;
                } else if (writer.record_from_array.empty()) {
                    writer.output += "{\n";
                    writer.record_from_array.push_back(false);
                    writer.record_is_last.push_back(true);
                } else {
                    writer.output += "{\n";
                    writer.record_from_array.push_back(false);
                    writer.record_is_last.push_back(true);
                }
                break;
            case BrfTraversalEventKind::field:
                if (!event.present)
                    break;
                writer.output += indent(event.depth + 1U);
                if (!event.field.name.empty())
                    writer.output += event.field.name;
                else {
                    writer.output.push_back('@');
                    if (!append_number(writer.output, event.field.field_index))
                        writer.failed = true;
                }
                writer.output += ": ";
                break;
            case BrfTraversalEventKind::scalar:
                if (event.array.has_value())
                    break;
                if (!event.value.has_value() || !append_value(writer.output, *event.value))
                    writer.failed = true;
                writer.output.push_back('\n');
                break;
            case BrfTraversalEventKind::array_begin:
                writer.output.push_back('[');
                writer.arrays.push_back(*event.array);
                if (event.array->size() != 0U && event.array->element_type().code == 15U)
                    writer.output.push_back('\n');
                break;
            case BrfTraversalEventKind::array_element: {
                const auto value = event.value;
                if (!value.has_value()) {
                    writer.failed = true;
                    break;
                }
                const auto array = event.array;
                if (value->kind() == GenericBrfValueKind::record) {
                    writer.output += indent(event.depth + 1U);
                    writer.pending_array_record = true;
                    if (array.has_value())
                        writer.pending_array_record_last = event.index + 1U == array->size();
                } else {
                    if (event.index != 0U)
                        writer.output += ", ";
                    if (!append_value(writer.output, *value))
                        writer.failed = true;
                }
                break;
            }
            case BrfTraversalEventKind::array_end:
                if (!writer.arrays.empty())
                    writer.arrays.pop_back();
                if (event.array->size() == 0U) {
                    writer.output += "]\n";
                    break;
                }
                if (event.array->element_type().code != 15U) {
                    writer.output += "]\n";
                    break;
                }
                writer.output += indent(event.depth + 1U);
                writer.output += "]\n";
                break;
            case BrfTraversalEventKind::record_end: {
                const bool from_array =
                    !writer.record_from_array.empty() && writer.record_from_array.back();
                const bool last = !writer.record_is_last.empty() && writer.record_is_last.back();
                if (!from_array) {
                    writer.output += indent(event.depth);
                    writer.output += "}\n";
                    if (!writer.record_from_array.empty()) {
                        writer.record_from_array.pop_back();
                        writer.record_is_last.pop_back();
                    }
                } else {
                    writer.output += indent(event.depth + 1U);
                    writer.output += last ? "}" : "},";
                    writer.output.push_back('\n');
                    writer.record_from_array.pop_back();
                    writer.record_is_last.pop_back();
                }
                break;
            }
            }
            return writer.failed ? BrfTraversalControl::Stop : BrfTraversalControl::Continue;
        },
        limits);
    if (writer.failed || result == BrfTraversalResult::internal_error)
        return {std::nullopt, QtfExportError::invalid_value};
    if (result == BrfTraversalResult::work_limit || result == BrfTraversalResult::depth_limit)
        return {std::nullopt, QtfExportError::traversal_limit};
    if (result == BrfTraversalResult::stopped)
        return {std::nullopt, QtfExportError::invalid_value};
    return {std::move(writer.output), QtfExportError::none};
}

} // namespace quarry::runtime
