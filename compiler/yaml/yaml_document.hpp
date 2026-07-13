#pragma once

#include "compiler/support/source_location.hpp"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace breadcrumbs::compiler::yaml {

enum class YamlScalarKind {
    Plain,
    SingleQuoted,
    DoubleQuoted,
    Literal,
    Folded,
    Unknown,
};

struct YamlNode;
using YamlNodePtr = std::unique_ptr<YamlNode>;

struct YamlScalarNode {
    std::string value;
    YamlScalarKind kind = YamlScalarKind::Plain;
};

struct YamlSequenceNode {
    std::vector<YamlNodePtr> elements;
};

struct YamlMappingEntry {
    YamlNodePtr key;
    YamlNodePtr value;
};

struct YamlMappingNode {
    std::vector<YamlMappingEntry> entries;
};

struct YamlNode {
    using Value = std::variant<YamlScalarNode, YamlSequenceNode, YamlMappingNode>;

    support::SourceRange source_range = support::SourceRange::invalid();
    Value value = YamlScalarNode{};
};

struct YamlDocument {
    support::SourceRange source_range = support::SourceRange::invalid();
    YamlNodePtr root;
};

} // namespace breadcrumbs::compiler::yaml
