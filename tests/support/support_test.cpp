#include "compiler/support/file_system.hpp"
#include "compiler/support/source_location.hpp"
#include "compiler/support/source_manager.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::support::FileReadResult;
using breadcrumbs::compiler::support::LineColumn;
using breadcrumbs::compiler::support::RealFileSystem;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceLocation;
using breadcrumbs::compiler::support::SourceManager;
using breadcrumbs::compiler::support::SourceRange;

TEST(SourceLocationTest, RepresentsValidAndInvalidLocations) {
    const SourceLocation invalid;
    EXPECT_FALSE(invalid.is_valid());
    EXPECT_EQ(invalid, SourceLocation::invalid());

    const SourceFileId file_id(0);
    const SourceLocation first(file_id, 0);
    const SourceLocation also_first(file_id, 0);
    const SourceLocation second(file_id, 1);

    EXPECT_TRUE(first.is_valid());
    EXPECT_EQ(first.file_id(), file_id);
    EXPECT_EQ(first.byte_offset(), 0U);
    EXPECT_EQ(first, also_first);
    EXPECT_NE(first, second);
}

TEST(SourceRangeTest, RepresentsHalfOpenRanges) {
    const SourceFileId file_id(0);
    const SourceRange invalid;
    const SourceRange range(SourceLocation(file_id, 2), SourceLocation(file_id, 5));
    const SourceRange empty(SourceLocation(file_id, 3), SourceLocation(file_id, 3));
    const SourceRange reversed(SourceLocation(file_id, 5), SourceLocation(file_id, 2));
    const SourceRange different_files(SourceLocation(file_id, 2),
                                      SourceLocation(SourceFileId(1), 5));

    EXPECT_FALSE(invalid.is_valid());
    EXPECT_EQ(invalid, SourceRange::invalid());
    EXPECT_TRUE(range.is_valid());
    EXPECT_FALSE(range.is_empty());
    EXPECT_TRUE(empty.is_valid());
    EXPECT_TRUE(empty.is_empty());
    EXPECT_TRUE(range.contains(SourceLocation(file_id, 2)));
    EXPECT_TRUE(range.contains(SourceLocation(file_id, 4)));
    EXPECT_FALSE(range.contains(SourceLocation(file_id, 5)));
    EXPECT_FALSE(reversed.is_valid());
    EXPECT_FALSE(different_files.is_valid());
}

TEST(SourceManagerTest, RegistersSourcesAndLooksThemUpByStableId) {
    SourceManager manager;

    const SourceFileId first = manager.add_source("/project/first.bc", "first\nsecond");
    const SourceFileId second = manager.add_source("/project/second.bc", "other");

    EXPECT_TRUE(first.is_valid());
    EXPECT_TRUE(second.is_valid());
    EXPECT_NE(first, second);
    EXPECT_TRUE(manager.contains(first));
    EXPECT_TRUE(manager.contains(second));
    EXPECT_EQ(manager.source_text(first), std::optional<std::string_view>("first\nsecond"));
    EXPECT_EQ(manager.source_path(first), std::optional<std::string_view>("/project/first.bc"));
    EXPECT_EQ(manager.source_text(second), std::optional<std::string_view>("other"));
    EXPECT_EQ(manager.sources().size(), 2U);
}

TEST(SourceManagerTest, DerivesOneBasedLineAndColumn) {
    SourceManager manager;
    const SourceFileId file_id = manager.add_source("/project/source.bc", "abc\ndef");

    EXPECT_EQ(manager.line_column(SourceLocation(file_id, 0)), std::optional<LineColumn>({1, 1}));
    EXPECT_EQ(manager.line_column(SourceLocation(file_id, 2)), std::optional<LineColumn>({1, 3}));
    EXPECT_EQ(manager.line_column(SourceLocation(file_id, 3)), std::optional<LineColumn>({1, 4}));
    EXPECT_EQ(manager.line_column(SourceLocation(file_id, 4)), std::optional<LineColumn>({2, 1}));
    EXPECT_EQ(manager.line_column(SourceLocation(file_id, 6)), std::optional<LineColumn>({2, 3}));
    EXPECT_EQ(manager.line_column(SourceLocation(file_id, 7)), std::optional<LineColumn>({2, 4}));
}

TEST(SourceManagerTest, HandlesEmptyFilesAndFinalLines) {
    SourceManager manager;
    const SourceFileId empty = manager.add_source("/project/empty.bc", "");
    const SourceFileId no_newline = manager.add_source("/project/no-newline.bc", "last");
    const SourceFileId trailing_newline =
        manager.add_source("/project/trailing-newline.bc", "line\n");

    EXPECT_EQ(manager.line_column(SourceLocation(empty, 0)), std::optional<LineColumn>({1, 1}));
    EXPECT_EQ(manager.line_column(SourceLocation(no_newline, 3)),
              std::optional<LineColumn>({1, 4}));
    EXPECT_EQ(manager.line_column(SourceLocation(no_newline, 4)),
              std::optional<LineColumn>({1, 5}));
    EXPECT_EQ(manager.line_column(SourceLocation(trailing_newline, 5)),
              std::optional<LineColumn>({2, 1}));
}

TEST(SourceManagerTest, HandlesCrlfAndSourceRangeSlices) {
    SourceManager manager;
    const SourceFileId file_id = manager.add_source("/project/crlf.bc", "ab\r\ncd");
    const SourceRange first_line(SourceLocation(file_id, 0), SourceLocation(file_id, 3));
    const SourceRange second_line(SourceLocation(file_id, 4), SourceLocation(file_id, 6));

    EXPECT_EQ(manager.line_column(SourceLocation(file_id, 0)), std::optional<LineColumn>({1, 1}));
    EXPECT_EQ(manager.line_column(SourceLocation(file_id, 2)), std::optional<LineColumn>({1, 3}));
    EXPECT_EQ(manager.line_column(SourceLocation(file_id, 3)), std::optional<LineColumn>({1, 4}));
    EXPECT_EQ(manager.line_column(SourceLocation(file_id, 4)), std::optional<LineColumn>({2, 1}));
    EXPECT_EQ(manager.source_text(first_line), std::optional<std::string_view>("ab\r"));
    EXPECT_EQ(manager.source_text(second_line), std::optional<std::string_view>("cd"));
}

TEST(SourceManagerTest, RejectsInvalidIdsAndOffsets) {
    SourceManager manager;
    const SourceFileId file_id = manager.add_source("/project/source.bc", "text");
    const SourceFileId missing(99);

    EXPECT_FALSE(manager.contains(SourceFileId::invalid()));
    EXPECT_FALSE(manager.contains(missing));
    EXPECT_FALSE(manager.source_text(missing).has_value());
    EXPECT_FALSE(manager.source_path(missing).has_value());
    EXPECT_FALSE(manager.line_column(SourceLocation(missing, 0)).has_value());
    EXPECT_FALSE(manager.line_column(SourceLocation(file_id, 5)).has_value());
    EXPECT_FALSE(manager.is_valid_offset(SourceLocation(file_id, 5)));
    EXPECT_FALSE(manager.is_valid_range(
        SourceRange(SourceLocation(file_id, 1), SourceLocation(file_id, 5))));
}

TEST(RealFileSystemTest, ReadsFilesAndNormalizesPaths) {
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

    EXPECT_TRUE(read.found);
    EXPECT_EQ(read.text, "source text");
    EXPECT_FALSE(missing.found);
    EXPECT_TRUE(file_system.exists(source_file.string()));
    EXPECT_FALSE(file_system.exists(missing_file.string()));
    EXPECT_TRUE(std::filesystem::path(normalized).is_absolute());
    EXPECT_EQ(std::filesystem::path(normalized).filename(), "source.bc");

    std::filesystem::remove(source_file);
    std::filesystem::remove(directory);
}

} // namespace
