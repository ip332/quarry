#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace breadcrumbs::compiler::support {

class SourceFileId {
public:
    using ValueType = std::uint32_t;

    constexpr SourceFileId() = default;
    explicit constexpr SourceFileId(ValueType value) : value_(value) {}

    [[nodiscard]] static constexpr SourceFileId invalid() { return SourceFileId{}; }
    [[nodiscard]] constexpr bool is_valid() const { return value_ != invalid_value; }
    [[nodiscard]] constexpr ValueType value() const { return value_; }

    friend constexpr bool operator==(SourceFileId lhs, SourceFileId rhs) = default;

private:
    static constexpr ValueType invalid_value = std::numeric_limits<ValueType>::max();

    ValueType value_ = invalid_value;
};

class SourceLocation {
public:
    constexpr SourceLocation() = default;
    constexpr SourceLocation(SourceFileId file_id, std::size_t byte_offset)
        : file_id_(file_id), byte_offset_(byte_offset) {}

    [[nodiscard]] static constexpr SourceLocation invalid() { return SourceLocation{}; }
    [[nodiscard]] constexpr bool is_valid() const { return file_id_.is_valid(); }
    [[nodiscard]] constexpr SourceFileId file_id() const { return file_id_; }
    [[nodiscard]] constexpr std::size_t byte_offset() const { return byte_offset_; }

    friend constexpr bool operator==(SourceLocation lhs, SourceLocation rhs) = default;

private:
    SourceFileId file_id_;
    std::size_t byte_offset_ = 0;
};

class SourceRange {
public:
    constexpr SourceRange() = default;
    constexpr SourceRange(SourceLocation begin, SourceLocation end) : begin_(begin), end_(end) {}

    [[nodiscard]] static constexpr SourceRange invalid() { return SourceRange{}; }
    [[nodiscard]] constexpr SourceLocation begin() const { return begin_; }
    [[nodiscard]] constexpr SourceLocation end() const { return end_; }
    [[nodiscard]] constexpr bool is_valid() const {
        return begin_.is_valid() && end_.is_valid() && begin_.file_id() == end_.file_id() &&
               begin_.byte_offset() <= end_.byte_offset();
    }
    [[nodiscard]] constexpr bool is_empty() const {
        return is_valid() && begin_.byte_offset() == end_.byte_offset();
    }
    [[nodiscard]] constexpr bool contains(SourceLocation location) const {
        return is_valid() && location.is_valid() && location.file_id() == begin_.file_id() &&
               begin_.byte_offset() <= location.byte_offset() &&
               location.byte_offset() < end_.byte_offset();
    }

    friend constexpr bool operator==(SourceRange lhs, SourceRange rhs) = default;

private:
    SourceLocation begin_;
    SourceLocation end_;
};

} // namespace breadcrumbs::compiler::support
