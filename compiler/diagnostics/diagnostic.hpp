#pragma once

#include "compiler/support/source_location.hpp"

#include <string>
#include <vector>

namespace breadcrumbs::compiler::diagnostics {

enum class Severity {
    Error,
    Warning,
    Note,
    InternalCompilerError,
};

struct Diagnostic {
    std::string id;
    Severity severity = Severity::Error;
    std::string message;
    support::SourceLocation primary_location;
    std::vector<support::SourceLocation> related_locations;
    std::string pass;
};

class DiagnosticCollection {
public:
    void add(Diagnostic diagnostic);
    [[nodiscard]] const std::vector<Diagnostic>& all() const;
    [[nodiscard]] bool has_errors() const;

private:
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace breadcrumbs::compiler::diagnostics
