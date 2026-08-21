#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/qbs/qbs.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace quarry::compiler::qbs {

struct QbsSerializeResult {
    std::vector<std::uint8_t> bytes;
};

[[nodiscard]] std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> input);

[[nodiscard]] std::optional<QbsSerializeResult>
serialize_qbs(const QbsImageModel& model, diagnostics::DiagnosticCollection& diagnostics);

} // namespace quarry::compiler::qbs
