#include "runtime/binary_record.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <utility>
#include <vector>

namespace {

[[noreturn]] void fail_invariant() {
    std::abort();
}

void verify_parse_success(std::span<const std::byte> input,
                          const breadcrumbs::runtime::ParsedRecord& record) {
    std::uint16_t previous_index = 0U;
    bool have_previous_index = false;
    std::vector<std::pair<const std::byte*, const std::byte*>> ranges;
    ranges.reserve(record.fields.size());

    for (const breadcrumbs::runtime::FieldView& field : record.fields) {
        if (have_previous_index && previous_index >= field.field_index) {
            fail_invariant();
        }
        previous_index = field.field_index;
        have_previous_index = true;

        if (!field.bytes.empty()) {
            const std::byte* input_begin = input.data();
            const std::byte* input_end = input.data() + input.size();
            const std::byte* field_begin = field.bytes.data();
            const std::byte* field_end = field.bytes.data() + field.bytes.size();
            if (field_begin < input_begin || field_begin > input_end || field_end < field_begin ||
                field_end > input_end) {
                fail_invariant();
            }
            ranges.emplace_back(field_begin, field_end);
        }

        const breadcrumbs::runtime::FieldView* found =
            breadcrumbs::runtime::find_field(record, field.field_index);
        if (found == nullptr || found->field_index != field.field_index) {
            fail_invariant();
        }
    }

    std::sort(ranges.begin(), ranges.end());
    for (std::size_t index = 1U; index < ranges.size(); ++index) {
        if (ranges[index - 1U].second > ranges[index].first) {
            fail_invariant();
        }
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto input = std::as_bytes(std::span<const std::uint8_t>(data, size));
    const auto parsed = breadcrumbs::runtime::parse_record(input);
    if (parsed.record.has_value()) {
        if (parsed.error != breadcrumbs::runtime::DecodeError::none) {
            fail_invariant();
        }
        verify_parse_success(input, *parsed.record);
    }
    return 0;
}
