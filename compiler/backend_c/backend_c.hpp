#pragma once

#include "compiler/layout/layout.hpp"
#include "compiler/output_planning/output_planning.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/support/configuration_identity.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quarry::compiler::backend_c {

struct CodegenOptions {
    bool optimize_size = false;
    bool cpp_wrapper = false;
    std::string output_directory = "generated";
    std::string root_file_stem = "schema";
    std::string header_extension = ".generated.h";
    std::string source_extension = ".generated.c";
    // Required for generate(), ignored by plan(). The schema compiler computes it
    // once from the resolved configuration; see missing_configuration_identity_message.
    std::optional<support::ConfigurationIdentity> configuration_identity;
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
                                  const CodegenOptions& options,
                                  const output_planning::OutputPlan* output_plan = nullptr) const;

    // Same as above, but uses `layout_model`, the complete BRF layout that the
    // caller already computed from `schema_ir` with layout::LayoutComputer, so
    // one compilation computes its layout once. The layout is only read during
    // the call and must have been computed without errors.
    [[nodiscard]] PlanResult plan(const schema_ir::SchemaIrModel& schema_ir,
                                  const layout::LayoutModel& layout_model,
                                  const CodegenOptions& options,
                                  const output_planning::OutputPlan* output_plan = nullptr) const;

    [[nodiscard]] CodegenResult
    generate(const schema_ir::SchemaIrModel& schema_ir, const CodegenOptions& options,
             const output_planning::OutputPlan* output_plan = nullptr) const;

    // Same as above, but uses `layout_model`, the complete BRF layout that the
    // caller already computed from `schema_ir` with layout::LayoutComputer, so
    // one compilation computes its layout once. The layout is only read during
    // the call and must have been computed without errors.
    [[nodiscard]] CodegenResult
    generate(const schema_ir::SchemaIrModel& schema_ir, const layout::LayoutModel& layout_model,
             const CodegenOptions& options,
             const output_planning::OutputPlan* output_plan = nullptr) const;
};

} // namespace quarry::compiler::backend_c
