/* This consumer sees only installed SDK headers and the installed C archive. */
#include <quarry/runtime_c/schema_id.h>
#include <quarry/runtime_c/generic_brf.h>

int verify_schema_identity_sdk(void) {
    quarry_schema_id_t id;
    if (quarry_qbs_schema_id(0, &id) != QUARRY_GENERIC_INVALID_ARGUMENT || id.algorithm != 0U)
        return 1;
    for (unsigned int i = 0U; i < QUARRY_SCHEMA_ID_SIZE; ++i)
        if (id.bytes[i] != 0U)
            return 2;
    id.algorithm = QUARRY_SCHEMA_ID_ALGORITHM_SHA256_128;
    return quarry_schema_id_equals(&id, id.algorithm, id.bytes) ? 0 : 3;
}
