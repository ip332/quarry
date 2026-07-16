#include "breadcrumbs/telemetry.generated.hpp"

#include <cstdint>
#include <span>
#include <vector>

int main() {
    breadcrumbs::telemetry::SampleBuilder builder;
    if (!builder.set_count(42U)) {
        return 1;
    }
    const breadcrumbs::telemetry::Sample sample = builder.build();

    auto encoded = breadcrumbs::telemetry::encode(sample);
    if (!encoded.has_value()) {
        return 2;
    }

    auto decoded = breadcrumbs::telemetry::decode_Sample(std::span<const std::byte>(*encoded));
    if (!decoded.has_value()) {
        return 3;
    }
    if (!decoded->has_count() || decoded->count() == nullptr || *decoded->count() != 42U) {
        return 4;
    }

    return 0;
}

