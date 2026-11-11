#pragma once

#include "compiler/output_planning/output_planning.hpp"
#include "compiler/schema_ir/schema_ir.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quarry::compiler::backend_go {

// Compatibility epoch embedded in every generated Go file as a compile-time
// check against github.com/ip332/quarry/runtime/go/brf's
// GeneratedCodeAPIVersion. Independent of the C, C++ and Python epochs; kept
// in sync with runtime/go/brf/brf.go by hand and checked by a test.
inline constexpr std::uint32_t kGeneratedCodeApiVersionGo = 1U;

// Import path of the Go runtime package generated code depends on.
inline constexpr std::string_view kRuntimeImportPath = "github.com/ip332/quarry/runtime/go/brf";

struct CodegenOptions {
    std::string output_directory = "generated";
    // File stem of each generated file ("<namespace dirs>/<stem>.quarry.go")
    // and the package name of the root (unnamed) namespace.
    std::string root_file_stem = "schema";
    // Go import path corresponding to output_directory, for example
    // "example.com/telemetry/gen". Required only when a generated package
    // refers to a record or enum declared in another namespace.
    std::string go_import_prefix;
};

struct GeneratedFile {
    std::string path;
    std::string content;
};

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

// Schema IR -> Go packages, one directory per namespace that owns records or
// enums. Generated code uses BRF v2 via the Go runtime; see
// docs/design/go-backend.md.
class Backend {
public:
    [[nodiscard]] PlanResult plan(const schema_ir::SchemaIrModel& schema_ir,
                                  const CodegenOptions& options,
                                  const output_planning::OutputPlan* output_plan = nullptr) const;

    [[nodiscard]] CodegenResult
    generate(const schema_ir::SchemaIrModel& schema_ir, const CodegenOptions& options,
             const output_planning::OutputPlan* output_plan = nullptr) const;
};

} // namespace quarry::compiler::backend_go
