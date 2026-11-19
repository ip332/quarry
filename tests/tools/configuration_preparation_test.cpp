// Configuration preparation in quarry-schema-compiler (issue #92): one
// authoritative layout per compilation, identity hashed only when required,
// and both handed unchanged to every backend.

#include "tools/schema_compiler/configuration_preparation.hpp"

#include "compiler/backend/backend.hpp"
#include "compiler/backend_c/backend_c.hpp"
#include "compiler/backend_go/backend_go.hpp"
#include "compiler/backend_python/backend_python.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/frontend/yaml_compiler.hpp"
#include "compiler/qbs/qbs.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace backend = quarry::compiler::backend;
namespace backend_c = quarry::compiler::backend_c;
namespace backend_go = quarry::compiler::backend_go;
namespace backend_python = quarry::compiler::backend_python;
namespace layout = quarry::compiler::layout;
namespace output_planning = quarry::compiler::output_planning;
namespace qbs = quarry::compiler::qbs;
namespace support = quarry::compiler::support;
using quarry::compiler::diagnostics::DiagnosticEngine;
using quarry::compiler::frontend::YamlCompilationResult;
using quarry::compiler::schema_ir::SchemaIrModel;
using quarry::tools::schema_compiler::generate_bindings;
using quarry::tools::schema_compiler::plan_outputs;
using quarry::tools::schema_compiler::prepare_configuration;
using quarry::tools::schema_compiler::PreparedConfiguration;

[[nodiscard]] std::filesystem::path truck_configuration() {
    return std::filesystem::path(QUARRY_TEST_SOURCE_DIR) / "examples" / "truck" / "schemas" /
           "configuration.brd";
}

[[nodiscard]] YamlCompilationResult compile(const std::filesystem::path& path,
                                            std::vector<std::string> generate = {}) {
    quarry::compiler::context::CompilerContext context;
    const auto read = context.file_system().read_text_file(path.string());
    EXPECT_TRUE(read.found) << path;
    const auto file_id = context.source_manager().add_source(
        context.file_system().normalize_path(path.string()), read.text);
    DiagnosticEngine diagnostics;
    YamlCompilationResult result = quarry::compiler::frontend::YamlCompiler{}.compile(
        file_id, context, diagnostics,
        output_planning::GenerationSelection{.namespaces = std::move(generate)});
    EXPECT_TRUE(result.succeeded());
    EXPECT_FALSE(diagnostics.has_errors());
    return result;
}

[[nodiscard]] std::string hex(const support::ConfigurationIdentity& identity) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : identity.schema_id) {
        text.push_back(kDigits[byte >> 4U]);
        text.push_back(kDigits[byte & 0x0FU]);
    }
    return text;
}

[[nodiscard]] PreparedConfiguration prepare(const YamlCompilationResult& compilation,
                                            bool compute_identity) {
    DiagnosticEngine diagnostics;
    auto prepared = prepare_configuration(*compilation.schema_ir, compute_identity, diagnostics);
    EXPECT_TRUE(prepared.has_value());
    EXPECT_FALSE(diagnostics.has_errors());
    return prepared.has_value() ? std::move(*prepared) : PreparedConfiguration{};
}

// Stands in for a backend and records exactly what the driver hands it.
struct RecordingBackend {
    struct Result {
        bool success = true;
        std::string error_message;
    };

    mutable int plan_calls = 0;
    mutable int generate_calls = 0;
    mutable const SchemaIrModel* schema_ir = nullptr;
    mutable const layout::LayoutModel* layout_model = nullptr;
    mutable const output_planning::OutputPlan* output_plan = nullptr;
    mutable backend_c::CodegenOptions options;

    Result plan(const SchemaIrModel& ir, const layout::LayoutModel& layout,
                const backend_c::CodegenOptions& received,
                const output_planning::OutputPlan* plan) const {
        ++plan_calls;
        record(ir, layout, received, plan);
        return {};
    }

    Result generate(const SchemaIrModel& ir, const layout::LayoutModel& layout,
                    const backend_c::CodegenOptions& received,
                    const output_planning::OutputPlan* plan) const {
        ++generate_calls;
        record(ir, layout, received, plan);
        return {};
    }

private:
    void record(const SchemaIrModel& ir, const layout::LayoutModel& layout,
                const backend_c::CodegenOptions& received,
                const output_planning::OutputPlan* plan) const {
        schema_ir = &ir;
        layout_model = &layout;
        options = received;
        output_plan = plan;
    }
};

TEST(ConfigurationPreparationTest, IdentityIsTheTruckGoldenAndMatchesQbsInBothModes) {
    const YamlCompilationResult compilation = compile(truck_configuration());
    const PreparedConfiguration prepared = prepare(compilation, true);
    ASSERT_TRUE(prepared.identity.has_value());
    EXPECT_EQ(prepared.identity->algorithm, support::kConfigurationIdentitySha256_128);
    EXPECT_EQ(hex(*prepared.identity), "9200ca494f010801b830d0857cf66dde");

    for (const qbs::BuildMode mode : {qbs::BuildMode::Minimal, qbs::BuildMode::Reflective}) {
        DiagnosticEngine diagnostics;
        const auto model =
            qbs::QbsModelBuilder{}.build(*compilation.schema_ir, prepared.layout,
                                         qbs::QbsBuildOptions{.mode = mode}, diagnostics);
        ASSERT_TRUE(model.has_value());
        EXPECT_EQ(model->configuration_identity, prepared.identity);
    }
}

TEST(ConfigurationPreparationTest, IdentityIsOnlyComputedWhenRequested) {
    const YamlCompilationResult compilation = compile(truck_configuration());
    const PreparedConfiguration with_identity = prepare(compilation, true);
    const PreparedConfiguration without_identity = prepare(compilation, false);
    EXPECT_TRUE(with_identity.identity.has_value());
    EXPECT_FALSE(without_identity.identity.has_value());
    EXPECT_EQ(without_identity.layout.records.size(), with_identity.layout.records.size());
}

TEST(ConfigurationPreparationTest, LayoutAndIdentityCoverTheCompleteConfigurationNotTheSelection) {
    const YamlCompilationResult complete = compile(truck_configuration());
    const YamlCompilationResult selected = compile(truck_configuration(), {"truck.engine"});
    const PreparedConfiguration complete_prepared = prepare(complete, true);
    const PreparedConfiguration selected_prepared = prepare(selected, true);
    EXPECT_EQ(selected_prepared.identity, complete_prepared.identity);
    ASSERT_EQ(selected_prepared.layout.records.size(), complete_prepared.layout.records.size());
    for (std::size_t index = 0; index < complete_prepared.layout.records.size(); ++index) {
        EXPECT_EQ(selected_prepared.layout.records[index].fqn,
                  complete_prepared.layout.records[index].fqn);
        EXPECT_EQ(selected_prepared.layout.records[index].record_id,
                  complete_prepared.layout.records[index].record_id);
    }
}

TEST(ConfigurationPreparationTest, GenerateBindingsHandsBackendThePreparedIdentityAndLayout) {
    const YamlCompilationResult compilation = compile(truck_configuration());
    PreparedConfiguration prepared = prepare(compilation, true);
    // A deliberately non-derivable identity: the backend must receive exactly
    // the prepared value, not a default or a recomputation.
    prepared.identity->schema_id.fill(0xA5U);
    prepared.identity->schema_id.front() = 0x01U;

    backend_c::CodegenOptions options;
    options.root_file_stem = "configured";
    options.cpp_wrapper = true;
    options.configuration_identity.emplace(); // stale caller value (all-zero schema_id)

    const RecordingBackend recorder;
    const auto result = generate_bindings(recorder, *compilation.schema_ir, prepared, options,
                                          *compilation.output_plan);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(recorder.generate_calls, 1);
    EXPECT_EQ(recorder.plan_calls, 0);
    EXPECT_EQ(recorder.options.configuration_identity, *prepared.identity);
    EXPECT_EQ(recorder.layout_model, &prepared.layout);
    EXPECT_EQ(recorder.schema_ir, &*compilation.schema_ir);
    EXPECT_EQ(recorder.output_plan, &*compilation.output_plan);
    EXPECT_EQ(recorder.options.root_file_stem, "configured");
    EXPECT_TRUE(recorder.options.cpp_wrapper);
}

TEST(ConfigurationPreparationTest, GenerateBindingsRefusesAnUnpreparedIdentity) {
    const YamlCompilationResult compilation = compile(truck_configuration());
    const PreparedConfiguration prepared = prepare(compilation, false);
    const RecordingBackend recorder;
    const auto result = generate_bindings(recorder, *compilation.schema_ir, prepared,
                                          backend_c::CodegenOptions{}, *compilation.output_plan);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, "configuration identity was not computed");
    EXPECT_EQ(recorder.generate_calls, 0);
}

TEST(ConfigurationPreparationTest, PlanOutputsHandsBackendThePreparedLayout) {
    const YamlCompilationResult compilation = compile(truck_configuration());
    const PreparedConfiguration prepared = prepare(compilation, false);
    const RecordingBackend recorder;
    const auto result = plan_outputs(recorder, *compilation.schema_ir, prepared,
                                     backend_c::CodegenOptions{}, *compilation.output_plan);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(recorder.plan_calls, 1);
    EXPECT_EQ(recorder.layout_model, &prepared.layout);
    EXPECT_EQ(recorder.output_plan, &*compilation.output_plan);
}

// The prepared-layout entry points of every real backend variant produce the
// same plan and the same bytes as the self-contained entry points.
template <typename Backend, typename Options>
void expect_prepared_path_matches_self_contained(const Options& options) {
    const YamlCompilationResult compilation = compile(truck_configuration());
    const PreparedConfiguration prepared = prepare(compilation, true);
    const Backend backend;
    Options self_contained_options = options;
    self_contained_options.configuration_identity = *prepared.identity;

    const auto expected_plan =
        backend.plan(*compilation.schema_ir, self_contained_options, &*compilation.output_plan);
    const auto plan =
        plan_outputs(backend, *compilation.schema_ir, prepared, options, *compilation.output_plan);
    ASSERT_TRUE(expected_plan.success) << expected_plan.error_message;
    ASSERT_TRUE(plan.success) << plan.error_message;
    EXPECT_EQ(plan.plan.files.size(), expected_plan.plan.files.size());

    const auto expected =
        backend.generate(*compilation.schema_ir, self_contained_options, &*compilation.output_plan);
    const auto generated = generate_bindings(backend, *compilation.schema_ir, prepared, options,
                                             *compilation.output_plan);
    ASSERT_TRUE(expected.success) << expected.error_message;
    ASSERT_TRUE(generated.success) << generated.error_message;
    ASSERT_EQ(generated.files.size(), expected.files.size());
    ASSERT_FALSE(generated.files.empty());
    for (std::size_t index = 0; index < expected.files.size(); ++index) {
        EXPECT_EQ(generated.files[index].path, expected.files[index].path);
        EXPECT_EQ(generated.files[index].content, expected.files[index].content)
            << expected.files[index].path;
    }
}

TEST(ConfigurationPreparationTest, CppOwningBackendMatchesSelfContainedOutput) {
    expect_prepared_path_matches_self_contained<backend::Backend>(backend::CodegenOptions{});
}

TEST(ConfigurationPreparationTest, CBackendMatchesSelfContainedOutput) {
    expect_prepared_path_matches_self_contained<backend_c::Backend>(backend_c::CodegenOptions{});
}

TEST(ConfigurationPreparationTest, CppCStorageBackendMatchesSelfContainedOutput) {
    backend_c::CodegenOptions options;
    options.cpp_wrapper = true;
    options.header_extension = ".generated.hpp";
    options.source_extension = ".generated.cpp";
    expect_prepared_path_matches_self_contained<backend_c::Backend>(options);
}

TEST(ConfigurationPreparationTest, PythonBackendMatchesSelfContainedOutput) {
    expect_prepared_path_matches_self_contained<backend_python::Backend>(
        backend_python::CodegenOptions{});
}

TEST(ConfigurationPreparationTest, GoBackendMatchesSelfContainedOutput) {
    backend_go::CodegenOptions options;
    options.go_import_prefix = "example.com/truck/gen";
    expect_prepared_path_matches_self_contained<backend_go::Backend>(options);
}

} // namespace
