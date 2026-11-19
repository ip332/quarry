#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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

// Generation entry points receive the identity as
// std::optional<ConfigurationIdentity>: an absent identity is an explicit
// state, never encoded as a special hash value (every 128-bit value, including
// all zeros, is a valid schema_id). Backends reject generation without one,
// after their own schema validation and before rendering any file; planning
// (--list-outputs) never needs one.
[[nodiscard]] inline std::string
missing_configuration_identity_message(std::string_view backend_name) {
    std::string message(backend_name);
    message += ": code generation requires a configuration identity "
               "(CodegenOptions::configuration_identity); compute it from the resolved "
               "configuration, as quarry-schema-compiler does, and pass it explicitly";
    return message;
}

// The 32 lowercase hexadecimal digits of `identity.schema_id`, in QBS header
// byte order, as printed by --print-schema-id. Backends embed this spelling
// (or its uppercase form) in configuration-specific names such as the C++
// inline namespace quarry_cfg_<hex> and C/C++ header guards.
[[nodiscard]] inline std::string schema_id_hex(const ConfigurationIdentity& identity) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(kConfigurationSchemaIdSize * 2U);
    for (const std::uint8_t byte : identity.schema_id) {
        hex.push_back(kHexDigits[byte >> 4U]);
        hex.push_back(kHexDigits[byte & 0x0FU]);
    }
    return hex;
}

// `schema_id_hex` in uppercase, for macro names.
[[nodiscard]] inline std::string schema_id_hex_upper(const ConfigurationIdentity& identity) {
    std::string hex = schema_id_hex(identity);
    for (char& ch : hex) {
        if (ch >= 'a' && ch <= 'f') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    return hex;
}

// Big-endian 32-bit word `index` (0..3) of `identity.schema_id`; word 0 holds
// schema_id bytes 0..3. Generated C and C++ identity macros use these words.
[[nodiscard]] inline std::uint32_t schema_id_word(const ConfigurationIdentity& identity,
                                                  std::size_t index) {
    std::uint32_t word = 0U;
    for (std::size_t offset = 0; offset < 4U; ++offset) {
        word = (word << 8U) | identity.schema_id.at((index * 4U) + offset);
    }
    return word;
}

// Big-endian 64-bit half `index` (0 = high, 1 = low) of `identity.schema_id`,
// for the generated Go QuarrySchemaIDHi/QuarrySchemaIDLo constants.
[[nodiscard]] inline std::uint64_t schema_id_half(const ConfigurationIdentity& identity,
                                                  std::size_t index) {
    return (static_cast<std::uint64_t>(schema_id_word(identity, index * 2U)) << 32U) |
           schema_id_word(identity, (index * 2U) + 1U);
}

} // namespace quarry::compiler::support
