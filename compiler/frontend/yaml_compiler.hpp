#pragma once

#include "compiler/context/compiler_context.hpp"
#include "compiler/output_planning/output_planning.hpp"
#include "compiler/schema_ir/schema_ir.hpp"

#include <optional>

namespace quarry::compiler::frontend {

struct YamlCompilationResult {
    std::optional<output_planning::OutputPlan> output_plan;
    std::optional<schema_ir::SchemaIrModel> schema_ir;

    [[nodiscard]] bool succeeded() const { return schema_ir.has_value(); }
};

class YamlCompiler {
public:
    [[nodiscard]] YamlCompilationResult
    compile(support::SourceFileId source_file_id, context::CompilerContext& context,
            diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace quarry::compiler::frontend
