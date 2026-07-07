#include "compiler/backend/backend.hpp"

#include <gtest/gtest.h>

namespace {

TEST(BackendSmokeTest, ConstructsBackend) {
    breadcrumbs::compiler::backend::Backend backend;
    (void)backend;
}

} // namespace
