#include "quarry/telemetry.generated.hpp"

#include <quarry/runtime/version.hpp>

#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

#ifndef QUARRY_PACKAGE_GENERATED_CODE_API_VERSION
#error "QUARRY_PACKAGE_GENERATED_CODE_API_VERSION must be defined"
#endif

static_assert(::quarry::runtime::kGeneratedCodeApiVersion ==
              QUARRY_PACKAGE_GENERATED_CODE_API_VERSION);

namespace {

// CodecResult carries only a compact error enum, never a message (see
// runtime/README.md, "Diagnostic String Boundary"). Downstream applications
// that want names for logging write their own small mapping, tailored to
// their own conventions, over this fixed enum set — exactly like this.
const char* decode_error_name(::quarry::runtime::DecodeError error) {
    switch (error) {
    case ::quarry::runtime::DecodeError::truncated_header:
        return "truncated_header";
    case ::quarry::runtime::DecodeError::invalid_field_length:
        return "invalid_field_length";
    default:
        return "other";
    }
}

void print_decode_failure(const ::quarry::runtime::DecodeResult<quarry::telemetry::Sample>& result) {
    std::cout << "  error: " << decode_error_name(result.error) << '\n';
    if (result.path.empty()) {
        std::cout << "  path: (empty -- detected before any field was examined)\n";
    } else {
        std::cout << "  path:";
        for (const auto& element : result.path) {
            std::cout << " field_index=" << static_cast<unsigned>(element.field_index);
            if (element.array_index.has_value()) {
                std::cout << " array_index=" << *element.array_index;
            }
        }
        std::cout << '\n';
    }
    std::cout << "  byte_offset: "
              << (result.byte_offset.has_value() ? std::to_string(*result.byte_offset) : "(none)")
              << '\n';
}

} // namespace

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

    // The round trip above used the optional-returning convenience wrappers
    // (`encode`/`decode_Sample`), which collapse any failure to
    // `std::nullopt` and discard why it failed. Applications that need to
    // know why a decode failed -- to log it, report it upstream, or decide
    // how to react -- call the `_result` functions instead, which return the
    // full `quarry::runtime::DecodeResult<Sample>`: `.error`, `.path`, and
    // `.byte_offset`, alongside `.value`.
    //
    // The two cases below decode genuinely corrupted/truncated copies of the
    // valid `encoded` bytes produced above -- not hand-built error objects --
    // the way a real caller would actually encounter them.

    std::cout << "case 1: truncated payload (e.g. a short network read)\n";
    const std::vector<std::byte> truncated(encoded->begin(), encoded->begin() + 4);
    const auto truncated_result =
        quarry::telemetry::decode_Sample_result(std::span<const std::byte>(truncated));
    // A truncated buffer fails before the Field Directory, let alone any
    // field, is examined, so `path` is empty by design: there is no field
    // context yet to report. `byte_offset` still identifies where the
    // problem was detected -- the very start of the record.
    if (truncated_result.value.has_value() ||
        truncated_result.error != ::quarry::runtime::DecodeError::truncated_header ||
        !truncated_result.path.empty() || truncated_result.byte_offset != 0U) {
        return 5;
    }
    print_decode_failure(truncated_result);

    std::cout << "case 2: corrupted field-directory length (e.g. a bit flip in transit)\n";
    std::vector<std::byte> corrupted = *encoded;
    // Byte 18 is the Field Directory's length varuint for field 0 (`count`):
    // 16-byte header + 1-byte field_index + 1-byte field_offset varuint.
    // Overwriting the recorded length (4, for a uint32) with 3 leaves the
    // record otherwise structurally valid, so this reaches the field-level
    // decoder rather than the structural parser.
    corrupted[18] = static_cast<std::byte>(0x03);
    const auto corrupted_result =
        quarry::telemetry::decode_Sample_result(std::span<const std::byte>(corrupted));
    // A field-level failure on an otherwise valid record attaches exactly
    // one path frame identifying the field, and a byte_offset pointing at
    // that field's payload.
    if (corrupted_result.value.has_value() ||
        corrupted_result.error != ::quarry::runtime::DecodeError::invalid_field_length ||
        corrupted_result.path.size() != 1U || corrupted_result.path[0].field_index != 0U ||
        corrupted_result.path[0].array_index.has_value() ||
        corrupted_result.byte_offset != 19U) {
        return 6;
    }
    print_decode_failure(corrupted_result);

    return 0;
}
