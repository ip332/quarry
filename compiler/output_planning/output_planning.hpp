#pragma once

#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"

#include <string>
#include <vector>

namespace quarry::compiler::output_planning {

// Generated output is planned per namespace, not per source file: every
// record of a namespace, whichever file declares it, belongs to that
// namespace's generated artifacts. Selection decides only which namespaces
// are emitted; record IDs, Schema IR and QBS always cover the complete import
// closure of the compilation root.

// --generate: the namespaces to emit. Empty means the default for the root:
// a record root emits its own namespace, a configuration root emits every
// namespace of its import closure.
struct GenerationSelection {
    std::vector<std::string> namespaces;
};

struct PlannedSourceUnit {
    std::string source_unit_identity;
    std::string canonical_path;
    std::string namespace_fqn;
    support::SourceRange source_range = support::SourceRange::invalid();
    bool is_root = false;
    // True when the unit's namespace is emitted. All units of one namespace
    // share the value.
    bool emits_output = false;
    std::string output_key;
    std::vector<std::string> dependency_identities;
};

struct PlannedNamespace {
    std::string namespace_fqn;
    bool emits_output = false;
    // Source units contributing records to the namespace, in unit order.
    std::vector<std::string> source_unit_identities;
};

struct OutputPlan {
    // Record-declaring units in the source graph's deterministic
    // dependency-first order. A configuration unit declares nothing and is not
    // listed.
    std::vector<PlannedSourceUnit> units;
    // This is the order in which future backend generation should visit units.
    std::vector<std::string> generation_order;
    // Every namespace of the import closure that declares records, sorted by
    // FQN. Unselected namespaces stay planned so that emitted files can
    // reference their (separately generated) bindings.
    std::vector<PlannedNamespace> namespaces;
    bool root_is_configuration = false;
};

class OutputPlanner {
public:
    [[nodiscard]] OutputPlan plan(const context::CompilerContext& context,
                                  diagnostics::DiagnosticCollection& diagnostics) const;

    [[nodiscard]] OutputPlan plan(const context::CompilerContext& context,
                                  const GenerationSelection& selection,
                                  diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace quarry::compiler::output_planning
