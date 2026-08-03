#include "demo/c.generated.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    demo_c_Sample_t sample;
    demo_c_Sample_init(&sample);

    sample.has_count = true;
    sample.count = 42U;
    sample.has_label = true;
    memcpy(sample.label, "hello", 5U);
    sample.label_length = 5U;
    sample.has_blob = true;
    {
        const uint8_t blob[] = {0x00U, 0xFFU, 0x10U};
        memcpy(sample.blob, blob, sizeof(blob));
        sample.blob_length = sizeof(blob);
    }

    uint8_t encoded[128];
    const demo_c_Sample_encode_result_t encoded_result =
        demo_c_Sample_encode(&sample, encoded, sizeof(encoded));
    if (encoded_result.status != QUARRY_C_STATUS_OK) {
        return 1;
    }

    const demo_c_Sample_decode_result_t decoded =
        demo_c_Sample_decode(encoded, encoded_result.bytes_written);
    if (decoded.status != QUARRY_C_STATUS_OK || !decoded.value.has_count ||
        decoded.value.count != 42U || !decoded.value.has_label ||
        decoded.value.label_length != 5U || memcmp(decoded.value.label, "hello", 5U) != 0 ||
        !decoded.value.has_blob || decoded.value.blob_length != 3U) {
        return 2;
    }

    printf("decoded count: %u\n", (unsigned)decoded.value.count);
    return 0;
}
