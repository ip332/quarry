#include <breadcrumbs/runtime/binary_record.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

int main() {
    std::vector<std::byte> field_bytes;
    breadcrumbs::runtime::append_u32(field_bytes, 42U);

    const auto encoded = breadcrumbs::runtime::encode_record_result(
        1U,
        std::vector<breadcrumbs::runtime::FieldBytes>{
            breadcrumbs::runtime::FieldBytes{
                .field_index = 0U,
                .bytes = field_bytes,
            },
        });
    if (!encoded.value.has_value()) {
        std::cerr << "encode failed\n";
        return 1;
    }

    const auto parsed = breadcrumbs::runtime::parse_record(std::span<const std::byte>(*encoded.value));
    if (!parsed.record.has_value()) {
        std::cerr << "decode failed\n";
        return 1;
    }

    const auto* field = breadcrumbs::runtime::find_field(*parsed.record, 0U);
    if (field == nullptr) {
        std::cerr << "field missing\n";
        return 1;
    }

    const auto decoded = breadcrumbs::runtime::read_u32(field->bytes);
    if (!decoded.value.has_value()) {
        std::cerr << "field decode failed\n";
        return 1;
    }

    std::cout << "decoded value: " << *decoded.value << '\n';
    return *decoded.value == 42U ? 0 : 1;
}
