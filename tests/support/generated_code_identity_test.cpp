// Generated-code identity (#111 G1): the versioned canonical encoding, its
// SHA-256/128, and the structured renderer placeholders.
//
// The pinned vectors were computed independently from the encoding
// specification in generated_code_identity.hpp (a separate Python
// implementation using hashlib), not from this implementation. A change to any
// of them is a change of the format and needs a new format version.

#include "compiler/support/generated_code_identity.hpp"
#include "compiler/support/sha256.hpp"
#include "tests/support/test_configuration_identity.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace support = quarry::compiler::support;
using support::GeneratedCodeIdentity;
using support::GeneratedCodeIdentityDependency;
using support::GeneratedCodeIdentityFile;
using support::GeneratedCodeIdentityInput;
using support::GeneratedCodeVariant;
using support::GeneratedFileRole;

[[nodiscard]] std::string hex(const std::vector<std::uint8_t>& bytes) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : bytes) {
        text.push_back(kDigits[byte >> 4U]);
        text.push_back(kDigits[byte & 0x0FU]);
    }
    return text;
}

[[nodiscard]] GeneratedCodeIdentity filled(std::uint8_t value) {
    GeneratedCodeIdentity identity;
    identity.bytes.fill(value);
    return identity;
}

[[nodiscard]] std::string gid_of(const GeneratedCodeIdentityInput& input) {
    std::string error;
    const auto identity = support::compute_generated_code_identity(input, error);
    EXPECT_TRUE(identity.has_value()) << error;
    return identity.has_value() ? support::generated_code_identity_hex(*identity) : error;
}

[[nodiscard]] GeneratedCodeIdentityInput vector_1() {
    GeneratedCodeIdentityInput input;
    input.variant = GeneratedCodeVariant::CppOwning;
    input.configuration = quarry::testing::kTestConfigurationIdentity;
    input.namespace_fqn = "fleet.engine";
    input.files = {
        GeneratedCodeIdentityFile{.role = GeneratedFileRole::Header, .normalized_content = "x"}};
    return input;
}

TEST(GeneratedCodeIdentityTest, Vector1PinsTheCanonicalEncoding) {
    std::string error;
    const auto encoded = support::encode_generated_code_identity_input(vector_1(), error);
    ASSERT_TRUE(encoded.has_value()) << error;
    // "QUARRY-GENERATED-CODE-ID" 00 | format 01 | variant 02 | algorithm 01 |
    // schema_id | lp("fleet.engine") | 1 file: role 01, lp("x") | 0 dependencies
    EXPECT_EQ(hex(*encoded), "5155415252592d47454e4552415445442d434f44452d4944"
                             "00"
                             "01"
                             "02"
                             "01"
                             "00112233445566778899aabbccddeeff"
                             "0000000c666c6565742e656e67696e65"
                             "00000001"
                             "01"
                             "0000000178"
                             "00000000");
    EXPECT_EQ(gid_of(vector_1()), "f035215ced8e20ef40032285252b7084");
}

TEST(GeneratedCodeIdentityTest, Vector2SortsFilesByRoleAndDependenciesByName) {
    GeneratedCodeIdentityInput input;
    input.variant = GeneratedCodeVariant::CppCStorage;
    input.configuration = support::ConfigurationIdentity{};
    input.namespace_fqn = "";
    // Deliberately out of canonical order.
    input.files = {
        GeneratedCodeIdentityFile{.role = GeneratedFileRole::Source, .normalized_content = "src"},
        GeneratedCodeIdentityFile{.role = GeneratedFileRole::Header, .normalized_content = "hdr"},
    };
    input.dependencies = {
        GeneratedCodeIdentityDependency{.namespace_fqn = "b",
                                        .variant = GeneratedCodeVariant::CppCStorage,
                                        .identity = filled(0x11U)},
        GeneratedCodeIdentityDependency{.namespace_fqn = "a",
                                        .variant = GeneratedCodeVariant::CppCStorage,
                                        .identity = filled(0x22U)},
    };
    EXPECT_EQ(gid_of(input), "e5c9bd8ca63c6e76060d86d7c748934c");

    std::swap(input.files[0], input.files[1]);
    std::swap(input.dependencies[0], input.dependencies[1]);
    EXPECT_EQ(gid_of(input), "e5c9bd8ca63c6e76060d86d7c748934c") << "input order must not matter";
}

TEST(GeneratedCodeIdentityTest, Vector3HashesPlaceholdersAsWritten) {
    GeneratedCodeIdentityInput input = vector_1();
    const std::string content = "inline namespace quarry_gen_" +
                                support::placeholder::self_identity('h') + " {} #include \"" +
                                support::placeholder::include_path("fleet.common") + "\" " +
                                support::placeholder::macro_stem("fleet.common");
    input.files = {GeneratedCodeIdentityFile{.role = GeneratedFileRole::Header,
                                             .normalized_content = content}};
    GeneratedCodeIdentity dependency;
    for (std::size_t index = 0; index < dependency.bytes.size(); ++index) {
        dependency.bytes[index] = static_cast<std::uint8_t>(index);
    }
    input.dependencies = {
        GeneratedCodeIdentityDependency{.namespace_fqn = "fleet.common",
                                        .variant = GeneratedCodeVariant::CppOwning,
                                        .identity = dependency}};
    EXPECT_EQ(gid_of(input), "a539a2e49209ef992b227e610905ca61");
}

TEST(GeneratedCodeIdentityTest, EveryInputComponentChangesTheIdentity) {
    const std::string base = gid_of(vector_1());
    GeneratedCodeIdentityInput input = vector_1();
    input.variant = GeneratedCodeVariant::CppCStorage;
    EXPECT_EQ(gid_of(input), "99317b841463b98794d799c5f9825be8");

    input = vector_1();
    input.configuration.schema_id[15] ^= 0x01U;
    EXPECT_NE(gid_of(input), base);

    input = vector_1();
    input.namespace_fqn = "fleet.engine2";
    EXPECT_NE(gid_of(input), base);

    input = vector_1();
    input.files[0].normalized_content = "y";
    EXPECT_NE(gid_of(input), base);

    input = vector_1();
    input.files.push_back(
        GeneratedCodeIdentityFile{.role = GeneratedFileRole::Source, .normalized_content = ""});
    EXPECT_NE(gid_of(input), base) << "an empty source file is still a file";

    input = vector_1();
    input.dependencies = {
        GeneratedCodeIdentityDependency{.namespace_fqn = "fleet.common",
                                        .variant = GeneratedCodeVariant::CppOwning,
                                        .identity = filled(0U)}};
    const std::string with_dependency = gid_of(input);
    EXPECT_NE(with_dependency, base);
    input.dependencies[0].identity.bytes[0] = 1U;
    EXPECT_NE(gid_of(input), with_dependency) << "dependency identities propagate";
}

TEST(GeneratedCodeIdentityTest, RejectsMalformedInputs) {
    std::string error;
    GeneratedCodeIdentityInput input = vector_1();
    input.files.push_back(input.files.front());
    EXPECT_FALSE(support::compute_generated_code_identity(input, error).has_value());
    EXPECT_NE(error.find("file role twice"), std::string::npos) << error;

    input = vector_1();
    input.dependencies = {
        GeneratedCodeIdentityDependency{.namespace_fqn = "a", .variant = {}, .identity = {}},
        GeneratedCodeIdentityDependency{.namespace_fqn = "a", .variant = {}, .identity = {}}};
    EXPECT_FALSE(support::compute_generated_code_identity(input, error).has_value());
    EXPECT_NE(error.find("dependency twice"), std::string::npos) << error;

    input = vector_1();
    input.dependencies = {GeneratedCodeIdentityDependency{
        .namespace_fqn = "fleet.engine", .variant = {}, .identity = {}}};
    EXPECT_FALSE(support::compute_generated_code_identity(input, error).has_value());
    EXPECT_NE(error.find("own namespace"), std::string::npos) << error;

    input = vector_1();
    input.files[0].normalized_content = "broken \x1ESz\x1F";
    EXPECT_FALSE(support::compute_generated_code_identity(input, error).has_value());
    EXPECT_NE(error.find("malformed identity placeholder"), std::string::npos) << error;
}

TEST(GeneratedCodeIdentityTest, PlaceholderTextValidation) {
    std::string error;
    const std::string valid = "a" + support::placeholder::self_identity('b') + "b" +
                              support::placeholder::include_path("") +
                              support::placeholder::macro_stem("x.y_z");
    EXPECT_TRUE(support::validate_placeholder_text(valid, error)) << error;
    for (const std::string& text :
         {std::string("\x1F"), std::string("\x1ESh"), std::string("\x1E\x1F"),
          std::string("\x1EQx\x1F"), std::string("\x1EPa-b\x1F"), std::string("\x1ESh2\x1F"),
          std::string("\x1EP\x1E\x1F")}) {
        EXPECT_FALSE(support::validate_placeholder_text(text, error)) << "text accepted";
    }
}

TEST(GeneratedCodeIdentityTest, SubstitutesEveryPlaceholderForm) {
    support::PlaceholderValues values;
    for (std::size_t index = 0; index < values.self.bytes.size(); ++index) {
        values.self.bytes[index] = static_cast<std::uint8_t>(0xF0U + index);
    }
    values.include_paths.emplace("fleet.common", "fleet/common.generated.hpp");
    values.include_paths.emplace("", "schema.generated.hpp");
    values.macro_stems.emplace("fleet.common", "QUARRY_GENERATED_CPP_FLEET_COMMON");
    namespace placeholder = support::placeholder;
    const std::string text =
        placeholder::self_identity('h') + "|" + placeholder::self_identity('H') + "|" +
        placeholder::self_identity('0') + "|" + placeholder::self_identity('3') + "|" +
        placeholder::self_identity('b') + "|" + placeholder::include_path("fleet.common") + "|" +
        placeholder::include_path("") + "|" + placeholder::macro_stem("fleet.common");
    std::string error;
    const auto out = support::substitute_placeholders(text, values, error);
    ASSERT_TRUE(out.has_value()) << error;
    EXPECT_EQ(*out, "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff|F0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF|"
                    "0xf0f1f2f3UL|0xfcfdfeffUL|"
                    "0xf0U, 0xf1U, 0xf2U, 0xf3U, 0xf4U, 0xf5U, 0xf6U, 0xf7U, 0xf8U, 0xf9U, 0xfaU, "
                    "0xfbU, 0xfcU, 0xfdU, 0xfeU, 0xffU|"
                    "fleet/common.generated.hpp|schema.generated.hpp|"
                    "QUARRY_GENERATED_CPP_FLEET_COMMON");

    EXPECT_FALSE(
        support::substitute_placeholders(placeholder::include_path("other"), values, error));
    EXPECT_NE(error.find("'other'"), std::string::npos) << error;
}

TEST(GeneratedCodeIdentityTest, SharedSha256MatchesFips180Vectors) {
    const std::string abc = "abc";
    const auto digest = quarry::compiler::support::sha256(
        std::span(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()));
    EXPECT_EQ(hex(std::vector<std::uint8_t>(digest.begin(), digest.end())),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

} // namespace
