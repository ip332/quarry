#pragma once

#include "quarry/runtime/qbs_brf_reader.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace quarry::runtime {

enum class GenericBrfEncodeError {
    none,
    invalid_schema,
    field_count_mismatch,
    unsupported_type,
    invalid_value,
    invalid_enum,
    overflow,
};

using BrfEncodeValue = std::variant<bool, std::int64_t, std::uint64_t, float, double, std::string,
                                    std::vector<std::uint8_t>>;

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_brf_record(const quarry::compiler::qbs::ValidatedQbsView& schema,
                  const quarry::compiler::qbs::QbsRecordView& record_schema,
                  std::span<const std::optional<BrfEncodeValue>> fields,
                  GenericBrfEncodeError* error = nullptr);

} // namespace quarry::runtime
