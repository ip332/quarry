#pragma once

#include "compiler/support/configuration_identity.hpp"

// Explicit configuration identity for tests that call a backend directly
// instead of going through quarry-schema-compiler (which computes the real
// identity once per invocation). Backends reject generation without an
// identity (#113), so every direct call names this value on purpose.
//
// The bytes are distinctive and are not the schema_id of any fixture, so
// expected output shows exactly the identity the test supplied: a backend that
// embedded a default or recomputed value would not match.
namespace quarry::testing {

inline constexpr compiler::support::ConfigurationIdentity kTestConfigurationIdentity{
    .algorithm = compiler::support::kConfigurationIdentitySha256_128,
    .schema_id = {0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U, 0x99U, 0xAAU,
                  0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU},
};

// Lowercase hex spelling of kTestConfigurationIdentity, as embedded in C++
// inline namespaces (quarry_cfg_<hex>) and, uppercased, in header guards.
inline constexpr const char* kTestSchemaIdHex = "00112233445566778899aabbccddeeff";

// `options` (any backend's CodegenOptions) carrying kTestConfigurationIdentity.
template <typename Options> [[nodiscard]] Options with_test_identity(Options options) {
    options.configuration_identity = kTestConfigurationIdentity;
    return options;
}

} // namespace quarry::testing
