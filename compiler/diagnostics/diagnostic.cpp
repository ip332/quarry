#include "compiler/diagnostics/diagnostic.hpp"

#include <algorithm>
#include <cctype>
#include <numeric>
#include <sstream>
#include <tuple>
#include <utility>

namespace quarry::compiler::diagnostics {
namespace {

constexpr std::string_view unknown_location = "<unknown>";

struct LocationKey {
    bool has_location = false;
    std::string path;
    std::size_t byte_offset = 0;
};

[[nodiscard]] std::optional<support::SourceLocation>
ordering_location(const Diagnostic& diagnostic) {
    if (diagnostic.source_range().has_value() && diagnostic.source_range()->is_valid()) {
        return diagnostic.source_range()->begin();
    }

    if (diagnostic.primary_location().has_value() && diagnostic.primary_location()->is_valid()) {
        return diagnostic.primary_location();
    }

    return std::nullopt;
}

[[nodiscard]] LocationKey location_key(const Diagnostic& diagnostic,
                                       const support::SourceManager& source_manager) {
    const std::optional<support::SourceLocation> location = ordering_location(diagnostic);
    if (!location.has_value()) {
        return {};
    }

    const std::optional<std::string_view> path = source_manager.source_path(location->file_id());
    if (!path.has_value() || !source_manager.is_valid_offset(*location)) {
        return {};
    }

    return LocationKey{
        .has_location = true,
        .path = std::string(*path),
        .byte_offset = location->byte_offset(),
    };
}

[[nodiscard]] std::string format_location(support::SourceLocation location,
                                          const support::SourceManager& source_manager) {
    const std::optional<std::string_view> path = source_manager.source_path(location.file_id());
    const std::optional<support::LineColumn> line_column = source_manager.line_column(location);
    if (!path.has_value() || !line_column.has_value()) {
        return std::string(unknown_location);
    }

    std::ostringstream output;
    output << *path << ':' << line_column->line << ':' << line_column->column;
    return output.str();
}

[[nodiscard]] std::string format_range(support::SourceRange range,
                                       const support::SourceManager& source_manager) {
    if (!range.is_valid()) {
        return std::string(unknown_location);
    }

    const std::optional<std::string_view> path =
        source_manager.source_path(range.begin().file_id());
    const std::optional<support::LineColumn> begin = source_manager.line_column(range.begin());
    const std::optional<support::LineColumn> end = source_manager.line_column(range.end());
    if (!path.has_value() || !begin.has_value() || !end.has_value()) {
        return std::string(unknown_location);
    }

    std::ostringstream output;
    output << *path << ':' << begin->line << ':' << begin->column << '-' << end->line << ':'
           << end->column;
    return output.str();
}

[[nodiscard]] std::string format_primary_location(const Diagnostic& diagnostic,
                                                  const support::SourceManager& source_manager) {
    if (diagnostic.source_range().has_value()) {
        return format_range(*diagnostic.source_range(), source_manager);
    }

    if (diagnostic.primary_location().has_value()) {
        return format_location(*diagnostic.primary_location(), source_manager);
    }

    return std::string(unknown_location);
}

[[nodiscard]] std::string format_related_location(const RelatedLocation& related_location,
                                                  const support::SourceManager& source_manager) {
    if (related_location.range().has_value()) {
        return format_range(*related_location.range(), source_manager);
    }

    if (related_location.location().has_value()) {
        return format_location(*related_location.location(), source_manager);
    }

    return std::string(unknown_location);
}

} // namespace

std::string_view to_string(Severity severity) {
    switch (severity) {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Note:
        return "note";
    case Severity::InternalCompilerError:
        return "internal compiler error";
    }

    return "unknown";
}

DiagnosticId::DiagnosticId(std::string value) {
    if (is_valid_id(value)) {
        value_ = std::move(value);
    }
}

DiagnosticId DiagnosticId::invalid() { return DiagnosticId{}; }

std::optional<DiagnosticId> DiagnosticId::parse(std::string_view value) {
    if (!is_valid_id(value)) {
        return std::nullopt;
    }

    return DiagnosticId(std::string(value));
}

bool DiagnosticId::is_valid() const { return !value_.empty(); }

const std::string& DiagnosticId::str() const { return value_; }

bool DiagnosticId::is_valid_id(std::string_view value) {
    if (value.size() != 6 || value[0] != 'B' || value[1] != 'C') {
        return false;
    }

    return std::all_of(value.begin() + 2, value.end(), [](char character) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0;
    });
}

RelatedLocation RelatedLocation::at_location(support::SourceLocation location,
                                             std::string message) {
    return RelatedLocation(location, std::nullopt, std::move(message));
}

RelatedLocation RelatedLocation::at_range(support::SourceRange range, std::string message) {
    return RelatedLocation(std::nullopt, range, std::move(message));
}

const std::optional<support::SourceLocation>& RelatedLocation::location() const {
    return location_;
}

const std::optional<support::SourceRange>& RelatedLocation::range() const { return range_; }

const std::string& RelatedLocation::message() const { return message_; }

RelatedLocation::RelatedLocation(std::optional<support::SourceLocation> location,
                                 std::optional<support::SourceRange> range, std::string message)
    : location_(location), range_(range), message_(std::move(message)) {}

Diagnostic::Builder::Builder(DiagnosticId id, Severity severity, std::string message)
    : id_(std::move(id)), severity_(severity), message_(std::move(message)) {}

Diagnostic::Builder& Diagnostic::Builder::at(support::SourceLocation location) {
    primary_location_ = location;
    source_range_ = std::nullopt;
    return *this;
}

Diagnostic::Builder& Diagnostic::Builder::at(support::SourceRange range) {
    source_range_ = range;
    primary_location_ = range.begin();
    return *this;
}

Diagnostic::Builder& Diagnostic::Builder::with_related(RelatedLocation related_location) {
    related_locations_.push_back(std::move(related_location));
    return *this;
}

Diagnostic::Builder& Diagnostic::Builder::with_note(std::string note) {
    notes_.push_back(std::move(note));
    return *this;
}

Diagnostic::Builder& Diagnostic::Builder::with_suggested_fix(std::string suggested_fix) {
    suggested_fix_ = std::move(suggested_fix);
    return *this;
}

Diagnostic::Builder& Diagnostic::Builder::from_pass(std::string compiler_pass) {
    compiler_pass_ = std::move(compiler_pass);
    return *this;
}

Diagnostic Diagnostic::Builder::build() const {
    return Diagnostic(id_, severity_, message_, primary_location_, source_range_,
                      related_locations_, notes_, suggested_fix_, compiler_pass_);
}

Diagnostic::Builder Diagnostic::create(DiagnosticId id, Severity severity, std::string message) {
    return Builder(std::move(id), severity, std::move(message));
}

const DiagnosticId& Diagnostic::id() const { return id_; }

Severity Diagnostic::severity() const { return severity_; }

const std::string& Diagnostic::message() const { return message_; }

const std::optional<support::SourceLocation>& Diagnostic::primary_location() const {
    return primary_location_;
}

const std::optional<support::SourceRange>& Diagnostic::source_range() const {
    return source_range_;
}

const std::vector<RelatedLocation>& Diagnostic::related_locations() const {
    return related_locations_;
}

const std::vector<std::string>& Diagnostic::notes() const { return notes_; }

const std::optional<std::string>& Diagnostic::suggested_fix() const { return suggested_fix_; }

const std::string& Diagnostic::compiler_pass() const { return compiler_pass_; }

Diagnostic::Diagnostic(DiagnosticId id, Severity severity, std::string message,
                       std::optional<support::SourceLocation> primary_location,
                       std::optional<support::SourceRange> source_range,
                       std::vector<RelatedLocation> related_locations,
                       std::vector<std::string> notes, std::optional<std::string> suggested_fix,
                       std::string compiler_pass)
    : id_(std::move(id)), severity_(severity), message_(std::move(message)),
      primary_location_(primary_location), source_range_(source_range),
      related_locations_(std::move(related_locations)), notes_(std::move(notes)),
      suggested_fix_(std::move(suggested_fix)), compiler_pass_(std::move(compiler_pass)) {}

void DiagnosticEngine::emit(Diagnostic diagnostic) {
    diagnostics_.push_back(std::move(diagnostic));
}

void DiagnosticEngine::add(Diagnostic diagnostic) { emit(std::move(diagnostic)); }

void DiagnosticEngine::clear() { diagnostics_.clear(); }

const std::vector<Diagnostic>& DiagnosticEngine::diagnostics() const { return diagnostics_; }

const std::vector<Diagnostic>& DiagnosticEngine::all() const { return diagnostics_; }

std::vector<const Diagnostic*>
DiagnosticEngine::sorted_diagnostics(const support::SourceManager& source_manager) const {
    std::vector<std::size_t> sorted_indices(diagnostics_.size());
    std::iota(sorted_indices.begin(), sorted_indices.end(), 0);

    std::stable_sort(
        sorted_indices.begin(), sorted_indices.end(),
        [&](std::size_t lhs_index, std::size_t rhs_index) {
            const Diagnostic& lhs = diagnostics_[lhs_index];
            const Diagnostic& rhs = diagnostics_[rhs_index];
            const LocationKey lhs_location = location_key(lhs, source_manager);
            const LocationKey rhs_location = location_key(rhs, source_manager);

            return std::tuple(!lhs_location.has_location, lhs_location.path,
                              lhs_location.byte_offset, lhs.compiler_pass(), lhs.id().str()) <
                   std::tuple(!rhs_location.has_location, rhs_location.path,
                              rhs_location.byte_offset, rhs.compiler_pass(), rhs.id().str());
        });

    std::vector<const Diagnostic*> sorted;
    sorted.reserve(diagnostics_.size());
    for (std::size_t index : sorted_indices) {
        sorted.push_back(&diagnostics_[index]);
    }

    return sorted;
}

std::size_t DiagnosticEngine::error_count() const {
    return static_cast<std::size_t>(
        std::count_if(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& diagnostic) {
            return diagnostic.severity() == Severity::Error;
        }));
}

std::size_t DiagnosticEngine::warning_count() const {
    return static_cast<std::size_t>(
        std::count_if(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& diagnostic) {
            return diagnostic.severity() == Severity::Warning;
        }));
}

std::size_t DiagnosticEngine::internal_error_count() const {
    return static_cast<std::size_t>(
        std::count_if(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& diagnostic) {
            return diagnostic.severity() == Severity::InternalCompilerError;
        }));
}

bool DiagnosticEngine::has_fatal_diagnostics() const {
    return std::any_of(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity() == Severity::Error ||
               diagnostic.severity() == Severity::InternalCompilerError;
    });
}

bool DiagnosticEngine::has_errors() const { return has_fatal_diagnostics(); }

bool DiagnosticEngine::empty() const { return diagnostics_.empty(); }

std::string DiagnosticFormatter::format(const Diagnostic& diagnostic,
                                        const support::SourceManager& source_manager) {
    std::ostringstream output;
    output << format_primary_location(diagnostic, source_manager) << ": "
           << to_string(diagnostic.severity()) << ' ';

    if (diagnostic.id().is_valid()) {
        output << diagnostic.id().str();
    } else {
        output << "invalid";
    }

    output << ": " << diagnostic.message();
    if (!diagnostic.compiler_pass().empty()) {
        output << " [" << diagnostic.compiler_pass() << ']';
    }

    for (const RelatedLocation& related_location : diagnostic.related_locations()) {
        output << '\n'
               << "  related: " << format_related_location(related_location, source_manager) << ": "
               << related_location.message();
    }

    for (const std::string& note : diagnostic.notes()) {
        output << '\n' << "  note: " << note;
    }

    if (diagnostic.suggested_fix().has_value()) {
        output << '\n' << "  fix: " << *diagnostic.suggested_fix();
    }

    return output.str();
}

std::string DiagnosticFormatter::format_all(const DiagnosticEngine& engine,
                                            const support::SourceManager& source_manager) {
    std::ostringstream output;
    bool first = true;

    for (const Diagnostic* diagnostic : engine.sorted_diagnostics(source_manager)) {
        if (!first) {
            output << '\n';
        }
        first = false;
        output << format(*diagnostic, source_manager);
    }

    return output.str();
}

} // namespace quarry::compiler::diagnostics
