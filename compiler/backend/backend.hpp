#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/support/compiler_context.hpp"

#include <string>
#include <vector>

namespace breadcrumbs::compiler::backend {

struct BackendArtifact {
    std::string path;
};

struct BackendResult {
    std::vector<BackendArtifact> artifacts;
};

class Backend {
public:
    [[nodiscard]] BackendResult generate(const schema_ir::SchemaIrModel& schema_ir,
                                         support::CompilerContext& context,
                                         diagnostics::DiagnosticCollection& diagnostics) const;
};

}  // namespace breadcrumbs::compiler::backend
