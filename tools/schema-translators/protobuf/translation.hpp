#pragma once

#include "descriptor_model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quarry::tools::protobuf {

using FieldBounds = DescriptorBounds;

struct ResolvedBounds {
    FieldBounds values;
    std::string source;
    std::string source_type;
    std::vector<std::string> override_chain;
    std::uint32_t source_line = 0;
    std::uint32_t source_column = 0;
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
                                                           const std::string& output_directory,
                                                           const std::string& options_path = {});

} // namespace quarry::tools::protobuf
