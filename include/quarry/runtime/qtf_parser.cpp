#include "quarry/runtime/qtf_parser.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace quarry::runtime {
namespace {
using namespace quarry::compiler::qbs;
using quarry::compiler::diagnostics::Diagnostic;
using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::compiler::diagnostics::DiagnosticId;
using quarry::compiler::diagnostics::Severity;

enum class Kind {
    eof,
    identifier,
    number,
    string,
    bytes,
    at,
    left_brace,
    right_brace,
    left_bracket,
    right_bracket,
    colon,
    comma
};
struct Token {
    Kind kind = Kind::eof;
    std::string text;
    std::size_t offset = 0U;
};

bool valid_utf8(std::string_view value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t i = 0U;
    while (i < value.size()) {
        const auto first = bytes[i++];
        if (first <= 0x7FU)
            continue;
        const std::size_t continuation = first >= 0xC2U && first <= 0xDFU   ? 1U
                                         : first >= 0xE0U && first <= 0xEFU ? 2U
                                         : first >= 0xF0U && first <= 0xF4U ? 3U
                                                                            : 0U;
        if (continuation == 0U || value.size() - i < continuation)
            return false;
        std::uint32_t code_point = first & ((1U << (7U - continuation)) - 1U);
        for (std::size_t j = 0U; j < continuation; ++j) {
            const auto next = bytes[i++];
            if ((next & 0xC0U) != 0x80U)
                return false;
            code_point = (code_point << 6U) | (next & 0x3FU);
        }
        if ((continuation == 1U && code_point < 0x80U) ||
            (continuation == 2U && code_point < 0x800U) ||
            (continuation == 3U && code_point < 0x10000U) || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU))
            return false;
    }
    return true;
}

bool signed_range(std::int64_t value, std::uint16_t width) {
    if (width == 0U || width > 8U)
        return false;
    if (width == 8U)
        return true;
    const auto bits = static_cast<unsigned>(width) * 8U;
    return value >= -(std::int64_t{1} << (bits - 1U)) &&
           value <= (std::int64_t{1} << (bits - 1U)) - 1;
}

bool unsigned_range(std::uint64_t value, std::uint16_t width) {
    return width != 0U && width <= 8U &&
           (width == 8U || value <= (std::uint64_t{1} << (width * 8U)) - 1U);
}

template <typename T> bool parse_float(std::string_view text, T& value) {
    std::string owned(text);
    char* end = nullptr;
    if constexpr (std::is_same_v<T, float>)
        value = std::strtof(owned.c_str(), &end);
    else
        value = std::strtod(owned.c_str(), &end);
    return end == owned.c_str() + owned.size();
}

void error(DiagnosticCollection& diagnostics, std::size_t offset, std::string message) {
    message += " at byte " + std::to_string(offset);
    diagnostics.add(
        Diagnostic::create(DiagnosticId("qtf.parse"), Severity::Error, std::move(message)).build());
}

class Lexer {
public:
    Lexer(std::string_view text, DiagnosticCollection& diagnostics, std::size_t max_tokens)
        : text_(text), diagnostics_(diagnostics), max_tokens_(max_tokens) {}
    Token next() {
        if (tokens_++ >= max_tokens_) {
            error(diagnostics_, pos_, "QTF token limit exceeded");
            return {};
        }
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\r' || text_[pos_] == '\n'))
            ++pos_;
        const auto start = pos_;
        if (pos_ == text_.size())
            return {Kind::eof, {}, start};
        const char c = text_[pos_++];
        switch (c) {
        case '{':
            return {Kind::left_brace, {}, start};
        case '}':
            return {Kind::right_brace, {}, start};
        case '[':
            return {Kind::left_bracket, {}, start};
        case ']':
            return {Kind::right_bracket, {}, start};
        case ':':
            return {Kind::colon, {}, start};
        case ',':
            return {Kind::comma, {}, start};
        case '@':
            return {Kind::at, {}, start};
        case '"':
            return string(start, false);
        default:
            break;
        }
        while (pos_ < text_.size() && text_[pos_] != ' ' && text_[pos_] != '\t' &&
               text_[pos_] != '\r' && text_[pos_] != '\n' && text_[pos_] != '{' &&
               text_[pos_] != '}' && text_[pos_] != '[' && text_[pos_] != ']' &&
               text_[pos_] != ':' && text_[pos_] != ',')
            ++pos_;
        std::string word(text_.substr(start, pos_ - start));
        if (word.starts_with("hex\"") && word.back() == '"')
            return {Kind::bytes, word.substr(4, word.size() - 5U), start};
        bool numeric = !word.empty() &&
                       (word[0] == '-' || word[0] == '.' || (word[0] >= '0' && word[0] <= '9'));
        return {numeric ? Kind::number : Kind::identifier, std::move(word), start};
    }

private:
    Token string(std::size_t start, bool) {
        std::string value;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                if (!valid_utf8(value)) {
                    error(diagnostics_, start, "invalid UTF-8 string");
                    return {};
                }
                return {Kind::string, std::move(value), start};
            }
            if (static_cast<unsigned char>(c) < 0x20U) {
                error(diagnostics_, pos_ - 1U, "unescaped control character in string");
                return {};
            }
            if (c != '\\') {
                value.push_back(c);
                continue;
            }
            if (pos_ == text_.size())
                break;
            const char e = text_[pos_++];
            switch (e) {
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u': {
                if (pos_ + 4U > text_.size())
                    break;
                unsigned value16 = 0U;
                for (unsigned i = 0U; i < 4U; ++i) {
                    const char h = text_[pos_++];
                    const auto digit = h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0';
                    if (digit < 0 || digit > 15) {
                        error(diagnostics_, pos_, "invalid string escape");
                        return {};
                    }
                    value16 = value16 * 16U + static_cast<unsigned>(digit);
                }
                if (value16 > 0x7FU) {
                    error(diagnostics_, start, "non-ASCII unicode escapes are not supported");
                    return {};
                }
                value.push_back(static_cast<char>(value16));
                break;
            }
            default:
                error(diagnostics_, pos_ - 1U, "invalid string escape");
                return {};
            }
        }
        error(diagnostics_, start, "unterminated string");
        return {};
    }
    std::string_view text_;
    DiagnosticCollection& diagnostics_;
    std::size_t max_tokens_;
    std::size_t pos_ = 0U;
    std::size_t tokens_ = 0U;
};

class Parser {
public:
    Parser(std::string_view text, const ValidatedQbsView& schema, DiagnosticCollection& diagnostics,
           QtfParseLimits limits)
        : lexer_(text, diagnostics, limits.max_tokens), schema_(schema), diagnostics_(diagnostics),
          limits_(limits) {
        current_ = lexer_.next();
    }
    std::optional<BrfRecordInput> root(const QbsRecordView& record) {
        auto value = parse_record(record, 0U);
        if (!value || current_.kind != Kind::eof) {
            if (current_.kind != Kind::eof)
                error(diagnostics_, current_.offset, "trailing QTF input");
            return std::nullopt;
        }
        return value;
    }

private:
    void advance() { current_ = lexer_.next(); }
    bool take(Kind kind, const char* expected) {
        if (current_.kind != kind) {
            error(diagnostics_, current_.offset, std::string("expected ") + expected);
            return false;
        }
        advance();
        return true;
    }
    std::optional<BrfRecordInput> parse_record(const QbsRecordView& record, std::size_t depth) {
        if (depth > limits_.max_depth) {
            error(diagnostics_, current_.offset, "QTF nesting depth exceeded");
            return std::nullopt;
        }
        if (!take(Kind::left_brace, "{"))
            return std::nullopt;
        BrfRecordInput result;
        result.record_id = record.record_id;
        result.identity = std::string(record.identity);
        result.fields.resize(record.field_count);
        std::vector<bool> seen(record.field_count, false);
        while (current_.kind != Kind::right_brace) {
            if (current_.kind == Kind::eof) {
                error(diagnostics_, current_.offset, "missing }");
                return std::nullopt;
            }
            std::optional<std::uint16_t> index;
            if (current_.kind == Kind::identifier) {
                const auto field = schema_.find_field_by_name(record_index(record), current_.text);
                if (!field) {
                    error(diagnostics_, current_.offset, "unknown QTF field");
                    return std::nullopt;
                }
                index = field->field_index;
                advance();
            } else if (current_.kind == Kind::at) {
                advance();
                if (current_.kind != Kind::number || current_.text.empty() ||
                    current_.text[0] == '-') {
                    error(diagnostics_, current_.offset, "invalid field index");
                    return std::nullopt;
                }
                std::uint32_t parsed = 0U;
                auto [p, ec] = std::from_chars(current_.text.data(),
                                               current_.text.data() + current_.text.size(), parsed);
                if (ec != std::errc{} || p != current_.text.data() + current_.text.size() ||
                    parsed >= record.field_count) {
                    error(diagnostics_, current_.offset, "invalid field index");
                    return std::nullopt;
                }
                index = static_cast<std::uint16_t>(parsed);
                advance();
            } else {
                error(diagnostics_, current_.offset, "expected field identifier");
                return std::nullopt;
            }
            if (!take(Kind::colon, ":") || seen[*index]) {
                error(diagnostics_, current_.offset,
                      seen[*index] ? "duplicate QTF field" : "invalid field");
                return std::nullopt;
            }
            seen[*index] = true;
            const auto field = schema_.field(record.field_start + *index);
            const auto value = parse_value(schema_.type(field.type_index), depth + 1U);
            if (!value)
                return std::nullopt;
            result.fields[*index] = *value;
        }
        advance();
        return result;
    }
    std::optional<BrfEncodeValue> parse_value(const QbsTypeView& type, std::size_t depth) {
        if (type.code == 15U) {
            const auto child = schema_.record(type.reference);
            auto parsed = parse_record(child, depth);
            if (!parsed)
                return std::nullopt;
            return BrfEncodeValue{std::make_shared<const BrfRecordInput>(*parsed)};
        }
        if (type.code == 16U)
            return parse_array(type, depth);
        if (type.code == 13U && current_.kind == Kind::string) {
            if (current_.text.size() > type.max_bytes) {
                error(diagnostics_, current_.offset, "string exceeds QBS max_bytes");
                return std::nullopt;
            }
            auto value = current_.text;
            advance();
            return BrfEncodeValue{std::move(value)};
        }
        if (type.code == 14U && current_.kind == Kind::bytes) {
            if (current_.text.size() / 2U > type.max_bytes) {
                error(diagnostics_, current_.offset, "bytes exceed QBS max_bytes");
                return std::nullopt;
            }
            std::vector<std::uint8_t> bytes;
            if (current_.text.size() % 2U) {
                error(diagnostics_, current_.offset, "odd byte literal");
                return std::nullopt;
            }
            for (std::size_t i = 0; i < current_.text.size(); i += 2U) {
                auto digit = [](char c) -> int {
                    if (c >= '0' && c <= '9')
                        return c - '0';
                    if (c >= 'a' && c <= 'f')
                        return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F')
                        return c - 'A' + 10;
                    return -1;
                };
                const int a = digit(current_.text[i]), b = digit(current_.text[i + 1U]);
                if (a < 0 || b < 0) {
                    error(diagnostics_, current_.offset, "invalid hex literal");
                    return std::nullopt;
                }
                bytes.push_back(static_cast<std::uint8_t>(a * 16 + b));
            }
            advance();
            return BrfEncodeValue{std::move(bytes)};
        }
        if (current_.kind != Kind::number && current_.kind != Kind::identifier) {
            error(diagnostics_, current_.offset, "expected scalar");
            return std::nullopt;
        }
        const auto text = current_.text;
        advance();
        if (type.code == 1U) {
            if (text == "true")
                return BrfEncodeValue{true};
            if (text == "false")
                return BrfEncodeValue{false};
        }
        if (type.code == 10U || type.code == 11U) {
            if (text == "nan")
                return type.code == 10U ? BrfEncodeValue{std::numeric_limits<float>::quiet_NaN()}
                                        : BrfEncodeValue{std::numeric_limits<double>::quiet_NaN()};
            if (text == "inf" || text == "-inf")
                return type.code == 10U ? BrfEncodeValue{text == "-inf" ? -INFINITY : INFINITY}
                                        : BrfEncodeValue{text == "-inf" ? -INFINITY : INFINITY};
            if (type.code == 10U) {
                float v{};
                if (parse_float(text, v))
                    return BrfEncodeValue{v};
            } else {
                double v{};
                if (parse_float(text, v))
                    return BrfEncodeValue{v};
            }
        }
        if (type.code == 12U || type.code == 3U || type.code == 5U || type.code == 7U ||
            type.code == 9U) {
            std::uint64_t v{};
            auto [p, e] = std::from_chars(text.data(), text.data() + text.size(), v);
            if (e == std::errc{} && p == text.data() + text.size() &&
                unsigned_range(v, type.encoded_width) &&
                (type.code != 12U || std::find(schema_.enum_type(type.reference).values.begin(),
                                               schema_.enum_type(type.reference).values.end(), v) !=
                                         schema_.enum_type(type.reference).values.end()))
                return BrfEncodeValue{v};
        }
        if (type.code == 2U || type.code == 4U || type.code == 6U || type.code == 8U) {
            std::int64_t v{};
            auto [p, e] = std::from_chars(text.data(), text.data() + text.size(), v);
            if (e == std::errc{} && p == text.data() + text.size() &&
                signed_range(v, type.encoded_width))
                return BrfEncodeValue{v};
        }
        error(diagnostics_, current_.offset, "invalid scalar for QBS type");
        return std::nullopt;
    }
    std::optional<BrfEncodeValue> parse_array(const QbsTypeView& type, std::size_t depth) {
        if (!take(Kind::left_bracket, "["))
            return std::nullopt;
        std::vector<BrfEncodeValue> values;
        auto element = schema_.type(type.reference);
        if (element.code == 15U) {
            auto records = std::make_shared<std::vector<BrfNestedRecordValue>>();
            if (current_.kind != Kind::right_bracket)
                while (true) {
                    auto child = parse_record(schema_.record(element.reference), depth);
                    if (!child)
                        return std::nullopt;
                    records->push_back(std::make_shared<const BrfRecordInput>(*child));
                    if (current_.kind == Kind::right_bracket)
                        break;
                    if (!take(Kind::comma, ","))
                        return std::nullopt;
                    if (records->size() >= type.max_elements) {
                        error(diagnostics_, current_.offset,
                              "record array exceeds QBS max_elements");
                        return std::nullopt;
                    }
                }
            if (!take(Kind::right_bracket, "]"))
                return std::nullopt;
            return BrfEncodeValue{records};
        }
        BrfEncodeArray array;
        if (element.code == 1U)
            array = BrfBoolArray{};
        else if (element.code == 10U)
            array = BrfFloat32Array{};
        else if (element.code == 11U)
            array = BrfFloat64Array{};
        else if (element.code == 2U || element.code == 4U || element.code == 6U ||
                 element.code == 8U)
            array = BrfSignedArray{};
        else
            array = BrfUnsignedArray{};
        if (current_.kind != Kind::right_bracket)
            while (true) {
                auto v = parse_value(element, depth);
                if (!v)
                    return std::nullopt;
                std::visit(
                    [&](auto&& x) {
                        using T = std::decay_t<decltype(x)>;
                        if constexpr (std::is_same_v<T, bool>)
                            std::get<BrfBoolArray>(array).push_back(x);
                        else if constexpr (std::is_same_v<T, std::int64_t>)
                            std::get<BrfSignedArray>(array).push_back(x);
                        else if constexpr (std::is_same_v<T, std::uint64_t>)
                            std::get<BrfUnsignedArray>(array).push_back(x);
                        else if constexpr (std::is_same_v<T, float>)
                            std::get<BrfFloat32Array>(array).push_back(x);
                        else if constexpr (std::is_same_v<T, double>)
                            std::get<BrfFloat64Array>(array).push_back(x);
                    },
                    *v);
                if (std::visit([](auto const& a) { return a.size(); }, array) > type.max_elements) {
                    error(diagnostics_, current_.offset, "array exceeds QBS max_elements");
                    return std::nullopt;
                }
                if (current_.kind == Kind::right_bracket)
                    break;
                if (!take(Kind::comma, ","))
                    return std::nullopt;
            }
        if (!take(Kind::right_bracket, "]"))
            return std::nullopt;
        return BrfEncodeValue{std::move(array)};
    }
    std::size_t record_index(const QbsRecordView& record) const {
        for (std::size_t i = 0; i < schema_.record_count(); ++i)
            if (schema_.record(i).record_id == record.record_id)
                return i;
        return 0U;
    }
    Lexer lexer_;
    const ValidatedQbsView& schema_;
    DiagnosticCollection& diagnostics_;
    QtfParseLimits limits_;
    Token current_;
};
} // namespace
std::optional<BrfRecordInput>
parse_qtf(std::string_view text, const quarry::compiler::qbs::ValidatedQbsView& schema,
          const quarry::compiler::qbs::QbsRecordView& record,
          quarry::compiler::diagnostics::DiagnosticCollection& diagnostics, QtfParseLimits limits) {
    if (text.size() > limits.max_input_bytes) {
        error(diagnostics, 0, "QTF input exceeds limit");
        return std::nullopt;
    }
    return Parser(text, schema, diagnostics, limits).root(record);
}
} // namespace quarry::runtime
