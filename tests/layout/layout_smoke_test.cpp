#include "compiler/layout/layout.hpp"

#include <gtest/gtest.h>

namespace {

TEST(LayoutSmokeTest, ConstructsLayoutComputer) {
    breadcrumbs::compiler::layout::LayoutComputer computer;
    (void)computer;
}

} // namespace
