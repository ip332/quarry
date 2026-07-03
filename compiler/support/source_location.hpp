#pragma once

#include <cstddef>
#include <string>

namespace breadcrumbs::compiler::support {

struct SourceLocation {
    std::string file;
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t byte_offset = 0;
};

struct SourceRange {
    SourceLocation begin;
    SourceLocation end;
};

}  // namespace breadcrumbs::compiler::support
