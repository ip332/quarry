#include "compiler/support/sha256.hpp"

#include <cstddef>
#include <vector>

namespace quarry::compiler::support {

namespace {

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U, 0x923F82A4U,
    0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU,
    0x9BDC06A7U, 0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU,
    0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU, 0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU,
    0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U, 0xA2BFE8A1U, 0xA81A664BU,
    0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U, 0x19A4C116U,
    0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U, 0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U,
    0xC67178F2U};

[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t value, unsigned amount) {
    return (value >> amount) | (value << (32U - amount));
}

} // namespace

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> padded(input.begin(), input.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(input.size()) * 8U;
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U) {
        padded.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<std::uint8_t>(bit_length >> shift));
    }

    std::array<std::uint32_t, 8> state = {0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
                                          0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};
    for (std::size_t block = 0; block < padded.size(); block += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16U; ++i) {
            const std::size_t at = block + i * 4U;
            words[i] = (static_cast<std::uint32_t>(padded[at]) << 24U) |
                       (static_cast<std::uint32_t>(padded[at + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(padded[at + 2U]) << 8U) |
                       static_cast<std::uint32_t>(padded[at + 3U]);
        }
        for (std::size_t i = 16U; i < words.size(); ++i) {
            const auto s0 =
                rotr(words[i - 15U], 7U) ^ rotr(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
            const auto s1 =
                rotr(words[i - 2U], 17U) ^ rotr(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
            words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
        }
        auto working = state;
        for (std::size_t i = 0; i < words.size(); ++i) {
            const auto s1 = rotr(working[4], 6U) ^ rotr(working[4], 11U) ^ rotr(working[4], 25U);
            const auto choose = (working[4] & working[5]) ^ ((~working[4]) & working[6]);
            const auto temp1 = working[7] + s1 + choose + kSha256RoundConstants[i] + words[i];
            const auto s0 = rotr(working[0], 2U) ^ rotr(working[0], 13U) ^ rotr(working[0], 22U);
            const auto majority =
                (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
            const auto temp2 = s0 + majority;
            working = {temp1 + temp2,      working[0], working[1], working[2],
                       working[3] + temp1, working[4], working[5], working[6]};
        }
        for (std::size_t i = 0; i < state.size(); ++i) {
            state[i] += working[i];
        }
    }

    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0; i < state.size(); ++i) {
        digest[i * 4U] = static_cast<std::uint8_t>(state[i] >> 24U);
        digest[i * 4U + 1U] = static_cast<std::uint8_t>(state[i] >> 16U);
        digest[i * 4U + 2U] = static_cast<std::uint8_t>(state[i] >> 8U);
        digest[i * 4U + 3U] = static_cast<std::uint8_t>(state[i]);
    }
    return digest;
}

} // namespace quarry::compiler::support
