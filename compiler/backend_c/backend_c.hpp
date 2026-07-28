#pragma once

#include "compiler/schema_ir/schema_ir.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace quarry::compiler::backend_c {

struct CodegenOptions {
    std::string output_directory = "generated";
    std::string root_file_stem = "schema";
    std::string header_extension = ".generated.h";
    std::string source_extension = ".generated.c";
};

struct GeneratedFile {
    std::string path;
    std::string content;
};

struct PlannedGeneratedFile {
    std::string relative_header_path;
    std::string relative_source_path;
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

} // namespace quarry::compiler::backend_c
