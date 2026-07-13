#include "compiler/yaml/yaml_parser.hpp"

#include <yaml.h>

#include <algorithm>
#include <cassert>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace breadcrumbs::compiler::yaml {
namespace {

constexpr std::string_view yaml_pass = "yaml-parser";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] support::SourceLocation location(support::SourceFileId file_id, std::size_t offset) {
    return support::SourceLocation(file_id, offset);
}

[[nodiscard]] support::SourceRange range(support::SourceFileId file_id, std::size_t begin,
                                         std::size_t end) {
    return support::SourceRange(location(file_id, begin), location(file_id, end));
}

[[nodiscard]] support::SourceRange range_from_offsets(support::SourceFileId file_id,
                                                      std::size_t begin_offset,
                                                      std::size_t end_offset) {
    return range(file_id, begin_offset, end_offset);
}

[[nodiscard]] support::SourceRange point_range(support::SourceFileId file_id, std::size_t offset) {
    return range(file_id, offset, offset);
}

[[nodiscard]] std::string scalar_text(const yaml_event_t& event) {
    return std::string(reinterpret_cast<const char*>(event.data.scalar.value),
                       static_cast<std::size_t>(event.data.scalar.length));
}

[[nodiscard]] YamlScalarKind scalar_kind(yaml_scalar_style_t style) {
    switch (style) {
    case YAML_PLAIN_SCALAR_STYLE:
        return YamlScalarKind::Plain;
    case YAML_SINGLE_QUOTED_SCALAR_STYLE:
        return YamlScalarKind::SingleQuoted;
    case YAML_DOUBLE_QUOTED_SCALAR_STYLE:
        return YamlScalarKind::DoubleQuoted;
    case YAML_LITERAL_SCALAR_STYLE:
        return YamlScalarKind::Literal;
    case YAML_FOLDED_SCALAR_STYLE:
        return YamlScalarKind::Folded;
    default:
        return YamlScalarKind::Unknown;
    }
}

[[nodiscard]] bool has_anchor(const yaml_char_t* anchor) {
    return anchor != nullptr && anchor[0] != '\0';
}

[[nodiscard]] bool has_tag(const yaml_char_t* tag) { return tag != nullptr && tag[0] != '\0'; }

class YamlParserImpl {
public:
    YamlParserImpl(support::SourceFileId source_file_id, diagnostics::DiagnosticEngine& diagnostics,
                   std::string_view source)
        : source_file_id_(source_file_id), diagnostics_(diagnostics), source_(source) {
        if (yaml_parser_initialize(&parser_) == 0) {
            throw std::runtime_error("failed to initialize libyaml parser");
        }
        yaml_parser_set_input_string(&parser_,
                                     reinterpret_cast<const unsigned char*>(source_.data()),
                                     static_cast<unsigned long>(source_.size()));
    }

    ~YamlParserImpl() {
        if (has_event_) {
            yaml_event_delete(&event_);
        }
        yaml_parser_delete(&parser_);
    }

    [[nodiscard]] YamlParseResult parse() {
        YamlParseResult result;
        if (!advance()) {
            return result;
        }

        if (event_.type != YAML_STREAM_START_EVENT) {
            emit_malformed("expected YAML stream start", event_range());
            return result;
        }

        if (!advance()) {
            return result;
        }

        if (event_.type == YAML_STREAM_END_EVENT) {
            emit_malformed("expected exactly one YAML document", event_range());
            return result;
        }

        if (event_.type != YAML_DOCUMENT_START_EVENT) {
            emit_malformed("expected YAML document start", event_range());
            return result;
        }

        const yaml_mark_t document_start = event_.start_mark;
        if (!advance()) {
            return result;
        }

        YamlDocument document;
        if (event_.type == YAML_DOCUMENT_END_EVENT) {
            document.source_range =
                range_from_offsets(source_file_id_, static_cast<std::size_t>(document_start.index),
                                   static_cast<std::size_t>(event_.end_mark.index));
        } else {
            document.root = parse_node();
            if (document.root == nullptr) {
                return result;
            }

            if (event_.type != YAML_DOCUMENT_END_EVENT) {
                emit_malformed("expected YAML document end", event_range());
                return result;
            }
            document.source_range =
                range_from_offsets(source_file_id_, static_cast<std::size_t>(document_start.index),
                                   static_cast<std::size_t>(event_.end_mark.index));
        }

        if (!advance()) {
            return result;
        }

        if (event_.type == YAML_STREAM_END_EVENT) {
            result.document = std::move(document);
            return result;
        }

        if (event_.type == YAML_DOCUMENT_START_EVENT) {
            emit_multiple_documents(event_range());
            return result;
        }

        emit_malformed("expected end of YAML stream", event_range());
        return result;
    }

private:
    [[nodiscard]] bool advance() {
        if (has_event_) {
            yaml_event_delete(&event_);
            has_event_ = false;
        }

        if (yaml_parser_parse(&parser_, &event_) == 0) {
            const std::size_t problem_offset =
                std::min<std::size_t>(static_cast<std::size_t>(parser_.problem_mark.index),
                                      source_.size());
            emit_malformed(parser_.problem != nullptr ? parser_.problem : "malformed YAML",
                           point_range(source_file_id_, problem_offset));
            return false;
        }

        has_event_ = true;
        return true;
    }

    [[nodiscard]] support::SourceRange event_range() const {
        return range_from_offsets(source_file_id_, static_cast<std::size_t>(event_.start_mark.index),
                                  static_cast<std::size_t>(event_.end_mark.index));
    }

    void emit_diagnostic(std::string_view id, diagnostics::Severity severity,
                         std::string_view message, support::SourceRange diagnostic_range) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id(id), severity, std::string(message))
                .from_pass(std::string(yaml_pass));
        if (diagnostic_range.is_valid()) {
            builder.at(diagnostic_range);
        }
        diagnostics_.emit(builder.build());
    }

    void emit_malformed(std::string_view message, support::SourceRange diagnostic_range) {
        emit_diagnostic("BC2101", diagnostics::Severity::Error, message, diagnostic_range);
    }

    void emit_multiple_documents(support::SourceRange diagnostic_range) {
        emit_diagnostic("BC2102", diagnostics::Severity::Error,
                        "multiple YAML documents are not supported", diagnostic_range);
    }

    void emit_unsupported_anchor(support::SourceRange diagnostic_range) {
        emit_diagnostic("BC2103", diagnostics::Severity::Error,
                        "YAML anchors are not supported", diagnostic_range);
    }

    void emit_unsupported_alias(support::SourceRange diagnostic_range) {
        emit_diagnostic("BC2104", diagnostics::Severity::Error,
                        "YAML aliases are not supported", diagnostic_range);
    }

    void emit_unsupported_tag(support::SourceRange diagnostic_range) {
        emit_diagnostic("BC2105", diagnostics::Severity::Error,
                        "YAML tags are not supported", diagnostic_range);
    }

    void emit_unsupported_merge_key(support::SourceRange diagnostic_range) {
        emit_diagnostic("BC2106", diagnostics::Severity::Error,
                        "YAML merge keys are not supported", diagnostic_range);
    }

    [[nodiscard]] std::unique_ptr<YamlNode> parse_node() {
        switch (event_.type) {
        case YAML_SCALAR_EVENT:
            return parse_scalar();
        case YAML_SEQUENCE_START_EVENT:
            return parse_sequence();
        case YAML_MAPPING_START_EVENT:
            return parse_mapping();
        case YAML_ALIAS_EVENT:
            emit_unsupported_alias(event_range());
            return nullptr;
        default:
            emit_malformed("expected a YAML node", event_range());
            return nullptr;
        }
    }

    [[nodiscard]] std::unique_ptr<YamlNode> parse_scalar() {
        if (has_anchor(event_.data.scalar.anchor)) {
            emit_unsupported_anchor(event_range());
            return nullptr;
        }
        if (has_tag(event_.data.scalar.tag)) {
            emit_unsupported_tag(event_range());
            return nullptr;
        }

        auto node = std::make_unique<YamlNode>();
        node->source_range = event_range();
        node->value = YamlScalarNode{.value = scalar_text(event_),
                                     .kind = scalar_kind(event_.data.scalar.style)};

        if (!advance()) {
            return nullptr;
        }
        return node;
    }

    [[nodiscard]] std::unique_ptr<YamlNode> parse_sequence() {
        if (has_anchor(event_.data.sequence_start.anchor)) {
            emit_unsupported_anchor(event_range());
            return nullptr;
        }
        if (has_tag(event_.data.sequence_start.tag)) {
            emit_unsupported_tag(event_range());
            return nullptr;
        }

        const yaml_mark_t sequence_start = event_.start_mark;
        auto node = std::make_unique<YamlNode>();
        node->value = YamlSequenceNode{};

        if (!advance()) {
            return nullptr;
        }

        auto& sequence = std::get<YamlSequenceNode>(node->value);
        while (event_.type != YAML_SEQUENCE_END_EVENT) {
            std::unique_ptr<YamlNode> element = parse_node();
            if (element == nullptr) {
                return nullptr;
            }
            sequence.elements.push_back(std::move(element));
        }

        node->source_range =
            range_from_offsets(source_file_id_, static_cast<std::size_t>(sequence_start.index),
                               static_cast<std::size_t>(event_.end_mark.index));
        if (!advance()) {
            return nullptr;
        }
        return node;
    }

    [[nodiscard]] std::unique_ptr<YamlNode> parse_mapping() {
        if (has_anchor(event_.data.mapping_start.anchor)) {
            emit_unsupported_anchor(event_range());
            return nullptr;
        }
        if (has_tag(event_.data.mapping_start.tag)) {
            emit_unsupported_tag(event_range());
            return nullptr;
        }

        const yaml_mark_t mapping_start = event_.start_mark;
        auto node = std::make_unique<YamlNode>();
        node->value = YamlMappingNode{};

        if (!advance()) {
            return nullptr;
        }

        auto& mapping = std::get<YamlMappingNode>(node->value);
        while (event_.type != YAML_MAPPING_END_EVENT) {
            std::unique_ptr<YamlNode> key = parse_node();
            if (key == nullptr) {
                return nullptr;
            }

            const YamlScalarNode* key_scalar = std::get_if<YamlScalarNode>(&key->value);
            if (key_scalar != nullptr && key_scalar->value == "<<") {
                emit_unsupported_merge_key(key->source_range);
                return nullptr;
            }

            std::unique_ptr<YamlNode> value = parse_node();
            if (value == nullptr) {
                return nullptr;
            }

            mapping.entries.push_back(
                YamlMappingEntry{.key = std::move(key), .value = std::move(value)});
        }

        node->source_range =
            range_from_offsets(source_file_id_, static_cast<std::size_t>(mapping_start.index),
                               static_cast<std::size_t>(event_.end_mark.index));
        if (!advance()) {
            return nullptr;
        }
        return node;
    }

    support::SourceFileId source_file_id_;
    diagnostics::DiagnosticEngine& diagnostics_;
    std::string_view source_;

    yaml_parser_t parser_{};
    yaml_event_t event_{};
    bool has_event_ = false;
};

} // namespace

YamlParseResult YamlParser::parse(const support::SourceManager& source_manager,
                                  support::SourceFileId source_file_id,
                                  diagnostics::DiagnosticEngine& diagnostics) {
    const std::optional<std::string_view> source = source_manager.source_text(source_file_id);
    if (!source.has_value()) {
        throw std::invalid_argument(
            "YamlParser requires a SourceFileId registered in SourceManager");
    }

    YamlParserImpl parser(source_file_id, diagnostics, *source);
    return parser.parse();
}

} // namespace breadcrumbs::compiler::yaml
