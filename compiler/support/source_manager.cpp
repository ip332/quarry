#include "compiler/support/source_manager.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace quarry::compiler::support {

SourceFileId SourceManager::add_source(std::string path, std::string text) {
    const auto next_id = sources_.size();
    if (next_id >= std::numeric_limits<SourceFileId::ValueType>::max()) {
        return SourceFileId::invalid();
    }

    sources_.push_back(SourceFile{
        .path = std::move(path),
        .text = std::move(text),
        .line_starts = {},
    });
    sources_.back().line_starts = compute_line_starts(sources_.back().text);
    return SourceFileId(static_cast<SourceFileId::ValueType>(next_id));
}

bool SourceManager::contains(SourceFileId file_id) const { return lookup(file_id) != nullptr; }

std::optional<std::string_view> SourceManager::source_text(SourceFileId file_id) const {
    const SourceFile* file = lookup(file_id);
    if (file == nullptr) {
        return std::nullopt;
    }
    return std::string_view(file->text);
}

std::optional<std::string_view> SourceManager::source_path(SourceFileId file_id) const {
    const SourceFile* file = lookup(file_id);
    if (file == nullptr) {
        return std::nullopt;
    }
    return std::string_view(file->path);
}

std::optional<LineColumn> SourceManager::line_column(SourceLocation location) const {
    const SourceFile* file = lookup(location.file_id());
    if (file == nullptr || location.byte_offset() > file->text.size()) {
        return std::nullopt;
    }

    const auto next_line_start = std::upper_bound(file->line_starts.begin(),
                                                  file->line_starts.end(), location.byte_offset());
    const auto line_index =
        static_cast<std::size_t>(std::distance(file->line_starts.begin(), next_line_start) - 1);
    const std::size_t line_start = file->line_starts[line_index];

    return LineColumn{
        .line = line_index + 1,
        .column = location.byte_offset() - line_start + 1,
    };
}

std::optional<std::string_view> SourceManager::source_text(SourceRange range) const {
    if (!is_valid_range(range)) {
        return std::nullopt;
    }

    const SourceFile* file = lookup(range.begin().file_id());
    if (file == nullptr) {
        return std::nullopt;
    }

    return std::string_view(file->text)
        .substr(range.begin().byte_offset(),
                range.end().byte_offset() - range.begin().byte_offset());
}

bool SourceManager::is_valid_offset(SourceLocation location) const {
    const SourceFile* file = lookup(location.file_id());
    return file != nullptr && location.byte_offset() <= file->text.size();
}

bool SourceManager::is_valid_range(SourceRange range) const {
    return range.is_valid() && is_valid_offset(range.begin()) && is_valid_offset(range.end());
}

const std::vector<SourceFile>& SourceManager::sources() const { return sources_; }

const SourceFile* SourceManager::lookup(SourceFileId file_id) const {
    if (!file_id.is_valid()) {
        return nullptr;
    }

    const std::size_t index = file_id.value();
    if (index >= sources_.size()) {
        return nullptr;
    }

    return &sources_[index];
}

std::vector<std::size_t> SourceManager::compute_line_starts(std::string_view text) {
    std::vector<std::size_t> line_starts;
    line_starts.push_back(0);

    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\n') {
            line_starts.push_back(index + 1);
        }
    }

    return line_starts;
}

} // namespace quarry::compiler::support
