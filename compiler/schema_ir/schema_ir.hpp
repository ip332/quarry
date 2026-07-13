#pragma once

#include "breadcrumbs/schema_ir.pb.h"
#include "compiler/ast/ast.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/symbols/symbols.hpp"

namespace breadcrumbs::compiler::schema_ir {

using SchemaIrModel = ::breadcrumbs::schema_ir::SchemaIR;

class SchemaIrBuilder {
public:
    [[nodiscard]] SchemaIrModel
    build(const ast::Ast& ast, const semantic::SemanticModel& semantic_model,
          const layout::LayoutModel& layout_model, const symbols::SymbolTable& symbol_model,
          context::CompilerContext& context, diagnostics::DiagnosticCollection& diagnostics) const;
    [[nodiscard]] SchemaIrModel
    build(const source_schema::NormalizedSourceSchemaDocument& schema,
          const semantic::SemanticModel& semantic_model,
          const layout::LayoutModel& layout_model, context::CompilerContext& context,
          diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace breadcrumbs::compiler::schema_ir
