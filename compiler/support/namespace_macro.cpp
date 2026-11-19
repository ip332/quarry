#include "compiler/support/namespace_macro.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace quarry::compiler::support {
namespace {

[[nodiscard]] bool is_identifier_start(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_';
}

[[nodiscard]] bool is_identifier_continue(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

[[nodiscard]] bool is_identifier(std::string_view text) {
    if (text.empty() || !is_identifier_start(text.front())) {
        return false;
    }
    for (const char character : text) {
        if (!is_identifier_continue(character)) {
            return false;
        }
    }
    return true;
}

// Prefix code described in namespace_macro.hpp. The result contains no '_'.
[[nodiscard]] std::string encode_segment(std::string_view segment) {
    std::string encoded;
    encoded.reserve(segment.size());
    for (const char character : segment) {
        if (character == 'q') {
            encoded.append("qq");
        } else if (character == '_') {
            encoded.append("q0");
        } else {
            encoded.push_back(character);
        }
    }
    return encoded;
}

[[nodiscard]] std::optional<std::string> decode_segment(std::string_view encoded) {
    std::string segment;
    segment.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        const char character = encoded[index];
        if (character == 'q') {
            if (index + 1 >= encoded.size()) {
                return std::nullopt;
            }
            const char follower = encoded[++index];
            if (follower == 'q') {
                segment.push_back('q');
            } else if (follower == '0') {
                segment.push_back('_');
            } else {
                return std::nullopt;
            }
            continue;
        }
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            return std::nullopt;
        }
        segment.push_back(character);
    }
    if (!is_identifier(segment)) {
        return std::nullopt;
    }
    return segment;
}

} // namespace

std::optional<std::string>
encode_namespace_macro_segments(std::span<const std::string_view> segments) {
    if (segments.empty()) {
        return std::string("0");
    }
    std::string encoded;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (!is_identifier(segments[index])) {
            return std::nullopt;
        }
        if (index != 0) {
            encoded.push_back('_');
        }
        encoded.append(encode_segment(segments[index]));
    }
    return encoded;
}

std::optional<std::string> encode_namespace_macro(std::string_view namespace_fqn) {
    if (namespace_fqn.empty()) {
        return std::string("0");
    }
    std::vector<std::string_view> segments;
    std::size_t begin = 0;
    while (begin <= namespace_fqn.size()) {
        const std::size_t end = namespace_fqn.find('.', begin);
        const std::string_view segment = end == std::string_view::npos
                                             ? namespace_fqn.substr(begin)
                                             : namespace_fqn.substr(begin, end - begin);
        segments.push_back(segment);
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return encode_namespace_macro_segments(segments);
}

std::optional<std::string> decode_namespace_macro(std::string_view encoded) {
    if (encoded == "0") {
        return std::string();
    }
    if (encoded.empty() || encoded.front() == '_' || encoded.back() == '_' ||
        encoded.find("__") != std::string_view::npos) {
        return std::nullopt;
    }
    std::string fqn;
    std::size_t begin = 0;
    while (begin <= encoded.size()) {
        const std::size_t end = encoded.find('_', begin);
        const std::string_view piece = end == std::string_view::npos
                                           ? encoded.substr(begin)
                                           : encoded.substr(begin, end - begin);
        const std::optional<std::string> segment = decode_segment(piece);
        if (!segment.has_value()) {
            return std::nullopt;
        }
        if (!fqn.empty()) {
            fqn.push_back('.');
        }
        fqn.append(*segment);
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    // A successful decode is in the image only when it re-encodes to the input.
    // The prefix-code argument makes this equality hold for every valid body;
    // rejecting a mismatch keeps a broken encoder out of the inverse.
    const std::optional<std::string> round_trip = encode_namespace_macro(fqn);
    if (!round_trip.has_value() || *round_trip != encoded) {
        return std::nullopt;
    }
    return fqn;
}

bool is_unreserved_macro_identifier(std::string_view identifier) {
    if (identifier.empty() || std::isalpha(static_cast<unsigned char>(identifier.front())) == 0) {
        return false;
    }
    for (const char character : identifier) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '_') {
            return false;
        }
    }
    return identifier.find("__") == std::string_view::npos;
}

} // namespace quarry::compiler::support
