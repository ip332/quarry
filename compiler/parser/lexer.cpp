#include "compiler/parser/lexer.hpp"

#include <array>
#include <cassert>
#include <cctype>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace breadcrumbs::compiler::parser {
namespace {

struct Keyword {
    std::string_view spelling;
    TokenKind kind;
};

constexpr std::array keywords = {
    Keyword{"import", TokenKind::KeywordImport}, Keyword{"namespace", TokenKind::KeywordNamespace},
    Keyword{"record", TokenKind::KeywordRecord}, Keyword{"enum", TokenKind::KeywordEnum},
    Keyword{"true", TokenKind::KeywordTrue},     Keyword{"false", TokenKind::KeywordFalse},
    Keyword{"bool", TokenKind::KeywordBool},     Keyword{"u8", TokenKind::KeywordU8},
    Keyword{"u16", TokenKind::KeywordU16},       Keyword{"u32", TokenKind::KeywordU32},
    Keyword{"u64", TokenKind::KeywordU64},       Keyword{"i8", TokenKind::KeywordI8},
    Keyword{"i16", TokenKind::KeywordI16},       Keyword{"i32", TokenKind::KeywordI32},
    Keyword{"i64", TokenKind::KeywordI64},       Keyword{"f32", TokenKind::KeywordF32},
    Keyword{"f64", TokenKind::KeywordF64},       Keyword{"string", TokenKind::KeywordString},
    Keyword{"bytes", TokenKind::KeywordBytes},
};

[[nodiscard]] bool is_identifier_start(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_';
}

[[nodiscard]] bool is_identifier_continue(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

[[nodiscard]] bool is_digit(char character) {
    return std::isdigit(static_cast<unsigned char>(character)) != 0;
}

[[nodiscard]] TokenKind keyword_kind(std::string_view spelling) {
    for (const Keyword keyword : keywords) {
        if (keyword.spelling == spelling) {
            return keyword.kind;
        }
    }
    return TokenKind::Identifier;
}

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] std::string describe_character(char character) {
    if (character == '\n') {
        return "\\n";
    }
    if (character == '\r') {
        return "\\r";
    }
    if (character == '\t') {
        return "\\t";
    }
    return std::string(1, character);
}

} // namespace

std::string_view token_kind_name(TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier:
        return "identifier";
    case TokenKind::IntegerLiteral:
        return "integer_literal";
    case TokenKind::StringLiteral:
        return "string_literal";
    case TokenKind::KeywordImport:
        return "import";
    case TokenKind::KeywordNamespace:
        return "namespace";
    case TokenKind::KeywordRecord:
        return "record";
    case TokenKind::KeywordEnum:
        return "enum";
    case TokenKind::KeywordTrue:
        return "true";
    case TokenKind::KeywordFalse:
        return "false";
    case TokenKind::KeywordBool:
        return "bool";
    case TokenKind::KeywordU8:
        return "u8";
    case TokenKind::KeywordU16:
        return "u16";
    case TokenKind::KeywordU32:
        return "u32";
    case TokenKind::KeywordU64:
        return "u64";
    case TokenKind::KeywordI8:
        return "i8";
    case TokenKind::KeywordI16:
        return "i16";
    case TokenKind::KeywordI32:
        return "i32";
    case TokenKind::KeywordI64:
        return "i64";
    case TokenKind::KeywordF32:
        return "f32";
    case TokenKind::KeywordF64:
        return "f64";
    case TokenKind::KeywordString:
        return "string";
    case TokenKind::KeywordBytes:
        return "bytes";
    case TokenKind::LeftBrace:
        return "{";
    case TokenKind::RightBrace:
        return "}";
    case TokenKind::LeftParen:
        return "(";
    case TokenKind::RightParen:
        return ")";
    case TokenKind::LeftBracket:
        return "[";
    case TokenKind::RightBracket:
        return "]";
    case TokenKind::Colon:
        return ":";
    case TokenKind::Semicolon:
        return ";";
    case TokenKind::Comma:
        return ",";
    case TokenKind::Dot:
        return ".";
    case TokenKind::Equals:
        return "=";
    case TokenKind::At:
        return "@";
    case TokenKind::EndOfFile:
        return "eof";
    case TokenKind::Invalid:
        return "invalid";
    }
    return "unknown";
}

Lexer::Lexer(const support::SourceManager& source_manager, support::SourceFileId source_file_id,
             diagnostics::DiagnosticEngine& diagnostics)
    : source_manager_(source_manager), source_file_id_(source_file_id), diagnostics_(diagnostics) {
    const std::optional<std::string_view> source = source_manager_.source_text(source_file_id_);
    if (!source.has_value()) {
        throw std::invalid_argument("Lexer requires a SourceFileId registered in SourceManager");
    }
    source_ = *source;
}

Token Lexer::next_token() {
    skip_whitespace_and_comments();

    if (is_at_end()) {
        return make_token(TokenKind::EndOfFile, offset_, offset_);
    }

    const char current = peek();
    if (is_identifier_start(current)) {
        return lex_identifier_or_keyword();
    }
    if (is_digit(current)) {
        return lex_integer_literal();
    }
    if (current == '"') {
        return lex_string_literal();
    }
    return lex_punctuation_or_invalid();
}

std::vector<Token> Lexer::lex_all() {
    std::vector<Token> tokens;
    while (true) {
        Token token = next_token();
        const bool is_eof = token.kind == TokenKind::EndOfFile;
        tokens.push_back(token);
        if (is_eof) {
            break;
        }
    }
    return tokens;
}

bool Lexer::is_at_end() const { return offset_ >= source_.size(); }

char Lexer::peek(std::size_t lookahead) const {
    const std::size_t index = offset_ + lookahead;
    if (index >= source_.size()) {
        return '\0';
    }
    return source_[index];
}

support::SourceLocation Lexer::location(std::size_t offset) const {
    return support::SourceLocation(source_file_id_, offset);
}

support::SourceRange Lexer::range(std::size_t begin, std::size_t end) const {
    return support::SourceRange(location(begin), location(end));
}

Token Lexer::make_token(TokenKind kind, std::size_t begin, std::size_t end) const {
    return Token{
        .kind = kind,
        .source_range = range(begin, end),
        .spelling = source_.substr(begin, end - begin),
    };
}

void Lexer::skip_whitespace_and_comments() {
    while (!is_at_end()) {
        const char current = peek();
        if (current == ' ' || current == '\t' || current == '\r' || current == '\n') {
            ++offset_;
            continue;
        }

        if (current == '/' && peek(1) == '/') {
            offset_ += 2;
            while (!is_at_end() && peek() != '\n') {
                ++offset_;
            }
            continue;
        }

        break;
    }
}

Token Lexer::lex_identifier_or_keyword() {
    const std::size_t begin = offset_;
    ++offset_;
    while (!is_at_end() && is_identifier_continue(peek())) {
        ++offset_;
    }

    const std::size_t end = offset_;
    const std::string_view spelling = source_.substr(begin, end - begin);
    return make_token(keyword_kind(spelling), begin, end);
}

Token Lexer::lex_integer_literal() {
    const std::size_t begin = offset_;
    ++offset_;
    while (!is_at_end() && is_digit(peek())) {
        ++offset_;
    }
    return make_token(TokenKind::IntegerLiteral, begin, offset_);
}

Token Lexer::lex_string_literal() {
    const std::size_t begin = offset_;
    ++offset_;

    while (!is_at_end()) {
        const char current = peek();
        if (current == '"') {
            ++offset_;
            return make_token(TokenKind::StringLiteral, begin, offset_);
        }

        if (current == '\n' || current == '\r') {
            emit_unterminated_string(ByteOffset{begin});
            return make_token(TokenKind::Invalid, begin, offset_);
        }

        if (current == '\\') {
            const std::size_t escape_offset = offset_;
            ++offset_;
            if (is_at_end()) {
                emit_unterminated_string(ByteOffset{begin});
                return make_token(TokenKind::Invalid, begin, offset_);
            }

            const char escaped = peek();
            if (escaped != '"' && escaped != '\\' && escaped != 'n' && escaped != 'r' &&
                escaped != 't') {
                emit_invalid_escape(ByteOffset{escape_offset}, escaped);
            }
            ++offset_;
            continue;
        }

        ++offset_;
    }

    emit_unterminated_string(ByteOffset{begin});
    return make_token(TokenKind::Invalid, begin, offset_);
}

Token Lexer::lex_punctuation_or_invalid() {
    const std::size_t begin = offset_;
    const char current = peek();
    ++offset_;

    switch (current) {
    case '{':
        return make_token(TokenKind::LeftBrace, begin, offset_);
    case '}':
        return make_token(TokenKind::RightBrace, begin, offset_);
    case '(':
        return make_token(TokenKind::LeftParen, begin, offset_);
    case ')':
        return make_token(TokenKind::RightParen, begin, offset_);
    case '[':
        return make_token(TokenKind::LeftBracket, begin, offset_);
    case ']':
        return make_token(TokenKind::RightBracket, begin, offset_);
    case ':':
        return make_token(TokenKind::Colon, begin, offset_);
    case ';':
        return make_token(TokenKind::Semicolon, begin, offset_);
    case ',':
        return make_token(TokenKind::Comma, begin, offset_);
    case '.':
        return make_token(TokenKind::Dot, begin, offset_);
    case '=':
        return make_token(TokenKind::Equals, begin, offset_);
    case '@':
        return make_token(TokenKind::At, begin, offset_);
    default:
        emit_invalid_character(ByteOffset{begin}, current);
        return make_token(TokenKind::Invalid, begin, offset_);
    }
}

void Lexer::emit_invalid_character(ByteOffset offset, char character) {
    std::ostringstream message;
    message << "invalid character '" << describe_character(character) << "'";
    diagnostics_.emit(diagnostics::Diagnostic::create(diagnostic_id("BC2001"),
                                                      diagnostics::Severity::Error, message.str())
                          .at(range(offset.value, offset.value + 1))
                          .from_pass("lexer")
                          .build());
}

void Lexer::emit_unterminated_string(ByteOffset begin) {
    diagnostics_.emit(diagnostics::Diagnostic::create(diagnostic_id("BC2002"),
                                                      diagnostics::Severity::Error,
                                                      "unterminated string literal")
                          .at(range(begin.value, offset_))
                          .from_pass("lexer")
                          .build());
}

void Lexer::emit_invalid_escape(ByteOffset offset, char escaped) {
    std::ostringstream message;
    message << "invalid escape sequence '\\" << describe_character(escaped) << "'";
    diagnostics_.emit(diagnostics::Diagnostic::create(diagnostic_id("BC2003"),
                                                      diagnostics::Severity::Error, message.str())
                          .at(range(offset.value, offset.value + 2))
                          .from_pass("lexer")
                          .build());
}

} // namespace breadcrumbs::compiler::parser
