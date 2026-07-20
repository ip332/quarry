#include "quarry/telemetry.generated.hpp"

#include <quarry/runtime/version.hpp>

#include <cstdint>
#include <span>
#include <vector>

#ifndef QUARRY_PACKAGE_GENERATED_CODE_API_VERSION
#error "QUARRY_PACKAGE_GENERATED_CODE_API_VERSION must be defined"
#endif

static_assert(::quarry::runtime::kGeneratedCodeApiVersion ==
              QUARRY_PACKAGE_GENERATED_CODE_API_VERSION);

int main() {
    quarry::telemetry::SampleBuilder builder;
    if (!builder.set_count(42U)) {
        return 1;
    }
    const quarry::telemetry::Sample sample = builder.build();

    auto encoded = quarry::telemetry::encode(sample);
    if (!encoded.has_value()) {
        return 2;
    }

    auto decoded = quarry::telemetry::decode_Sample(std::span<const std::byte>(*encoded));
    if (!decoded.has_value()) {
        return 3;
    }
    if (!decoded->has_count() || decoded->count() == nullptr || *decoded->count() != 42U) {
        return 4;
    }

    return 0;
}
