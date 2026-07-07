#pragma once

#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/schema_ir/schema_ir.hpp"

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
                                         context::CompilerContext& context,
                                         diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace breadcrumbs::compiler::backend
