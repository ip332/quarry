#include "compiler/schema_ir/schema_ir.hpp"

namespace breadcrumbs::compiler::schema_ir {

SchemaIrModel SchemaIrBuilder::build(const semantic::SemanticModel&, const layout::LayoutModel&,
                                     support::CompilerContext&,
                                     diagnostics::DiagnosticCollection&) const {
    return SchemaIrModel{.backend_ready = true};
}

} // namespace breadcrumbs::compiler::schema_ir
