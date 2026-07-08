#pragma once

#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/schema_ir/schema_ir.hpp"

namespace breadcrumbs::compiler::schema_ir {

class SchemaIrValidator {
public:
    void validate(const SchemaIrModel& schema_ir, context::CompilerContext& context,
                  diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace breadcrumbs::compiler::schema_ir
