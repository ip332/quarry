# Parser

Owns lexical analysis and future parsing.

## Lexer Responsibility

The lexer converts `SourceManager`-owned source text into syntax tokens for the
future parser. It is source-syntax infrastructure only. It does not build AST
nodes, resolve names, validate schema semantics, expand imports, or inspect
later compiler representations.

The lexer is constructed from:

* `SourceManager`
* `SourceFileId`
* `DiagnosticEngine`

The `SourceFileId` must identify a source already registered in
`SourceManager`. Passing an unknown id is a compiler API/precondition violation;
the lexer rejects construction with `std::invalid_argument`. This does not emit
a user-facing lexical diagnostic.

It can scan one token at a time or lex the full source file into a token vector.

## Token Model

Tokens contain:

* token kind
* `SourceRange`
* spelling as `std::string_view` into the source buffer

The source buffer is owned by `SourceManager`; callers must keep that source
text alive while token spellings are used.

Token kinds cover identifiers, integer literals, string literals, keywords,
primitive type keywords, punctuation, end-of-file, and invalid tokens.

## Source Ranges

Every token has a valid `SourceRange`. The EOF token has a zero-length range at
the end of the source buffer. Tokens do not store line or column values; those
are derived through `SourceManager`.

## Comments

The lexer skips whitespace and `//` line comments. Comments are not emitted as
tokens.

Block comments are not implemented yet. A `/` that is not part of a `//` line
comment is reported as an invalid character.

## String Literals

The lexer recognizes double-quoted string literals. It accepts the basic escape
sequences:

* `\"`
* `\\`
* `\n`
* `\r`
* `\t`

Invalid escapes are diagnosed but the lexer continues scanning the string.
Unterminated strings are emitted as invalid tokens with diagnostics.

## Diagnostics

The lexer emits diagnostics for:

* invalid character (`BC2001`)
* unterminated string literal (`BC2002`)
* invalid escape sequence (`BC2003`)

Diagnostics use precise source ranges and the compiler pass name `lexer`.

## Lexer / Parser Boundary

The lexer does not expose parser state or parser recovery behavior. The future
parser should consume tokens and decide how to recover from syntax errors.

## Dependency Restrictions

Allowed dependencies:

* C++ standard library
* `compiler/diagnostics`
* `compiler/support`

Parser code may also depend on `compiler/ast`.

Lexer and token code must not depend on imports, symbols, semantic validation,
layout computation, Schema IR construction, backends, or compiler context.
