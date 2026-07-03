#pragma once

#include "compiler/support/source_location.hpp"

#include <string>
#include <vector>

namespace breadcrumbs::compiler::ast {

struct SyntaxNode {
    support::SourceRange source_range;
};

struct DeclarationSyntax : SyntaxNode {
    std::string name;
};

struct Ast {
    std::vector<DeclarationSyntax> declarations;
};

}  // namespace breadcrumbs::compiler::ast
