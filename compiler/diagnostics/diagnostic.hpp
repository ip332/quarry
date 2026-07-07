#pragma once

#include "compiler/support/source_location.hpp"
#include "compiler/support/source_manager.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace breadcrumbs::compiler::diagnostics {

enum class Severity {
    Error,
    Warning,
    Note,
    InternalCompilerError,
};

[[nodiscard]] std::string_view to_string(Severity severity);

class DiagnosticId {
public:
    DiagnosticId() = default;
    explicit DiagnosticId(std::string value);

    [[nodiscard]] static DiagnosticId invalid();
    [[nodiscard]] static std::optional<DiagnosticId> parse(std::string_view value);
    [[nodiscard]] bool is_valid() const;
    [[nodiscard]] const std::string& str() const;

    friend bool operator==(const DiagnosticId& lhs, const DiagnosticId& rhs) = default;

private:
    [[nodiscard]] static bool is_valid_id(std::string_view value);

    std::string value_;
};

class RelatedLocation {
public:
    static RelatedLocation at_location(support::SourceLocation location, std::string message);
    static RelatedLocation at_range(support::SourceRange range, std::string message);

    [[nodiscard]] const std::optional<support::SourceLocation>& location() const;
    [[nodiscard]] const std::optional<support::SourceRange>& range() const;
    [[nodiscard]] const std::string& message() const;

private:
    RelatedLocation(std::optional<support::SourceLocation> location,
                    std::optional<support::SourceRange> range, std::string message);

    std::optional<support::SourceLocation> location_;
    std::optional<support::SourceRange> range_;
    std::string message_;
};

class Diagnostic {
public:
    class Builder {
    public:
        Builder(DiagnosticId id, Severity severity, std::string message);

        Builder& at(support::SourceLocation location);
        Builder& at(support::SourceRange range);
        Builder& with_related(RelatedLocation related_location);
        Builder& with_note(std::string note);
        Builder& with_suggested_fix(std::string suggested_fix);
        Builder& from_pass(std::string compiler_pass);

        [[nodiscard]] Diagnostic build() const;

    private:
        DiagnosticId id_;
        Severity severity_;
        std::string message_;
        std::optional<support::SourceLocation> primary_location_;
        std::optional<support::SourceRange> source_range_;
        std::vector<RelatedLocation> related_locations_;
        std::vector<std::string> notes_;
        std::optional<std::string> suggested_fix_;
        std::string compiler_pass_;
    };

    [[nodiscard]] static Builder create(DiagnosticId id, Severity severity, std::string message);

    [[nodiscard]] const DiagnosticId& id() const;
    [[nodiscard]] Severity severity() const;
    [[nodiscard]] const std::string& message() const;
    [[nodiscard]] const std::optional<support::SourceLocation>& primary_location() const;
    [[nodiscard]] const std::optional<support::SourceRange>& source_range() const;
    [[nodiscard]] const std::vector<RelatedLocation>& related_locations() const;
    [[nodiscard]] const std::vector<std::string>& notes() const;
    [[nodiscard]] const std::optional<std::string>& suggested_fix() const;
    [[nodiscard]] const std::string& compiler_pass() const;

private:
    Diagnostic(DiagnosticId id, Severity severity, std::string message,
               std::optional<support::SourceLocation> primary_location,
               std::optional<support::SourceRange> source_range,
               std::vector<RelatedLocation> related_locations, std::vector<std::string> notes,
               std::optional<std::string> suggested_fix, std::string compiler_pass);

    DiagnosticId id_;
    Severity severity_;
    std::string message_;
    std::optional<support::SourceLocation> primary_location_;
    std::optional<support::SourceRange> source_range_;
    std::vector<RelatedLocation> related_locations_;
    std::vector<std::string> notes_;
    std::optional<std::string> suggested_fix_;
    std::string compiler_pass_;
};

class DiagnosticEngine {
public:
    void emit(Diagnostic diagnostic);
    void add(Diagnostic diagnostic);
    void clear();

    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const;
    [[nodiscard]] const std::vector<Diagnostic>& all() const;
    [[nodiscard]] std::vector<const Diagnostic*>
    sorted_diagnostics(const support::SourceManager& source_manager) const;

    [[nodiscard]] std::size_t error_count() const;
    [[nodiscard]] std::size_t warning_count() const;
    [[nodiscard]] std::size_t internal_error_count() const;
    [[nodiscard]] bool has_fatal_diagnostics() const;
    [[nodiscard]] bool has_errors() const;
    [[nodiscard]] bool empty() const;

private:
    std::vector<Diagnostic> diagnostics_;
};

class DiagnosticFormatter {
public:
    [[nodiscard]] static std::string format(const Diagnostic& diagnostic,
                                            const support::SourceManager& source_manager);
    [[nodiscard]] static std::string format_all(const DiagnosticEngine& engine,
                                                const support::SourceManager& source_manager);
};

using DiagnosticCollection = DiagnosticEngine;

} // namespace breadcrumbs::compiler::diagnostics
