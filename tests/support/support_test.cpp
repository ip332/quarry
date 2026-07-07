#include "compiler/support/compiler_context.hpp"
#include "compiler/support/file_system.hpp"
#include "compiler/support/source_location.hpp"
#include "compiler/support/source_manager.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

using breadcrumbs::compiler::support::CompilerContext;
using breadcrumbs::compiler::support::FileReadResult;
using breadcrumbs::compiler::support::FileSystem;
using breadcrumbs::compiler::support::LineColumn;
using breadcrumbs::compiler::support::RealFileSystem;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceLocation;
using breadcrumbs::compiler::support::SourceManager;
using breadcrumbs::compiler::support::SourceRange;

class StubFileSystem final : public FileSystem {
public:
    [[nodiscard]] FileReadResult read_text_file(std::string_view path) const override {
        (void)path;
        return FileReadResult{
            .found = true,
            .text = "stub",
        };
    }

    [[nodiscard]] bool exists(std::string_view path) const override {
        (void)path;
        return true;
    }

    [[nodiscard]] std::string normalize_path(std::string_view path) const override {
        return std::string(path);
    }
};

void test_source_location() {
    const SourceLocation invalid;
    assert(!invalid.is_valid());
    assert(invalid == SourceLocation::invalid());

    const SourceFileId file_id(0);
    const SourceLocation first(file_id, 0);
    const SourceLocation also_first(file_id, 0);
    const SourceLocation second(file_id, 1);

    assert(first.is_valid());
    assert(first.file_id() == file_id);
    assert(first.byte_offset() == 0);
    assert(first == also_first);
    assert(!(first == second));
}

void test_source_range() {
    const SourceFileId file_id(0);
    const SourceRange invalid;
    const SourceRange range(SourceLocation(file_id, 2), SourceLocation(file_id, 5));
    const SourceRange empty(SourceLocation(file_id, 3), SourceLocation(file_id, 3));
    const SourceRange reversed(SourceLocation(file_id, 5), SourceLocation(file_id, 2));
    const SourceRange different_files(SourceLocation(file_id, 2),
                                      SourceLocation(SourceFileId(1), 5));

    assert(!invalid.is_valid());
    assert(invalid == SourceRange::invalid());
    assert(range.is_valid());
    assert(!range.is_empty());
    assert(empty.is_valid());
    assert(empty.is_empty());
    assert(range.contains(SourceLocation(file_id, 2)));
    assert(range.contains(SourceLocation(file_id, 4)));
    assert(!range.contains(SourceLocation(file_id, 5)));
    assert(!reversed.is_valid());
    assert(!different_files.is_valid());
}

void test_source_manager_registration_and_lookup() {
    SourceManager manager;

    const SourceFileId first = manager.add_source("/project/first.bc", "first\nsecond");
    const SourceFileId second = manager.add_source("/project/second.bc", "other");

    assert(first.is_valid());
    assert(second.is_valid());
    assert(!(first == second));
    assert(manager.contains(first));
    assert(manager.contains(second));
    assert(manager.source_text(first) == std::optional<std::string_view>("first\nsecond"));
    assert(manager.source_path(first) == std::optional<std::string_view>("/project/first.bc"));
    assert(manager.source_text(second) == std::optional<std::string_view>("other"));
    assert(manager.sources().size() == 2);
}

void test_source_manager_line_column() {
    SourceManager manager;
    const SourceFileId file_id = manager.add_source("/project/source.bc", "abc\ndef");

    assert(manager.line_column(SourceLocation(file_id, 0)) == std::optional<LineColumn>({1, 1}));
    assert(manager.line_column(SourceLocation(file_id, 2)) == std::optional<LineColumn>({1, 3}));
    assert(manager.line_column(SourceLocation(file_id, 3)) == std::optional<LineColumn>({1, 4}));
    assert(manager.line_column(SourceLocation(file_id, 4)) == std::optional<LineColumn>({2, 1}));
    assert(manager.line_column(SourceLocation(file_id, 6)) == std::optional<LineColumn>({2, 3}));
    assert(manager.line_column(SourceLocation(file_id, 7)) == std::optional<LineColumn>({2, 4}));
}

void test_source_manager_empty_and_final_line() {
    SourceManager manager;
    const SourceFileId empty = manager.add_source("/project/empty.bc", "");
    const SourceFileId no_newline = manager.add_source("/project/no-newline.bc", "last");
    const SourceFileId trailing_newline =
        manager.add_source("/project/trailing-newline.bc", "line\n");

    assert(manager.line_column(SourceLocation(empty, 0)) == std::optional<LineColumn>({1, 1}));
    assert(manager.line_column(SourceLocation(no_newline, 3)) == std::optional<LineColumn>({1, 4}));
    assert(manager.line_column(SourceLocation(no_newline, 4)) == std::optional<LineColumn>({1, 5}));
    assert(manager.line_column(SourceLocation(trailing_newline, 5)) ==
           std::optional<LineColumn>({2, 1}));
}

void test_source_manager_crlf_and_ranges() {
    SourceManager manager;
    const SourceFileId file_id = manager.add_source("/project/crlf.bc", "ab\r\ncd");
    const SourceRange first_line(SourceLocation(file_id, 0), SourceLocation(file_id, 3));
    const SourceRange second_line(SourceLocation(file_id, 4), SourceLocation(file_id, 6));

    assert(manager.line_column(SourceLocation(file_id, 0)) == std::optional<LineColumn>({1, 1}));
    assert(manager.line_column(SourceLocation(file_id, 2)) == std::optional<LineColumn>({1, 3}));
    assert(manager.line_column(SourceLocation(file_id, 3)) == std::optional<LineColumn>({1, 4}));
    assert(manager.line_column(SourceLocation(file_id, 4)) == std::optional<LineColumn>({2, 1}));
    assert(manager.source_text(first_line) == std::optional<std::string_view>("ab\r"));
    assert(manager.source_text(second_line) == std::optional<std::string_view>("cd"));
}

void test_source_manager_invalid_inputs() {
    SourceManager manager;
    const SourceFileId file_id = manager.add_source("/project/source.bc", "text");
    const SourceFileId missing(99);

    assert(!manager.contains(SourceFileId::invalid()));
    assert(!manager.contains(missing));
    assert(!manager.source_text(missing).has_value());
    assert(!manager.source_path(missing).has_value());
    assert(!manager.line_column(SourceLocation(missing, 0)).has_value());
    assert(!manager.line_column(SourceLocation(file_id, 5)).has_value());
    assert(!manager.is_valid_offset(SourceLocation(file_id, 5)));
    assert(!manager.is_valid_range(
        SourceRange(SourceLocation(file_id, 1), SourceLocation(file_id, 5))));
}

void test_real_file_system() {
    const auto directory = std::filesystem::temp_directory_path() / "breadcrumbs-support-test";
    std::filesystem::create_directories(directory);

    const auto source_file = directory / "source.bc";
    {
        std::ofstream output(source_file);
        output << "source text";
    }

    const RealFileSystem file_system;
    const FileReadResult read = file_system.read_text_file(source_file.string());
    const auto missing_file = directory / "missing.bc";
    const FileReadResult missing = file_system.read_text_file(missing_file.string());
    const std::string normalized =
        file_system.normalize_path((directory / "." / "source.bc").string());

    assert(read.found);
    assert(read.text == "source text");
    assert(!missing.found);
    assert(file_system.exists(source_file.string()));
    assert(!file_system.exists(missing_file.string()));
    assert(std::filesystem::path(normalized).is_absolute());
    assert(std::filesystem::path(normalized).filename() == "source.bc");

    std::filesystem::remove(source_file);
    std::filesystem::remove(directory);
}

void test_compiler_context() {
    CompilerContext default_context;
    const SourceFileId file_id =
        default_context.source_manager().add_source("/project/context.bc", "context");

    assert(default_context.source_manager().source_text(file_id) ==
           std::optional<std::string_view>("context"));

    CompilerContext context(std::make_unique<StubFileSystem>());
    assert(context.file_system().exists("anything"));
    assert(context.file_system().read_text_file("anything").text == "stub");
}

} // namespace

int main() try {
    test_source_location();
    test_source_range();
    test_source_manager_registration_and_lookup();
    test_source_manager_line_column();
    test_source_manager_empty_and_final_line();
    test_source_manager_crlf_and_ranges();
    test_source_manager_invalid_inputs();
    test_real_file_system();
    test_compiler_context();
    return 0;
} catch (...) {
    return 1;
}
