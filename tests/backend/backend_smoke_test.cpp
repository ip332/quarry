#include "compiler/backend/backend.hpp"

#include <gtest/gtest.h>

namespace {

TEST(BackendSmokeTest, ConstructsBackend) {
    quarry::compiler::backend::Backend backend;
    (void)backend;
}

} // namespace
