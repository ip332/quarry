#include "compiler/diagnostics/diagnostic.hpp"

int main() {
    breadcrumbs::compiler::diagnostics::DiagnosticCollection diagnostics;
    return diagnostics.has_errors() ? 1 : 0;
}
