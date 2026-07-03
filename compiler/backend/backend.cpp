#include "compiler/backend/backend.hpp"

namespace breadcrumbs::compiler::backend {

BackendResult Backend::generate(const schema_ir::SchemaIrModel&,
                                support::CompilerContext&,
                                diagnostics::DiagnosticCollection&) const {
    return {};
}

}  // namespace breadcrumbs::compiler::backend
