#include "quarry/telemetry.generated.hpp"

#include <quarry/runtime/version.hpp>

#include <cstdint>
#include <iostream>
#include <span>

#ifndef QUARRY_PACKAGE_GENERATED_CODE_API_VERSION
#error "QUARRY_PACKAGE_GENERATED_CODE_API_VERSION must be defined"
#endif

static_assert(::quarry::runtime::kGeneratedCodeApiVersion ==
              QUARRY_PACKAGE_GENERATED_CODE_API_VERSION);

int main() {
    quarry::shared::ChildBuilder child_builder;
    if (!child_builder.set_value(7U)) {
        return 1;
    }

    quarry::telemetry::SampleBuilder builder;
    if (!builder.set_count(42U)) {
        return 2;
    }
    if (!builder.set_child(child_builder.build())) {
        return 3;
    }
    const quarry::telemetry::Sample sample = builder.build();

    auto encoded = quarry::telemetry::encode(sample);
    if (!encoded.has_value()) {
        return 4;
    }

    auto decoded = quarry::telemetry::decode_Sample(std::span<const std::byte>(*encoded));
    if (!decoded.has_value()) {
        return 5;
    }
    if (!decoded->has_count() || decoded->count() == nullptr || *decoded->count() != 42U ||
        !decoded->has_child() || decoded->child() == nullptr || !decoded->child()->has_value() ||
        decoded->child()->value() == nullptr || *decoded->child()->value() != 7U) {
        return 6;
    }

    std::cout << "decoded count: " << *decoded->count() << "\n";

    return 0;
}
