#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/qbs/parser.hpp"
#include "quarry/runtime/qbs_brf_encoder.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace quarry::runtime {

struct QtfParseLimits {
    std::size_t max_input_bytes = 64U * 1024U * 1024U;
    std::size_t max_depth = 1024U;
    std::size_t max_tokens = 1U << 20U;
};

[[nodiscard]] std::optional<BrfRecordInput>
parse_qtf(std::string_view text, const quarry::compiler::qbs::ValidatedQbsView& schema,
          const quarry::compiler::qbs::QbsRecordView& record,
          quarry::compiler::diagnostics::DiagnosticCollection& diagnostics,
          QtfParseLimits limits = {});

} // namespace quarry::runtime
