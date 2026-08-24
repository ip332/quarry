#pragma once

#include "quarry/runtime/qtf_parser.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace quarry::runtime {

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
import_qtf(std::string_view text, const quarry::compiler::qbs::ValidatedQbsView& schema,
           const quarry::compiler::qbs::QbsRecordView& record,
           quarry::compiler::diagnostics::DiagnosticCollection& diagnostics,
           QtfParseLimits parse_limits = {}, BrfEncodeLimits encode_limits = {});

} // namespace quarry::runtime
