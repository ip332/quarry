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

[[nodiscard]] std::string display_namespace(std::string_view namespace_fqn) {
    return namespace_fqn.empty() ? "<root>" : std::string(namespace_fqn);
}

void emit_unknown_namespace(diagnostics::DiagnosticCollection& diagnostics,
                            support::SourceRange root_range, std::string_view requested,
                            const std::map<std::string, PlannedNamespace>& namespaces) {
    std::string available;
    for (const auto& [namespace_fqn, planned] : namespaces) {
        available += (available.empty() ? "" : ", ") + display_namespace(namespace_fqn);
    }
    auto builder = diagnostics::Diagnostic::create(
                       diagnostic_id("BC8002"), diagnostics::Severity::Error,
                       "cannot generate namespace '" + std::string(requested) +
                           "': no record in the compilation root's import closure declares it")
                       .with_note("namespaces declaring records: " + available)
                       .from_pass(std::string(output_planning_pass));
    if (root_range.is_valid()) {
        builder.at(root_range);
    }
    diagnostics.emit(builder.build());
}

} // namespace

OutputPlan OutputPlanner::plan(const context::CompilerContext& context,
                               diagnostics::DiagnosticCollection& diagnostics) const {
    return plan(context, GenerationSelection{}, diagnostics);
}

OutputPlan OutputPlanner::plan(const context::CompilerContext& context,
                               const GenerationSelection& selection,
                               diagnostics::DiagnosticCollection& diagnostics) const {
    OutputPlan output_plan;
    output_plan.units.reserve(context.source_units().size());
    output_plan.generation_order.reserve(context.source_units().size());

    // Aggregate record-declaring units by namespace.
    std::map<std::string, PlannedNamespace> namespaces;
    std::optional<std::string> root_namespace;
    support::SourceRange root_range = support::SourceRange::invalid();
    for (const context::SourceUnit& source_unit : context.source_units()) {
        if (source_unit.is_root) {
            root_range = source_unit.source_range;
        }
        if (source_unit.is_configuration) {
            output_plan.root_is_configuration = source_unit.is_root;
            continue;
        }
        PlannedNamespace& planned = namespaces[source_unit.namespace_fqn];
        planned.namespace_fqn = source_unit.namespace_fqn;
        planned.source_unit_identities.push_back(source_unit.identity);
        if (source_unit.is_root) {
            root_namespace = source_unit.namespace_fqn;
        }
    }

    // Decide which namespaces are emitted.
    if (!selection.namespaces.empty()) {
        bool valid = true;
        for (const std::string& requested : selection.namespaces) {
            const auto found = namespaces.find(requested);
            if (found == namespaces.end()) {
                emit_unknown_namespace(diagnostics, root_range, requested, namespaces);
                valid = false;
                continue;
            }
            found->second.emits_output = true;
        }
        if (!valid) {
            return output_plan;
        }
    } else if (output_plan.root_is_configuration) {
        for (auto& [namespace_fqn, planned] : namespaces) {
            planned.emits_output = true;
        }
    } else if (root_namespace.has_value()) {
        namespaces.at(*root_namespace).emits_output = true;
    }

    for (const context::SourceUnit& source_unit : context.source_units()) {
        if (source_unit.is_configuration) {
            continue;
        }
        PlannedSourceUnit planned;
        planned.source_unit_identity = source_unit.identity;
        planned.canonical_path = source_unit.canonical_path;
        planned.namespace_fqn = source_unit.namespace_fqn;
        planned.source_range = source_unit.source_range;
        planned.is_root = source_unit.is_root;
        planned.emits_output = namespaces.at(source_unit.namespace_fqn).emits_output;
        planned.output_key = display_namespace(source_unit.namespace_fqn);

        std::set<std::string> dependency_identities;
        for (const context::SourceUnitImport& import : source_unit.imports) {
            const context::SourceUnit* dependency =
                context.find_source_unit(import.resolved_path);
            if (dependency != nullptr && !dependency->is_configuration &&
                dependency_identities.insert(dependency->identity).second) {
                planned.dependency_identities.push_back(dependency->identity);
            }
        }

        output_plan.generation_order.push_back(planned.source_unit_identity);
        output_plan.units.push_back(std::move(planned));
    }

    output_plan.namespaces.reserve(namespaces.size());
    for (auto& [namespace_fqn, planned] : namespaces) {
        output_plan.namespaces.push_back(std::move(planned));
    }
    return output_plan;
}

} // namespace quarry::compiler::output_planning
