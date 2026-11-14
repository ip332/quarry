/* Engine ECU: encodes one EngineStatus report with the embedded C bindings
 * generated from the truck configuration root and writes it to a file. */
#include "truck/engine.generated.h"

#include <stdint.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT.brf\n", argv[0]);
        return 2;
    }

    truck_engine_EngineStatus_t status;
    truck_engine_EngineStatus_init(&status);
    status.has_time = true;
    status.time.has_unix_ms = true;
    status.time.unix_ms = 1767225600000ULL;
    status.has_rpm = true;
    status.rpm = 1450U;
    status.has_coolant_c = true;
    status.coolant_c = 88;

    truck_engine_FaultCode_t* fault = &status.faults[0];
    truck_engine_FaultCode_init(fault);
    fault->has_time = true;
    fault->time.has_unix_ms = true;
    fault->time.unix_ms = 1767225599000ULL;
    fault->has_spn = true;
    fault->spn = 110U; /* engine coolant temperature */
    fault->has_fmi = true;
    fault->fmi = 16U;
    status.faults_count = 1U;

    uint8_t encoded[256];
    const truck_engine_EngineStatus_encode_result_t result =
        truck_engine_EngineStatus_encode(&status, encoded, sizeof(encoded));
    if (result.status != QUARRY_C_STATUS_OK) {
        fprintf(stderr, "encode failed: %d\n", (int)result.status);
        return 1;
    }

    FILE* output = fopen(argv[1], "wb");
    if (output == NULL || fwrite(encoded, 1U, result.bytes_written, output) != result.bytes_written) {
        fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    fclose(output);
    printf("EngineStatus record_id 0x%08x, %u bytes\n",
           (unsigned)(((uint32_t)encoded[4] << 24) | ((uint32_t)encoded[5] << 16) |
                      ((uint32_t)encoded[6] << 8) | (uint32_t)encoded[7]),
           (unsigned)result.bytes_written);
    return 0;
}
