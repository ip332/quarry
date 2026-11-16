#pragma once

// Per-invocation preparation of the resolved configuration, shared by every
// consumer of one compilation (docs/compiler-architecture.md, "Configuration
// preparation"):
//
//   resolved configuration (Schema IR)
//     -> authoritative BRF layout            (computed once)
//     -> configuration identity              (hashed once, only when required)
//     -> backend generation or QBS construction, all reading the same layout
//
// Internal to quarry-schema-compiler; not installed and not a public API.

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/output_planning/output_planning.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/support/configuration_identity.hpp"

#include <optional>

namespace quarry::tools::schema_compiler {

struct PreparedConfiguration {
    // Complete BRF layout of the resolved configuration, never a --generate
    // subset. Owned here for the whole invocation; consumers only borrow it.
    compiler::layout::LayoutModel layout;
    // Present only when preparation was asked to compute it.
    std::optional<compiler::support::ConfigurationIdentity> identity;
};

// Computes the authoritative layout of `schema_ir` and, when
// `compute_identity` is true, its configuration identity from the canonical
// QBS identity input. Returns nullopt when layout computation fails; the
// layout errors are then in `layout_diagnostics`.
[[nodiscard]] std::optional<PreparedConfiguration>
prepare_configuration(const compiler::schema_ir::SchemaIrModel& schema_ir, bool compute_identity,
                      compiler::diagnostics::DiagnosticCollection& layout_diagnostics);

// Plans a backend's output from the prepared layout. Identity is not needed to
// plan output paths.
template <typename Backend, typename Options>
[[nodiscard]] auto plan_outputs(const Backend& backend,
                                const compiler::schema_ir::SchemaIrModel& schema_ir,
                                const PreparedConfiguration& prepared, const Options& options,
                                const compiler::output_planning::OutputPlan& output_plan) {
    return backend.plan(schema_ir, prepared.layout, options, &output_plan);
}

// Generates a backend's bindings from the prepared layout, with the prepared
// configuration identity as the backend's CodegenOptions::configuration_identity.
// Fails without calling the backend when the identity was not prepared.
template <typename Backend, typename Options>
[[nodiscard]] auto generate_bindings(const Backend& backend,
                                     const compiler::schema_ir::SchemaIrModel& schema_ir,
                                     const PreparedConfiguration& prepared, Options options,
                                     const compiler::output_planning::OutputPlan& output_plan) {
    using Result = decltype(backend.generate(schema_ir, prepared.layout, options, &output_plan));
    if (!prepared.identity.has_value()) {
        Result result;
        result.success = false;
        result.error_message = "configuration identity was not computed";
        return result;
    }
    options.configuration_identity = *prepared.identity;
    return backend.generate(schema_ir, prepared.layout, options, &output_plan);
}

} // namespace quarry::tools::schema_compiler
