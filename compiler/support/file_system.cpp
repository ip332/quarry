#include "compiler/support/file_system.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace quarry::compiler::support {

FileReadResult RealFileSystem::read_text_file(std::string_view path) const {
    const std::filesystem::path input_path{std::string(path)};
    std::ifstream input{input_path};
    if (!input) {
        return {};
    }

    return FileReadResult{
        .found = true,
        .text =
            std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()),
    };
}

bool RealFileSystem::exists(std::string_view path) const {
    std::error_code error;
    return std::filesystem::exists(std::filesystem::path{std::string(path)}, error);
}

std::string RealFileSystem::normalize_path(std::string_view path) const {
    const std::filesystem::path input_path{std::string(path)};
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(input_path, error);
    if (!error) {
        return canonical.lexically_normal().string();
    }

    error.clear();
    const auto absolute = std::filesystem::absolute(input_path, error);
    if (!error) {
        return absolute.lexically_normal().string();
    }

    return input_path.lexically_normal().string();
}

} // namespace quarry::compiler::support
