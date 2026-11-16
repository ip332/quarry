#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace quarry::compiler::support {

inline constexpr std::uint8_t kConfigurationIdentitySha256_128 = 1U;
inline constexpr std::size_t kConfigurationSchemaIdSize = 16U;

// Compiler-only value passed to backends; generated bindings and runtimes do
// not depend on this header. The bytes use the existing QBS header order.
struct ConfigurationIdentity {
    std::uint8_t algorithm = kConfigurationIdentitySha256_128;
    std::array<std::uint8_t, kConfigurationSchemaIdSize> schema_id{};

    friend bool operator==(const ConfigurationIdentity&, const ConfigurationIdentity&) = default;
};

} // namespace quarry::compiler::support
