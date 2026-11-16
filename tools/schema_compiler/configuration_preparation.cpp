#include "tools/schema_compiler/configuration_preparation.hpp"

#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"

namespace quarry::tools::schema_compiler {

std::optional<PreparedConfiguration>
prepare_configuration(const compiler::schema_ir::SchemaIrModel& schema_ir, bool compute_identity,
                      compiler::diagnostics::DiagnosticCollection& layout_diagnostics) {
    PreparedConfiguration prepared;
    prepared.layout = compiler::layout::LayoutComputer{}.compute(schema_ir, layout_diagnostics);
    if (layout_diagnostics.has_errors()) {
        return std::nullopt;
    }
    if (compute_identity) {
        prepared.identity = compiler::qbs::compute_configuration_identity(
            compiler::qbs::build_configuration_identity_input(schema_ir, prepared.layout));
    }
    return prepared;
}

} // namespace quarry::tools::schema_compiler
