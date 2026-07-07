#include "compiler/semantic/semantic.hpp"

#include <gtest/gtest.h>

namespace {

TEST(SemanticSmokeTest, ConstructsSemanticValidator) {
    breadcrumbs::compiler::semantic::SemanticValidator validator;
    (void)validator;
}

} // namespace
