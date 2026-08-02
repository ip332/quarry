#include "compiler/output_planning/output_planning.hpp"

#include <cassert>
#include <map>
#include <optional>
#include <set>
#include <string_view>

namespace quarry::compiler::output_planning {
namespace {

constexpr std::string_view output_planning_pass = "output-planning";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

void emit_duplicate_output(diagnostics::DiagnosticCollection& diagnostics,
                           const PlannedSourceUnit& current,
                           const PlannedSourceUnit& previous) {
    diagnostics.emit(
        diagnostics::Diagnostic::create(
            diagnostic_id("BC8001"), diagnostics::Severity::Error,
            "source units '" + current.source_unit_identity + "' and '" +
                previous.source_unit_identity + "' produce the same generated output '" +
                current.output_key + "'")
            .at(current.source_range)
            .with_note("first source unit: " + previous.canonical_path)
            .with_note("second source unit: " + current.canonical_path)
            .from_pass(std::string(output_planning_pass))
            .build());
}

} // namespace

OutputPlan OutputPlanner::plan(const context::CompilerContext& context,
                               diagnostics::DiagnosticCollection& diagnostics) const {
    OutputPlan output_plan;
    output_plan.units.reserve(context.source_units().size());
    output_plan.generation_order.reserve(context.source_units().size());

    std::map<std::string, const PlannedSourceUnit*> output_keys;
    for (const context::SourceUnit& source_unit : context.source_units()) {
        PlannedSourceUnit planned;
        planned.source_unit_identity = source_unit.identity;
        planned.canonical_path = source_unit.canonical_path;
        planned.namespace_fqn = source_unit.namespace_fqn;
        planned.source_range = source_unit.source_range;
        planned.is_root = source_unit.is_root;
        planned.emits_output = source_unit.is_root;
        planned.output_key = source_unit.namespace_fqn.empty() ? "<root>"
                                                               : source_unit.namespace_fqn;

        std::set<std::string> dependency_identities;
        for (const context::SourceUnitImport& import : source_unit.imports) {
            const context::SourceUnit* dependency =
                context.find_source_unit(import.resolved_path);
            if (dependency != nullptr && dependency_identities.insert(dependency->identity).second) {
                planned.dependency_identities.push_back(dependency->identity);
            }
        }

        output_plan.generation_order.push_back(planned.source_unit_identity);
        output_plan.units.push_back(std::move(planned));
        const PlannedSourceUnit& inserted = output_plan.units.back();
        if (inserted.emits_output) {
            const auto [it, inserted_key] = output_keys.emplace(inserted.output_key, &inserted);
            if (!inserted_key) {
                emit_duplicate_output(diagnostics, inserted, *it->second);
            }
        }
    }
    return output_plan;
}

} // namespace quarry::compiler::output_planning
