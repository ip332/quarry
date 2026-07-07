#include "compiler/symbols/symbols.hpp"

#include <gtest/gtest.h>

namespace {

TEST(SymbolsSmokeTest, ConstructsNamespaceBuilder) {
    breadcrumbs::compiler::symbols::NamespaceBuilder builder;
    (void)builder;
}

} // namespace
