#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/context/compiler_context.hpp"

namespace breadcrumbs::compiler::schema_ir {

struct SchemaIrModel {
    bool backend_ready = false;
};

class SchemaIrBuilder {
public:
    [[nodiscard]] SchemaIrModel build(const semantic::SemanticModel& semantic_model,
                                      const layout::LayoutModel& layout_model,
                                      context::CompilerContext& context,
                                      diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace breadcrumbs::compiler::schema_ir
