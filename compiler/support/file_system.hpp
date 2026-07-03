#pragma once

#include <string>

namespace breadcrumbs::compiler::support {

struct FileReadResult {
    bool found = false;
    std::string text;
};

class FileSystem {
public:
    [[nodiscard]] FileReadResult read_text_file(const std::string& path) const;
};

} // namespace breadcrumbs::compiler::support
