#include <breadcrumbs/runtime/binary_record.hpp>
#include <breadcrumbs/runtime/version.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#ifndef BREADCRUMBS_PACKAGE_GENERATED_CODE_API_VERSION
#error "BREADCRUMBS_PACKAGE_GENERATED_CODE_API_VERSION must be defined"
#endif

int main() {
    static_assert(breadcrumbs::runtime::kGeneratedCodeApiVersion ==
                  BREADCRUMBS_PACKAGE_GENERATED_CODE_API_VERSION);

    using breadcrumbs::runtime::DecodeError;
    using breadcrumbs::runtime::FieldBytes;
    using breadcrumbs::runtime::append_u32;
    using breadcrumbs::runtime::encode_record_result;
    using breadcrumbs::runtime::find_field;
    using breadcrumbs::runtime::parse_record;
    using breadcrumbs::runtime::read_u32;

    std::vector<std::byte> field_bytes;
    append_u32(field_bytes, 0x01020304U);

    const auto encoded = encode_record_result(7U, std::vector<FieldBytes>{
                                                      FieldBytes{
                                                          .field_index = 0U,
                                                          .bytes = field_bytes,
                                                      },
                                                  });
    if (!encoded.value.has_value()) {
        return 1;
    }

    const auto parsed = parse_record(std::span<const std::byte>(*encoded.value));
    if (!parsed.record.has_value() || parsed.error != DecodeError::none) {
        return 2;
    }
    if (parsed.record->record_id != 7U) {
        return 3;
    }

    const auto* field = find_field(*parsed.record, 0U);
    if (field == nullptr) {
        return 4;
    }

    const auto decoded = read_u32(field->bytes);
    if (!decoded.value.has_value() || *decoded.value != 0x01020304U) {
        return 5;
    }

    return 0;
}
