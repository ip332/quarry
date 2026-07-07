#include "compiler/imports/imports.hpp"

#include <gtest/gtest.h>

namespace {

TEST(ImportsSmokeTest, ConstructsImportResolver) {
    breadcrumbs::compiler::imports::ImportResolver resolver;
    (void)resolver;
}

} // namespace
