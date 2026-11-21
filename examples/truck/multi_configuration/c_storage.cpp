#include "rev_a/truck.generated.hpp"
#include "rev_a/truck/engine.generated.hpp"
#include "rev_b/truck.generated.hpp"
#include "rev_b/truck/cms.generated.hpp"
#include "rev_b/truck/engine.generated.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <type_traits>

int main() {
    using A = quarry::cxx::revq0a_truck_engine_EngineStatus;
    using B = quarry::cxx::revq0b_truck_engine_EngineStatus;
    static_assert(!std::is_same_v<A, B>);
    A a;
    B b;
    a.set_rpm(1450);
    b.set_rpm(1450);
    a.native().has_time = b.native().has_time = true;
    a.native().time.has_unix_ms = b.native().time.has_unix_ms = true;
    a.native().time.unix_ms = b.native().time.unix_ms = 42;
    std::array<std::byte, 256> wa{}, wb{};
    const auto ea = a.encode(wa);
    const auto eb = b.encode(wb);
    if (ea.status != QUARRY_C_STATUS_OK || eb.status != QUARRY_C_STATUS_OK ||
        ea.bytes_written != eb.bytes_written ||
        std::memcmp(wa.data(), wb.data(), ea.bytes_written) != 0 || wa[4] != std::byte{0} ||
        wa[5] != std::byte{5} || wa[6] != std::byte{0} || wa[7] != std::byte{1})
        return 1;
    A da;
    B db;
    if (da.decode(std::span<const std::byte>(wa.data(), ea.bytes_written)).status !=
            QUARRY_C_STATUS_OK ||
        db.decode(std::span<const std::byte>(wb.data(), eb.bytes_written)).status !=
            QUARRY_C_STATUS_OK ||
        da.native().rpm != 1450 || db.native().rpm != 1450 || da.native().time.unix_ms != 42)
        return 2;
    quarry::cxx::revq0b_truck_cms_MirrorFold mirror;
    mirror.set_folded(true);
    const auto em = mirror.encode(wb);
    if (em.status != QUARRY_C_STATUS_OK || wb[5] != std::byte{3} || wb[7] != std::byte{2})
        return 3;
    decltype(mirror) dm;
    if (dm.decode(std::span<const std::byte>(wb.data(), em.bytes_written)).status !=
            QUARRY_C_STATUS_OK ||
        !dm.native().has_folded || !dm.native().folded)
        return 4;
    std::cout << "C++/C storage: both prefixed revisions round-trip\n";
}
