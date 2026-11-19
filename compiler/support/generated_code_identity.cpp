#include "compiler/support/generated_code_identity.hpp"

#include "compiler/support/sha256.hpp"

#include <algorithm>
#include <set>

namespace quarry::compiler::support {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";
constexpr char kHexDigitsUpper[] = "0123456789ABCDEF";

void put_u8(std::vector<std::uint8_t>& out, std::uint8_t value) { out.push_back(value); }

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
    }
}

[[nodiscard]] bool put_lp(std::vector<std::uint8_t>& out, std::string_view bytes,
                          std::string& error) {
    if (bytes.size() > 0xFFFFFFFFU) {
        error = "generated-code identity input field exceeds 4 GiB";
        return false;
    }
    put_u32(out, static_cast<std::uint32_t>(bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end());
    return true;
}

[[nodiscard]] bool is_fqn_char(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '.';
}

[[nodiscard]] bool is_self_form(char form) {
    return form == 'h' || form == 'H' || form == 'b' || (form >= '0' && form <= '3');
}

[[nodiscard]] std::string word_literal(std::uint32_t word) {
    std::string literal = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        literal.push_back(kHexDigits[(word >> static_cast<unsigned>(shift)) & 0x0FU]);
    }
    literal += "UL";
    return literal;
}

[[nodiscard]] std::string self_value(const GeneratedCodeIdentity& identity, char form) {
    switch (form) {
    case 'h':
        return generated_code_identity_hex(identity);
    case 'H':
        return generated_code_identity_hex_upper(identity);
    case 'b': {
        std::string list;
        for (std::size_t index = 0; index < identity.bytes.size(); ++index) {
            if (index != 0U) {
                list += ", ";
            }
            list += "0x";
            list.push_back(kHexDigits[identity.bytes[index] >> 4U]);
            list.push_back(kHexDigits[identity.bytes[index] & 0x0FU]);
            list.push_back('U');
        }
        return list;
    }
    default:
        return word_literal(
            generated_code_identity_word(identity, static_cast<std::size_t>(form - '0')));
    }
}

// Visits `text`, calling `literal(chunk)` for text outside placeholders and
// `marker(kind, payload)` for each placeholder. Returns false (with `error`)
// on malformed framing; marker may also fail.
template <typename Literal, typename Marker>
[[nodiscard]] bool scan(std::string_view text, Literal&& literal, Marker&& marker,
                        std::string& error) {
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t begin = text.find_first_of(std::string_view("\x1E\x1F", 2), at);
        if (begin == std::string_view::npos) {
            literal(text.substr(at));
            return true;
        }
        if (text[begin] == placeholder::kEnd) {
            error = "generated text contains a stray placeholder terminator";
            return false;
        }
        literal(text.substr(at, begin - at));
        const std::size_t end = text.find(placeholder::kEnd, begin + 1U);
        if (end == std::string_view::npos || end == begin + 1U) {
            error = "generated text contains an unterminated placeholder";
            return false;
        }
        const char kind = text[begin + 1U];
        const std::string_view payload = text.substr(begin + 2U, end - begin - 2U);
        if (payload.find(placeholder::kBegin) != std::string_view::npos) {
            error = "generated text contains a nested placeholder";
            return false;
        }
        if (kind == 'S') {
            if (payload.size() != 1U || !is_self_form(payload.front())) {
                error = "generated text contains a malformed identity placeholder";
                return false;
            }
        } else if (kind == 'P' || kind == 'M') {
            if (!std::all_of(payload.begin(), payload.end(), is_fqn_char)) {
                error = "generated text contains a malformed namespace placeholder";
                return false;
            }
        } else {
            error = "generated text contains an unknown placeholder kind";
            return false;
        }
        if (!marker(kind, payload)) {
            return false;
        }
        at = end + 1U;
    }
    return true;
}

} // namespace

std::optional<std::vector<std::uint8_t>>
encode_generated_code_identity_input(const GeneratedCodeIdentityInput& input, std::string& error) {
    std::vector<const GeneratedCodeIdentityFile*> files;
    std::set<GeneratedFileRole> roles;
    for (const GeneratedCodeIdentityFile& file : input.files) {
        if (!roles.insert(file.role).second) {
            error = "generated-code identity input lists a file role twice";
            return std::nullopt;
        }
        if (!validate_placeholder_text(file.normalized_content, error)) {
            return std::nullopt;
        }
        files.push_back(&file);
    }
    std::sort(files.begin(), files.end(),
              [](const auto* left, const auto* right) { return left->role < right->role; });

    std::vector<const GeneratedCodeIdentityDependency*> dependencies;
    std::set<std::string_view> dependency_names;
    for (const GeneratedCodeIdentityDependency& dependency : input.dependencies) {
        if (dependency.namespace_fqn == input.namespace_fqn) {
            error = "generated-code identity input lists its own namespace as a dependency";
            return std::nullopt;
        }
        if (!dependency_names.insert(dependency.namespace_fqn).second) {
            error = "generated-code identity input lists a dependency twice";
            return std::nullopt;
        }
        dependencies.push_back(&dependency);
    }
    std::sort(dependencies.begin(), dependencies.end(), [](const auto* left, const auto* right) {
        return left->namespace_fqn < right->namespace_fqn; // byte order
    });

    std::vector<std::uint8_t> out(kGeneratedCodeIdentityDomain.begin(),
                                  kGeneratedCodeIdentityDomain.end());
    put_u8(out, kGeneratedCodeIdentityFormat);
    put_u8(out, static_cast<std::uint8_t>(input.variant));
    put_u8(out, input.configuration.algorithm);
    out.insert(out.end(), input.configuration.schema_id.begin(),
               input.configuration.schema_id.end());
    if (!put_lp(out, input.namespace_fqn, error)) {
        return std::nullopt;
    }
    put_u32(out, static_cast<std::uint32_t>(files.size()));
    for (const GeneratedCodeIdentityFile* file : files) {
        put_u8(out, static_cast<std::uint8_t>(file->role));
        if (!put_lp(out, file->normalized_content, error)) {
            return std::nullopt;
        }
    }
    put_u32(out, static_cast<std::uint32_t>(dependencies.size()));
    for (const GeneratedCodeIdentityDependency* dependency : dependencies) {
        if (!put_lp(out, dependency->namespace_fqn, error)) {
            return std::nullopt;
        }
        put_u8(out, static_cast<std::uint8_t>(dependency->variant));
        put_u8(out, dependency->identity.format);
        out.insert(out.end(), dependency->identity.bytes.begin(), dependency->identity.bytes.end());
    }
    return out;
}

std::optional<GeneratedCodeIdentity>
compute_generated_code_identity(const GeneratedCodeIdentityInput& input, std::string& error) {
    const auto encoded = encode_generated_code_identity_input(input, error);
    if (!encoded.has_value()) {
        return std::nullopt;
    }
    const std::array<std::uint8_t, 32> digest = sha256(*encoded);
    GeneratedCodeIdentity identity;
    std::copy_n(digest.begin(), identity.bytes.size(), identity.bytes.begin());
    return identity;
}

std::string generated_code_identity_hex(const GeneratedCodeIdentity& identity) {
    std::string hex;
    hex.reserve(kGeneratedCodeIdentitySize * 2U);
    for (const std::uint8_t byte : identity.bytes) {
        hex.push_back(kHexDigits[byte >> 4U]);
        hex.push_back(kHexDigits[byte & 0x0FU]);
    }
    return hex;
}

std::string generated_code_identity_hex_upper(const GeneratedCodeIdentity& identity) {
    std::string hex;
    hex.reserve(kGeneratedCodeIdentitySize * 2U);
    for (const std::uint8_t byte : identity.bytes) {
        hex.push_back(kHexDigitsUpper[byte >> 4U]);
        hex.push_back(kHexDigitsUpper[byte & 0x0FU]);
    }
    return hex;
}

std::uint32_t generated_code_identity_word(const GeneratedCodeIdentity& identity,
                                           std::size_t index) {
    std::uint32_t word = 0U;
    for (std::size_t offset = 0; offset < 4U; ++offset) {
        word = (word << 8U) | identity.bytes.at((index * 4U) + offset);
    }
    return word;
}

namespace placeholder {

std::string self_identity(char form) { return std::string{kBegin, 'S', form, kEnd}; }

std::string include_path(std::string_view namespace_fqn) {
    std::string text{kBegin, 'P'};
    text.append(namespace_fqn);
    text.push_back(kEnd);
    return text;
}

std::string macro_stem(std::string_view namespace_fqn) {
    std::string text{kBegin, 'M'};
    text.append(namespace_fqn);
    text.push_back(kEnd);
    return text;
}

} // namespace placeholder

bool validate_placeholder_text(std::string_view text, std::string& error) {
    return scan(text, [](std::string_view) {}, [](char, std::string_view) { return true; }, error);
}

std::optional<std::string> substitute_placeholders(std::string_view text,
                                                   const PlaceholderValues& values,
                                                   std::string& error) {
    std::string out;
    out.reserve(text.size());
    const bool ok = scan(
        text, [&](std::string_view chunk) { out.append(chunk); },
        [&](char kind, std::string_view payload) {
            if (kind == 'S') {
                out += self_value(values.self, payload.front());
                return true;
            }
            const auto& table = kind == 'P' ? values.include_paths : values.macro_stems;
            const auto it = table.find(payload);
            if (it == table.end()) {
                error = std::string("generated text names namespace '") + std::string(payload) +
                        "' without a known " + (kind == 'P' ? "include path" : "macro stem");
                return false;
            }
            out += it->second;
            return true;
        },
        error);
    if (!ok) {
        return std::nullopt;
    }
    return out;
}

} // namespace quarry::compiler::support
