#include "rev_a/truck.generated.h"
#include "rev_a/truck/engine.generated.h"
#include "rev_b/truck.generated.h"
#include "rev_b/truck/cms.generated.h"
#include "rev_b/truck/engine.generated.h"

#include <stdio.h>
#include <string.h>

static uint32_t record_id(const uint8_t* data) {
    return ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 8) |
           data[7];
}

int main(void) {
    revq0a_truck_engine_EngineStatus_t a;
    revq0b_truck_engine_EngineStatus_t b;
    revq0a_truck_engine_EngineStatus_init(&a);
    revq0b_truck_engine_EngineStatus_init(&b);
    a.has_rpm = b.has_rpm = true;
    a.rpm = b.rpm = 1450;
    a.has_time = b.has_time = true;
    a.time.has_unix_ms = b.time.has_unix_ms = true;
    a.time.unix_ms = b.time.unix_ms = 42;
    uint8_t bytes_a[256], bytes_b[256];
    const revq0a_truck_engine_EngineStatus_encode_result_t ea =
        revq0a_truck_engine_EngineStatus_encode(&a, bytes_a, sizeof(bytes_a));
    const revq0b_truck_engine_EngineStatus_encode_result_t eb =
        revq0b_truck_engine_EngineStatus_encode(&b, bytes_b, sizeof(bytes_b));
    if (ea.status != QUARRY_C_STATUS_OK || eb.status != QUARRY_C_STATUS_OK ||
        ea.bytes_written != eb.bytes_written || memcmp(bytes_a, bytes_b, ea.bytes_written) != 0 ||
        record_id(bytes_a) != 0x00050001U)
        return 1;
    const revq0a_truck_engine_EngineStatus_decode_result_t da =
        revq0a_truck_engine_EngineStatus_decode(bytes_a, ea.bytes_written);
    const revq0b_truck_engine_EngineStatus_decode_result_t db =
        revq0b_truck_engine_EngineStatus_decode(bytes_b, eb.bytes_written);
    if (da.status != QUARRY_C_STATUS_OK || db.status != QUARRY_C_STATUS_OK || !da.value.has_rpm ||
        !db.value.has_rpm || da.value.rpm != 1450 || db.value.rpm != 1450 ||
        !da.value.time.has_unix_ms || da.value.time.unix_ms != 42)
        return 2;
    if (memcmp(quarry_ns_revq0a_truck_engine_schema_id(), quarry_ns_revq0b_truck_engine_schema_id(),
               16) == 0)
        return 3;
    revq0b_truck_cms_MirrorFold_t mirror;
    revq0b_truck_cms_MirrorFold_init(&mirror);
    mirror.has_folded = true;
    mirror.folded = true;
    const revq0b_truck_cms_MirrorFold_encode_result_t em =
        revq0b_truck_cms_MirrorFold_encode(&mirror, bytes_b, sizeof(bytes_b));
    if (em.status != QUARRY_C_STATUS_OK || record_id(bytes_b) != 0x00030002U)
        return 4;
    const revq0b_truck_cms_MirrorFold_decode_result_t dm =
        revq0b_truck_cms_MirrorFold_decode(bytes_b, em.bytes_written);
    if (dm.status != QUARRY_C_STATUS_OK || !dm.value.has_folded || !dm.value.folded)
        return 5;
    puts("C99: two revisions, EngineStatus 0x00050001, MirrorFold 0x00030002");
    return 0;
}
