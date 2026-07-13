#pragma once

#include "compiler/context/compiler_context.hpp"
#include "compiler/schema_ir/schema_ir.hpp"

#include <optional>

namespace breadcrumbs::compiler::frontend {

struct YamlCompilationResult {
    std::optional<schema_ir::SchemaIrModel> schema_ir;

    [[nodiscard]] bool succeeded() const { return schema_ir.has_value(); }
};

class YamlCompiler {
public:
    [[nodiscard]] YamlCompilationResult
    compile(support::SourceFileId source_file_id, context::CompilerContext& context,
            diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace breadcrumbs::compiler::frontend
