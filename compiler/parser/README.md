# Parser

Owns lexical scanning and syntax parsing for the current schema language.

## Lexer Responsibility

The lexer converts `SourceManager`-owned source text into syntax tokens. It is
source-syntax infrastructure only. It does not build AST nodes, resolve names,
validate schema semantics, expand imports, or inspect later compiler
representations.

The lexer is constructed from:

* `SourceManager`
* `SourceFileId`
* `DiagnosticEngine`

The `SourceFileId` must identify a source already registered in
`SourceManager`. Passing an unknown id is a compiler API/precondition
violation; the lexer rejects construction with `std::invalid_argument`. This
does not emit a user-facing lexical diagnostic.

It can scan one token at a time or lex the full source file into a token
vector.

## Token Model

Tokens contain:

* token kind
* `SourceRange`
* spelling as `std::string_view` into the source buffer

The source buffer is owned by `SourceManager`; callers must keep that source
text alive while token spellings are used.

Token kinds cover identifiers, integer literals, string literals, keywords,
primitive type keywords, punctuation, end-of-file, and invalid tokens.

## Parser Responsibility

The parser consumes lexer tokens and builds the syntax-oriented AST. It keeps
source ranges on declarations and type syntax where practical and reports syntax
errors through the diagnostics framework.

Current implementation status:

* the normative `.brd` grammar is defined in `docs/specifications/schema-language.md`
* the current parser implementation is transitional scaffolding for that YAML
  grammar
* the repository also contains a separate generic YAML syntax layer under
  `compiler/yaml`; it preserves YAML order and source ranges but does not yet
  decode Breadcrumbs schema vocabulary
* the production-facing YAML orchestration layer now compiles import-free YAML
  through validated Schema IR without a compatibility AST hop after source-
  schema normalization
* the current temporary declaration grammar, annotation parsing, and
  fixed-size array handling are not normative language contracts
* current annotations are transitional string-valued metadata; the normative
  YAML contract uses native typed field properties
* each parser invocation consumes exactly one `SourceFileId` registered in a
  caller-owned `SourceManager`
* parser output is a `ParseResult` containing the parsed AST together with the
  manager-local `SourceFileId` for the source that was parsed
* the parser does not load files or resolve import names to paths

## Source Ranges

Every token has a valid `SourceRange`. The EOF token has a zero-length range at
the end of the source buffer. Tokens do not store line or column values; those
are derived through `SourceManager`.

Parser output uses token ranges to populate AST source metadata. Human-readable
locations are derived through `SourceManager` later.

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

The parser emits syntax diagnostics through `DiagnosticEngine` using the
compiler pass name `parser`. The current parser uses a small set of stable IDs
for unexpected tokens and missing syntax. Diagnostics are intentionally simple
and deterministic.

## Lexer / Parser Boundary

The lexer does not expose parser state or parser recovery behavior. The parser
consumes tokens and decides how to recover from syntax errors.

## Dependency Restrictions

Allowed dependencies:

* C++ standard library
* `compiler/ast`
* `compiler/diagnostics`
* `compiler/support`

Parser code must not depend on imports, symbols, semantic validation, layout
computation, Schema IR construction, backends, or compiler context.
