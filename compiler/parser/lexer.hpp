#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/support/source_location.hpp"
#include "compiler/support/source_manager.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace quarry::compiler::parser {

enum class TokenKind {
    Identifier,
    IntegerLiteral,
    StringLiteral,

    KeywordImport,
    KeywordNamespace,
    KeywordRecord,
    KeywordEnum,
    KeywordTrue,
    KeywordFalse,
    KeywordBool,
    KeywordU8,
    KeywordU16,
    KeywordU32,
    KeywordU64,
    KeywordI8,
    KeywordI16,
    KeywordI32,
    KeywordI64,
    KeywordF32,
    KeywordF64,
    KeywordString,
    KeywordBytes,

    LeftBrace,
    RightBrace,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    Colon,
    Semicolon,
    Comma,
    Dot,
    Equals,
    At,

    EndOfFile,
    Invalid,
};

[[nodiscard]] std::string_view token_kind_name(TokenKind kind);

struct Token {
    TokenKind kind = TokenKind::Invalid;
    support::SourceRange source_range;
    std::string_view spelling;
};

class Lexer {
public:
    // The source file id must refer to a source registered in SourceManager.
    // Passing an unknown id is a compiler API violation and throws std::invalid_argument.
    Lexer(const support::SourceManager& source_manager, support::SourceFileId source_file_id,
          diagnostics::DiagnosticEngine& diagnostics);

    [[nodiscard]] Token next_token();
    [[nodiscard]] std::vector<Token> lex_all();

private:
    struct ByteOffset {
        std::size_t value = 0;
    };

    [[nodiscard]] bool is_at_end() const;
    [[nodiscard]] char peek(std::size_t lookahead = 0) const;
    [[nodiscard]] support::SourceLocation location(std::size_t offset) const;
    [[nodiscard]] support::SourceRange range(std::size_t begin, std::size_t end) const;
    [[nodiscard]] Token make_token(TokenKind kind, std::size_t begin, std::size_t end) const;

    void skip_whitespace_and_comments();
    [[nodiscard]] Token lex_identifier_or_keyword();
    [[nodiscard]] Token lex_integer_literal();
    [[nodiscard]] Token lex_string_literal();
    [[nodiscard]] Token lex_punctuation_or_invalid();

    void emit_invalid_character(ByteOffset offset, char character);
    void emit_unterminated_string(ByteOffset begin);
    void emit_invalid_escape(ByteOffset offset, char escaped);

    const support::SourceManager& source_manager_;
    support::SourceFileId source_file_id_;
    diagnostics::DiagnosticEngine& diagnostics_;
    std::string_view source_;
    std::size_t offset_ = 0;
};

} // namespace quarry::compiler::parser
