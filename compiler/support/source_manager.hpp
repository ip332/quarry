#pragma once

#include "compiler/support/source_location.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quarry::compiler::support {

struct LineColumn {
    std::size_t line = 0;
    std::size_t column = 0;

    friend constexpr bool operator==(LineColumn lhs, LineColumn rhs) = default;
};

struct SourceFile {
    std::string path;
    std::string text;
    std::vector<std::size_t> line_starts;
};

class SourceManager {
public:
    [[nodiscard]] SourceFileId add_source(std::string path, std::string text);

    [[nodiscard]] bool contains(SourceFileId file_id) const;
    [[nodiscard]] std::optional<std::string_view> source_text(SourceFileId file_id) const;
    [[nodiscard]] std::optional<std::string_view> source_path(SourceFileId file_id) const;
    [[nodiscard]] std::optional<LineColumn> line_column(SourceLocation location) const;
    [[nodiscard]] std::optional<std::string_view> source_text(SourceRange range) const;

    [[nodiscard]] bool is_valid_offset(SourceLocation location) const;
    [[nodiscard]] bool is_valid_range(SourceRange range) const;
    [[nodiscard]] const std::vector<SourceFile>& sources() const;

private:
    [[nodiscard]] const SourceFile* lookup(SourceFileId file_id) const;
    [[nodiscard]] static std::vector<std::size_t> compute_line_starts(std::string_view text);

    std::vector<SourceFile> sources_;
};

} // namespace quarry::compiler::support
