// Namespace-to-macro encoding (#117). The decoder is the inverse used to
// demonstrate injectivity; these tests check that inverse on the adversarial
// pairs from the issue and on every combination of the identifier shapes the
// frontend accepts.

#include "compiler/support/namespace_macro.hpp"

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace support = quarry::compiler::support;

[[nodiscard]] std::string body_of(std::string_view fqn) {
    const std::optional<std::string> body = support::encode_namespace_macro(fqn);
    EXPECT_TRUE(body.has_value()) << fqn;
    return body.value_or("");
}

void expect_round_trip(std::string_view fqn) {
    const std::string body = body_of(fqn);
    const std::optional<std::string> decoded = support::decode_namespace_macro(body);
    ASSERT_TRUE(decoded.has_value()) << fqn << " -> " << body;
    EXPECT_EQ(*decoded, fqn) << body;
    EXPECT_EQ(support::encode_namespace_macro(*decoded), body);
}

void expect_composed_unreserved(std::string_view prefix, std::string_view body,
                                std::string_view suffix) {
    const std::string identifier = std::string(prefix) + std::string(body) + std::string(suffix);
    EXPECT_TRUE(support::is_unreserved_macro_identifier(identifier)) << identifier;
}

} // namespace

TEST(NamespaceMacroTest, RootIsExplicitAndOutsideTheSegmentImage) {
    EXPECT_EQ(body_of(""), "0");
    EXPECT_EQ(support::decode_namespace_macro("0"), "");
    EXPECT_EQ(support::encode_namespace_macro_segments({}), "0");
}

TEST(NamespaceMacroTest, AdversarialPairsStayDistinctAndCaseIsPreserved) {
    const std::pair<std::string_view, std::string_view> pairs[] = {
        {"a.b", "a_b"},
        {"Ab", "ab"},
        {"a.b_c", "a_b.c"},
        {"dep.engine", "dep_engine"},
        {"Dep.Engine", "DEP.ENGINE"},
        {"a.b2", "a_b2"},
        {"q", "qq"},
        {"q0", "_"},
    };
    for (const auto& [left, right] : pairs) {
        const std::string left_body = body_of(left);
        const std::string right_body = body_of(right);
        EXPECT_NE(left_body, right_body) << left << " vs " << right;
        expect_round_trip(left);
        expect_round_trip(right);
    }
    EXPECT_EQ(body_of("a.b"), "a_b");
    EXPECT_EQ(body_of("a_b"), "aq0b");
    EXPECT_EQ(body_of("Ab"), "Ab");
    EXPECT_EQ(body_of("ab"), "ab");
    EXPECT_EQ(body_of("a.b_c"), "a_bq0c");
    EXPECT_EQ(body_of("a_b.c"), "aq0b_c");
    EXPECT_EQ(body_of("dep.engine"), "dep_engine");
    EXPECT_EQ(body_of("dep_engine"), "depq0engine");
}

TEST(NamespaceMacroTest, UnderscoresDigitsAndQRoundTrip) {
    const std::string_view names[] = {
        "a",
        "A",
        "_",
        "__",
        "___",
        "a_",
        "_a",
        "a__b",
        "_a_",
        "a_b_c",
        "a.b.c",
        "a0",
        "a.b2",
        "b2.c3",
        "q",
        "qq",
        "q0",
        "q_q",
        "_q",
        "q_",
        "a.q_b",
        "truck.engine",
        "QUARRY.NS_fleet",
    };
    for (const std::string_view name : names) {
        expect_round_trip(name);
        EXPECT_EQ(body_of(name).find("__"), std::string::npos) << name;
    }
}

TEST(NamespaceMacroTest, SegmentCombinationsAreInjective) {
    const std::string_view segments[] = {"a",    "A", "b",  "_",  "__", "a_", "_a",
                                         "a__b", "q", "qq", "q0", "a0", "b2", "a_b"};
    std::set<std::string> seen_bodies;
    std::set<std::string> seen_names;
    const auto consider = [&](std::string fqn) {
        ASSERT_TRUE(seen_names.insert(fqn).second) << fqn;
        const std::string body = body_of(fqn);
        EXPECT_TRUE(seen_bodies.insert(body).second) << fqn << " -> " << body;
        expect_round_trip(fqn);
    };
    consider("");
    for (const std::string_view first : segments) {
        consider(std::string(first));
        for (const std::string_view second : segments) {
            consider(std::string(first) + "." + std::string(second));
        }
    }
}

TEST(NamespaceMacroTest, NamePrefixIsALeadingSegment) {
    const std::string_view prefixed[] = {"rev_a", "truck", "engine"};
    const std::optional<std::string> as_segments =
        support::encode_namespace_macro_segments(prefixed);
    ASSERT_TRUE(as_segments.has_value());
    EXPECT_EQ(*as_segments, body_of("rev_a.truck.engine"));
    // A prefix that itself ends in '_' must be escaped, not concatenated.
    const std::string_view underscored[] = {"rev_", "a_b"};
    const std::optional<std::string> escaped =
        support::encode_namespace_macro_segments(underscored);
    ASSERT_TRUE(escaped.has_value());
    EXPECT_EQ(*escaped, "revq0_aq0b");
    EXPECT_EQ(escaped->find("__"), std::string::npos);
    EXPECT_NE(*escaped, std::string("rev_") + body_of("a_b"));
}

TEST(NamespaceMacroTest, ComposedIdentityMacrosAreUnreserved) {
    const std::string_view prefixes[] = {
        support::kCppOwningNamespaceMacroPrefix,
        support::kCppCStorageNamespaceMacroPrefix,
        support::kCNamespaceMacroPrefix,
        support::kCNamespaceAccessorPrefix,
    };
    const std::string_view suffixes[] = {
        "_SCHEMA_ID_ALGORITHM", "_SCHEMA_ID_W0", "_SCHEMA_ID_W1",
        "_SCHEMA_ID_W2",        "_SCHEMA_ID_W3", "_CODE_ID_FORMAT",
        "_CODE_ID_W0",          "_CODE_ID_W1",   "_CODE_ID_W2",
        "_CODE_ID_W3",          "_schema_id",    "_0123456789ABCDEF0123456789ABCDEF_",
    };
    const std::string_view names[] = {"",      "truck.engine", "dep.engine", "dep_engine", "a.b_c",
                                      "a_b.c", "Ab",           "_a_",        "a__b",       "q0"};
    for (const std::string_view name : names) {
        const std::string body = body_of(name);
        for (const std::string_view prefix : prefixes) {
            for (const std::string_view suffix : suffixes) {
                expect_composed_unreserved(prefix, body, suffix);
            }
        }
    }
}

TEST(NamespaceMacroTest, RejectsNamesTheFrontendRejectsAndStringsOutsideTheImage) {
    EXPECT_FALSE(support::encode_namespace_macro(".").has_value());
    EXPECT_FALSE(support::encode_namespace_macro(".a").has_value());
    EXPECT_FALSE(support::encode_namespace_macro("a.").has_value());
    EXPECT_FALSE(support::encode_namespace_macro("a..b").has_value());
    EXPECT_FALSE(support::encode_namespace_macro("1a").has_value());
    EXPECT_FALSE(support::encode_namespace_macro("a.1b").has_value());
    EXPECT_FALSE(support::encode_namespace_macro("a b").has_value());
    EXPECT_FALSE(support::encode_namespace_macro_segments(std::vector<std::string_view>{"rev-a"})
                     .has_value());

    const std::string_view rejected[] = {"",   "00", "q",    "q1", "q_",
                                         "_a", "a_", "a__b", "0a", "a_0b"};
    for (const std::string_view encoded : rejected) {
        EXPECT_FALSE(support::decode_namespace_macro(encoded).has_value()) << encoded;
    }
    EXPECT_FALSE(support::is_unreserved_macro_identifier(""));
    EXPECT_FALSE(support::is_unreserved_macro_identifier("0"));
    EXPECT_FALSE(support::is_unreserved_macro_identifier("_A"));
    EXPECT_FALSE(support::is_unreserved_macro_identifier("A__B"));
    EXPECT_FALSE(support::is_unreserved_macro_identifier("A-B"));
    EXPECT_TRUE(support::is_unreserved_macro_identifier("QUARRY_NS_0_SCHEMA_ID_W0"));
}
