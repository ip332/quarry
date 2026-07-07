#pragma once

#include <string>
#include <string_view>

namespace breadcrumbs::compiler::support {

struct FileReadResult {
    bool found = false;
    std::string text;
};

class FileSystem {
public:
    virtual ~FileSystem() = default;

    [[nodiscard]] virtual FileReadResult read_text_file(std::string_view path) const = 0;
    [[nodiscard]] virtual bool exists(std::string_view path) const = 0;
    [[nodiscard]] virtual std::string normalize_path(std::string_view path) const = 0;
};

class RealFileSystem final : public FileSystem {
public:
    [[nodiscard]] FileReadResult read_text_file(std::string_view path) const override;
    [[nodiscard]] bool exists(std::string_view path) const override;
    [[nodiscard]] std::string normalize_path(std::string_view path) const override;
};

} // namespace breadcrumbs::compiler::support
