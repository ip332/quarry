#pragma once

#include "compiler/schema_ir/schema_ir.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace quarry::compiler::backend_python {

struct CodegenOptions {
    std::string output_directory = "generated";
    std::string root_module_stem = "schema";
};

struct GeneratedFile {
    std::string path;
    std::string content;
};

// Python has no header/source split and no include mechanism analogous to
// C/C++ -- every planned file is exactly one path (either a namespace's own
// generated module, or an ancestor package's __init__.py). See
// docs/design/python-backend.md for why this is a single flat list rather
// than mirroring backend_c's header/source pair.
struct PlannedGeneratedFile {
    std::string relative_output_path;
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

} // namespace quarry::compiler::backend_python
