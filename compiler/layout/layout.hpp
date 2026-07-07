#pragma once

#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/semantic/semantic.hpp"

#include <cstdint>
#include <vector>

namespace breadcrumbs::compiler::layout {

struct FieldLayout {
    std::uint32_t field_index = 0;
};

struct RecordLayout {
    std::uint32_t record_id = 0;
    std::vector<FieldLayout> fields;
};

struct LayoutModel {
    std::vector<RecordLayout> records;
};

class LayoutComputer {
public:
    [[nodiscard]] LayoutModel compute(const semantic::SemanticModel& semantic_model,
                                      context::CompilerContext& context,
                                      diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace breadcrumbs::compiler::layout
