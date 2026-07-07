#include "compiler/schema_ir/schema_ir.hpp"

#include <gtest/gtest.h>

namespace {

TEST(SchemaIrSmokeTest, ConstructsSchemaIrBuilder) {
    breadcrumbs::compiler::schema_ir::SchemaIrBuilder builder;
    (void)builder;
}

} // namespace
