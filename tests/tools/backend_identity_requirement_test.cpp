// Every backend variant requires an explicit configuration identity to
// generate code (issue #113): an absent identity is rejected without producing
// any file, planning never needs one, validation diagnostics keep precedence,
// and a supplied identity, including an all-zero one, is embedded exactly.

#include "tools/schema_compiler/configuration_preparation.hpp"

#include "compiler/backend/backend.hpp"
#include "compiler/backend_c/backend_c.hpp"
#include "compiler/backend_go/backend_go.hpp"
#include "compiler/backend_python/backend_python.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/frontend/yaml_compiler.hpp"
#include "compiler/support/configuration_identity.hpp"
#include "tests/support/test_configuration_identity.hpp"

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
namespace output_planning = quarry::compiler::output_planning;
namespace support = quarry::compiler::support;
using quarry::compiler::diagnostics::DiagnosticEngine;
using quarry::compiler::frontend::YamlCompilationResult;
using quarry::compiler::schema_ir::SchemaIrModel;
using quarry::tools::schema_compiler::generate_bindings;
using quarry::tools::schema_compiler::prepare_configuration;
using quarry::tools::schema_compiler::PreparedConfiguration;

[[nodiscard]] YamlCompilationResult compile_truck() {
    const std::filesystem::path path = std::filesystem::path(QUARRY_TEST_SOURCE_DIR) / "examples" /
                                       "truck" / "schemas" / "configuration.brd";
    quarry::compiler::context::CompilerContext context;
    const auto read = context.file_system().read_text_file(path.string());
    EXPECT_TRUE(read.found) << path;
    const auto file_id = context.source_manager().add_source(
        context.file_system().normalize_path(path.string()), read.text);
    DiagnosticEngine diagnostics;
    YamlCompilationResult result =
        quarry::compiler::frontend::YamlCompiler{}.compile(file_id, context, diagnostics);
    EXPECT_TRUE(result.succeeded());
    return result;
}

[[nodiscard]] std::string hex32(std::uint32_t word) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (int shift = 28; shift >= 0; shift -= 4) {
        text.push_back(kDigits[(word >> static_cast<unsigned>(shift)) & 0x0FU]);
    }
    return text;
}

// One entry per generated-code variant: how to configure it, the name its
// rejection message uses, and how its output spells a given identity.
struct CppOwning {
    using Backend = backend::Backend;
    using Options = backend::CodegenOptions;
    static constexpr const char* kName = "backend codegen";
    static Options options() { return {}; }
    static std::string marker(const support::ConfigurationIdentity& identity) {
        return "inline namespace quarry_cfg_" + support::schema_id_hex(identity) + " {";
    }
};

struct PlainC {
    using Backend = backend_c::Backend;
    using Options = backend_c::CodegenOptions;
    static constexpr const char* kName = "backend_c";
    static Options options() { return {}; }
    static std::string marker(const support::ConfigurationIdentity& identity) {
        return "_QUARRY_SCHEMA_ID_W0 0x" + hex32(support::schema_id_word(identity, 0)) + "UL";
    }
};

struct CppCStorage {
    using Backend = backend_c::Backend;
    using Options = backend_c::CodegenOptions;
    static constexpr const char* kName = "backend_c";
    static Options options() {
        Options options;
        options.cpp_wrapper = true;
        options.header_extension = ".generated.hpp";
        options.source_extension = ".generated.cpp";
        return options;
    }
    static std::string marker(const support::ConfigurationIdentity& identity) {
        return "inline namespace quarry_cfg_" + support::schema_id_hex(identity) + " {";
    }
};

struct Python {
    using Backend = backend_python::Backend;
    using Options = backend_python::CodegenOptions;
    static constexpr const char* kName = "backend_python";
    static Options options() { return {}; }
    static std::string marker(const support::ConfigurationIdentity& identity) {
        return "QUARRY_SCHEMA_ID = bytes.fromhex(\"" + support::schema_id_hex(identity) + "\")";
    }
};

struct Go {
    using Backend = backend_go::Backend;
    using Options = backend_go::CodegenOptions;
    static constexpr const char* kName = "backend_go";
    static Options options() {
        Options options;
        options.go_import_prefix = "example.com/truck/gen";
        return options;
    }
    static std::string marker(const support::ConfigurationIdentity& identity) {
        return "const QuarrySchemaIDHi uint64 = 0x" + hex32(support::schema_id_word(identity, 0)) +
               hex32(support::schema_id_word(identity, 1));
    }
};

template <typename Variant> class BackendIdentityRequirementTest : public ::testing::Test {};
using Variants = ::testing::Types<CppOwning, PlainC, CppCStorage, Python, Go>;
TYPED_TEST_SUITE(BackendIdentityRequirementTest, Variants);

template <typename Result>
void expect_rejected_without_output(const Result& result, const char* backend_name) {
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, support::missing_configuration_identity_message(backend_name));
    EXPECT_TRUE(result.files.empty()) << "a rejected generation must not produce any file";
}

template <typename Result>
[[nodiscard]] std::size_t files_containing(const Result& result, const std::string& text) {
    std::size_t count = 0;
    for (const auto& file : result.files) {
        if (file.content.find(text) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

TYPED_TEST(BackendIdentityRequirementTest, DefaultOptionsCarryNoIdentity) {
    EXPECT_FALSE(TypeParam::options().configuration_identity.has_value());
}

TYPED_TEST(BackendIdentityRequirementTest, SelfContainedGenerationRejectsMissingIdentity) {
    const YamlCompilationResult compilation = compile_truck();
    const typename TypeParam::Backend backend;
    expect_rejected_without_output(
        backend.generate(*compilation.schema_ir, TypeParam::options(), &*compilation.output_plan),
        TypeParam::kName);
}

TYPED_TEST(BackendIdentityRequirementTest, PreparedLayoutGenerationRejectsMissingIdentity) {
    const YamlCompilationResult compilation = compile_truck();
    DiagnosticEngine diagnostics;
    const auto prepared = prepare_configuration(*compilation.schema_ir, false, diagnostics);
    ASSERT_TRUE(prepared.has_value());
    const typename TypeParam::Backend backend;
    expect_rejected_without_output(backend.generate(*compilation.schema_ir, prepared->layout,
                                                    TypeParam::options(),
                                                    &*compilation.output_plan),
                                   TypeParam::kName);
}

TYPED_TEST(BackendIdentityRequirementTest, PlanningNeedsNoIdentity) {
    const YamlCompilationResult compilation = compile_truck();
    const typename TypeParam::Backend backend;
    const auto plan =
        backend.plan(*compilation.schema_ir, TypeParam::options(), &*compilation.output_plan);
    ASSERT_TRUE(plan.success) << plan.error_message;
    EXPECT_FALSE(plan.plan.files.empty());

    // The plan is the same with an identity: identity never affects paths.
    const auto with_identity = backend.plan(
        *compilation.schema_ir, quarry::testing::with_test_identity(TypeParam::options()),
        &*compilation.output_plan);
    ASSERT_TRUE(with_identity.success) << with_identity.error_message;
    EXPECT_EQ(with_identity.plan.files.size(), plan.plan.files.size());
}

// A record whose only field has no type: every backend rejects it while
// computing layout or planning, before any identity is needed.
[[nodiscard]] SchemaIrModel make_untyped_field_schema_ir() {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    auto* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    root->set_name("");
    root->set_fqn("");
    auto* record = root->add_records();
    record->set_ir_id(2);
    record->set_name("Broken");
    record->set_fqn("Broken");
    auto* field = record->add_fields();
    field->set_name("missing");
    field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_UNSPECIFIED);
    return schema_ir;
}

TYPED_TEST(BackendIdentityRequirementTest, ValidationDiagnosticsTakePrecedenceOverMissingIdentity) {
    // The schema compiler relies on this when layout preparation fails: the
    // backend's established diagnostic, not the identity requirement, is
    // reported.
    const SchemaIrModel invalid = make_untyped_field_schema_ir();
    const typename TypeParam::Backend backend;
    const auto result = backend.generate(invalid, TypeParam::options());
    const auto with_identity =
        backend.generate(invalid, quarry::testing::with_test_identity(TypeParam::options()));
    ASSERT_FALSE(with_identity.success) << "the fixture must be invalid for every backend";
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, with_identity.error_message);
    EXPECT_NE(result.error_message,
              support::missing_configuration_identity_message(TypeParam::kName));
    EXPECT_TRUE(result.files.empty());
}

TYPED_TEST(BackendIdentityRequirementTest, SuppliedIdentityIsEmbeddedExactly) {
    const YamlCompilationResult compilation = compile_truck();
    const typename TypeParam::Backend backend;
    support::ConfigurationIdentity other = quarry::testing::kTestConfigurationIdentity;
    other.schema_id.fill(0xC3U);
    for (const support::ConfigurationIdentity& identity :
         {quarry::testing::kTestConfigurationIdentity, other}) {
        auto options = TypeParam::options();
        options.configuration_identity = identity;
        const auto result =
            backend.generate(*compilation.schema_ir, options, &*compilation.output_plan);
        ASSERT_TRUE(result.success) << result.error_message;
        EXPECT_GT(files_containing(result, TypeParam::marker(identity)), 0U)
            << TypeParam::marker(identity);
    }
}

TYPED_TEST(BackendIdentityRequirementTest, AllZeroIdentityIsAValidIdentity) {
    // Every 128-bit value is a possible schema_id; absence is expressed only by
    // an empty optional, never by a sentinel value.
    const YamlCompilationResult compilation = compile_truck();
    const typename TypeParam::Backend backend;
    auto options = TypeParam::options();
    options.configuration_identity.emplace();
    const auto result =
        backend.generate(*compilation.schema_ir, options, &*compilation.output_plan);
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_GT(files_containing(result, TypeParam::marker(support::ConfigurationIdentity{})), 0U);
}

TYPED_TEST(BackendIdentityRequirementTest, DriverPropagatesThePreparedIdentity) {
    // The schema compiler's driver supplies the identity it prepared once;
    // its output embeds that identity, which is the configuration's schema_id.
    const YamlCompilationResult compilation = compile_truck();
    DiagnosticEngine diagnostics;
    const auto prepared = prepare_configuration(*compilation.schema_ir, true, diagnostics);
    ASSERT_TRUE(prepared.has_value());
    ASSERT_TRUE(prepared->identity.has_value());
    EXPECT_EQ(support::schema_id_hex(*prepared->identity), "9200ca494f010801b830d0857cf66dde");
    const typename TypeParam::Backend backend;
    const auto result = generate_bindings(backend, *compilation.schema_ir, *prepared,
                                          TypeParam::options(), *compilation.output_plan);
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_GT(files_containing(result, TypeParam::marker(*prepared->identity)), 0U);
}

TEST(BackendIdentityRequirementMessageTest, NamesTheBackendAndTheRemedy) {
    EXPECT_EQ(support::missing_configuration_identity_message("backend_c"),
              "backend_c: code generation requires a configuration identity "
              "(CodegenOptions::configuration_identity); compute it from the resolved "
              "configuration, as quarry-schema-compiler does, and pass it explicitly");
}

} // namespace
