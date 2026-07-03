#include "compiler/support/file_system.hpp"

#include <fstream>
#include <iterator>

namespace breadcrumbs::compiler::support {

FileReadResult FileSystem::read_text_file(const std::string& path) const {
    std::ifstream input(path);
    if (!input) {
        return {};
    }

    return FileReadResult{
        .found = true,
        .text =
            std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()),
    };
}

} // namespace breadcrumbs::compiler::support
