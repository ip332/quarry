#pragma once

#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"

#include <string>
#include <vector>

namespace quarry::compiler::output_planning {

struct PlannedSourceUnit {
    std::string source_unit_identity;
    std::string canonical_path;
    std::string namespace_fqn;
    support::SourceRange source_range = support::SourceRange::invalid();
    bool is_root = false;
    bool emits_output = false;
    std::string output_key;
    std::vector<std::string> dependency_identities;
};

struct OutputPlan {
    // Units are in the source graph's deterministic dependency-first order.
    std::vector<PlannedSourceUnit> units;
    // This is the order in which future backend generation should visit units.
    std::vector<std::string> generation_order;
};

class OutputPlanner {
public:
    [[nodiscard]] OutputPlan plan(const context::CompilerContext& context,
                                  diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace quarry::compiler::output_planning
