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

[[nodiscard]] support::ConfigurationIdentity
compute_configuration_identity(std::span<const std::uint8_t> identity_input);

[[nodiscard]] support::ConfigurationIdentity
compute_configuration_identity(const QbsImageModel& model);

namespace detail {

[[nodiscard]] bool checked_iss_offset_advance(std::uint64_t identity_size,
                                              std::uint32_t current_offset,
                                              std::uint32_t& next_offset);

} // namespace detail

// The model must carry its previously computed configuration_identity. This
// function writes that value without hashing or changing the identity input.
[[nodiscard]] std::optional<QbsSerializeResult>
serialize_qbs(const QbsImageModel& model, diagnostics::DiagnosticCollection& diagnostics);

} // namespace quarry::compiler::qbs
