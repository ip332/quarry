#pragma once

#include "descriptor_model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quarry::tools::protobuf {

using FieldBounds = DescriptorBounds;

enum class OptionsFormat {
    Quarry,
    Nanopb,
};

struct BoundEntry {
    std::string path;
    FieldBounds values;
    std::string source;
    std::uint32_t source_line = 0;
    std::string original_option;
};

struct ResolvedBounds {
    FieldBounds values;
    std::string source;
    std::string source_type;
    std::vector<std::string> override_chain;
    std::uint32_t source_line = 0;
    std::uint32_t source_column = 0;
    std::string original_option;
};

struct BoundsConfig {
    std::vector<BoundEntry> entries;
};

struct TranslationResult {
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool succeeded() const { return diagnostics.empty(); }
};

[[nodiscard]] TranslationResult load_bounds_config(const std::string& path,
                                                   BoundsConfig& config);

[[nodiscard]] TranslationResult load_options_config(const std::vector<std::string>& paths,
                                                    OptionsFormat format,
                                                    BoundsConfig& config);

[[nodiscard]] TranslationResult translate_descriptor_model(const DescriptorModel& model,
                                                           const std::string& root,
                                                           const std::string& bounds_path,
                                                           const std::string& output_directory,
                                                           const std::string& options_path = {});

[[nodiscard]] TranslationResult translate_descriptor_model(
    const DescriptorModel& model, const std::string& root,
    const std::vector<std::string>& options_paths, OptionsFormat options_format,
    const std::string& output_directory);

} // namespace quarry::tools::protobuf
