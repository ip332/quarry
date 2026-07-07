#include "compiler/parser/parser.hpp"

#include <gtest/gtest.h>

namespace {

TEST(ParserSmokeTest, ConstructsParser) {
    breadcrumbs::compiler::parser::Parser parser;
    (void)parser;
}

} // namespace
