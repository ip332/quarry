#include "rev_a/truck.generated.hpp"
#include "rev_a/truck/engine.generated.hpp"
#include "rev_b/truck.generated.hpp"
#include "rev_b/truck/cms.generated.hpp"
#include "rev_b/truck/engine.generated.hpp"

#include <cstdint>
#include <iostream>
#include <type_traits>

int main() {
    static_assert(
        !std::is_same_v<rev_a::truck::engine::EngineStatus, rev_b::truck::engine::EngineStatus>);
    static_assert(rev_a::truck::engine::kQuarrySchemaId != rev_b::truck::engine::kQuarrySchemaId);
    rev_a::truck::common::TimestampBuilder time_a;
    rev_b::truck::common::TimestampBuilder time_b;
    rev_a::truck::engine::EngineStatusBuilder a;
    rev_b::truck::engine::EngineStatusBuilder b;
    if (!time_a.set_unix_ms(42) || !time_b.set_unix_ms(42) || !a.set_time(time_a.build()) ||
        !b.set_time(time_b.build()) || !a.set_rpm(1450) || !b.set_rpm(1450))
        return 1;
    const auto encoded_a = rev_a::truck::engine::encode_result(a.build());
    const auto encoded_b = rev_b::truck::engine::encode_result(b.build());
    if (!encoded_a || !encoded_b || *encoded_a.value != *encoded_b.value)
        return 2;
    const auto decoded_a = rev_a::truck::engine::decode_EngineStatus_result(*encoded_a.value);
    const auto decoded_b = rev_b::truck::engine::decode_EngineStatus_result(*encoded_b.value);
    if (!decoded_a || !decoded_b || !decoded_a.value->rpm() || !decoded_b.value->rpm() ||
        *decoded_a.value->rpm() != 1450 || *decoded_b.value->rpm() != 1450 ||
        !decoded_a.value->time() || *decoded_a.value->time()->unix_ms() != 42)
        return 3;
    rev_b::truck::cms::MirrorFoldBuilder mirror;
    if (!mirror.set_folded(true))
        return 4;
    const auto bytes = rev_b::truck::cms::encode_result(mirror.build());
    if (!bytes)
        return 5;
    const auto& wire = *bytes.value;
    if (wire.size() < 8 || wire[4] != std::byte{0} || wire[5] != std::byte{3} ||
        wire[6] != std::byte{0} || wire[7] != std::byte{2})
        return 6;
    const auto decoded = rev_b::truck::cms::decode_MirrorFold_result(wire);
    if (!decoded || !decoded.value->folded() || !*decoded.value->folded())
        return 7;
    const auto& engine = *encoded_a.value;
    if (engine.size() < 8 || engine[4] != std::byte{0} || engine[5] != std::byte{5} ||
        engine[6] != std::byte{0} || engine[7] != std::byte{1})
        return 8;
    std::cout << "C++: two revisions, EngineStatus 0x00050001, MirrorFold 0x00030002\n";
}
