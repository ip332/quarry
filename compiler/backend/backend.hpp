#pragma once

#include "compiler/schema_ir/schema_ir.hpp"

#include <string>
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

struct CodegenResult {
    bool success = true;
    std::string error_message;
    std::vector<GeneratedFile> files;
};

class Backend {
public:
    [[nodiscard]] CodegenResult generate(const schema_ir::SchemaIrModel& schema_ir,
                                         const CodegenOptions& options) const;
};

} // namespace breadcrumbs::compiler::backend
