#include "quarry/runtime_c/generic_brf.h"

#include <string.h>

quarry_generic_status_t quarry_qbs_schema_id(const quarry_qbs_view_t* qbs,
                                             quarry_schema_id_t* out) {
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (qbs == NULL || qbs->bytes == NULL || qbs->size < 40U)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    const uint8_t* bytes = qbs->bytes;
    if (bytes[8] != QUARRY_SCHEMA_ID_ALGORITHM_SHA256_128 || bytes[10] != 0U ||
        bytes[11] != QUARRY_SCHEMA_ID_SIZE)
        return QUARRY_GENERIC_MALFORMED_QBS;
    out->algorithm = bytes[8];
    memcpy(out->bytes, bytes + 12U, QUARRY_SCHEMA_ID_SIZE);
    return QUARRY_GENERIC_OK;
}
