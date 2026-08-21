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

namespace detail {

[[nodiscard]] bool checked_iss_offset_advance(std::uint64_t identity_size,
                                              std::uint32_t current_offset,
                                              std::uint32_t& next_offset);

} // namespace detail

[[nodiscard]] std::optional<QbsSerializeResult>
serialize_qbs(const QbsImageModel& model, diagnostics::DiagnosticCollection& diagnostics);

} // namespace quarry::compiler::qbs
