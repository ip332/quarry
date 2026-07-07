#include "compiler/parser/parser.hpp"

#include "compiler/parser/lexer.hpp"

#include <cassert>
#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace breadcrumbs::compiler::parser {
namespace {

constexpr std::string_view parser_pass = "parser";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] bool is_type_keyword(TokenKind kind) {
    switch (kind) {
    case TokenKind::KeywordBool:
    case TokenKind::KeywordU8:
    case TokenKind::KeywordU16:
    case TokenKind::KeywordU32:
    case TokenKind::KeywordU64:
    case TokenKind::KeywordI8:
    case TokenKind::KeywordI16:
    case TokenKind::KeywordI32:
    case TokenKind::KeywordI64:
    case TokenKind::KeywordF32:
    case TokenKind::KeywordF64:
    case TokenKind::KeywordString:
    case TokenKind::KeywordBytes:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_literal_token(TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier:
    case TokenKind::IntegerLiteral:
    case TokenKind::StringLiteral:
    case TokenKind::KeywordTrue:
    case TokenKind::KeywordFalse:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_declaration_start(TokenKind kind) {
    switch (kind) {
    case TokenKind::KeywordImport:
    case TokenKind::KeywordNamespace:
    case TokenKind::KeywordRecord:
    case TokenKind::KeywordEnum:
    case TokenKind::At:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_member_start(TokenKind kind) {
    return kind == TokenKind::At || kind == TokenKind::Identifier;
}

[[nodiscard]] std::string unquote_string_literal(std::string_view spelling) {
    if (spelling.size() >= 2 && spelling.front() == '"' && spelling.back() == '"') {
        return std::string(spelling.substr(1, spelling.size() - 2));
    }
    return std::string(spelling);
}

} // namespace

class ParserImpl {
public:
    ParserImpl(const support::SourceManager& source_manager, support::SourceFileId source_file_id,
               diagnostics::DiagnosticEngine& diagnostics)
        : diagnostics_(diagnostics) {
        Lexer lexer(source_manager, source_file_id, diagnostics_);
        tokens_ = lexer.lex_all();
    }

    [[nodiscard]] ParseResult parse() {
        ParseResult result;
        result.ast.declarations = parse_declarations();
        result.ast.source_range = compute_schema_file_range();
        return result;
    }

private:
    [[nodiscard]] const Token& current() const { return tokens_[current_]; }

    [[nodiscard]] const Token& previous() const { return tokens_[current_ - 1]; }

    [[nodiscard]] bool at_end() const { return current().kind == TokenKind::EndOfFile; }

    [[nodiscard]] bool check(TokenKind kind) const { return !at_end() && current().kind == kind; }

    [[nodiscard]] bool match(TokenKind kind) {
        if (!check(kind)) {
            return false;
        }
        ++current_;
        return true;
    }

    [[nodiscard]] Token advance() {
        if (!at_end()) {
            ++current_;
        }
        return previous();
    }

    void skip_invalid_tokens() {
        while (check(TokenKind::Invalid)) {
            (void)advance();
        }
    }

    void emit_error(const Token& token, diagnostics::DiagnosticId id, std::string message) {
        diagnostics_.emit(diagnostics::Diagnostic::create(
                              std::move(id), diagnostics::Severity::Error, std::move(message))
                              .at(token.source_range)
                              .from_pass(std::string(parser_pass))
                              .build());
    }

    void emit_expected(const Token& token, std::string_view expected) {
        std::ostringstream message;
        message << "expected " << expected;
        emit_error(token, diagnostic_id("BC3002"), message.str());
    }

    void emit_unexpected(const Token& token, std::string_view context) {
        std::ostringstream message;
        message << "unexpected token " << token_kind_name(token.kind);
        if (!context.empty()) {
            message << " while " << context;
        }
        emit_error(token, diagnostic_id("BC3001"), message.str());
    }

    [[nodiscard]] ast::IdentifierSyntax parse_name_part(bool allow_type_keywords,
                                                        std::string_view expected_name_kind) {
        const Token& token = current();
        if (token.kind == TokenKind::Identifier ||
            (allow_type_keywords && is_type_keyword(token.kind))) {
            (void)advance();
            return ast::IdentifierSyntax{
                .source_range = token.source_range,
                .text = std::string(token.spelling),
            };
        }

        emit_expected(token, std::string(expected_name_kind));
        if (!at_end()) {
            (void)advance();
        }
        return ast::IdentifierSyntax{
            .source_range = token.source_range,
            .text = std::string(token.spelling),
        };
    }

    [[nodiscard]] ast::QualifiedNameSyntax
    parse_qualified_name(bool allow_type_keywords, std::string_view expected_name_kind) {
        const Token& begin_token = current();
        std::vector<ast::IdentifierSyntax> parts;
        parts.push_back(parse_name_part(allow_type_keywords, expected_name_kind));
        while (match(TokenKind::Dot)) {
            parts.push_back(parse_name_part(allow_type_keywords, expected_name_kind));
        }

        const support::SourceLocation begin =
            parts.empty() ? begin_token.source_range.begin() : parts.front().source_range.begin();
        const support::SourceLocation end =
            parts.empty() ? begin_token.source_range.end() : parts.back().source_range.end();
        return ast::QualifiedNameSyntax{
            .source_range = support::SourceRange(begin, end),
            .parts = std::move(parts),
        };
    }

    [[nodiscard]] ast::AnnotationSyntax parse_annotation() {
        const Token at_token = advance();
        ast::QualifiedNameSyntax name = parse_qualified_name(false, "annotation name");
        std::optional<std::string> value;
        support::SourceLocation end = name.source_range.end();

        if (match(TokenKind::LeftParen)) {
            const Token& token = current();
            if (token.kind == TokenKind::StringLiteral) {
                value = unquote_string_literal(token.spelling);
                end = token.source_range.end();
                (void)advance();
            } else {
                emit_expected(token, "string literal");
            }

            if (match(TokenKind::RightParen)) {
                end = previous().source_range.end();
            } else {
                emit_expected(current(), "')'");
            }
        }

        return ast::AnnotationSyntax{
            .source_range = support::SourceRange(at_token.source_range.begin(), end),
            .name = std::move(name),
            .value = std::move(value),
        };
    }

    [[nodiscard]] std::vector<ast::AnnotationSyntax> parse_annotations() {
        std::vector<ast::AnnotationSyntax> annotations;
        while (check(TokenKind::At)) {
            annotations.push_back(parse_annotation());
        }
        return annotations;
    }

    [[nodiscard]] ast::TypeReferenceSyntax parse_type_reference() {
        const ast::QualifiedNameSyntax name = parse_qualified_name(true, "type name");
        return ast::TypeReferenceSyntax{
            .source_range = name.source_range,
            .name = name,
        };
    }

    [[nodiscard]] ast::TypeSyntax parse_type() {
        ast::TypeReferenceSyntax element_type = parse_type_reference();
        if (!match(TokenKind::LeftBracket)) {
            return ast::TypeSyntax(std::move(element_type));
        }

        std::optional<std::size_t> fixed_size;
        const Token& token = current();
        if (token.kind == TokenKind::IntegerLiteral) {
            try {
                fixed_size = static_cast<std::size_t>(std::stoull(std::string(token.spelling)));
            } catch (const std::exception&) {
                fixed_size = std::nullopt;
            }
            (void)advance();
        } else if (token.kind != TokenKind::RightBracket) {
            emit_expected(token, "integer literal or ']'");
        }

        support::SourceLocation end = previous().source_range.end();
        if (match(TokenKind::RightBracket)) {
            end = previous().source_range.end();
        } else {
            emit_expected(current(), "']'");
        }

        return ast::TypeSyntax(ast::ArrayTypeSyntax{
            .source_range = support::SourceRange(element_type.source_range.begin(), end),
            .element_type = std::move(element_type),
            .fixed_size = fixed_size,
        });
    }

    [[nodiscard]] std::optional<std::string> parse_enum_literal() {
        const Token& token = current();
        if (!is_literal_token(token.kind)) {
            emit_expected(token, "literal");
            return std::nullopt;
        }

        (void)advance();
        return std::string(token.spelling);
    }

    [[nodiscard]] ast::FieldDeclarationSyntax
    parse_field(const std::vector<ast::AnnotationSyntax>& annotations, const Token& start_token) {
        ast::IdentifierSyntax name = parse_name_part(false, "field name");
        const Token after_name = current();
        if (!match(TokenKind::Colon)) {
            emit_expected(after_name, "':' after field name");
        }

        ast::TypeSyntax type = parse_type();
        if (match(TokenKind::Semicolon)) {
            (void)previous();
        }

        support::SourceLocation end =
            std::visit([](const auto& typed) { return typed.source_range.end(); }, type);
        if (current_ > 0 && previous().kind == TokenKind::Semicolon) {
            end = previous().source_range.end();
        }

        return ast::FieldDeclarationSyntax{
            .source_range = support::SourceRange(start_token.source_range.begin(), end),
            .name = std::move(name),
            .type = std::move(type),
            .annotations = annotations,
        };
    }

    [[nodiscard]] ast::EnumValueDeclarationSyntax
    parse_enum_value(const std::vector<ast::AnnotationSyntax>& annotations,
                     const Token& start_token) {
        ast::IdentifierSyntax name = parse_name_part(false, "enum value name");
        std::optional<std::string> value;
        support::SourceLocation end = name.source_range.end();

        if (match(TokenKind::Equals)) {
            value = parse_enum_literal();
            if (!value.has_value()) {
                end = current().source_range.begin();
            } else {
                end = previous().source_range.end();
            }
        }

        if (match(TokenKind::Semicolon)) {
            end = previous().source_range.end();
        }

        return ast::EnumValueDeclarationSyntax{
            .source_range = support::SourceRange(start_token.source_range.begin(), end),
            .name = std::move(name),
            .value = std::move(value),
            .annotations = annotations,
        };
    }

    [[nodiscard]] ast::ImportDeclarationSyntax parse_import(const Token& start_token) {
        const ast::QualifiedNameSyntax imported_name = parse_qualified_name(false, "import name");
        if (match(TokenKind::Semicolon)) {
            return ast::ImportDeclarationSyntax{
                .source_range = support::SourceRange(start_token.source_range.begin(),
                                                     previous().source_range.end()),
                .imported_name = imported_name,
            };
        }

        return ast::ImportDeclarationSyntax{
            .source_range = support::SourceRange(start_token.source_range.begin(),
                                                 imported_name.source_range.end()),
            .imported_name = imported_name,
        };
    }

    [[nodiscard]] ast::NamespaceDeclarationSyntax
    parse_namespace(const std::vector<ast::AnnotationSyntax>& annotations,
                    const Token& start_token) {
        const ast::QualifiedNameSyntax name = parse_qualified_name(false, "namespace name");
        const Token brace_token = current();
        if (!match(TokenKind::LeftBrace)) {
            emit_expected(brace_token, "'{'");
        }

        std::vector<ast::DeclarationPtr> declarations =
            parse_block_declarations([](TokenKind kind) { return is_declaration_start(kind); });

        support::SourceLocation end = current().source_range.begin();
        if (match(TokenKind::RightBrace)) {
            end = previous().source_range.end();
        } else {
            emit_expected(current(), "'}'");
        }

        return ast::NamespaceDeclarationSyntax{
            .source_range = support::SourceRange(start_token.source_range.begin(), end),
            .name = name,
            .declarations = std::move(declarations),
            .annotations = annotations,
        };
    }

    [[nodiscard]] ast::RecordDeclarationSyntax
    parse_record(const std::vector<ast::AnnotationSyntax>& annotations, const Token& start_token) {
        const ast::IdentifierSyntax name = parse_name_part(false, "record name");
        const Token brace_token = current();
        if (!match(TokenKind::LeftBrace)) {
            emit_expected(brace_token, "'{'");
        }

        std::vector<ast::FieldDeclarationSyntax> fields;
        while (!at_end() && !check(TokenKind::RightBrace)) {
            skip_invalid_tokens();
            if (check(TokenKind::RightBrace) || at_end()) {
                break;
            }
            if (check(TokenKind::Semicolon)) {
                (void)advance();
                continue;
            }
            if (!is_member_start(current().kind)) {
                emit_unexpected(current(), "parsing record fields");
                (void)advance();
                continue;
            }

            const Token member_start = current();
            const std::vector<ast::AnnotationSyntax> member_annotations = parse_annotations();
            if (!check(TokenKind::Identifier)) {
                emit_expected(current(), "field name");
                if (!at_end()) {
                    (void)advance();
                }
                continue;
            }

            fields.push_back(parse_field(member_annotations, member_start));
        }

        support::SourceLocation end = current().source_range.begin();
        if (match(TokenKind::RightBrace)) {
            end = previous().source_range.end();
        } else {
            emit_expected(current(), "'}'");
        }

        return ast::RecordDeclarationSyntax{
            .source_range = support::SourceRange(start_token.source_range.begin(), end),
            .name = name,
            .fields = std::move(fields),
            .annotations = annotations,
        };
    }

    [[nodiscard]] ast::EnumDeclarationSyntax
    parse_enum(const std::vector<ast::AnnotationSyntax>& annotations, const Token& start_token) {
        const ast::IdentifierSyntax name = parse_name_part(false, "enum name");
        const Token brace_token = current();
        if (!match(TokenKind::LeftBrace)) {
            emit_expected(brace_token, "'{'");
        }

        std::vector<ast::EnumValueDeclarationSyntax> values;
        while (!at_end() && !check(TokenKind::RightBrace)) {
            skip_invalid_tokens();
            if (check(TokenKind::RightBrace) || at_end()) {
                break;
            }
            if (check(TokenKind::Semicolon)) {
                (void)advance();
                continue;
            }
            if (!is_member_start(current().kind)) {
                emit_unexpected(current(), "parsing enum values");
                (void)advance();
                continue;
            }

            const Token member_start = current();
            const std::vector<ast::AnnotationSyntax> member_annotations = parse_annotations();
            if (!check(TokenKind::Identifier)) {
                emit_expected(current(), "enum value name");
                if (!at_end()) {
                    (void)advance();
                }
                continue;
            }

            values.push_back(parse_enum_value(member_annotations, member_start));
        }

        support::SourceLocation end = current().source_range.begin();
        if (match(TokenKind::RightBrace)) {
            end = previous().source_range.end();
        } else {
            emit_expected(current(), "'}'");
        }

        return ast::EnumDeclarationSyntax{
            .source_range = support::SourceRange(start_token.source_range.begin(), end),
            .name = name,
            .values = std::move(values),
            .annotations = annotations,
        };
    }

    [[nodiscard]] std::unique_ptr<ast::DeclarationSyntax> parse_declaration() {
        skip_invalid_tokens();
        const Token start_token = current();
        const std::vector<ast::AnnotationSyntax> annotations = parse_annotations();
        if (annotations.empty() && check(TokenKind::KeywordImport)) {
            (void)advance();
            return ast::make_declaration(parse_import(start_token));
        }

        if (!annotations.empty() && check(TokenKind::KeywordImport)) {
            emit_error(start_token, diagnostic_id("BC3001"),
                       "annotations are not supported on import declarations");
            (void)advance();
            return ast::make_declaration(parse_import(start_token));
        }

        if (!is_declaration_start(current().kind)) {
            emit_unexpected(current(), "parsing a declaration");
            if (!at_end()) {
                (void)advance();
            }
            return nullptr;
        }

        const Token keyword_token = current();
        switch (keyword_token.kind) {
        case TokenKind::KeywordNamespace:
            (void)advance();
            return ast::make_declaration(parse_namespace(annotations, keyword_token));
        case TokenKind::KeywordRecord:
            (void)advance();
            return ast::make_declaration(parse_record(annotations, keyword_token));
        case TokenKind::KeywordEnum:
            (void)advance();
            return ast::make_declaration(parse_enum(annotations, keyword_token));
        case TokenKind::KeywordImport:
            (void)advance();
            return ast::make_declaration(parse_import(keyword_token));
        case TokenKind::At:
            return nullptr;
        default:
            emit_unexpected(keyword_token, "parsing a declaration");
            if (!at_end()) {
                (void)advance();
            }
            return nullptr;
        }
    }

    [[nodiscard]] std::vector<ast::DeclarationPtr> parse_declarations() {
        std::vector<ast::DeclarationPtr> declarations;
        while (!at_end()) {
            skip_invalid_tokens();
            if (at_end() || check(TokenKind::RightBrace)) {
                break;
            }

            if (check(TokenKind::Semicolon)) {
                (void)advance();
                continue;
            }

            if (!is_declaration_start(current().kind)) {
                emit_unexpected(current(), "parsing a declaration");
                (void)advance();
                continue;
            }

            std::unique_ptr<ast::DeclarationSyntax> declaration = parse_declaration();
            if (declaration != nullptr) {
                declarations.push_back(std::move(declaration));
            }
        }
        return declarations;
    }

    [[nodiscard]] std::vector<ast::DeclarationPtr>
    parse_block_declarations(bool (*can_start_declaration)(TokenKind)) {
        std::vector<ast::DeclarationPtr> declarations;
        while (!at_end() && !check(TokenKind::RightBrace)) {
            skip_invalid_tokens();
            if (at_end() || check(TokenKind::RightBrace)) {
                break;
            }
            if (check(TokenKind::Semicolon)) {
                (void)advance();
                continue;
            }
            if (!can_start_declaration(current().kind)) {
                emit_unexpected(current(), "parsing nested declarations");
                (void)advance();
                continue;
            }

            std::unique_ptr<ast::DeclarationSyntax> declaration = parse_declaration();
            if (declaration != nullptr) {
                declarations.push_back(std::move(declaration));
            }
        }
        return declarations;
    }

    [[nodiscard]] support::SourceRange compute_schema_file_range() const {
        if (tokens_.empty()) {
            return support::SourceRange::invalid();
        }

        const Token& first = tokens_.front();
        const Token& last = tokens_.back();
        if (first.kind == TokenKind::EndOfFile && last.kind == TokenKind::EndOfFile) {
            return support::SourceRange(first.source_range.begin(), last.source_range.end());
        }

        for (const Token& token : tokens_) {
            if (token.kind != TokenKind::EndOfFile) {
                return support::SourceRange(token.source_range.begin(),
                                            tokens_.back().source_range.end());
            }
        }

        return support::SourceRange(tokens_.back().source_range.begin(),
                                    tokens_.back().source_range.end());
    }

    diagnostics::DiagnosticEngine& diagnostics_;
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
};

ParseResult Parser::parse(const support::SourceManager& source_manager,
                          support::SourceFileId source_file_id,
                          diagnostics::DiagnosticEngine& diagnostics) {
    ParserImpl parser(source_manager, source_file_id, diagnostics);
    return parser.parse();
}

} // namespace breadcrumbs::compiler::parser
