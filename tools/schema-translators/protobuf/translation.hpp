#pragma once

#include "descriptor_model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quarry::tools::protobuf {

struct FieldBounds {
    std::optional<std::uint32_t> max_bytes;
    std::optional<std::uint32_t> max_elements;
};

struct BoundsConfig {
    std::vector<std::pair<std::string, FieldBounds>> entries;
};

struct TranslationResult {
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool succeeded() const { return diagnostics.empty(); }
};

[[nodiscard]] TranslationResult load_bounds_config(const std::string& path,
                                                   BoundsConfig& config);

[[nodiscard]] TranslationResult translate_descriptor_model(const DescriptorModel& model,
                                                           const std::string& root,
                                                           const std::string& bounds_path,
                                                           const std::string& output_directory);

} // namespace quarry::tools::protobuf
