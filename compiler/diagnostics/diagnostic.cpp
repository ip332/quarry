#include "compiler/diagnostics/diagnostic.hpp"

#include <utility>

namespace breadcrumbs::compiler::diagnostics {

void DiagnosticCollection::add(Diagnostic diagnostic) {
    diagnostics_.push_back(std::move(diagnostic));
}

const std::vector<Diagnostic>& DiagnosticCollection::all() const {
    return diagnostics_;
}

bool DiagnosticCollection::has_errors() const {
    for (const auto& diagnostic : diagnostics_) {
        if (diagnostic.severity == Severity::Error ||
            diagnostic.severity == Severity::InternalCompilerError) {
            return true;
        }
    }
    return false;
}

}  // namespace breadcrumbs::compiler::diagnostics
