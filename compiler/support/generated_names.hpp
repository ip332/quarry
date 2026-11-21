#pragma once

#include "compiler/support/namespace_macro.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// Source naming only. Canonical Schema IR FQNs and identity-input keys never
// pass through this projection. Empty prefixes preserve every old spelling.
namespace quarry::compiler::support {

[[nodiscard]] inline bool valid_name_prefix(std::string_view prefix) {
    if (!is_unreserved_macro_identifier(prefix) || prefix == "quarry" || prefix == "std" ||
        prefix == "kQuarrySchemaId" || prefix == "kQuarrySchemaIdAlgorithm" ||
        prefix == "kQuarryGeneratedCodeId" || prefix == "kQuarryGeneratedCodeIdFormat" ||
        prefix.starts_with("quarry_gen_") || prefix.starts_with("quarry_cfg_")) {
        return false;
    }
    // A prefix is used literally in C++ namespaces and Python packages. Use
    // one portable spelling for all backends, rather than silently renaming it.
    static constexpr std::string_view keywords[] = {"alignas",
                                                    "alignof",
                                                    "and",
                                                    "and_eq",
                                                    "asm",
                                                    "auto",
                                                    "bitand",
                                                    "bitor",
                                                    "bool",
                                                    "break",
                                                    "case",
                                                    "catch",
                                                    "char",
                                                    "char8_t",
                                                    "char16_t",
                                                    "char32_t",
                                                    "class",
                                                    "compl",
                                                    "concept",
                                                    "const",
                                                    "consteval",
                                                    "constexpr",
                                                    "constinit",
                                                    "const_cast",
                                                    "continue",
                                                    "co_await",
                                                    "co_return",
                                                    "co_yield",
                                                    "decltype",
                                                    "default",
                                                    "delete",
                                                    "do",
                                                    "double",
                                                    "dynamic_cast",
                                                    "else",
                                                    "enum",
                                                    "explicit",
                                                    "export",
                                                    "extern",
                                                    "false",
                                                    "float",
                                                    "for",
                                                    "friend",
                                                    "goto",
                                                    "if",
                                                    "inline",
                                                    "int",
                                                    "long",
                                                    "mutable",
                                                    "namespace",
                                                    "new",
                                                    "noexcept",
                                                    "not",
                                                    "not_eq",
                                                    "nullptr",
                                                    "operator",
                                                    "or",
                                                    "or_eq",
                                                    "private",
                                                    "protected",
                                                    "public",
                                                    "register",
                                                    "reinterpret_cast",
                                                    "requires",
                                                    "restrict",
                                                    "return",
                                                    "short",
                                                    "signed",
                                                    "sizeof",
                                                    "static",
                                                    "static_assert",
                                                    "static_cast",
                                                    "struct",
                                                    "switch",
                                                    "template",
                                                    "this",
                                                    "thread_local",
                                                    "throw",
                                                    "true",
                                                    "try",
                                                    "typedef",
                                                    "typeid",
                                                    "typename",
                                                    "union",
                                                    "unsigned",
                                                    "using",
                                                    "virtual",
                                                    "void",
                                                    "volatile",
                                                    "wchar_t",
                                                    "while",
                                                    "xor",
                                                    "xor_eq",
                                                    "False",
                                                    "None",
                                                    "True",
                                                    "as",
                                                    "assert",
                                                    "async",
                                                    "await",
                                                    "def",
                                                    "del",
                                                    "elif",
                                                    "except",
                                                    "finally",
                                                    "from",
                                                    "global",
                                                    "import",
                                                    "in",
                                                    "is",
                                                    "lambda",
                                                    "nonlocal",
                                                    "pass",
                                                    "raise",
                                                    "with",
                                                    "yield",
                                                    "chan",
                                                    "defer",
                                                    "fallthrough",
                                                    "func",
                                                    "go",
                                                    "interface",
                                                    "map",
                                                    "match",
                                                    "package",
                                                    "range",
                                                    "select",
                                                    "type",
                                                    "var"};
    return std::find(std::begin(keywords), std::end(keywords), prefix) == std::end(keywords);
}

[[nodiscard]] inline bool valid_prefix_for_namespace(std::string_view prefix,
                                                     std::string_view fqn) {
    return prefix.empty() || (valid_name_prefix(prefix) && fqn.substr(0, fqn.find('.')) != prefix);
}

[[nodiscard]] inline std::string named_namespace(std::string_view prefix, std::string_view fqn) {
    if (prefix.empty()) {
        return std::string(fqn);
    }
    return std::string(prefix) + (fqn.empty() ? "" : "." + std::string(fqn));
}

[[nodiscard]] inline std::string named_path(std::string_view prefix, std::string_view path) {
    return prefix.empty() ? std::string(path) : std::string(prefix) + "/" + std::string(path);
}

[[nodiscard]] inline std::string named_macro_body(std::string_view prefix, std::string_view fqn) {
    if (prefix.empty()) {
        return encode_namespace_macro(fqn).value_or("");
    }
    std::vector<std::string_view> segments{prefix};
    while (!fqn.empty()) {
        const auto end = fqn.find('.');
        segments.push_back(fqn.substr(0, end));
        if (end == std::string_view::npos) {
            break;
        }
        fqn.remove_prefix(end + 1U);
        if (fqn.empty()) {
            return ""; // A trailing separator is not a canonical namespace.
        }
    }
    return encode_namespace_macro_segments(segments).value_or("");
}

} // namespace quarry::compiler::support
