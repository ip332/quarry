#include "fuzz/representative_generated_schema.hpp"

#include "runtime/binary_record.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>

namespace {

[[noreturn]] void fail_invariant() {
    std::abort();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto input = std::as_bytes(std::span<const std::uint8_t>(data, size));
    const auto decoded = breadcrumbs::fuzz_generated::decode_Example_result(input);
    const std::optional<breadcrumbs::fuzz_generated::Example> optional_decoded =
        breadcrumbs::fuzz_generated::decode_Example(input);

    if (decoded.value.has_value() != optional_decoded.has_value()) {
        fail_invariant();
    }
    if (!decoded.value.has_value()) {
        return 0;
    }
    if (decoded.error != breadcrumbs::runtime::DecodeError::none) {
        fail_invariant();
    }

    const auto encoded = breadcrumbs::fuzz_generated::encode_result(*decoded.value);
    if (!encoded.value.has_value()) {
        fail_invariant();
    }
    const auto parsed = breadcrumbs::runtime::parse_record(*encoded.value);
    if (!parsed.record.has_value() || parsed.error != breadcrumbs::runtime::DecodeError::none ||
        parsed.record->record_id != 1U) {
        fail_invariant();
    }

    return 0;
}
