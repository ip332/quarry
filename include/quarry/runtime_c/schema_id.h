#ifndef QUARRY_RUNTIME_C_SCHEMA_ID_H_
#define QUARRY_RUNTIME_C_SCHEMA_ID_H_

#include <stdbool.h>
#include <stdint.h>

#define QUARRY_SCHEMA_ID_ALGORITHM_SHA256_128 1U
#define QUARRY_SCHEMA_ID_SIZE 16U

/* Wire configuration identity, in QBS header byte order. Not generated-code gid. */
typedef struct {
    uint8_t algorithm;
    uint8_t bytes[QUARRY_SCHEMA_ID_SIZE];
} quarry_schema_id_t;

/* Compares values only; does not compute or authenticate a schema hash.
 * A null identity or byte pointer compares unequal. */
static inline bool quarry_schema_id_equals(const quarry_schema_id_t* id, uint8_t algorithm,
                                           const uint8_t bytes[QUARRY_SCHEMA_ID_SIZE]) {
    if (!id || !bytes || id->algorithm != algorithm)
        return false;
    for (unsigned int i = 0U; i < QUARRY_SCHEMA_ID_SIZE; ++i)
        if (id->bytes[i] != bytes[i])
            return false;
    return true;
}

#endif /* QUARRY_RUNTIME_C_SCHEMA_ID_H_ */
