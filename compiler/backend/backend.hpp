#pragma once

#include "compiler/schema_ir/schema_ir.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace breadcrumbs::compiler::backend {

struct CodegenOptions {
    std::string output_directory = "generated";
    std::string root_file_stem = "schema";
    std::string file_extension = ".generated.hpp";
};

struct GeneratedFile {
    std::string path;
    std::string content;
};

enum class GeneratedLanguage {
    Cpp,
};

enum class GeneratedArtifactRole {
    Header,
};

struct PlannedGeneratedFile {
    GeneratedLanguage language = GeneratedLanguage::Cpp;
    GeneratedArtifactRole role = GeneratedArtifactRole::Header;
    std::string relative_output_path;
    std::string generated_include_path;
};

struct GenerationPlan {
    std::vector<PlannedGeneratedFile> files;
};

struct PlanResult {
    bool success = true;
    std::string error_message;
    GenerationPlan plan;
};

struct CodegenResult {
    bool success = true;
    std::string error_message;
    std::vector<GeneratedFile> files;
};

[[nodiscard]] std::string output_path_for_planned_file(const CodegenOptions& options,
                                                       std::string_view relative_path);

class Backend {
public:
    [[nodiscard]] PlanResult plan(const schema_ir::SchemaIrModel& schema_ir,
                                  const CodegenOptions& options) const;

    [[nodiscard]] CodegenResult generate(const schema_ir::SchemaIrModel& schema_ir,
                                         const CodegenOptions& options) const;
};

} // namespace breadcrumbs::compiler::backend
