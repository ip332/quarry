#include "compiler/parser/lexer.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::parser::Lexer;
using breadcrumbs::compiler::parser::Token;
using breadcrumbs::compiler::parser::TokenKind;
using breadcrumbs::compiler::support::LineColumn;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceLocation;
using breadcrumbs::compiler::support::SourceManager;
using breadcrumbs::compiler::support::SourceRange;

class LexerTest : public testing::Test {
protected:
    [[nodiscard]] std::vector<Token> lex(std::string text) {
        diagnostics.clear();
        source_manager = SourceManager();
        source_file_id = source_manager.add_source("/test/schema.brd", std::move(text));
        Lexer lexer(source_manager, source_file_id, diagnostics);
        return lexer.lex_all();
    }

    SourceManager source_manager;
    SourceFileId source_file_id;
    DiagnosticEngine diagnostics;
};

[[nodiscard]] std::vector<TokenKind> kinds(const std::vector<Token>& tokens) {
    std::vector<TokenKind> result;
    result.reserve(tokens.size());
    for (const Token& token : tokens) {
        result.push_back(token.kind);
    }
    return result;
}

[[nodiscard]] SourceRange range(SourceFileId source_file_id, std::size_t begin, std::size_t end) {
    return SourceRange(SourceLocation(source_file_id, begin), SourceLocation(source_file_id, end));
}

TEST_F(LexerTest, EmptyInputEmitsEof) {
    const std::vector<Token> tokens = lex("");

    ASSERT_EQ(tokens.size(), 1U);
    EXPECT_EQ(tokens[0].kind, TokenKind::EndOfFile);
    EXPECT_EQ(tokens[0].source_range, range(source_file_id, 0, 0));
    EXPECT_TRUE(tokens[0].spelling.empty());
}

TEST_F(LexerTest, RejectsUnknownSourceFileIdAtConstruction) {
    source_manager = SourceManager();
    diagnostics.clear();

    EXPECT_THROW(
        {
            Lexer lexer(source_manager, SourceFileId(7), diagnostics);
            (void)lexer;
        },
        std::invalid_argument);
    EXPECT_TRUE(diagnostics.empty());
}

TEST_F(LexerTest, LexesIdentifiers) {
    const std::vector<Token> tokens = lex("alpha beta_2 _gamma");

    EXPECT_EQ(kinds(tokens), (std::vector<TokenKind>{
                                 TokenKind::Identifier,
                                 TokenKind::Identifier,
                                 TokenKind::Identifier,
                                 TokenKind::EndOfFile,
                             }));
    EXPECT_EQ(tokens[0].spelling, "alpha");
    EXPECT_EQ(tokens[1].spelling, "beta_2");
    EXPECT_EQ(tokens[2].spelling, "_gamma");
}

TEST_F(LexerTest, LexesKeywords) {
    const std::vector<Token> tokens = lex("import namespace record enum true false");

    EXPECT_EQ(kinds(tokens), (std::vector<TokenKind>{
                                 TokenKind::KeywordImport,
                                 TokenKind::KeywordNamespace,
                                 TokenKind::KeywordRecord,
                                 TokenKind::KeywordEnum,
                                 TokenKind::KeywordTrue,
                                 TokenKind::KeywordFalse,
                                 TokenKind::EndOfFile,
                             }));
}

TEST_F(LexerTest, LexesPrimitiveTypeKeywords) {
    const std::vector<Token> tokens =
        lex("bool u8 u16 u32 u64 i8 i16 i32 i64 f32 f64 string bytes");

    EXPECT_EQ(kinds(tokens), (std::vector<TokenKind>{
                                 TokenKind::KeywordBool,
                                 TokenKind::KeywordU8,
                                 TokenKind::KeywordU16,
                                 TokenKind::KeywordU32,
                                 TokenKind::KeywordU64,
                                 TokenKind::KeywordI8,
                                 TokenKind::KeywordI16,
                                 TokenKind::KeywordI32,
                                 TokenKind::KeywordI64,
                                 TokenKind::KeywordF32,
                                 TokenKind::KeywordF64,
                                 TokenKind::KeywordString,
                                 TokenKind::KeywordBytes,
                                 TokenKind::EndOfFile,
                             }));
}

TEST_F(LexerTest, LexesIntegerLiterals) {
    const std::vector<Token> tokens = lex("0 42 123456");

    EXPECT_EQ(kinds(tokens), (std::vector<TokenKind>{
                                 TokenKind::IntegerLiteral,
                                 TokenKind::IntegerLiteral,
                                 TokenKind::IntegerLiteral,
                                 TokenKind::EndOfFile,
                             }));
    EXPECT_EQ(tokens[0].spelling, "0");
    EXPECT_EQ(tokens[1].spelling, "42");
    EXPECT_EQ(tokens[2].spelling, "123456");
}

TEST_F(LexerTest, LexesPunctuation) {
    const std::vector<Token> tokens = lex("{}()[]:;,.=@");

    EXPECT_EQ(kinds(tokens), (std::vector<TokenKind>{
                                 TokenKind::LeftBrace,
                                 TokenKind::RightBrace,
                                 TokenKind::LeftParen,
                                 TokenKind::RightParen,
                                 TokenKind::LeftBracket,
                                 TokenKind::RightBracket,
                                 TokenKind::Colon,
                                 TokenKind::Semicolon,
                                 TokenKind::Comma,
                                 TokenKind::Dot,
                                 TokenKind::Equals,
                                 TokenKind::At,
                                 TokenKind::EndOfFile,
                             }));
}

TEST_F(LexerTest, SkipsWhitespaceAndTracksNewlinesThroughSourceManager) {
    const std::vector<Token> tokens = lex("  \n\tname");

    ASSERT_GE(tokens.size(), 2U);
    EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[0].source_range, range(source_file_id, 4, 8));
    EXPECT_EQ(source_manager.line_column(tokens[0].source_range.begin()),
              std::optional<LineColumn>({2, 2}));
}

TEST_F(LexerTest, SkipsLineComments) {
    const std::vector<Token> tokens = lex("alpha // skipped\nbeta");

    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[0].spelling, "alpha");
    EXPECT_EQ(tokens[1].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[1].spelling, "beta");
    EXPECT_EQ(tokens[2].kind, TokenKind::EndOfFile);
}

TEST_F(LexerTest, LexesStringLiterals) {
    const std::vector<Token> tokens = lex("\"hello\" \"escaped\\n\"");

    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].spelling, "\"hello\"");
    EXPECT_EQ(tokens[1].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[1].spelling, "\"escaped\\n\"");
    EXPECT_TRUE(diagnostics.empty());
}

TEST_F(LexerTest, EmitsDiagnosticForUnterminatedString) {
    const std::vector<Token> tokens = lex("\"unterminated");

    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens[0].kind, TokenKind::Invalid);
    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    const auto& diagnostic = diagnostics.diagnostics()[0];
    EXPECT_EQ(diagnostic.id().str(), "BC2002");
    EXPECT_EQ(diagnostic.compiler_pass(), "lexer");
    EXPECT_EQ(diagnostic.source_range(), std::optional<SourceRange>(range(source_file_id, 0, 13)));
}

TEST_F(LexerTest, EmitsDiagnosticForInvalidEscapeSequence) {
    const std::vector<Token> tokens = lex("\"bad\\q\"");

    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    const auto& diagnostic = diagnostics.diagnostics()[0];
    EXPECT_EQ(diagnostic.id().str(), "BC2003");
    EXPECT_EQ(diagnostic.source_range(), std::optional<SourceRange>(range(source_file_id, 4, 6)));
}

TEST_F(LexerTest, EmitsDiagnosticForInvalidCharacter) {
    const std::vector<Token> tokens = lex("$");

    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens[0].kind, TokenKind::Invalid);
    EXPECT_EQ(tokens[0].source_range, range(source_file_id, 0, 1));
    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    const auto& diagnostic = diagnostics.diagnostics()[0];
    EXPECT_EQ(diagnostic.id().str(), "BC2001");
    EXPECT_EQ(diagnostic.source_range(), std::optional<SourceRange>(range(source_file_id, 0, 1)));
}

TEST_F(LexerTest, EmitsEofRangeAtEndOfSource) {
    const std::vector<Token> tokens = lex("name");

    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens[1].kind, TokenKind::EndOfFile);
    EXPECT_EQ(tokens[1].source_range, range(source_file_id, 4, 4));
}

TEST_F(LexerTest, PreservesTokenSourceRanges) {
    const std::vector<Token> tokens = lex("record Location");

    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens[0].source_range, range(source_file_id, 0, 6));
    EXPECT_EQ(tokens[1].source_range, range(source_file_id, 7, 15));
}

} // namespace
