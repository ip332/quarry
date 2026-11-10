#include "demo/c.generated.hpp"
#include <array>
#include <cstddef>
#include <iostream>

int main() {
    using Sample = quarry::cxx::demo_c_Sample;
    Sample sample;
    sample.set_count(42);
    if (!sample.set_label("hello")) return 1;
    const std::array<std::byte, 3> payload{std::byte{0}, std::byte{255}, std::byte{16}};
    if (!sample.set_blob(payload)) return 1;
    std::array<std::byte, 128> buffer{};
    const auto encoded = sample.encode(buffer);
    if (encoded.status != QUARRY_C_STATUS_OK) return 1;
    Sample decoded;
    const auto result = decoded.decode(std::span<const std::byte>(buffer).first(encoded.bytes_written));
    if (result.status != QUARRY_C_STATUS_OK || !decoded.has_count() || decoded.count() != 42 ||
        decoded.label() != "hello" || decoded.blob().size() != payload.size()) return 1;
    std::cout << "decoded count: " << decoded.count() << '\n';
}
