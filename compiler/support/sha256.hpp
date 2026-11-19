#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace quarry::compiler::support {

// SHA-256 (FIPS 180-4) of `input`. Shared by the QBS configuration identity
// (schema_id) and the generated-code identity; neither depends on the other.
[[nodiscard]] std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> input);

} // namespace quarry::compiler::support
